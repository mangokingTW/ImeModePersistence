#include "autostart.h"

#include <string>

namespace autostart {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"ImeModePersistence";

// GetModuleFileNameW truncates instead of failing when the buffer is too small,
// so grow until the path fits rather than assuming MAX_PATH.
std::wstring module_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (written == 0) {
            return {};
        }
        if (written < path.size()) {
            path.resize(written);
            return path;
        }
        if (path.size() >= 32768) {
            return {};
        }
        path.resize(path.size() * 2);
    }
}

// Quoted so a path containing spaces survives the shell's command-line parsing.
std::wstring launch_command() {
    const std::wstring path = module_path();
    if (path.empty()) {
        return {};
    }
    return L'"' + path + L'"';
}

std::wstring read_value() {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                     nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return {};
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                     nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }

    // RegGetValueW reports the size including the terminating null.
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

} // namespace

bool elevated() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, size, &size);
    CloseHandle(token);

    return ok && elevation.TokenIsElevated != 0;
}

bool is_enabled() {
    const std::wstring expected = launch_command();
    if (expected.empty()) {
        return false;
    }

    const std::wstring actual = read_value();
    if (actual.empty()) {
        return false;
    }

    return CompareStringOrdinal(
               actual.c_str(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool set_enabled(bool enable) {
    if (!enable) {
        const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    const std::wstring command = launch_command();
    if (command.empty()) {
        return false;
    }

    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(
        key,
        kValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        bytes);
    RegCloseKey(key);

    return status == ERROR_SUCCESS;
}

} // namespace autostart
