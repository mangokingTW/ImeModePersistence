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

## What counts as the foreground application

`GetForegroundWindow` does not answer "which application is the user working in" on Windows 11. The shell's own UI takes the foreground for windows nobody is typing into, and they are not all part of explorer — `SearchHost.exe` is a separate process under `SystemApps`, which is why excluding the shell process alone was not enough.

Two filters, for two different problems:

- **Cloaked or invisible foreground windows are skipped entirely.** A cloaked window is composed but not shown, which is the state SearchHost and the Start menu sit in after being dismissed. `DwmGetWindowAttribute` with `DWMWA_CLOAKED` identifies them. This is not cosmetic: treating one as a context switch re-keys the observer, resets the dwell timer and interrupts a restore already in flight, so these ghosts were actively breaking enforcement, not just the diagnostics.
- **Visible shell surfaces are tracked but never recorded as "the last application".** SearchHost with the search box genuinely open is visible and focused, and is still not what the user means. A name list, because these processes have nothing structural in common.

## Telling the user apart from the system

Conversion mode belongs to a *thread and keyboard layout*, not to a window: two windows of the same thread share one mode. State is therefore keyed on the `(thread, HKL)` pair, which also means switching to a non-IME layout such as US English reads as a system event rather than the user turning native mode off.

Three gates decide whether an observed change becomes the new global intent:

- **Identity is `(thread, HKL)`**, not `HWND`.
- **250 ms post-restore suppression** — anything seen right after our own write is that write echoing back.
- **150 ms dwell** — a change counts as the user's only once the input context has been stable that long, which excludes focus-transition churn.

An earlier attempt used a `restoring` boolean guard. It could never be observed as `true`: `set_mode` is synchronous and the observer timer runs on the same thread, so the flag was always back to `false` by the time anything looked at it.

## Best-effort writes

Every write is verified by reading the state back, because an IME that is still activating can discard it. After four attempts (~930 ms) the utility adopts whatever mode the target settled on rather than fighting it. If a context only becomes readable after the attempts run out, the observer starts a fresh round.

## Turning the global behaviour off

Per-application bindings are useful without the carry-over, so the tray menu can disable it. Off means three things stop: no mode is promoted to the global target, no mode is restored on a context switch, and the recovery round that retries an unreadable context does not run. Bindings are unaffected, since they are enforced before the mode in `restore_tick` and do not consult the target mode at all.

Toggling either way clears the remembered target. Keeping it would show a desired mode in the status box that is not being applied, and re-enabling should pick up what the user is doing now rather than something from before the switch.

## App language bindings

Binding a layout is a different operation from setting a conversion mode. The mode lives on whichever layout a thread already has active; a rule replaces the layout itself, which is how Bopomofo, a US keyboard and a Japanese IME become distinct targets rather than shades of one.

**Rules store a LANGID, not an HKL.** The high word of an `HKL` is a runtime device handle that differs between logon sessions, so a persisted `HKL` would be meaningless on the next boot. The low word is the language identifier and is stable, so rules store that and resolve it against `GetKeyboardLayoutList` at the moment of use. The cost is that a rule binds a *language*: where one language has several IMEs installed, the first installed one wins and a specific IME cannot be singled out. Doing better needs `ITfInputProcessorProfileMgr::ActivateProfile` with a profile GUID.

**A rule is keyed by full path or by bare file name**, and which one it is can be read off the key itself: a key containing a path separator is a path rule. Lookup tries the path first, so one particular copy of an application can be singled out — two Electron apps both called `app.exe`, say — while a bare name still matches wherever the application is installed. Nothing extra is stored to distinguish the two forms.

A window class was rejected as the key: neither stable nor discoverable by whoever writes the rule. `QueryFullProcessImageNameW` with `PROCESS_QUERY_LIMITED_INFORMATION` reads the name — the limited right is deliberate, being the only one granted across integrity levels.

**The layout is enforced before the mode.** Writing the mode first would write it to the layout that is on its way out.

**Switching another process's layout has no single reliable mechanism.** `WM_INPUTLANGCHANGEREQUEST` only takes effect if the receiving window lets it reach `DefWindowProc`, and plenty of applications — anything Chromium-based, most UWP and WinUI — do not. v0.4.4 posted it to the top-level foreground window with `INPUTLANGCHANGE_SYSCHARSET` and did not work at all on real applications.

Three mechanisms are now tried in escalating order, one per retry attempt, each verified by reading the layout back:

1. **Focus window.** `GetGUIThreadInfo` gives the window that actually holds keyboard focus, which is what owns the input language; the top-level window frequently forwards nothing. `wParam` is now 0 — `INPUTLANGCHANGE_SYSCHARSET` asks the window to switch only if the layout matches the system character set, a condition irrelevant to an explicit request that some windows honour by refusing.
2. **Every top-level window of the thread**, via `EnumThreadWindows`. Some applications keep a separate message-handling window that honours the request when the visible one ignores it.
3. **`ITfInputProcessorProfileMgr::ActivateProfile` with `TF_IPPMF_FORSESSION`.** Asks the Text Services Framework to move the *session's* input language rather than asking the window to move its own, so nothing about the target is read, opened or attached to. That matters beyond tidiness: an anti-cheat protected process refuses to be opened at all, and probing it is the behaviour anti-cheat exists to catch.

   This replaced `AttachThreadInput` + `ActivateKeyboardLayout`, which was never shown to work and was the only technique here that anti-cheat is built to notice. Removing it lowers the risk to the user's account and loses nothing demonstrated.

   **Confirmed to reach a raw-input fullscreen game protected by anti-cheat** (Helldivers 2, window class `stingray_window`). This was the central unknown: the documentation says session rather than thread scope, and whether that crossed into such a process could only be settled by trying it. It does.

Which mechanism was last tried, and whether the layout ended up where the rule wanted it, is reported in two places.

**The tray tooltip is the primary one, because hovering does not change the foreground window.** The status box originally read the live foreground and so reported `explorer.exe` every single time it was opened: clicking the tray icon is what hands the foreground to the shell, so the act of asking destroyed the answer. It now reports a snapshot of the last application that was neither this process nor the shell — identified by comparing process ids against `GetShellWindow`, not by matching an executable name. Without those, an ignored request is indistinguishable from a rule that never matched — and rules can fail to match for an unrelated reason: the executable a user browses to is sometimes a launcher stub whose process image path differs, which is common for Store applications under `WindowsApps`.

A rule naming an uninstalled layout is dropped rather than retried, since retrying cannot help.

The readouts show whatever identity could be established, in exactly the form a rule key takes: the full path when readable, otherwise `class:<name>`. Reporting only the failure — which the first attempt did — left no way to see the class a rule has to match, so a `class:` rule could not be checked against reality.

**Identification is the other half of the problem.** Reading an executable path needs `OpenProcess`, which an anti-cheat protected game refuses even to an administrator — so for those the rule never matched and no switching mechanism could have helped. `GetClassName` reads a window property and needs no access to the process, which makes a `class:` rule the only way to name such an application. Lookup runs most specific to least: full path, file name, window class.

The bindings dialog carries `WS_EX_APPWINDOW`. The taskbar omits owned windows, and this dialog's owner is the hidden tool window, so without it the dialog has no taskbar button and vanishes behind whatever the user clicks next. It also tracks its own handle: the dialog is modal only to that hidden owner, which leaves the tray menu live and able to ask for a second copy, so a repeat request raises the existing window rather than nesting another modal loop.

The bindings dialog receives the last foreground application from the caller instead of asking the system, and from the same shell-excluding snapshot the status box uses. It originally had its own field updated on every context switch, which meant **Use last app** filled in `explorer.exe` for exactly the reason the status box did: reaching the dialog goes through the tray icon. Once the dialog is open, the foreground window belongs to this process — which is also why the observer now skips windows by process ID rather than by comparing against the message-only window's handle.

## Autostart

Not a Windows service: a service runs in session 0 with no interactive desktop, so `GetForegroundWindow` would never see the user's windows and `WM_IME_CONTROL` would never reach their threads. This has to live in the interactive session.

`HKCU\...\Run` rather than `HKLM` or a scheduled task: it needs no administrator rights and no UAC prompt, and it starts the utility at the same integrity level as the ordinary applications whose IME state it adjusts. Only a scheduled task running with highest privileges could also reach elevated windows, at the cost of keeping an elevated process resident.

A single-instance mutex is required once autostart is on, because two copies overwrite each other's restores in a loop.

## Installer

**Administrator, installing to Program Files.** This reverses the original per-user design, and the reason is that reading a window belonging to an elevated process requires equal privileges — anti-cheat protected games are elevated, so an unelevated utility cannot even see which application is in front. Elevation is therefore the default rather than the exception it was assumed to be.

Setup needs the same rights to register the logon task, and gets one thing for free: only an elevated Setup can close an elevated copy of the utility, so updating no longer asks the user to close it by hand.

`PrivilegesRequiredOverridesAllowed=dialog` leaves the decision with the user: install for all users and elevate, or for this user only and not elevate anywhere. Forcing admin would lock out someone who only cares about ordinary applications, and `{autopf}` follows whichever they pick.

Autostart is therefore a single checkbox whose mechanism follows the install mode — a scheduled task with highest privileges when elevated, a Run entry when not. Offering both mechanisms would ask the same question twice and permit the contradictory answer of both at once. The Run key cannot start an elevated program at all, and a task is the only way to do it without a UAC prompt at every logon.

Elevation cannot be added to a running process, so the tray offers **Restart as administrator**, which hands over to a fresh elevated copy. The single-instance mutex is released before the handover, since the new copy checks it while starting and would otherwise exit immediately; if the UAC prompt is declined the mutex is taken back rather than leaving the instance unguarded. The item is hidden when already elevated instead of greyed, because a disabled item invites the question of how to enable it.

Inno Setup has no maintenance mode, so `InitializeSetup` reads `DisplayVersion` from the uninstall key and branches: an older installed copy is upgraded in place with no prompt, the same version offers repair or removal, and a newer one warns before downgrading. An unparseable version compares as equal, so a corrupted registry value lands on the prompt rather than silently upgrading or downgrading.

Windows cannot replace a running executable, so an upgrade has to stop the utility first. `AppMutex` alone turns that into a prompt telling the user to close it by hand, which is a poor trade for something the installer can do itself: `InitializeSetup` posts `WM_CLOSE` to the window, waits up to 5 s for the mutex to clear, and `[Run]` starts it again afterwards. `AppMutex` stays as the last-resort guard for when a hung process ignores the request.

This is why the hidden window is a normal top-level window rather than an `HWND_MESSAGE` one. Message-only windows are children of `HWND_MESSAGE`, so they are invisible to `FindWindow` and never receive `WM_CLOSE` or `WM_QUERYENDSESSION` — unreachable by both the installer and the shell at logoff. `WS_EX_TOOLWINDOW` and never calling `ShowWindow` keep it out of the taskbar and Alt-Tab. A second `[Registry]` entry with `ValueType: none` and `dontcreatekey` writes nothing at install time but registers the autostart value for deletion, so uninstalling also cleans up an entry enabled from the tray rather than through Setup.

## Icon

The source artwork carries three elements inside a double border, which turns to mush below about 32 px. An `.ico` may hold distinct images per size and Windows picks per slot, so 16/20/24 px use a purpose-drawn 中 badge placed on integer coordinates — no antialiasing, maximum crispness — and 32 px and up use the artwork.

`-type TrueColorAlpha` is forced when assembling: left alone, ImageMagick reduces the two-colour small sizes to a 4-bit palette, and palette ICO entries carry a 1-bit mask instead of an alpha channel, which turns the antialiased badge corners into jagged steps.

Background removal floods inward from a corner rather than matching a colour, because the glyphs are white and the canvas was near-white; matching on colour would punch holes in the artwork.

## Appearance

Three things that all hang off one file. `assets/ImeModePersistence.manifest` is embedded as an `RT_MANIFEST` resource and supplies:

- the **ComCtl32 version 6** dependency, without which the dialog renders with Windows 95 era controls regardless of anything done in code;
- **Per-Monitor V2** DPI awareness, which brings automatic non-client scaling, dialog scaling and control scaling, so no `WM_DPICHANGED` handling is needed here;
- `asInvoker`, restating in the manifest that this never elevates;
- the Windows 10/11 `supportedOS` GUID, which stops the shell applying compatibility shims meant for older applications.

The linker is told `/MANIFEST:NO`. MSVC generates a manifest by default, two would collide, and the generated one would win — silently taking visual styles and DPI awareness with it.

`InitCommonControlsEx` loads the DLL and registers the classes; the manifest is what selects the version.

**Dark mode is the title bar only.** `DwmSetWindowAttribute` with `DWMWA_USE_IMMERSIVE_DARK_MODE` is documented and stable — the attribute settled on 20 in Windows 10 20H1 and was 19 in builds 18985-19041, so both are tried and a failure is harmless. Making the controls dark as well needs undocumented uxtheme ordinals (`SetPreferredAppMode` and friends), a dependency that can break on any Windows update; not worth it for a dialog this size. Dark state is read from `AppsUseLightTheme` in the registry because the documented alternative is WinRT, for one DWORD. `WM_SETTINGCHANGE` with `ImmersiveColorSet` re-applies it, so a light/dark switch while the dialog is open is followed rather than stale until reopened.

The status and error boxes are task dialogs rather than message boxes. Besides carrying the visual style, `TaskDialog` centres on the *screen* while `MessageBox` centres on its *owner* — and this owner is a hidden zero-sized window at the top-left corner. Both fall back to `MessageBox` if the call fails.

## Language

User-visible text lives in `src/strings.cpp` as one struct per language, chosen once from `GetUserDefaultUILanguage`. The UI language rather than the locale: someone running English Windows in a Taiwanese locale is reading English menus everywhere else.

A struct with designated initialisers, not an enum indexing parallel arrays — adding a string then forces every language to supply it at the same place instead of silently shifting every index after it.

The dialog template keeps English text and is relabelled at `WM_INITDIALOG`, so there is one layout to maintain rather than one per language. Control widths are sized for the English strings, which are the longer of the two. The static labels share `IDC_STATIC`, so they are addressed by position in the child order.

**`/utf-8` is mandatory for MSVC.** The sources are UTF-8 without a BOM, and without that flag MSVC reads them in the system ANSI code page: on an en-US machine every Chinese literal becomes CP1252 mojibake. It compiles without a single warning — those bytes are nearly all valid CP1252, so not even C4819 fires — and passes every other check, so v0.4.2 and v0.4.3 both shipped with an unreadable Chinese UI. A CI step now searches the built binary for the UTF-16LE bytes a menu string should have, because nothing short of inspecting the binary or the screen catches this.

Any Chinese display language selects Traditional; no Simplified translation is provided. The installer wizard stays English because Inno Setup's official distribution ships no Chinese `.isl`, and fetching an unofficial translation at build time would put a third-party download in the release path.

## Security boundaries

- No global `WH_CALLWNDPROC` DLL injection.
- UIPI stops any process from changing IME state at a higher integrity level. Run the utility at the same integrity level as the target application when that matters.
- No attempt is made to bypass Windows security boundaries, and the utility never requests elevation.
