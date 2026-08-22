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

class NotepadWindow:
    """Manages a genuine Windows Notepad process with full Microsoft TSF IME candidate window support."""

    def __init__(self, x: int, y: int, w: int, h: int):
        from pywinauto.application import Application

        self.proc = subprocess.Popen(["notepad.exe"])
        time.sleep(1.0)
        self.app = Application(backend="uia").connect(process=self.proc.pid)
        self.dlg = self.app.top_window()
        self.hwnd = self.dlg.handle

        # Move and resize window
        user32.MoveWindow(self.hwnd, x, y, w, h, True)
        self.set_foreground()

    def set_foreground(self):
        user32.keybd_event(0, 0, 0, 0)
        self.dlg.set_focus()
        time.sleep(0.3)

    def type_text(self, text: str, delay_per_char: float = 0.04):
        self.set_foreground()
        self.dlg.type_keys(text, with_spaces=True, with_newlines=True, pause=delay_per_char)
        time.sleep(0.3)

    def type_bopomofo(self, key_sequence: str):
        """Types authentic bopomofo keys using PyAutoGUI punchy syntax."""
        import pyautogui

        self.set_foreground()
        time.sleep(0.3)
        pyautogui.write(key_sequence, interval=0.1)
        time.sleep(0.3)
        pyautogui.press(['space', 'enter'])
        time.sleep(0.4)


    def set_chinese(self):
        self.set_foreground()
        hkl = user32.LoadKeyboardLayoutW("00000404", 1)
        if hkl:
            user32.ActivateKeyboardLayout(hkl, 0)
            user32.SendMessageW(self.hwnd, WM_INPUTLANGCHANGEREQUEST, 0, hkl)

        ime_wnd = imm32.ImmGetDefaultIMEWnd(self.hwnd)
        if ime_wnd:
            user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 1)
            user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, 1)

        himc = imm32.ImmGetContext(self.hwnd)
        if himc:
            try:
                imm32.ImmSetOpenStatus(himc, 1)
                imm32.ImmSetConversionStatus(himc, 1, 0)
            finally:
                imm32.ImmReleaseContext(self.hwnd, himc)

        import pyautogui
        pyautogui.press('shift')
        time.sleep(0.3)

    def set_alphanumeric(self):
        self.set_foreground()
        import pyautogui
        pyautogui.press('shift')
        time.sleep(0.3)


    def close(self):
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

    # Enable PersistMode in registry
    key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\ImeModePersistence")
    winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, 2)
    winreg.SetValueEx(key, "PersistMode", 0, winreg.REG_DWORD, 1)
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

    engine = subprocess.Popen([EXE])
    time.sleep(1.0)

    win_a = NotepadWindow(x=40, y=80, w=480, h=360)
    win_b = NotepadWindow(x=550, y=80, w=480, h=360)
    time.sleep(0.5)

    recorder = ContinuousRecorder(fps=60)

    try:
        print("Starting continuous real-time desktop recording (60 FPS)...")
        recorder.start()

        # Step 1: Window A activated, set Chinese mode, type authentic bopomofo via PyAutoGUI
        win_a.set_foreground()
        win_a.set_chinese()
        time.sleep(0.4)
        win_a.type_text("【視窗 A】已啟用微軟注音繁體中文模式...\n注音輸入：")
        # 模擬打出「測試」（2g4 空格 Enter，g4 空格 Enter）
        win_a.type_bopomofo("2g4")
        win_a.type_bopomofo("g4")
        win_a.type_text("\n")
        time.sleep(1.8)  # Dwell to let engine adopt Chinese mode

        # Step 2: Switch to Window B -> Engine automatically maintains Chinese and native candidate selection
        win_b.set_foreground()
        time.sleep(0.8)
        win_b.type_text("【視窗 B】切換至此視窗，ImeModePersistence 自動同步維持繁中模式！\n注音輸入：")
        win_b.type_bopomofo("2g4")
        win_b.type_bopomofo("g4")
        win_b.type_text("\n")
        time.sleep(2.0)


        # Step 3: Switch to English mode in Window B
        win_b.set_alphanumeric()
        time.sleep(0.4)
        win_b.type_text("【視窗 B】手動切換為英數模式 (Switch to English)\n")
        win_b.type_text("Typing in English without manual switching!\n")
        time.sleep(1.8)  # Dwell to let engine adopt Alphanumeric mode

        # Step 4: Switch back to Window A -> Engine restores English mode
        win_a.set_foreground()
        time.sleep(0.8)
        win_a.type_text("【視窗 A】切換回視窗 A，引擎自動還原為最新英數模式！\n")
        win_a.type_text("Engine restores latest alphanumeric state automatically!\n")
        time.sleep(2.0)

        all_frames = recorder.stop()
        print(f"Recording finished! Total frames captured: {len(all_frames)}")

        # Save pristine 60 FPS H.264 MP4 video only (no screenshots)

        if len(all_frames) > 0:
            mp4_path = os.path.join(OUT, "ime-recording.mp4")
            # H.264 requires even width and height
            w = all_frames[0].width - (all_frames[0].width % 2)
            h = all_frames[0].height - (all_frames[0].height % 2)

            try:
                import imageio_ffmpeg
                ffmpeg_bin = imageio_ffmpeg.get_ffmpeg_exe()
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

    finally:
        win_a.close()
        win_b.close()
        engine.terminate()
        try:
            engine.wait(timeout=2)
        except Exception:
            engine.kill()

    return 0

if __name__ == "__main__":
    sys.exit(main())
