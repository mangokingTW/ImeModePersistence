"""Capture a multi-frame demo of ImeModePersistence's cross-window IME persistence.

Creates a GIF and individual PNG frames showing:
  Frame 0 – Two Win32 windows created; Window A focused, IME set to Chinese (Native).
  Frame 1 – Focus switched to Window B; ImeModePersistence syncs the mode.
  Frame 2 – IME set to Alphanumeric in Window B; engine adopts the new preference.
  Frame 3 – Focus returned to Window A; engine restores Chinese automatically.

Each frame is a full-desktop screenshot (ImageGrab.grab) cropped to the two
windows plus the Windows taskbar, so the real IME language indicator (中 / A)
is visible in every frame.

Usage:
    python tests/ui/capture_ime_recording.py [exe] [out-dir]

Output:
    <out-dir>/ime-recording.gif   – animated demo
    <out-dir>/ime-frame-N.png     – individual frames 0-3
Exit 0 on success, 1 if any frame failed.
"""

from __future__ import annotations

import ctypes
import os
import sys
import time
import winreg
import subprocess
import threading
from ctypes import wintypes
from typing import Optional

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"
OUT = sys.argv[2] if len(sys.argv) > 2 else "ime-recording"

# ---------------------------------------------------------------------------
# Win32 helpers
# ---------------------------------------------------------------------------

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
imm32 = ctypes.windll.imm32
kernel32 = ctypes.windll.kernel32
dwmapi = ctypes.windll.dwmapi

DWMWA_EXTENDED_FRAME_BOUNDS = 9

# Fix 64-bit return types
user32.CreateWindowExW.restype = wintypes.HWND
user32.CreateWindowExW.argtypes = [
    wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR,
    wintypes.DWORD, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
    wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID,
]
user32.RegisterClassW.restype = wintypes.ATOM
user32.FindWindowW.restype = wintypes.HWND
user32.SetForegroundWindow.restype = wintypes.BOOL
user32.ShowWindow.restype = wintypes.BOOL
user32.GetMessageW.restype = ctypes.c_int
user32.DispatchMessageW.restype = ctypes.c_long
user32.SetFocus.restype = wintypes.HWND
user32.GetFocus.restype = wintypes.HWND
user32.DestroyWindow.restype = wintypes.BOOL
imm32.ImmGetDefaultIMEWnd.restype = wintypes.HWND
kernel32.GetModuleHandleW.restype = wintypes.HINSTANCE

WS_OVERLAPPEDWINDOW = 0x00CF0000
WS_VISIBLE = 0x10000000
CS_HREDRAW = 0x0002
CS_VREDRAW = 0x0001
WM_DESTROY = 0x0002
WM_IME_CONTROL = 0x0283
IMC_SETCONVERSIONMODE = 0x0002
IME_CMODE_NATIVE = 0x0001
IME_CMODE_FULLSHAPE = 0x0008
CHINESE_MODE = IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE   # = 9
ALPHANUMERIC_MODE = 0                                    # English


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
    """Pump the Win32 message queue for a short time so windows paint."""
    deadline = time.monotonic() + seconds
    msg = ctypes.create_string_buffer(48)  # sizeof(MSG)
    while time.monotonic() < deadline:
        if user32.PeekMessageW(msg, None, 0, 0, 1):  # PM_REMOVE=1
            user32.TranslateMessage(msg)
            user32.DispatchMessageW(msg)
        else:
            time.sleep(0.01)


def create_win32_window(title: str, x: int, y: int, w: int, h: int) -> wintypes.HWND:
    """Register a minimal window class and create a visible window."""
    hinstance = kernel32.GetModuleHandleW(None)
    class_name = f"ImeRecording_{title.replace(' ', '_')}"

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
    wc.style = CS_HREDRAW | CS_VREDRAW
    wc.lpfnWndProc = _wndproc
    wc.hInstance = hinstance
    wc.hbrBackground = wintypes.HANDLE(6)  # COLOR_WINDOW+1
    wc.lpszClassName = class_name
    user32.RegisterClassW(ctypes.byref(wc))

    hwnd = user32.CreateWindowExW(
        0, class_name, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, w, h,
        None, None, hinstance, None,
    )
    return hwnd


def set_ime_mode(hwnd: wintypes.HWND, mode: int) -> None:
    """Set the IME conversion mode for the given window via WM_IME_CONTROL."""
    ime_wnd = imm32.ImmGetDefaultIMEWnd(hwnd)
    if ime_wnd:
        user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, mode)


def visible_bounds(hwnd):
    """True visible rect via DWM extended frame bounds."""
    rect = wintypes.RECT()
    hr = dwmapi.DwmGetWindowAttribute(
        wintypes.HWND(hwnd), DWMWA_EXTENDED_FRAME_BOUNDS,
        ctypes.byref(rect), ctypes.sizeof(rect),
    )
    if hr != 0 or (rect.right - rect.left) <= 0:
        user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(rect))
    return (rect.left, rect.top, rect.right, rect.bottom)


def taskbar_hwnd() -> Optional[wintypes.HWND]:
    return user32.FindWindowW("Shell_TrayWnd", None) or None


def _printwindow_grab(hwnd) -> "Image.Image":
    """Fallback capture via PrintWindow for non-interactive sessions."""
    from PIL import Image as _Image
    rect = wintypes.RECT()
    user32.GetWindowRect(wintypes.HWND(hwnd), ctypes.byref(rect))
    w = max(1, rect.right - rect.left)
    h = max(1, rect.bottom - rect.top)
    hdc_wnd = user32.GetWindowDC(wintypes.HWND(hwnd))
    hdc_mem = gdi32.CreateCompatibleDC(hdc_wnd)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_wnd, w, h)
    old = gdi32.SelectObject(hdc_mem, hbmp)
    user32.PrintWindow(wintypes.HWND(hwnd), hdc_mem, 2)

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
    gdi32.SelectObject(hdc_mem, old)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(wintypes.HWND(hwnd), hdc_wnd)
    gdi32.DeleteObject(hbmp)
    return _Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")


def capture_frame(hwnd_a, hwnd_b, label: str) -> "Image.Image":
    """Full-desktop grab (or PrintWindow fallback) of both windows + taskbar strip."""
    from PIL import Image as _Image, ImageDraw, ImageFont

    # Try ImageGrab first (works on GitHub Actions interactive desktop)
    # Fall back to PrintWindow side-by-side composite for non-interactive sessions
    try:
        from PIL import ImageGrab
        ba = visible_bounds(hwnd_a)
        bb = visible_bounds(hwnd_b)
        left = max(0, min(ba[0], bb[0]) - 20)
        top = max(0, min(ba[1], bb[1]) - 30)
        right = max(ba[2], bb[2]) + 20
        sm_cy = user32.GetSystemMetrics(1)
        frame = ImageGrab.grab(bbox=(left, top, right, sm_cy), all_screens=True)
        # Verify it captured something real
        px = list(frame.getdata())
        unique = len(set(px))
        if unique <= 2:
            raise OSError("blank screen - not interactive")
    except OSError:
        # Non-interactive session fallback: composite PrintWindow captures side by side
        img_a = _printwindow_grab(hwnd_a)
        img_b = _printwindow_grab(hwnd_b)
        total_w = img_a.width + img_b.width + 20
        total_h = max(img_a.height, img_b.height)
        frame = _Image.new("RGB", (total_w, total_h + 40), (243, 244, 246))
        frame.paste(img_a, (0, 40))
        frame.paste(img_b, (img_a.width + 20, 40))

    draw = ImageDraw.Draw(frame)
    banner_h = 34
    draw.rectangle([0, 0, frame.width, banner_h], fill=(17, 24, 39))
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/msjhbd.ttc", 15)
    except Exception:
        font = ImageFont.load_default()
    draw.text((12, 8), label, font=font, fill=(255, 255, 255))

    return frame


# ---------------------------------------------------------------------------
# Main recording logic
# ---------------------------------------------------------------------------

def main() -> int:
    os.makedirs(OUT, exist_ok=True)
    failures = 0

    # Set Traditional Chinese UI
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\ImeModePersistence")
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, 2)
    winreg.CloseKey(key)

    # Kill any previous instance
    subprocess.run(["taskkill", "/F", "/IM", "ImeModePersistence.exe"],
                   capture_output=True)
    time.sleep(0.3)

    # Start ImeModePersistence engine
    engine = subprocess.Popen([EXE])
    time.sleep(1.0)  # let it register its WinEventHook

    # Create two windows side by side
    hwnd_a = create_win32_window(
        "【測試視窗 A】繁體中文輸入 (Window A)",
        x=40, y=100, w=480, h=380,
    )
    hwnd_b = create_win32_window(
        "【測試視窗 B】英數輸入 (Window B)",
        x=560, y=100, w=480, h=380,
    )

    _pump_messages_briefly(0.5)  # let windows appear and paint

    frames = []

    try:
        # ── Frame 0: Window A focused, Chinese mode ──────────────────────────
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(hwnd_a)
        _pump_messages_briefly(0.3)
        set_ime_mode(hwnd_a, CHINESE_MODE)
        time.sleep(0.5)  # allow ImeModePersistence to react
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 1：聚焦視窗 A，設為繁體中文模式 ── IME 指示器應顯示「中」",
            ))
            frames[0].save(os.path.join(OUT, "ime-frame-0.png"))
            print("captured frame 0")
        except Exception as exc:
            print("FAILED frame 0:", exc)
            failures += 1

        # ── Frame 1: Switch to Window B, engine syncs ────────────────────────
        user32.SetForegroundWindow(hwnd_b)
        user32.SetFocus(hwnd_b)
        _pump_messages_briefly(0.3)
        time.sleep(0.5)
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 2：切換至視窗 B ── ImeModePersistence 自動同步維持繁體中文",
            ))
            frames[1].save(os.path.join(OUT, "ime-frame-1.png"))
            print("captured frame 1")
        except Exception as exc:
            print("FAILED frame 1:", exc)
            failures += 1

        # ── Frame 2: Set Alphanumeric in Window B ────────────────────────────
        set_ime_mode(hwnd_b, ALPHANUMERIC_MODE)
        time.sleep(0.5)
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 3：在視窗 B 切換為英數模式 ── IME 指示器顯示「A」",
            ))
            frames[2].save(os.path.join(OUT, "ime-frame-2.png"))
            print("captured frame 2")
        except Exception as exc:
            print("FAILED frame 2:", exc)
            failures += 1

        # ── Frame 3: Switch back to Window A, engine restores ────────────────
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(hwnd_a)
        _pump_messages_briefly(0.3)
        time.sleep(0.5)
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 4：切換回視窗 A ── IME 自動還原為繁體中文，指示器回到「中」✅",
            ))
            frames[3].save(os.path.join(OUT, "ime-frame-3.png"))
            print("captured frame 3")
        except Exception as exc:
            print("FAILED frame 3:", exc)
            failures += 1

    finally:
        # Destroy windows cleanly
        user32.DestroyWindow(hwnd_a)
        user32.DestroyWindow(hwnd_b)
        engine.terminate()
        engine.wait(timeout=5)

    # Save animated GIF
    if frames:
        gif_path = os.path.join(OUT, "ime-recording.gif")
        frames[0].save(
            gif_path,
            save_all=True,
            append_images=frames[1:],
            duration=1800,
            loop=0,
        )
        print(f"saved {gif_path} ({len(frames)} frames)")

    # Restore defaults
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\ImeModePersistence")
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, 0)
    winreg.CloseKey(key)

    print(f"done: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
