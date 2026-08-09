#include "check.h"

#include <string>
#include <vector>

#include "presets.h"
#include "rules.h"

namespace {

constexpr wchar_t kRulesKey[] = L"Software\\ImeModePersistence\\TestPresetRules";
constexpr wchar_t kSeededKey[] = L"Software\\ImeModePersistence\\TestSeeded";

constexpr LANGID kEnglish = 0x0409;
constexpr LANGID kJapanese = 0x0411;

void wipe() {
    RegDeleteTreeW(HKEY_CURRENT_USER, kRulesKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kSeededKey);
}

std::wstring scratch_marker() {
    wchar_t base[MAX_PATH]{};
    const DWORD written = GetTempPathW(ARRAYSIZE(base), base);
    if (written == 0 || written >= ARRAYSIZE(base)) {
        return {};
    }
    std::wstring path(base, written);
    path += L"ImeModePreset-";
    path += std::to_wstring(GetCurrentProcessId());
    path += L".txt";
    return path;
}

bool write_utf8(const std::wstring& path, const std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD wrote = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
                              &wrote, nullptr) != FALSE;
    CloseHandle(file);
    return ok;
}

LANGID rule_for(const std::wstring& key) {
    for (const rules::Rule& rule : rules::load()) {
        if (rule.executable == key) {
            return rule.language;
        }
    }
    return 0;
}

void parses_the_documented_format() {
    const std::vector<presets::Preset> got = presets::parse(
        "class:stingray_window=0409\n"
        "notepad.exe=0x0411\n");

    CHECK_MSG(got.size() == 2, "expected 2 presets, got %zu", got.size());
    if (got.size() == 2) {
        CHECK(got[0].key == "class:stingray_window");
        CHECK(got[0].language == kEnglish);
        // The 0x prefix is accepted, so a hand-edit in either form works.
        CHECK(got[1].key == "notepad.exe");
        CHECK(got[1].language == kJapanese);
    }
}

void ignores_comments_blanks_and_whitespace() {
    const std::vector<presets::Preset> got = presets::parse(
        "; a comment\n"
        "# another\n"
        "\n"
        "   \n"
        "  class:stingray_window = 0409  \r\n"     // surrounding space, CRLF
        "class:other=0411");                        // no trailing newline

    CHECK_MSG(got.size() == 2, "expected 2 presets, got %zu", got.size());
    if (got.size() == 2) {
        CHECK(got[0].key == "class:stingray_window");
        CHECK(got[0].language == kEnglish);
        CHECK(got[1].key == "class:other");
    }
}

void skips_malformed_lines_without_losing_the_rest() {
    const std::vector<presets::Preset> got = presets::parse(
        "no-equals-sign\n"
        "=0409\n"                 // empty key
        "empty-value=\n"          // empty value
        "zero=0000\n"             // a zero LANGID is "no rule", not a rule
        "toobig=10409\n"          // beyond 0xFFFF
        "nothex=english\n"        // not a number
        "trailing=0409x\n"        // hex with junk after it
        "good=0409");             // the one survivor

    CHECK_MSG(got.size() == 1, "expected 1 valid preset, got %zu", got.size());
    if (got.size() == 1) {
        CHECK(got[0].key == "good");
        CHECK(got[0].language == kEnglish);
    }
}

void empty_input_is_empty() {
    CHECK(presets::parse("").empty());
    CHECK(presets::parse("; only a comment\n").empty());
}

void seeds_once_then_leaves_the_user_alone() {
    wipe();
    const std::wstring marker = scratch_marker();
    if (marker.empty() || !write_utf8(marker, "class:stingray_window=0409\n")) {
        check::skip("could not write a scratch marker file");
        return;
    }

    // First run: the rule the installer offered appears.
    presets::seed(marker);
    CHECK_MSG(rule_for(L"class:stingray_window") == kEnglish,
              "the preset rule was not seeded");

    // The user changes their mind and removes it.
    CHECK(rules::clear(L"class:stingray_window"));
    CHECK(rule_for(L"class:stingray_window") == 0);

    // A later start must not resurrect it: the offer was already made.
    presets::seed(marker);
    CHECK_MSG(rule_for(L"class:stingray_window") == 0,
              "a deleted preset rule came back on the next start");

    DeleteFileW(marker.c_str());
    wipe();
}

void never_overwrites_a_rule_the_user_set() {
    wipe();
    const std::wstring marker = scratch_marker();
    if (marker.empty() || !write_utf8(marker, "class:stingray_window=0409\n")) {
        check::skip("could not write a scratch marker file");
        return;
    }

    // The user already bound this class to something else before ever seeing the
    // preset. The preset is a starting point, not an override.
    CHECK(rules::set(L"class:stingray_window", kJapanese));

    presets::seed(marker);
    CHECK_MSG(rule_for(L"class:stingray_window") == kJapanese,
              "the preset overwrote the user's own rule");

    DeleteFileW(marker.c_str());
    wipe();
}

void a_missing_marker_is_harmless() {
    wipe();
    // No file at this path. Nothing seeded, no crash.
    presets::seed(scratch_marker());
    CHECK(rules::load().empty());
    wipe();
}

} // namespace

void run_presets_tests() {
    // Pure-function cases first: no state to redirect.
    parses_the_documented_format();
    ignores_comments_blanks_and_whitespace();
    skips_malformed_lines_without_losing_the_rest();
    empty_input_is_empty();

    // The seeding cases write real rules and flags, so redirect both away from
    // the keys the utility uses -- as the rules suite does -- and refuse to run
    // if the redirect did not take.
    rules::set_storage_key(kRulesKey);
    presets::set_seeded_key(kSeededKey);
    if (rules::storage_key() != kRulesKey) {
        return;
    }

    seeds_once_then_leaves_the_user_alone();
    never_overwrites_a_rule_the_user_set();
    a_missing_marker_is_harmless();

    wipe();
}
