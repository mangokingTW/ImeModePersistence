# Design notes

Why this utility is built the way it is. The README covers what it does; this covers the reasoning, including approaches that were tried and rejected.

## Reading another process's conversion mode

Two obvious approaches do not work for an out-of-process utility:

- **`ITfThreadMgr` with `GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION`.** TSF compartments are per-thread and live inside the owning process. A utility cannot activate a thread manager on a foreign thread, so the TSF interfaces only ever describe its own thread.
- **`ImmGetContext` / `ImmSetOpenStatus`**, used by the first prototype. An `HIMC` is process-local, so `ImmGetContext` returns `nullptr` for a window owned by another process. That prototype silently reported `Unknown` for every foreground window except its own — the persistence logic was correct but operating on nothing.

What does work is the IMM32 ↔ TSF interop layer (CUAS). `WM_IME_CONTROL` is handled by the target thread's default IME window (`ImmGetDefaultIMEWnd`), which marshals the request into that thread and reports the real `IME_CMODE_*` conversion mode — including for TSF text services such as Microsoft Bopomofo. `src/ime_interop.cpp` implements that path with `SendMessageTimeout`, so a hung foreground application cannot stall the message pump.

This is what makes native vs alphanumeric detection meaningful: Bopomofo clears `IME_CMODE_NATIVE` on Shift while the IME stays *open*, a distinction `ImmGetOpenStatus` alone cannot express. Restores preserve the target's other conversion flags (full/half shape, roman) and change only the native bit.

Confirmed working with Microsoft Bopomofo on real hardware.

## State machine

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

`SetWinEventHook(EVENT_SYSTEM_FOREGROUND, ...)` observes focus changes without injecting a DLL into every process. A 50 ms observer separates mode changes made while the same input context stays active from changes that only appear after a focus transition.

## Telling the user apart from the system

Conversion mode belongs to a *thread and keyboard layout*, not to a window: two windows of the same thread share one mode. State is therefore keyed on the `(thread, HKL)` pair, which also means switching to a non-IME layout such as US English reads as a system event rather than the user turning native mode off.

Three gates decide whether an observed change becomes the new global intent:

- **Identity is `(thread, HKL)`**, not `HWND`.
- **250 ms post-restore suppression** — anything seen right after our own write is that write echoing back.
- **150 ms dwell** — a change counts as the user's only once the input context has been stable that long, which excludes focus-transition churn.

An earlier attempt used a `restoring` boolean guard. It could never be observed as `true`: `set_mode` is synchronous and the observer timer runs on the same thread, so the flag was always back to `false` by the time anything looked at it.

## Best-effort writes

Every write is verified by reading the state back, because an IME that is still activating can discard it. After four attempts (~930 ms) the utility adopts whatever mode the target settled on rather than fighting it. If a context only becomes readable after the attempts run out, the observer starts a fresh round.

## Autostart

Not a Windows service: a service runs in session 0 with no interactive desktop, so `GetForegroundWindow` would never see the user's windows and `WM_IME_CONTROL` would never reach their threads. This has to live in the interactive session.

`HKCU\...\Run` rather than `HKLM` or a scheduled task: it needs no administrator rights and no UAC prompt, and it starts the utility at the same integrity level as the ordinary applications whose IME state it adjusts. Only a scheduled task running with highest privileges could also reach elevated windows, at the cost of keeping an elevated process resident.

A single-instance mutex is required once autostart is on, because two copies overwrite each other's restores in a loop.

## Installer

Per-user into `%LocalAppData%\Programs`, matching the autostart reasoning above: no admin, no UAC, same integrity level as the targets.

Inno Setup has no maintenance mode, so `InitializeSetup` reads `DisplayVersion` from the uninstall key and branches: an older installed copy is upgraded in place with no prompt, the same version offers repair or removal, and a newer one warns before downgrading. An unparseable version compares as equal, so a corrupted registry value lands on the prompt rather than silently upgrading or downgrading.

`AppMutex` reuses the single-instance mutex so Setup detects a running copy instead of failing to overwrite a locked executable. A second `[Registry]` entry with `ValueType: none` and `dontcreatekey` writes nothing at install time but registers the autostart value for deletion, so uninstalling also cleans up an entry enabled from the tray rather than through Setup.

## Icon

The source artwork carries three elements inside a double border, which turns to mush below about 32 px. An `.ico` may hold distinct images per size and Windows picks per slot, so 16/20/24 px use a purpose-drawn 中 badge placed on integer coordinates — no antialiasing, maximum crispness — and 32 px and up use the artwork.

`-type TrueColorAlpha` is forced when assembling: left alone, ImageMagick reduces the two-colour small sizes to a 4-bit palette, and palette ICO entries carry a 1-bit mask instead of an alpha channel, which turns the antialiased badge corners into jagged steps.

Background removal floods inward from a corner rather than matching a colour, because the glyphs are white and the canvas was near-white; matching on colour would punch holes in the artwork.

## Security boundaries

- No global `WH_CALLWNDPROC` DLL injection.
- UIPI stops any process from changing IME state at a higher integrity level. Run the utility at the same integrity level as the target application when that matters.
- No attempt is made to bypass Windows security boundaries, and the utility never requests elevation.
