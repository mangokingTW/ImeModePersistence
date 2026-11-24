#include "ime_interop.h"
#include "helper.h"
#include "rules.h"

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

bool is_explorer_or_dialog(std::wstring_view topClass) {
    return topClass == L"cabinetwclass" ||
           topClass == L"explorewclass" ||
           topClass == L"#32770";
}

// The windows to try, best first. With focus-child targeting on (elevated only),
// the actually-focused child window comes first: for a TSF/WinUI app its IME
// context is the real one, distinct from the top-level window's, and the only one
// a write actually reaches. Even when unelevated, classic Win32 Edit controls in
// File Explorer and standard file/path picker dialogs are reachable directly. The
// top-level window is always the fallback, keeping the historical behaviour.
struct Targets {
    HWND wnd[2];
    int count;
};

Targets targets_for(HWND hwnd) {
    Targets t{};
    HWND focus = focus_child(hwnd);
    if (focus && focus != hwnd) {
        const bool allow =
            g_focusChild ||
            allow_unelevated_focus_child(rules::window_class_of(hwnd),
                                         rules::window_class_of(focus));
        if (allow) {
            t.wnd[t.count++] = focus;
        }
    }
    t.wnd[t.count++] = hwnd;
    return t;
}

} // namespace

void set_focus_child_targeting(bool enabled) {
    g_focusChild = enabled;
}

HWND focus_child(HWND topHwnd) {
    if (!topHwnd || !IsWindow(topHwnd)) {
        return nullptr;
    }

    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndFocus && IsWindow(gti.hwndFocus)) {
        if (gti.hwndFocus == topHwnd || GetAncestor(gti.hwndFocus, GA_ROOT) == topHwnd) {
            return gti.hwndFocus;
        }
    }

    const DWORD thread = GetWindowThreadProcessId(topHwnd, nullptr);
    gti = {};
    gti.cbSize = sizeof(gti);
    if (thread && GetGUIThreadInfo(thread, &gti) && gti.hwndFocus &&
        IsWindow(gti.hwndFocus)) {
        return gti.hwndFocus;
    }

    return nullptr;
}

DWORD input_thread(HWND topHwnd) {
    if (!topHwnd || !IsWindow(topHwnd)) {
        return 0;
    }
    if (HWND focus = focus_child(topHwnd)) {
        const DWORD focusThread = GetWindowThreadProcessId(focus, nullptr);
        if (focusThread) {
            return focusThread;
        }
    }
    return GetWindowThreadProcessId(topHwnd, nullptr);
}

bool allow_unelevated_focus_child(std::wstring_view topClass,
                                  std::wstring_view focusClass) {
    if (!is_explorer_or_dialog(topClass)) {
        return false;
    }
    return focusClass == L"edit" ||
           focusClass == L"richedit20w" ||
           focusClass == L"richedit50w";
}

bool is_non_text_shell_control(std::wstring_view topClass,
                               std::wstring_view focusClass) {
    if (!is_explorer_or_dialog(topClass)) {
        return false;
    }
    if (focusClass.empty()) {
        return true;
    }
    static constexpr std::wstring_view kNonTextClasses[] = {
        L"directuihwnd",
        L"syslistview32",
        L"systreeview32",
        L"toolbarwindow32",
        L"button",
        L"static",
        L"sysheader32",
        L"scrollbar",
        L"shelldll_defview",
        L"shelltabwindowclass",
        L"cabinetwclass",
        L"explorewclass",
        L"#32770",
    };
    for (const std::wstring_view cls : kNonTextClasses) {
        if (focusClass == cls) {
            return true;
        }
    }
    return false;
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
