#pragma once

#include <windows.h>

// Runs the utility at logon through the per-user Run key.
//
// Why not a Windows service: a service runs in session 0 with no interactive
// desktop, so GetForegroundWindow never sees the user's windows and
// WM_IME_CONTROL never reaches their threads. This utility has to live in the
// interactive session.
//
// This is the unelevated route, and the Run key can only ever start an unelevated
// copy. Elevated autostart is a scheduled task registered by the installer: a task
// with highest privileges is the only way to start elevated at logon without a UAC
// prompt every time, and creating one needs administrator rights that the utility
// itself does not ask for.
namespace autostart {

// True when the Run key entry exists *and* points at this executable. A stale
// entry left behind by moving the .exe therefore reads as disabled, and enabling
// again rewrites it to the current path.
bool is_enabled();

bool set_enabled(bool enable);

// Whether this process is running elevated. Everything about what the utility can
// reach depends on it: Windows does not let a lower-privileged program read the
// windows of a higher-privileged one, so an unelevated copy cannot see an
// anti-cheat protected game at all.
bool elevated();

} // namespace autostart
