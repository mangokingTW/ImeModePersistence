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

Every write is verified by reading the state back, because an IME that is still activating can discard it. After four attempts the utility adopts whatever mode the target settled on rather than fighting it. If a context only becomes readable after the attempts run out, the observer starts a fresh round.

## Two cadences, because the two reads cost different amounts

Observation is split across two timers, and the reason is the cost of the read rather than the importance of the value.

Reading the **conversion mode** means `SendMessageTimeoutW` to another process's default IME window. That happens on every observer tick, so the 50 ms interval is already twenty cross-process messages a second into whatever is in front — and what is in front may be an anti-cheat-protected game. Polling it faster multiplies exactly the traffic this design has otherwise gone out of its way not to generate.

Reading the **layout** costs nothing comparable: `GetKeyboardLayout` asks the window manager about a thread and sends that thread nothing. So the layout a binding enforces is polled every **15 ms** on its own timer, by a tick that deliberately re-derives nothing — no process identity, no registry lookup, no cloaked-window vetting, all of which the foreground hook and the observer have already done for the window in question. The required `HKL` is resolved once when the rule is looked up, so the fast poll is a handle comparison.

**What this buys.** Pressing Win+Space in a bound application used to survive the 50 ms observer plus a 60 ms first attempt; it now survives the 15 ms poll plus a 10 ms one. A binding therefore behaves like a lock on the keyboard layout: an unwanted switch is put back in roughly a fiftieth of a second instead of a tenth, and there is no limit on how many times, because the four-attempt budget is per round and every new drift starts a new one.

**Why the first delay differs by trigger.** The 60 ms wait exists because a foreground change fires *before* the new thread's IME is usable, and an attempt that lands too early is spent for nothing. Drift inside the application already in front has nothing to wait for — that thread is running and its IME is up — so its first attempt goes out as soon as `SetTimer` can deliver one. The two schedules live in `src/schedule.cpp` and are asserted in `tests/test_schedule.cpp`, because they are numbers whose safety is a relationship rather than a value.

**The cooldown is part of the faster poll, not a caveat to it.** Raising the polling rate without one would mean that an application which insists on its own layout gets a fresh round of requests every 15 ms for as long as it stays in front. So a round whose layout was still refused when the budget ran out marks the target as left alone, logged as such — for three seconds after the first loss, doubling per consecutive loss up to thirty. A flat pause meant a hopeless argument was re-fought, seven log lines a round, every few seconds indefinitely; the back-off makes it asymptotically quiet while still retrying on a human timescale, and one won round resets it. Only then: a protected target's conversion mode is typically unreadable, so its rounds routinely exhaust the budget on the *mode* with the layout satisfied on the first attempt, and punishing the poll for that would disable it in exactly the scenario it exists for. A genuine context change clears the cooldown: switching away and back is the way to make a binding try again, and it is what the wiki tells the user to do.

**One application is one argument, however many threads it has.** A Chromium-style application moves the foreground between its own threads constantly, and each move used to re-key the context and restart the escalation at the first mechanism — so a target that ignores that mechanism was asked with it forever, and the ladder never climbed past "attempt 1" (a user's diagnostic log showed precisely that). A context switch that resolves to the same application under the same rule is now a *continuation*: it keeps the attempt counter, the cooldown and the lost-round history, while a genuine change of application still resets them all. Identity is the executable when readable, the window class when not — the class being what the rule matched anyway. This is also what stops an application from clearing its own cooldown by shuffling focus between its threads.

**The observer must not answer drift itself.** The 50 ms tick re-keys its state when the foreground thread's layout changes, and an in-flight drift round *is* such a change -- treating it as a context switch killed the round mid-escalation, restarted the budget, and wiped the cooldown, which both slowed the reassert back down to the old latency and re-opened the endless-fight case the cooldown closes. So a same-thread layout change in a bound window is adopted and otherwise left to the fast poll; only a change with no rule in play re-keys the observer, because there it really is the user switching layouts by hand.

**Satisfaction is judged by language, not by handle.** A rule stores a LANGID, and `find_by_language` resolves it to the first matching HKL -- but a user with two layouts of one language (QWERTY and Dvorak, two IMEs of one language) satisfies the rule with either. Comparing the full HKL made the binding revert the user's pick between same-language variants every 15 ms, enforcing a distinction no rule can express.

That circuit breaker is also the answer to whether this can lock the keyboard up. It cannot fight indefinitely, and every mechanism it uses is bounded: nothing intercepts keystrokes, so quitting the utility or removing the rule restores normal behaviour immediately.

**Blocking reads are re-entry points.** Reading another process's IME state blocks inside `SendMessageTimeoutW`, and out-of-context WinEvent callbacks are delivered during that wait -- so a foreground change can run `note_context_switch` *inside* `restore_tick`'s read, after which the resumed code would overwrite the newer round's state with the stale window's. Every function that blocks across a read now checks afterwards whether the world it captured still exists: a generation counter bumped by each context switch, and `pendingWindow` identity for a round's own reschedules.

## Why not actually lock the layout

There is no per-application input-method restriction in Windows to use. The installed layout list is per-user (`HKCU\Keyboard Layout\Preload`) and activation is per-thread; no API, policy or registry value expresses "this process may use only this layout". Enforcement by reassertion is the only thing available, which is why the latency above is the whole game.

Genuinely *preventing* a switch rather than undoing it would need a low-level keyboard hook swallowing Win+Space, Ctrl+Shift and Shift. Rejected: `WH_KEYBOARD_LL` is a technique anti-cheat watches for, a global hook that eats keystrokes has the same shape as a keylogger, and its failure mode is a machine that cannot switch input methods at all until the utility is killed. Reasserting in ~25 ms gets the same result for the user without any of that.

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

   **Not confirmed to reach a raw-input fullscreen game.** This was recorded as confirmed after the class-binding work made Helldivers 2 work, which was an inference: at that point the escalation still tried both window-message mechanisms first, so which of the three actually succeeded was never established. The diagnostic log later showed four consecutive TSF attempts leaving the layout untouched, so TSF alone does not reach such a target.

**Every target gets the full escalation.** v0.7.1 sent protected targets straight to TSF, to avoid posting window messages into an anti-cheat process. That rested on TSF being what reached such a target — an assumption never tested, and the diagnostic log disproved it: four TSF attempts in a row left the layout unchanged. Whatever worked before that change was one of the window-message mechanisms, so skipping them silently broke the feature the whole thing exists for.

The lesson is narrower than "do not optimise": the change was made on an inference presented as a finding, and there was no way to observe the difference until the log existed. Each attempt now also records whether the call was *issued* or *refused*, so a mechanism that reports success and changes nothing can be told from one that was rejected.

Which mechanism was last tried, and whether the layout ended up where the rule wanted it, is reported in two places.

**The tray tooltip is the primary one, because hovering does not change the foreground window.** The status box originally read the live foreground and so reported `explorer.exe` every single time it was opened: clicking the tray icon is what hands the foreground to the shell, so the act of asking destroyed the answer. It now reports a snapshot of the last application that was neither this process nor the shell — identified by comparing process ids against `GetShellWindow`, not by matching an executable name. Without those, an ignored request is indistinguishable from a rule that never matched — and rules can fail to match for an unrelated reason: the executable a user browses to is sometimes a launcher stub whose process image path differs, which is common for Store applications under `WindowsApps`.

A rule naming an uninstalled layout is dropped rather than retried, since retrying cannot help.

The readouts show whatever identity could be established, in exactly the form a rule key takes: the full path when readable, otherwise `class:<name>`. Reporting only the failure — which the first attempt did — left no way to see the class a rule has to match, so a `class:` rule could not be checked against reality.

**Identification is the other half of the problem.** Reading an executable path needs `OpenProcess`, which an anti-cheat protected game refuses even to an administrator — so for those the rule never matched and no switching mechanism could have helped. `GetClassName` reads a window property and needs no access to the process, which makes a `class:` rule the only way to name such an application. Lookup runs most specific to least: full path, file name, window class.

The bindings dialog carries `WS_EX_APPWINDOW`. The taskbar omits owned windows, and this dialog's owner is the hidden tool window, so without it the dialog has no taskbar button and vanishes behind whatever the user clicks next. It also tracks its own handle: the dialog is modal only to that hidden owner, which leaves the tray menu live and able to ask for a second copy, so a repeat request raises the existing window rather than nesting another modal loop.

The bindings dialog receives the last foreground application from the caller instead of asking the system, and from the same shell-excluding snapshot the status box uses. It originally had its own field updated on every context switch, which meant **Use last app** filled in `explorer.exe` for exactly the reason the status box did: reaching the dialog goes through the tray icon. Once the dialog is open, the foreground window belongs to this process — which is also why the observer now skips windows by process ID rather than by comparing against the message-only window's handle.

## Autostart

Not a Windows service: a service runs in session 0 with no interactive desktop, so `GetForegroundWindow` would never see the user's windows and `WM_IME_CONTROL` would never reach their threads. This has to live in the interactive session.

Two mechanisms, because the Run key can only ever start an **unelevated** copy. An elevated utility needs a scheduled task with highest privileges instead, which is also the only way to start elevated without a UAC prompt at every logon.

This section originally argued for the Run key alone, on the grounds that the utility should sit at the same integrity level as ordinary applications. That reasoning was overturned by evidence: reading the windows of an elevated process requires equal privileges, and the games this exists for are elevated.

A single-instance mutex is required once autostart is on, because two copies overwrite each other's restores in a loop.

The account the task is registered for is the **interactive session's user**, not the process token's. The two differ under over-the-shoulder elevation -- a standard user typing an administrator's credentials leaves the elevated copy (and elevated Setup) running as the administrator, and a task registered with `/RU` that account never fires for the user who actually logs in. The utility reads the session user from WTS; the installer reads LogonUI's record of who is signed in at the console, with `{username}` as the fallback.

## Installer

**Administrator, installing to Program Files.** This reverses the original per-user design, and the reason is that reading a window belonging to an elevated process requires equal privileges — anti-cheat protected games are elevated, so an unelevated utility cannot even see which application is in front. Elevation is therefore the default rather than the exception it was assumed to be.

Setup needs the same rights to register the logon task, and gets one thing for free: only an elevated Setup can close an elevated copy of the utility, so updating no longer asks the user to close it by hand.

**Two installers, compiled twice from one script.** `/DUserInstall` switches the privileges, the install directory, the output name, and which autostart mechanisms exist. Shipping both is what removes the trade-off: whoever has no administrator rights, or does not want elevation anywhere, gets an installer that asks for neither. CI compiles both variants, because a syntax error inside a `#ifdef` branch is invisible unless that branch is compiled.

**Reading the uninstall entry from both hives.** An elevated install records itself in `HKLM`, an unelevated one in `HKCU`. The version comparison originally read only `HKCU`, so from the moment Setup became elevated in v0.7.0 an administrator install was invisible to it: re-running Setup neither recognised an upgrade nor offered removal. That shipped.

Setup always elevates in the administrator variant. An earlier attempt used `PrivilegesRequiredOverridesAllowed=dialog` and read "install for all users or just me" as the elevation question, which conflates install *scope* with whether the *utility* runs elevated — not the same choice, and only the second one is what a user cares about here. Setup being elevated is also what lets it register the logon task and close a running elevated copy when updating. Anyone without administrator rights has the portable archive.

Elevation is therefore a task checkbox, ticked by default, and autostart is a second independent one. Elevation decides the autostart mechanism: a scheduled task with highest privileges when elevated, a Run entry when not.

**The tray toggle follows the same rule**, managing whichever mechanism matches the privileges the utility currently has. It previously only ever wrote the Run key, reasoning that creating a task needs administrator rights the utility does not have — which stopped being true the moment elevation became the default. An elevated copy has exactly those rights, so the toggle had been offering the one mechanism that cannot start the copy the user is running.

Turning autostart off removes both mechanisms: leaving the other behind would keep starting the utility after the user turned it off. Enabling while elevated also clears any Run entry, which would otherwise start a *second*, unelevated copy.

`schtasks.exe` rather than the Task Scheduler COM API — one documented command line against several interfaces and a great deal of boilerplate. It runs with `CREATE_NO_WINDOW`, and with a bounded wait because it is called from the UI thread: a wedged `schtasks` must not take the tray menu with it.

Both `[Run]` entries that start the utility use `shellexec`. Inno's default is `CreateProcess`, which **refuses** to start an executable carrying the `RUNASADMIN` compatibility layer — it fails with 740, `ERROR_ELEVATION_REQUIRED`, because elevation is the shell's job, and it fails even when Setup is already elevated. v0.7.1 set that layer without changing these entries, so installation ended in that error.

**The `RUNASADMIN` compatibility layer was removed.** It made every launch elevate rather than only the one the logon task starts, which closed a real trap — but it is the most malware-like thing the installer did (silent elevation persistence is a standard technique), and antivirus was deleting the installer. Weighed against being installable at all, a compatibility layer that only covers manual launches loses. The tray's **Restart as administrator** covers that case, and the logon task still starts elevated.

Removing it also emptied the separate "run as administrator" task: its only other effect was choosing the autostart mechanism, so ticking it while leaving autostart off did nothing. Each installer variant now offers only the autostart its privileges support.

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

## Diagnostic log

`%LocalAppData%\ImeModePersistence\log.txt`, opened from the tray menu.

Everything this utility does happens to *other* processes and leaves nothing behind: when a language switch has no effect there is no error to read. Every diagnosis during development went through asking the user to hover the tray icon and describe what they saw, which cost several release cycles — the log exists so a file can be attached instead.

**Only state changes and actions are recorded.** The observer runs twenty times a second; logging that would bury the few lines that matter. What is logged: startup configuration, each context switch with the identity that could be established and whether a rule matched, each layout attempt with its mechanism and outcome, each mode restore, giving up, and user actions from the tray.

`LocalAppData` rather than the install directory, because Program Files is not writable by an unelevated copy, and `LocalAppData` resolves to the same user whether elevated or not — so both write to one file. Opened with `FILE_SHARE_READ` so it can be read in Notepad while the utility runs, with a UTF-8 BOM so Notepad does not have to guess the encoding of a non-ASCII path or window class. Flushed every line, since a log that loses its tail is worthless for diagnosing a hang, and the volume is low enough for that to cost nothing.

**Size is bounded two ways, because they solve different halves.** Rotation at 1 MB into a single `.old` caps what is on disk, and the size is checked **on every write** from a running byte count rather than only at startup — the first version checked at startup only, so a copy running for weeks without a restart could grow without bound within one session. Querying the filesystem per write would cost more than the write, hence the counter.

Separately, `write_once` removes the repetition at source. A context line describes a *situation*, and the same handful of applications are switched between all day, so it is written once per distinct line — with the whole formatted message as the key, so a line that differs in any detail is still recorded. Event lines (what was attempted, whether it worked, giving up, user actions) are never deduplicated. That turns thousands of lines a day into a few dozen while keeping every fact worth diagnosing, including the "rule none" line that shows a binding is not matching. A rotation clears the set, so the new file is not left without its context lines.

## Tests

`cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`. Five suites, one CTest entry each; no external framework, because gtest would be more build machinery than this project has and vendoring it would mean the tests no longer compile with exactly the flags the shipped objects do. The shared sources are a static library, `ime_core`, that both the executable and the test binary link, so the code under test is the code that ships rather than a copy compiled differently -- `/utf-8` in particular is `PUBLIC` on that target, without which the test asserting the log holds UTF-8 Chinese would agree with the bug instead of catching it.

**What no test here can reach.** The failure that matters is a call that succeeds while the target does not change, in a raw-input fullscreen game running elevated under anti-cheat. Nothing on a developer machine or a CI runner reproduces that target, and pretending otherwise is how the wrong conclusion got written down in the first place. The instrument for it is the diagnostic log, which is why each attempt records whether the call was *issued* or *refused*.

**What is worth testing is the decision, not the mechanism.** v0.7.1 added a branch sending targets whose executable could not be read straight to TSF, on the inference that TSF was what reached them. The inference was wrong and the branch silently disabled layout binding for exactly the applications the feature exists for; it survived a release because nothing stated the invariant it broke. The escalation order now lives in `layout::method_for_attempt`, a pure function, and `tests/test_layout.cpp` asserts that attempt 0 is a window message and that nothing about the target changes where the sequence starts. That test would have failed the day the branch was added.

The general lesson, and the reason this section exists: the bug was not a coding error but a claim recorded as a finding. A test is the cheapest place to write down a claim in a form that has to keep being true.

**Testability cost paid in production code.** Two seams had to be added, both narrow and both documented where they are declared. `rules::set_storage_key` exists because proving the lookup precedence requires writing real rules, and against the default key that would destroy the rules of whoever ran the suite. `diag::Options` carries the log folder and the size limit so rotation can be exercised in a scratch directory without writing a megabyte. Neither is referenced by the application, which uses the defaults.

**Skipped is not passed.** The two window suites need a real window and a real keyboard layout. `layout-switch` first checks whether the session can change a thread's layout at all using plain `ActivateKeyboardLayout`, before anything in `layout.cpp` is involved: without that baseline, a failure on a headless runner would be indistinguishable between "the mechanism is broken" and "there is no interactive desktop" -- the same ambiguity that let the real bug survive. When the environment cannot support a suite it exits 77, CTest's `SKIP_RETURN_CODE`, and the run reports it as skipped. A green run that tested nothing is worse than a red one.

TSF is deliberately not exercised. It moves the input language for the whole session rather than one thread, so a test would change state outside the process -- and the field evidence is already that it is not the mechanism that matters.

## Security boundaries

- No global `WH_CALLWNDPROC` DLL injection.
- UIPI stops any process from changing IME state at a higher integrity level. Run the utility at the same integrity level as the target application when that matters.
- No attempt is made to bypass Windows security boundaries, and the utility never requests elevation.
