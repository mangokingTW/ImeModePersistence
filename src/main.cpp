#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>

#include "ime_state.h"

namespace {

constexpr UINT WMAPP_TRAY = WM_APP + 1;
constexpr UINT_PTR TIMER_RESTORE = 1;
constexpr UINT_PTR TIMER_OBSERVE = 2;
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr wchar_t kClassName[] = L"ImeModePersistenceHiddenWindow";

struct AppState {
    HWND hwnd{};
    HWINEVENTHOOK foregroundHook{};

    // The mode last observed while the user was interacting with the same
    // foreground window. A focus transition must NOT overwrite this value,
    // because the target window may be carrying an old/stale IME state.
    ime::Mode lastUserMode{ime::Mode::Unknown};

    HWND observedForeground{};
    ime::Mode observedMode{ime::Mode::Unknown};
    HWND pendingForeground{};

    bool restoring{false};
    NOTIFYICONDATAW tray{};
};

AppState g_app;

void show_error(const wchar_t* text) {
    MessageBoxW(g_app.hwnd, text, L"ImeModePersistence", MB_ICONERROR | MB_OK);
}

void set_tray_icon(bool add) {
    if (add) {
        g_app.tray.cbSize = sizeof(g_app.tray);
        g_app.tray.hWnd = g_app.hwnd;
        g_app.tray.uID = 1;
        g_app.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_app.tray.uCallbackMessage = WMAPP_TRAY;
        g_app.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        StringCchCopyW(g_app.tray.szTip, ARRAYSIZE(g_app.tray.szTip), L"IME Mode Persistence");
        Shell_NotifyIconW(NIM_ADD, &g_app.tray);
    } else {
        Shell_NotifyIconW(NIM_DELETE, &g_app.tray);
    }
}

void schedule_restore(HWND hwnd) {
    g_app.pendingForeground = hwnd;
    KillTimer(g_app.hwnd, TIMER_RESTORE);
    // Give Windows/TSF/IME a chance to finish activating the new context.
    SetTimer(g_app.hwnd, TIMER_RESTORE, 50, nullptr);
}

void restore_current_mode() {
    const HWND hwnd = g_app.pendingForeground;
    const ime::Mode desired = g_app.lastUserMode;
    if (!IsWindow(hwnd) || desired == ime::Mode::Unknown) {
        return;
    }

    g_app.restoring = true;
    ime::set_mode(hwnd, desired);
    g_app.restoring = false;

    // Observe again after the restore. If the IME/TSF overwrites our change,
    // the next observation gives us a chance to retry without changing the
    // global desired mode.
    g_app.observedForeground = hwnd;
    g_app.observedMode = ime::query_mode(hwnd);
}

void observe_current_window() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || hwnd == g_app.hwnd) {
        return;
    }

    const ime::Mode current = ime::query_mode(hwnd);

    if (hwnd != g_app.observedForeground) {
        // This is a focus transition. Never infer a new global mode from the
        // target window because its state may simply be the state Windows had
        // stored for that window/thread.
        g_app.observedForeground = hwnd;
        g_app.observedMode = current;
        schedule_restore(hwnd);
        return;
    }

    if (g_app.restoring) {
        g_app.observedMode = current;
        return;
    }

    // Same foreground window + mode changed = the strongest signal available
    // without injecting into every process: the user (or the IME UI) changed
    // the mode while interacting with this window. Promote it to global state.
    if (current != ime::Mode::Unknown &&
        g_app.observedMode != ime::Mode::Unknown &&
        current != g_app.observedMode) {
        g_app.lastUserMode = current;
    }

    g_app.observedMode = current;
}

void CALLBACK win_event_proc(
    HWINEVENTHOOK,
    DWORD event,
    HWND hwnd,
    LONG,
    LONG,
    DWORD,
    DWORD) {
    if (event != EVENT_SYSTEM_FOREGROUND || !hwnd || hwnd == g_app.hwnd) {
        return;
    }
    schedule_restore(hwnd);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_RESTORE) {
            KillTimer(hwnd, TIMER_RESTORE);
            restore_current_mode();
        } else if (wParam == TIMER_OBSERVE) {
            observe_current_window();
        }
        return 0;

    case WMAPP_TRAY:
        if (lParam == WM_LBUTTONDBLCLK) {
            const wchar_t* mode = ime::mode_name(g_app.lastUserMode);
            MessageBoxW(hwnd, mode, L"Last IME mode", MB_OK | MB_ICONINFORMATION);
        } else if (lParam == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            if (menu) {
                AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
                POINT pt{};
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
            }
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_RESTORE);
        KillTimer(hwnd, TIMER_OBSERVE);
        if (g_app.foregroundHook) {
            UnhookWinEvent(g_app.foregroundHook);
            g_app.foregroundHook = nullptr;
        }
        set_tray_icon(false);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;

    if (!RegisterClassW(&wc)) {
        return 1;
    }

    g_app.hwnd = CreateWindowExW(
        0, kClassName, L"ImeModePersistence", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_app.hwnd) {
        return 1;
    }

    HWND foreground = GetForegroundWindow();
    if (foreground && foreground != g_app.hwnd) {
        g_app.observedForeground = foreground;
        g_app.observedMode = ime::query_mode(foreground);
        g_app.lastUserMode = g_app.observedMode;
    }

    g_app.foregroundHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        win_event_proc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (!g_app.foregroundHook) {
        show_error(L"SetWinEventHook failed.");
        DestroyWindow(g_app.hwnd);
        return 1;
    }

    // Polling is intentional: it lets us distinguish a mode change while the
    // same window remains focused from a mode change caused by focus switching.
    SetTimer(g_app.hwnd, TIMER_OBSERVE, 50, nullptr);
    set_tray_icon(true);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
