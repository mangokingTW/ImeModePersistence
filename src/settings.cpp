#include "settings.h"

namespace settings {
namespace {

constexpr wchar_t kKey[] = L"Software\\ImeModePersistence";
constexpr wchar_t kPersistMode[] = L"PersistModeAcrossWindows";
constexpr wchar_t kIndicator[] = L"ShowCaretIndicator";
constexpr wchar_t kUiLanguage[] = L"UiLanguage";

bool read_flag(const wchar_t* name, bool fallback) {
    DWORD value = fallback ? 1 : 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD,
                     nullptr, &value, &bytes) != ERROR_SUCCESS) {
        return fallback;
    }
    return value != 0;
}

bool write_flag(const wchar_t* name, bool enabled) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD value = enabled ? 1 : 0;
    const LSTATUS status = RegSetValueExW(
        key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

int read_dword(const wchar_t* name, int fallback) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD,
                     nullptr, &value, &bytes) != ERROR_SUCCESS) {
        return fallback;
    }
    return static_cast<int>(value);
}

bool write_dword(const wchar_t* name, int number) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD value = static_cast<DWORD>(number);
    const LSTATUS status = RegSetValueExW(
        key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

} // namespace

bool persist_mode() {
    // Absent means never configured, and the default is the behaviour the utility
    // exists for.
    return read_flag(kPersistMode, true);
}

bool set_persist_mode(bool enabled) {
    return write_flag(kPersistMode, enabled);
}

bool indicator_enabled() {
    return read_flag(kIndicator, false);
}

bool set_indicator_enabled(bool enabled) {
    return write_flag(kIndicator, enabled);
}

int ui_language() {
    // Absent means never configured: follow the Windows display language.
    return read_dword(kUiLanguage, 0);
}

bool set_ui_language(int language) {
    return write_dword(kUiLanguage, language);
}

} // namespace settings
