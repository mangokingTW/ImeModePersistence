#include "presets.h"

#include <cwchar>
#include <string>

#include "rules.h"

namespace presets {
namespace {

std::wstring g_seededKey = L"Software\\ImeModePersistence\\SeededPresets";

std::wstring trim(const std::wstring& text) {
    size_t begin = 0;
    size_t end = text.size();
    const auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r'; };
    while (begin < end && space(text[begin])) {
        ++begin;
    }
    while (end > begin && space(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// Hex, with an optional 0x. Zero, out-of-range, and anything with trailing
// non-hex characters return 0, which the caller treats as "skip this line".
LANGID parse_langid(const std::wstring& value) {
    std::wstring digits = value;
    if (digits.size() > 2 && digits[0] == L'0' && (digits[1] == L'x' || digits[1] == L'X')) {
        digits = digits.substr(2);
    }
    if (digits.empty()) {
        return 0;
    }

    wchar_t* stop = nullptr;
    const unsigned long parsed = std::wcstoul(digits.c_str(), &stop, 16);
    if (stop == nullptr || *stop != L'\0' || parsed == 0 || parsed > 0xFFFF) {
        return 0;
    }
    return static_cast<LANGID>(parsed);
}

bool already_offered(const std::wstring& key) {
    return RegGetValueW(HKEY_CURRENT_USER, g_seededKey.c_str(), key.c_str(),
                        RRF_RT_REG_DWORD, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void mark_offered(const std::wstring& key) {
    HKEY handle{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_seededKey.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &handle, nullptr) != ERROR_SUCCESS) {
        return;
    }
    const DWORD one = 1;
    RegSetValueExW(handle, key.c_str(), 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&one), sizeof(one));
    RegCloseKey(handle);
}

bool rule_exists(const std::wstring& key) {
    for (const rules::Rule& rule : rules::load()) {
        if (rule.executable == key) {
            return true;
        }
    }
    return false;
}

// UTF-8, because that is what the installer writes and what a hand-edit is most
// likely to be. A leading BOM is tolerated. Empty on any failure, which seed
// reads as "nothing to do".
std::wstring read_file(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::string bytes;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        bytes.append(buffer, read);
    }
    CloseHandle(file);

    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
                                           static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
                        text.data(), length);
    return text;
}

} // namespace

std::vector<Preset> parse(const std::wstring& text) {
    std::vector<Preset> result;

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t newline = text.find(L'\n', pos);
        const std::wstring line =
            trim(text.substr(pos, newline == std::wstring::npos ? std::wstring::npos
                                                                : newline - pos));
        pos = newline == std::wstring::npos ? text.size() + 1 : newline + 1;

        if (line.empty() || line[0] == L';' || line[0] == L'#') {
            continue;
        }

        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }

        const std::wstring key = trim(line.substr(0, equals));
        const LANGID language = parse_langid(trim(line.substr(equals + 1)));
        if (key.empty() || language == 0) {
            continue;
        }

        result.push_back({key, language});
    }

    return result;
}

void seed(const std::wstring& markerPath) {
    const std::wstring text = read_file(markerPath);
    if (text.empty()) {
        return;
    }

    for (const Preset& preset : parse(text)) {
        if (already_offered(preset.key)) {
            continue;
        }
        // Only when the user has no rule of their own for this key: the preset is
        // a starting point, not an override.
        if (!rule_exists(preset.key)) {
            rules::set(preset.key, preset.language);
        }
        // Recorded even when a rule already existed, so the offer is made once and
        // a rule the user later removes is not resurrected.
        mark_offered(preset.key);
    }
}

void set_seeded_key(const std::wstring& subkey) {
    if (!subkey.empty()) {
        g_seededKey = subkey;
    }
}

} // namespace presets
