#pragma once

#include <windows.h>
#include <string>

namespace ime {

enum class Mode {
    Unknown,
    Alphanumeric,
    Native,
};

// Best-effort IMM32 state reader. IMM32 exposes the open/closed state reliably;
// the distinction between native/alphanumeric is IME-specific, so this module
// intentionally keeps that limitation explicit rather than pretending it is
// universally observable through one API.
Mode query_mode(HWND hwnd);

bool set_mode(HWND hwnd, Mode mode);

const wchar_t* mode_name(Mode mode);

} // namespace ime
