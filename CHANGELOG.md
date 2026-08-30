# Changelog

Per-version **highlights** live under a `## <tag>` heading (e.g. `## v1.0.4`),
bilingual (English + 繁體中文). At release time the workflow takes that section
and appends a consistent install / verify section, so the published GitHub
release note reads as a finished, formatted note rather than a raw changelog.
Keep each section to what changed; the boilerplate is added automatically. If a
tag has no section here, the workflow falls back to auto-generated notes.

## v1.5.8

Store availability for the native ARM64 build:

- Microsoft Store now carries the native ARM64 build. The ARM64 version shipped with v1.5.7 on GitHub, but that release never completed Store certification, so Store users were still receiving v1.5.6 and the emulated x64 build. Installing or updating from the Store now gets the native build on Snapdragon and other Windows-on-ARM devices.
- Portable archive names: the download files are now `-win-x64.zip` and `-win-arm64.zip` (previously `-x64.zip` and `-arm64.zip`).

繁體中文:原生 ARM64 版本上架微軟商店:

- 微軟商店已提供原生 ARM64 版本。ARM64 版本雖在 v1.5.7 隨 GitHub 發布,但該版未完成商店認證,因此商店使用者仍停留在 v1.5.6 的 x64 模擬版。現在從商店安裝或更新,Snapdragon 等 Windows on ARM 裝置即可取得原生版本。
- 免安裝壓縮檔更名:下載檔名改為 `-win-x64.zip` 與 `-win-arm64.zip`（原為 `-x64.zip` 與 `-arm64.zip`）。

## v1.5.7

Native ARM64 support:

- Native ARM64 build: Snapdragon and other Windows-on-ARM devices now run a native build instead of the emulated x64 one. Available as the portable `-arm64.zip` and, for Microsoft Store installs, inside the package the Store now serves — the Store hands each device the matching architecture automatically, so nothing changes for existing x64 users.
- ARM64 verification in CI: Each release additionally runs the full real-IME end-to-end suite and screen recording on an ARM64 runner.

繁體中文:原生 ARM64 支援：

- 原生 ARM64 版本：Snapdragon 等 Windows on ARM 裝置改以原生版本執行，不再透過 x64 模擬。提供免安裝的 `-arm64.zip`；微軟商店安裝亦已納入，商店會自動依裝置架構配送對應版本,現有 x64 使用者不受影響。
- ARM64 自動驗證：每次發布會額外在 ARM64 環境完整執行真實輸入法端對端測試與畫面錄影驗證。

## v1.5.6

Microsoft Store visual assets and project maintenance:

- Store listing trailers: Added multi-language preview video and live demonstration assets for Traditional Chinese, English, Simplified Chinese, Japanese, and Korean Store listings.

繁體中文:微軟商店視覺資源與維護更新：

- 應用商店預覽影片：為繁體中文、英文、簡體中文、日文與韓文等微軟商店頁面配置即時動態預覽影片與在地化標題展示。

## v1.5.5

Documentation and project maintenance release:

- Documentation & preview assets: Updated README and Microsoft Store listings with live demonstrations of input mode persistence and caret indicators.

繁體中文:說明文件與維護更新：

- 說明文件與預覽資源更新：更新專案首頁與應用商店資訊，提供直觀的輸入法模式維持與游標指示器動態預覽。

## v1.5.4

Fix shell window detection for tray overflow and modern flyouts:

- Tray overflow shell filter: Added window class detection for Windows 11 modern tray overflow flyouts (`TopLevelWindowForOverflowXamlIsland`), classic overflow flyouts (`NotifyIconOverflowWindow`), XAML popups, and shell context menus.
- Robust Explorer process classification: Prevents tray overflow and shell interactions from overwriting the last observed foreground application with `explorer.exe`, ensuring rule dialog prefill and IME mode persistence accurately capture target applications (e.g. Discord or Chrome).

繁體中文:修正系統匣溢位選單與現代快顯視窗之 Shell 視窗過濾判定：

- 系統匣溢位選單過濾：新增對 Windows 11 現代 XAML 托盤溢位選單（`TopLevelWindowForOverflowXamlIsland`）、經典溢位視窗（`NotifyIconOverflowWindow`）、XAML 彈出視窗與右鍵選單之視窗類別辨識。
- 強化 Explorer 行程判定：防止展開托盤隱藏圖示選單時將前一個目標應用程式誤判並覆蓋為 `explorer.exe`，確保規則新增對話框能精準預填使用者先前的應用程式（如 Discord 或 Chrome）。

## v1.5.3

Unified dual-mode installer & Sidecar Helper layout switching:

- Unified dual-mode installer: Merged separate `setup-admin.exe` and `setup-user.exe` into a single `ImeModePersistence-<version>-setup.exe` installer with `PrivilegesRequiredOverridesAllowed=dialog commandline`. Interactive installs offer per-user (no UAC required) or all-users (Program Files, elevated) choices; package managers use `/CURRENTUSER` or `/ALLUSERS`.
- Layout switching via Sidecar Helper: Extended Helper IPC protocol with `SwitchLayout` command, allowing unelevated desktop and Microsoft Store packaged builds to switch keyboard layouts for elevated or anti-cheat protected fullscreen games (such as Helldivers 2 with `class:stingray_window`) across UIPI boundaries.
- Desktop Helper menu availability: Made "Enable WinUI/Admin support..." (Sidecar Helper) available across all unelevated builds with toggle stop support.

繁體中文:二合一安裝程式與 Sidecar Helper 鍵盤配置切換支援：

- 二合一安裝程式：將原先分開的 `setup-admin.exe` 與 `setup-user.exe` 合併為單一 `ImeModePersistence-<version>-setup.exe`。互動安裝時提供「為目前使用者安裝（免提權）」與「為全機所有使用者安裝（需要管理員提權）」選項；命令列支援 `/CURRENTUSER` 與 `/ALLUSERS` 參數。
- 跨 UIPI 鍵盤配置切換：擴充 Sidecar Helper IPC 協定支援 `SwitchLayout` 指令，使一般權限桌面版與 Store 版在啟用 Helper 輔助服務後，能跨越 UIPI 屏障為高權限 / 反作弊全螢幕遊戲（如 Helldivers 2 搭配 `class:stingray_window`）自動切換與綁定語言。
- 桌面版選單支援 Helper：所有未提權版本皆一律顯示「啟用現代視窗 (WinUI) 支援…」選單項目，並支援再次點擊關閉。

## v1.5.2

Reliability fixes for Sidecar Helper IPC and shutdown lifecycle:

- Synchronous IPC pipe: Reverted named pipe from overlapped I/O to fully synchronous I/O, guaranteeing accurate byte counts for `ReadFile`/`WriteFile` and resolving intermittent IME state persistence failures on modern/WinUI applications.
- Clean shutdown watchdog: Upgraded parent process watchdog thread to listen on both parent process termination and internal shutdown events, eliminating potential join deadlocks on normal application exit or manual helper stop.

繁體中文:Sidecar Helper IPC 通訊與關閉生命週期可靠度修正：

- 同步 IPC 管道：具名管道回歸純同步 I/O，確保 `ReadFile`/`WriteFile` 傳輸位元組精確可靠，解決現代 / WinUI 應用程式（如記事本）偶發性 IME 狀態維持失效問題。
- 乾淨關閉守護執行緒：父行程監聽執行緒改為同時等待父行程終止與內部關閉事件，徹底消除手動停止或正常關閉時的執行緒卡死問題。

## v1.5.1

Security hardening and lifecycle improvements for Sidecar Helper:

- Hardened elevation path: Sidecar Helper now elevates directly from the original executable path, eliminating writable AppData copy and preventing potential local privilege escalation (LPE) / TOCTOU risks.
- Restricted IPC access: Tightened named pipe security descriptor (SDDL) to interactive users and administrators only.
- Parent process watchdog: Helper service now monitors the main application's process lifecycle and cleanly terminates when the parent process exits.
- Improved data integrity: Upgraded window handle passing in IPC responses to 64-bit HWND.

繁體中文:Sidecar Helper 輔助服務之安全加固與生命週期改善：

- 提權路徑加固：Sidecar Helper 改為直接對原始執行檔路徑提權，移除 AppData 複本機制，消滅潛在的本機提權（LPE）與 TOCTOU 風險。
- IPC 連線權限收斂：收斂具名管道 SDDL 安全描述元，僅允許當前互動登入者與管理員連線。
- 孤兒行程守護：Helper 服務新增父行程生命週期監聽，於主程式結束或崩潰時自動退出。
- 資料完整性提升：IPC 回應之視窗代碼欄位升級為 64 位元 HWND。

## v1.5.0

Microsoft Store (MSIX) and non-elevated builds now support modern (WinUI/TSF) apps via Sidecar Helper:

- Added Sidecar Helper architecture: Microsoft Store and non-elevated users can now enable "WinUI / Admin support" directly from the system tray menu. It launches a lightweight elevated background helper service via standard Windows UAC without requiring third-party code signing certificates.
- Full bidirectional read/write synchronization: Accurately reads and persists IME conversion mode for modern WinUI 3 / XAML / TSF applications (such as Windows 11 modern Notepad `RichEditD2DPT` child focus controls) as well as elevated windows across window switches.

繁體中文:Microsoft Store（市集版）與非提權環境現已支援現代（WinUI/TSF）視窗：

- 新增 Sidecar Helper 輔助服務架構：市集版與一般權限使用者現在可直接從系統匣選單點選「啟用現代視窗／管理員支援」，透過標準 Windows UAC 啟動輕量提權背景服務，免自備付費代碼簽章憑證。
- 完整雙向讀寫同步：精準讀取並延續 Windows 11 現代記事本（WinUI 3 / XAML / TSF 子焦點控制項 `RichEditD2DPT`）與高權限視窗的中／英輸入法轉換模式。

## v1.4.1

(v1.4.0 was an experimental in-process TSF text-service beta; it was withdrawn as
unnecessary. This is the next stable release, built on v1.3.1.)

Input-mode persistence now reaches modern (TSF/WinUI) apps when run elevated:

- Fixed: the mode you carry is now applied to packaged/WinUI apps such as the
  modern Notepad. Their real text control ignores a mode written to the
  top-level window (the write "verified" but typing stayed in the wrong mode);
  the mode is now written to the focused control's input context, which actually
  takes. This needs the app to run **as administrator** -- that input context is
  reachable only from a high-integrity process (the same reason a protected game
  needs it). Run unelevated, behaviour is unchanged from v1.3.1: classic and
  Chromium apps keep working, and modern-Notepad-class apps are simply left as
  they were (no hang, no change).

繁體中文:提升權限執行時,輸入模式維持現在能作用到現代(TSF/WinUI)程式:

- 修正:維持的模式現在能套用到 packaged/WinUI 程式(如新版記事本)。這類程式
  的真實文字控制項會忽略寫到「最上層視窗」的模式(先前寫入「看似成功」但打字
  仍是錯的模式);現在改寫到「被焦點的控制項」的輸入 context,才會真正生效。
  此功能需**以系統管理員執行** —— 該 context 只有高完整性行程搆得到(與受保護
  遊戲需要提權同理)。未提權時行為與 v1.3.1 相同:傳統與 Chromium 程式照常,
  新版記事本這類則維持原樣(不卡頓、不改變)。

## v1.3.1

Cross-window input-mode persistence is more reliable:

- Fixed: touching the taskbar or desktop, or clicking through it to the tray, no
  longer snaps the IME back to its native (e.g. Chinese) mode -- the shell is no
  longer mistaken for the app you switched to.
- Fixed: File Explorer windows are kept in sync like any other app (they were
  wrongly grouped with the taskbar/desktop and left alone).
- The mode you last chose now holds through the moment right after a switch, when
  Windows re-applies the layout's default a beat late; and a brief misread of the
  input mode no longer flips what is carried.

Known limitation: apps that drive the Text Services Framework themselves --
Chromium browsers (Chrome, Discord, Electron) and packaged apps such as the
modern Notepad -- cannot have their input mode read or set reliably from outside
the process, so persistence there is best-effort. Classic apps are unaffected.

繁體中文:跨視窗維持輸入模式更可靠了:

- 修正:碰到工作列或桌面、或穿過它去點托盤,不會再把輸入法切回預設(例如中文)模式——殼層不再被誤當成你切換到的程式。
- 修正:檔案總管視窗現在會和其他程式一樣正常維持(先前被和工作列/桌面歸為一類而略過)。
- 你最後選的模式,現在能撐過「剛切換視窗、Windows 慢一拍套用版面預設」的那一刻;輸入模式的短暫誤讀也不會再翻掉維持中的模式。

已知限制:自行驅動 TSF(文字服務框架)的程式——Chromium 系瀏覽器(Chrome、Discord、Electron)與封裝版程式(如新版記事本)——無法從行程外可靠讀寫其輸入模式,那些情況為 best-effort;傳統程式不受影響。

## v1.3.0

App language bindings gained ordering and a default:

- Each binding can be set to **apply once** when you switch to the window,
  instead of being continuously enforced, so a manual change afterwards sticks.
- **Drag** bindings in the list to reorder them; the first one that matches wins,
  so you decide precedence.
- A new **default** (used when no binding matches) — off by default, with its own
  language and the same once / continuous choice.

繁體中文:程式綁定輸入語言新增排序與預設:

- 每條綁定可設為**切換到該視窗時只套用一次**,而非持續強制,之後你手動改就會保留。
- 在清單中**拖曳**綁定即可調整順序;由上到下第一個符合的優先,先後順序由你決定。
- 新增**預設**(所有綁定都不符合時套用)——預設不啟用,有自己的語言與同樣的一次性／持續選項。

## v1.2.0

The app interface is now available in Simplified Chinese, Japanese and Korean, on
top of English and Traditional Chinese. It follows your Windows display language
by default (Traditional for TW/HK/MO, Simplified elsewhere), and you can also
pick a language yourself from the new tray **Language** submenu — "Automatic"
restores following Windows. The Simplified Chinese, Japanese and Korean text is
machine-translated for now and may be refined by native speakers later.

繁體中文:App 介面新增簡體中文、日文、韓文(原本已有英文與繁體中文)。預設依 Windows 顯示語言自動選擇(繁體給台/港/澳,其餘給簡體),也可以從新的托盤「顯示語言」子選單自行指定(選「自動」即回到依 Windows)。簡中、日文、韓文目前為機器翻譯,日後可能由母語者校正。

## v1.1.1

The tray right-click menu now shows the app name and version at the top, so you
can tell which build is running at a glance.

繁體中文:托盤右鍵選單頂端現在會顯示 App 名稱與版本號,一眼就能看出目前執行的是哪個版本。

## v1.1.0

Fixed: the Microsoft Store build could crash when toggling "Start at logon".
That build's autostart is a Windows StartupTask; driving it from the app faulted
in the COM runtime, so the tray item now opens Windows Settings > Startup apps to
turn it on or off (the menu still shows the current state). Also renamed the file
description shown in Task Manager's Startup tab to "IME Mode Persistence".

繁體中文:修正 Microsoft Store 版切換「開機時啟動」時可能 crash 的問題。市集版的開機啟動是 Windows 的 StartupTask,由 App 直接切換會在 COM 元件崩潰;托盤選項現在改為開啟「Windows 設定 → 啟動應用程式」讓你開關(選單仍顯示目前狀態)。另把工作管理員「啟動」分頁顯示的檔案描述改為「IME Mode Persistence」。

## v1.0.6

Overview of everything ImeModePersistence does — it keeps your IME conversion
mode (Chinese ↔ alphanumeric) consistent as you move between windows, so you
don't land in the wrong mode after switching apps.

- Per-app input-language binding: pin a specific app (by process) to a keyboard
  input language — e.g. force a game to English while your editor stays Chinese.
- A caret input indicator shows the current input state right where you type.
- Prompts for elevation when a target needs administrator rights.
- Optional diagnostic log for troubleshooting.

Note: the Microsoft Store (MSIX) build can't elevate, so it can't control apps
run as administrator or anti-cheat games — for those use the desktop build
(installer / Scoop / winget) run as administrator. "Start at logon" is available
in the Store build.

繁體中文:ImeModePersistence 功能總覽——讓輸入法模式(中文 ↔ 英數)在視窗間切換時保持一致,不會換 app 後停在錯的模式。

- 依 App 綁定輸入語言:可依程式把特定 app 固定成某個鍵盤輸入語言——例如把遊戲強制成英文,編輯器維持中文。
- 游標輸入指示器:在你打字的位置顯示目前輸入狀態。
- 需要管理員權限時會提示提權。
- 可選的診斷記錄,方便排查問題。

注意:Microsoft Store(MSIX)版無法提權,碰不到以系統管理員執行的程式或反作弊遊戲——那類請用桌面版(安裝檔／Scoop／winget)並以系統管理員執行。市集版支援「開機時啟動」。

## v1.0.5

Overview of everything ImeModePersistence does — it keeps your IME conversion
mode (Chinese ↔ alphanumeric) consistent as you move between windows, so you
don't land in the wrong mode after switching apps.

- Per-app input-language binding: pin a specific app (by process) to a keyboard
  input language — e.g. force a game to English while your editor stays Chinese.
- A caret input indicator shows the current input state right where you type.
- Prompts for elevation when a target needs administrator rights.
- Optional diagnostic log for troubleshooting.

Note: the Microsoft Store (MSIX) build can't elevate, so it can't control apps
run as administrator or anti-cheat games — for those use the desktop build
(installer / Scoop / winget) run as administrator. "Start at logon" is available
in the Store build.

繁體中文:ImeModePersistence 功能總覽——讓輸入法模式(中文 ↔ 英數)在視窗間切換時保持一致,不會換 app 後停在錯的模式。

- 依 App 綁定輸入語言:可依程式把特定 app 固定成某個鍵盤輸入語言——例如把遊戲強制成英文,編輯器維持中文。
- 游標輸入指示器:在你打字的位置顯示目前輸入狀態。
- 需要管理員權限時會提示提權。
- 可選的診斷記錄,方便排查問題。

注意:Microsoft Store(MSIX)版無法提權,碰不到以系統管理員執行的程式或反作弊遊戲——那類請用桌面版(安裝檔／Scoop／winget)並以系統管理員執行。市集版支援「開機時啟動」。

## v1.0.4

Overview of everything ImeModePersistence does — it keeps your IME conversion
mode (Chinese ↔ alphanumeric) consistent as you move between windows, so you
don't land in the wrong mode after switching apps.

- Per-app input-language binding: pin a specific app (by process) to a keyboard
  input language — e.g. force a game to English while your editor stays Chinese.
- A caret input indicator shows the current input state right where you type.
- Prompts for elevation when a target needs administrator rights.
- Optional diagnostic log for troubleshooting.

Note: the Microsoft Store (MSIX) build can't elevate, so it can't control apps
run as administrator or anti-cheat games — for those use the desktop build
(installer / Scoop / winget) run as administrator. "Start at logon" is available
in the Store build.

繁體中文:ImeModePersistence 功能總覽——讓輸入法模式(中文 ↔ 英數)在視窗間切換時保持一致,不會換 app 後停在錯的模式。

- 依 App 綁定輸入語言:可依程式把特定 app 固定成某個鍵盤輸入語言——例如把遊戲強制成英文,編輯器維持中文。
- 游標輸入指示器:在你打字的位置顯示目前輸入狀態。
- 需要管理員權限時會提示提權。
- 可選的診斷記錄,方便排查問題。

注意:Microsoft Store(MSIX)版無法提權,碰不到以系統管理員執行的程式或反作弊遊戲——那類請用桌面版(安裝檔／Scoop／winget)並以系統管理員執行。市集版支援「開機時啟動」。

## v1.0.3

Fixed: the "Open diagnostic log" tray item vanished after Restart as
administrator. The log is now shared for write, so the elevated instance can open
it during the handoff instead of running the whole session with no log.

繁體中文：修正「以系統管理員身分重新啟動」後,托盤的「開啟診斷記錄」選項會消失。現在記錄檔允許共享寫入,提權後的實例在交接時能正常開啟。

## v1.0.2

Microsoft Store build — "Start at logon" now works. The tray toggle drives the
package's StartupTask directly (Windows shows a one-time consent the first time);
if you'd turned it off in Windows Settings, the toggle opens Settings > Apps >
Startup.

繁體中文：Microsoft Store 版「開機時啟動」可以用了。托盤開關直接驅動封裝的 StartupTask(第一次會跳一次 Windows 同意)。

## v1.0.1

Microsoft Store build: when a bound layout can't be applied to a protected /
elevated target, it now points you at the desktop version (which can reach such
targets) instead of silently doing nothing.

繁體中文：Store 版遇到無法套用到受保護/提權目標時,會引導你改用桌面版,而不是默默失效。
