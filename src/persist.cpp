#include "persist.h"

namespace persist {

bool Engine::settling(unsigned long long now, bool restore_pending) const {
    return now < suppress_until_ ||
           now - context_since_ < kPromotionDwellMs ||
           restore_pending;
}

Action Engine::decide_context_restore() {
    if (!enabled_) {
        // Bindings are handled by the caller; without persistence there is
        // nothing to restore.
        return Action::None;
    }
    if (desired_ == ime::Mode::Unknown) {
        // Nothing carried yet: seed it from the context we can now read.
        desired_ = observed_;
        return Action::None;
    }
    return Action::ScheduleRestore;
}

void Engine::note_layout_drift(unsigned long long now) {
    observed_ = ime::Mode::Unknown;
    context_since_ = now;
}

Outcome Engine::observe(ime::Mode mode, bool valid, unsigned long long now,
                        bool restore_pending) {
    if (!valid) {
        observed_ = ime::Mode::Unknown;
        return {};
    }

    const bool is_settling = settling(now, restore_pending);

    // The context became readable only after the restore attempts ran out: start
    // a fresh round now that there is something to write to.
    if (enabled_ && !is_settling && observed_ == ime::Mode::Unknown &&
        desired_ != ime::Mode::Unknown && mode != desired_) {
        observed_ = mode;
        return {Action::ScheduleRestore, false};
    }

    // A value differing from the carried mode is credited to the user only once
    // it has held for kAdoptDebounceMs, so a transient interop misread does not
    // flip the carried mode.
    bool adopted = false;
    if (enabled_ && !is_settling && mode != ime::Mode::Unknown &&
        desired_ != ime::Mode::Unknown && mode != desired_) {
        if (adopt_candidate_ != mode) {
            adopt_candidate_ = mode;
            adopt_candidate_since_ = now;
        } else if (now - adopt_candidate_since_ >= kAdoptDebounceMs) {
            desired_ = mode;
            adopt_candidate_ = ime::Mode::Unknown;
            adopted = true;
        }
    } else {
        // Matches what we carry, or still settling: nothing pending to adopt.
        adopt_candidate_ = ime::Mode::Unknown;
    }

    observed_ = mode;
    return {Action::None, adopted};
}

void Engine::accept_restored(ime::Mode mode, unsigned long long now) {
    observed_ = mode;
    context_since_ = now;
    suppress_until_ = now + kPostRestoreSuppressMs;
}

} // namespace persist
