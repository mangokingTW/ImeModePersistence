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

## Install

Grab the latest [release](https://github.com/mangokingTW/ImeModePersistence/releases). Two options:

- **`ImeModePersistence-<version>-setup.exe`** &mdash; installer. Installs per-user into `%LocalAppData%\Programs\ImeModePersistence`, so it needs no administrator rights and raises no UAC prompt. Offers a *Start with Windows* checkbox during setup, and uninstalls from **Settings > Apps > Installed apps** like any other program, removing the autostart entry with it.
- **`ImeModePersistence-<version>-x64.zip`** / **`-x86.zip`** &mdash; portable. Unzip and run the executable; nothing is written outside the registry entry the tray toggle manages.

### Uninstalling

Any of these works:

- **Settings > Apps > Installed apps > ImeModePersistence > Uninstall**
- **Start menu > ImeModePersistence > Uninstall ImeModePersistence**
- Re-run the installer: it detects the existing copy and offers to remove it
- `%LocalAppData%\Programs\ImeModePersistence\unins000.exe`

Uninstalling removes the install directory and the autostart Run entry, including one enabled from the tray menu rather than during setup. Nothing else is left behind.

`SHA256SUMS.txt` accompanies every release. Both downloads are **unsigned**, so SmartScreen will warn on first run &mdash; choose *More info > Run anyway*, or verify the checksum first.

The installer is per-user on purpose. A machine-wide install buys nothing here, and only a scheduled task running with highest privileges could also reach elevated windows, at the price of keeping an elevated process resident. See *Scope and security* below.

## Running at logon

Toggle **Start with Windows** in the tray menu, or tick the box during setup. Both write the same value to:

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
```

Per-user, no administrator rights, no UAC prompt, and the utility starts at the same integrity level as the ordinary applications whose IME state it adjusts.

This is deliberately **not** a Windows service. A service runs in session 0 with no interactive desktop, so `GetForegroundWindow` would never see the user's windows and `WM_IME_CONTROL` would never reach their threads.

Only one instance runs per logon session, guarded by a named mutex &mdash; two copies would overwrite each other's restores.

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

The installer needs both architectures present, then [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 or newer:

```powershell
iscc /DAppVersion=0.2.1 installer\ImeModePersistence.iss
```

## Icon

`assets/app_icon.png` is the source artwork; `assets/ImeModePersistence.ico` is what the build embeds. Regenerate with [ImageMagick](https://imagemagick.org) 7:

```bash
./tools/make_icon.sh
```

The artwork's three elements inside a double border stop being readable below about 32 px, so the `.ico` carries a purpose-drawn **中** badge at 16, 20 and 24 px &mdash; the sizes the notification area actually uses &mdash; and the full artwork from 32 px up. Windows selects per slot. The small images are placed on integer coordinates so nothing is antialiased and they stay crisp.

## Versioning

`project(ImeModePersistence VERSION ...)` in `CMakeLists.txt` is the single source of truth. It is stamped into each executable's `VERSIONINFO` resource, so the version Explorer shows in **Properties > Details** is the version that was built.

**Every pull request must bump it.** CI fails a PR that leaves the version untouched, or that moves it sideways or backwards.

## Releasing

Push a tag matching the version already declared in `CMakeLists.txt`, and the `Release` workflow builds both architectures, compiles the installer, and publishes a GitHub release with the installer, both portable archives, and `SHA256SUMS.txt`:

```powershell
git tag v0.2.1
git push origin v0.2.1
```

The tag must be `vMAJOR.MINOR.PATCH`, because Inno Setup's `VersionInfoVersion` accepts nothing else. The workflow refuses to publish a tag that disagrees with `CMakeLists.txt`, so a release can never ship a binary whose reported version contradicts it. The workflow can also be dispatched manually with a tag name.

## Scope and security

- No global `WH_CALLWNDPROC` DLL injection is required by the current prototype.
- `SetWinEventHook(..., WINEVENT_OUTOFCONTEXT, ...)` observes foreground changes from the utility process.
- Windows security boundaries (UIPI / elevated applications) can prevent changing IME state in another integrity level. Run at the same integrity level as the target application when required.
- This project does not attempt to bypass Windows security boundaries.
- Autostart is a per-user `HKCU` Run entry, never a machine-wide `HKLM` one, and the utility never requests elevation.

## License

[MIT](LICENSE).

## Roadmap

- [x] TSF-aware conversion-mode adapter (via the IMM32/TSF interop layer, since TSF interfaces are per-thread and in-process only)
- [ ] Microsoft Bopomofo verification on real hardware
- [x] Retry/verify after focus activation
- [x] Better distinction between user-initiated and system-initiated changes
- [ ] Configuration UI
- [x] Start with Windows
- [x] Automated Windows CI
- [x] Installer and release pipeline
