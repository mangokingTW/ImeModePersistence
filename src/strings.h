#pragma once

#include <windows.h>

// User-visible text, chosen once from the user's UI language.
//
// A struct with designated initialisers rather than an enum indexing parallel
// arrays: adding a string then forces every language to supply it at the same
// place, instead of silently shifting every index after it.
namespace text {

struct Strings {
    const wchar_t* trayTip;

    const wchar_t* menuAutostart;
    const wchar_t* menuRules;
    const wchar_t* menuExit;

    const wchar_t* statusTitle;
    const wchar_t* statusFormat;      // desired, foreground, reachable, app, layout
    const wchar_t* modeNative;
    const wchar_t* modeAlphanumeric;
    const wchar_t* modeUnknown;
    const wchar_t* yes;
    const wchar_t* no;
    const wchar_t* unknownApplication;
    const wchar_t* noRule;

    const wchar_t* errorTitle;
    const wchar_t* errorAutostart;
    const wchar_t* errorHook;

    const wchar_t* rulesCaption;
    const wchar_t* rulesHeader;
    const wchar_t* labelExecutable;
    const wchar_t* labelLayout;
    const wchar_t* buttonUseLast;
    const wchar_t* buttonBrowse;
    const wchar_t* buttonAdd;
    const wchar_t* buttonRemove;
    const wchar_t* buttonClose;

    const wchar_t* hintIntro;
    const wchar_t* hintNeedExecutable;
    const wchar_t* hintNeedLayout;
    const wchar_t* hintWriteFailed;
    const wchar_t* hintBoundFormat;    // executable, layout
    const wchar_t* hintSelectRule;
    const wchar_t* hintRemoveFailed;
    const wchar_t* hintRemovedFormat;  // executable
    const wchar_t* hintNoLastApp;
    const wchar_t* hintPickLayout;
    const wchar_t* hintNameOnly;

    const wchar_t* suffixIme;
    const wchar_t* suffixNotInstalled;

    const wchar_t* browseTitle;
    const wchar_t* browseFilter;       // double-null terminated
};

// Reads the UI language on first use. Any Chinese UI language selects the
// Traditional strings, which is the only Chinese translation provided.
const Strings& s();

} // namespace text
