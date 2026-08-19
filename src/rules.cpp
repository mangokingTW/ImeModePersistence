#include "rules.h"

#include <algorithm>
#include <cwchar>

namespace rules {
namespace {

std::wstring g_key = L"Software\\ImeModePersistence\\Rules";

// The stored DWORD is the LANGID in its low 16 bits; this bit, above them,
// carries Rule::applyOnce. A value written before the flag existed has it clear,
// so an older rule reads back as "keep enforcing".
constexpr DWORD kApplyOnceFlag = 0x10000u;

// The 1-based order sits in the bits above the flag; 0 (an older rule, or one the
// user never dragged) means "unplaced", sorted by the old specificity precedence.
constexpr int kOrderShift = 17;
constexpr DWORD kOrderMask = 0x7FFFu;

// Reserved value name for the catch-all default. The leading \x01 is a control
// character no real pattern (a path, class or glob) can start with, so it never
// collides with a rule; load() skips any name beginning with it.
constexpr wchar_t kDefaultName[] = L"\x01""default";

std::wstring lower(std::wstring text) {
    if (!text.empty()) {
        // CharLowerBuffW rather than towlower per character: it follows the
        // user's locale, which matters for non-ASCII paths.
        CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    }
    return text;
}

Match read(const std::wstring& key) {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, g_key.c_str(), key.c_str(),
                     RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    const LANGID language = static_cast<LANGID>(value & 0xFFFF);
    if (language == 0) {
        return {};
    }
    return {language, (value & kApplyOnceFlag) != 0};
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

// Specificity rank for an unplaced (order 0) rule; higher is tried first, exactly
// reproducing the old precedence: exact path > exact name > exact class > longer
// path-glob > longer class-glob.
long long specificity_rank(const Rule& rule) {
    auto starts = [&](const wchar_t* prefix) {
        const size_t n = wcslen(prefix);
        return rule.executable.size() >= n && rule.executable.compare(0, n, prefix) == 0;
    };
    const long long length = static_cast<long long>(rule.executable.size());
    if (starts(kClassGlobPrefix)) return 1000000 + length;
    if (starts(kGlobPrefix))      return 2000000 + length;
    if (starts(kClassPrefix))     return 3000000;
    if (rule.executable.find_first_of(L"\\/") != std::wstring::npos) return 5000000;
    return 4000000;  // bare file name
}

// Whether a rule's pattern matches this window's (already lower-cased) identity.
bool rule_matches(const Rule& rule, const std::wstring& loweredPath,
                  const std::wstring& loweredName, const std::wstring& loweredClass) {
    auto starts = [&](const wchar_t* prefix) {
        const size_t n = wcslen(prefix);
        return rule.executable.size() >= n && rule.executable.compare(0, n, prefix) == 0;
    };
    if (starts(kClassGlobPrefix)) {
        return !loweredClass.empty() &&
               matches_glob(rule.executable.substr(wcslen(kClassGlobPrefix)), loweredClass);
    }
    if (starts(kGlobPrefix)) {
        return !loweredPath.empty() &&
               matches_glob(rule.executable.substr(wcslen(kGlobPrefix)), loweredPath);
    }
    if (starts(kClassPrefix)) {
        return !loweredClass.empty() &&
               rule.executable == std::wstring(kClassPrefix) + loweredClass;
    }
    if (rule.executable.find_first_of(L"\\/") != std::wstring::npos) {
        return !loweredPath.empty() && rule.executable == loweredPath;
    }
    return !loweredName.empty() && rule.executable == loweredName;
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
        // The reserved catch-all default is stored here but is not a rule.
        if (nameChars > 0 && name[0] == L'\x01') {
            continue;
        }
        const LANGID language = static_cast<LANGID>(value & 0xFFFF);
        if (type != REG_DWORD || language == 0) {
            continue;
        }

        Rule rule;
        rule.executable.assign(name, nameChars);
        rule.language = language;
        rule.applyOnce = (value & kApplyOnceFlag) != 0;
        rule.order = (static_cast<unsigned>(value) >> kOrderShift) & kOrderMask;
        result.push_back(rule);
    }

    RegCloseKey(key);
    return result;
}

std::vector<Rule> load_ordered() {
    std::vector<Rule> ordered = load();
    std::stable_sort(ordered.begin(), ordered.end(), [](const Rule& a, const Rule& b) {
        const bool aPlaced = a.order != 0;
        const bool bPlaced = b.order != 0;
        if (aPlaced != bPlaced) {
            return aPlaced;  // hand-placed rules first
        }
        if (aPlaced) {
            return a.order < b.order;  // in the position the user gave them
        }
        const long long ra = specificity_rank(a);
        const long long rb = specificity_rank(b);
        if (ra != rb) {
            return ra > rb;  // more specific first, reproducing the old precedence
        }
        return a.executable < b.executable;  // stable ordinal tiebreak
    });
    return ordered;
}

bool set(const std::wstring& executable, LANGID language, bool applyOnce, unsigned order) {
    if (executable.empty() || language == 0) {
        return false;
    }

    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_key.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD value = static_cast<DWORD>(language) |
                        (applyOnce ? kApplyOnceFlag : 0u) |
                        ((static_cast<DWORD>(order) & kOrderMask) << kOrderShift);
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

bool reorder(const std::vector<std::wstring>& executablesInOrder) {
    // Re-read each rule's language / apply-once so only its position changes.
    const std::vector<Rule> current = load();
    bool ok = true;
    for (size_t i = 0; i < executablesInOrder.size(); ++i) {
        const std::wstring lowered = lower(executablesInOrder[i]);
        for (const Rule& rule : current) {
            if (rule.executable == lowered) {
                ok = set(rule.executable, rule.language, rule.applyOnce,
                         static_cast<unsigned>(i + 1)) && ok;
                break;
            }
        }
    }
    return ok;
}

Default default_binding() {
    const Match match = read(kDefaultName);
    return {match.language != 0, match.language, match.applyOnce};
}

bool set_default(bool enabled, LANGID language, bool applyOnce) {
    if (!enabled || language == 0) {
        const LSTATUS status =
            RegDeleteKeyValueW(HKEY_CURRENT_USER, g_key.c_str(), kDefaultName);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_key.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD value = static_cast<DWORD>(language) | (applyOnce ? kApplyOnceFlag : 0u);
    const LSTATUS status = RegSetValueExW(
        key, kDefaultName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

Match lookup(const std::wstring& path, const std::wstring& windowClass) {
    const std::wstring loweredPath = lower(path);
    const std::wstring loweredClass = lower(windowClass);
    const std::wstring loweredName = file_name_of(loweredPath);

    // The first rule that matches, in the list's order, wins. Loading and sorting
    // the whole set is affordable because lookup runs on a focus change, never on
    // the fast tooltip poll.
    for (const Rule& rule : load_ordered()) {
        if (rule_matches(rule, loweredPath, loweredName, loweredClass)) {
            return {rule.language, rule.applyOnce};
        }
    }

    // Nothing matched: the catch-all default is the last resort, if enabled.
    const Default fallback = default_binding();
    if (fallback.enabled) {
        return {fallback.language, fallback.applyOnce};
    }
    return {};
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
