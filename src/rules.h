#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Per-application layout bindings, kept in HKCU next to the autostart entry.
//
// A rule is keyed by one of three things, and which one is readable off the key
// itself: a key containing a path separator is a full path, a key starting with
// "class:" is a window class, anything else is a bare executable file name.
// Lookup goes from most specific to least, so one particular copy of an
// application can be told apart from another sharing its file name.
//
// Window class rules exist because reading an executable path needs OpenProcess,
// which anti-cheat-protected games refuse even to an administrator. GetClassName
// reads a window property and needs no access to the process at all, so it is the
// only way to identify such an application without touching it.
namespace rules {

struct Rule {
    std::wstring executable;   // lower-cased full path, or bare file name
    LANGID language{};
};

std::vector<Rule> load();

bool set(const std::wstring& executable, LANGID language);
bool clear(const std::wstring& executable);

// Zero when none of the three forms has a rule. windowClass may be empty.
LANGID lookup(const std::wstring& path, const std::wstring& windowClass);

// Prefix that marks a key as naming a window class.
extern const wchar_t* const kClassPrefix;

// Lower-cased full path of the process owning hwnd, empty when it cannot be
// read. Reading another process's image name needs no elevation, but a window
// owned by a protected process still yields nothing.
std::wstring executable_of(HWND hwnd);

// Trailing component of a path, or the input unchanged when it has none.
std::wstring file_name_of(const std::wstring& path);

// Class name of hwnd, empty when it cannot be read. Needs no process access.
std::wstring window_class_of(HWND hwnd);

} // namespace rules
