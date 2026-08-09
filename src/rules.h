#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Per-application layout bindings, kept in HKCU next to the autostart entry.
//
// Applications are identified by executable file name rather than full path or
// window class: a path breaks when the user moves or updates the application,
// and a window class is neither stable nor discoverable by the person writing
// the rule.
namespace rules {

struct Rule {
    std::wstring executable;   // lower-cased file name, e.g. L"notepad.exe"
    LANGID language{};
};

std::vector<Rule> load();

bool set(const std::wstring& executable, LANGID language);
bool clear(const std::wstring& executable);

// Zero when the executable has no rule.
LANGID lookup(const std::wstring& executable);

// Lower-cased file name of the process owning hwnd, empty when it cannot be
// read. Reading another process's image name needs no elevation, but a window
// owned by a protected process still yields nothing.
std::wstring executable_of(HWND hwnd);

} // namespace rules
