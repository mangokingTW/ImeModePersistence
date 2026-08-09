#pragma once

#include <windows.h>

#include <string>

namespace config {

// Modal rules editor.
//
// lastApplication is supplied by the caller rather than read here. Reaching this
// dialog means clicking the tray icon, which hands the foreground first to the
// shell and then to this process, so by the time the dialog exists the system can
// no longer say which application the user meant. The caller keeps a snapshot
// that excludes both.
void show_rules(HINSTANCE instance, HWND owner, const std::wstring& lastApplication);

} // namespace config
