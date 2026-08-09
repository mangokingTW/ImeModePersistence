// WIN32_LEAN_AND_MEAN, NOMINMAX and UNICODE come from the build definitions in
// CMakeLists.txt, so every translation unit sees the same configuration.
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

constexpr UINT kObserveIntervalMs = 50;

// A foreground change fires before the new thread's IME is usable, so the first
// restore attempt waits, and every attempt verifies its own result.
constexpr UINT kRestoreDelaysMs[] = {60, 120, 250, 500};
constexpr int kMaxRestoreAttempts = static_cast<int>(ARRAYSIZE(kRestoreDelaysMs));

// After a successful restore the IME keeps settling for a moment. Anything
// observed inside this window is our own change echoing back, not the user.
constexpr ULONGLONG kPostRestoreSuppressMs = 250;

// A mode change is only credited to the user once the input context has been
// stable for this long, which excludes the churn of a focus transition.
constexpr ULONGLONG kPromotionDwellMs = 150;

struct AppState {
    HWND hwnd{};
    HWINEVENTHOOK foregroundHook{};

    // The mode the user last selected deliberately. A focus transition must NOT
    // overwrite this value, because the window being switched to may be carrying
    // a stale IME state that Windows saved for it earlier.
    ime::Mode desiredMode{ime::Mode::Unknown};

    // IME conversion mode is per-thread and per-layout, not per-window: two
    // windows of the same thread share one mode, so identity is the (thread,
    // layout) pair rather than the HWND.
    DWORD observedThread{};
    HKL observedLayout{};
    ime::Mode observedMode{ime::Mode::Unknown};
    ULONGLONG contextSince{};

    HWND pendingWindow{};
    int restoreAttempt{};
    ULONGLONG suppressPromotionUntil{};

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

void cancel_restore() {
    KillTimer(g_app.hwnd, TIMER_RESTORE);
    g_app.pendingWindow = nullptr;
    g_app.restoreAttempt = 0;
}

void schedule_restore_attempt(HWND hwnd) {
    if (g_app.restoreAttempt >= kMaxRestoreAttempts) {
        // Out of attempts. Adopt whatever the target settled on so the next
        // observation does not read the difference as a user decision.
        g_app.observedMode = ime::query_state(hwnd).mode;
        cancel_restore();
        return;
    }

    const UINT delay = kRestoreDelaysMs[g_app.restoreAttempt];
    ++g_app.restoreAttempt;
    g_app.pendingWindow = hwnd;
    KillTimer(g_app.hwnd, TIMER_RESTORE);
    SetTimer(g_app.hwnd, TIMER_RESTORE, delay, nullptr);
}

void accept_restored_state(HWND hwnd, const ime::State& state) {
    g_app.observedThread = GetWindowThreadProcessId(hwnd, nullptr);
    g_app.observedLayout = GetKeyboardLayout(g_app.observedThread);
    g_app.observedMode = state.mode;
    g_app.contextSince = GetTickCount64();
    g_app.suppressPromotionUntil = g_app.contextSince + kPostRestoreSuppressMs;
    cancel_restore();
}

// Records that the input context changed, without ever inferring a new desired
// mode from the window being switched to.
void note_context_switch(HWND hwnd) {
    if (!hwnd || hwnd == g_app.hwnd) {
        return;
    }

    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (!thread) {
        return;
    }

    g_app.observedThread = thread;
    g_app.observedLayout = GetKeyboardLayout(thread);
    g_app.contextSince = GetTickCount64();

    const ime::State state = ime::query_state(hwnd);
    g_app.observedMode = state.mode;

    if (g_app.desiredMode == ime::Mode::Unknown) {
        // Nothing to restore yet: seed the desired mode from the first context
        // we can actually read.
        g_app.desiredMode = state.mode;
        cancel_restore();
        return;
    }

    g_app.restoreAttempt = 0;
    schedule_restore_attempt(hwnd);
}

void restore_tick() {
    KillTimer(g_app.hwnd, TIMER_RESTORE);

    const HWND hwnd = g_app.pendingWindow;
    const ime::Mode desired = g_app.desiredMode;
    if (!hwnd || !IsWindow(hwnd) || desired == ime::Mode::Unknown) {
        cancel_restore();
        return;
    }
    if (hwnd != GetForegroundWindow()) {
        // Focus moved on while we were waiting; the switch to the new window
        // schedules its own restore.
        cancel_restore();
        return;
    }

    const ime::State before = ime::query_state(hwnd);
    if (!before.valid) {
        // The IME/TSF context is not activated yet, or the active layout is not
        // an IME at all. Either way there is nothing to write.
        schedule_restore_attempt(hwnd);
        return;
    }
    if (before.mode == desired) {
        accept_restored_state(hwnd, before);
        return;
    }

    ime::set_mode(hwnd, desired);

    // Verify instead of trusting the write: an IME that is still activating can
    // overwrite our change with the state Windows had saved for this thread.
    const ime::State after = ime::query_state(hwnd);
    if (after.valid && after.mode == desired) {
        accept_restored_state(hwnd, after);
        return;
    }

    schedule_restore_attempt(hwnd);
}

void observe_tick() {
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd || hwnd == g_app.hwnd) {
        return;
    }

    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (!thread) {
        return;
    }

    if (thread != g_app.observedThread || GetKeyboardLayout(thread) != g_app.observedLayout) {
        // Also covers a layout switch inside the same thread, which is a system
        // event rather than the user picking a conversion mode.
        note_context_switch(hwnd);
        return;
    }

    const ime::State state = ime::query_state(hwnd);
    if (!state.valid) {
        g_app.observedMode = ime::Mode::Unknown;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const bool settling = now < g_app.suppressPromotionUntil ||
                          now - g_app.contextSince < kPromotionDwellMs ||
                          g_app.pendingWindow != nullptr;

    if (!settling &&
        g_app.observedMode == ime::Mode::Unknown &&
        g_app.desiredMode != ime::Mode::Unknown &&
        state.mode != g_app.desiredMode) {
        // The context became readable only after the restore attempts ran out,
        // so start a fresh round now that there is something to write to.
        g_app.observedMode = state.mode;
        g_app.restoreAttempt = 0;
        schedule_restore_attempt(hwnd);
        return;
    }

    // Same input context + a settled mode change is the strongest signal
    // available without injecting into every process: the user changed the mode
    // while working in this window, so it becomes the new global intent.
    if (!settling &&
        state.mode != ime::Mode::Unknown &&
        g_app.observedMode != ime::Mode::Unknown &&
        state.mode != g_app.observedMode) {
        g_app.desiredMode = state.mode;
    }

    g_app.observedMode = state.mode;
}

void CALLBACK win_event_proc(
    HWINEVENTHOOK,
    DWORD event,
    HWND hwnd,
    LONG,
    LONG,
    DWORD,
    DWORD) {
    if (event != EVENT_SYSTEM_FOREGROUND || !hwnd) {
        return;
    }
    note_context_switch(hwnd);
}

void show_status() {
    const HWND foreground = GetForegroundWindow();
    const ime::State state = ime::query_state(foreground);

    wchar_t text[256]{};
    StringCchPrintfW(
        text,
        ARRAYSIZE(text),
        L"Desired mode: %s\nForeground mode: %s\nIME reachable: %s",
        ime::mode_name(g_app.desiredMode),
        ime::mode_name(state.mode),
        state.valid ? L"yes" : L"no");
    MessageBoxW(g_app.hwnd, text, L"IME Mode Persistence", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_RESTORE) {
            restore_tick();
        } else if (wParam == TIMER_OBSERVE) {
            observe_tick();
        }
        return 0;

    case WMAPP_TRAY:
        if (lParam == WM_LBUTTONDBLCLK) {
            show_status();
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

    note_context_switch(GetForegroundWindow());

    // Polling is intentional: it is what lets us tell a mode change made while
    // the same window stays focused apart from one caused by switching windows.
    SetTimer(g_app.hwnd, TIMER_OBSERVE, kObserveIntervalMs, nullptr);
    set_tray_icon(true);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
