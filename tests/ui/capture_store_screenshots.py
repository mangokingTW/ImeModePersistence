"""Capture window screenshots of the app UI for the Microsoft Store listing.

For each UI language it launches the app with a test hook that opens a surface
(the rules dialog, the status box, and the tray context menu) and saves a
cropped screenshot of just that surface. Language is selected by writing the
same registry value the tray Language menu writes
(HKCU\\Software\\ImeModePersistence\\UiLanguage), so no extra flag is needed.

Crops use the DWM "extended frame bounds" (the true visible rectangle) rather
than GetWindowRect, so Windows 11's invisible resize border -- through which the
CI console window would otherwise bleed in at the edges -- is excluded.

Usage:
    python tests/ui/capture_store_screenshots.py [exe] [out-dir]

Output: <out-dir>/{rules,status,menu}-{en,zh-tw,zh-cn,ja,ko}.png
Exit 0 if every screenshot was captured, 1 otherwise.
"""

import os
import sys
import time
import ctypes
import winreg
import subprocess
from ctypes import wintypes

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"
OUT = sys.argv[2] if len(sys.argv) > 2 else "store-shots"

# (UiLanguage registry value, file-name tag). Matches text::Language:
# Auto=0, English=1, TraditionalChinese=2, SimplifiedChinese=3, Japanese=4, Korean=5.
LANGS = [(1, "en"), (2, "zh-tw"), (3, "zh-cn"), (4, "ja"), (5, "ko")]
# (command-line hook, file-name tag, kind). "window" = a process window; "menu"
# = the transient #32768 popup menu.
SCREENS = [
    ("--show-rules", "rules", "window"),
    ("--show-status", "status", "window"),
    ("--show-menu", "menu", "menu"),
]
KEY = r"Software\ImeModePersistence"
DWMWA_EXTENDED_FRAME_BOUNDS = 9
_user32 = ctypes.windll.user32
_dwmapi = ctypes.windll.dwmapi


def set_language(value):
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, KEY)
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, value)
    winreg.CloseKey(key)


def visible_bounds(hwnd):
    """The true visible rectangle: DWM extended frame bounds, falling back to
    GetWindowRect (used for menus, which have no DWM frame)."""
    rect = wintypes.RECT()
    hr = _dwmapi.DwmGetWindowAttribute(
        wintypes.HWND(hwnd), DWMWA_EXTENDED_FRAME_BOUNDS,
        ctypes.byref(rect), ctypes.sizeof(rect))
    if hr != 0 or (rect.right - rect.left) <= 0 or (rect.bottom - rect.top) <= 0:
        _user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(rect))
    return (rect.left, rect.top, rect.right, rect.bottom)


def grab(hwnd, out_path):
    from PIL import ImageGrab
    ImageGrab.grab(bbox=visible_bounds(hwnd), all_screens=True).save(out_path)


def largest_visible_window(pid):
    """Handle of the process's largest visible top-level window (the dialog),
    skipping the hidden zero-size message window."""
    from pywinauto import Application
    app = Application(backend="uia").connect(process=pid, timeout=30)
    for _ in range(20):  # up to ~10s to appear and paint
        best, best_area = None, 0
        for win in app.windows():
            try:
                if not win.is_visible():
                    continue
                r = win.rectangle()
                area = r.width() * r.height()
                if area > best_area:
                    best, best_area = win, area
            except Exception:
                continue
        if best is not None and best_area > 0:
            return best.handle
        time.sleep(0.5)
    raise RuntimeError("no visible window found for the process")


def popup_menu_window():
    """Handle of the transient popup menu (window class #32768)."""
    for _ in range(20):
        hwnd = _user32.FindWindowW("#32768", None)
        if hwnd:
            return hwnd
        time.sleep(0.5)
    raise RuntimeError("popup menu window (#32768) not found")


def main():
    os.makedirs(OUT, exist_ok=True)
    failures = 0
    for value, lang in LANGS:
        set_language(value)
        for flag, screen, kind in SCREENS:
            out_path = os.path.join(OUT, f"{screen}-{lang}.png")
            if kind == "menu":
                # The menu opens at the cursor (GetCursorPos in the handler), so
                # place the cursor somewhere fully on-screen first.
                _user32.SetCursorPos(400, 300)
            proc = subprocess.Popen([EXE, flag])
            try:
                time.sleep(1.5)
                hwnd = popup_menu_window() if kind == "menu" else largest_visible_window(proc.pid)
                time.sleep(0.5)  # let it finish painting
                grab(hwnd, out_path)
                print("captured", out_path)
            except Exception as exc:
                print("FAILED", out_path, repr(exc))
                failures += 1
            finally:
                try:
                    proc.terminate()
                except Exception:
                    pass
                time.sleep(0.5)
    # Leave the machine following Windows again rather than pinned to Korean.
    set_language(0)
    print(f"done: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
