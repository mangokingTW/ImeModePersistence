#pragma once

#include <windows.h>

namespace theme {

// True when the user has apps set to dark in Settings > Personalisation. Read
// from the registry rather than a system call because the documented API for it
// is WinRT, and this is one DWORD.
bool dark_mode();

// Paints the title bar dark when the user is in dark mode, and light again when
// they are not, so it also serves as the response to a theme change.
//
// Only the title bar: making the controls dark as well needs undocumented
// uxtheme ordinals, which are not worth depending on here.
void apply_titlebar(HWND window);

// True when a WM_SETTINGCHANGE is the one that follows a light/dark switch.
bool is_colour_change(LPARAM lParam);

} // namespace theme
