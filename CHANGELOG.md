# Changelog

Per-version **highlights** live under a `## <tag>` heading (e.g. `## v1.0.4`),
bilingual (English + 繁體中文). At release time the workflow takes that section
and appends a consistent install / verify section, so the published GitHub
release note reads as a finished, formatted note rather than a raw changelog.
Keep each section to what changed; the boilerplate is added automatically. If a
tag has no section here, the workflow falls back to auto-generated notes.

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
