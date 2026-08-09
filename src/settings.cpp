#include "settings.h"

namespace settings {
namespace {

constexpr wchar_t kKey[] = L"Software\\ImeModePersistence";
constexpr wchar_t kPersistMode[] = L"PersistModeAcrossWindows";

} // namespace

bool persist_mode() {
    DWORD value = 1;
    DWORD bytes = sizeof(value);

    if (RegGetValueW(HKEY_CURRENT_USER, kKey, kPersistMode, RRF_RT_REG_DWORD,
                     nullptr, &value, &bytes) != ERROR_SUCCESS) {
        // Absent means never configured, and the default is the behaviour the
        // utility exists for.
        return true;
    }
    return value != 0;
}

bool set_persist_mode(bool enabled) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD value = enabled ? 1 : 0;
    const LSTATUS status = RegSetValueExW(
        key, kPersistMode, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);

    return status == ERROR_SUCCESS;
}

} // namespace settings
