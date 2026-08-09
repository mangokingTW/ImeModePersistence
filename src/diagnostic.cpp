#include "diagnostic.h"

#include <shlobj.h>
#include <strsafe.h>

#include <cstdarg>

namespace diag {
namespace {

// One rotation is enough. A tray application that runs for months would otherwise
// grow without bound, and nobody reads a log older than the previous session.
constexpr LONGLONG kMaxBytes = 1024 * 1024;

HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_path;

std::wstring directory() {
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
    if (size < kMaxBytes) {
        return;
    }

    const std::wstring previous = file + L".old";
    DeleteFileW(previous.c_str());
    MoveFileW(file.c_str(), previous.c_str());
}

} // namespace

bool initialise() {
    const std::wstring folder = directory();
    if (folder.empty()) {
        return false;
    }

    const std::wstring file = folder + L"\\log.txt";
    rotate_if_large(file);

    // FILE_SHARE_READ so the file can be opened in Notepad while the utility runs,
    // which is the whole point of having it.
    g_file = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
        return false;
    }

    g_path = file;

    // A BOM on a new file, so Notepad reads the UTF-8 rather than guessing: paths
    // and window class names are not always ASCII.
    LARGE_INTEGER size{};
    if (GetFileSizeEx(g_file, &size) && size.QuadPart == 0) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        WriteFile(g_file, bom, sizeof(bom), &written, nullptr);
    }

    return true;
}

void shutdown() {
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_path.clear();
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
    WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);

    // Flushed every line: the volume is low because only state changes are logged,
    // and a log that loses its last entries is worthless for diagnosing a crash or
    // a hang. Everything here runs on the UI thread, so there is nothing to lock.
    FlushFileBuffers(g_file);
}

std::wstring path() {
    return g_path;
}

} // namespace diag
