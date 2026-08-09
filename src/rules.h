#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Per-application layout bindings, kept in HKCU next to the autostart entry.
//
// A rule is keyed by either the executable's full path or its bare file name,
// and which one it is can be read off the key itself: a key containing a path
// separator is a path rule. Lookup prefers the path, so two applications that
// happen to share a file name can be told apart, while a bare name still matches
// wherever the application is installed.
namespace rules {

struct Rule {
    std::wstring executable;   // lower-cased full path, or bare file name
    LANGID language{};
};

std::vector<Rule> load();

bool set(const std::wstring& executable, LANGID language);
bool clear(const std::wstring& executable);

// Zero when neither the path nor its file name has a rule.
LANGID lookup(const std::wstring& path);

// Lower-cased full path of the process owning hwnd, empty when it cannot be
// read. Reading another process's image name needs no elevation, but a window
// owned by a protected process still yields nothing.
std::wstring executable_of(HWND hwnd);

// Trailing component of a path, or the input unchanged when it has none.
std::wstring file_name_of(const std::wstring& path);

} // namespace rules
