# ImeModePersistence

[![Windows build](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml/badge.svg)](https://github.com/mangokingTW/ImeModePersistence/actions/workflows/windows-build.yml)

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
             same (thread, layout) context
                        |
                 settled for 150 ms
                        |
                        v
                 desiredMode = X

Foreground window change
          |
          v
 EVENT_SYSTEM_FOREGROUND
          |
          v
   wait for IME/TSF activation
          |
          v
   write desiredMode
          |
          v
   read it back to verify  --mismatch--> retry (60/120/250/500 ms)
          |
        match
          |
          v
   suppress observation for 250 ms
```

The prototype uses `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, ...)` rather than injecting a DLL into every process. A 50 ms observer distinguishes mode changes that happen while the same input context remains active from changes observed only after a focus transition.

Conversion mode belongs to a *thread and keyboard layout*, not to a window: two windows of the same thread share one mode. The observer therefore keys its state on the `(thread, HKL)` pair, which also means switching to a non-IME layout such as US English is treated as a system event rather than as the user turning native mode off.

## Reading another process's conversion mode

Two obvious approaches do not work for an out-of-process utility:

- **`ITfThreadMgr` and `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`.** TSF compartments are per-thread and live inside the owning process. A utility cannot activate a thread manager on a foreign thread, so the TSF interfaces only ever describe its own thread.
- **`ImmGetContext` / `ImmSetOpenStatus`** (used by the first prototype). An `HIMC` is process-local, so `ImmGetContext` returns `nullptr` for a window owned by another process. That prototype silently reported `Unknown` for every foreground window except its own.

What does work is the IMM32 &harr; TSF interop layer (CUAS). `WM_IME_CONTROL` is handled by the target thread's default IME window (`ImmGetDefaultIMEWnd`), which marshals the request into that thread and reports the real `IME_CMODE_*` conversion mode &mdash; including for TSF text services such as Microsoft Bopomofo. `src/ime_interop.cpp` implements that path with `SendMessageTimeout`, so a hung foreground application cannot stall the message pump.

This is what makes native vs alphanumeric detection meaningful: Microsoft Bopomofo clears `IME_CMODE_NATIVE` on Shift while the IME stays *open*, a distinction `ImmGetOpenStatus` alone cannot express. Restores preserve the target's other conversion flags (full/half shape, roman) and only change the native bit.

## Remaining limitation

Every write is best-effort and verified by reading the state back, because an IME that is still activating can discard the change. After four attempts (~930 ms) the utility gives up on that window and adopts whatever mode the target settled on, rather than fighting it.

Behaviour has been validated by compilation in CI for x64 and x86. Runtime behaviour with **Microsoft Bopomofo** on real hardware is still unverified &mdash; see the roadmap.

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Executable:

```text
build\Release\ImeModePersistence.exe
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

- [x] TSF-aware conversion-mode adapter (via the IMM32/TSF interop layer, since TSF interfaces are per-thread and in-process only)
- [ ] Microsoft Bopomofo verification on real hardware
- [x] Retry/verify after focus activation
- [x] Better distinction between user-initiated and system-initiated changes
- [ ] Configuration UI
- [ ] Start with Windows
- [x] Automated Windows CI
