#pragma once

#include <windows.h>

namespace ime {

enum class Mode {
    Unknown,
    Alphanumeric,
    Native,
};

struct State {
    bool valid{false};       // false when the target thread has no reachable IME
    bool open{false};
    DWORD conversion{0};     // IME_CMODE_* flags
    Mode mode{Mode::Unknown};
};

// Reads the conversion mode of hwnd's thread through the IMM32/TSF interop
// layer, so TSF text services (Microsoft Bopomofo and friends) report their
// real native/alphanumeric state rather than just an open/closed flag.
State query_state(HWND hwnd);

// Moves hwnd's thread to `desired`, preserving every conversion flag the target
// already had (full/half shape, roman, and so on). Returns false when the write
// could not even be attempted; callers must verify by reading the state back,
// because an IME that is still activating can silently discard the change.
bool set_mode(HWND hwnd, Mode desired);

Mode query_mode(HWND hwnd);

const wchar_t* mode_name(Mode mode);

} // namespace ime
