#pragma once

#include <windows.h>

// Runs the utility at logon through the per-user Run key.
//
// Why not a Windows service: a service runs in session 0 with no interactive
// desktop, so GetForegroundWindow never sees the user's windows and
// WM_IME_CONTROL never reaches their threads. This utility has to live in the
// interactive session.
//
// Why HKCU rather than HKLM or a scheduled task: HKCU needs no administrator
// rights and no UAC prompt, and it starts the utility at the same integrity
// level as the ordinary applications whose IME state it adjusts. Only a
// scheduled task with highest privileges could also reach elevated windows, at
// the cost of keeping an elevated process resident.
namespace autostart {

// True when the Run key entry exists *and* points at this executable. A stale
// entry left behind by moving the .exe therefore reads as disabled, and enabling
// again rewrites it to the current path.
bool is_enabled();

bool set_enabled(bool enable);

} // namespace autostart
