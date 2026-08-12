# Privacy Policy

_Last updated: 2026-08-12_

ImeModePersistence ("the app") is a local Windows utility. **It collects no
personal information, makes no network connections, and sends nothing off your
device.** There is no telemetry, no account, and no sharing with third parties.

## What the app accesses, and why

To do its job the app reads, on your device and in memory only:

- the identity of the foreground application (executable path and/or window
  class) — to apply the input-language binding you configured for it;
- the current keyboard layout and IME conversion mode — to keep your chosen mode
  consistent as you switch windows;
- the text caret position and whether the focused control is an editable text
  field, **only when you turn on the optional caret input indicator** — to draw
  the badge next to the cursor.

This information is used only to provide the features above. It is not sold,
shared, or transmitted.

## What the app stores, locally

- **Settings and per-application bindings** — in the Windows registry under
  `HKEY_CURRENT_USER\Software\ImeModePersistence`.
- **A diagnostic log** — a plain-text file at
  `%LocalAppData%\ImeModePersistence\log.txt` recording context switches and
  whether a binding took effect, to help you troubleshoot. It stays on your
  device, rotates at 1 MB, and can be deleted at any time.

Uninstalling the app and deleting that folder removes all of the above.

## Network

The app makes no network requests of its own.

## Contact

Questions or reports: <https://github.com/mangokingTW/ImeModePersistence/issues>

---

# 隱私權政策

_最後更新：2026-08-12_

ImeModePersistence（下稱「本程式」）是在本機執行的 Windows 工具。**它不蒐集任何個人資訊、不進行任何網路連線，也不會把任何資料傳出你的裝置。** 沒有遙測、沒有帳號、不與第三方分享。

## 本程式會存取什麼、為什麼

為了運作，本程式僅在你的裝置上、於記憶體中讀取：

- 前景程式的識別（執行檔路徑與／或視窗類別）—— 用來套用你為它設定的輸入語言綁定；
- 目前的鍵盤配置與輸入法中／英模式 —— 用來在切換視窗時維持你選擇的模式；
- 文字游標位置與聚焦控制項是否為可編輯欄位，**僅在你開啟選用的「游標輸入指示器」時** —— 用來在游標旁顯示徽章。

上述資訊僅用於提供以上功能，不販售、不分享、不傳輸。

## 本程式在本機儲存什麼

- **設定與各程式綁定** —— 存於 Windows 登錄 `HKEY_CURRENT_USER\Software\ImeModePersistence`。
- **診斷記錄** —— 純文字檔 `%LocalAppData%\ImeModePersistence\log.txt`，記錄視窗切換與綁定是否生效，供你排查問題。它只留在你的裝置、達 1 MB 會輪替，可隨時刪除。

解除安裝並刪除該資料夾即可清除以上全部。

## 網路

本程式本身不發出任何網路要求。

## 聯絡

問題或回報：<https://github.com/mangokingTW/ImeModePersistence/issues>
