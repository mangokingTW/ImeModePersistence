# ImeModePersistence

Windows utility that attempts to keep the **last user-selected IME mode** when switching foreground windows.

## Intended behavior

Example:

1. Window A is in Chinese/native input mode.
2. Switch to Window B -> the utility restores Chinese/native mode.
3. User intentionally switches B to English/alphanumeric mode.
4. Switch to Window C -> the utility restores English/alphanumeric mode.
5. The global desired mode therefore follows the user's most recent mode change.

This is intentionally **not** a "force Chinese" or "force Japanese" tool.

## Current architecture

```text
                 User changes mode
                        |
                 same foreground HWND
                        |
                        v
                 lastUserMode = X

Foreground window change
          |
          v
 EVENT_SYSTEM_FOREGROUND
          |
          v
   wait for IME/TSF activation
          |
          v
   restore lastUserMode
```

The prototype uses `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, ...)` rather than injecting a DLL into every process. A 50 ms observer distinguishes mode changes that happen while the same foreground window remains active from changes observed only after a focus transition.

## Important limitation

`IMM32::ImmGetOpenStatus` and `ImmSetOpenStatus` expose the IME open/closed state, not every TSF conversion-mode detail. For many traditional Windows IMEs this is a useful approximation of native vs alphanumeric input, but it is **not sufficient to claim complete support for every modern TSF IME**.

The next implementation step is a TSF adapter for the target IME (especially Microsoft Traditional Chinese / Microsoft Bopomofo) and verification/retry after TSF focus activation.

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Executable:

```text
build\\Release\\ImeModePersistence.exe
```

For x86:

```powershell
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

## Scope and security

- No global `WH_CALLWNDPROC` DLL injection is required by the current prototype.
- `SetWinEventHook(..., WINEVENT_OUTOFCONTEXT, ...)` observes foreground changes from the utility process.
- Windows security boundaries (UIPI / elevated applications) can prevent changing IME state in another integrity level. Run at the same integrity level as the target application when required.
- This project does not attempt to bypass Windows security boundaries.

## Roadmap

- [ ] TSF mode adapter
- [ ] Microsoft Bopomofo verification
- [ ] Retry/verify after focus activation
- [ ] Better distinction between user-initiated and system-initiated changes
- [ ] Configuration UI
- [ ] Start with Windows
- [ ] Automated Windows CI
