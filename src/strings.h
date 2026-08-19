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
    const wchar_t* tooltipFormat;     // application, bound language, current language,
                                      // attempt, privileges
    const wchar_t* tooltipUnelevated;

    const wchar_t* menuPersist;
    const wchar_t* menuIndicator;
    const wchar_t* menuAutostart;
    const wchar_t* menuElevate;
    const wchar_t* menuRules;
    const wchar_t* menuLog;
    const wchar_t* menuExit;
    const wchar_t* menuVersion;       // "Version" / "版本"; the number is appended

    const wchar_t* statusTitle;
    const wchar_t* statusFormat;      // desired, foreground, reachable, app,
                                      // bound language, current language, switch attempt
    const wchar_t* modeNative;
    const wchar_t* modeAlphanumeric;
    const wchar_t* modeUnknown;
    const wchar_t* yes;
    const wchar_t* no;
    const wchar_t* unknownApplication;
    const wchar_t* noRule;
    const wchar_t* autostartTask;
    const wchar_t* autostartRegistry;
    const wchar_t* autostartStartupTask;
    const wchar_t* autostartOff;
    const wchar_t* elevatedYes;
    const wchar_t* elevatedNo;
    const wchar_t* switchOk;
    const wchar_t* switchFailed;
    const wchar_t* switchNotAttempted;

    const wchar_t* errorTitle;
    const wchar_t* errorAutostart;
    const wchar_t* errorHook;

    const wchar_t* notifyElevateTitle;
    const wchar_t* notifyElevateText;
    const wchar_t* notifyDesktopTitle;
    const wchar_t* notifyDesktopText;

    const wchar_t* rulesCaption;
    const wchar_t* rulesHeader;
    const wchar_t* groupAddUpdate;
    const wchar_t* labelExecutable;
    const wchar_t* labelLayout;
    const wchar_t* buttonUseLast;
    const wchar_t* buttonUseClass;
    const wchar_t* buttonBrowse;
    const wchar_t* buttonAdd;
    const wchar_t* buttonRemove;
    const wchar_t* buttonClose;
    const wchar_t* ruleApplyOnce;      // "apply once on switch" checkbox
    const wchar_t* ruleOnceSuffix;     // list marker on an apply-once rule
    const wchar_t* ruleReorderHint;    // drag-to-reorder hint on the list header
    const wchar_t* defaultGroup;       // "Default (when no rule matches)" group
    const wchar_t* defaultEnable;      // "use a default language" checkbox

    const wchar_t* hintIntro;
    const wchar_t* hintNeedExecutable;
    const wchar_t* hintNeedLayout;
    const wchar_t* hintWriteFailed;
    const wchar_t* hintBoundFormat;    // executable, layout
    const wchar_t* hintSelectRule;
    const wchar_t* hintRemoveFailed;
    const wchar_t* hintRemovedFormat;  // executable
    const wchar_t* hintNoLastApp;
    const wchar_t* hintNoLastClass;
    const wchar_t* hintClassRule;
    const wchar_t* hintPickLayout;

    const wchar_t* suffixIme;
    const wchar_t* suffixNotInstalled;

    const wchar_t* browseTitle;
    const wchar_t* browseFilter;       // double-null terminated

    const wchar_t* menuLanguage;       // tray "Language" submenu title
    const wchar_t* languageAuto;       // the "follow Windows" entry in that submenu
};

// The UI languages the app ships. Auto follows the Windows display language.
enum class Language { Auto = 0, English, TraditionalChinese, SimplifiedChinese, Japanese, Korean };

// Reads the UI language on first use. Any Chinese UI language selects the
// Traditional strings, which is the only Chinese translation provided.
const Strings& s();

// Override the language shown. Auto restores following the Windows display
// language. Text read after the call uses it (the tray menu and tooltip pick it
// up right away; an already-open window updates when next shown).
void set_language(Language language);

// The current override (Auto when following Windows), for the menu check mark.
Language language();

} // namespace text
