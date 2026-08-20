#include "schedule.h"

namespace schedule {
namespace {

// The first wait is for the new thread's IME to become usable; shortening it
// made the first attempt land before there was anything to write to -- which
// cost an attempt rather than saving time -- so it stays at 60 ms. The next two
// are also 60 ms rather than escalating: an app whose write DOES take but needs
// a moment (or flips the mode back once right after focus) is caught within the
// first ~200 ms by three closely spaced attempts (at 60/120/180 ms) instead of
// waiting out one long gap. Only after that do the waits grow, for a target that
// is genuinely refusing -- there, more haste is just a heavier flood.
constexpr UINT kFocusDelaysMs[] = {60, 60, 60, 120, 250, 500};

// The application is already in front and its IME is already running, so the
// first attempt goes out as soon as SetTimer can deliver it. USER_TIMER_MINIMUM
// is 10 ms and SetTimer clamps to it, so 10 is the honest value rather than 0.
//
// Front-loaded like the focus case: a few closely spaced early attempts catch a
// layout that flips back right after the change, then the waits grow so a target
// that keeps refusing is not flooded.
constexpr UINT kDriftDelaysMs[] = {10, 30, 60, 120, 250, 500};

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

UINT cooldown_ms(int consecutiveLosses) {
    // Long enough that a target which insists is left in peace, short enough that
    // the binding resumes on its own once whatever was fighting stops. A focus
    // change clears it outright, so switching away and back is the way out.
    constexpr UINT kBaseMs = 3000;

    // The cap keeps the retry on a human timescale: half a minute is long enough
    // to end the log churn, short enough that a game whose fight stops (a
    // loading screen ends, an overlay closes) is rebound without the user doing
    // anything.
    constexpr UINT kCapMs = 30000;

    UINT value = kBaseMs;
    for (int loss = 1; loss < consecutiveLosses && value < kCapMs; ++loss) {
        value *= 2;
    }
    return value < kCapMs ? value : kCapMs;
}

} // namespace schedule
