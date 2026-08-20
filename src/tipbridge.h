#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "ime_state.h"

// The tray-side end of the Text Input Processor bridge (see src/tip/). The TIP is
// loaded into other applications and, over a named pipe, reports the conversion
// mode it sees on each thread and accepts a mode to set. This is the server that
// collects those reports and issues those commands, so the message loop can ask a
// reliable in-process source for a TSF/packaged app instead of the IMM32 interop
// -- which reads stale and writes nothing for exactly those apps.
//
// Entirely additive: when no TIP is connected for a thread (a classic IMM32 app,
// or the service is not registered/loaded), mode_for returns nullopt and set_mode
// returns false, and the caller falls back to the existing ime:: path unchanged.
namespace tipbridge {

// Starts the pipe server on a background thread. Best-effort: a failure leaves the
// bridge dormant and every query a no-op, so the app runs exactly as it did
// without it. Safe to call once at startup.
void start();

// Stops the server and disconnects every client. Safe to call at shutdown even if
// start() failed.
void stop();

// The mode last reported by a TIP running on `tid`, or nullopt when none is
// connected for that thread. A returned Unknown means a TIP is present but could
// not read the compartment.
std::optional<ime::Mode> mode_for(DWORD tid);

// Asks the TIP on `tid` to set the conversion mode in its own process. Returns
// false when no TIP is connected for that thread (so the caller uses ime::set_mode
// instead) or the command could not be sent.
bool set_mode(DWORD tid, ime::Mode mode);

// Registers / unregisters the TIP COM+TSF entries by loading the DLL and calling
// its self-registration exports. Machine-wide, so both require an elevated
// process; the caller checks elevation and logs a hint when it is missing.
// `dllPath` is the full path to ImeModePersistenceTip.dll (next to the exe).
bool register_tip(const std::wstring& dllPath);
bool unregister_tip(const std::wstring& dllPath);

} // namespace tipbridge
