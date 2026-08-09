#pragma once

#include <windows.h>

#include <string>

namespace config {

// Modal rules editor.
//
// lastApplication is supplied by the caller rather than read here: while the
// dialog is open the foreground window belongs to this process, so there is no
// way to ask the system which application the user actually wants to bind.
void show_rules(HINSTANCE instance, HWND owner, const std::wstring& lastApplication);

} // namespace config
