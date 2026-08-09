#include "check.h"

#include <algorithm>
#include <string>

#include "rules.h"

namespace {

// Not the key the utility uses. These tests write real rules -- there is no way
// to prove the lookup precedence without them -- and against the real key that
// would silently destroy the bindings of whoever ran the suite.
constexpr wchar_t kTestKey[] = L"Software\\ImeModePersistence\\TestRules";

constexpr LANGID kEnglish = 0x0409;
constexpr LANGID kChinese = 0x0404;
constexpr LANGID kJapanese = 0x0411;

void wipe() {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
}

void path_splitting() {
    CHECK(rules::file_name_of(L"C:\\Windows\\notepad.exe") == L"notepad.exe");

    // Forward slashes reach this from a rule typed by hand or pasted from a
    // config file, and file_name_of is what decides whether a key counts as a
    // path at all.
    CHECK(rules::file_name_of(L"C:/Windows/notepad.exe") == L"notepad.exe");
    CHECK(rules::file_name_of(L"C:\\Program Files\\A B\\c d.exe") == L"c d.exe");

    // No separator: the input is already a bare name and must survive unchanged
    // rather than being emptied.
    CHECK(rules::file_name_of(L"notepad.exe") == L"notepad.exe");
    CHECK(rules::file_name_of(L"") == L"");

    // A trailing separator has no name after it. The caller compares the result
    // against the whole key to decide whether to try a name lookup, so an empty
    // answer here is correct and must not be a copy of the input.
    CHECK(rules::file_name_of(L"C:\\Windows\\") == L"");
}

void rejects_unusable_input() {
    // Both would otherwise create a rule that can never match, or a zero LANGID
    // that lookup reports as "no rule" -- indistinguishable from absence.
    CHECK(!rules::set(L"", kEnglish));
    CHECK(!rules::set(L"notepad.exe", 0));

    CHECK(!rules::clear(L""));
}

void full_path_rules_match_one_copy() {
    wipe();

    CHECK(rules::set(L"C:\\tools\\a\\thing.exe", kEnglish));
    CHECK(rules::lookup(L"c:\\tools\\a\\thing.exe", L"") == kEnglish);

    // The point of a path rule: another copy of the same executable elsewhere is
    // a different application as far as the rule is concerned.
    CHECK(rules::lookup(L"c:\\tools\\b\\thing.exe", L"") == 0);

    wipe();
}

void bare_name_rules_match_anywhere() {
    wipe();

    CHECK(rules::set(L"thing.exe", kChinese));
    CHECK(rules::lookup(L"c:\\tools\\a\\thing.exe", L"") == kChinese);
    CHECK(rules::lookup(L"d:\\elsewhere\\thing.exe", L"") == kChinese);

    // A name rule must not match a different executable that merely contains it.
    CHECK(rules::lookup(L"c:\\tools\\other-thing.exe", L"") == 0);

    wipe();
}

void class_rules_apply_when_the_path_is_unreadable() {
    wipe();

    CHECK(rules::set(std::wstring(rules::kClassPrefix) + L"stingray_window", kEnglish));

    // The case this exists for: an anti-cheat-protected process refuses
    // OpenProcess, so executable_of returns nothing and the class is all there is.
    CHECK(rules::lookup(L"", L"stingray_window") == kEnglish);

    // And the class must not be consulted under its bare name, or a rule for a
    // window class would collide with one for an executable.
    CHECK(rules::lookup(L"stingray_window", L"") == 0);

    wipe();
}

void lookup_goes_from_most_specific_to_least() {
    wipe();

    // All three forms set at once, each to a different language, so the order is
    // read off the answer rather than inferred.
    CHECK(rules::set(L"C:\\tools\\a\\thing.exe", kEnglish));
    CHECK(rules::set(L"thing.exe", kChinese));
    CHECK(rules::set(std::wstring(rules::kClassPrefix) + L"ThingClass", kJapanese));

    const LANGID all = rules::lookup(L"c:\\tools\\a\\thing.exe", L"ThingClass");
    CHECK_MSG(all == kEnglish, "path rule should win, got 0x%04X",
              static_cast<unsigned>(all));

    // With the path rule gone, the name rule takes over -- not the class rule.
    CHECK(rules::clear(L"C:\\tools\\a\\thing.exe"));
    const LANGID byName = rules::lookup(L"c:\\tools\\a\\thing.exe", L"ThingClass");
    CHECK_MSG(byName == kChinese, "name rule should win over class, got 0x%04X",
              static_cast<unsigned>(byName));

    // Only with neither does the class rule apply.
    CHECK(rules::clear(L"thing.exe"));
    const LANGID byClass = rules::lookup(L"c:\\tools\\a\\thing.exe", L"ThingClass");
    CHECK_MSG(byClass == kJapanese, "class rule should apply last, got 0x%04X",
              static_cast<unsigned>(byClass));

    wipe();
}

void keys_are_case_insensitive() {
    wipe();

    // Windows paths are case-insensitive, and the two sides of a comparison come
    // from different places: the rule is typed or picked by the user, the lookup
    // key comes from QueryFullProcessImageNameW. They will not agree on case.
    CHECK(rules::set(L"C:\\Tools\\A\\Thing.EXE", kEnglish));
    CHECK(rules::lookup(L"c:\\tools\\a\\thing.exe", L"") == kEnglish);

    CHECK(rules::set(std::wstring(rules::kClassPrefix) + L"Stingray_Window", kChinese));
    CHECK(rules::lookup(L"", L"STINGRAY_WINDOW") == kChinese);

    wipe();
}

void clearing_is_idempotent() {
    wipe();

    CHECK(rules::set(L"thing.exe", kEnglish));
    CHECK(rules::clear(L"thing.exe"));
    CHECK(rules::lookup(L"c:\\a\\thing.exe", L"") == 0);

    // Removing a rule that is already absent is the state the caller asked for,
    // so it reports success rather than an error the dialog would have to explain.
    CHECK(rules::clear(L"thing.exe"));
    CHECK(rules::clear(L"never-existed.exe"));

    wipe();
}

void load_lists_what_was_set() {
    wipe();

    CHECK(rules::load().empty());

    CHECK(rules::set(L"one.exe", kEnglish));
    CHECK(rules::set(L"C:\\two\\two.exe", kChinese));
    CHECK(rules::set(std::wstring(rules::kClassPrefix) + L"three", kJapanese));

    const std::vector<rules::Rule> loaded = rules::load();
    CHECK_MSG(loaded.size() == 3, "expected 3 rules, got %zu", loaded.size());

    const auto has = [&loaded](const std::wstring& key, LANGID language) {
        return std::find_if(loaded.begin(), loaded.end(), [&](const rules::Rule& rule) {
                   return rule.executable == key && rule.language == language;
               }) != loaded.end();
    };

    // Stored lower-cased, because that is the form lookup compares against.
    CHECK(has(L"one.exe", kEnglish));
    CHECK(has(L"c:\\two\\two.exe", kChinese));
    CHECK(has(L"class:three", kJapanese));

    wipe();
}

void garbage_in_the_registry_is_ignored() {
    wipe();

    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kTestKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        check::skip("could not create the scratch registry key");
        return;
    }

    // A string where a DWORD belongs, and a DWORD too large to be a LANGID. Both
    // are reachable by hand-editing, and neither should take the list with it.
    const wchar_t text[] = L"not a langid";
    RegSetValueExW(key, L"bad-type.exe", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(text), sizeof(text));

    const DWORD tooLarge = 0x00010409;
    RegSetValueExW(key, L"bad-range.exe", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&tooLarge), sizeof(tooLarge));

    const DWORD zero = 0;
    RegSetValueExW(key, L"zero.exe", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&zero), sizeof(zero));
    RegCloseKey(key);

    CHECK(rules::set(L"good.exe", kEnglish));

    const std::vector<rules::Rule> loaded = rules::load();
    CHECK_MSG(loaded.size() == 1, "expected only the valid rule, got %zu", loaded.size());
    if (loaded.size() == 1) {
        CHECK(loaded[0].executable == L"good.exe");
    }

    CHECK(rules::lookup(L"c:\\a\\bad-type.exe", L"") == 0);
    CHECK(rules::lookup(L"c:\\a\\bad-range.exe", L"") == 0);
    CHECK(rules::lookup(L"c:\\a\\zero.exe", L"") == 0);

    wipe();
}

} // namespace

void run_rules_tests() {
    rules::set_storage_key(kTestKey);
    CHECK(rules::storage_key() == kTestKey);

    // Every test below writes to the registry. If the override did not take, they
    // would be writing to the user's own rules, so stop rather than proceed.
    if (rules::storage_key() != kTestKey) {
        return;
    }

    path_splitting();
    rejects_unusable_input();
    full_path_rules_match_one_copy();
    bare_name_rules_match_anywhere();
    class_rules_apply_when_the_path_is_unreadable();
    lookup_goes_from_most_specific_to_least();
    keys_are_case_insensitive();
    clearing_is_idempotent();
    load_lists_what_was_set();
    garbage_in_the_registry_is_ignored();

    wipe();
}
