#include "rules.h"

namespace rules {
namespace {

std::wstring g_key = L"Software\\ImeModePersistence\\Rules";

std::wstring lower(std::wstring text) {
    if (!text.empty()) {
        // CharLowerBuffW rather than towlower per character: it follows the
        // user's locale, which matters for non-ASCII paths.
        CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    }
    return text;
}

LANGID read(const std::wstring& key) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, g_key.c_str(), key.c_str(),
                     RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS) {
        return 0;
    }
    return (value != 0 && value <= 0xFFFF) ? static_cast<LANGID>(value) : 0;
}

} // namespace

const wchar_t* const kClassPrefix = L"class:";
const wchar_t* const kGlobPrefix = L"glob:";
const wchar_t* const kClassGlobPrefix = L"class-glob:";

bool matches_glob(const std::wstring& pattern, const std::wstring& text) {
    // Iterative wildcard match with backtracking on '*'. Linear in practice and,
    // unlike std::regex, has no catastrophic-backtracking failure mode, so a
    // hand-written or pasted pattern can never hang the focus-change path.
    const wchar_t* p = pattern.c_str();
    const wchar_t* s = text.c_str();
    const wchar_t* star = nullptr;   // last '*' in the pattern, for backtracking
    const wchar_t* resume = nullptr; // where in the text to resume after it
    while (*s) {
        if (*p == L'?' || *p == *s) {
            ++p;
            ++s;
        } else if (*p == L'*') {
            star = p++;
            resume = s;
        } else if (star) {
            // The run matched by '*' was too short; let it swallow one more.
            p = star + 1;
            s = ++resume;
        } else {
            return false;
        }
    }
    while (*p == L'*') {
        ++p;
    }
    return *p == L'\0';
}

// First matching glob under the given prefix, most specific (longest pattern)
// first. Ties break by ordinal comparison so the answer never depends on the
// order the registry happens to enumerate values in.
LANGID best_glob(const std::vector<Rule>& all, const std::wstring& prefix,
                 const std::wstring& subject) {
    if (subject.empty()) {
        return 0;
    }
    const size_t plen = prefix.size();
    LANGID result = 0;
    std::wstring bestPattern;
    for (const Rule& rule : all) {
        if (rule.executable.size() <= plen ||
            rule.executable.compare(0, plen, prefix) != 0) {
            continue;
        }
        const std::wstring pattern = rule.executable.substr(plen);
        if (!matches_glob(pattern, subject)) {
            continue;
        }
        if (result == 0 || pattern.size() > bestPattern.size() ||
            (pattern.size() == bestPattern.size() && pattern < bestPattern)) {
            bestPattern = pattern;
            result = rule.language;
        }
    }
    return result;
}

const std::wstring& storage_key() {
    return g_key;
}

void set_storage_key(const std::wstring& subkey) {
    if (!subkey.empty()) {
        g_key = subkey;
    }
}

std::wstring window_class_of(HWND hwnd) {
    wchar_t name[256]{};
    const int written = GetClassNameW(hwnd, name, ARRAYSIZE(name));
    return written > 0 ? lower(std::wstring(name, static_cast<size_t>(written))) : std::wstring{};
}

std::wstring file_name_of(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::vector<Rule> load() {
    std::vector<Rule> result;

    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, g_key.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return result;
    }

    for (DWORD index = 0;; ++index) {
        // Registry value names allow far more than MAX_PATH, and a rule keyed by
        // a long path has to survive being listed.
        wchar_t name[1024]{};
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
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_key.c_str(), 0, nullptr, 0,
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
        RegDeleteKeyValueW(HKEY_CURRENT_USER, g_key.c_str(), lower(executable).c_str());
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

LANGID lookup(const std::wstring& path, const std::wstring& windowClass) {
    const std::wstring loweredPath = lower(path);
    const std::wstring loweredClass = lower(windowClass);

    // Most specific first, so a rule naming one particular copy of an application
    // wins over one naming its file name, which in turn wins over a class rule
    // that could match several unrelated windows.
    if (!loweredPath.empty()) {
        if (const LANGID byPath = read(loweredPath); byPath != 0) {
            return byPath;
        }

        const std::wstring name = file_name_of(loweredPath);
        if (name != loweredPath) {
            if (const LANGID byName = read(name); byName != 0) {
                return byName;
            }
        }
    }

    if (!loweredClass.empty()) {
        if (const LANGID byClass = read(kClassPrefix + loweredClass); byClass != 0) {
            return byClass;
        }
    }

    // No literal rule matched. Wildcard rules are the fallback: they need the
    // whole set loaded and scanned, which is affordable only because lookup runs
    // on a focus change, never on the fast tooltip poll. Path globs are more
    // specific than class globs, so try them first.
    if (loweredPath.empty() && loweredClass.empty()) {
        return 0;
    }
    const std::vector<Rule> all = load();
    if (const LANGID byPathGlob = best_glob(all, kGlobPrefix, loweredPath); byPathGlob != 0) {
        return byPathGlob;
    }
    return best_glob(all, kClassGlobPrefix, loweredClass);
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

    wchar_t path[1024]{};
    DWORD chars = ARRAYSIZE(path);
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &chars);
    CloseHandle(process);

    if (!ok) {
        return {};
    }
    return lower(std::wstring(path, chars));
}

} // namespace rules
