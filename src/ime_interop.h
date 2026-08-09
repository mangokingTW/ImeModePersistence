#pragma once

#include <windows.h>

// Low-level access to the conversion mode of *another thread's* IME.
//
// Why not ITfThreadMgr / GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION:
// TSF compartments are per-thread and live inside the owning process. An
// out-of-process utility cannot activate a thread manager on a foreign thread,
// so the TSF interfaces only ever describe *our own* thread and are useless
// here.
//
// Why not ImmGetContext / ImmSetOpenStatus (what this project used before):
// an HIMC is process-local. ImmGetContext on a window owned by another process
// returns nullptr, so the previous implementation silently reported Unknown for
// every foreground window that was not our own.
//
// The supported route is the IMM32 <-> TSF interop layer (CUAS). WM_IME_CONTROL
// is handled by the target thread's default IME window, which marshals the call
// into that thread and reports the real conversion mode -- including for TSF
// text services such as Microsoft Bopomofo.
namespace ime::interop {

struct Conversion {
    bool valid{false};   // false when no IME is reachable on the target thread
    bool open{false};    // IME open/closed
    DWORD bits{0};       // IME_CMODE_* flags
};

// True when the keyboard layout active on hwnd's thread is an IME rather than
// a plain layout such as US English. A layout switch is a system event and must
// not be mistaken for the user changing conversion mode.
bool has_ime(HWND hwnd);

Conversion read(HWND hwnd);

bool write_open(HWND hwnd, bool open);
bool write_conversion(HWND hwnd, DWORD bits);

} // namespace ime::interop
