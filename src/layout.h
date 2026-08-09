#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Binding a keyboard layout is a different operation from setting a conversion
// mode. The conversion mode lives on whichever layout a thread already has
// active (native vs alphanumeric within Bopomofo, say); this replaces the layout
// itself, so Bopomofo, a US keyboard and a Japanese IME become distinct targets.
namespace layout {

struct Installed {
    HKL hkl{};
    LANGID language{};
    std::wstring name;
    bool is_ime{false};
};

// One entry per language, not per HKL. Rules are keyed by language (see
// find_by_language), so listing two IMEs of the same language would offer a
// distinction the rules cannot express.
std::vector<Installed> installed();

// Rules persist a LANGID rather than an HKL because the high word of an HKL is a
// runtime handle that differs between logon sessions. Returns nullptr when no
// installed layout matches, which happens when a rule names a layout the user
// has since removed.
HKL find_by_language(LANGID language);

LANGID language_of(HKL hkl);

// Localised display name, falling back to the hex LANGID.
std::wstring describe(LANGID language);

// Layout active on the thread that owns hwnd.
HKL current(HWND hwnd);

// No single mechanism works everywhere. WM_INPUTLANGCHANGEREQUEST only takes
// effect if the receiving window lets it reach DefWindowProc, and plenty of
// applications -- anything Chromium-based, most UWP and WinUI -- do not. So the
// caller works through these in order and verifies after each.
enum class Method {
    FocusWindow,    // post to the focus window of the owning thread
    ThreadWindows,  // post to every top-level window of that thread
    TsfSession,     // ask the Text Services Framework, never the window
};

const wchar_t* method_name(Method method);

// The escalation order, by zero-based attempt. Attempts past the end repeat the
// last method, because a target that ignored all three will keep ignoring them.
//
// Every target gets the whole sequence, and that is the invariant this function
// exists to make assertable. v0.7.1 added a branch sending targets whose
// executable could not be read -- anti-cheat, in practice -- straight to
// TsfSession, on the unverified assumption that TSF was what reached them. It is
// not, and the branch silently disabled the feature for exactly the applications
// it was built for. The decision lives here, in a pure function, so a test can
// state that no property of the target changes attempt 0.
Method method_for_attempt(int attempt);

// Asks the owning thread to switch. Every method is best-effort with no useful
// return value of its own, so the caller has to read the layout back to learn
// whether it took.
bool request(HWND hwnd, HKL hkl, Method method);

} // namespace layout
