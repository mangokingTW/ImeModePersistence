#include "presets.h"

#include <cstdlib>
#include <string>

// rules.h and the registry are Windows-only; the pure parser below builds
// everywhere so the fuzz harness can exercise it on Linux.
#ifdef _WIN32
#include "rules.h"
#endif

namespace presets {
namespace {

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
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
LANGID parse_langid(const std::string& value) {
    std::string digits = value;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits = digits.substr(2);
    }
    if (digits.empty()) {
        return 0;
    }

    char* stop = nullptr;
    const unsigned long parsed = std::strtoul(digits.c_str(), &stop, 16);
    if (stop == nullptr || *stop != '\0' || parsed == 0 || parsed > 0xFFFF) {
        return 0;
    }
    return static_cast<LANGID>(parsed);
}

#ifdef _WIN32
std::wstring g_seededKey = L"Software\\ImeModePersistence\\SeededPresets";

// Rule keys are compared and stored as wide strings; the marker is UTF-8, so
// widening happens here, at the boundary, rather than in the parser.
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), length);
    return wide;
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

// The raw bytes of the marker, with a leading UTF-8 BOM tolerated. Empty on any
// failure, which seed reads as "nothing to do". Handed to parse verbatim.
std::string read_file(const std::wstring& path) {
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
    return bytes;
}
#endif  // _WIN32

} // namespace

std::vector<Preset> parse(const std::string& text) {
    std::vector<Preset> result;

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t newline = text.find('\n', pos);
        const std::string line =
            trim(text.substr(pos, newline == std::string::npos ? std::string::npos
                                                              : newline - pos));
        pos = newline == std::string::npos ? text.size() + 1 : newline + 1;

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, equals));
        const LANGID language = parse_langid(trim(line.substr(equals + 1)));
        if (key.empty() || language == 0) {
            continue;
        }

        result.push_back({key, language});
    }

    return result;
}

#ifdef _WIN32
void seed(const std::wstring& markerPath) {
    const std::string text = read_file(markerPath);
    if (text.empty()) {
        return;
    }

    for (const Preset& preset : parse(text)) {
        const std::wstring key = widen(preset.key);
        if (key.empty() || already_offered(key)) {
            continue;
        }
        // Only when the user has no rule of their own for this key: the preset is
        // a starting point, not an override.
        if (!rule_exists(key)) {
            rules::set(key, preset.language);
        }
        // Recorded even when a rule already existed, so the offer is made once and
        // a rule the user later removes is not resurrected.
        mark_offered(key);
    }
}

void set_seeded_key(const std::wstring& subkey) {
    if (!subkey.empty()) {
        g_seededKey = subkey;
    }
}
#endif  // _WIN32

} // namespace presets
