#include "check.h"

#include <cstdio>
#include <cstring>

void run_layout_tests();
void run_schedule_tests();
void run_rules_tests();
void run_diagnostic_tests();
void run_window_identity_tests();
void run_layout_switch_tests();

namespace {

struct Suite {
    const char* name;
    void (*run)();
};

// One CTest entry per suite, so a failure names the area rather than the binary,
// and so a suite skipped for want of an interactive desktop is reported as skipped
// on its own.
constexpr Suite kSuites[] = {
    {"layout", run_layout_tests},
    {"schedule", run_schedule_tests},
    {"rules", run_rules_tests},
    {"diagnostic", run_diagnostic_tests},
    {"window-identity", run_window_identity_tests},
    {"layout-switch", run_layout_switch_tests},
};

// CTest's SKIP_RETURN_CODE. A suite whose environment cannot support it must not
// report success: a green run that tested nothing is worse than a red one.
constexpr int kSkipExitCode = 77;

int usage() {
    std::printf("usage: ime_tests <suite>\nsuites:");
    for (const Suite& suite : kSuites) {
        std::printf(" %s", suite.name);
    }
    std::printf("\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return usage();
    }

    for (const Suite& suite : kSuites) {
        if (std::strcmp(argv[1], suite.name) != 0) {
            continue;
        }

        suite.run();

        if (check::failures > 0) {
            std::printf("%s: %d failed\n", suite.name, check::failures);
            return 1;
        }
        if (check::skipped > 0) {
            std::printf("%s: skipped (%d)\n", suite.name, check::skipped);
            return kSkipExitCode;
        }

        std::printf("%s: ok\n", suite.name);
        return 0;
    }

    std::printf("unknown suite '%s'\n", argv[1]);
    return usage();
}
