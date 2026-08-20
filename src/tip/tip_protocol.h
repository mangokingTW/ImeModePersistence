#pragma once

#include <windows.h>

#include <cstdint>

// The wire contract between the in-process Text Input Processor (the DLL Windows
// loads into every application) and the tray coordinator (the exe). Kept in one
// tiny header both sides include so the two can never drift: a fixed-size,
// fixed-layout struct with no pointers, so a single ReadFile / WriteFile of
// sizeof(Message) is one whole message and framing is trivial.
//
// The DLL is deliberately standalone -- it does not link ime_core -- so the mode
// travels as a plain int on the wire (WireMode) rather than ime::Mode, and each
// side converts at its own edge.
namespace tip_ipc {

// A per-session duplex pipe. The tray is the server; each TIP instance (one per
// application process that loads a keyboard text service) is a client. Session
// isolation matches the app's single-instance-per-session model.
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\ImeModePersistence.tip";

enum class MsgType : uint32_t {
    Hello = 1,       // TIP -> tray: a text service activated on (pid, tid); carries its mode
    ModeReport = 2,  // TIP -> tray: the conversion mode on this thread changed
    SetMode = 3,     // tray -> TIP: enforce this mode on the thread (done in-process)
    Bye = 4,         // TIP -> tray: the text service is deactivating
};

// Mirrors ime::Mode without depending on it, so the DLL stays free of ime_core.
enum class WireMode : int32_t {
    Unknown = 0,
    Alphanumeric = 1,
    Native = 2,
};

// One message is exactly this struct, little-endian, as written by the machine.
// Both ends are the same architecture (x64) built by the same toolchain, so no
// portable serialisation is needed; the static_assert nails the size down so an
// accidental field change cannot silently desynchronise the framing.
struct Message {
    uint32_t type;   // MsgType
    uint32_t pid;    // GetCurrentProcessId of the TIP's host process
    uint32_t tid;    // Win32 thread id the text service is running on
    int32_t mode;    // WireMode
};

static_assert(sizeof(Message) == 16, "the pipe framing depends on a fixed message size");

} // namespace tip_ipc
