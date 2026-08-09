# ImeModePersistence

[![Windows build](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml/badge.svg)](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml)

**繁體中文** · [English](#english)

Windows 小工具，切換視窗時保留你**最後一次選擇的輸入模式**。

## 這是什麼

在 A 視窗用中文輸入 → 切到 B 視窗，中文模式被還原。你在 B 視窗按 Shift 改成英數 → 切到 C 視窗，還原的是英數。全域的目標模式跟著你最近一次的手動切換走。

預設行為**不是**「強制中文」或「強制日文」—— 全域延續才是預設。若你要某個程式永遠用固定的鍵盤配置，可以另外加規則覆寫（見下方**程式規則**）。

已在實機上與**微軟注音**確認可用。

## 安裝

到 [Releases](https://github.com/mangokingTW/ImeModePersistence/releases) 下載：

- **`...-setup.exe`** — 安裝檔。裝在 `%LocalAppData%\Programs`，不需系統管理員權限、不會彈 UAC，可從「設定 > 應用程式」正常卸除。
- **`...-x64.zip`** / **`-x86.zip`** — 免安裝。解壓即用。

兩者都**未經簽章**，首次執行會有 SmartScreen 警告，選「更多資訊 > 仍要執行」，或先用 `SHA256SUMS.txt` 核對。

**更新**：直接執行新版安裝檔，它會就地升級，保留安裝位置與開機啟動設定。執行中的程式會被自動關閉並在安裝後自動重啟，不需要你手動處理。
**卸除**：設定 > 應用程式，或開始功能表的 Uninstall 捷徑，或重跑同版本安裝檔。安裝目錄與開機啟動的登錄值都會清掉。

## 使用

常駐在通知區域：

- **左鍵雙擊** — 顯示目前的目標模式與前景視窗狀態
- **右鍵** — 「Start with Windows」開機自動啟動（寫 `HKCU\...\Run`，不需管理員權限）、結束

每個登入工作階段只會有一份執行中。

## 程式規則

托盤右鍵 → **Application rules...** 可以把指定的程式綁定到固定的鍵盤配置，例如 `wt.exe` 綁美式鍵盤、`word.exe` 綁微軟注音。切到該程式時會自動切換配置。

- 程式以**執行檔名稱**辨識（`notepad.exe`），不是完整路徑 —— 路徑會因為搬移或更新而失效
- **Use last app** 會填入你切過來之前那個程式的檔名，不必自己查
- 規則存在 `HKCU\Software\ImeModePersistence\Rules`

規則綁定的是**語言**（注音 / 美式 / 日文…）。同一個語言裝了多個輸入法時（例如注音與倉頡都是 zh-TW），會用第一個已安裝的，無法細分到特定輸入法。

## 限制

寫入都是 best-effort 並會讀回驗證；IME 仍在啟動中時可能丟棄變更，重試四次（約 930 ms）後就接受目標視窗的狀態，不再強求。

UIPI 會阻止修改**更高完整性等級**程式的輸入法狀態，例如以管理員身分開啟的終端機。需要的話請讓本工具跑在同一等級。

## 開發

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

安裝檔需要兩種架構都先建好，再用 [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 以上：

```powershell
cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
iscc /DAppVersion=0.4.1 installer\ImeModePersistence.iss
```

圖示重新產生需要 [ImageMagick](https://imagemagick.org) 7：`./tools/make_icon.sh`

**版本**：`CMakeLists.txt` 的 `project(... VERSION ...)` 是唯一來源，會寫進 `VERSIONINFO`。**每個 PR 都必須 bump**，CI 會擋沒 bump 的 PR。

**發佈**：推一個與 `CMakeLists.txt` 相符的 `vMAJOR.MINOR.PATCH` tag，workflow 會建置雙架構、產生安裝檔並發佈 release。版本不符會被拒絕。

設計取捨與被否決的做法記在 **[docs/design.md](docs/design.md)**。

## 授權

[MIT](LICENSE)

## Roadmap

- [x] TSF 感知的轉換模式配接（走 IMM32/TSF 互通層）
- [x] 微軟注音實機驗證
- [x] 還原後重試與驗證
- [x] 區分使用者操作與系統事件
- [x] 開機自動啟動
- [x] Windows CI、安裝檔與發佈流程
- [x] 設定介面
- [x] 指定程式綁定鍵盤配置

---

# English

Windows utility that keeps the **last input mode you chose** when you switch windows.

## What it does

Type Chinese in window A → switch to window B, Chinese is restored. Press Shift in B to go alphanumeric → switch to window C, alphanumeric is restored. The global desired mode follows your most recent deliberate change.

By default this is **not** a "force Chinese" or "force Japanese" tool — global persistence is the default. Per-application rules can override it (see **Application rules** below).

Confirmed working with **Microsoft Bopomofo** on real hardware.

## Install

From [Releases](https://github.com/mangokingTW/ImeModePersistence/releases):

- **`...-setup.exe`** — installer. Per-user into `%LocalAppData%\Programs`, so no administrator rights and no UAC prompt, and it uninstalls from **Settings > Apps** like anything else.
- **`...-x64.zip`** / **`-x86.zip`** — portable. Unzip and run.

Both are **unsigned**, so SmartScreen warns on first run — choose *More info > Run anyway*, or verify against `SHA256SUMS.txt` first.

**Updating**: run the newer installer. It upgrades in place, keeping the install directory and the autostart choice. A running copy is closed and restarted for you — Windows cannot replace a running executable, so stopping it is unavoidable, but doing it by hand is not.
**Uninstalling**: Settings > Apps, the Start menu shortcut, or re-running the same version's installer. The install directory and the autostart registry value both go.

## Using it

It lives in the notification area:

- **Double-click** — show the current desired mode and the foreground window's state
- **Right-click** — *Start with Windows* (a per-user `HKCU\...\Run` entry, no admin rights), or exit

One instance runs per logon session.

## Application rules

Right-click the tray icon → **Application rules...** binds an application to a fixed keyboard layout: `wt.exe` to a US keyboard, `word.exe` to Microsoft Bopomofo. Switching to that application switches the layout.

- Applications are identified by **executable file name** (`notepad.exe`), not full path — a path breaks when the application moves or updates
- **Use last app** fills in the application you were in before opening the dialog, so you do not have to go looking for the name
- Rules live in `HKCU\Software\ImeModePersistence\Rules`

A rule binds a **language** (Bopomofo / US / Japanese...). Where one language has several IMEs installed — Bopomofo and Cangjie are both zh-TW — the first installed one is used; a specific IME cannot be singled out.

## Limitations

Writes are best-effort and verified by reading the state back. An IME that is still activating can discard one; after four attempts (~930 ms) the utility accepts whatever the target settled on rather than fighting it.

UIPI prevents changing the IME state of a **higher integrity level** process, such as an elevated terminal. Run the utility at the same level when that matters.

## Development

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The installer needs both architectures built, then [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 or newer:

```powershell
cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
iscc /DAppVersion=0.4.1 installer\ImeModePersistence.iss
```

Regenerating the icon needs [ImageMagick](https://imagemagick.org) 7: `./tools/make_icon.sh`

**Versioning**: `project(... VERSION ...)` in `CMakeLists.txt` is the single source of truth and is stamped into `VERSIONINFO`. **Every PR must bump it** — CI fails a PR that does not.

**Releasing**: push a `vMAJOR.MINOR.PATCH` tag matching `CMakeLists.txt`. The workflow builds both architectures, compiles the installer, and publishes the release. A tag that disagrees is refused.

Design trade-offs and rejected approaches are in **[docs/design.md](docs/design.md)**.

## License

[MIT](LICENSE)
