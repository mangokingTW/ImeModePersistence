# ImeModePersistence - Antigravity Agent Guidelines & Project Context

## 1. 專案基本資訊
- **專案名稱**：ImeModePersistence（輸入法模式持續工具）
- **GitHub 儲存庫**：mangokingTW/ImeModePersistence
- **最新版本**：`v1.5.4`（已發布 GitHub Release，MSIX 已提交微軟商店審核，`main` 分支完全乾淨）
- **開發環境**：Windows 11 / x64 C++20（CMake 3.21+）
- **MSVC 編譯選項**：強制啟用 `/WX`（零警告）、`/utf-8`、`/analyze`

---

## 2. 核心架構與各子系統
1. **Sidecar Helper 輔助服務 (`src/helper.h`, `src/helper.cpp`)**：
   - 具名管道：`\\.\pipe\ImeModePersistence.Sidecar`（純同步雙向訊息管道，SDDL 限制本機互動使用者與管理員）。
   - 指令協定：`Ping(1)`, `WriteConversion(2)`, `WriteOpen(3)`, `Stop(4)`, `Read(5)`, `SwitchLayout(6)`。
   - Watchdog 生命週期守護執行緒監聽父行程與關閉事件。
   - 提權嚴禁複製到 Temp 目錄，直接由 `autostart::module_path()` 呼叫 `runas`。
2. **鍵盤配置與語言管理 (`src/layout.h`, `src/layout.cpp`, `src/tsf.h`, `src/tsf.cpp`)**：
   - 階梯式切換：`FocusWindow` ➔ `ThreadWindows` ➔ `TsfSession`（優先嘗試 Helper 委派）。
3. **IME 狀態讀寫 (`src/ime_interop.h`, `src/ime_interop.cpp`)**：
   - 透過 `GetGUIThreadInfo` 與 `ImmGetDefaultIMEWnd` 鎖定真實焦點子視窗。
   - 透過 `WM_IME_CONTROL` 讀寫 `IMC_GETCONVERSIONMODE` / `IMC_SETCONVERSIONMODE` / `IMC_GETOPENSTATUS` / `IMC_SETOPENSTATUS`。
4. **狀態機、輪詢與防幽靈過濾 (`src/persist.h`, `src/persist.cpp`, `src/main.cpp`)**：
   - 15ms~50ms 自適應輪詢，嚴密排除桌面、工作列、Windows 11 XAML 托盤溢位視窗（`TopLevelWindowForOverflowXamlIsland`）等 Shell 介面。
5. **程式綁定規則 (`src/rules.h`, `src/rules.cpp`)**：
   - 支援完整路徑、檔名、視窗類別（`class:stingray_window` 給《絕地戰兵 2》等遊戲）、萬用字元 Glob。
6. **游標輸入指示徽章 (`src/caret.h`, `src/overlay.h`)**：
   - 在文字游標旁繪製半透明小徽章（`中` / `あ` / `한` / `Ａ` / `EN`），支援 Chromium 網址列避讓。

---

## 3. 嚴格遵守之安全鐵律（不可妥協）
- ❌ **嚴禁 DLL 注入**（零 `VirtualAllocEx` / `WriteProcessMemory` / `CreateRemoteThread`）。
- ❌ **嚴禁全域鍵盤 Hook**（零 `WH_KEYBOARD_LL`）。
- ❌ **嚴禁模擬按鍵**（零 `SendInput` / `keybd_event`）。
- ❌ **嚴禁側錄鍵盤**（零 `GetAsyncKeyState`）。
- ❌ **零網路連線**（不連結任何網路庫）。
- 🔒 **提權最小化**：預設一般使用者權限，所有提權需由使用者手動觸發。

---

## 4. 發布與版本規範
- **三處版號一致性**：`CMakeLists.txt`、`packaging/msix/AppxManifest.xml`（末位補 0）、`CHANGELOG.md`（雙語對照）。
- **CI/CD 發布**：透過推 Git Tag（`vX.Y.Z`）自動觸發 `release.yml`（編譯 x64、Inno Setup、MSIX、SLSA 來源證明、GitHub Release、Partner Center API 提交）。
- **協作流程**：Branch + PR，所有 CI 檢查全綠（GREEN），以 **Squash Merge** 合併至 `main`。
