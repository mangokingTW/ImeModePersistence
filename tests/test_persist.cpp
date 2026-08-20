#include "check.h"

#include "ime_state.h"
#include "persist.h"

// The conversion-mode persistence engine, driven by synthetic (mode, time)
// sequences. These are the exact edge cases that broke in the field -- a switch
// carrying the wrong mode, a one-frame interop misread flipping the carried mode,
// an app's own write echoing back inside the suppress window -- reproduced here
// so a regression fails in CI rather than on a user's desktop. The engine calls
// no Win32, so time is just a number and nothing has to sleep.
namespace {

using ime::Mode;
using persist::Action;

// Seed the engine into a settled "carrying `carried`, observing `carried`" state
// past the post-switch enforcement window (and so past the shorter promotion
// dwell), so a test can go straight to the adoption behaviour it means to
// exercise without the enforcement window re-forcing instead. Returns the time it
// left the clock at.
unsigned long long settle_carrying(persist::Engine& e, Mode carried,
                                   unsigned long long now) {
    e.begin_context(now);
    e.set_observed(carried);
    e.decide_context_restore();  // seeds desired from the observation
    return now + persist::kEnforceWindowMs + 1;  // past the enforcement window
}

// 1. The first readable context seeds the carried mode; a later switch to a
//    window that came up in a different mode asks for a restore, without changing
//    what is carried.
void a_switch_carries_the_seeded_mode() {
    persist::Engine e;

    e.begin_context(0);
    e.set_observed(Mode::Alphanumeric);
    CHECK(e.decide_context_restore() == Action::None);  // nothing carried yet: seed
    CHECK(e.desired() == Mode::Alphanumeric);

    // Switch to a window whose IME came up native.
    e.begin_context(5000);
    e.set_observed(Mode::Native);
    CHECK(e.decide_context_restore() == Action::ScheduleRestore);
    CHECK_MSG(e.desired() == Mode::Alphanumeric, "carried mode changed on a switch");
}

// 2. A one-frame interop misread (native for a moment on an alphanumeric window,
//    then back) must NOT flip the carried mode.
void a_phantom_blip_is_not_adopted() {
    persist::Engine e;
    const unsigned long long t = settle_carrying(e, Mode::Alphanumeric, 0);

    const persist::Outcome a = e.observe(Mode::Native, true, t, false);
    CHECK(!a.adopted);
    const persist::Outcome b = e.observe(Mode::Native, true, t + 100, false);
    CHECK(!b.adopted);  // still short of the debounce
    const persist::Outcome c = e.observe(Mode::Alphanumeric, true, t + 150, false);
    CHECK(!c.adopted);  // returned to the carried mode

    CHECK_MSG(e.desired() == Mode::Alphanumeric, "a blip flipped the carried mode");
}

// 3. A change the user makes and keeps -- held past the debounce -- is adopted as
//    the new carried mode.
void a_sustained_change_is_adopted() {
    persist::Engine e;
    const unsigned long long t = settle_carrying(e, Mode::Alphanumeric, 0);

    const persist::Outcome first = e.observe(Mode::Native, true, t, false);
    CHECK(!first.adopted);  // candidate opened, not yet believed

    const persist::Outcome held =
        e.observe(Mode::Native, true, t + persist::kAdoptDebounceMs, false);
    CHECK_MSG(held.adopted, "a change held past the debounce was not adopted");
    CHECK(e.desired() == Mode::Native);
}

// 4. While the context is settling -- inside the dwell, or with a restore round in
//    flight -- a differing read is not adopted, however long it is seen.
void settling_suppresses_adoption() {
    persist::Engine e;
    e.begin_context(0);
    e.set_observed(Mode::Alphanumeric);
    e.decide_context_restore();

    // Inside the promotion dwell after the switch.
    CHECK(e.settling(100, false));
    const persist::Outcome inDwell = e.observe(Mode::Native, true, 100, false);
    CHECK(!inDwell.adopted);

    // Past the dwell, but a restore round is in flight: still settling, and even a
    // long-held differing read is not credited to the user.
    CHECK(e.settling(100000, true));
    const persist::Outcome pending = e.observe(Mode::Native, true, 100000, true);
    CHECK(!pending.adopted);

    CHECK_MSG(e.desired() == Mode::Alphanumeric, "adopted while settling");
}

// 5. When the context becomes readable only after the restore attempts ran out
//    (observed reset to Unknown, a mode still carried, the read disagreeing), the
//    engine asks for a fresh restore rather than adopting the disagreement.
void becoming_readable_re_forces_the_carried_mode() {
    persist::Engine e;
    const unsigned long long t = settle_carrying(e, Mode::Alphanumeric, 0);

    // An unreadable read leaves the observed mode Unknown.
    const persist::Outcome invalid = e.observe(Mode::Unknown, false, t, false);
    CHECK(invalid.action == Action::None);
    CHECK(e.observed() == Mode::Unknown);

    // Now a valid read that disagrees with the carried mode, past settling.
    const persist::Outcome readable =
        e.observe(Mode::Native, true, t + 1000, false);
    CHECK_MSG(readable.action == Action::ScheduleRestore,
              "became-readable did not re-force the carried mode");
    CHECK(!readable.adopted);
    CHECK(e.desired() == Mode::Alphanumeric);
}

// 6. Right after a restore lands, the target's own write echoes back inside the
//    suppress window; that ripple must not be adopted. Once the window passes, a
//    genuinely sustained change is adopted again.
void the_post_restore_ripple_is_suppressed() {
    persist::Engine e;
    settle_carrying(e, Mode::Alphanumeric, 0);

    // A restore round settled the mode at t = 1000.
    e.accept_restored(Mode::Alphanumeric, 1000);

    // The ripple: a native read 100 ms later, still inside the suppress window.
    CHECK(e.settling(1100, false));
    const persist::Outcome ripple = e.observe(Mode::Native, true, 1100, false);
    CHECK_MSG(!ripple.adopted, "the post-restore ripple was adopted");
    CHECK(e.desired() == Mode::Alphanumeric);

    // Past the suppress window, a sustained change is believed again.
    const unsigned long long t = 1000 + persist::kPostRestoreSuppressMs + 200;
    const persist::Outcome open = e.observe(Mode::Native, true, t, false);
    CHECK(!open.adopted);
    const persist::Outcome held =
        e.observe(Mode::Native, true, t + persist::kAdoptDebounceMs, false);
    CHECK_MSG(held.adopted, "suppress window never re-opened adoption");
    CHECK(e.desired() == Mode::Native);
}

// 7. Within a short window after the switch, a mode that drifted from the carried
//    one -- Windows applying the layout's default a beat after focus, clobbering
//    the restore -- is re-forced (ScheduleRestore), not adopted. The window is
//    measured from the switch, so a re-accepting restore does not extend it: the
//    same drift after the window is the user's and is adopted.
void a_post_switch_clobber_is_re_forced() {
    persist::Engine e;

    // Carry alphanumeric; switch at t = 0; a restore lands at t = 60.
    e.begin_context(0);
    e.set_observed(Mode::Alphanumeric);
    e.decide_context_restore();               // seeds desired = Alphanumeric
    e.accept_restored(Mode::Alphanumeric, 60);

    // Windows flips it native at t = 200, inside the enforcement window and inside
    // the post-restore suppress window: re-force it rather than adopt.
    const persist::Outcome clobber = e.observe(Mode::Native, true, 200, false);
    CHECK_MSG(clobber.action == Action::ScheduleRestore,
              "a clobber inside the enforcement window was not re-forced");
    CHECK(!clobber.adopted);
    CHECK(e.desired() == Mode::Alphanumeric);

    // The re-corrected restore re-accepts, but the window is measured from the
    // switch, not the restore, so it does not slide.
    e.accept_restored(Mode::Alphanumeric, 250);

    // Past the window (and the suppress window): a sustained native read is the
    // user's, and is adopted rather than fought.
    const unsigned long long t = persist::kEnforceWindowMs + 400;  // 700
    const persist::Outcome open = e.observe(Mode::Native, true, t, false);
    CHECK_MSG(open.action == Action::None, "still re-forcing after the window");
    CHECK(!open.adopted);
    const persist::Outcome held =
        e.observe(Mode::Native, true, t + persist::kAdoptDebounceMs, false);
    CHECK_MSG(held.adopted, "a change after the enforcement window was not adopted");
    CHECK(e.desired() == Mode::Native);
}

} // namespace

void run_persist_tests() {
    a_switch_carries_the_seeded_mode();
    a_phantom_blip_is_not_adopted();
    a_sustained_change_is_adopted();
    settling_suppresses_adoption();
    becoming_readable_re_forces_the_carried_mode();
    the_post_restore_ripple_is_suppressed();
    a_post_switch_clobber_is_re_forced();
}
