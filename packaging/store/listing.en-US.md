# Microsoft Store listing copy — English (en-US)

> **What this file is:** the `en-us` Store listing copy for **IME Mode Persistence**, kept under version control as the source of truth. The app already has a live `en-us` listing in Partner Center; this file records the canonical text so it can be reviewed and pasted back in when it changes.
>
> **Note:** the **Description** below is a **static listing field** that is maintained by hand in Partner Center. The per-version **"What's new" (release notes)** is not written here — it is set automatically by the release workflow from each version's `CHANGELOG.md` section.

---

## 1. Title

**IME Mode Persistence**

> (This is the Store display name, kept as the spaced form. The package / repo id is the unspaced `ImeModePersistence`; keep the two distinct — the Store shows the spaced name.)

---

## 2. Description

A small tray utility that controls how input methods behave across programs. It does two things: it carries the **input mode you last chose** (native/Chinese or alphanumeric) to the next window, and it can **bind a specific program to a fixed keyboard input language** — including programs whose executable can't be read, such as anti-cheat protected fullscreen games.

**Keep the conversion mode consistent across windows.** Windows keeps IME state **per thread**, so when you move to another window the conversion mode reverts to that IME's default — for a Chinese keyboard, that means Chinese. Press Shift for alphanumeric, switch window, and you're typing Chinese again. The setting *Let me use a different input method for each app window* (Settings → Time & language → Typing → Advanced keyboard settings) does **not** fix this: it governs *which* input method is active, not whether that method is in native or alphanumeric mode. No Windows setting covers the conversion mode. This utility does — the native/alphanumeric mode you chose follows you to the next window, and you can turn it off from the tray menu. Confirmed working with Microsoft Bopomofo on real hardware.

**Bind a program to an input language.** Pin a terminal to English and Word to Chinese, and each one stays put. Rules match by **full path** (browse for an executable, so two programs sharing a name are configured separately), by bare **file name** (`notepad.exe`, wherever it's installed), or by **window class** (`class:<name>`). Window class is the only way to reach a fullscreen game that reads raw input: it takes the keyboard directly, doesn't participate in IME state at all, and its path can't be read even by an administrator — so it can only be handled from outside. Confirmed working for anti-cheat protected fullscreen games (Helldivers 2 uses `class:stingray_window`).

**Caret input indicator.** An optional small badge beside the text caret shows what you'll actually type — `中` / `あ` / `한` (that language's native mode), `Ａ` (the IME switched to alphanumeric), or a language code such as `EN`. It's off by default, and the choice is remembered.

**Elevation.** Reading or changing the windows of an elevated program requires equal privileges. When a target needs administrator rights, the app prompts you, and **Restart as administrator** in the tray menu elevates it on the spot. An optional diagnostic log records every context switch, whether a rule matched, and which mechanism was used, for troubleshooting.

**About this Store build.** The Microsoft Store (MSIX) build **can't elevate**, so it can't control programs run as administrator or anti-cheat games. For those targets, use the **desktop build** (installer / Scoop / winget) run as administrator. **Start at logon is available** in this Store build.

Nothing is injected, hooked, or synthesized — no key simulation. Switching posts Windows' standard input-language-change notification (which a window is free to ignore) and falls back to TSF's public API; it's best-effort and verified by reading the state back. The interface follows your Windows display language, English or Traditional Chinese (Taiwan).

**Highlights**

- Carries the native/alphanumeric conversion mode you chose to the next window — the one thing no Windows setting covers.
- Per-app input-language binding by full path, file name, or window class.
- Window class binding reaches anti-cheat fullscreen games that read raw input and can't be matched any other way.
- Optional caret input indicator showing the current input state right where you type.
- Prompts for elevation and offers "Restart as administrator" when a target needs admin rights.
- Store build can't elevate (use the desktop build for admin / anti-cheat targets); "Start at logon" works in the Store build.
- No injection, hooking, or key simulation — best-effort with read-back verification.
- English and Traditional Chinese (Taiwan) interface, chosen by your Windows display language.

---

## 3. (optional) Short description / search keywords

**Short description (one line):**
Keeps your IME conversion mode (Chinese/alphanumeric) consistent across windows and binds a chosen input language per program — even anti-cheat fullscreen games.

**Suggested search keywords:**
IME, input method, IME mode, Chinese input, Bopomofo, keyboard language, input language, per-app, per-application, conversion mode, tray utility
