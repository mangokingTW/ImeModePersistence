#pragma once

#include <windows.h>

// When the next attempt happens, and how many there are.
//
// Separated from the message loop so the numbers can be asserted rather than
// merely read. They are not self-evidently safe: the whole point of a shorter
// wait is to revert an unwanted layout switch sooner, and a shorter wait against
// a target that refuses is a faster flood into that target. The retry budget and
// the cooldown are what keep the second from following from the first, so all
// three live together where the relationship is visible.
namespace schedule {

// What started a round of attempts. The two cases differ in what is happening at
// the moment the round begins, which is why they cannot share one delay.
enum class Trigger {
    // Focus moved to another application. A foreground change fires before the
    // new thread's IME is usable, so the first attempt has to wait for it.
    FocusChange,

    // The layout changed inside the application already in front -- the user
    // pressing Win+Space, or the application switching it back. That thread is
    // already running with a usable IME, so the wait the focus case needs is pure
    // latency here, and it is the latency the user actually feels.
    LayoutDrift,
};

// Four, for both triggers: the escalation in layout::method_for_attempt has three
// mechanisms and repeats the last, and the budget is what stops a losing argument
// from running forever.
int max_attempts();

// Milliseconds before the given zero-based attempt. Attempts outside the budget
// clamp rather than read off the end.
UINT delay_for(int attempt, Trigger trigger);

// How long to leave a target alone after a round has given up on it. This exists
// because of the faster poll, not despite it: without a cooldown, an application
// that insists on its own layout would be sent a request every poll for as long
// as it stayed in front -- and for an anti-cheat-protected game, repeatedly
// posting into it is the one thing worth not doing.
UINT cooldown_ms();

} // namespace schedule
