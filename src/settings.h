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

// Whether a small badge showing the current input language and mode is drawn next
// to the text caret. Off by default: it is a heavier, always-on overlay that not
// everyone wants, so it is opt-in from the tray menu.
bool indicator_enabled();
bool set_indicator_enabled(bool enabled);

// Which UI language to show, as a text::Language value. 0 = Auto, i.e. follow the
// Windows display language (the default); 1..5 pin a specific language. Stored so
// the choice survives a restart.
int ui_language();
bool set_ui_language(int language);

} // namespace settings
