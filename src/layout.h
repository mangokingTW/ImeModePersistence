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

// Asks the owning thread to switch. Posted rather than sent: the switch happens
// on that thread's message loop, so there is no meaningful return value and the
// caller has to read the layout back to know whether it took.
bool request(HWND hwnd, HKL hkl);

} // namespace layout
