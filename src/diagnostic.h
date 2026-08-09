#pragma once

#include <windows.h>

#include <string>

// A plain text log of decisions and outcomes, written to
// %LocalAppData%\ImeModePersistence\log.txt.
//
// It exists because everything this utility does happens to *other* processes and
// leaves nothing behind: when a language switch does not take effect there is no
// error to read, and diagnosing it meant asking the user to hover the tray icon
// and describe what they saw. A file can be attached to an issue instead.
//
// Only state changes and actions are recorded. The observer runs twenty times a
// second, and logging that would bury the few lines that matter.
//
// Size is bounded two ways. Repetition is removed at the source by write_once,
// because the same handful of applications are switched between all day; and the
// file rotates at 1 MB, checked on every write rather than only at startup, so the
// limit holds for a copy that runs for weeks without restarting.
namespace diag {

// Rotates an oversized log and opens the file. Failure is not fatal: every write
// then becomes a no-op and the utility carries on.
bool initialise();

void shutdown();

// printf-style, wide. A newline is added; callers should not include one.
void write(const wchar_t* format, ...);

// As write, but skips a message identical to one already written since the log was
// opened. For lines that describe a situation rather than an event: which
// application is in front repeats thousands of times a day and is worth recording
// once, while what was attempted and whether it worked is worth recording every
// time.
void write_once(const wchar_t* format, ...);

// Empty when the log could not be opened.
std::wstring path();

} // namespace diag
