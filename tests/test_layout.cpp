#include "check.h"

#include <set>
#include <string>

#include "layout.h"

namespace {

// The reason this file exists. v0.7.1 added a branch that sent targets whose
// executable could not be read straight to TsfSession, skipping both
// window-message mechanisms. That silently broke layout binding for
// anti-cheat-protected games -- the applications the feature was built for -- and
// it went unnoticed until a user reported it and a diagnostic log was read by
// hand, because the failure is invisible from inside the process: the calls all
// succeed and the target simply does not change.
//
// No test on any machine can reproduce that target. What can be stated is the
// invariant the branch violated: the escalation starts at the cheapest mechanism
// and works up, for every target, with nothing about the target able to change
// where it starts.
void escalation_order() {
    CHECK(layout::method_for_attempt(0) == layout::Method::FocusWindow);
    CHECK(layout::method_for_attempt(1) == layout::Method::ThreadWindows);
    CHECK(layout::method_for_attempt(2) == layout::Method::TsfSession);

    // The retry budget is four attempts against three mechanisms, so the last is
    // repeated. Anything beyond that stays there rather than falling off the end.
    CHECK(layout::method_for_attempt(3) == layout::Method::TsfSession);
    CHECK(layout::method_for_attempt(4) == layout::Method::TsfSession);
    CHECK(layout::method_for_attempt(1000) == layout::Method::TsfSession);

    // The caller derives the attempt from a counter it decrements, so a negative
    // must not index off the front of the table.
    CHECK(layout::method_for_attempt(-1) == layout::Method::FocusWindow);

    // Stated separately from the sequence above, because this is the property
    // that was lost: the first attempt is a window message. If a future change
    // reintroduces a reason to skip ahead, this is the line that should have to
    // be deleted deliberately.
    CHECK(layout::method_for_attempt(0) != layout::Method::TsfSession);
}

void method_names_are_distinct() {
    const layout::Method methods[] = {
        layout::Method::FocusWindow,
        layout::Method::ThreadWindows,
        layout::Method::TsfSession,
    };

    // These names go into the diagnostic log, and the log is the only instrument
    // for the failures that matter. Two mechanisms sharing a name, or one coming
    // back "unknown", would make a report unreadable.
    std::set<std::wstring> seen;
    for (const layout::Method method : methods) {
        const std::wstring name = layout::method_name(method);
        CHECK(!name.empty());
        CHECK(name != L"unknown");
        CHECK(seen.insert(name).second);
    }
}

// reinterpret_cast to a pointer goes through UINT_PTR rather than straight from a
// literal, so the width is explicit on both 32- and 64-bit builds.
HKL as_hkl(UINT_PTR value) {
    return reinterpret_cast<HKL>(value);
}

void language_extraction() {
    // The low word of an HKL is the LANGID; the high word is a device handle that
    // differs between logon sessions, which is why rules store the LANGID alone.
    CHECK(layout::language_of(as_hkl(0x04090409)) == 0x0409);
    CHECK(layout::language_of(as_hkl(0x04040404)) == 0x0404);

    // An IME's high word is not the language repeated -- it is 0xEnnn -- so a
    // naive shift or an accidental sign extension would show up here.
    CHECK(layout::language_of(as_hkl(0xE0010404)) == 0x0404);
    CHECK(layout::language_of(as_hkl(0xE0200411)) == 0x0411);

    CHECK(layout::language_of(nullptr) == 0);
}

void descriptions_never_come_back_empty() {
    // describe() feeds both the tray tooltip and the log, so an empty string
    // would silently erase the subject of the sentence.
    CHECK(!layout::describe(0x0409).empty());
    CHECK(!layout::describe(0x0404).empty());

    // A LANGID no locale claims must still describe itself somehow, so that an
    // unrecognised rule is diagnosable rather than a blank in the log. Only
    // non-emptiness is asserted: whether LCIDToLocaleName rejects a given
    // unassigned LANGID or resolves it to some custom locale is not a contract
    // Windows offers, and pinning the hex fallback to a specific input would be
    // asserting an implementation detail of the OS.
    constexpr LANGID kUnassigned = 0x0EFF;
    const std::wstring unknown = layout::describe(kUnassigned);
    CHECK_MSG(!unknown.empty(), "describe(0x0EFF) came back empty");
}

} // namespace

void run_layout_tests() {
    escalation_order();
    method_names_are_distinct();
    language_extraction();
    descriptions_never_come_back_empty();
}
