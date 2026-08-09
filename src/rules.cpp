#include "rules.h"

namespace rules {
namespace {

constexpr wchar_t kRulesKey[] = L"Software\\ImeModePersistence\\Rules";

std::wstring lower(std::wstring text) {
    if (!text.empty()) {
        // CharLowerBuffW rather than towlower per character: it follows the
        // user's locale, which matters for non-ASCII executable names.
        CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    }
    return text;
}

std::wstring file_name(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

} // namespace

std::vector<Rule> load() {
    std::vector<Rule> result;

    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRulesKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return result;
    }

    for (DWORD index = 0;; ++index) {
        wchar_t name[MAX_PATH]{};
        DWORD nameChars = ARRAYSIZE(name);
        DWORD type = 0;
        DWORD value = 0;
        DWORD bytes = sizeof(value);

        const LSTATUS status = RegEnumValueW(
            key, index, name, &nameChars, nullptr, &type,
            reinterpret_cast<BYTE*>(&value), &bytes);

        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS) {
            // A single unreadable value should not hide the rest.
            continue;
        }
        if (type != REG_DWORD || value == 0 || value > 0xFFFF) {
            continue;
        }

        Rule rule;
        rule.executable.assign(name, nameChars);
        rule.language = static_cast<LANGID>(value);
        result.push_back(rule);
    }

    RegCloseKey(key);
    return result;
}

bool set(const std::wstring& executable, LANGID language) {
    if (executable.empty() || language == 0) {
        return false;
    }

    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRulesKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD value = language;
    const LSTATUS status = RegSetValueExW(
        key, lower(executable).c_str(), 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);

    return status == ERROR_SUCCESS;
}

bool clear(const std::wstring& executable) {
    if (executable.empty()) {
        return false;
    }
    const LSTATUS status =
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRulesKey, lower(executable).c_str());
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

LANGID lookup(const std::wstring& executable) {
    if (executable.empty()) {
        return 0;
    }

    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRulesKey, lower(executable).c_str(),
                     RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS) {
        return 0;
    }
    return (value != 0 && value <= 0xFFFF) ? static_cast<LANGID>(value) : 0;
}

std::wstring executable_of(HWND hwnd) {
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid) || pid == 0) {
        return {};
    }

    // PROCESS_QUERY_LIMITED_INFORMATION is the least this needs and, unlike
    // PROCESS_QUERY_INFORMATION, is granted across integrity levels.
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    DWORD chars = ARRAYSIZE(path);
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &chars);
    CloseHandle(process);

    if (!ok) {
        return {};
    }
    return lower(file_name(std::wstring(path, chars)));
}

} // namespace rules
