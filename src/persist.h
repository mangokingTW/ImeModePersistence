#pragma once

#include "ime_state.h"

// The conversion-mode persistence decision core, lifted out of the Win32 message
// loop so it can be exercised deterministically in tests. It holds no window
// handles and calls no Win32 API: everything it needs -- the mode read from the
// foreground thread, the current time in milliseconds, whether a restore round is
// in flight -- is passed in, and its decisions are handed back for the caller to
// carry out. The caller (main.cpp) keeps the timers, the cross-process reads and
// writes, the restore attempt ladder, and the rule/layout enforcement.
namespace persist {

// After a successful restore the IME keeps settling for a moment; anything
// observed inside this window is our own write echoing back, not the user.
constexpr unsigned long long kPostRestoreSuppressMs = 250;

// A conversion-mode read that differs from the carried mode must hold this long
// before it is adopted as the new intent. The mode is read cross-process through
// the IMM32/TSF interop, which intermittently reports a transient wrong value;
// the debounce filters those blips while still following a real change kept.
constexpr unsigned long long kAdoptDebounceMs = 600;

// A mode change is only credited to the user once the input context has been
// stable for this long, which excludes the churn of a focus transition.
constexpr unsigned long long kPromotionDwellMs = 150;

// For this long after a window switch, a mode that has drifted from the carried
// one is Windows applying the layout's default a beat after focus -- clobbering
// our restore -- rather than the user, so it is re-corrected instead of adopted.
// Measured from the switch itself (never reset by a restore), so re-correcting
// cannot slide into an endless fight; kept short because that clobber lands
// within a few hundred ms and nobody toggles the mode this soon after switching.
constexpr unsigned long long kEnforceWindowMs = 300;

// What the caller should do about the carried mode after an observation.
enum class Action {
    None,            // nothing to schedule; caller cancels any pending restore
    ScheduleRestore, // start a restore round toward desired()
};

// The result of an observe(): the action to take, and whether the carried mode
// was just adopted from the observation (so the caller can log it with the
// window identity it alone knows).
struct Outcome {
    Action action{Action::None};
    bool adopted{false};
};

class Engine {
public:
    ime::Mode desired() const { return desired_; }
    ime::Mode observed() const { return observed_; }
    bool enabled() const { return enabled_; }

    void set_enabled(bool on) { enabled_ = on; }

    // Assign the observed mode directly, for the two places the caller learns it
    // outside observe(): the read at the tail of a context switch, and the state
    // a restore round adopted when it gave up.
    void set_observed(ime::Mode mode) { observed_ = mode; }

    // The observer must not credit a mode change as user intent while the input
    // context is still settling: within the post-restore suppress window, within
    // the promotion dwell after a switch, or while a restore round is in flight.
    bool settling(unsigned long long now, bool restore_pending) const;

    // A context switch was noticed at `now`, before the (possibly blocking) mode
    // read. Starts the dwell and the (non-sliding) post-switch enforcement window.
    void begin_context(unsigned long long now) {
        context_since_ = now;
        switch_at_ = now;
    }

    // The no-rule tail of a context switch: seed the carried mode from the
    // current observation when nothing is carried yet, and report whether the
    // carried mode should be restored onto the new window. Assumes set_observed()
    // was already called with the freshly read mode.
    Action decide_context_restore();

    // A layout change inside the same thread: the conversion mode lives on
    // (thread, layout), so the old observation describes nothing now.
    void note_layout_drift(unsigned long long now);

    // The observer read `mode` (valid per `valid`) at `now`, with `restore_pending`
    // true while a restore round is in flight. Applies the "became readable after
    // the attempts ran out" re-force and the debounced adoption of a deliberate
    // change, and updates the observed mode.
    Outcome observe(ime::Mode mode, bool valid, unsigned long long now,
                    bool restore_pending);

    // A restore round settled the mode to `mode` at `now`: adopt it as observed,
    // restart the dwell, and open the post-restore suppress window.
    void accept_restored(ime::Mode mode, unsigned long long now);

    // The user toggled persistence off/on: forget the carried target, so a stale
    // one is neither shown nor applied and re-enabling picks up the current mode.
    void reset_desired() { desired_ = ime::Mode::Unknown; }

private:
    ime::Mode desired_{ime::Mode::Unknown};
    ime::Mode observed_{ime::Mode::Unknown};
    unsigned long long context_since_{};
    unsigned long long switch_at_{};
    unsigned long long suppress_until_{};
    ime::Mode adopt_candidate_{ime::Mode::Unknown};
    unsigned long long adopt_candidate_since_{};
    bool enabled_{true};
};

} // namespace persist
