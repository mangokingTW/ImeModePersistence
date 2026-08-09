// WIN32_LEAN_AND_MEAN, NOMINMAX and UNICODE come from the build definitions in
// CMakeLists.txt, so every translation unit sees the same configuration.
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <strsafe.h>

#include <string>

#include "autostart.h"
#include "config_dialog.h"
#include "diagnostic.h"
#include "ime_state.h"
#include "layout.h"
#include "resource.h"
#include "rules.h"
#include "settings.h"
#include "strings.h"
#include "theme.h"
#include "tsf.h"

namespace {

constexpr UINT WMAPP_TRAY = WM_APP + 1;
constexpr UINT_PTR TIMER_RESTORE = 1;
constexpr UINT_PTR TIMER_OBSERVE = 2;
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr UINT ID_TRAY_AUTOSTART = 1002;
constexpr UINT ID_TRAY_RULES = 1003;
constexpr UINT ID_TRAY_PERSIST = 1004;
constexpr UINT ID_TRAY_ELEVATE = 1005;
constexpr UINT ID_TRAY_LOG = 1006;
constexpr wchar_t kClassName[] = L"ImeModePersistenceHiddenWindow";

// Session-local: one instance per interactive logon session is what we want, and
// two instances would fight over restoring each other's writes.
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\ImeModePersistence.SingleInstance";

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

    // When off, only per-application bindings act; the global carry-over stops.
    bool persistMode{true};

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

    // Executable of the current foreground application, and the language its rule
    // binds it to (zero when it has no rule). Kept so the rules dialog can offer
    // the last application the user was actually working in: once the dialog is
    // open, the foreground window belongs to us.
    std::wstring observedExecutable;
    LANGID ruleLanguage{};

    // Which switching mechanism was last tried and whether the layout ended up
    // where the rule wanted it. Surfaced in the status box: no single mechanism
    // works for every application, so knowing which one took effect is the only
    // way to tell an ignored request apart from a rule that never matched.
    layout::Method layoutMethod{layout::Method::FocusWindow};
    bool layoutRequested{false};
    bool layoutSatisfied{false};

    // Clicking the tray icon hands the foreground to the shell, so a status box
    // that reported the live foreground reported explorer.exe every single time.
    // It reports this snapshot of the last real application instead.
    std::wstring snapshotApp;
    std::wstring snapshotClass;
    ime::Mode snapshotMode{ime::Mode::Unknown};
    bool snapshotReachable{false};
    LANGID snapshotRule{};
    LANGID snapshotLayout{};
    layout::Method snapshotMethod{layout::Method::FocusWindow};
    bool snapshotRequested{false};
    bool snapshotSatisfied{false};

    // Recomposing the tooltip is only worth doing when something in it changed.
    std::wstring tooltip;

    HANDLE singleInstance{};
    HICON trayIcon{};
    NOTIFYICONDATAW tray{};
};

AppState g_app;

// A task dialog rather than a message box: it carries the current visual style,
// and unlike MessageBox it centres on the screen instead of on its owner, which
// matters because this owner is a hidden zero-sized window in the top-left
// corner. Falls back if comctl32 v6 is somehow unavailable.
void show_message(const wchar_t* title, const wchar_t* body, bool error) {
    int pressed = 0;
    const HRESULT result = TaskDialog(
        g_app.hwnd,
        nullptr,
        title,
        nullptr,
        body,
        TDCBF_OK_BUTTON,
        error ? TD_ERROR_ICON : TD_INFORMATION_ICON,
        &pressed);

    if (FAILED(result)) {
        MessageBoxW(g_app.hwnd, body, title,
                    MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }
}

void show_error(const wchar_t* body) {
    show_message(text::s().errorTitle, body, true);
}

// ime::mode_name stays in English so the adapter has no opinion on presentation.
const wchar_t* mode_label(ime::Mode mode) {
    switch (mode) {
    case ime::Mode::Native: return text::s().modeNative;
    case ime::Mode::Alphanumeric: return text::s().modeAlphanumeric;
    default: return text::s().modeUnknown;
    }
}

// Our own windows must never be treated as a target. Checking the message-only
// window by handle is not enough now that the rules dialog exists: it is a
// different window in the same process, and it takes the foreground when open.
bool own_window(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

// Windows 11's own UI takes the foreground for windows the user is not working
// in: a cloaked window is composed but not shown, which is the state SearchHost
// and the Start menu sit in after being dismissed. Treating one as a context
// switch re-keys the observer, resets the dwell timer, and interrupts a restore
// in progress -- so these are skipped outright rather than merely ignored by the
// diagnostics.
bool ghost_window(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) {
        return true;
    }

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != 0;
    }
    return false;
}

// Shell surfaces that are genuinely visible while focused but are never what the
// user means by "the application I was in". A name list because they are separate
// processes with nothing structural in common -- SearchHost is not part of
// explorer, which is why excluding the shell alone was not enough.
bool shell_ui(const std::wstring& executable) {
    static constexpr const wchar_t* kHosts[] = {
        L"searchhost.exe",
        L"startmenuexperiencehost.exe",
        L"shellexperiencehost.exe",
        L"textinputhost.exe",
        L"lockapp.exe",
    };

    const std::wstring name = rules::file_name_of(executable);
    for (const wchar_t* host : kHosts) {
        if (CompareStringOrdinal(name.c_str(), -1, host, -1, TRUE) == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

// The taskbar and the desktop are the same process, and clicking either is
// exactly what happens on the way to the tray icon. Compared by process id
// rather than by executable name so a renamed or replaced shell still counts.
bool shell_window(HWND hwnd) {
    HWND shell = GetShellWindow();
    if (!shell) {
        return false;
    }

    DWORD shellPid = 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(shell, &shellPid);
    GetWindowThreadProcessId(hwnd, &pid);
    return shellPid != 0 && shellPid == pid;
}

// LoadImageW picks the image of the requested size out of the .ico rather than
// scaling one, which is what keeps the notification area crisp at any DPI.
HICON load_app_icon(HINSTANCE instance, int cx, int cy) {
    HICON icon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));

    // Falls back to the generic application icon. That one is shared and must
    // never be destroyed, which is why nothing here calls DestroyIcon: the
    // process only ever loads these once and exits.
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void set_tray_icon(bool add) {
    if (add) {
        g_app.tray.cbSize = sizeof(g_app.tray);
        g_app.tray.hWnd = g_app.hwnd;
        g_app.tray.uID = 1;
        g_app.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_app.tray.uCallbackMessage = WMAPP_TRAY;
        g_app.tray.hIcon = g_app.trayIcon;
        StringCchCopyW(g_app.tray.szTip, ARRAYSIZE(g_app.tray.szTip), text::s().trayTip);
        Shell_NotifyIconW(NIM_ADD, &g_app.tray);
    } else {
        Shell_NotifyIconW(NIM_DELETE, &g_app.tray);
    }
}

// What the utility could actually determine about the foreground window. An
// anti-cheat protected process refuses to have its path read, and reporting only
// that failure left no way to see the window class a rule has to match -- so the
// class is what gets shown when the path is unavailable, in exactly the form a
// rule key takes.
std::wstring window_identity(HWND hwnd, const std::wstring& executable) {
    if (!executable.empty()) {
        return executable;
    }

    const std::wstring windowClass = rules::window_class_of(hwnd);
    if (!windowClass.empty()) {
        return rules::kClassPrefix + windowClass;
    }
    return {};
}

void record_snapshot(HWND hwnd) {
    if (!hwnd || own_window(hwnd) || shell_window(hwnd) ||
        shell_ui(g_app.observedExecutable)) {
        return;
    }

    const HKL current = layout::current(hwnd);

    const ime::State state = ime::query_state(hwnd);

    g_app.snapshotApp = window_identity(hwnd, g_app.observedExecutable);
    g_app.snapshotClass = rules::window_class_of(hwnd);
    g_app.snapshotMode = state.mode;
    g_app.snapshotReachable = state.valid;
    g_app.snapshotRule = g_app.ruleLanguage;
    g_app.snapshotLayout = current ? layout::language_of(current) : 0;
    g_app.snapshotMethod = g_app.layoutMethod;
    g_app.snapshotRequested = g_app.layoutRequested;
    g_app.snapshotSatisfied = g_app.layoutSatisfied;
}

// Hovering the tray icon does not change the foreground window, which is what
// makes the tooltip the one place a diagnostic can be read without disturbing
// the thing being diagnosed.
void update_tooltip(HWND hwnd) {
    const text::Strings& t = text::s();

    const HKL current = hwnd ? layout::current(hwnd) : nullptr;
    const std::wstring identity =
        hwnd ? window_identity(hwnd, g_app.observedExecutable) : std::wstring{};

    wchar_t attempt[128]{};
    if (!g_app.layoutRequested) {
        StringCchCopyW(attempt, ARRAYSIZE(attempt), t.switchNotAttempted);
    } else {
        StringCchPrintfW(attempt, ARRAYSIZE(attempt),
                         g_app.layoutSatisfied ? t.switchOk : t.switchFailed,
                         layout::method_name(g_app.layoutMethod));
    }

    wchar_t composed[ARRAYSIZE(g_app.tray.szTip)]{};
    StringCchPrintfW(
        composed,
        ARRAYSIZE(composed),
        t.tooltipFormat,
        // The full class is shown rather than a trimmed name: it is the thing a
        // rule matches, so an abbreviation would defeat the point.
        identity.empty() ? t.unknownApplication : identity.c_str(),
        g_app.ruleLanguage == 0 ? t.noRule : layout::describe(g_app.ruleLanguage).c_str(),
        current ? layout::describe(layout::language_of(current)).c_str() : t.unknownApplication,
        attempt,
        // Shown only when it is a problem: the successful case stays compact, and
        // the tooltip is where this has to appear, since hovering is the one way
        // to read state without disturbing the window being read.
        autostart::elevated() ? L"" : t.tooltipUnelevated);

    if (g_app.tooltip == composed) {
        return;
    }
    g_app.tooltip = composed;

    if (!g_app.hwnd) {
        return;
    }
    NOTIFYICONDATAW update{};
    update.cbSize = sizeof(update);
    update.hWnd = g_app.hwnd;
    update.uID = 1;
    update.uFlags = NIF_TIP;
    StringCchCopyW(update.szTip, ARRAYSIZE(update.szTip), composed);
    Shell_NotifyIconW(NIM_MODIFY, &update);
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
        diag::write(L"gave up after %d attempts; adopting the target's own state",
                    kMaxRestoreAttempts);
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
    record_snapshot(hwnd);
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
    if (!hwnd || own_window(hwnd) || ghost_window(hwnd)) {
        return;
    }

    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (!thread) {
        return;
    }

    g_app.observedThread = thread;
    g_app.observedLayout = GetKeyboardLayout(thread);
    g_app.contextSince = GetTickCount64();

    g_app.observedExecutable = rules::executable_of(hwnd);
    g_app.ruleLanguage =
        rules::lookup(g_app.observedExecutable, rules::window_class_of(hwnd));

    // Reset per-application, so the status box describes the application in
    // front rather than whatever was tried last.
    g_app.layoutRequested = false;
    g_app.layoutSatisfied = false;

    const ime::State state = ime::query_state(hwnd);
    g_app.observedMode = state.mode;

    diag::write(L"context: %s | rule %s | mode %s | ime %s",
                window_identity(hwnd, g_app.observedExecutable).c_str(),
                g_app.ruleLanguage == 0 ? L"none"
                                        : layout::describe(g_app.ruleLanguage).c_str(),
                ime::mode_name(state.mode),
                state.valid ? L"readable" : L"unreadable");

    if (g_app.ruleLanguage != 0) {
        // A rule needs enforcing even when there is no mode to restore yet.
        g_app.restoreAttempt = 0;
        schedule_restore_attempt(hwnd);
        return;
    }

    if (!g_app.persistMode) {
        // Bindings are handled above; without one there is nothing left to do.
        cancel_restore();
        return;
    }

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
    const ime::Mode desired = g_app.persistMode ? g_app.desiredMode : ime::Mode::Unknown;
    if (!hwnd || !IsWindow(hwnd)) {
        cancel_restore();
        return;
    }
    if (hwnd != GetForegroundWindow()) {
        // Focus moved on while we were waiting; the switch to the new window
        // schedules its own restore.
        cancel_restore();
        return;
    }

    // The layout comes first: the conversion mode belongs to whichever layout is
    // active, so restoring the mode before the bound layout is in place would
    // write it to the layout on its way out.
    if (g_app.ruleLanguage != 0) {
        HKL required = layout::find_by_language(g_app.ruleLanguage);
        if (!required) {
            // The rule names a layout that is no longer installed. Nothing to
            // enforce, and retrying cannot help.
            diag::write(L"layout: rule names language 0x%04X, which is not installed",
                        g_app.ruleLanguage);
            g_app.ruleLanguage = 0;
        } else if (layout::current(hwnd) == required) {
            if (g_app.layoutRequested && !g_app.layoutSatisfied) {
                diag::write(L"layout: satisfied via %s",
                            layout::method_name(g_app.layoutMethod));
            }
            g_app.layoutSatisfied = true;
        } else {
            // Each attempt escalates, because no one mechanism reaches every
            // application. The last is repeated rather than adding a fourth,
            // since a target that ignored all three will keep ignoring them.
            static constexpr layout::Method kMethods[] = {
                layout::Method::FocusWindow,
                layout::Method::ThreadWindows,
                layout::Method::TsfSession,
                layout::Method::TsfSession,
            };
            const int attempt = g_app.restoreAttempt > 0 ? g_app.restoreAttempt - 1 : 0;
            const size_t index = static_cast<size_t>(attempt) < ARRAYSIZE(kMethods)
                                     ? static_cast<size_t>(attempt)
                                     : ARRAYSIZE(kMethods) - 1;

            // A target whose executable could not be read is a protected process,
            // which in practice means anti-cheat. Posting window messages into one
            // is contact it never has to tolerate, and it would repeat on every
            // switch into that application -- while the framework route reaches it
            // without touching it at all. So go straight there.
            const bool protectedTarget = g_app.observedExecutable.empty();
            g_app.layoutMethod =
                protectedTarget ? layout::Method::TsfSession : kMethods[index];
            g_app.layoutRequested = true;
            g_app.layoutSatisfied = false;
            diag::write(L"layout: want %s, have %s, attempt %d via %s%s",
                        layout::describe(g_app.ruleLanguage).c_str(),
                        layout::describe(layout::language_of(layout::current(hwnd))).c_str(),
                        attempt + 1,
                        layout::method_name(g_app.layoutMethod),
                        protectedTarget ? L" (protected target)" : L"");
            layout::request(hwnd, required, g_app.layoutMethod);

            // Every mechanism is asynchronous or unverifiable, so come back and
            // read the layout rather than assuming anything.
            schedule_restore_attempt(hwnd);
            return;
        }
    }

    if (desired == ime::Mode::Unknown) {
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
    diag::write(L"mode: wanted %s, was %s, now %s",
                ime::mode_name(desired), ime::mode_name(before.mode),
                ime::mode_name(after.mode));
    if (after.valid && after.mode == desired) {
        accept_restored_state(hwnd, after);
        return;
    }

    schedule_restore_attempt(hwnd);
}

void observe_tick() {
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd || own_window(hwnd) || ghost_window(hwnd)) {
        // Leaves the last real application's context in place, so neither our own
        // dialogs nor a dismissed Start menu looks like a context switch.
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

    record_snapshot(hwnd);
    update_tooltip(hwnd);

    const ULONGLONG now = GetTickCount64();
    const bool settling = now < g_app.suppressPromotionUntil ||
                          now - g_app.contextSince < kPromotionDwellMs ||
                          g_app.pendingWindow != nullptr;

    if (g_app.persistMode && !settling &&
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
    if (g_app.persistMode && !settling &&
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

// Elevation cannot be added to a running process, so this hands over to a fresh
// one. Offered because the choice made at install time should not be permanent:
// someone who installed unelevated still needs a way to reach an elevated game.
void restart_elevated() {
    wchar_t path[1024]{};
    if (GetModuleFileNameW(nullptr, path, ARRAYSIZE(path)) == 0) {
        return;
    }

    // Released first: the new copy checks this mutex while starting and would exit
    // immediately if this one still held it.
    if (g_app.singleInstance) {
        CloseHandle(g_app.singleInstance);
        g_app.singleInstance = nullptr;
    }

    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.lpVerb = L"runas";
    execute.lpFile = path;
    execute.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&execute)) {
        DestroyWindow(g_app.hwnd);
        return;
    }

    // Declined at the UAC prompt, so take the mutex back and carry on unelevated
    // rather than leaving the instance unguarded.
    g_app.singleInstance = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
}

// Which mechanism is configured, not merely whether one is: an elevated copy and an
// unelevated one need different ones, so "on" alone would not say whether autostart
// will actually reproduce the current privileges.
const wchar_t* autostart_label() {
    const text::Strings& t = text::s();
    switch (autostart::current()) {
    case autostart::Kind::ScheduledTask: return t.autostartTask;
    case autostart::Kind::Registry: return t.autostartRegistry;
    default: return t.autostartOff;
    }
}

void show_status() {
    // Every line comes from the snapshot. Reading the live foreground here would
    // describe the shell, because opening this box is what put it in front.
    const text::Strings& t = text::s();

    wchar_t attempt[256]{};
    if (!g_app.snapshotRequested) {
        StringCchCopyW(attempt, ARRAYSIZE(attempt), t.switchNotAttempted);
    } else {
        StringCchPrintfW(attempt, ARRAYSIZE(attempt),
                         g_app.snapshotSatisfied ? t.switchOk : t.switchFailed,
                         layout::method_name(g_app.snapshotMethod));
    }

    wchar_t body[1280]{};
    StringCchPrintfW(
        body,
        ARRAYSIZE(body),
        t.statusFormat,
        mode_label(g_app.desiredMode),
        mode_label(g_app.snapshotMode),
        g_app.snapshotReachable ? t.yes : t.no,
        g_app.snapshotApp.empty() ? t.unknownApplication : g_app.snapshotApp.c_str(),
        g_app.snapshotRule == 0 ? t.noRule : layout::describe(g_app.snapshotRule).c_str(),
        g_app.snapshotLayout == 0 ? t.unknownApplication
                                  : layout::describe(g_app.snapshotLayout).c_str(),
        attempt,
        autostart::elevated() ? t.elevatedYes : t.elevatedNo,
        autostart_label());
    show_message(t.statusTitle, body, false);
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
                AppendMenuW(
                    menu,
                    MF_STRING | (autostart::is_enabled() ? MF_CHECKED : MF_UNCHECKED),
                    ID_TRAY_AUTOSTART,
                    text::s().menuAutostart);
                AppendMenuW(
                    menu,
                    MF_STRING | (g_app.persistMode ? MF_CHECKED : MF_UNCHECKED),
                    ID_TRAY_PERSIST,
                    text::s().menuPersist);
                AppendMenuW(menu, MF_STRING, ID_TRAY_RULES, text::s().menuRules);
                if (!autostart::elevated()) {
                    // Hidden rather than greyed when already elevated: a disabled
                    // item invites the question of how to enable it.
                    AppendMenuW(menu, MF_STRING, ID_TRAY_ELEVATE, text::s().menuElevate);
                }
                if (!diag::path().empty()) {
                    // Only when there is a file to open, so the menu never offers
                    // something that does nothing.
                    AppendMenuW(menu, MF_STRING, ID_TRAY_LOG, text::s().menuLog);
                }
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, text::s().menuExit);
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
        if (LOWORD(wParam) == ID_TRAY_AUTOSTART) {
            diag::write(L"user: autostart -> %s", autostart::is_enabled() ? L"off" : L"on");
            if (!autostart::set_enabled(!autostart::is_enabled())) {
                show_error(text::s().errorAutostart);
            }
            return 0;
        }
        if (LOWORD(wParam) == ID_TRAY_PERSIST) {
            g_app.persistMode = !g_app.persistMode;
            settings::set_persist_mode(g_app.persistMode);
            diag::write(L"user: persist mode -> %s", g_app.persistMode ? L"on" : L"off");

            // Forget the target either way: leaving a stale one would show a
            // desired mode that is not being applied, and re-enabling should pick
            // up whatever the user is doing now rather than something from before.
            g_app.desiredMode = ime::Mode::Unknown;
            cancel_restore();
            return 0;
        }
        if (LOWORD(wParam) == ID_TRAY_LOG) {
            const std::wstring file = diag::path();
            if (!file.empty()) {
                ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }
        if (LOWORD(wParam) == ID_TRAY_ELEVATE) {
            diag::write(L"user: restarting elevated");
            restart_elevated();
            return 0;
        }
        if (LOWORD(wParam) == ID_TRAY_RULES) {
            config::show_rules(
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                hwnd,
                // snapshotApp, not the live context: opening this dialog goes
                // through the tray icon, so the live context is the shell.
                g_app.snapshotApp,
                g_app.snapshotClass);

            // Rules may have changed, so re-evaluate the application that is in
            // the foreground once the dialog closes.
            g_app.observedThread = 0;
            g_app.observedLayout = nullptr;
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
    // The manifest activates ComCtl32 version 6; this loads it and registers the
    // classes. Per-Monitor V2 awareness comes from the manifest too, so there is
    // no DPI call to make here.
    // Before any window exists, because the framework wants COM on this thread.
    // A failure is not fatal: the other switching mechanisms still work.
    g_app.persistMode = settings::persist_mode();

    diag::initialise();
    diag::write(L"---- started, version %hs, %s, autostart %s, persist mode %s",
                APP_VERSION_STRING,
                autostart::elevated() ? L"elevated" : L"not elevated",
                autostart_label(),
                g_app.persistMode ? L"on" : L"off");

    if (!tsf::initialise()) {
        // Not fatal, but it removes the one mechanism that reaches a protected
        // process, so it is worth knowing when a binding mysteriously stops.
        diag::write(L"TSF initialisation failed; the session-level switch is unavailable");
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    // Held for the process lifetime; the OS releases it on exit.
    g_app.singleInstance = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (!g_app.singleInstance || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hIcon = load_app_icon(instance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));

    if (!RegisterClassW(&wc)) {
        return 1;
    }

    g_app.trayIcon = load_app_icon(
        instance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));

    // A hidden top-level window rather than an HWND_MESSAGE one. Message-only
    // windows are children of HWND_MESSAGE, so they are invisible to
    // FindWindow and never receive WM_CLOSE or WM_QUERYENDSESSION. The installer
    // needs to ask this process to exit before it can replace a running
    // executable, and being unreachable is what forced the user to close it by
    // hand. WS_EX_TOOLWINDOW plus never calling ShowWindow keeps it out of the
    // taskbar and Alt-Tab, so it stays just as invisible as before.
    g_app.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, kClassName, L"ImeModePersistence", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
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
        show_error(text::s().errorHook);
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

    diag::write(L"---- exiting");
    diag::shutdown();
    tsf::shutdown();
    return static_cast<int>(msg.wParam);
}
