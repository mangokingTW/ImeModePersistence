#include "ime_interop.h"

#include <imm.h>

#pragma comment(lib, "imm32.lib")

// IMC_* are declared in immdev.h, which is not shipped by every SDK flavour.
// The values are part of the stable WM_IME_CONTROL contract.
#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif
#ifndef IMC_SETCONVERSIONMODE
#define IMC_SETCONVERSIONMODE 0x0002
#endif
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif
#ifndef IMC_SETOPENSTATUS
#define IMC_SETOPENSTATUS 0x0006
#endif

namespace ime::interop {
namespace {

// The default IME window belongs to the foreground thread, which may be busy or
// hung. Never block the message pump waiting for it.
constexpr UINT kSendTimeoutMs = 120;

bool send(HWND imeWnd, WPARAM command, LPARAM value, DWORD_PTR& result) {
    result = 0;
    return SendMessageTimeoutW(
               imeWnd,
               WM_IME_CONTROL,
               command,
               value,
               SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
               kSendTimeoutMs,
               &result) != 0;
}

HWND default_ime_window(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !has_ime(hwnd)) {
        return nullptr;
    }
    HWND imeWnd = ImmGetDefaultIMEWnd(hwnd);
    return (imeWnd && IsWindow(imeWnd)) ? imeWnd : nullptr;
}

} // namespace

bool has_ime(HWND hwnd) {
    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (!thread) {
        return false;
    }
    return ImmIsIME(GetKeyboardLayout(thread)) != FALSE;
}

Conversion read(HWND hwnd) {
    Conversion state;

    HWND imeWnd = default_ime_window(hwnd);
    if (!imeWnd) {
        return state;
    }

    // WM_IME_CONTROL returns 0 both for "alphanumeric" and for a failed call,
    // so success is decided by delivery of the message, not by its result.
    DWORD_PTR open = 0;
    if (!send(imeWnd, IMC_GETOPENSTATUS, 0, open)) {
        return state;
    }

    DWORD_PTR bits = 0;
    if (!send(imeWnd, IMC_GETCONVERSIONMODE, 0, bits)) {
        return state;
    }

    state.valid = true;
    state.open = open != 0;
    state.bits = static_cast<DWORD>(bits);
    return state;
}

bool write_open(HWND hwnd, bool open) {
    HWND imeWnd = default_ime_window(hwnd);
    if (!imeWnd) {
        return false;
    }
    DWORD_PTR result = 0;
    return send(imeWnd, IMC_SETOPENSTATUS, open ? TRUE : FALSE, result);
}

bool write_conversion(HWND hwnd, DWORD bits) {
    HWND imeWnd = default_ime_window(hwnd);
    if (!imeWnd) {
        return false;
    }
    DWORD_PTR result = 0;
    return send(imeWnd, IMC_SETCONVERSIONMODE, static_cast<LPARAM>(bits), result);
}

} // namespace ime::interop
