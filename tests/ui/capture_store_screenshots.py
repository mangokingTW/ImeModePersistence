"""Capture window screenshots of the app UI for the Microsoft Store listing.

For each UI language it launches the app with a test hook that opens a window
(the rules dialog and the status box), then saves a cropped screenshot of just
that window. Language is selected by writing the same registry value the tray
Language menu writes (HKCU\\Software\\ImeModePersistence\\UiLanguage), so no
extra command-line flag is needed.

Windows are located by the launched process id and picking its largest visible
top-level window, so this is language-agnostic (no title matching) and ignores
the hidden zero-size message window.

Usage:
    python tests/ui/capture_store_screenshots.py [exe] [out-dir]

Output: <out-dir>/{rules,status}-{en,zh-tw,zh-cn,ja,ko}.png
Exit 0 if every screenshot was captured, 1 otherwise.
"""

import os
import sys
import time
import winreg
import subprocess

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"
OUT = sys.argv[2] if len(sys.argv) > 2 else "store-shots"

# (UiLanguage registry value, file-name tag). Matches text::Language:
# Auto=0, English=1, TraditionalChinese=2, SimplifiedChinese=3, Japanese=4, Korean=5.
LANGS = [(1, "en"), (2, "zh-tw"), (3, "zh-cn"), (4, "ja"), (5, "ko")]
SCREENS = [("--show-rules", "rules"), ("--show-status", "status")]
KEY = r"Software\ImeModePersistence"


def set_language(value):
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, KEY)
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, value)
    winreg.CloseKey(key)


def capture(pid, out_path):
    from pywinauto import Application

    app = Application(backend="uia").connect(process=pid, timeout=30)
    # app.windows() returns already-resolved UIAWrapper objects (no .wait()), so
    # poll for the largest visible top-level window to appear and paint. The
    # hidden zero-size message window is skipped by the is_visible()/area check.
    best = None
    for _ in range(20):  # up to ~10s
        best, best_area = None, 0
        for win in app.windows():
            try:
                if not win.is_visible():
                    continue
                rect = win.rectangle()
                area = rect.width() * rect.height()
                if area > best_area:
                    best, best_area = win, area
            except Exception:
                continue
        if best is not None and best_area > 0:
            break
        time.sleep(0.5)
    if best is None:
        raise RuntimeError("no visible window found for the process")
    time.sleep(0.5)  # let it finish painting
    best.capture_as_image().save(out_path)


def main():
    os.makedirs(OUT, exist_ok=True)
    failures = 0
    for value, lang in LANGS:
        set_language(value)
        for flag, screen in SCREENS:
            out_path = os.path.join(OUT, f"{screen}-{lang}.png")
            proc = subprocess.Popen([EXE, flag])
            try:
                capture(proc.pid, out_path)
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
