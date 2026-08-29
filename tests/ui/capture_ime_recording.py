"""Continuous 60 FPS real-time desktop recorder for ImeModePersistence demo.

Uses independent thread test windows (matching real multi-app scenarios) so that
Windows provides isolated per-thread IME contexts, allowing ImeModePersistence
to actively demonstrate cross-window Chinese/Alphanumeric persistence.

Outputs:
  - ime-recording.webp: Native 60 FPS video recording.
  - ime-recording.gif: Smooth 30 FPS GIF animation.
  - ime-frame-*.png: Key step static screenshots.

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
from PIL import Image

try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

try:
    # pydirectinput aborts if the pointer reaches a screen corner, so a human
    # can stop a script that has taken over their mouse. There is no human on a
    # CI runner, and the abort is not harmless: once tripped, every later
    # keystroke raises, so a single mislanded click kills the whole capture --
    # which is exactly how an ARM64 run died, in type_english, long after the
    # click that moved the pointer. Losing a keystroke is recoverable; losing
    # the run is not. The corner position is logged instead, since a pointer in
    # a corner still means a click went somewhere it should not have.
    import pydirectinput as _pydirectinput

    _pydirectinput.FAILSAFE = False
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
    """Clears the desktop of windows that could take focus away from the test.

    Console windows are minimized. Windows Terminal windows are *closed*
    instead, because minimizing them has been shown not to be enough: the
    ARM64 runner's interactive session sometimes has a Windows Terminal open
    on `C:\\Windows\\system32\\wsl.exe`, and the one release run where the
    typed line break went missing is the one run where that window was
    present -- every other visible window was identical between the two runs.
    Terminal was already being minimized there, so the likeliest explanation
    is that it came back to the foreground on its own (wsl.exe has no distro
    to run on this image, so it errors and exits) at the moment a hardware
    keystroke was in flight. A minimized window can return; a closed one
    cannot.

    Only a Windows Terminal whose title names wsl.exe is closed, and that
    narrowness is not caution for its own sake. On windows-latest the Actions
    runner *itself* is hosted in a Windows Terminal window, titled "Default";
    closing it on class alone made the runner log "received a shutdown signal"
    on the very next line and killed the job. Matching the title is what
    separates the ARM64 image's stray `C:\\Windows\\system32\\wsl.exe` window
    from the window this job is running inside.

    If the offending window ever appears under some other title this will miss
    it -- which is why press_enter re-sends the keystroke rather than relying
    on the desktop being clean. Failing to close is recoverable; closing the
    wrong window is not.
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
    top of a freshly launched Notepad window, which is why pywinauto's
    top_window() couldn't find it -- the window existed, it was just obscured
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

        from pywinauto import Desktop
        from pywinauto.application import Application

        # Connecting by PID (Application(...).connect(process=pid)) assumes
        # the process launched by Popen is the one that ends up owning the
        # window. That broke on a newer Windows build (observed on an ARM64
        # runner, Windows 11 build 26100+) where notepad.exe launches the
        # modern tabbed Notepad -- the window is genuinely there (confirmed
        # visually in the recording, not obscured by anything), but pywinauto
        # could never find it by that PID. Find its handle by title instead,
        # which works regardless of which process ends up owning the window:
        # launch, then poll for a new top-level "Notepad" window that wasn't
        # already open before (so a second instance -- win_b -- doesn't just
        # grab win_a's window).
        #
        # Desktop(...).windows() hands back already-resolved wrapper objects,
        # not a self-re-resolving WindowSpecification -- holding one of those
        # for the whole lifetime of this object (repeated set_focus/type_keys
        # calls over several seconds, while other windows get focus in
        # between) let it go stale silently: later calls stopped raising but
        # also stopped doing anything, so text just never landed. Use the
        # discovered handle to reconnect via Application(...).connect(handle=)
        # instead, and keep app.window(handle=) (which re-resolves on every
        # call) as self.dlg for everything from here on.
        try:
            existing_handles = {w.handle for w in Desktop(backend="uia").windows(title_re=".*Notepad.*")}
        except Exception:
            existing_handles = set()

        self.proc = subprocess.Popen(["notepad.exe"])
        deadline = time.monotonic() + 10.0
        last_error = None
        found_handle = None
        while time.monotonic() < deadline:
            try:
                for win in Desktop(backend="uia").windows(title_re=".*Notepad.*"):
                    if win.handle not in existing_handles:
                        found_handle = win.handle
                        break
                if found_handle is not None:
                    break
            except Exception as exc:
                last_error = exc
            time.sleep(0.2)
        if found_handle is None:
            raise RuntimeError(f"Notepad (pid {self.proc.pid}) never got a window") from last_error

        self.hwnd = found_handle
        self.app = Application(backend="uia").connect(handle=self.hwnd)
        self.dlg = self.app.window(handle=self.hwnd)

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
        self.dlg.set_focus()
        time.sleep(0.2)
        try:
            edit = self.dlg.child_window(control_type="Edit")
            edit.click_input()
        except Exception:
            try:
                doc = self.dlg.child_window(control_type="Document")
                doc.click_input()
            except Exception:
                pass
        time.sleep(0.3)

    def type_text(self, text: str, delay_per_char: float = 0.04):
        self.set_foreground()
        if text == "\n":
            self.press_enter()
        else:
            self.dlg.type_keys(text, with_spaces=True, with_newlines=True, pause=delay_per_char)
        time.sleep(0.3)

    def press_enter(self, attempts: int = 3):
        """Adds a line break, confirming it actually landed.

        pywinauto's type_keys("\\n", with_newlines=True) sends a WM_CHAR
        carriage return, which the modern tabbed Notepad's document control
        doesn't turn into a visible line break the way the classic Edit control
        did -- the two typed segments land correctly but with no separator
        between them. A raw hardware-level Enter (the same mechanism
        type_bopomofo relies on) does work.

        But a hardware keystroke goes wherever the focus is, not to a window we
        name, so it is only as reliable as the focus was at that instant. On the
        ARM64 runner that has been observed to miss: in one run window A's
        newline landed and window B's did not, from identical code moments
        apart. So rather than sending it and hoping, read the text back and
        send it again if the line count did not move.

        Retrying is safe for the content check: a duplicate Enter only adds a
        blank line, and empty lines are stripped before comparing. It is not
        free otherwise -- each attempt re-runs set_foreground(), which clicks,
        and on the ARM64 runner enough of those walked the pointer into a
        screen corner and tripped pydirectinput's fail-safe, which then makes
        every later keystroke raise. Hence few attempts, and the fail-safe
        turned off in this script (see the top of the file).

        Each failed attempt prints what was actually read back. The first
        version of this only printed "did not register", and an ARM64 run then
        reported that fifteen times while the recording plainly showed text
        going into Notepad and the window title read "*測試 - Notepad" -- so
        the check itself may be the thing that is wrong, not the keystroke. Not
        being able to tell those apart is what this logging is for.
        """
        import pydirectinput

        before_text = self.get_text()
        before = before_text.count("\n")
        for attempt in range(attempts):
            self.set_foreground()
            pydirectinput.press('enter')
            time.sleep(0.4)
            after_text = self.get_text()
            if after_text.count("\n") > before:
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
        """Types authentic bopomofo keys via pydirectinput DirectX hardware scan codes."""
        import pydirectinput

        self.set_foreground()
        time.sleep(0.3)
        pydirectinput.write(key_sequence, interval=0.1)
        time.sleep(0.3)
        pydirectinput.press('enter')
        time.sleep(0.4)

    def type_english(self, text: str, interval: float = 0.08):
        """Types raw English keys via pydirectinput to demonstrate direct alphanumeric input."""
        import pydirectinput
        self.set_foreground()
        time.sleep(0.3)
        pydirectinput.write(text, interval=interval)
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
        """Reads text from Notepad control. Tries the classic Edit control
        first, then Document (the modern tabbed Notepad's text area reports
        as a Document control type under UIA)."""
        try:
            edit = self.dlg.child_window(control_type="Edit")
            val = edit.get_value()
            if val:
                return val
        except Exception:
            pass
        try:
            edit = self.dlg.child_window(class_name="Edit")
            return edit.window_text()
        except Exception:
            pass
        try:
            doc = self.dlg.child_window(control_type="Document")
            val = doc.get_value()
            if val:
                return val
        except Exception:
            pass
        try:
            doc = self.dlg.child_window(control_type="Document")
            return doc.window_text()
        except Exception:
            return ""

    def close(self):
        # Close the actual window first -- with the modern tabbed Notepad the
        # process launched by Popen may not be the one that ends up owning
        # it, so terminating self.proc alone can leave the real window (and
        # its owning process) running.
        try:
            self.dlg.close()
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

def grab_real_screen() -> Image.Image:
    w = user32.GetSystemMetrics(0)
    h = user32.GetSystemMetrics(1)
    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    gdi32.SelectObject(hdc_mem, hbmp)
    gdi32.BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, 0, 0, 0x00CC0020)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
            ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
            ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
            ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", ctypes.c_long),
            ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wintypes.DWORD),
            ("biClrImportant", wintypes.DWORD),
        ]

    bmi = BITMAPINFOHEADER()
    bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.biWidth = w
    bmi.biHeight = -h
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    bmi.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc_mem, hbmp, 0, h, buf, ctypes.byref(bmi), 0)

    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(0, hdc_screen)
    gdi32.DeleteObject(hbmp)

    return Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")

class ContinuousRecorder:
    def __init__(self, fps: int = 60):
        self.interval = 1.0 / fps
        self.frames: list[Image.Image] = []
        self.stop_event = threading.Event()
        self.thread: threading.Thread | None = None

    def start(self):
        self.frames.clear()
        self.stop_event.clear()
        self.thread = threading.Thread(target=self._record_loop, daemon=True)
        self.thread.start()

    def _record_loop(self):
        while not self.stop_event.is_set():
            t0 = time.monotonic()
            try:
                self.frames.append(grab_real_screen())
            except Exception:
                pass
            elapsed = time.monotonic() - t0
            sleep_time = max(0.001, self.interval - elapsed)
            time.sleep(sleep_time)

    def stop(self) -> list[Image.Image]:
        self.stop_event.set()
        if self.thread:
            self.thread.join(timeout=3)
        return self.frames

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
    expected_a = "測試\npersistence test"
    expected_b = "測試\nhello world"
    norm_a = ""
    norm_b = ""

    recorder = ContinuousRecorder(fps=60)
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
        all_frames = recorder.stop()
        print(f"Recording finished! Total frames captured: {len(all_frames)}")

        # Save pristine 60 FPS H.264 MP4 video only (no screenshots)
        if len(all_frames) > 0:
            mp4_path = os.path.join(OUT, "ime-recording.mp4")
            # H.264 requires even width and height
            w = all_frames[0].width - (all_frames[0].width % 2)
            h = all_frames[0].height - (all_frames[0].height % 2)

            # imageio_ffmpeg bundles a prebuilt binary for common platforms, but
            # on one it doesn't cover (observed on Windows ARM64) get_ffmpeg_exe()
            # returns a path with nothing there instead of raising -- so the
            # except below never fires and Popen dies with a raw WinError 2.
            # Checking the path actually exists catches that case too, falling
            # back to a system "ffmpeg" on PATH.
            try:
                import imageio_ffmpeg
                ffmpeg_bin = imageio_ffmpeg.get_ffmpeg_exe()
                if not os.path.isfile(ffmpeg_bin):
                    raise FileNotFoundError(ffmpeg_bin)
            except Exception:
                ffmpeg_bin = "ffmpeg"

            ffmpeg_cmd = [
                ffmpeg_bin, "-y",
                "-f", "rawvideo",
                "-vcodec", "rawvideo",
                "-s", f"{w}x{h}",
                "-pix_fmt", "rgb24",
                "-r", "60",
                "-i", "-",
                "-c:v", "libx264",
                "-pix_fmt", "yuv420p",
                "-preset", "fast",
                "-crf", "18",
                mp4_path,
            ]
            try:
                proc = subprocess.Popen(
                    ffmpeg_cmd,
                    stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,
                )
                raw_bytes = b"".join(
                    (f.resize((w, h), Image.Resampling.BILINEAR) if f.size != (w, h) else f).tobytes()
                    for f in all_frames
                )
                _, stderr_data = proc.communicate(input=raw_bytes, timeout=60)
                if proc.returncode != 0:
                    print(f"FFmpeg returned error code {proc.returncode}: {stderr_data.decode('utf-8', errors='ignore')}")
                else:
                    print(f"Saved pristine 60 FPS MP4 video ({len(all_frames)} frames): {mp4_path}")
            except Exception as exc:
                print(f"FFmpeg encoding error: {exc}")

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

    return 0

if __name__ == "__main__":
    sys.exit(main())
