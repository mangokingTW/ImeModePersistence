#include "check.h"

#include <string>
#include <vector>

#include "diagnostic.h"

namespace {

std::wstring scratch_folder() {
    wchar_t base[MAX_PATH]{};
    const DWORD written = GetTempPathW(ARRAYSIZE(base), base);
    if (written == 0 || written >= ARRAYSIZE(base)) {
        return {};
    }

    // Named after the process rather than randomly, so two runs cannot collide
    // and a leftover directory is identifiable.
    std::wstring folder(base, written);
    folder += L"ImeModePersistenceTests-";
    folder += std::to_wstring(GetCurrentProcessId());
    return folder;
}

void remove_folder(const std::wstring& folder) {
    if (folder.empty()) {
        return;
    }
    DeleteFileW((folder + L"\\log.txt").c_str());
    DeleteFileW((folder + L"\\log.txt.old").c_str());
    RemoveDirectoryW(folder.c_str());
}

// Read back as bytes, not text. The log is UTF-8 with a BOM, and the point of
// several of these checks is what the bytes are -- a run through a text decoder
// would hide exactly the encoding faults worth catching.
std::string contents(const std::wstring& file) {
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::string result;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        result.append(buffer, read);
    }

    CloseHandle(handle);
    return result;
}

bool exists(const std::wstring& file) {
    return GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES;
}

LONGLONG size_of(const std::wstring& file) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info)) {
        return -1;
    }
    return (static_cast<LONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}

void writes_utf8_with_a_bom(const std::wstring& folder) {
    diag::Options options;
    options.folder = folder;
    CHECK(diag::initialise(options));
    CHECK(!diag::path().empty());

    // Traditional Chinese, because this is the exact thing that shipped broken
    // twice: a log full of CP1252 mojibake is worse than no log, since it reads as
    // a working feature. These are the bytes for 繁體中文.
    diag::write(L"layout: want %s", L"繁體中文");
    diag::write(L"a plain line");

    const std::string text = contents(diag::path());
    CHECK(text.size() > 3);

    const bool bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                     static_cast<unsigned char>(text[1]) == 0xBB &&
                     static_cast<unsigned char>(text[2]) == 0xBF;
    const auto byte_at = [&text](size_t index) -> unsigned {
        return index < text.size() ? static_cast<unsigned char>(text[index]) : 0u;
    };
    CHECK_MSG(bom, "no UTF-8 BOM; first bytes were 0x%02X 0x%02X 0x%02X",
              byte_at(0), byte_at(1), byte_at(2));

    CHECK(text.find("a plain line") != std::string::npos);
    CHECK_MSG(text.find("\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87") != std::string::npos,
              "Chinese was not written as UTF-8; the log holds %zu bytes", text.size());

    // Every line is timestamped and terminated, so a reader can tell one event
    // from the next and a truncated final line is visible as such.
    CHECK(text.find("\r\n") != std::string::npos);
    CHECK(text.find("layout: want") != std::string::npos);

    diag::shutdown();
}

void write_once_removes_repeats(const std::wstring& folder) {
    diag::Options options;
    options.folder = folder;
    CHECK(diag::initialise(options));

    // The observer runs twenty times a second against the same few applications.
    // Without this the file is nothing but the same context line.
    for (int i = 0; i < 50; ++i) {
        diag::write_once(L"context: notepad.exe | rule none");
    }

    // A line differing in any detail is a different situation and is kept.
    diag::write_once(L"context: notepad.exe | rule English");

    // write, by contrast, records every occurrence: what was attempted and
    // whether it worked has to be countable.
    diag::write(L"layout: attempt");
    diag::write(L"layout: attempt");

    const std::string text = contents(diag::path());

    const auto count = [&text](const char* needle) {
        size_t total = 0;
        for (size_t at = text.find(needle); at != std::string::npos;
             at = text.find(needle, at + 1)) {
            ++total;
        }
        return total;
    };

    CHECK_MSG(count("rule none") == 1, "expected 1 deduplicated line, got %zu",
              count("rule none"));
    CHECK_MSG(count("rule English") == 1, "expected the differing line, got %zu",
              count("rule English"));
    CHECK_MSG(count("layout: attempt") == 2, "write must not deduplicate, got %zu",
              count("layout: attempt"));

    diag::shutdown();
}

void shutdown_clears_the_deduplication_set(const std::wstring& folder) {
    diag::Options options;
    options.folder = folder;

    CHECK(diag::initialise(options));
    diag::write_once(L"context: seen before");
    diag::shutdown();

    DeleteFileW((folder + L"\\log.txt").c_str());

    // A second run of the utility must produce a complete log. If the set
    // survived, every context line from the previous session would be missing
    // from this one -- and the absence would be indistinguishable from the
    // situation never having arisen.
    CHECK(diag::initialise(options));
    diag::write_once(L"context: seen before");
    const std::string text = contents(diag::path());
    CHECK_MSG(text.find("seen before") != std::string::npos,
              "a reopened log dropped the line, %zu bytes written", text.size());

    diag::shutdown();
}

void rotates_at_the_limit(const std::wstring& folder) {
    const std::wstring log = folder + L"\\log.txt";
    const std::wstring old = folder + L"\\log.txt.old";
    DeleteFileW(log.c_str());
    DeleteFileW(old.c_str());

    diag::Options options;
    options.folder = folder;

    // Small enough to reach in a few dozen lines. The shipped limit is 1 MB; what
    // is being tested is that the limit is enforced on every write rather than
    // only at startup, which is what makes it hold for a copy running for weeks.
    options.maxBytes = 4096;
    CHECK(diag::initialise(options));

    for (int i = 0; i < 200; ++i) {
        diag::write(L"layout: want English, have Chinese, attempt %d via focus window", i);
    }

    CHECK_MSG(exists(old), "no rotated file; the limit was not enforced while running");

    const LONGLONG live = size_of(log);
    CHECK_MSG(live >= 0 && live < options.maxBytes,
              "current log is %lld bytes against a %lld limit", live, options.maxBytes);

    const std::string text = contents(log);

    // The first line cannot still be here. 200 lines against a 4 KB limit is
    // several rotations over, so its absence proves the live file is a new
    // generation rather than the original one being appended to forever.
    //
    // Deliberately not asserting that the *last* line is in the live file: whether
    // it is depends on where the threshold happens to fall relative to the final
    // write, which makes it a property of the message length rather than of the
    // rotation.
    CHECK_MSG(text.find("attempt 0 via") == std::string::npos,
              "the earliest line is still in the live log, %zu bytes", text.size());

    // The contract the user asked for: bounded, not merely rotated. One live file
    // under the limit plus one rotated copy, and nothing else accumulating --
    // roughly 8 KB here where 17 KB was written.
    const LONGLONG kept = live + size_of(old);
    CHECK_MSG(kept > 0 && kept < 2 * options.maxBytes + 4096,
              "%lld bytes retained against a %lld limit", kept, options.maxBytes);

    // And the rotation must not leave the new file unreadable.
    const bool bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF;
    CHECK(bom);

    diag::shutdown();
}

void rotates_an_oversized_log_on_open(const std::wstring& folder) {
    const std::wstring log = folder + L"\\log.txt";
    const std::wstring old = folder + L"\\log.txt.old";
    DeleteFileW(log.c_str());
    DeleteFileW(old.c_str());

    // Planted at a known size rather than grown, so the threshold being crossed is
    // not a matter of where the previous test happened to stop.
    const std::string filler(4096, 'x');
    HANDLE handle = CreateFileW(log.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        check::skip("could not plant an oversized log");
        return;
    }
    DWORD written = 0;
    WriteFile(handle, filler.data(), static_cast<DWORD>(filler.size()), &written, nullptr);
    CloseHandle(handle);
    CHECK(size_of(log) == 4096);

    // The startup path rather than the running one: a log left oversized by a
    // previous session must be moved aside before the new session appends to it.
    diag::Options options;
    options.folder = folder;
    options.maxBytes = 1024;
    CHECK(diag::initialise(options));

    const LONGLONG live = size_of(log);
    CHECK_MSG(live >= 0 && live < 1024,
              "an oversized log was not rotated on open, %lld bytes remain", live);
    CHECK_MSG(size_of(old) == 4096, "the previous log was not preserved, %lld bytes",
              size_of(old));

    diag::shutdown();
}

void writing_after_shutdown_is_harmless() {
    // Not a crash test for its own sake: initialise is allowed to fail -- a full
    // disk, a redirected profile -- and the utility carries on without a log. Every
    // write in the codebase would then be hitting this path.
    diag::shutdown();
    diag::write(L"nobody is listening");
    diag::write_once(L"nor to this");
    CHECK(diag::path().empty());
}

} // namespace

void run_diagnostic_tests() {
    const std::wstring folder = scratch_folder();
    if (folder.empty()) {
        check::skip("no writable temporary directory");
        return;
    }

    writes_utf8_with_a_bom(folder);
    write_once_removes_repeats(folder);
    shutdown_clears_the_deduplication_set(folder);
    rotates_at_the_limit(folder);
    rotates_an_oversized_log_on_open(folder);
    writing_after_shutdown_is_harmless();

    remove_folder(folder);
}
