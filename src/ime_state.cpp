#include "ime_state.h"

#include "ime_interop.h"

#include <imm.h>

namespace ime {
namespace {

Mode classify(const interop::Conversion& c) {
    if (!c.open) {
        return Mode::Alphanumeric;
    }
    // An open IME still distinguishes native from alphanumeric input through the
    // conversion mode: Microsoft Bopomofo, for example, clears IME_CMODE_NATIVE
    // on Shift while staying open. This is exactly the detail ImmGetOpenStatus
    // alone could not express.
    return (c.bits & IME_CMODE_NATIVE) ? Mode::Native : Mode::Alphanumeric;
}

} // namespace

State query_state(HWND hwnd) {
    State state;

    const interop::Conversion c = interop::read(hwnd);
    if (!c.valid) {
        return state;
    }

    state.valid = true;
    state.open = c.open;
    state.conversion = c.bits;
    state.mode = classify(c);
    return state;
}

bool set_mode(HWND hwnd, Mode desired) {
    if (desired == Mode::Unknown) {
        return true;
    }

    const State current = query_state(hwnd);
    if (!current.valid) {
        return false;
    }
    if (current.mode == desired) {
        return true;
    }

    DWORD bits = current.conversion;
    if (desired == Mode::Native) {
        bits |= IME_CMODE_NATIVE;
        // A closed IME ignores the native flag, so reopen it first.
        if (!current.open && !interop::write_open(hwnd, true)) {
            return false;
        }
    } else {
        // Clear the native flag but leave the IME open. Closing it entirely is a
        // heavier change than the user asked for and loses their other flags.
        bits &= ~static_cast<DWORD>(IME_CMODE_NATIVE);
    }

    return interop::write_conversion(hwnd, bits);
}

Mode query_mode(HWND hwnd) {
    return query_state(hwnd).mode;
}

const wchar_t* mode_name(Mode mode) {
    switch (mode) {
    case Mode::Native: return L"Native";
    case Mode::Alphanumeric: return L"Alphanumeric";
    default: return L"Unknown";
    }
}

} // namespace ime
