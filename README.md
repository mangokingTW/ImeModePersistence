# ImeModePersistence

A Windows 11 C++ tray utility that experiments with keeping the user's last IME input state across foreground-window changes.

## Current implementation

- Win32 C++20
- `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` instead of injecting a DLL into every process
- IMM32 adapter for reading/writing the coarse IME open state
- 30 ms deferred restore after foreground changes
- x64/x86 can be built with the same CMake project
- No administrator privileges are required for ordinary same-integrity applications

## Important limitation

The current adapter maps `ImmGetOpenStatus()` to `Native`/`Alphanumeric`. Modern Windows TSF IMEs can expose conversion modes that are more detailed than IMM32's open/closed flag. Therefore this first implementation is intentionally a foundation, not a claim of complete Microsoft Pinyin/Traditional Chinese TSF compatibility.

For robust Microsoft Bopomofo / Traditional Chinese support, the next implementation step is a TSF adapter that observes the active `ITfDocumentMgr` / `ITfContext` and the IME conversion mode, while preserving the distinction between user-initiated mode changes and focus-transition resets.

## Build

Use a Visual Studio Developer PowerShell or a shell with CMake and MSVC available:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable will be under `build\\Release\\ImeModePersistence.exe`.

For 32-bit:

```powershell
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

## Architecture

```text
Foreground window change
        |
        v
SetWinEventHook(EVENT_SYSTEM_FOREGROUND)
        |
        v
30 ms deferred callback
        |
        v
Query target IME state
        |
        v
Restore last known state
```

## Security / integrity notes

`SetWinEventHook(..., WINEVENT_OUTOFCONTEXT, ...)` avoids the DLL-injection model of a global `WH_CALLWNDPROC` hook. UIPI still matters: a lower-integrity process cannot freely manipulate every higher-integrity target window. Run the utility at the same integrity level as the applications it needs to control. Do not run it elevated unless necessary.

## Roadmap

1. Add a TSF-backed state adapter.
2. Detect user-initiated mode changes reliably.
3. Add retry/verification around TSF focus transitions.
4. Add per-IME behavior and diagnostics.
5. Add automated tests for same-integrity foreground transitions.
