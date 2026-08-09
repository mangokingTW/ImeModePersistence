#include "check.h"

#include <cstdarg>
#include <cstdio>

namespace check {

int failures = 0;
int skipped = 0;

void record(bool ok, const char* expression, const char* file, int line) {
    if (ok) {
        return;
    }
    ++failures;
    std::printf("FAIL %s:%d  %s\n", file, line, expression);
    std::fflush(stdout);
}

void detail(const char* format, ...) {
    std::printf("     ");

    va_list arguments;
    va_start(arguments, format);
    std::vprintf(format, arguments);
    va_end(arguments);

    std::printf("\n");
    std::fflush(stdout);
}

void skip(const char* reason) {
    ++skipped;
    std::printf("SKIP %s\n", reason);
    std::fflush(stdout);
}

std::string utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                          static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        result.data(), bytes, nullptr, nullptr);
    return result;
}

} // namespace check
