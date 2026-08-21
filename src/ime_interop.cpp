#include "ime_interop.h"
#include "helper.h"

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

// Off by default: only an elevated process sets it (see the header). An unelevated
// process must never touch the focus-child context -- the send is refused and can
// stall -- so the default keeps the exact top-level behaviour of every prior
// release.
bool g_focusChild = false;

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

// The windows to try, best first. With focus-child targeting on (elevated only),
// the actually-focused child window comes first: for a TSF/WinUI app its IME
// context is the real one, distinct from the top-level window's, and the only one
// a write actually reaches. The top-level window is always the fallback, and is
// the sole target when targeting is off -- identical to the historical behaviour.
struct Targets {
    HWND wnd[2];
    int count;
};

Targets targets_for(HWND hwnd) {
    Targets t{};
    if (g_focusChild) {
        const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
        GUITHREADINFO gti{};
        gti.cbSize = sizeof(gti);
        if (thread && GetGUIThreadInfo(thread, &gti) && gti.hwndFocus &&
            gti.hwndFocus != hwnd && IsWindow(gti.hwndFocus)) {
            t.wnd[t.count++] = gti.hwndFocus;
        }
    }
    t.wnd[t.count++] = hwnd;
    return t;
}

} // namespace

void set_focus_child_targeting(bool enabled) {
    g_focusChild = enabled;
}

bool has_ime(HWND hwnd) {
    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (!thread) {
        return false;
    }
    return ImmIsIME(GetKeyboardLayout(thread)) != FALSE;
}

Conversion read(HWND hwnd) {
    Conversion state;
    if (!g_focusChild) {
        bool open = false;
        DWORD bits = 0;
        if (helper::try_read(hwnd, open, bits)) {
            state.valid = true;
            state.open = open;
            state.bits = bits;
            return state;
        }
    }

    const Targets targets = targets_for(hwnd);
    for (int i = 0; i < targets.count; ++i) {
        HWND imeWnd = default_ime_window(targets.wnd[i]);
        if (!imeWnd) {
            continue;
        }

        // WM_IME_CONTROL returns 0 both for "alphanumeric" and for a failed call,
        // so success is decided by delivery of the message, not by its result. A
        // target that will not answer (the focus child from an unelevated process)
        // falls through to the next -- the top-level window.
        DWORD_PTR open = 0;
        if (!send(imeWnd, IMC_GETOPENSTATUS, 0, open)) {
            continue;
        }
        DWORD_PTR bits = 0;
        if (!send(imeWnd, IMC_GETCONVERSIONMODE, 0, bits)) {
            continue;
        }

        state.valid = true;
        state.open = open != 0;
        state.bits = static_cast<DWORD>(bits);
        return state;
    }
    return state;
}

bool write_open(HWND hwnd, bool open) {
    if (!g_focusChild) {
        if (helper::try_write_open(hwnd, open)) {
            return true;
        }
    }
    const Targets targets = targets_for(hwnd);
    for (int i = 0; i < targets.count; ++i) {
        HWND imeWnd = default_ime_window(targets.wnd[i]);
        if (!imeWnd) {
            continue;
        }
        DWORD_PTR result = 0;
        if (send(imeWnd, IMC_SETOPENSTATUS, open ? TRUE : FALSE, result)) {
            return true;
        }
    }
    return false;
}

bool write_conversion(HWND hwnd, DWORD bits) {
    if (!g_focusChild) {
        if (helper::try_write_conversion(hwnd, bits)) {
            return true;
        }
    }
    const Targets targets = targets_for(hwnd);
    for (int i = 0; i < targets.count; ++i) {
        HWND imeWnd = default_ime_window(targets.wnd[i]);
        if (!imeWnd) {
            continue;
        }
        DWORD_PTR result = 0;
        if (send(imeWnd, IMC_SETCONVERSIONMODE, static_cast<LPARAM>(bits), result)) {
            return true;
        }
    }
    return false;
}

} // namespace ime::interop
