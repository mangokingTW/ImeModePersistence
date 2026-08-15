#include "strings.h"

namespace text {
namespace {

const Strings kEnglish{
    .trayTip = L"IME Mode Persistence",
    .tooltipFormat = L"App: %s\nBound language: %s\nActive language: %s\n%s%s",
    .tooltipUnelevated = L"\nNormal privileges - elevated programs are invisible",

    .menuPersist = L"Keep mode across windows",
    .menuIndicator = L"Show input indicator at the cursor",
    .menuAutostart = L"Start automatically at logon",
    .menuElevate = L"Restart as administrator",
    .menuRules = L"App language bindings...",
    .menuLog = L"Open diagnostic log",
    .menuExit = L"Exit",
    .menuVersion = L"Version",

    .statusTitle = L"IME Mode Persistence",
    .statusFormat = L"Desired mode: %s\n"
                    L"Foreground mode: %s\n"
                    L"IME reachable: %s\n"
                    L"Current app: %s\n"
                    L"Bound language: %s\n"
                    L"Active language: %s\n"
                    L"Last switch attempt: %s\n"
                    L"Running as administrator: %s\n"
                    L"Autostart: %s",
    .modeNative = L"Native",
    .modeAlphanumeric = L"Alphanumeric",
    .modeUnknown = L"Unknown",
    .yes = L"yes",
    .no = L"no",
    .unknownApplication = L"(unknown)",
    .noRule = L"(no rule)",
    .autostartTask = L"scheduled task, elevated",
    .autostartRegistry = L"registry entry, normal privileges",
    .autostartStartupTask = L"startup task",
    .autostartOff = L"off",
    .elevatedYes = L"yes",
    .elevatedNo = L"no - cannot control elevated programs",
    .switchOk = L"%s - took effect",
    .switchFailed = L"%s - ignored by the application",
    .switchNotAttempted = L"none needed",

    .errorTitle = L"ImeModePersistence",
    .errorAutostart = L"Could not update the Run registry entry.",
    .errorHook = L"SetWinEventHook failed.",
    .notifyElevateTitle = L"IME Mode Persistence",
    .notifyElevateText = L"This program needs administrator access to switch its input. "
                         L"Use \"Restart as administrator\" in the tray menu.",
    .notifyDesktopTitle = L"IME Mode Persistence",
    .notifyDesktopText = L"This target needs the desktop version run as administrator; "
                         L"the Store build can't elevate. Get it at "
                         L"github.com/mangokingTW/ImeModePersistence",

    .rulesCaption = L"ImeModePersistence - app language bindings",
    .rulesHeader = L"Applications bound to an input language:",
    .groupAddUpdate = L"Add or update a binding",
    .labelExecutable = L"Application or class:",
    .labelLayout = L"Language:",
    .buttonUseLast = L"Use &last app",
    .buttonUseClass = L"Use window &class",
    .buttonBrowse = L"&Browse...",
    .buttonAdd = L"&Add / update",
    .buttonRemove = L"&Remove selected",
    .buttonClose = L"Close",

    .hintIntro = L"An application bound to a language switches to it when you "
                 L"activate that application. Where one language has several IMEs "
                 L"installed, the first is used.",
    .hintNeedExecutable = L"Enter a path, an executable name such as notepad.exe, or a class: key. "
                          L"Wildcards: glob:*\\game.exe (path) or class-glob:name_* (class); * and ?.",
    .hintNeedLayout = L"No input language is selected.",
    .hintWriteFailed = L"Could not write the rule to the registry.",
    .hintBoundFormat = L"%s is now bound to %s.",
    .hintSelectRule = L"Select a rule to remove.",
    .hintRemoveFailed = L"Could not remove the rule from the registry.",
    .hintRemovedFormat = L"Removed the rule for %s.",
    .hintNoLastApp = L"No other application has been in the foreground yet.",
    .hintNoLastClass = L"No window class has been seen yet.",
    .hintClassRule = L"Matches any window of this class. Use it when the executable "
                     L"cannot be read, as with anti-cheat protected games.",
    .hintPickLayout = L"Pick a language, then choose Add / update.",

    .suffixIme = L" (IME)",
    .suffixNotInstalled = L"  (not installed)",

    .browseTitle = L"Select an application",
    .browseFilter = L"Programs\0*.exe\0All files\0*.*\0",
};

const Strings kTraditionalChinese{
    .trayTip = L"輸入法模式延續",
    .tooltipFormat = L"當前程式：%s\n綁定語言：%s\n實際語言：%s\n%s%s",
    .tooltipUnelevated = L"\n一般權限 － 看不到提權的程式",

    .menuPersist = L"跨程式維持輸入模式",
    .menuIndicator = L"在游標旁顯示輸入指示",
    .menuAutostart = L"開機時自動啟動",
    .menuElevate = L"以管理員身分重新啟動",
    .menuRules = L"程式綁定輸入語言...",
    .menuLog = L"開啟診斷記錄",
    .menuExit = L"結束",
    .menuVersion = L"版本",

    .statusTitle = L"輸入法模式延續",
    .statusFormat = L"目標模式：%s\n"
                    L"前景視窗模式：%s\n"
                    L"可讀取輸入法：%s\n"
                    L"當前程式：%s\n"
                    L"綁定語言：%s\n"
                    L"實際語言：%s\n"
                    L"上次切換嘗試：%s\n"
                    L"以管理員身分執行：%s\n"
                    L"開機自動啟動：%s",
    .modeNative = L"中文",
    .modeAlphanumeric = L"英數",
    .modeUnknown = L"未知",
    .yes = L"是",
    .no = L"否",
    .unknownApplication = L"（未知）",
    .noRule = L"（無規則）",
    .autostartTask = L"排程工作（提權）",
    .autostartRegistry = L"登錄項目（一般權限）",
    .autostartStartupTask = L"開機啟動",
    .autostartOff = L"關閉",
    .elevatedYes = L"是",
    .elevatedNo = L"否 － 無法控制提權的程式",
    .switchOk = L"%s － 已生效",
    .switchFailed = L"%s － 該程式未理會",
    .switchNotAttempted = L"不需切換",

    .errorTitle = L"輸入法模式延續",
    .errorAutostart = L"無法更新開機啟動的登錄項目。",
    .errorHook = L"SetWinEventHook 失敗。",
    .notifyElevateTitle = L"輸入法模式延續",
    .notifyElevateText = L"需要以管理員身分才能切換這個程式的輸入法。"
                         L"請用托盤選單的「以管理員身分重新啟動」。",
    .notifyDesktopTitle = L"輸入法模式延續",
    .notifyDesktopText = L"這個目標需要以系統管理員執行的桌面版;市集版無法提權。"
                         L"下載:github.com/mangokingTW/ImeModePersistence",

    .rulesCaption = L"輸入法模式延續 － 程式綁定輸入語言",
    .rulesHeader = L"已綁定輸入語言的程式：",
    .groupAddUpdate = L"新增或更新綁定",
    .labelExecutable = L"程式或類別：",
    .labelLayout = L"輸入語言：",
    .buttonUseLast = L"用剛才的程式(&L)",
    .buttonUseClass = L"用視窗類別(&C)",
    .buttonBrowse = L"瀏覽(&B)...",
    .buttonAdd = L"新增／更新(&A)",
    .buttonRemove = L"刪除選取的規則(&R)",
    .buttonClose = L"關閉",

    .hintIntro = L"綁定後，切到該程式就會自動切換到綁定的輸入語言。同一語言裝了多個輸入法（例如注音與倉頡）時，會使用第一個已安裝的。",
    .hintNeedExecutable = L"請輸入路徑、執行檔名稱（例如 notepad.exe），或 class: 開頭的類別鍵值。"
                          L"萬用字元：glob:*\\game.exe（路徑）或 class-glob:name_*（類別），支援 * 與 ?。",
    .hintNeedLayout = L"尚未選擇輸入語言。",
    .hintWriteFailed = L"無法將規則寫入登錄。",
    .hintBoundFormat = L"%s 已綁定到 %s。",
    .hintSelectRule = L"請先選取要刪除的規則。",
    .hintRemoveFailed = L"無法從登錄刪除規則。",
    .hintRemovedFormat = L"已刪除 %s 的規則。",
    .hintNoLastApp = L"還沒有其他程式進入過前景。",
    .hintNoLastClass = L"還沒有取得任何視窗類別。",
    .hintClassRule = L"會比對這個類別的任何視窗。適用於讀不到執行檔的情況，例如有反作弊的遊戲。",
    .hintPickLayout = L"選擇輸入語言後按「新增／更新」。",

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
