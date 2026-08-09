#pragma once

#include <windows.h>

// Text Services Framework profile activation.
//
// Every other mechanism asks the target window to change its own input language,
// which fails for anything that does not route the message to DefWindowProc --
// a raw-input fullscreen game, for instance. This asks the framework instead, so
// nothing about the target process is read, opened or attached to. That matters
// beyond tidiness: a game protected by anti-cheat refuses to be opened at all,
// and probing it is exactly the behaviour anti-cheat exists to catch.
namespace tsf {

// Initialises COM on the calling thread and creates the profile manager.
bool initialise();

void shutdown();

// Activates an enabled profile for the language with TF_IPPMF_FORSESSION, which
// applies to the whole session rather than only this thread.
bool activate_language(LANGID language);

} // namespace tsf
