#pragma once

#include <windows.h>

// User preferences, alongside the bindings in HKCU.
namespace settings {

// Whether the last mode the user chose is carried to the next window. On by
// default, since it is what the utility is for -- but per-application bindings
// are useful on their own, and someone who only wants those should be able to
// turn the global behaviour off rather than work around it.
bool persist_mode();
bool set_persist_mode(bool enabled);

} // namespace settings
