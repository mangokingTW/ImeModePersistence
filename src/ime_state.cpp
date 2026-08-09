#include "ime_state.h"

#include <imm.h>

#pragma comment(lib, "imm32.lib")

namespace ime {

Mode query_mode(HWND hwnd) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) {
        return Mode::Unknown;
    }

    const BOOL open = ImmGetOpenStatus(himc);
    ImmReleaseContext(hwnd, himc);

    // IMM32's open flag is a reliable coarse signal, but it is not a complete
    // representation of modern TSF conversion modes. Treat it as Native when
    // open and Alphanumeric when closed; the main application can later replace
    // this adapter with a TSF-specific implementation for a target IME.
    return open ? Mode::Native : Mode::Alphanumeric;
}

bool set_mode(HWND hwnd, Mode mode) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) {
        return false;
    }

    BOOL ok = FALSE;
    switch (mode) {
    case Mode::Native:
        ok = ImmSetOpenStatus(himc, TRUE);
        break;
    case Mode::Alphanumeric:
        ok = ImmSetOpenStatus(himc, FALSE);
        break;
    case Mode::Unknown:
        ok = TRUE;
        break;
    }

    ImmReleaseContext(hwnd, himc);
    return ok == TRUE;
}

const wchar_t* mode_name(Mode mode) {
    switch (mode) {
    case Mode::Native: return L"Native";
    case Mode::Alphanumeric: return L"Alphanumeric";
    default: return L"Unknown";
    }
}

} // namespace ime
