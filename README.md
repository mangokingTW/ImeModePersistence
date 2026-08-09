# ImeModePersistence

[![Windows build](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml/badge.svg)](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml)

**繁體中文** · [English](#english)

Windows 小工具，控制輸入法在程式之間的行為：切換視窗時延續你最後選擇的**輸入模式**（中文／英數），並可把指定程式**綁定到固定的輸入語言** —— 連讀不到執行檔的程式也行，例如有防作弊的全螢幕遊戲。

操作教學在 **[Wiki](https://github.com/mangokingTW/ImeModePersistence/wiki)**，設計取捨與被否決的做法在 **[docs/design.md](docs/design.md)**。

## 這是什麼

在 A 視窗用中文輸入 → 切到 B 視窗，中文模式被還原。你在 B 按 Shift 改成英數 → 切到 C 視窗，還原的是英數。全域目標跟著你最近一次的手動切換走，可以在托盤選單關閉。

已在實機上與**微軟注音**確認可用。

## 為什麼會需要它

Windows 把輸入法狀態綁在**每個執行緒**上。切到另一個視窗時，中／英轉換模式會回到該輸入法的預設值 —— 對中文鍵盤來說就是**中文**。所以你剛按 Shift 切成英數，換個視窗又打出中文。

「設定 → 時間與語言 → 輸入 → 進階鍵盤設定」裡的**「允許我為每個應用程式視窗使用不同的輸入法」解決不了這件事**。那個設定管的是「哪一個輸入法在作用」，不管「該輸入法處於中文還是英數」—— 關掉它之後，切換視窗照樣切回中文。

**沒有任何 Windows 設定能處理轉換模式** —— 這是這個工具的**第一個**目的：讓你選的中／英模式跟著你走。

**第二個**目的是把特定程式固定在某個輸入語言，包含連設定都碰不到的那類：**使用 raw input 的全螢幕遊戲**直接讀取鍵盤裝置，完全不參與輸入法的狀態管理，只能從外部處理。[Helldivers 2](https://github.com/mangokingTW/ImeModePersistence/wiki/Helldivers-2) 就是這種。

## 安裝

到 [Releases](https://github.com/mangokingTW/ImeModePersistence/releases) 下載：

| 檔案 | 說明 |
|---|---|
| `...-setup-admin.exe` | 需要管理員權限，裝在 Program Files。**有防作弊的遊戲需要這個版本** |
| `...-setup-user.exe` | 不需要管理員權限，裝在使用者目錄。無法控制提權的程式 |
| `...-x64.zip` / `-x86.zip` | 免安裝，解壓即用 |

> **防毒軟體可能刪除安裝檔。** 未簽章的安裝檔常被啟發式誤判 —— 遇到時改用免安裝的 zip，功能完全相同。

安裝時有兩個勾選項。「開機時自動啟動」：管理員版建立**登入時以最高權限執行**的排程工作（不彈 UAC），免管理員版寫登錄項目 —— 登錄項目無法啟動提權的程式。手動啟動要提權時用托盤的**以管理員身分重新啟動**。「綁定 Helldivers 2 為英文輸入」：預設不勾，勾了會在第一次啟動時自動加上 `class:stingray_window` → 英文的規則（若你已有自己的規則就不覆蓋，日後刪掉也不會復活）。

**更新**：執行新版安裝檔，會就地升級並自動關閉重啟執行中的副本。**卸除**：設定 → 應用程式。安裝目錄、登錄項目與排程工作都會清掉。

## 使用

常駐在通知區域。右鍵選單：**跨程式維持輸入模式**（可關閉）、**開機時自動啟動**、**程式綁定輸入語言…**、**以管理員身分重新啟動**（未提權時才出現）、**結束**。

**把滑鼠停在圖示上**會顯示當前程式、綁定語言、實際語言與上次切換結果 —— 停留不會改變前景視窗，點下去會。左鍵雙擊顯示完整狀態。每個登入工作階段只有一份執行中。

切換沒有如預期作用時，右鍵選單的**開啟診斷記錄**會叫出 `%LocalAppData%\ImeModePersistence\log.txt`，裡面記錄每次上下文切換、規則比對結果、以及切換用了哪個機制與是否成功。回報問題時附上它。

### 程式綁定輸入語言

把程式綁定到一個輸入語言，例如終端機綁英文、Word 綁中文。

- **瀏覽** 選執行檔 → 規則是那個**完整路徑**，同名的兩個執行檔可以分開設定
- 只填**檔名**（`notepad.exe`）→ 不管裝在哪都套用
- **用視窗類別** 填 `class:類別名` → 有防作弊的遊戲讀不到路徑（連管理員也讀不到），視窗類別是唯一不碰該程式就能識別它的方式
- **用剛才的程式** 自動填入你開這個視窗之前用的程式

比對順序：完整路徑 → 檔名 → 視窗類別。綁定的是**語言**，同一語言裝多個輸入法（注音與倉頡都是 zh-TW）時用第一個已安裝的。

**已確認可用於有防作弊的全螢幕遊戲** —— Helldivers 2 用 `class:stingray_window` 即可，詳見 [Wiki](https://github.com/mangokingTW/ImeModePersistence/wiki/Helldivers-2)。

## 限制

- 寫入是 best-effort 並讀回驗證。IME 仍在啟動中時可能丟棄變更，重試四次後就接受目標視窗的狀態
- 讀取或修改**提權程式**的視窗需要同等權限，所以控制有防作弊的遊戲必須提權執行
- 介面依 Windows 顯示語言選繁體中文或英文；沒有簡體版本，安裝程式仍是英文
- 深色模式只作用於標題列
- 不注入、不掛勾、不模擬按鍵。切換是向視窗投遞 Windows 標準的「切換輸入語言」通知（視窗可自行忽略），行不通再請 TSF 以公開 API 切換；仍無效就放手，愈輸愈少問

## 開發

```powershell
cmake -S . -B build -A x64 && cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
iscc /DAppVersion=0.0.0 installer\ImeModePersistence.iss
iscc /DAppVersion=0.0.0 /DUserInstall installer\ImeModePersistence.iss
```

安裝檔需要 [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3+，圖示重新產生需要 [ImageMagick](https://imagemagick.org) 7（`./tools/make_icon.sh`）。

`CMakeLists.txt` 的 `project(... VERSION ...)` 是版本的唯一來源，會寫進 `VERSIONINFO`。**每個 PR 都必須 bump**，CI 會擋。發佈時推一個與它相符的 `vMAJOR.MINOR.PATCH` tag。

## 授權

[MIT](LICENSE)

## Roadmap

- [x] TSF 感知的轉換模式配接、還原後重試與驗證、區分使用者操作與系統事件
- [x] 微軟注音實機驗證
- [x] 開機自動啟動、設定介面、Windows CI、安裝檔與發佈流程
- [x] 程式綁定輸入語言，含視窗類別綁定
- [ ] 同語言多輸入法的細分（注音 vs 倉頡）

---

# English

Windows utility that controls how input methods behave across programs: it carries the **input mode you last chose** (native or alphanumeric) to the next window, and can **bind a program to a fixed input language** — including programs whose executable cannot be read, such as anti-cheat protected fullscreen games.

Step-by-step guides are in the **[wiki](https://github.com/mangokingTW/ImeModePersistence/wiki/Home-English)**; design trade-offs and rejected approaches in **[docs/design.md](docs/design.md)**.

## What it does

Type Chinese in window A → switch to B, Chinese is restored. Press Shift in B to go alphanumeric → switch to C, alphanumeric is restored. The global target follows your most recent deliberate change, and can be turned off from the tray menu.

Confirmed working with **Microsoft Bopomofo** on real hardware.

## Why you would want it

Windows keeps IME state **per thread**. Move to another window and the conversion mode reverts to the IME's default — for a Chinese keyboard, that means **Chinese**. So you press Shift for alphanumeric, switch window, and you are typing Chinese again.

**Settings → Time & language → Typing → Advanced keyboard settings → _Let me use a different input method for each app window_ does not fix this.** That setting governs *which* input method is active, not whether that method is in native or alphanumeric mode — turn it off and switching windows still reverts to Chinese.

**No Windows setting covers the conversion mode.** That is this utility's **first** purpose: carrying the native/alphanumeric mode you chose to the next window.

Its **second** purpose is pinning a specific program to an input language, including the kind no setting can reach: a **fullscreen game reading raw input** takes the keyboard directly and does not participate in IME state management at all, so it can only be handled from outside. [Helldivers 2](https://github.com/mangokingTW/ImeModePersistence/wiki/Helldivers-2-English) is one.

## Install

From [Releases](https://github.com/mangokingTW/ImeModePersistence/releases):

| File | What it is |
|---|---|
| `...-setup-admin.exe` | Needs administrator rights, installs to Program Files. **Required for anti-cheat protected games** |
| `...-setup-user.exe` | No administrator rights, installs into your user directory. Cannot control elevated programs |
| `...-x64.zip` / `-x86.zip` | Portable; unzip and run |

> **Antivirus may delete the installer.** Unsigned installers are frequently caught by heuristics — use the portable archive, which is functionally identical.

Setup has two options. *Start automatically at logon*: the administrator installer registers a scheduled task running at logon with highest privileges (no UAC prompt); the user installer writes a registry entry, since a registry entry cannot start an elevated program. To elevate a manual launch, use **Restart as administrator** in the tray menu. *Bind Helldivers 2 to English input* (off by default): on first run it adds a `class:stingray_window` → English rule for you — it never overwrites a rule you already have, and does not come back if you later remove it.

**Updating**: run the newer installer; it upgrades in place, closing and restarting a running copy. **Uninstalling**: Settings → Apps. The install directory, registry entries and scheduled task all go.

## Using it

It lives in the notification area. Right-click for **Keep mode across windows** (which can be turned off), **Start automatically at logon**, **App language bindings...**, **Restart as administrator** (shown only when not elevated) and **Exit**.

**Hover** the icon to see the current program, its bound language, the language actually in use, and whether the last switch worked — hovering does not change the foreground window, clicking does. Double-click for full status. One instance runs per logon session.

When a switch does not do what you expect, **Open diagnostic log** in the tray menu opens `%LocalAppData%\ImeModePersistence\log.txt`, which records every context switch, whether a rule matched, and which mechanism was used with its outcome. Attach it when reporting a problem.

### App language bindings

Bind a program to an input language — a terminal to English, Word to Chinese.

- **Browse** for an executable → the rule is that **full path**, so two executables sharing a name can be configured separately
- A bare **file name** (`notepad.exe`) → applies wherever the program is installed
- **Use window class** → `class:<name>`, the only way to identify an anti-cheat protected game, whose path cannot be read even by an administrator
- **Use last app** → fills in the program you were in before opening the dialog

Lookup order: full path, file name, window class. A binding pins a **language**; where one language has several IMEs installed (Bopomofo and Cangjie are both zh-TW) the first is used.

**Confirmed working for anti-cheat protected fullscreen games** — Helldivers 2 needs `class:stingray_window`; see the [wiki](https://github.com/mangokingTW/ImeModePersistence/wiki/Helldivers-2-English).

## Limitations

- Writes are best-effort and verified by reading back. An IME still activating can discard one; after four attempts the utility accepts whatever the target settled on
- Reading or changing the windows of an **elevated** program requires equal privileges, so controlling an anti-cheat protected game means running elevated
- The interface follows Windows' display language, Traditional Chinese or English. No Simplified translation; the installer is English only
- Dark mode applies to the title bar only
- Nothing is injected, hooked or synthesised. Switching posts the window Windows' standard input-language-change notification (which it is free to ignore), then falls back to TSF's public API; where neither takes, the utility backs off, asking less the more it loses

## Development

```powershell
cmake -S . -B build -A x64 && cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake -S . -B build-x86 -A Win32 && cmake --build build-x86 --config Release
iscc /DAppVersion=0.0.0 installer\ImeModePersistence.iss
iscc /DAppVersion=0.0.0 /DUserInstall installer\ImeModePersistence.iss
```

The installers need [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 or newer; regenerating the icon needs [ImageMagick](https://imagemagick.org) 7 (`./tools/make_icon.sh`).

`project(... VERSION ...)` in `CMakeLists.txt` is the single source of truth and is stamped into `VERSIONINFO`. **Every PR must bump it** — CI fails one that does not. To release, push a `vMAJOR.MINOR.PATCH` tag matching it.

## License

[MIT](LICENSE)
