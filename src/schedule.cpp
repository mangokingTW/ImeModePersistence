#include "schedule.h"

namespace schedule {
namespace {

// Unchanged from the original schedule. The first wait is for the new thread's
// IME to become usable, and shortening it made the first attempt land before
// there was anything to write to -- which cost an attempt out of the budget
// rather than saving time.
constexpr UINT kFocusDelaysMs[] = {60, 120, 250, 500};

// The application is already in front and its IME is already running, so the
// first attempt goes out as soon as SetTimer can deliver it. USER_TIMER_MINIMUM
// is 10 ms and SetTimer clamps to it, so 10 is the honest value rather than 0.
//
// The later waits stay close to the focus case on purpose: if the first, fastest
// attempt did not take, the target is not merely slow to start and hurrying the
// rest only adds traffic.
constexpr UINT kDriftDelaysMs[] = {10, 60, 200, 500};

static_assert(ARRAYSIZE(kFocusDelaysMs) == ARRAYSIZE(kDriftDelaysMs),
              "both triggers share one attempt budget and one escalation");

} // namespace

int max_attempts() {
    return static_cast<int>(ARRAYSIZE(kFocusDelaysMs));
}

UINT delay_for(int attempt, Trigger trigger) {
    const UINT* table = trigger == Trigger::LayoutDrift ? kDriftDelaysMs : kFocusDelaysMs;

    const int count = max_attempts();
    const int index = attempt < 0 ? 0 : (attempt >= count ? count - 1 : attempt);
    return table[index];
}

UINT cooldown_ms() {
    // Long enough that a target which insists is left in peace, short enough that
    // the binding resumes on its own once whatever was fighting stops. A focus
    // change clears it outright, so switching away and back is the way out.
    return 3000;
}

} // namespace schedule
