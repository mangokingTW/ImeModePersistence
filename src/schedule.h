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

// Six, for both triggers: three closely spaced early attempts (front-loaded to
// catch a write that takes but needs a moment, or a quick revert right after the
// switch) followed by an escalating tail. The escalation in
// layout::method_for_attempt has three mechanisms and repeats the last; the
// budget is what stops a losing argument from running forever.
int max_attempts();

// Milliseconds before the given zero-based attempt. Attempts outside the budget
// clamp rather than read off the end.
UINT delay_for(int attempt, Trigger trigger);

// How long to leave a target alone after losing `consecutiveLosses` rounds in a
// row against it. This exists because of the faster poll, not despite it:
// without a cooldown, an application that insists on its own layout would be
// sent a request every poll for as long as it stayed in front -- and for an
// anti-cheat-protected game, repeatedly posting into it is the one thing worth
// not doing.
//
// It doubles per consecutive loss, up to a cap. A flat pause meant a target
// that always wins was re-fought -- and its defeat logged, seven lines a
// round -- every few seconds indefinitely; backing off makes a hopeless
// argument asymptotically quiet while still retrying now and then, and one won
// round (or a genuine change of application) resets it.
UINT cooldown_ms(int consecutiveLosses);

} // namespace schedule
