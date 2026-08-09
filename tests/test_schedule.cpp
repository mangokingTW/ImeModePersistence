#include "check.h"

#include "schedule.h"

// The retry schedule is three numbers that only make sense together: how soon the
// first attempt goes out, how many attempts there are, and how long a target that
// refused is left alone. Shortening the first without the other two is what turns
// "reverts an unwanted switch quickly" into "posts into an anti-cheat process
// several times a second", so the relationships are asserted rather than trusted
// to the comment next to them.
namespace {

void drift_reacts_faster_than_a_focus_change() {
    // The reason the two triggers exist. A focus change has to wait for the new
    // thread's IME to become usable; a layout change inside the application
    // already in front has nothing to wait for, and that wait is exactly the delay
    // the user feels when a binding puts their switch back.
    const UINT focus = schedule::delay_for(0, schedule::Trigger::FocusChange);
    const UINT drift = schedule::delay_for(0, schedule::Trigger::LayoutDrift);

    CHECK_MSG(drift < focus, "first drift attempt is %u ms, focus is %u ms", drift, focus);

    // Not zero. SetTimer clamps anything below USER_TIMER_MINIMUM, so a zero here
    // would claim a latency the timer cannot deliver.
    CHECK_MSG(drift >= 10, "first drift attempt is %u ms, below USER_TIMER_MINIMUM", drift);

    // And the focus delay must not be shortened by accident: it was measured
    // against a real IME that is not ready the instant the foreground changes, and
    // an attempt that lands too early is spent for nothing.
    CHECK_MSG(focus >= 60, "focus delay dropped to %u ms", focus);
}

void every_attempt_has_a_delay() {
    const schedule::Trigger triggers[] = {
        schedule::Trigger::FocusChange,
        schedule::Trigger::LayoutDrift,
    };

    for (const schedule::Trigger trigger : triggers) {
        for (int attempt = 0; attempt < schedule::max_attempts(); ++attempt) {
            const UINT delay = schedule::delay_for(attempt, trigger);

            // A zero or absurd delay is how a retry loop becomes a busy loop.
            CHECK_MSG(delay >= 10 && delay <= 2000, "attempt %d waits %u ms",
                      attempt, delay);
        }
    }
}

void delays_never_shrink_as_attempts_run_out() {
    const schedule::Trigger triggers[] = {
        schedule::Trigger::FocusChange,
        schedule::Trigger::LayoutDrift,
    };

    // Backing off is the whole idea: the first attempt is fast because it usually
    // works, and the later ones are slower because a target that ignored the fast
    // one is not going to be persuaded by more of them sooner.
    for (const schedule::Trigger trigger : triggers) {
        for (int attempt = 1; attempt < schedule::max_attempts(); ++attempt) {
            const UINT previous = schedule::delay_for(attempt - 1, trigger);
            const UINT current = schedule::delay_for(attempt, trigger);
            CHECK_MSG(current >= previous, "attempt %d waits %u ms after %u ms",
                      attempt, current, previous);
        }
    }
}

void out_of_range_attempts_clamp() {
    const UINT last = schedule::delay_for(schedule::max_attempts() - 1,
                                         schedule::Trigger::LayoutDrift);

    // The caller derives the attempt from a counter, and reading off the end of
    // the table would be undefined rather than merely wrong.
    CHECK(schedule::delay_for(schedule::max_attempts(), schedule::Trigger::LayoutDrift) == last);
    CHECK(schedule::delay_for(1000, schedule::Trigger::LayoutDrift) == last);

    const UINT first = schedule::delay_for(0, schedule::Trigger::FocusChange);
    CHECK(schedule::delay_for(-1, schedule::Trigger::FocusChange) == first);
}

void the_budget_matches_the_escalation() {
    // Four attempts against the three mechanisms in layout::method_for_attempt.
    // Fewer would mean a mechanism never gets tried; the budget existing at all is
    // what stops a losing argument from running forever.
    CHECK_MSG(schedule::max_attempts() >= 3, "only %d attempts for three mechanisms",
              schedule::max_attempts());
    CHECK_MSG(schedule::max_attempts() <= 8, "%d attempts is a long fight",
              schedule::max_attempts());
}

void a_lost_round_is_followed_by_a_real_pause() {
    const UINT first = schedule::cooldown_ms(1);

    // This is the safety property the 15 ms poll depends on. Without a cooldown
    // meaningfully longer than the poll, an application that insists on its own
    // layout would be sent a fresh round of requests every poll for as long as it
    // stayed in front.
    CHECK_MSG(first >= 1000, "first cooldown is only %u ms", first);

    // But not so long that a binding appears broken after one lost argument. A
    // focus change clears it, and that is the documented way out.
    CHECK_MSG(first <= 30000, "first cooldown is %u ms, long enough to look broken",
              first);

    // The whole point: one lost round costs far more waiting than one poll.
    UINT round = 0;
    for (int attempt = 0; attempt < schedule::max_attempts(); ++attempt) {
        round += schedule::delay_for(attempt, schedule::Trigger::LayoutDrift);
    }
    CHECK_MSG(first > round, "cooldown %u ms is shorter than a full round of %u ms",
              first, round);
}

void repeated_losses_back_off_but_never_stop() {
    // Never shorter after another loss: shrinking would mean fighting harder the
    // more hopeless the argument gets.
    for (int losses = 1; losses < 12; ++losses) {
        const UINT current = schedule::cooldown_ms(losses);
        const UINT next = schedule::cooldown_ms(losses + 1);
        CHECK_MSG(next >= current, "loss %d waits %u ms after %u ms",
                  losses + 1, next, current);
    }

    // Capped, not unbounded: the retry has to stay on a human timescale, so a
    // game whose fight stops (a loading screen ends) is rebound without the user
    // switching windows. And the cap must not overflow into zero for absurd
    // counts, which would re-open the every-15-ms flood at maximum frustration.
    const UINT capped = schedule::cooldown_ms(20);
    CHECK(schedule::cooldown_ms(1000) == capped);
    CHECK_MSG(capped <= 60000, "cap is %u ms; a binding that silent looks broken",
              capped);
    CHECK_MSG(capped > schedule::cooldown_ms(1), "the back-off never backs off");

    // Nonsense counts clamp to the base rather than misbehaving.
    CHECK(schedule::cooldown_ms(0) == schedule::cooldown_ms(1));
    CHECK(schedule::cooldown_ms(-5) == schedule::cooldown_ms(1));
}

} // namespace

void run_schedule_tests() {
    drift_reacts_faster_than_a_focus_change();
    every_attempt_has_a_delay();
    delays_never_shrink_as_attempts_run_out();
    out_of_range_attempts_clamp();
    the_budget_matches_the_escalation();
    a_lost_round_is_followed_by_a_real_pause();
    repeated_losses_back_off_but_never_stop();
}
