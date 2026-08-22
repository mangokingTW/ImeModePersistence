"""Capture a multi-frame demo of ImeModePersistence's cross-window IME persistence.

Directly grabs the full desktop screen (including the real Windows taskbar and IME indicator)
at each step of the cross-window persistence flow:
  Frame 0 – Window A focused, set to Chinese mode.
  Frame 1 – Switch focus to Window B; engine syncs Chinese mode.
  Frame 2 – Switch to Alphanumeric mode in Window B; engine adopts preference.
  Frame 3 – Switch back to Window A; engine restores Alphanumeric mode automatically.

Usage:
    python tests/ui/capture_ime_recording.py [exe] [out-dir]
"""

from __future__ import annotations

import ctypes
import os
import sys
import time
import winreg
import subprocess
from ctypes import wintypes
from PIL import Image

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"
OUT = sys.argv[2] if len(sys.argv) > 2 else "ime-recording"

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
imm32 = ctypes.windll.imm32
kernel32 = ctypes.windll.kernel32

user32.CreateWindowExW.restype = wintypes.HWND
user32.CreateWindowExW.argtypes = [
    wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR,
    wintypes.DWORD, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
    wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID,
]
user32.RegisterClassW.restype = wintypes.ATOM
user32.SendMessageW.restype = ctypes.c_long

WS_OVERLAPPEDWINDOW = 0x00CF0000
WS_VISIBLE = 0x10000000
WS_CHILD = 0x40000000
WS_BORDER = 0x00800000
ES_MULTILINE = 0x0004
WM_DESTROY = 0x0002
WM_IME_CONTROL = 0x0283
IMC_SETCONVERSIONMODE = 0x0002

IME_CMODE_ALPHANUMERIC = 0x0000
IME_CMODE_NATIVE = 0x0001
IME_CMODE_FULLSHAPE = 0x0008
CHINESE_MODE = IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE
ALPHANUMERIC_MODE = IME_CMODE_ALPHANUMERIC

WNDPROC = ctypes.WINFUNCTYPE(
    ctypes.c_long, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
)


@WNDPROC
def _wndproc(hwnd, msg, wparam, lparam):
    if msg == WM_DESTROY:
        user32.PostQuitMessage(0)
        return 0
    return user32.DefWindowProcW(
        wintypes.HWND(hwnd), wintypes.UINT(msg),
        wintypes.WPARAM(wparam), wintypes.LPARAM(lparam),
    )


def _pump_messages_briefly(seconds: float = 0.15) -> None:
    deadline = time.monotonic() + seconds
    msg = ctypes.create_string_buffer(48)
    while time.monotonic() < deadline:
        if user32.PeekMessageW(msg, None, 0, 0, 1):
            user32.TranslateMessage(msg)
            user32.DispatchMessageW(msg)
        else:
            time.sleep(0.01)


def create_test_window(title: str, text: str, x: int, y: int, w: int, h: int) -> tuple[wintypes.HWND, wintypes.HWND]:
    hinstance = kernel32.GetModuleHandleW(None)
    class_name = f"ImeRec_{abs(hash(title))}"

    class WNDCLASSW(ctypes.Structure):
        _fields_ = [
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
        ]

    wc = WNDCLASSW()
    wc.style = 0x0003
    wc.lpfnWndProc = _wndproc
    wc.hInstance = hinstance
    wc.hbrBackground = wintypes.HANDLE(6)
    wc.lpszClassName = class_name
    user32.RegisterClassW(ctypes.byref(wc))

    hwnd = user32.CreateWindowExW(
        0, class_name, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, w, h,
        None, None, hinstance, None,
    )

    hwnd_edit = user32.CreateWindowExW(
        0, "EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE,
        15, 15, w - 45, h - 70,
        hwnd, None, hinstance, None,
    )

    return hwnd, hwnd_edit


def set_ime_mode(hwnd: wintypes.HWND, mode: int) -> None:
    ime_wnd = imm32.ImmGetDefaultIMEWnd(hwnd)
    if ime_wnd:
        user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, mode)


def grab_real_screen() -> Image.Image:
    """Capture the actual physical desktop screen via GDI BitBlt."""
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


def main() -> int:
    os.makedirs(OUT, exist_ok=True)
    failures = 0

    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\ImeModePersistence")
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, 2)
    winreg.CloseKey(key)

    subprocess.run(["taskkill", "/F", "/IM", "ImeModePersistence.exe"], capture_output=True)
    time.sleep(0.3)

    engine = subprocess.Popen([EXE])
    time.sleep(1.0)

    hwnd_a, edit_a = create_test_window(
        "Window A (Chinese)", "測試視窗 A - 繁體中文輸入",
        x=50, y=100, w=500, h=380,
    )
    hwnd_b, edit_b = create_test_window(
        "Window B (English)", "Window B - English Alphanumeric",
        x=600, y=100, w=500, h=380,
    )

    _pump_messages_briefly(0.5)

    frames = []

    try:
        # Step 0: Focus Window A, set Chinese
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(edit_a)
        _pump_messages_briefly(0.3)
        set_ime_mode(hwnd_a, CHINESE_MODE)
        time.sleep(0.8)
        _pump_messages_briefly(0.3)
        img0 = grab_real_screen()
        frames.append(img0)
        img0.save(os.path.join(OUT, "ime-frame-0.png"))
        print("captured real frame 0")

        # Step 1: Switch to Window B (Engine syncs Chinese)
        user32.SetForegroundWindow(hwnd_b)
        user32.SetFocus(edit_b)
        _pump_messages_briefly(0.3)
        time.sleep(0.8)
        _pump_messages_briefly(0.3)
        img1 = grab_real_screen()
        frames.append(img1)
        img1.save(os.path.join(OUT, "ime-frame-1.png"))
        print("captured real frame 1")

        # Step 2: Switch to English in Window B
        set_ime_mode(hwnd_b, ALPHANUMERIC_MODE)
        time.sleep(0.8)
        _pump_messages_briefly(0.3)
        img2 = grab_real_screen()
        frames.append(img2)
        img2.save(os.path.join(OUT, "ime-frame-2.png"))
        print("captured real frame 2")

        # Step 3: Switch back to Window A (Engine restores English)
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(edit_a)
        _pump_messages_briefly(0.3)
        time.sleep(0.8)
        _pump_messages_briefly(0.3)
        img3 = grab_real_screen()
        frames.append(img3)
        img3.save(os.path.join(OUT, "ime-frame-3.png"))
        print("captured real frame 3")

        # Generate GIF
        if len(frames) == 4:
            gif_path = os.path.join(OUT, "ime-recording.gif")
            frames[0].save(
                gif_path,
                save_all=True,
                append_images=frames[1:],
                duration=1800,
                loop=0,
            )
            print(f"Saved real screen GIF: {gif_path}")

    except Exception as exc:
        print("Error during recording:", exc)
        failures += 1
    finally:
        user32.DestroyWindow(hwnd_a)
        user32.DestroyWindow(hwnd_b)
        engine.terminate()
        try:
            engine.wait(timeout=2)
        except Exception:
            engine.kill()

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
