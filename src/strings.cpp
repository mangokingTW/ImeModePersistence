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
    .menuHelper = L"Enable WinUI / Admin support",
    .menuHelperActive = L"WinUI / Admin support (active)",
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
    .ruleApplyOnce = L"Apply once on switch (don't keep enforcing)",
    .ruleOnceSuffix = L"  (once)",
    .ruleReorderHint = L"Drag a rule to reorder — the topmost match wins",
    .defaultGroup = L"Default (when no rule matches)",
    .defaultEnable = L"Use a default language",

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

    .menuLanguage = L"Language",
    .languageAuto = L"Automatic (Windows)",
};

const Strings kTraditionalChinese{
    .trayTip = L"輸入法模式延續",
    .tooltipFormat = L"當前程式：%s\n綁定語言：%s\n實際語言：%s\n%s%s",
    .tooltipUnelevated = L"\n一般權限 － 看不到提權的程式",

    .menuPersist = L"跨程式維持輸入模式",
    .menuIndicator = L"在游標旁顯示輸入指示",
    .menuAutostart = L"開機時自動啟動",
    .menuElevate = L"以管理員身分重新啟動",
    .menuHelper = L"啟用現代視窗／管理員支援",
    .menuHelperActive = L"現代視窗／管理員支援（運作中）",
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
    .ruleApplyOnce = L"切換時只套用一次（不持續強制）",
    .ruleOnceSuffix = L"（一次）",
    .ruleReorderHint = L"拖曳規則可調整順序，最上方符合者優先",
    .defaultGroup = L"預設（無規則符合時）",
    .defaultEnable = L"使用預設語言",

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

    .menuLanguage = L"顯示語言",
    .languageAuto = L"自動（依 Windows）",
};

// Machine-translated, pending native-speaker review.
const Strings kSimplifiedChinese{
    .trayTip = L"输入法模式延续",
    .tooltipFormat = L"当前程序：%s\n绑定语言：%s\n实际语言：%s\n%s%s",
    .tooltipUnelevated = L"\n普通权限 － 看不到提权的程序",

    .menuPersist = L"跨程序保持输入模式",
    .menuIndicator = L"在光标旁显示输入指示",
    .menuAutostart = L"开机时自动启动",
    .menuElevate = L"以管理员身份重新启动",
    .menuHelper = L"启用现代窗口／管理员支持",
    .menuHelperActive = L"现代窗口／管理员支持（运行中）",
    .menuRules = L"程序绑定输入语言...",
    .menuLog = L"打开诊断日志",
    .menuExit = L"退出",
    .menuVersion = L"版本",

    .statusTitle = L"输入法模式延续",
    .statusFormat = L"目标模式：%s\n"
                    L"前景窗口模式：%s\n"
                    L"可读取输入法：%s\n"
                    L"当前程序：%s\n"
                    L"绑定语言：%s\n"
                    L"实际语言：%s\n"
                    L"上次切换尝试：%s\n"
                    L"以管理员身份运行：%s\n"
                    L"开机自启动：%s",
    .modeNative = L"中文",
    .modeAlphanumeric = L"英数",
    .modeUnknown = L"未知",
    .yes = L"是",
    .no = L"否",
    .unknownApplication = L"（未知）",
    .noRule = L"（无规则）",
    .autostartTask = L"计划任务（提权）",
    .autostartRegistry = L"注册表项（普通权限）",
    .autostartStartupTask = L"开机启动",
    .autostartOff = L"关闭",
    .elevatedYes = L"是",
    .elevatedNo = L"否 － 无法控制提权的程序",
    .switchOk = L"%s － 已生效",
    .switchFailed = L"%s － 该程序未理会",
    .switchNotAttempted = L"无需切换",

    .errorTitle = L"输入法模式延续",
    .errorAutostart = L"无法更新开机启动的注册表项。",
    .errorHook = L"SetWinEventHook 失败。",
    .notifyElevateTitle = L"输入法模式延续",
    .notifyElevateText = L"需要以管理员身份才能切换这个程序的输入法。"
                         L"请使用托盘菜单的“以管理员身份重新启动”。",
    .notifyDesktopTitle = L"输入法模式延续",
    .notifyDesktopText = L"这个目标需要以管理员身份运行的桌面版；应用商店版无法提权。"
                         L"下载：github.com/mangokingTW/ImeModePersistence",

    .rulesCaption = L"输入法模式延续 － 程序绑定输入语言",
    .rulesHeader = L"已绑定输入语言的程序：",
    .groupAddUpdate = L"新增或更新绑定",
    .labelExecutable = L"程序或类别：",
    .labelLayout = L"输入语言：",
    .buttonUseLast = L"用刚才的程序(&L)",
    .buttonUseClass = L"用窗口类别(&C)",
    .buttonBrowse = L"浏览(&B)...",
    .buttonAdd = L"新增／更新(&A)",
    .buttonRemove = L"删除选中的规则(&R)",
    .buttonClose = L"关闭",
    .ruleApplyOnce = L"切换时只应用一次（不持续强制）",
    .ruleOnceSuffix = L"（一次）",
    .ruleReorderHint = L"拖动规则可调整顺序，最上方符合者优先",
    .defaultGroup = L"默认（无规则符合时）",
    .defaultEnable = L"使用默认语言",

    .hintIntro = L"绑定后，切到该程序就会自动切换到绑定的输入语言。同一语言装了多个输入法（例如拼音与五笔）时，会使用第一个已安装的。",
    .hintNeedExecutable = L"请输入路径、可执行文件名（例如 notepad.exe），或 class: 开头的类别键值。"
                          L"通配符：glob:*\\game.exe（路径）或 class-glob:name_*（类别），支持 * 与 ?。",
    .hintNeedLayout = L"尚未选择输入语言。",
    .hintWriteFailed = L"无法将规则写入注册表。",
    .hintBoundFormat = L"%s 已绑定到 %s。",
    .hintSelectRule = L"请先选中要删除的规则。",
    .hintRemoveFailed = L"无法从注册表删除规则。",
    .hintRemovedFormat = L"已删除 %s 的规则。",
    .hintNoLastApp = L"还没有其他程序进入过前景。",
    .hintNoLastClass = L"还没有取得任何窗口类别。",
    .hintClassRule = L"会匹配这个类别的任何窗口。适用于读不到可执行文件的情况，例如带反作弊的游戏。",
    .hintPickLayout = L"选择输入语言后按“新增／更新”。",

    .suffixIme = L"（输入法）",
    .suffixNotInstalled = L"（未安装）",

    .browseTitle = L"选择程序",
    .browseFilter = L"程序\0*.exe\0所有文件\0*.*\0",

    .menuLanguage = L"显示语言",
    .languageAuto = L"自动（依 Windows）",
};

// Machine-translated, pending native-speaker review.
const Strings kJapanese{
    .trayTip = L"IME モード維持",
    .tooltipFormat = L"アプリ：%s\nバインド言語：%s\n現在の言語：%s\n%s%s",
    .tooltipUnelevated = L"\n通常権限 － 昇格したプログラムは見えません",

    .menuPersist = L"ウィンドウ間でモードを維持",
    .menuIndicator = L"カーソル横に入力インジケーターを表示",
    .menuAutostart = L"ログオン時に自動起動",
    .menuElevate = L"管理者として再起動",
    .menuHelper = L"WinUI／管理者サポートを有効化",
    .menuHelperActive = L"WinUI／管理者サポート（動作中）",
    .menuRules = L"アプリの入力言語バインド...",
    .menuLog = L"診断ログを開く",
    .menuExit = L"終了",
    .menuVersion = L"バージョン",

    .statusTitle = L"IME モード維持",
    .statusFormat = L"目標モード：%s\n"
                    L"前面ウィンドウのモード：%s\n"
                    L"IME 取得可否：%s\n"
                    L"現在のアプリ：%s\n"
                    L"バインド言語：%s\n"
                    L"現在の言語：%s\n"
                    L"前回の切り替え：%s\n"
                    L"管理者として実行：%s\n"
                    L"自動起動：%s",
    .modeNative = L"かな",
    .modeAlphanumeric = L"英数",
    .modeUnknown = L"不明",
    .yes = L"はい",
    .no = L"いいえ",
    .unknownApplication = L"（不明）",
    .noRule = L"（ルールなし）",
    .autostartTask = L"タスク（昇格）",
    .autostartRegistry = L"レジストリ（通常権限）",
    .autostartStartupTask = L"スタートアップ タスク",
    .autostartOff = L"オフ",
    .elevatedYes = L"はい",
    .elevatedNo = L"いいえ － 昇格したプログラムは制御できません",
    .switchOk = L"%s － 反映されました",
    .switchFailed = L"%s － アプリに無視されました",
    .switchNotAttempted = L"切り替え不要",

    .errorTitle = L"IME モード維持",
    .errorAutostart = L"自動起動のレジストリ エントリを更新できませんでした。",
    .errorHook = L"SetWinEventHook に失敗しました。",
    .notifyElevateTitle = L"IME モード維持",
    .notifyElevateText = L"このプログラムの入力を切り替えるには管理者権限が必要です。"
                         L"トレイ メニューの「管理者として再起動」を使ってください。",
    .notifyDesktopTitle = L"IME モード維持",
    .notifyDesktopText = L"この対象には管理者として実行するデスクトップ版が必要です。ストア版は昇格できません。"
                         L"入手：github.com/mangokingTW/ImeModePersistence",

    .rulesCaption = L"IME モード維持 － アプリの入力言語バインド",
    .rulesHeader = L"入力言語にバインドされたアプリ：",
    .groupAddUpdate = L"バインドの追加・更新",
    .labelExecutable = L"アプリまたはクラス：",
    .labelLayout = L"入力言語：",
    .buttonUseLast = L"直前のアプリを使う(&L)",
    .buttonUseClass = L"ウィンドウ クラスを使う(&C)",
    .buttonBrowse = L"参照(&B)...",
    .buttonAdd = L"追加／更新(&A)",
    .buttonRemove = L"選択したルールを削除(&R)",
    .buttonClose = L"閉じる",
    .ruleApplyOnce = L"切り替え時に一度だけ適用（以降は強制しない）",
    .ruleOnceSuffix = L"（一度）",
    .ruleReorderHint = L"ドラッグで並べ替え。上にあるものほど優先",
    .defaultGroup = L"既定（ルールに一致しないとき）",
    .defaultEnable = L"既定の言語を使う",

    .hintIntro = L"アプリを言語にバインドすると、そのアプリをアクティブにしたときにその言語へ切り替わります。1つの言語に複数の IME がある場合は最初のものが使われます。",
    .hintNeedExecutable = L"パス、実行ファイル名（例：notepad.exe）、または class: で始まるクラスキーを入力してください。"
                          L"ワイルドカード：glob:*\\game.exe（パス）または class-glob:name_*（クラス）、* と ? に対応。",
    .hintNeedLayout = L"入力言語が選択されていません。",
    .hintWriteFailed = L"ルールをレジストリに書き込めませんでした。",
    .hintBoundFormat = L"%s を %s にバインドしました。",
    .hintSelectRule = L"削除するルールを選択してください。",
    .hintRemoveFailed = L"レジストリからルールを削除できませんでした。",
    .hintRemovedFormat = L"%s のルールを削除しました。",
    .hintNoLastApp = L"まだ他のアプリが前面になっていません。",
    .hintNoLastClass = L"まだウィンドウ クラスを取得していません。",
    .hintClassRule = L"このクラスの任意のウィンドウに一致します。アンチチート保護されたゲームなど、実行ファイルを読み取れない場合に使います。",
    .hintPickLayout = L"入力言語を選んでから「追加／更新」を押してください。",

    .suffixIme = L"（IME）",
    .suffixNotInstalled = L"（未インストール）",

    .browseTitle = L"アプリを選択",
    .browseFilter = L"プログラム\0*.exe\0すべてのファイル\0*.*\0",

    .menuLanguage = L"表示言語",
    .languageAuto = L"自動（Windows に従う）",
};

// Machine-translated, pending native-speaker review.
const Strings kKorean{
    .trayTip = L"IME 모드 유지",
    .tooltipFormat = L"앱: %s\n바인딩 언어: %s\n현재 언어: %s\n%s%s",
    .tooltipUnelevated = L"\n일반 권한 － 권한 상승된 프로그램은 보이지 않음",

    .menuPersist = L"창 간 모드 유지",
    .menuIndicator = L"커서 옆에 입력 표시기 표시",
    .menuAutostart = L"로그온 시 자동 시작",
    .menuElevate = L"관리자 권한으로 다시 시작",
    .menuHelper = L"WinUI／관리자 지원 활성화",
    .menuHelperActive = L"WinUI／관리자 지원（작동 중）",
    .menuRules = L"앱 입력 언어 바인딩...",
    .menuLog = L"진단 로그 열기",
    .menuExit = L"종료",
    .menuVersion = L"버전",

    .statusTitle = L"IME 모드 유지",
    .statusFormat = L"목표 모드: %s\n"
                    L"전경 창 모드: %s\n"
                    L"IME 접근 가능: %s\n"
                    L"현재 앱: %s\n"
                    L"바인딩 언어: %s\n"
                    L"현재 언어: %s\n"
                    L"마지막 전환 시도: %s\n"
                    L"관리자 권한 실행: %s\n"
                    L"자동 시작: %s",
    .modeNative = L"한글",
    .modeAlphanumeric = L"영문",
    .modeUnknown = L"알 수 없음",
    .yes = L"예",
    .no = L"아니요",
    .unknownApplication = L"(알 수 없음)",
    .noRule = L"(규칙 없음)",
    .autostartTask = L"예약 작업(권한 상승)",
    .autostartRegistry = L"레지스트리 항목(일반 권한)",
    .autostartStartupTask = L"시작 작업",
    .autostartOff = L"꺼짐",
    .elevatedYes = L"예",
    .elevatedNo = L"아니요 － 권한 상승된 프로그램은 제어할 수 없음",
    .switchOk = L"%s － 적용됨",
    .switchFailed = L"%s － 앱이 무시함",
    .switchNotAttempted = L"전환 불필요",

    .errorTitle = L"IME 모드 유지",
    .errorAutostart = L"자동 시작 레지스트리 항목을 업데이트할 수 없습니다.",
    .errorHook = L"SetWinEventHook 실패.",
    .notifyElevateTitle = L"IME 모드 유지",
    .notifyElevateText = L"이 프로그램의 입력을 전환하려면 관리자 권한이 필요합니다. "
                         L"트레이 메뉴의 “관리자 권한으로 다시 시작”을 사용하세요.",
    .notifyDesktopTitle = L"IME 모드 유지",
    .notifyDesktopText = L"이 대상은 관리자 권한으로 실행하는 데스크톱 버전이 필요합니다. 스토어 버전은 권한 상승이 불가합니다. "
                         L"다운로드: github.com/mangokingTW/ImeModePersistence",

    .rulesCaption = L"IME 모드 유지 － 앱 입력 언어 바인딩",
    .rulesHeader = L"입력 언어에 바인딩된 앱:",
    .groupAddUpdate = L"바인딩 추가 또는 업데이트",
    .labelExecutable = L"앱 또는 클래스:",
    .labelLayout = L"입력 언어:",
    .buttonUseLast = L"최근 앱 사용(&L)",
    .buttonUseClass = L"창 클래스 사용(&C)",
    .buttonBrowse = L"찾아보기(&B)...",
    .buttonAdd = L"추가／업데이트(&A)",
    .buttonRemove = L"선택한 규칙 삭제(&R)",
    .buttonClose = L"닫기",
    .ruleApplyOnce = L"전환 시 한 번만 적용(계속 강제하지 않음)",
    .ruleOnceSuffix = L" (한 번)",
    .ruleReorderHint = L"드래그하여 순서 변경 — 위쪽이 우선",
    .defaultGroup = L"기본값(규칙이 일치하지 않을 때)",
    .defaultEnable = L"기본 언어 사용",

    .hintIntro = L"앱을 언어에 바인딩하면 해당 앱을 활성화할 때 그 언어로 전환됩니다. 한 언어에 여러 IME가 설치된 경우 첫 번째 것이 사용됩니다.",
    .hintNeedExecutable = L"경로, 실행 파일 이름(예: notepad.exe), 또는 class: 로 시작하는 클래스 키를 입력하세요. "
                          L"와일드카드: glob:*\\game.exe(경로) 또는 class-glob:name_*(클래스), * 와 ? 지원.",
    .hintNeedLayout = L"입력 언어가 선택되지 않았습니다.",
    .hintWriteFailed = L"규칙을 레지스트리에 쓸 수 없습니다.",
    .hintBoundFormat = L"%s 이(가) %s 에 바인딩되었습니다.",
    .hintSelectRule = L"삭제할 규칙을 선택하세요.",
    .hintRemoveFailed = L"레지스트리에서 규칙을 삭제할 수 없습니다.",
    .hintRemovedFormat = L"%s 의 규칙을 삭제했습니다.",
    .hintNoLastApp = L"아직 다른 앱이 전경에 온 적이 없습니다.",
    .hintNoLastClass = L"아직 창 클래스를 가져오지 못했습니다.",
    .hintClassRule = L"이 클래스의 모든 창과 일치합니다. 안티치트 보호 게임처럼 실행 파일을 읽을 수 없을 때 사용하세요.",
    .hintPickLayout = L"입력 언어를 선택한 후 “추가／업데이트”를 누르세요.",

    .suffixIme = L" (IME)",
    .suffixNotInstalled = L" (설치되지 않음)",

    .browseTitle = L"앱 선택",
    .browseFilter = L"프로그램\0*.exe\0모든 파일\0*.*\0",

    .menuLanguage = L"표시 언어",
    .languageAuto = L"자동（Windows 따름）",
};

// The UI language, not the locale: someone with an English Windows and a
// Taiwanese locale is reading English menus everywhere else. Chinese splits by
// sublanguage into Traditional (TW/HK/MO) and Simplified (everything else).
const Strings& detect() {
    const LANGID lang = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(lang)) {
    case LANG_CHINESE:
        switch (SUBLANGID(lang)) {
        case SUBLANG_CHINESE_TRADITIONAL:
        case SUBLANG_CHINESE_HONGKONG:
        case SUBLANG_CHINESE_MACAU:
            return kTraditionalChinese;
        default:
            return kSimplifiedChinese;
        }
    case LANG_JAPANESE:
        return kJapanese;
    case LANG_KOREAN:
        return kKorean;
    default:
        return kEnglish;
    }
}

// The chosen language, plus a cache so s() stays a cheap pointer read. g_override
// is Auto until set_language() says otherwise; Auto means follow detect().
Language g_override = Language::Auto;
const Strings* g_current = nullptr;

const Strings& resolve() {
    switch (g_override) {
    case Language::English:            return kEnglish;
    case Language::TraditionalChinese: return kTraditionalChinese;
    case Language::SimplifiedChinese:  return kSimplifiedChinese;
    case Language::Japanese:           return kJapanese;
    case Language::Korean:             return kKorean;
    case Language::Auto:               break;
    }
    return detect();
}

} // namespace

const Strings& s() {
    if (!g_current) {
        g_current = &resolve();
    }
    return *g_current;
}

void set_language(Language lang) {
    g_override = lang;
    g_current = &resolve();
}

Language language() {
    return g_override;
}

} // namespace text
