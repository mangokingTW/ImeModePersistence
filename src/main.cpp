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
#include "schedule.h"
#include "theme.h"
#include "tsf.h"

namespace {

constexpr UINT WMAPP_TRAY = WM_APP + 1;
constexpr UINT_PTR TIMER_RESTORE = 1;
constexpr UINT_PTR TIMER_OBSERVE = 2;
constexpr UINT_PTR TIMER_LAYOUT = 3;
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

// Reading the conversion mode is a cross-process SendMessage, and this tick does
// one every time. That cost is what sets this interval: polling the mode several
// times faster would mean several times as many messages into whatever is in
// front, which for an anti-cheat-protected game is traffic worth not generating.
constexpr UINT kObserveIntervalMs = 50;

// The layout, by contrast, is a local read -- GetKeyboardLayout asks the window
// manager about a thread and sends nothing to it. So the layout a rule binds can
// be checked far more often than the mode, and this is what decides how long an
// unwanted switch survives before it is put back.
constexpr UINT kLayoutPollIntervalMs = 15;

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

    // What began the current round, which decides how long its waits are.
    schedule::Trigger trigger{schedule::Trigger::FocusChange};

    // The layout the active rule wants and the window it was resolved for,
    // both settled once when the rule is looked up. The fast poll compares
    // against these instead of re-deriving them, which is what keeps it cheap
    // enough to run every 15 ms.
    HKL requiredLayout{};
    HWND ruleWindow{};

    // Set when a round gave up, so a target that refuses is left alone rather
    // than asked again on the very next poll.
    ULONGLONG layoutCooldownUntil{};

    // The class of the window the context was last keyed on, kept so a context
    // switch can recognise "same application, same rule" -- the executable is the
    // usual identity, but a protected process yields none and the class is all
    // there is.
    std::wstring observedWindowClass;

    // Consecutive rounds lost against the current target. Drives the cooldown's
    // back-off, so an application that always wins is asked ever less often.
    int lostRounds{};

    // Bumped by every context switch. Reading another process's IME state blocks
    // inside SendMessageTimeoutW, and an out-of-context WinEvent callback can run
    // note_context_switch during that wait -- so any function that blocked
    // compares this afterwards to learn whether the world it captured still
    // exists, instead of overwriting the newer round's state with stale data.
    unsigned contextGeneration{};

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
    // It reports this snapshot of the last real application instead. One struct,
    // so adding a piece of foreground state is one field and one assignment
    // rather than three coordinated edits.
    struct Snapshot {
        std::wstring app;
        std::wstring windowClass;
        ime::Mode mode{ime::Mode::Unknown};
        bool reachable{false};
        LANGID rule{};
        LANGID layout{};
        layout::Method method{layout::Method::FocusWindow};
        bool requested{false};
        bool satisfied{false};
    };
    Snapshot snapshot;

    // What autostart is configured to right now. Cached because reading it means
    // running schtasks.exe, and the tray menu used to do that synchronously on
    // the UI thread just to draw one checkmark. Refreshed when this process
    // changes it; a change made behind its back (Task Scheduler by hand) is
    // noticed at the next start.
    autostart::Kind autostartKind{autostart::Kind::None};

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

// Takes the state rather than querying it: both callers just paid for a fresh
// cross-process read, and this runs on the 50 ms hot path.
void record_snapshot(HWND hwnd, const ime::State& state) {
    if (!hwnd || own_window(hwnd) || shell_window(hwnd) ||
        shell_ui(g_app.observedExecutable)) {
        return;
    }

    const HKL current = layout::current(hwnd);

    g_app.snapshot.app = window_identity(hwnd, g_app.observedExecutable);
    g_app.snapshot.windowClass = rules::window_class_of(hwnd);
    g_app.snapshot.mode = state.mode;
    g_app.snapshot.reachable = state.valid;
    g_app.snapshot.rule = g_app.ruleLanguage;
    g_app.snapshot.layout = current ? layout::language_of(current) : 0;
    g_app.snapshot.method = g_app.layoutMethod;
    g_app.snapshot.requested = g_app.layoutRequested;
    g_app.snapshot.satisfied = g_app.layoutSatisfied;
}

// One rendering of what the last switch attempt did, shared by the tooltip and
// the status box. The two used to carry hand-copied versions that had already
// drifted in buffer size; any new outcome state now has one place to go.
void compose_attempt(wchar_t* out, size_t count, bool requested, bool satisfied,
                     layout::Method method) {
    const text::Strings& t = text::s();
    if (!requested) {
        StringCchCopyW(out, count, t.switchNotAttempted);
    } else {
        StringCchPrintfW(out, count, satisfied ? t.switchOk : t.switchFailed,
                         layout::method_name(method));
    }
}

// Hovering the tray icon does not change the foreground window, which is what
// makes the tooltip the one place a diagnostic can be read without disturbing
// the thing being diagnosed.
void update_tooltip(HWND hwnd) {
    const text::Strings& t = text::s();

    const HKL current = hwnd ? layout::current(hwnd) : nullptr;

    // Twenty times a second, in a steady state where nothing changes, composing
    // the tooltip means two locale lookups, a window-class read and several
    // allocations -- all discarded against the cached string at the end. The
    // inputs are trivially comparable, so compare those and skip the rest.
    struct Inputs {
        HWND hwnd;
        HKL layout;
        LANGID rule;
        bool requested;
        bool satisfied;
        layout::Method method;
        bool operator==(const Inputs&) const = default;
    };
    static Inputs last{};
    const Inputs inputs{hwnd,
                        current,
                        g_app.ruleLanguage,
                        g_app.layoutRequested,
                        g_app.layoutSatisfied,
                        g_app.layoutMethod};
    if (inputs == last) {
        return;
    }
    last = inputs;

    const std::wstring identity =
        hwnd ? window_identity(hwnd, g_app.observedExecutable) : std::wstring{};

    wchar_t attempt[128]{};
    compose_attempt(attempt, ARRAYSIZE(attempt), g_app.layoutRequested,
                    g_app.layoutSatisfied, g_app.layoutMethod);

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
    if (g_app.restoreAttempt >= schedule::max_attempts()) {
        // Out of attempts. Adopt whatever the target settled on so the next
        // observation does not read the difference as a user decision.
        diag::write(L"gave up after %d attempts; adopting the target's own state",
                    schedule::max_attempts());

        if (g_app.ruleLanguage != 0 && !g_app.layoutSatisfied) {
            // The cooldown belongs to the faster poll rather than to this budget.
            // Without it, an application that insists on its own layout would be
            // sent a fresh round every 15 ms for as long as it stayed in front:
            // the argument is already lost, and continuing it only floods a
            // target that may well be an anti-cheat-protected game.
            //
            // Only when the layout itself was refused. A protected target's
            // conversion mode is typically unreadable, so its rounds routinely
            // exhaust the budget on the mode with the layout satisfied on the
            // first attempt -- punishing the fast poll for that would disable it
            // in exactly the scenario it was built for.
            //
            // Doubling per lost round, so a target that always wins is asked --
            // and its victory logged -- ever less often, instead of seven lines
            // every few seconds for as long as it holds the foreground.
            ++g_app.lostRounds;
            const UINT cooldown = schedule::cooldown_ms(g_app.lostRounds);
            g_app.layoutCooldownUntil = GetTickCount64() + cooldown;
            diag::write(L"layout: leaving this target alone for %u ms (%d rounds lost)",
                        cooldown, g_app.lostRounds);
        }

        const ime::Mode settled = ime::query_state(hwnd).mode;
        if (g_app.pendingWindow != hwnd) {
            // A context switch re-keyed everything while the read above was
            // blocked; the state now describes the new round, not this one.
            return;
        }
        g_app.observedMode = settled;
        cancel_restore();
        return;
    }

    const UINT delay = schedule::delay_for(g_app.restoreAttempt, g_app.trigger);
    ++g_app.restoreAttempt;
    g_app.pendingWindow = hwnd;
    KillTimer(g_app.hwnd, TIMER_RESTORE);
    SetTimer(g_app.hwnd, TIMER_RESTORE, delay, nullptr);
}

void accept_restored_state(HWND hwnd, const ime::State& state) {
    record_snapshot(hwnd, state);
    g_app.observedThread = GetWindowThreadProcessId(hwnd, nullptr);
    g_app.observedLayout = GetKeyboardLayout(g_app.observedThread);
    g_app.observedMode = state.mode;
    g_app.contextSince = GetTickCount64();
    g_app.suppressPromotionUntil = g_app.contextSince + kPostRestoreSuppressMs;
    cancel_restore();
}

// The 15 ms poll exists to guard an active binding; with no rule resolved for
// the foreground window it would wake the process sixty-six times a second to
// check a null. Started and stopped as bindings come and go with the foreground.
void update_layout_timer() {
    if (g_app.ruleWindow && g_app.ruleLanguage != 0) {
        SetTimer(g_app.hwnd, TIMER_LAYOUT, kLayoutPollIntervalMs, nullptr);
    } else {
        KillTimer(g_app.hwnd, TIMER_LAYOUT);
    }
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

    const unsigned generation = ++g_app.contextGeneration;

    const std::wstring executable = rules::executable_of(hwnd);
    const std::wstring windowClass = rules::window_class_of(hwnd);
    const LANGID rule = rules::lookup(executable, windowClass);

    // The same application under the same rule, seen again. Chromium-style
    // applications move the foreground between their own threads constantly, and
    // every move lands here: treating each as a brand-new context restarted the
    // escalation at the first mechanism every time, so a target that ignores
    // that mechanism was asked with it forever -- the ladder never climbed. (A
    // user's diagnostic log showed exactly this: "attempt 1" repeating for as
    // long as Chrome held the foreground.) A continuation keeps the attempt
    // counter, the cooldown and the lost-round history; a genuine change of
    // application resets them all.
    //
    // Identity is the executable when it can be read; a protected process yields
    // none, and there the window class is what a rule matched anyway.
    const bool continuation =
        rule != 0 && rule == g_app.ruleLanguage &&
        (!executable.empty() ? executable == g_app.observedExecutable
                             : (!windowClass.empty() &&
                                windowClass == g_app.observedWindowClass));

    g_app.observedThread = thread;
    g_app.observedLayout = GetKeyboardLayout(thread);
    g_app.contextSince = GetTickCount64();

    g_app.observedExecutable = executable;
    g_app.observedWindowClass = windowClass;
    g_app.ruleLanguage = rule;

    // Resolved here rather than on every attempt: this is the one place the rule
    // itself can change, and the fast poll needs an answer it can compare against
    // without going back to the registry or enumerating layouts.
    g_app.requiredLayout =
        g_app.ruleLanguage != 0 ? layout::find_by_language(g_app.ruleLanguage) : nullptr;
    g_app.ruleWindow = g_app.requiredLayout ? hwnd : nullptr;
    update_layout_timer();

    if (!continuation) {
        // A genuine context change clears the fight's whole memory: whatever was
        // being argued about belonged to the application being left, and
        // switching away and back is the documented way to make a binding try
        // again. A continuation keeps all of it, or an application could reset
        // its own cooldown just by moving focus between its threads.
        g_app.layoutCooldownUntil = 0;
        g_app.lostRounds = 0;
        g_app.trigger = schedule::Trigger::FocusChange;

        // Reset per-application, so the status box describes the application in
        // front rather than whatever was tried last.
        g_app.layoutRequested = false;
        g_app.layoutSatisfied = false;
    }

    const ime::State state = ime::query_state(hwnd);
    if (g_app.contextGeneration != generation) {
        // The read blocked and the foreground moved on; a nested call has already
        // recorded the newer context and scheduled its round.
        return;
    }
    g_app.observedMode = state.mode;

    // write_once: this describes a situation, and the same few applications are
    // switched between all day. The lines that follow describe events and are not
    // deduplicated.
    diag::write_once(L"context: %s | rule %s | mode %s | ime %s",
                window_identity(hwnd, g_app.observedExecutable).c_str(),
                g_app.ruleLanguage == 0 ? L"none"
                                        : layout::describe(g_app.ruleLanguage).c_str(),
                ime::mode_name(state.mode),
                state.valid ? L"readable" : L"unreadable");

    if (g_app.ruleLanguage != 0) {
        if (continuation && GetTickCount64() < g_app.layoutCooldownUntil) {
            // Still backing off from this application; its internal focus moves
            // do not reopen the argument. The fast poll resumes when the
            // cooldown ends.
            return;
        }

        // A rule needs enforcing even when there is no mode to restore yet. A
        // continuation picks the ladder up where the previous window of the same
        // application left it, instead of starting over at the first mechanism.
        if (!continuation) {
            g_app.restoreAttempt = 0;
        }
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
        // Re-resolved on every attempt, not read from the cache: attempts only
        // run while something is wrong, and this is where a rule naming a layout
        // the user has since removed gets noticed and cleanly disabled. The cache
        // exists for the 15 ms poll, so it is refreshed here rather than trusted.
        HKL required = layout::find_by_language(g_app.ruleLanguage);
        g_app.requiredLayout = required;
        if (!required) {
            // The rule names a layout that is no longer installed. Nothing to
            // enforce, and retrying cannot help.
            diag::write(L"layout: rule names language 0x%04X, which is not installed",
                        g_app.ruleLanguage);
            g_app.ruleLanguage = 0;
            g_app.ruleWindow = nullptr;
            update_layout_timer();
        } else if (layout::language_of(layout::current(hwnd)) == g_app.ruleLanguage) {
            // Compared by language, not by HKL. The rule stores a LANGID, so any
            // layout of that language satisfies it -- a user who switches from
            // QWERTY to Dvorak inside a bound application has not left English,
            // and reverting that choice would be enforcing something no rule says.
            if (g_app.layoutRequested && !g_app.layoutSatisfied) {
                diag::write(L"layout: satisfied via %s",
                            layout::method_name(g_app.layoutMethod));
            }
            g_app.layoutSatisfied = true;

            // One won round ends the back-off: the fight is over, so the next
            // disagreement starts from the short cooldown again.
            g_app.lostRounds = 0;

            // Adopt what is actually active (not the HKL we asked for -- a
            // same-language variant also satisfies), so the next observe_tick does
            // not read our own success as a layout change and start over.
            g_app.observedLayout = layout::current(hwnd);
        } else {
            // Each attempt escalates, because no one mechanism reaches every
            // application. The order itself lives in layout::method_for_attempt so
            // that "every target gets the whole sequence" is a testable claim
            // rather than a comment; see the note there on how it was once broken.
            const int attempt = g_app.restoreAttempt > 0 ? g_app.restoreAttempt - 1 : 0;
            g_app.layoutMethod = layout::method_for_attempt(attempt);
            g_app.layoutRequested = true;
            g_app.layoutSatisfied = false;

            // Whether the call itself succeeded, so a mechanism that reports success
            // and changes nothing can be told from one that was refused outright.
            const bool issued = layout::request(hwnd, required, g_app.layoutMethod);
            diag::write(L"layout: want %s, have %s, attempt %d via %s (%s)",
                        layout::describe(g_app.ruleLanguage).c_str(),
                        layout::describe(layout::language_of(layout::current(hwnd))).c_str(),
                        attempt + 1,
                        layout::method_name(g_app.layoutMethod),
                        issued ? L"issued" : L"refused");

            if (g_app.pendingWindow != hwnd) {
                // The TSF path can pump messages; if a context switch re-keyed the
                // round meanwhile, the newer round owns the timer.
                return;
            }

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
    if (g_app.pendingWindow != hwnd) {
        // The cross-process read blocked long enough for a foreground change to
        // re-key the round; scheduling from here would clobber the new one.
        return;
    }
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
    if (g_app.pendingWindow != hwnd) {
        return;
    }
    diag::write(L"mode: wanted %s, was %s, now %s",
                ime::mode_name(desired), ime::mode_name(before.mode),
                ime::mode_name(after.mode));
    if (after.valid && after.mode == desired) {
        accept_restored_state(hwnd, after);
        return;
    }

    schedule_restore_attempt(hwnd);
}

// The layout half of the observation, run far more often than the rest.
//
// It exists because the two things being watched cost different amounts. Reading
// the conversion mode means a cross-process SendMessage; reading the layout does
// not. So this checks only the layout, only for the window a rule was already
// resolved for, and deliberately re-derives nothing -- no process identity, no
// registry lookup, no window vetting, all of which observe_tick and the foreground
// hook have already done for this window. That is what makes 15 ms affordable.
//
// The effect is on how long an unwanted switch lasts: pressing Win+Space in a
// bound application used to survive the 50 ms observer plus a 60 ms first attempt,
// and now survives this poll plus a 10 ms one.
void layout_tick() {
    if (g_app.ruleLanguage == 0 || !g_app.requiredLayout || !g_app.ruleWindow) {
        return;
    }

    // A round is already in flight. It has its own timer, its own escalation and
    // its own budget, and starting a second would reset all three.
    if (g_app.pendingWindow) {
        return;
    }

    if (GetTickCount64() < g_app.layoutCooldownUntil) {
        return;
    }

    // Only the window the rule was resolved for. Anything else is a context
    // change, which the foreground hook and observe_tick own.
    const HWND hwnd = GetForegroundWindow();
    if (hwnd != g_app.ruleWindow) {
        return;
    }

    // By language, not by HKL: the rule stores a LANGID, so a same-language
    // variant (Dvorak against a QWERTY-resolved rule, a second IME of the same
    // language) already satisfies it, and reverting the user's pick between them
    // would enforce a distinction no rule can express.
    const HKL actual = layout::current(hwnd);
    if (layout::language_of(actual) == g_app.ruleLanguage) {
        return;
    }

    // An event rather than a situation, so not deduplicated: how often a binding
    // has to be reasserted is exactly what a report needs to show.
    diag::write(L"layout: drifted to %s, reasserting %s",
                layout::describe(layout::language_of(actual)).c_str(),
                layout::describe(g_app.ruleLanguage).c_str());

    g_app.trigger = schedule::Trigger::LayoutDrift;
    g_app.restoreAttempt = 0;
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

    if (thread != g_app.observedThread) {
        note_context_switch(hwnd);
        return;
    }

    const HKL layoutNow = GetKeyboardLayout(thread);
    if (layoutNow != g_app.observedLayout) {
        // A layout change inside the same thread. In a bound window this is
        // drift, and drift belongs to layout_tick -- it has the 15 ms timer, the
        // in-flight round and the cooldown. Treating it as a context switch here
        // used to kill the round mid-escalation, restart the budget and wipe the
        // cooldown, so the observer defeated the machinery built for exactly this
        // case. Adopt the layout so this branch does not re-fire, and leave the
        // response to the fast poll.
        if (hwnd == g_app.ruleWindow && g_app.ruleLanguage != 0) {
            g_app.observedLayout = layoutNow;

            // The conversion mode lives on (thread, layout), so whatever was
            // observed on the old layout describes nothing now.
            g_app.observedMode = ime::Mode::Unknown;
            g_app.contextSince = GetTickCount64();
            return;
        }

        // Without a rule it is a system event -- the user switching layouts by
        // hand -- and re-keying is the correct response.
        note_context_switch(hwnd);
        return;
    }

    const unsigned generation = g_app.contextGeneration;
    const ime::State state = ime::query_state(hwnd);
    if (g_app.contextGeneration != generation) {
        // The read blocked across a context switch; everything below would mix
        // the old window's identity with the new one's state.
        return;
    }

    // Recorded before the validity check, not after: a window bound to a non-IME
    // layout (English -- the headline use of bindings) never has a valid IME
    // state, and gating the snapshot on validity froze the tooltip and status box
    // on the previous IME-capable application for exactly those windows.
    record_snapshot(hwnd, state);
    update_tooltip(hwnd);

    if (!state.valid) {
        g_app.observedMode = ime::Mode::Unknown;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const bool settling = now < g_app.suppressPromotionUntil ||
                          now - g_app.contextSince < kPromotionDwellMs ||
                          g_app.pendingWindow != nullptr;

    if (g_app.persistMode && !settling &&
        g_app.observedMode == ime::Mode::Unknown &&
        g_app.desiredMode != ime::Mode::Unknown &&
        state.mode != g_app.desiredMode) {
        // The context became readable only after the restore attempts ran out,
        // so start a fresh round now that there is something to write to. This is
        // a mode restore against an IME that only just became readable, so it
        // gets the focus-change schedule: a stale LayoutDrift trigger from an
        // earlier round would fire the first write at 10 ms into an IME still
        // settling, spending an attempt for nothing.
        g_app.trigger = schedule::Trigger::FocusChange;
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
    // autostart::module_path grows its buffer until the path fits; the fixed
    // 1024-char copy this replaced silently truncated long install paths, and a
    // truncated path here means relaunching some other executable.
    const std::wstring path = autostart::module_path();
    if (path.empty()) {
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
    execute.lpFile = path.c_str();
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
// will actually reproduce the current privileges. Reads the cache: the real answer
// costs a schtasks run, which is queried once at startup and after every change
// this process makes, never on the UI thread for a menu or a status box.
const wchar_t* autostart_label() {
    const text::Strings& t = text::s();
    switch (g_app.autostartKind) {
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
    compose_attempt(attempt, ARRAYSIZE(attempt), g_app.snapshot.requested,
                    g_app.snapshot.satisfied, g_app.snapshot.method);

    wchar_t body[1280]{};
    StringCchPrintfW(
        body,
        ARRAYSIZE(body),
        t.statusFormat,
        mode_label(g_app.desiredMode),
        mode_label(g_app.snapshot.mode),
        g_app.snapshot.reachable ? t.yes : t.no,
        g_app.snapshot.app.empty() ? t.unknownApplication : g_app.snapshot.app.c_str(),
        g_app.snapshot.rule == 0 ? t.noRule : layout::describe(g_app.snapshot.rule).c_str(),
        g_app.snapshot.layout == 0 ? t.unknownApplication
                                   : layout::describe(g_app.snapshot.layout).c_str(),
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
        } else if (wParam == TIMER_LAYOUT) {
            layout_tick();
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
                    // The cached kind: reading the real state runs schtasks.exe,
                    // and blocking the UI thread up to ten seconds to draw a
                    // checkmark is how right-click used to freeze the tray.
                    MF_STRING | (g_app.autostartKind != autostart::Kind::None
                                     ? MF_CHECKED
                                     : MF_UNCHECKED),
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
            const bool enable = g_app.autostartKind == autostart::Kind::None;
            diag::write(L"user: autostart -> %s", enable ? L"on" : L"off");
            if (autostart::set_enabled(enable)) {
                // Derived rather than re-queried: set_enabled succeeded, so the
                // outcome is known and the schtasks round-trip is unnecessary.
                g_app.autostartKind =
                    !enable ? autostart::Kind::None
                            : (autostart::elevated() ? autostart::Kind::ScheduledTask
                                                     : autostart::Kind::Registry);
            } else {
                show_error(text::s().errorAutostart);
                // Failure leaves the real state unknown; one query on an explicit
                // user action is acceptable where one per menu-open was not.
                g_app.autostartKind = autostart::current();
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
                // The snapshot, not the live context: opening this dialog goes
                // through the tray icon, so the live context is the shell.
                g_app.snapshot.app,
                g_app.snapshot.windowClass);

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
        KillTimer(hwnd, TIMER_LAYOUT);
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

    // Primed before anything reads autostart_label: the cache is the only thing
    // the UI consults, and this one schtasks run at startup is what pays for the
    // menu and status box never blocking on one again.
    g_app.autostartKind = autostart::current();

    diag::initialise();
    diag::write(L"---- started, version %hs, %s, autostart %s, persist mode %s, "
                L"layout poll %u ms",
                APP_VERSION_STRING,
                autostart::elevated() ? L"elevated" : L"not elevated",
                autostart_label(),
                g_app.persistMode ? L"on" : L"off",
                kLayoutPollIntervalMs);

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

    // TIMER_LAYOUT is deliberately not started here: update_layout_timer starts
    // it when a binding is actually resolved for the foreground window, and the
    // note_context_switch call above has already done that if one applies.
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
