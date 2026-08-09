#include "check.h"

#include <algorithm>
#include <set>
#include <string>

#include "layout.h"
#include "rules.h"

// Tests that need a real window and a real keyboard layout rather than a pure
// function. They cover the mechanism against the most cooperative target there is
// -- a window in this process using DefWindowProc -- which is a long way from an
// anti-cheat-protected fullscreen game, and that gap is the honest limit of what
// any test here can reach. What they do establish is that the window-message
// route works at all, which is the claim that was wrong for two releases.
namespace {

constexpr wchar_t kClassName[] = L"ImeModePersistenceTestTarget";

HWND create_target() {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);

    // DefWindowProc deliberately: it is what handles WM_INPUTLANGCHANGEREQUEST,
    // and a window that intercepted the message would be testing the test.
    description.lpfnWndProc = DefWindowProcW;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = kClassName;

    if (RegisterClassExW(&description) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }

    return CreateWindowExW(0, kClassName, L"ImeModePersistence test target",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           320, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void pump() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// The mechanisms post rather than send, and DefWindowProc's handling is not
// instantaneous, so the result has to be waited for. Bounded, because a mechanism
// that does not work must fail rather than hang.
bool pump_until_layout(HWND hwnd, HKL wanted, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        pump();
        if (layout::current(hwnd) == wanted) {
            return true;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(10);
    }
}

std::wstring own_executable() {
    wchar_t path[1024]{};
    const DWORD written = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (written == 0 || written >= ARRAYSIZE(path)) {
        return {};
    }

    std::wstring result(path, written);
    CharLowerBuffW(result.data(), static_cast<DWORD>(result.size()));
    return result;
}

void identifies_a_window_by_class(HWND hwnd) {
    // Lower-cased, because that is the form rules are stored in; a mismatch here
    // is exactly how a class rule silently never matches.
    std::wstring expected = kClassName;
    CharLowerBuffW(expected.data(), static_cast<DWORD>(expected.size()));

    const std::wstring actual = rules::window_class_of(hwnd);
    CHECK_MSG(actual == expected, "window_class_of gave \"%s\", expected \"%s\"",
              check::utf8(actual).c_str(), check::utf8(expected).c_str());

    // A destroyed window yields nothing rather than stale text, so the caller can
    // tell "unreadable" from "read successfully as empty".
    CHECK(rules::window_class_of(nullptr).empty());
}

void identifies_a_window_by_executable(HWND hwnd) {
    const std::wstring expected = own_executable();
    if (expected.empty()) {
        check::skip("could not read this process's own image path");
        return;
    }

    const std::wstring actual = rules::executable_of(hwnd);
    CHECK_MSG(actual == expected, "executable_of gave \"%s\", expected \"%s\"",
              check::utf8(actual).c_str(), check::utf8(expected).c_str());

    // The failure that matters is this one returning empty: it is how a protected
    // process presents itself, and it is what makes class rules necessary.
    CHECK(rules::executable_of(nullptr).empty());

    // And a rule keyed on the bare file name has to match the full path this
    // returns, which is the pairing the config dialog relies on.
    CHECK(!rules::file_name_of(actual).empty());
    CHECK(rules::file_name_of(actual) != actual);
}

void reads_the_active_layout(HWND hwnd) {
    const HKL direct = GetKeyboardLayout(GetCurrentThreadId());
    CHECK_MSG(layout::current(hwnd) == direct,
              "layout::current disagreed with GetKeyboardLayout for our own thread");

    CHECK(layout::current(nullptr) == nullptr);
}

void lists_installed_layouts_once_per_language() {
    const std::vector<layout::Installed> all = layout::installed();
    CHECK_MSG(!all.empty(), "no keyboard layouts reported at all");

    // One entry per language is the contract, because rules are keyed by LANGID
    // and a second entry for the same language would offer the user a distinction
    // the rules cannot store.
    std::set<LANGID> languages;
    for (const layout::Installed& entry : all) {
        CHECK(entry.language != 0);
        CHECK(entry.hkl != nullptr);
        CHECK(!entry.name.empty());
        CHECK_MSG(languages.insert(entry.language).second,
                  "language 0x%04X listed more than once",
                  static_cast<unsigned>(entry.language));
    }

    // Whatever this thread is using now must be among them, or a rule naming the
    // current layout would report it as uninstalled.
    const LANGID active = layout::language_of(GetKeyboardLayout(GetCurrentThreadId()));
    CHECK_MSG(languages.count(active) == 1, "the active language 0x%04X was not listed",
              static_cast<unsigned>(active));

    CHECK(layout::find_by_language(active) != nullptr);

    // A rule naming a layout the user has since removed has to come back as
    // nothing, which is what stops the caller retrying forever.
    constexpr LANGID kUnassigned = 0x0EFF;
    constexpr LANGID kNone = 0;
    CHECK(layout::find_by_language(kUnassigned) == nullptr);
    CHECK(layout::find_by_language(kNone) == nullptr);
}

// A second layout to switch to. Loading one does not change any system setting
// and needs no elevation; it is added to this process only.
HKL load_second_layout(HKL current) {
    // en-GB first, then German and French. Any will do -- what matters is a LANGID
    // distinguishable from the one already active.
    const wchar_t* const candidates[] = {L"00000809", L"00000407", L"0000040C"};
    for (const wchar_t* identifier : candidates) {
        const HKL loaded = LoadKeyboardLayoutW(identifier, 0);
        if (loaded && loaded != current &&
            layout::language_of(loaded) != layout::language_of(current)) {
            return loaded;
        }
    }
    return nullptr;
}

void switches_a_real_window(HWND hwnd) {
    const HKL original = GetKeyboardLayout(GetCurrentThreadId());
    const HKL other = load_second_layout(original);
    if (!other) {
        check::skip("no second keyboard layout could be loaded");
        return;
    }

    // Whether this environment can change a thread's layout at all, established
    // before anything in layout.cpp is involved. A headless CI session is not
    // guaranteed to support it, and without this baseline a failure below would be
    // indistinguishable between "the mechanism is broken" and "there is no
    // interactive desktop" -- which is precisely the ambiguity that let the real
    // bug survive.
    const bool baseline = ActivateKeyboardLayout(other, 0) != nullptr &&
                          GetKeyboardLayout(GetCurrentThreadId()) == other;
    ActivateKeyboardLayout(original, 0);
    if (!baseline) {
        check::skip("this session cannot change a thread's keyboard layout");
        UnloadKeyboardLayout(other);
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    pump();

    const layout::Method messageMethods[] = {
        layout::Method::FocusWindow,
        layout::Method::ThreadWindows,
    };

    for (const layout::Method method : messageMethods) {
        ActivateKeyboardLayout(original, 0);
        CHECK(layout::current(hwnd) == original);

        const bool issued = layout::request(hwnd, other, method);
        CHECK_MSG(issued, "request via %s was refused outright",
                  check::utf8(layout::method_name(method)).c_str());

        const bool took = pump_until_layout(hwnd, other, 2000);
        CHECK_MSG(took, "request via %s was issued but the layout did not change",
                  check::utf8(layout::method_name(method)).c_str());
    }

    // TsfSession is deliberately not exercised. It moves the input language for
    // the whole session rather than this thread, so a test would change the
    // machine's state outside the process -- and the field evidence is already
    // that it does not reach the targets this feature exists for.

    ActivateKeyboardLayout(original, 0);
    UnloadKeyboardLayout(other);
    ShowWindow(hwnd, SW_HIDE);
}

void refuses_impossible_requests(HWND hwnd) {
    const HKL current = GetKeyboardLayout(GetCurrentThreadId());

    // Each of these has to be reported as refused rather than issued, so the log
    // distinguishes "the call was rejected" from "the call worked and the target
    // ignored it". That distinction is the one instrument for a target no test
    // can reach.
    CHECK(!layout::request(nullptr, current, layout::Method::FocusWindow));
    CHECK(!layout::request(hwnd, nullptr, layout::Method::FocusWindow));
    CHECK(!layout::request(nullptr, nullptr, layout::Method::ThreadWindows));

    HWND destroyed = create_target();
    if (destroyed) {
        DestroyWindow(destroyed);
        pump();
        CHECK(!layout::request(destroyed, current, layout::Method::FocusWindow));
    }
}

} // namespace

// Registered as two suites rather than one, so that the mutating half being
// skipped for want of an interactive desktop does not report the read-only half as
// skipped along with it.
void run_window_identity_tests() {
    HWND hwnd = create_target();
    if (!hwnd) {
        // Not a pass. If windows cannot be created here, everything below is
        // untested and the result has to say so.
        check::skip("could not create a test window in this session");
        return;
    }

    identifies_a_window_by_class(hwnd);
    identifies_a_window_by_executable(hwnd);
    reads_the_active_layout(hwnd);
    lists_installed_layouts_once_per_language();
    refuses_impossible_requests(hwnd);

    DestroyWindow(hwnd);
    pump();
}

void run_layout_switch_tests() {
    HWND hwnd = create_target();
    if (!hwnd) {
        check::skip("could not create a test window in this session");
        return;
    }

    switches_a_real_window(hwnd);

    DestroyWindow(hwnd);
    pump();
}
