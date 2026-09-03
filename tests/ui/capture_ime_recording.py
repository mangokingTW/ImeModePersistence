"""Continuous 60 FPS real-time desktop recorder for ImeModePersistence demo.

Uses independent thread test windows (matching real multi-app scenarios) so that
Windows provides isolated per-thread IME contexts, allowing ImeModePersistence
to actively demonstrate cross-window Chinese/Alphanumeric persistence.

Output:
  - ime-recording.mp4: 60 FPS recording, with the keyboard HUD, the pointer and
    click markers drawn in. docs/demo.webp and packaging/store/store-preview.mp4
    are derived from this file; it is the only thing the script writes.

Usage:
    python tests/ui/capture_ime_recording.py [exe] [out-dir]
"""

from __future__ import annotations

import ctypes
import os
import sys
import threading
import time
import winreg
import subprocess
from ctypes import wintypes
from wintegrate import ContinuousRecorder

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"

OUT = sys.argv[2] if len(sys.argv) > 2 else "ime-recording"

# ---------------------------------------------------------------------------
# Win32 & IME Constants
# ---------------------------------------------------------------------------
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
imm32 = ctypes.windll.imm32
kernel32 = ctypes.windll.kernel32

WS_OVERLAPPEDWINDOW = 0x00CF0000
WS_VISIBLE = 0x10000000
WS_CHILD = 0x40000000
WS_BORDER = 0x00800000
ES_MULTILINE = 0x0004
ES_AUTOVSCROLL = 0x0040

WM_DESTROY = 0x0002
WM_SETFONT = 0x0030
WM_SETFOCUS = 0x0007
WM_ACTIVATE = 0x0006
WM_IME_CONTROL = 0x0283
WM_INPUTLANGCHANGEREQUEST = 0x0050

IMC_GETCONVERSIONMODE = 0x0001
IMC_SETCONVERSIONMODE = 0x0002
IMC_GETOPENSTATUS = 0x0005
IMC_SETOPENSTATUS = 0x0006

IME_CMODE_ALPHANUMERIC = 0x0000
IME_CMODE_NATIVE = 0x0001
IME_CMODE_FULLSHAPE = 0x0008

VK_SHIFT = 0x10
KEYEVENTF_KEYUP = 0x0002

WNDPROC = ctypes.WINFUNCTYPE(
    ctypes.c_long, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
)

class WNDCLASSEXW(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.UINT),
        ("style", wintypes.UINT),
        ("lpfnWndProc", WNDPROC),
        ("cbClsExtra", ctypes.c_int),
        ("cbWndExtra", ctypes.c_int),
        ("hInstance", wintypes.HINSTANCE),
        ("hIcon", wintypes.HANDLE),
        ("hCursor", wintypes.HANDLE),
        ("hbrBackground", wintypes.HANDLE),
        ("lpszMenuName", wintypes.LPCWSTR),
        ("lpszClassName", wintypes.LPCWSTR),
        ("hIconSm", wintypes.HANDLE),
    ]

@WNDPROC
def _window_wndproc(hwnd, msg, wparam, lparam):
    if msg == WM_DESTROY:
        user32.PostQuitMessage(0)
        return 0
    if msg == WM_ACTIVATE:
        if (wparam & 0xFFFF) != 0:
            user32.SetFocus(hwnd)
    return user32.DefWindowProcW(
        wintypes.HWND(hwnd), wintypes.UINT(msg),
        wintypes.WPARAM(wparam), wintypes.LPARAM(lparam),
    )

def promote_all_tray_icons():
    """Forces Windows taskbar to promote all notification icons (including ImeModePersistence) to visible taskbar."""
    try:
        exp_key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Explorer")
        winreg.SetValueEx(exp_key, "EnableAutoTray", 0, winreg.REG_DWORD, 0)
        winreg.CloseKey(exp_key)
    except Exception:
        pass
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Control Panel\NotifyIconSettings", 0, winreg.KEY_ALL_ACCESS) as notify_key:
            num_subkeys = winreg.QueryInfoKey(notify_key)[0]
            for idx in range(num_subkeys):
                try:
                    subkey_name = winreg.EnumKey(notify_key, idx)
                    with winreg.OpenKey(notify_key, subkey_name, 0, winreg.KEY_SET_VALUE) as subkey:
                        winreg.SetValueEx(subkey, "IsPromoted", 0, winreg.REG_DWORD, 1)
                except Exception:
                    pass
    except Exception:
        pass
    HWND_BROADCAST = 0xFFFF
    WM_SETTINGCHANGE = 0x001A
    user32.PostMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 0)

def _line_break_count(text: str) -> int:
    """Number of line breaks in text, whatever form they take.

    Notepad returns bare carriage returns, so counting "\\n" finds none.
    splitlines() is wrong too: a trailing break adds no element, and a
    trailing break is what Enter at the end of a document produces.
    """
    return text.replace("\r\n", "\n").replace("\r", "\n").count("\n")


def _foreground_window_description() -> str:
    """Class, title and owning pid of the foreground window, for diagnostics.

    A hardware keystroke goes to whatever holds focus, so when one appears not
    to arrive, the first question is which window actually had it.
    """
    try:
        hwnd = user32.GetForegroundWindow()
        if not hwnd:
            return "<none>"
        title = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(hwnd, title, 512)
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cls, 256)
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        pos = wintypes.POINT()
        user32.GetCursorPos(ctypes.byref(pos))
        return (f"hwnd={hwnd} class={cls.value} title={title.value!r} "
                f"pid={pid.value} cursor=({pos.x},{pos.y})")
    except Exception as exc:
        return f"<unavailable: {exc}>"


_closed_terminals = set()


def minimize_background_windows():
    """Clears the desktop of windows that could take focus from the test.

    Console windows are minimized. A Windows Terminal running wsl.exe is
    closed instead: minimizing loses to a window that reopens itself, and on
    the ARM64 runner that one does.

    Matched on the title, never the class alone -- on windows-latest the
    Actions runner is itself hosted in a Windows Terminal, and closing that
    kills the job. Missing a stray window is recoverable; closing the runner's
    is not.
    """
    SW_MINIMIZE = 6
    WM_CLOSE = 0x0010
    kernel32 = ctypes.windll.kernel32
    user32 = ctypes.windll.user32

    # Minimize own console window if exists
    console_hwnd = kernel32.GetConsoleWindow()
    if console_hwnd:
        user32.ShowWindow(console_hwnd, SW_MINIMIZE)

    # Minimize other console / terminal windows
    def enum_proc(hwnd, lparam):
        if user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            title = buf.value.lower()
            class_buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, class_buf, 256)
            cls = class_buf.value
            if cls == "CASCADIA_HOSTING_WINDOW_CLASS" and "wsl" in title and hwnd != console_hwnd:
                # Posted, not sent: a hung terminal must not block the test.
                user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
                if hwnd not in _closed_terminals:
                    _closed_terminals.add(hwnd)
                    print(f"[DESKTOP] closed stray terminal hwnd={hwnd} title={buf.value!r}", flush=True)
            elif any(k in title for k in ["cmd", "powershell", "host", "terminal", "github"]) or \
                    cls in ("ConsoleWindowClass", "CASCADIA_HOSTING_WINDOW_CLASS"):
                user32.ShowWindow(hwnd, SW_MINIMIZE)
        return True

    WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(WNDENUMPROC(enum_proc), 0)
    time.sleep(0.5)


def dismiss_stray_overlays():
    """Closes the Start menu (or similar shell overlay) if one is open.

    Observed on an ARM64 runner: the Start menu ended up open and sitting on
    top of a freshly launched Notepad window, which is why connecting to it
    by process id couldn't find it -- the window existed, it was just obscured
    by a topmost shell surface, the same class of problem as the Shell_OOBEProxy
    screen. Escape reliably closes the Start menu (and most other transient
    shell overlays) without needing to identify the exact window involved.
    """
    VK_ESCAPE = 0x1B
    KEYEVENTF_KEYUP = 0x0002
    user32.keybd_event(VK_ESCAPE, 0, 0, 0)
    user32.keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.3)

class NotepadWindow:
    """Manages a genuine Windows Notepad process with full Microsoft TSF IME candidate window support."""

    def __init__(self, x: int, y: int, w: int, h: int):

        from wintegrate import Window

        # Launch and window discovery are one call. It diffs a window census
        # taken before and after the launch, which is what makes it correct
        # here: notepad.exe on Windows 11 build 26100+ starts the modern
        # tabbed Notepad, so the process Popen returns is not the one that
        # ends up owning the visible window and connecting by PID finds
        # nothing while the window is plainly on screen. The same diff is what
        # stops the second instance (win_b) latching onto win_a's window,
        # since win_a's is already present in the "before" snapshot.
        #
        # Three criteria, any of which is enough, ordered by how little they
        # depend on the machine's language:
        #   window_classes  the modern tabbed Notepad still reports class
        #                   "Notepad" (confirmed in this repo's own ARM64
        #                   desktop census: class=Notepad title="*測試 - Notepad")
        #   process_names   locale-independent too, but returns "" whenever
        #                   OpenProcess is refused, which packaged apps can do
        #   title_pattern   last resort, and the only one that has to know
        #                   what Notepad is called in the runner's language
        self.proc, self.win = Window.launch_and_discover(
            ["notepad.exe"],
            timeout=15.0,
            window_classes=("Notepad",),
            process_names=("notepad.exe",),
            title_pattern=r"Notepad|記事本|메모장|メモ帳",
        )
        self.hwnd = self.win.hwnd

        # Move and resize window
        user32.MoveWindow(self.hwnd, x, y, w, h, True)
        self.set_foreground()

    def set_foreground(self):
        # A stray window (observed on an ARM64 runner: a WSL console) can pop
        # up mid-sequence and steal focus, not just at startup -- re-minimize
        # background console/terminal windows before every focus change, not
        # only once before the recording begins.
        minimize_background_windows()
        user32.keybd_event(0, 0, 0, 0)
        # set_foreground verifies the window actually reached the foreground
        # rather than assuming SetForegroundWindow succeeded; the click that
        # follows puts the caret in the text area so the IME has somewhere to
        # compose into.
        self.win.set_foreground()
        time.sleep(0.2)
        try:
            self.text_input().click()
        except Exception:
            pass
        time.sleep(0.3)

    def text_input(self):
        """The Notepad text area, resolved fresh each time.

        find_text_input walks a locale-independent ladder of window classes and
        UIA control types, which replaces the hand-rolled Edit-then-Document
        fallback: the modern tabbed Notepad exposes a Document where the
        classic one exposed an Edit, and neither name is stable across
        Windows builds.
        """
        return self.win.find_text_input(timeout=10.0)

    def type_text(self, text: str, delay_per_char: float = 0.04):
        self.set_foreground()
        if text == "\n":
            self.press_enter()
        else:
            # Only reached for non-IME text; the bopomofo path goes through
            # type_bopomofo, which must stay on physical scan codes.
            self.text_input().type_verified(text, verify_contains=text, delay_per_char=delay_per_char)
        time.sleep(0.3)

    def _enter(self):
        """Presses Enter as a scan-code keystroke.

        send_keys("{ENTER}") sends VK_RETURN without a scan code and inserts
        no line break here. send_char_input sends it with scan code 0x1C.
        """
        from wintegrate import send_char_input

        self.text_input().set_focus(verify=False)
        time.sleep(0.05)
        send_char_input("\n")

    def press_enter(self, attempts: int = 3):
        """Adds a line break, confirming it actually landed.

        A hardware keystroke goes wherever the focus is, not to a window we
        name, so on ARM64 it sometimes misses. Send it, read the text back,
        send it again if the line count did not move.

        Retrying is safe: a duplicate Enter adds a blank line and empty lines
        are stripped before comparing. It is not free -- each attempt clicks
        again -- so the count stays small. Failed attempts log what was read,
        because "the keystroke was lost" and "the check cannot see it" look
        identical otherwise.
        """
        before_text = self.get_text()
        before = _line_break_count(before_text)
        for attempt in range(attempts):
            self.set_foreground()
            self._enter()
            time.sleep(0.4)
            after_text = self.get_text()
            if _line_break_count(after_text) > before:
                return
            print(
                f"[RETRY] Enter did not register (attempt {attempt + 1}/{attempts}); "
                f"read before={before_text!r} after={after_text!r} "
                f"foreground={_foreground_window_description()}",
                flush=True,
            )
        # Deliberately not raising: the content verification later on reports the
        # exact expected/actual text, which says far more than an exception here,
        # and the recording still has to be saved either way.
        print("[RETRY] giving up on the line break; content verification will report it", flush=True)

    def type_bopomofo(self, key_sequence: str):
        """Types bopomofo keys as physical scan codes, so the IME composes them.

        Scan codes are required: Unicode injection hands the codepoint
        straight to the control, so composition never starts and the IME --
        the thing under test -- is bypassed.
        """
        self.set_foreground()
        time.sleep(0.3)

        # One uninterrupted sequence: pausing mid-way to read IME state broke
        # composition, and could not have read it anyway (this Notepad is a
        # XAML control on TSF, so IMM32 reports no context). The content
        # assertion is the evidence here -- 測試 means composed, hk4g4 means
        # raw letters.
        self.text_input().send_physical_keys(key_sequence, delay_per_key=0.1)
        time.sleep(0.3)
        self._enter()
        time.sleep(0.4)

    def type_english(self, text: str, interval: float = 0.08):
        """Types raw English keys as scan codes, to demonstrate direct alphanumeric input.

        Scan codes rather than Unicode injection for the same reason as
        type_bopomofo: this is meant to exercise the real keyboard path, with
        the IME in alphanumeric mode, not to shortcut around it.
        """
        self.set_foreground()
        time.sleep(0.3)
        self.text_input().send_physical_keys(text, delay_per_key=interval)
        time.sleep(0.3)

    def set_chinese(self):
        self.set_foreground()
        hkl = user32.LoadKeyboardLayoutW("00000404", 1)
        if hkl:
            user32.ActivateKeyboardLayout(hkl, 0)
            user32.SendMessageW(self.hwnd, WM_INPUTLANGCHANGEREQUEST, 0, hkl)

        # 1. Focus child targeting via GetGUIThreadInfo (identical to ImeModePersistence C++ engine)
        class GUITHREADINFO(ctypes.Structure):
            _fields_ = [
                ("cbSize", wintypes.DWORD),
                ("flags", wintypes.DWORD),
                ("hwndActive", wintypes.HWND),
                ("hwndFocus", wintypes.HWND),
                ("hwndCapture", wintypes.HWND),
                ("hwndMenuOwner", wintypes.HWND),
                ("hwndMoveSize", wintypes.HWND),
                ("hwndCaret", wintypes.HWND),
                ("rcCaret", wintypes.RECT),
            ]

        thread_id = user32.GetWindowThreadProcessId(self.hwnd, None)
        gti = GUITHREADINFO()
        gti.cbSize = ctypes.sizeof(GUITHREADINFO)
        targets = [self.hwnd]
        if user32.GetGUIThreadInfo(thread_id, ctypes.byref(gti)) and gti.hwndFocus:
            if gti.hwndFocus not in targets:
                targets.insert(0, gti.hwndFocus)

        for target in targets:
            ime_wnd = imm32.ImmGetDefaultIMEWnd(target)
            if ime_wnd:
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 1)
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, 1)  # IME_CMODE_NATIVE (1)
        time.sleep(0.3)

    def set_alphanumeric(self):
        self.set_foreground()
        class GUITHREADINFO(ctypes.Structure):
            _fields_ = [
                ("cbSize", wintypes.DWORD),
                ("flags", wintypes.DWORD),
                ("hwndActive", wintypes.HWND),
                ("hwndFocus", wintypes.HWND),
                ("hwndCapture", wintypes.HWND),
                ("hwndMenuOwner", wintypes.HWND),
                ("hwndMoveSize", wintypes.HWND),
                ("hwndCaret", wintypes.HWND),
                ("rcCaret", wintypes.RECT),
            ]

        thread_id = user32.GetWindowThreadProcessId(self.hwnd, None)
        gti = GUITHREADINFO()
        gti.cbSize = ctypes.sizeof(GUITHREADINFO)
        targets = [self.hwnd]
        if user32.GetGUIThreadInfo(thread_id, ctypes.byref(gti)) and gti.hwndFocus:
            if gti.hwndFocus not in targets:
                targets.insert(0, gti.hwndFocus)

        for target in targets:
            ime_wnd = imm32.ImmGetDefaultIMEWnd(target)
            if ime_wnd:
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 0)
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, 0)  # IME_CMODE_ALPHANUMERIC (0)
        time.sleep(0.3)

    def get_text(self) -> str:
        """Reads the text out of the Notepad document.

        get_value() chains TextPattern, ValuePattern and WM_GETTEXT
        internally, which replaces the four-branch Edit/Document ladder this
        used to carry -- the modern tabbed Notepad answers on a different one
        of those than the classic control did.
        """
        try:
            return self.text_input().get_value() or ""
        except Exception:
            return ""

    def close(self):
        # Close the actual window first -- with the modern tabbed Notepad the
        # process launched by Popen may not be the one that ends up owning
        # it, so terminating self.proc alone can leave the real window (and
        # its owning process) running.
        try:
            self.win.close()
        except Exception:
            pass
        try:
            self.proc.terminate()
            self.proc.wait(timeout=2)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass

def main() -> int:
    os.makedirs(OUT, exist_ok=True)

    # Configure taskbar to always show all notification tray icons directly (EnableAutoTray = 0 and NotifyIconSettings IsPromoted = 1)
    try:
        exp_key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Explorer")
        winreg.SetValueEx(exp_key, "EnableAutoTray", 0, winreg.REG_DWORD, 0)
        winreg.CloseKey(exp_key)
    except Exception:
        pass

    try:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Control Panel\NotifyIconSettings") as notify_key:
            num_subkeys = winreg.QueryInfoKey(notify_key)[0]
            for idx in range(num_subkeys):
                try:
                    subkey_name = winreg.EnumKey(notify_key, idx)
                    with winreg.OpenKey(notify_key, subkey_name, 0, winreg.KEY_SET_VALUE) as subkey:
                        winreg.SetValueEx(subkey, "IsPromoted", 0, winreg.REG_DWORD, 1)
                except Exception:
                    pass
    except Exception:
        pass

    # Enable PersistMode and ShowCaretIndicator (Caret / Cursor indicator) in registry
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\ImeModePersistence")
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, 2)
    winreg.SetValueEx(key, "PersistMode", 0, winreg.REG_DWORD, 1)
    winreg.SetValueEx(key, "ShowCaretIndicator", 0, winreg.REG_DWORD, 1)
    winreg.CloseKey(key)

    # Configure Microsoft Bopomofo default mode to Chinese ('中 ㄅ') and enable Shift switching & compatibility mode
    for path in [
        r"Software\Microsoft\IME\15.0\IMETC",
        r"Software\Microsoft\InputMethod\Settings\CHT",
        r"Software\Microsoft\InputMethod\Settings\IMETC",
    ]:
        try:
            ime_key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, path)
            winreg.SetValueEx(ime_key, "Default Input Mode", 0, winreg.REG_DWORD, 1)
            winreg.SetValueEx(ime_key, "Left Shift Usage", 0, winreg.REG_DWORD, 1)
            winreg.SetValueEx(ime_key, "Right Shift Usage", 0, winreg.REG_DWORD, 1)
            winreg.SetValueEx(ime_key, "Enable Compatibility Mode", 0, winreg.REG_DWORD, 1)
            winreg.CloseKey(ime_key)
        except Exception:
            pass

    # Activate zh-TW layout as primary in the recording process
    hkl_tw = user32.LoadKeyboardLayoutW("00000404", 1)
    if hkl_tw:
        user32.ActivateKeyboardLayout(hkl_tw, 0)

    subprocess.run(["taskkill", "/F", "/IM", "ImeModePersistence.exe"], capture_output=True)

    time.sleep(0.3)

    # Minimize all host / terminal / console windows before launching recording
    minimize_background_windows()
    dismiss_stray_overlays()

    engine = subprocess.Popen([EXE])
    time.sleep(1.0)
    promote_all_tray_icons()
    time.sleep(0.5)
    dismiss_stray_overlays()

    win_a = None
    win_b = None
    match_a = None
    match_b = None
    video_error = None
    expected_a = "測試\npersistence test"
    expected_b = "測試\nhello world"
    norm_a = ""
    norm_b = ""

    mp4_path = os.path.join(OUT, "ime-recording.mp4")
    # The overlays are stated rather than left to wintegrate's defaults, because
    # this recording is the product demo: the keyboard HUD is what makes the
    # bopomofo keystrokes visible, so a future default change must not silently
    # take it away. All three are composited after the screen grab, so nothing on
    # the desktop can cover them and no cursor has to exist in the capture.
    recorder = ContinuousRecorder(
        mp4_path,
        fps=60,
        draw_cursor=True,
        click_markers=True,
        key_hud=True,
    )
    print("Starting continuous real-time desktop recording (60 FPS)...")
    recorder.start()

    try:
        win_a = NotepadWindow(x=40, y=80, w=480, h=360)
        win_b = NotepadWindow(x=550, y=80, w=480, h=360)
        time.sleep(0.5)

        # Step 1: Window A activated, set Chinese mode, type authentic bopomofo 測試
        win_a.set_foreground()
        win_a.set_chinese()
        time.sleep(0.5)
        # 敲擊整組注音詞彙打出「測試」（hk4g4 = ㄘㄜˋㄕˋ ➔ 測試）
        win_a.type_bopomofo("hk4g4")
        win_a.type_text("\n")
        time.sleep(1.8)  # Dwell to let engine adopt Chinese mode

        # Step 2: Switch to Window B -> Engine automatically maintains Chinese mode
        win_b.set_foreground()
        time.sleep(0.8)
        win_b.type_bopomofo("hk4g4")
        win_b.type_text("\n")
        time.sleep(2.0)

        # Step 3: Switch to English mode in Window B -> Type English test
        win_b.set_alphanumeric()
        time.sleep(0.5)
        win_b.type_english("hello world")
        win_b.type_text("\n")
        time.sleep(1.8)  # Dwell to let engine adopt Alphanumeric mode

        # Step 4: Switch back to Window A -> Engine restores English mode -> Type English test
        win_a.set_foreground()
        time.sleep(0.8)
        win_a.type_english("persistence test")
        win_a.type_text("\n")
        time.sleep(2.0)

        # Check actual text contents of Window A and Window B
        text_a = win_a.get_text()
        text_b = win_b.get_text()
        print(f"\n==================== [CONTENT VERIFICATION] ====================", flush=True)
        print(f"--- Window A Content ---\n{text_a}", flush=True)
        print(f"--- Window B Content ---\n{text_b}", flush=True)
        print(f"=================================================================\n", flush=True)

        norm_a = "\n".join([line.strip() for line in text_a.strip().splitlines() if line.strip()])
        norm_b = "\n".join([line.strip() for line in text_b.strip().splitlines() if line.strip()])

        print(f"[VERIFY] Window A exact content matching:\nExpected:\n{expected_a}\nActual:\n{norm_a}\n", flush=True)
        print(f"[VERIFY] Window B exact content matching:\nExpected:\n{expected_b}\nActual:\n{norm_b}\n", flush=True)

        # Content is checked here but not asserted yet -- the recording below must
        # be saved regardless of the outcome, or a mismatch destroys the one
        # artifact that would explain why. The mismatch (if any) still fails the
        # run; it is just deferred past the save so the video always comes out.
        match_a = norm_a == expected_a
        match_b = norm_b == expected_b
        if match_a and match_b:
            print("[VERIFY] All window text contents strictly and perfectly matched without trailing spaces!", flush=True)
        else:
            if not match_a:
                print(f"[VERIFY] Window A text mismatch! Expected '{expected_a}', got '{norm_a}'", flush=True)
            if not match_b:
                print(f"[VERIFY] Window B text mismatch! Expected '{expected_b}', got '{norm_b}'", flush=True)

    finally:
        # Stopping the recorder and saving the video happen here, in finally,
        # so a crash at any point above (Notepad never getting a window, a
        # UIA call blowing up, anything) still produces a video of exactly
        # what was on screen -- the one artifact that actually explains what
        # happened, instead of losing it to whatever exception is about to
        # propagate.
        # backend is a property, and it reports None once stop() has closed the
        # encoder -- so it has to be read first, or it always says None.
        backend = recorder.backend
        frames = recorder.stop()
        # stop() writes the file itself, so there is no in-memory frame list to
        # encode here any more -- and no way to run out of memory doing it,
        # which is what used to break long ARM64 captures.
        size = os.path.getsize(mp4_path) if os.path.exists(mp4_path) else 0
        if size > 0:
            print(f"Saved 60 FPS MP4 video via {backend}: {mp4_path} "
                  f"({size} bytes, {frames} frames)", flush=True)
        else:
            video_error = f"no video written to {mp4_path} (backend={backend})"
            print(f"Recording error: {video_error}", flush=True)

        if win_a is not None:
            win_a.close()
        if win_b is not None:
            win_b.close()
        engine.terminate()
        try:
            engine.wait(timeout=2)
        except Exception:
            engine.kill()

    assert match_a, f"Window A text mismatch! Expected '{expected_a}', got '{norm_a}'"
    assert match_b, f"Window B text mismatch! Expected '{expected_b}', got '{norm_b}'"
    # Asserted last, and only after the content checks, because a mismatch says
    # more about what went wrong than a missing file does. But it is asserted:
    # this job exists to produce a recording, and it spent three ARM64 runs
    # reporting success while writing no video at all.
    assert video_error is None, f"recording was not produced -- {video_error}"

    return 0

if __name__ == "__main__":
    sys.exit(main())
