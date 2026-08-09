#include "diagnostic.h"

#include <shlobj.h>
#include <strsafe.h>

#include <cstdarg>
#include <set>

namespace diag {
namespace {

// One rotation is enough. A tray application that runs for months would otherwise
// grow without bound, and nobody reads a log older than the previous session.
LONGLONG g_maxBytes = 1024 * 1024;

HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_path;

// Tracked rather than queried: the size is needed on every write, and asking the
// filesystem each time would cost more than the write itself.
LONGLONG g_bytes = 0;

std::wstring directory(const std::wstring& preferred) {
    if (!preferred.empty()) {
        CreateDirectoryW(preferred.c_str(), nullptr);
        return preferred;
    }

    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        return {};
    }

    std::wstring result(base);
    CoTaskMemFree(base);

    // Not the install directory: Program Files is not writable by an unelevated
    // copy, and LocalAppData resolves to the same user whether elevated or not, so
    // both write to one file.
    result += L"\\ImeModePersistence";
    CreateDirectoryW(result.c_str(), nullptr);
    return result;
}

void rotate_if_large(const std::wstring& file) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info)) {
        return;
    }

    const LONGLONG size =
        (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size < g_maxBytes) {
        return;
    }

    const std::wstring previous = file + L".old";
    DeleteFileW(previous.c_str());
    MoveFileW(file.c_str(), previous.c_str());
}

// Repetition, not volume, is what fills this file: the same handful of
// applications are switched between all day. Context lines are therefore written
// once per distinct line, which keeps every fact worth diagnosing while removing
// the thousands of identical repeats.
std::set<std::wstring> g_seen;

// Opens the file, rotating first if the existing one is already oversized, and
// records the starting size so writes can rotate without querying the filesystem.
bool open_log(const std::wstring& file) {
    rotate_if_large(file);

    // FILE_SHARE_READ so the file can be opened in Notepad while the utility runs,
    // which is the whole point of having it.
    g_file = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    g_bytes = GetFileSizeEx(g_file, &size) ? size.QuadPart : 0;

    // A BOM on a new file, so Notepad reads the UTF-8 rather than guessing: paths
    // and window class names are not always ASCII.
    if (g_bytes == 0) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        if (WriteFile(g_file, bom, sizeof(bom), &written, nullptr)) {
            g_bytes += written;
        }
    }

    return true;
}

// Timestamps, converts and appends one already-formatted message, rotating when
// the file has grown past the limit. Rotating here rather than only at startup is
// what makes the limit hold for a copy that runs for weeks without restarting.
void emit(const wchar_t* message) {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t line[1280]{};
    StringCchPrintfW(line, ARRAYSIZE(line), L"%04u-%02u-%02u %02u:%02u:%02u.%03u  %s\r\n",
                     now.wYear, now.wMonth, now.wDay,
                     now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                     message);

    // Converted here rather than writing UTF-16, so the file opens correctly in
    // anything the user is likely to have to hand.
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return;
    }

    std::string utf8(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytes, nullptr, nullptr);

    DWORD written = 0;
    if (WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr)) {
        g_bytes += written;
    }

    // Flushed every line: the volume is low, and a log that loses its last entries
    // is worthless for diagnosing a crash or a hang. Everything here runs on the UI
    // thread, so there is nothing to lock.
    FlushFileBuffers(g_file);

    if (g_bytes < g_maxBytes || g_path.empty()) {
        return;
    }

    const std::wstring file = g_path;
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;

    // The new file has none of the context lines, so allow them to be written
    // again; otherwise a rotation would leave the log without them.
    g_seen.clear();
    open_log(file);
}

} // namespace

bool initialise(const Options& options) {
    g_maxBytes = options.maxBytes > 0 ? options.maxBytes : 1024 * 1024;

    const std::wstring folder = directory(options.folder);
    if (folder.empty()) {
        return false;
    }

    const std::wstring file = folder + L"\\log.txt";
    if (!open_log(file)) {
        return false;
    }

    g_path = file;
    return true;
}

void shutdown() {
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_path.clear();

    // So that reopening a log starts with a clean slate rather than silently
    // suppressing every context line written before the previous shutdown.
    g_seen.clear();
}

void write(const wchar_t* format, ...) {
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    wchar_t message[1024]{};
    va_list arguments;
    va_start(arguments, format);
    StringCchVPrintfW(message, ARRAYSIZE(message), format, arguments);
    va_end(arguments);

    emit(message);
}

void write_once(const wchar_t* format, ...) {
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    wchar_t message[1024]{};
    va_list arguments;
    va_start(arguments, format);
    StringCchVPrintfW(message, ARRAYSIZE(message), format, arguments);
    va_end(arguments);

    // The whole formatted message is the key, so a line that differs in any detail
    // -- a different mode, a rule that now matches -- is still recorded.
    if (!g_seen.insert(message).second) {
        return;
    }

    emit(message);
}

std::wstring path() {
    return g_path;
}

} // namespace diag
