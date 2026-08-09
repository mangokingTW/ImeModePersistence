# ImeModePersistence

[![Windows build](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml/badge.svg)](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml)

**繁體中文** · [English](#english)

Windows 小工具，控制輸入法在程式之間的行為：切換視窗時延續你最後選擇的**輸入模式**（中文／英數），並可把指定程式**固定在某個輸入語言**（中文／英文／日文…）—— 連讀不到執行檔的程式也能綁定，例如有反作弊的全螢幕遊戲。

## 這是什麼

在 A 視窗用中文輸入 → 切到 B 視窗，中文模式被還原。你在 B 按 Shift 改成英數 → 切到 C 視窗，還原的是英數。全域的目標模式跟著你最近一次的手動切換走。

已在實機上與**微軟注音**確認可用。這不是「強制中文」的工具 —— 但可以把個別程式綁定到固定的輸入語言。

## 安裝

到 [Releases](https://github.com/mangokingTW/ImeModePersistence/releases) 下載 **`...-setup.exe`** 或 **`...-x64.zip`** / **`-x86.zip`**（解壓即用）。

安裝時可以選擇**為所有使用者安裝**（提權）或**只為我安裝**（完全不提權）。建議前者：Windows 不讓權限較低的程式讀取權限較高的程式的視窗，而有反作弊的遊戲都是提權執行 —— 不提權就完全看不到它們。

**為所有使用者安裝之後，每一次啟動都會是管理員身分** —— 不只是登入時自動啟動的那一次。勾選「開機時自動啟動」會建立**登入時以最高權限執行**的排程工作（那一次不彈 UAC），手動啟動則會彈一次。

一般安裝（只為我）不會提權，開機啟動改寫一般權限的登錄項目。之後想臨時提權，用托盤選單的**以管理員身分重新啟動**。

要確認目前是哪一種：把滑鼠停在托盤圖示上，未提權時最後一行會寫「一般權限 － 看不到提權的程式」。

兩者都**未經簽章**，首次執行會有 SmartScreen 警告，選「更多資訊 → 仍要執行」，或先用 `SHA256SUMS.txt` 核對。

- **更新** — 執行新版安裝檔即可，會就地升級。執行中的程式會自動關閉並重啟；提權的安裝檔才有能力關閉提權的常駐程式，所以不需手動處理。
- **卸除** — 設定 → 應用程式，或開始功能表的 Uninstall 捷徑。安裝目錄、登錄項目與排程工作都會清掉。

## 使用

常駐在通知區域，右鍵選單四項：**跨程式維持輸入模式**（可關閉）、**開機時自動啟動**、**程式綁定輸入語言…**、**結束**。把滑鼠停在圖示上會顯示綁定與目前的輸入語言；左鍵雙擊顯示完整狀態。每個登入工作階段只會有一份執行中。

#### 關閉全域延續

**跨程式維持輸入模式**預設開啟，就是這個工具原本的行為。關掉之後只有下面的程式綁定會作用 —— 適合只想要「某些程式固定用某個語言」而不要全域跟隨的人。設定記在 `HKCU\Software\ImeModePersistence`。

## 程式綁定輸入語言

把程式綁定到一個輸入語言，例如終端機綁英文、Word 綁中文。綁定後切到該程式就會自動切換。

- **瀏覽** 選執行檔，規則就是那個**完整路徑**，所以兩個同名的執行檔可以分開設定
- 也可以只填**檔名**（`notepad.exe`），這樣不管程式裝在哪都套用。查詢時先比對完整路徑，找不到才比對檔名
- **用剛才的程式** 會填入你開這個視窗之前那個程式，不必自己找
- **用視窗類別** 填入 `class:視窗類別名`（Win32 視窗類別，不是程式名或標題）—— 有反作弊的遊戲讀不到執行檔路徑，連管理員也讀不到，視窗類別是唯一不需要碰該程式就能識別它的方式。不必自己查：這個按鈕會用你開視窗之前那個程式的類別填入，托盤提示在讀不到路徑時也會直接顯示 `class:實際類別`

比對順序由精確到寬鬆：完整路徑 → 檔名 → 視窗類別。

**已確認可用於有反作弊的全螢幕遊戲。** 例如 Helldivers 2 的視窗類別是 `stingray_window`（Autodesk Stingray 引擎），用 `class:stingray_window` 綁定即可生效 —— 它的執行檔路徑讀不到，但視窗類別讀得到。

綁定的是**語言**（中文／英文／日文…）。同一語言裝了多個輸入法（注音與倉頡都是 zh-TW）時，會用第一個已安裝的。

## 限制

- 寫入都是 best-effort 並會讀回驗證。IME 仍在啟動中時可能丟棄變更，重試四次（約 930 ms）後就接受目標視窗的狀態
- UIPI 會阻止修改**更高完整性等級**程式的輸入法狀態，例如以管理員身分開啟的終端機
- 介面依 Windows 顯示語言自動選繁體中文或英文；沒有簡體版本，安裝程式精靈仍是英文
- 深色模式只作用於**標題列**，控制項仍是淺色
- 提權執行是預設，因為讀取提權程式的視窗需要同等權限。代價是托盤的「開機時自動啟動（一般權限）」對提權的副本沒有意義 —— 提權的自動啟動由安裝檔建立的排程工作負責
- 不會為了切換輸入語言而注入或附加到其他程式。有反作弊的遊戲用視窗類別綁定即可，切換走 TSF 的工作階段層級 API，完全不接觸該程式；若仍無法生效，本工具會安靜放棄而不是加大力道

## 開發

```powershell
cmake -S . -B build -A x64 && cmake --build build --config Release
```

安裝檔需要兩種架構都建好，再用 [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3+：

```powershell
cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
iscc /DAppVersion=0.4.4 installer\ImeModePersistence.iss
```

圖示重新產生需要 [ImageMagick](https://imagemagick.org) 7：`./tools/make_icon.sh`

`CMakeLists.txt` 的 `project(... VERSION ...)` 是版本的唯一來源，會寫進 `VERSIONINFO`。**每個 PR 都必須 bump**，CI 會擋。發佈時推一個與它相符的 `vMAJOR.MINOR.PATCH` tag，workflow 會建置雙架構、產生安裝檔並發佈；版本不符會被拒絕。

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
- [x] 程式綁定輸入語言
- [ ] 同語言多輸入法的細分（注音 vs 倉頡）
- [x] 視窗類別綁定（供讀不到執行檔的程式使用）

---

# English

Windows utility for controlling how input methods behave across programs: it carries the **input mode you last chose** (native or alphanumeric) to the next window, and can pin a program to a fixed **input language** (Chinese, English, Japanese...) — including programs whose executable cannot be read, such as anti-cheat protected fullscreen games.

## What it does

Type Chinese in window A → switch to B, Chinese is restored. Press Shift in B to go alphanumeric → switch to C, alphanumeric is restored. The global desired mode follows your most recent deliberate change.

Confirmed working with **Microsoft Bopomofo** on real hardware. This is not a "force Chinese" tool — but individual applications can be bound to an input language.

## Install

From [Releases](https://github.com/mangokingTW/ImeModePersistence/releases), take **`...-setup.exe`** or **`...-x64.zip`** / **`-x86.zip`** (unzip and run).

Setup asks whether to install **for all users** (elevated) or **for me only** (no elevation anywhere). The first is recommended: Windows does not let a lower-privileged program read a higher-privileged one's windows, and anti-cheat protected games are elevated — without it they cannot be seen at all.

**After installing for all users, every launch is elevated** — not only the one the logon task starts. With autostart ticked, an elevated install registers a scheduled task running **at logon with highest privileges** (no UAC prompt for that one); launching by hand prompts once.

An install for the current user only never elevates, and writes a normal-privilege registry entry for autostart instead. **Restart as administrator** in the tray menu elevates on demand.

To tell which you have, hover the tray icon: when unelevated the last line reads *Normal privileges - elevated programs are invisible*.

Both are **unsigned**, so SmartScreen warns on first run — choose *More info → Run anyway*, or verify against `SHA256SUMS.txt`.

- **Updating** — run the newer installer; it upgrades in place. A running copy is closed and restarted for you.
- **Uninstalling** — Settings → Apps, or the Start menu shortcut. The install directory, registry entries and the scheduled task all go.

## Using it

It lives in the notification area. Right-click for **Keep mode across windows** (which can be turned off), **Start with Windows (normal privileges)**, **Restart as administrator** (shown only when not elevated), **App language bindings...** and **Exit**; hover for the bound and current input language; double-click for full status. One instance runs per logon session.

#### Turning off global persistence

**Keep mode across windows** is on by default and is what the utility is for. Turning it off leaves only the bindings below active — for someone who wants specific applications pinned to a language without the global carry-over. Stored in `HKCU\Software\ImeModePersistence`.

## App language bindings

Bind an application to an input language — a terminal to English, Word to Chinese. Activating a bound application switches to its language.

- **Browse** picks an executable and the rule is that **full path**, so two executables sharing a name can be configured separately
- A bare **file name** (`notepad.exe`) also works and applies wherever the application is installed. Lookup tries the path first, then the name
- **Use last app** fills in the application you were in before opening the dialog
- **Use window class** fills in `class:<name>` (the Win32 window class, not the program or title) — anti-cheat protected games refuse to have their executable path read, even by an administrator, and a window class is the only way to identify one without touching the process. You do not have to look it up: the button fills it from the application you were in, and the tooltip shows `class:<name>` whenever the path cannot be read

Lookup goes from most specific to least: full path, then file name, then window class.

**Confirmed working for anti-cheat protected fullscreen games.** Helldivers 2's window class is `stingray_window` (the Autodesk Stingray engine), so `class:stingray_window` binds it — its executable path cannot be read, but its window class can.

What gets bound is a **language** (Chinese / English / Japanese...). Where one language has several IMEs installed — Bopomofo and Cangjie are both zh-TW — the first is used.

## Limitations

- Writes are best-effort and verified by reading back. An IME still activating can discard one; after four attempts (~930 ms) the utility accepts whatever the target settled on
- UIPI prevents changing the IME state of a **higher integrity level** process, such as an elevated terminal
- The interface follows Windows' display language, choosing Traditional Chinese or English. No Simplified translation; the installer wizard is still English
- Dark mode applies to the **title bar** only; controls stay light
- Running elevated is the default, because reading an elevated program's windows requires equal privileges. The cost is that the tray's **Start with Windows (normal privileges)** is meaningless for an elevated copy — elevated autostart is the scheduled task registered by setup
- Nothing is injected into or attached to another process to switch its language. An anti-cheat protected game is bound by window class and switched through TSF's session-level API, which touches nothing belonging to it; where that still fails the utility gives up quietly rather than trying harder

## Development

```powershell
cmake -S . -B build -A x64 && cmake --build build --config Release
```

The installer needs both architectures built, then [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 or newer:

```powershell
cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
iscc /DAppVersion=0.4.4 installer\ImeModePersistence.iss
```

Regenerating the icon needs [ImageMagick](https://imagemagick.org) 7: `./tools/make_icon.sh`

`project(... VERSION ...)` in `CMakeLists.txt` is the single source of truth and is stamped into `VERSIONINFO`. **Every PR must bump it** — CI fails one that does not. To release, push a `vMAJOR.MINOR.PATCH` tag matching it; the workflow builds both architectures, compiles the installer and publishes. A tag that disagrees is refused.

Design trade-offs and rejected approaches are in **[docs/design.md](docs/design.md)**.

## License

[MIT](LICENSE)
