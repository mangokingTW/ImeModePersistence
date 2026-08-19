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

// Opts a window into the app's dark controls (the undocumented uxtheme ordinal),
// following the current mode. A no-op on Windows before 1809 (build 17763), where
// the ordinal is absent, so the caller degrades to a light body there.
void allow_dark_window(HWND window);

// Re-themes every child control of a dialog for the current mode: dark edit /
// combo / list / button visuals in dark mode, the default theme in light. Call
// after the controls exist and again on a light/dark switch.
void apply_dark_controls(HWND parent);

// Colours for the current mode. bg()/text() are the dialog body; control_bg() is
// the deeper fill of an edit / list. The *_brush() forms are cached; free_brushes
// releases them (call on the dialog's teardown and before recreating on a switch).
COLORREF bg();
COLORREF control_bg();
COLORREF text();
HBRUSH bg_brush();
HBRUSH control_bg_brush();
void free_brushes();

} // namespace theme
