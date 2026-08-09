#pragma once

#include <windows.h>

#include <string>

// A dependency-free assertion harness. gtest would be more than this whole
// project's worth of build machinery for what amounts to a counter and a printf,
// and vendoring it would mean the tests no longer compile with exactly the flags
// the shipped objects do.
namespace check {

// Incremented rather than thrown, so one failing expectation does not hide the
// rest of the suite behind it.
extern int failures;

// Reported separately from a pass. A test whose environment cannot support it --
// no interactive desktop, a keyboard layout that will not load -- must not report
// success, because a silent pass is how a test stops testing anything.
extern int skipped;

void record(bool ok, const char* expression, const char* file, int line);

// printf-style context on a failure, ASCII only: this goes to a CI log, and the
// last thing a project that shipped CP1252 mojibake should do is guess at the
// console code page. Wide strings go through utf8() first.
void detail(const char* format, ...);

void skip(const char* reason);

std::string utf8(const std::wstring& text);

} // namespace check

#define CHECK(expression) \
    ::check::record((expression) ? true : false, #expression, __FILE__, __LINE__)

// For the cases where knowing the expression is not enough to diagnose the
// failure from the log alone -- which LANGID came back, which path was compared.
#define CHECK_MSG(expression, ...)                                            \
    do {                                                                      \
        const bool ok_ = (expression) ? true : false;                         \
        ::check::record(ok_, #expression, __FILE__, __LINE__);                \
        if (!ok_) {                                                           \
            ::check::detail(__VA_ARGS__);                                     \
        }                                                                     \
    } while (false)
