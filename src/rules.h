#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Per-application layout bindings, kept in HKCU next to the autostart entry.
//
// A rule is keyed by one of several things, and which one is readable off the
// key itself: a key starting with "class:" is a window class, "glob:" a wildcard
// matched against the full path, "class-glob:" a wildcard matched against the
// window class; of the rest, a key containing a path separator is a full path
// and anything else is a bare executable file name. Lookup goes from most
// specific to least -- exact path, exact name, exact class, then the globs -- so
// one particular copy of an application can be told apart from another sharing
// its file name, and a literal rule always wins over a pattern.
//
// Window class rules exist because reading an executable path needs OpenProcess,
// which anti-cheat-protected games refuse even to an administrator. GetClassName
// reads a window property and needs no access to the process at all, so it is the
// only way to identify such an application without touching it.
//
// Glob rules use * (any run of characters, including path separators) and ?
// (one character), for applications whose path carries a version or install
// location that varies -- "glob:*\\game.exe" -- or whose window class has a
// generated suffix -- "class-glob:chrome_widgetwin_*". They are tested only
// after every exact rule has missed, and never on the fast tooltip path, so the
// linear scan they require costs nothing a user can feel.
namespace rules {

struct Rule {
    std::wstring executable;   // lower-cased full path, or bare file name
    LANGID language{};
};

// Subkey of HKCU the rules are stored under, and a way to point it elsewhere.
// The override exists for the tests: they have to write real rules to prove the
// lookup precedence, and against the default key that would destroy the rules of
// whoever ran them.
const std::wstring& storage_key();
void set_storage_key(const std::wstring& subkey);

std::vector<Rule> load();

bool set(const std::wstring& executable, LANGID language);
bool clear(const std::wstring& executable);

// Zero when none of the three forms has a rule. windowClass may be empty.
LANGID lookup(const std::wstring& path, const std::wstring& windowClass);

// Prefix that marks a key as naming a window class.
extern const wchar_t* const kClassPrefix;

// Prefixes that mark a key as a wildcard pattern: kGlobPrefix is matched against
// the executable's full path, kClassGlobPrefix against the window class.
extern const wchar_t* const kGlobPrefix;
extern const wchar_t* const kClassGlobPrefix;

// Whether text matches a glob pattern of * (any run, separators included) and ?
// (one character). Exposed for the tests; case is the caller's to normalise.
bool matches_glob(const std::wstring& pattern, const std::wstring& text);

// Lower-cased full path of the process owning hwnd, empty when it cannot be
// read. Reading another process's image name needs no elevation, but a window
// owned by a protected process still yields nothing.
std::wstring executable_of(HWND hwnd);

// Trailing component of a path, or the input unchanged when it has none.
std::wstring file_name_of(const std::wstring& path);

// Class name of hwnd, empty when it cannot be read. Needs no process access.
std::wstring window_class_of(HWND hwnd);

} // namespace rules
