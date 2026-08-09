#include "strings.h"

namespace text {
namespace {

const Strings kEnglish{
    .trayTip = L"IME Mode Persistence",

    .menuAutostart = L"Start with Windows",
    .menuRules = L"Application rules...",
    .menuExit = L"Exit",

    .statusTitle = L"IME Mode Persistence",
    .statusFormat = L"Desired mode: %s\n"
                    L"Foreground mode: %s\n"
                    L"IME reachable: %s\n"
                    L"Application: %s\n"
                    L"Bound layout: %s",
    .modeNative = L"Native",
    .modeAlphanumeric = L"Alphanumeric",
    .modeUnknown = L"Unknown",
    .yes = L"yes",
    .no = L"no",
    .unknownApplication = L"(unknown)",
    .noRule = L"(no rule)",

    .errorTitle = L"ImeModePersistence",
    .errorAutostart = L"Could not update the Run registry entry.",
    .errorHook = L"SetWinEventHook failed.",

    .rulesCaption = L"ImeModePersistence - application rules",
    .rulesHeader = L"Applications bound to a keyboard layout:",
    .labelExecutable = L"Executable:",
    .labelLayout = L"Layout:",
    .buttonUseLast = L"Use &last app",
    .buttonBrowse = L"&Browse...",
    .buttonAdd = L"&Add / update",
    .buttonRemove = L"&Remove selected",
    .buttonClose = L"Close",

    .hintIntro = L"A rule binds an application to a language. Where one language "
                 L"has several IMEs, the first installed one is used.",
    .hintNeedExecutable = L"Type an executable name such as notepad.exe, or use Browse.",
    .hintNeedLayout = L"No keyboard layout is selected.",
    .hintWriteFailed = L"Could not write the rule to the registry.",
    .hintBoundFormat = L"%s is now bound to %s.",
    .hintSelectRule = L"Select a rule to remove.",
    .hintRemoveFailed = L"Could not remove the rule from the registry.",
    .hintRemovedFormat = L"Removed the rule for %s.",
    .hintNoLastApp = L"No other application has been in the foreground yet.",
    .hintPickLayout = L"Pick a layout, then choose Add / update.",
    .hintNameOnly = L"Rules match the file name, so the folder you picked does not matter.",

    .suffixIme = L" (IME)",
    .suffixNotInstalled = L"  (not installed)",

    .browseTitle = L"Select an application",
    .browseFilter = L"Programs\0*.exe\0All files\0*.*\0",
};

const Strings kTraditionalChinese{
    .trayTip = L"輸入法模式延續",

    .menuAutostart = L"開機時自動啟動",
    .menuRules = L"程式規則...",
    .menuExit = L"結束",

    .statusTitle = L"輸入法模式延續",
    .statusFormat = L"目標模式：%s\n"
                    L"前景視窗模式：%s\n"
                    L"可讀取輸入法：%s\n"
                    L"程式：%s\n"
                    L"綁定配置：%s",
    .modeNative = L"中文",
    .modeAlphanumeric = L"英數",
    .modeUnknown = L"未知",
    .yes = L"是",
    .no = L"否",
    .unknownApplication = L"（未知）",
    .noRule = L"（無規則）",

    .errorTitle = L"輸入法模式延續",
    .errorAutostart = L"無法更新開機啟動的登錄項目。",
    .errorHook = L"SetWinEventHook 失敗。",

    .rulesCaption = L"輸入法模式延續 － 程式規則",
    .rulesHeader = L"已綁定鍵盤配置的程式：",
    .labelExecutable = L"執行檔：",
    .labelLayout = L"配置：",
    .buttonUseLast = L"用剛才的程式(&L)",
    .buttonBrowse = L"瀏覽(&B)...",
    .buttonAdd = L"新增／更新(&A)",
    .buttonRemove = L"刪除選取的規則(&R)",
    .buttonClose = L"關閉",

    .hintIntro = L"規則把程式綁定到一個語言。同一語言裝了多個輸入法時，會使用第一個已安裝的。",
    .hintNeedExecutable = L"請輸入執行檔名稱（例如 notepad.exe），或按「瀏覽」選取。",
    .hintNeedLayout = L"尚未選擇鍵盤配置。",
    .hintWriteFailed = L"無法將規則寫入登錄。",
    .hintBoundFormat = L"%s 已綁定到 %s。",
    .hintSelectRule = L"請先選取要刪除的規則。",
    .hintRemoveFailed = L"無法從登錄刪除規則。",
    .hintRemovedFormat = L"已刪除 %s 的規則。",
    .hintNoLastApp = L"還沒有其他程式進入過前景。",
    .hintPickLayout = L"選擇配置後按「新增／更新」。",
    .hintNameOnly = L"規則比對的是檔案名稱，所以你選的資料夾位置不影響結果。",

    .suffixIme = L"（輸入法）",
    .suffixNotInstalled = L"（未安裝）",

    .browseTitle = L"選擇程式",
    .browseFilter = L"程式\0*.exe\0所有檔案\0*.*\0",
};

bool prefers_chinese() {
    // The UI language, not the locale: someone with an English Windows and a
    // Taiwanese locale is reading English menus everywhere else.
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

} // namespace

const Strings& s() {
    static const Strings& chosen = prefers_chinese() ? kTraditionalChinese : kEnglish;
    return chosen;
}

} // namespace text
