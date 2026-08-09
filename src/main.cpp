#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "ime_state.h"

namespace {

constexpr UINT WMAPP_TRAY = WM_APP + 1;
constexpr UINT_PTR TIMER_RESTORE = 1;
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr wchar_t kClassName[] = L"ImeModePersistenceHiddenWindow";

struct AppState {
    HWND hwnd{};
    HWINEVENTHOOK foregroundHook{};
    ime::Mode lastUserMode{ime::Mode::Unknown};
    HWND lastForeground{};
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
    g_app.lastForeground = hwnd;
    KillTimer(g_app.hwnd, TIMER_RESTORE);
    SetTimer(g_app.hwnd, TIMER_RESTORE, 30, nullptr);
}

void restore_current_mode() {
    const HWND hwnd = g_app.lastForeground;
    const ime::Mode desired = g_app.lastUserMode;
    if (!IsWindow(hwnd) || desired == ime::Mode::Unknown) {
        return;
    }

    g_app.restoring = true;
    ime::set_mode(hwnd, desired);
    g_app.restoring = false;
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

    // Seed the state from the current foreground window. This is deliberately
    // best-effort; the user can start in a context where IMM32 is unavailable.
    HWND foreground = GetForegroundWindow();
    if (foreground && foreground != g_app.hwnd) {
        g_app.lastForeground = foreground;
        g_app.lastUserMode = ime::query_mode(foreground);
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

    set_tray_icon(true);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
