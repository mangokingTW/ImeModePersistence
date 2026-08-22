"""Continuous real-time screen recorder for ImeModePersistence cross-window demo.

Records a high-framerate (10 FPS) continuous video/GIF of the real desktop and Windows taskbar
while driving an interactive typing and window-switching workflow:
  1. Window A active: activate zh-TW Bopomofo and set Chinese mode ("中 ㄅ").
  2. Switch focus to Window B: ImeModePersistence automatically syncs Chinese mode.
  3. Window B switched to English ("英 ㄅ") and types alphanumeric text.
  4. Switch focus back to Window A: ImeModePersistence restores English mode automatically.

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

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"
OUT = sys.argv[2] if len(sys.argv) > 2 else "ime-recording"

# ---------------------------------------------------------------------------
# Win32 Helpers & Constants
# ---------------------------------------------------------------------------

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
WM_SETFONT = 0x0030
WM_SETTEXT = 0x000C
WM_INPUTLANGCHANGEREQUEST = 0x0050
WM_IME_CONTROL = 0x0283

IMC_GETCONVERSIONMODE = 0x0001
IMC_SETCONVERSIONMODE = 0x0002
IMC_GETOPENSTATUS = 0x0005
IMC_SETOPENSTATUS = 0x0006

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


def _pump_messages_briefly(seconds: float = 0.1) -> None:
    deadline = time.monotonic() + seconds
    msg = ctypes.create_string_buffer(48)
    while time.monotonic() < deadline:
        if user32.PeekMessageW(msg, None, 0, 0, 1):
            user32.TranslateMessage(msg)
            user32.DispatchMessageW(msg)
        else:
            time.sleep(0.01)


def create_editor_window(title: str, x: int, y: int, w: int, h: int) -> tuple[wintypes.HWND, wintypes.HWND]:
    """Create a window with a multiline text editor inside."""
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
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE,
        15, 15, w - 45, h - 70,
        hwnd, None, hinstance, None,
    )

    font = gdi32.CreateFontW(
        22, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 2, 0, "Microsoft JhengHei"
    )
    if font:
        user32.SendMessageW(hwnd_edit, WM_SETFONT, font, 1)

    return hwnd, hwnd_edit


def append_text(hwnd_edit: wintypes.HWND, text: str) -> None:
    current_len = user32.GetWindowTextLengthW(hwnd_edit)
    user32.SendMessageW(hwnd_edit, 0x00B1, current_len, current_len)  # EM_SETSEL
    user32.SendMessageW(hwnd_edit, 0x00C2, 0, text)  # EM_REPLACESEL


def set_chinese_mode(hwnd: wintypes.HWND, edit_hwnd: wintypes.HWND) -> None:
    """Explicitly activate zh-TW Bopomofo layout and enable Chinese mode ('中 ㄅ')."""
    hkl_tw = user32.LoadKeyboardLayoutW("00000404", 1)  # KLF_ACTIVATE
    if hkl_tw:
        user32.ActivateKeyboardLayout(hkl_tw, 0)
        user32.SendMessageW(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, hkl_tw)
        user32.SendMessageW(edit_hwnd, WM_INPUTLANGCHANGEREQUEST, 0, hkl_tw)

    for target_hwnd in (edit_hwnd, hwnd):
        ime_wnd = imm32.ImmGetDefaultIMEWnd(target_hwnd)
        if ime_wnd:
            user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 1)
            user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, CHINESE_MODE)
        himc = imm32.ImmGetContext(target_hwnd)
        if himc:
            try:
                imm32.ImmSetOpenStatus(himc, 1)
                imm32.ImmSetConversionStatus(himc, CHINESE_MODE, 0)
            finally:
                imm32.ImmReleaseContext(target_hwnd, himc)


def set_alphanumeric_mode(hwnd: wintypes.HWND, edit_hwnd: wintypes.HWND) -> None:
    """Set IME mode to Alphanumeric / English ('英 ㄅ')."""
    for target_hwnd in (edit_hwnd, hwnd):
        ime_wnd = imm32.ImmGetDefaultIMEWnd(target_hwnd)
        if ime_wnd:
            user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 0)
            user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, ALPHANUMERIC_MODE)
        himc = imm32.ImmGetContext(target_hwnd)
        if himc:
            try:
                imm32.ImmSetOpenStatus(himc, 0)
                imm32.ImmSetConversionStatus(himc, ALPHANUMERIC_MODE, 0)
            finally:
                imm32.ImmReleaseContext(target_hwnd, himc)


def grab_real_screen() -> Image.Image:
    """Grab the actual physical screen including taskbar via GDI."""
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


# ---------------------------------------------------------------------------
# Continuous Recorder Worker
# ---------------------------------------------------------------------------

class ContinuousRecorder:
    def __init__(self, fps: int = 10):
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
            sleep_time = max(0.01, self.interval - elapsed)
            time.sleep(sleep_time)

    def stop(self) -> list[Image.Image]:
        self.stop_event.set()
        if self.thread:
            self.thread.join(timeout=3)
        return self.frames


# ---------------------------------------------------------------------------
# Main Recording Scenario
# ---------------------------------------------------------------------------

def main() -> int:
    os.makedirs(OUT, exist_ok=True)

    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\ImeModePersistence")
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, 2)
    winreg.CloseKey(key)

    subprocess.run(["taskkill", "/F", "/IM", "ImeModePersistence.exe"], capture_output=True)
    time.sleep(0.3)

    engine = subprocess.Popen([EXE])
    time.sleep(1.0)

    hwnd_a, edit_a = create_editor_window(
        "【視窗 A】繁體中文編輯區 (Window A)",
        x=40, y=80, w=480, h=360,
    )
    hwnd_b, edit_b = create_editor_window(
        "【視窗 B】英數編輯區 (Window B)",
        x=550, y=80, w=480, h=360,
    )

    _pump_messages_briefly(0.5)

    recorder = ContinuousRecorder(fps=10)
    key_frames = []

    try:
        print("Starting continuous real-time desktop recording (10 FPS)...")
        recorder.start()

        # Step 1: Focus Window A, set Chinese, type text
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(edit_a)
        set_chinese_mode(hwnd_a, edit_a)
        _pump_messages_briefly(0.2)
        append_text(edit_a, "正在視窗 A 使用繁體中文注音輸入模式...\r\n")
        time.sleep(1.2)
        key_frames.append(grab_real_screen())

        # Step 2: Switch to Window B (Engine syncs Chinese)
        user32.SetForegroundWindow(hwnd_b)
        user32.SetFocus(edit_b)
        _pump_messages_briefly(0.2)
        time.sleep(0.5)
        append_text(edit_b, "切換至視窗 B，ImeModePersistence 自動同步維持繁中模式！\r\n")
        time.sleep(1.2)
        key_frames.append(grab_real_screen())

        # Step 3: Switch to English mode in Window B
        set_alphanumeric_mode(hwnd_b, edit_b)
        _pump_messages_briefly(0.2)
        time.sleep(0.5)
        append_text(edit_b, "Switch to English alphanumeric mode.\r\n")
        time.sleep(1.2)
        key_frames.append(grab_real_screen())

        # Step 4: Switch back to Window A (Engine restores English)
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(edit_a)
        _pump_messages_briefly(0.2)
        time.sleep(0.5)
        append_text(edit_a, "切換回視窗 A，引擎自動還原為最新英數模式！\r\n")
        time.sleep(1.5)
        key_frames.append(grab_real_screen())

        # Stop recording
        all_frames = recorder.stop()
        print(f"Recording finished! Total frames captured: {len(all_frames)}")

        # Save individual key step frames
        for i, kf in enumerate(key_frames):
            kf.save(os.path.join(OUT, f"ime-frame-{i}.png"))

        # Save high-framerate fluid animated GIF
        if len(all_frames) > 0:
            gif_path = os.path.join(OUT, "ime-recording.gif")
            sample_w = min(1280, all_frames[0].width)
            sample_h = int(all_frames[0].height * (sample_w / all_frames[0].width))
            resized_frames = [f.resize((sample_w, sample_h), Image.Resampling.LANCZOS) for f in all_frames]

            resized_frames[0].save(
                gif_path,
                save_all=True,
                append_images=resized_frames[1:],
                duration=100,  # 100ms per frame = 10 FPS
                loop=0,
                optimize=True,
            )
            print(f"Saved smooth video recording GIF ({len(all_frames)} frames @ 10 FPS): {gif_path}")

    finally:
        user32.DestroyWindow(hwnd_a)
        user32.DestroyWindow(hwnd_b)
        engine.terminate()
        try:
            engine.wait(timeout=2)
        except Exception:
            engine.kill()

    return 0


if __name__ == "__main__":
    sys.exit(main())
