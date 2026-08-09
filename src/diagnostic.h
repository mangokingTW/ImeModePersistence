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
namespace diag {

// Rotates an oversized log and opens the file. Failure is not fatal: every write
// then becomes a no-op and the utility carries on.
bool initialise();

void shutdown();

// printf-style, wide. A newline is added; callers should not include one.
void write(const wchar_t* format, ...);

// Empty when the log could not be opened.
std::wstring path();

} // namespace diag
