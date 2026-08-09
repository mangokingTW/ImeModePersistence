# ImeModePersistence

[![Windows build](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml/badge.svg)](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml)

**繁體中文** · [English](#english)

Windows 小工具，切換視窗時保留你**最後一次選擇的輸入模式**。

## 這是什麼

在 A 視窗用中文輸入 → 切到 B 視窗，中文模式被還原。你在 B 按 Shift 改成英數 → 切到 C 視窗，還原的是英數。全域的目標模式跟著你最近一次的手動切換走。

已在實機上與**微軟注音**確認可用。這不是「強制中文」的工具 —— 但可以把個別程式綁定到固定的輸入語言。

## 安裝

到 [Releases](https://github.com/mangokingTW/ImeModePersistence/releases) 下載 **`...-setup.exe`**（裝在 `%LocalAppData%\Programs`，不需管理員權限）或 **`...-x64.zip`** / **`-x86.zip`**（解壓即用）。

兩者都**未經簽章**，首次執行會有 SmartScreen 警告，選「更多資訊 → 仍要執行」，或先用 `SHA256SUMS.txt` 核對。

- **更新** — 執行新版安裝檔即可，會就地升級。執行中的程式會自動關閉並重啟，不需手動處理。
- **卸除** — 設定 → 應用程式，或開始功能表的 Uninstall 捷徑。安裝目錄與登錄項目都會清掉。

## 使用

常駐在通知區域，右鍵選單三項：**開機時自動啟動**、**程式綁定輸入語言…**、**結束**。把滑鼠停在圖示上會顯示綁定與目前的輸入語言；左鍵雙擊顯示完整狀態。每個登入工作階段只會有一份執行中。

### 程式綁定輸入語言

把程式綁定到一個輸入語言，例如終端機綁英文、Word 綁中文。綁定後切到該程式就會自動切換。

- **瀏覽** 選執行檔，規則就是那個**完整路徑**，所以兩個同名的執行檔可以分開設定
- 也可以只填**檔名**（`notepad.exe`），這樣不管程式裝在哪都套用。查詢時先比對完整路徑，找不到才比對檔名
- **用剛才的程式** 會填入你開這個視窗之前那個程式，不必自己找

綁定的是**語言**（中文／英文／日文…）。同一語言裝了多個輸入法（注音與倉頡都是 zh-TW）時，會用第一個已安裝的。

## 限制

- 寫入都是 best-effort 並會讀回驗證。IME 仍在啟動中時可能丟棄變更，重試四次（約 930 ms）後就接受目標視窗的狀態
- UIPI 會阻止修改**更高完整性等級**程式的輸入法狀態，例如以管理員身分開啟的終端機
- 介面依 Windows 顯示語言自動選繁體中文或英文；沒有簡體版本，安裝程式精靈仍是英文
- 深色模式只作用於**標題列**，控制項仍是淺色

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

---

# English

Windows utility that keeps the **last input mode you chose** when you switch windows.

## What it does

Type Chinese in window A → switch to B, Chinese is restored. Press Shift in B to go alphanumeric → switch to C, alphanumeric is restored. The global desired mode follows your most recent deliberate change.

Confirmed working with **Microsoft Bopomofo** on real hardware. This is not a "force Chinese" tool — but individual applications can be bound to an input language.

## Install

From [Releases](https://github.com/mangokingTW/ImeModePersistence/releases), take **`...-setup.exe`** (per-user into `%LocalAppData%\Programs`, no administrator rights) or **`...-x64.zip`** / **`-x86.zip`** (unzip and run).

Both are **unsigned**, so SmartScreen warns on first run — choose *More info → Run anyway*, or verify against `SHA256SUMS.txt`.

- **Updating** — run the newer installer; it upgrades in place. A running copy is closed and restarted for you.
- **Uninstalling** — Settings → Apps, or the Start menu shortcut. The install directory and registry entries both go.

## Using it

It lives in the notification area. Right-click for **Start with Windows**, **App language bindings...** and **Exit**; hover for the bound and current input language; double-click for full status. One instance runs per logon session.

### App language bindings

Bind an application to an input language — a terminal to English, Word to Chinese. Activating a bound application switches to its language.

- **Browse** picks an executable and the rule is that **full path**, so two executables sharing a name can be configured separately
- A bare **file name** (`notepad.exe`) also works and applies wherever the application is installed. Lookup tries the path first, then the name
- **Use last app** fills in the application you were in before opening the dialog

What gets bound is a **language** (Chinese / English / Japanese...). Where one language has several IMEs installed — Bopomofo and Cangjie are both zh-TW — the first is used.

## Limitations

- Writes are best-effort and verified by reading back. An IME still activating can discard one; after four attempts (~930 ms) the utility accepts whatever the target settled on
- UIPI prevents changing the IME state of a **higher integrity level** process, such as an elevated terminal
- The interface follows Windows' display language, choosing Traditional Chinese or English. No Simplified translation; the installer wizard is still English
- Dark mode applies to the **title bar** only; controls stay light

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
