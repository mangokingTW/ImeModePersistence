#pragma once

// LANGID is the one Windows type this header exposes; the parser below is pure
// and byte-based so the fuzz harness can build it on Linux, where windows.h does
// not exist. LANGID is a WORD -- an unsigned 16-bit value.
#ifdef _WIN32
#include <windows.h>
#else
typedef unsigned short LANGID;
#endif

#include <string>
#include <vector>

// A declarative way to ship a starting rule with the installer without the
// installer writing the rule.
//
// Rules live in HKCU, and the administrator installer runs elevated -- under
// over-the-shoulder elevation that is a different account's hive from the user
// who logs in, the same trap the logon task's /RU had. So the installer only
// drops a marker file next to the executable, and the utility -- which runs as
// the actual user (the logon task and the Run key both start it as that user) --
// reads the marker once and writes the rule to the correct hive.
namespace presets {

struct Preset {
    // UTF-8, exactly as it appears in the marker: a full path, a file name, or
    // "class:<name>". Widened to a wide string only when handed to the registry.
    std::string key;
    LANGID language{};
};

// Parses the marker's raw UTF-8 bytes: one "key=langid" per line, the langid in
// hex (an optional 0x is accepted). Blank lines and lines beginning with ';' or
// '#' are ignored, and a malformed line is skipped rather than failing the rest.
// Keys are taken verbatim and must already be in the lower-case form rules
// stores, because the marker is authored by the installer, not the user.
//
// Byte-based and free of platform types, so it compiles and behaves identically
// everywhere -- which is what lets the fuzz harness exercise the exact
// production logic rather than an approximation of it.
std::vector<Preset> parse(const std::string& text);

// Applies each preset the current user has not already been offered: adds the
// rule when the user has none for that key, then records the offer -- so a rule
// the user later deletes does not come back on the next start, and a rule the
// user already set is never overwritten. A missing marker file is a no-op.
void seed(const std::wstring& markerPath);

// The HKCU subkey the per-user "already offered" flags live under. Overridable
// so a test does not write to the real one.
void set_seeded_key(const std::wstring& subkey);

} // namespace presets
