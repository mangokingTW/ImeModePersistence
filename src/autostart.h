#pragma once

#include <windows.h>

// Starting the utility at logon, by whichever of two mechanisms fits the
// privileges it currently has.
//
// Why not a Windows service: a service runs in session 0 with no interactive
// desktop, so GetForegroundWindow never sees the user's windows and
// WM_IME_CONTROL never reaches their threads. This utility has to live in the
// interactive session.
//
// The Run key can only ever start an *unelevated* copy, so an elevated utility
// needs a scheduled task with highest privileges instead -- also the only way to
// start elevated without a UAC prompt at every logon. Creating one needs
// administrator rights, which an elevated copy already has; an unelevated one
// falls back to the Run key.
namespace autostart {

enum class Kind {
    None,
    Registry,       // HKCU Run entry, starts unelevated
    ScheduledTask,  // at logon with highest privileges
};

// What is configured right now, regardless of which mechanism this process would
// choose. A Run entry counts only when it points at *this* executable, so one left
// behind by moving the .exe reads as absent.
Kind current();

inline bool is_enabled() {
    return current() != Kind::None;
}

// Enabling picks the mechanism that matches the current privileges. Disabling
// removes both, so a leftover from the other mechanism cannot keep starting the
// utility after the user turned it off.
bool set_enabled(bool enable);

// Whether this process is running elevated. Everything about what the utility can
// reach depends on it: Windows does not let a lower-privileged program read the
// windows of a higher-privileged one, so an unelevated copy cannot see an
// anti-cheat protected game at all.
bool elevated();

} // namespace autostart
