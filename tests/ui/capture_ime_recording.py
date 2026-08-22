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


class ThreadedEditorWindow:
    """A standalone Win32 editor window on its own thread with isolated IME context."""

    _registered = False
    _lock = threading.Lock()
    CLASS_NAME = "ImeRecorderWindowClass"

    def __init__(self, title: str, x: int, y: int, w: int, h: int):
        self.title = title
        self.x, self.y, self.w, self.h = x, y, w, h
        self.hwnd = None
        self.edit_hwnd = None
        self.thread_id = 0
        self.ready_event = threading.Event()
        self.stop_event = threading.Event()

        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        if not self.ready_event.wait(timeout=5.0):
            raise TimeoutError(f"Window '{title}' failed to initialize.")

    def _run(self):
        hinst = kernel32.GetModuleHandleW(None)
        with ThreadedEditorWindow._lock:
            if not ThreadedEditorWindow._registered:
                wcex = WNDCLASSEXW()
                wcex.cbSize = ctypes.sizeof(WNDCLASSEXW)
                wcex.style = 0x0003
                wcex.lpfnWndProc = _window_wndproc
                wcex.hInstance = hinst
                wcex.hbrBackground = wintypes.HANDLE(6)
                wcex.lpszClassName = ThreadedEditorWindow.CLASS_NAME
                user32.RegisterClassExW(ctypes.byref(wcex))
                ThreadedEditorWindow._registered = True

        self.hwnd = user32.CreateWindowExW(
            0, ThreadedEditorWindow.CLASS_NAME, self.title,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            self.x, self.y, self.w, self.h,
            None, None, hinst, None,
        )

        self.edit_hwnd = user32.CreateWindowExW(
            0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
            15, 15, self.w - 45, self.h - 70,
            self.hwnd, None, hinst, None,
        )

        font = gdi32.CreateFontW(
            22, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 2, 0, "Microsoft JhengHei"
        )
        if font:
            user32.SendMessageW(self.edit_hwnd, WM_SETFONT, font, 1)

        pid = wintypes.DWORD()
        self.thread_id = user32.GetWindowThreadProcessId(self.hwnd, ctypes.byref(pid))
        self.ready_event.set()

        msg = ctypes.create_string_buffer(48)
        while not self.stop_event.is_set():
            if user32.PeekMessageW(msg, None, 0, 0, 1):
                user32.TranslateMessage(msg)
                user32.DispatchMessageW(msg)
            else:
                time.sleep(0.01)

    def set_foreground(self):
        user32.keybd_event(0, 0, 0, 0)  # Bypass Windows foreground lock
        cur_thread = kernel32.GetCurrentThreadId()
        fg_wnd = user32.GetForegroundWindow()
        fg_thread = user32.GetWindowThreadProcessId(fg_wnd, None) if fg_wnd else 0
        target_thread = self.thread_id

        if fg_thread and fg_thread != target_thread:
            user32.AttachThreadInput(cur_thread, target_thread, True)
            user32.AttachThreadInput(fg_thread, target_thread, True)

        user32.ShowWindow(self.hwnd, 9)  # SW_RESTORE
        user32.SetForegroundWindow(self.hwnd)
        user32.BringWindowToTop(self.hwnd)
        user32.SetFocus(self.edit_hwnd or self.hwnd)

        if fg_thread and fg_thread != target_thread:
            user32.AttachThreadInput(fg_thread, target_thread, False)
            user32.AttachThreadInput(cur_thread, target_thread, False)

        time.sleep(0.3)


    def type_text(self, text: str, delay_per_char: float = 0.045):
        """Simulates authentic real-time keyboard typing character by character."""
        for ch in text:
            cur_len = user32.GetWindowTextLengthW(self.edit_hwnd)
            user32.SendMessageW(self.edit_hwnd, 0x00B1, cur_len, cur_len)
            user32.SendMessageW(self.edit_hwnd, 0x00C2, 0, ch)
            time.sleep(delay_per_char)


    def set_chinese(self):
        hkl = user32.LoadKeyboardLayoutW("00000404", 1)
        if hkl:
            user32.ActivateKeyboardLayout(hkl, 0)
            user32.SendMessageW(self.hwnd, WM_INPUTLANGCHANGEREQUEST, 0, hkl)
            user32.SendMessageW(self.edit_hwnd, WM_INPUTLANGCHANGEREQUEST, 0, hkl)

        for w in (self.edit_hwnd, self.hwnd):
            ime_wnd = imm32.ImmGetDefaultIMEWnd(w)
            if ime_wnd:
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 1)
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE)
            himc = imm32.ImmGetContext(w)
            if himc:
                try:
                    imm32.ImmSetOpenStatus(himc, 1)
                    imm32.ImmSetConversionStatus(himc, IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE, 0)
                finally:
                    imm32.ImmReleaseContext(w, himc)

        # Trigger Shift press with hardware scan code 0x2A for Microsoft Bopomofo TIP indicator
        scan = user32.MapVirtualKeyW(VK_SHIFT, 0) or 0x2A
        user32.keybd_event(VK_SHIFT, scan, 0, 0)
        time.sleep(0.05)
        user32.keybd_event(VK_SHIFT, scan, KEYEVENTF_KEYUP, 0)

    def set_alphanumeric(self):
        for w in (self.edit_hwnd, self.hwnd):
            ime_wnd = imm32.ImmGetDefaultIMEWnd(w)
            if ime_wnd:
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 0)
                user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, IME_CMODE_ALPHANUMERIC)
            himc = imm32.ImmGetContext(w)
            if himc:
                try:
                    imm32.ImmSetOpenStatus(himc, 0)
                    imm32.ImmSetConversionStatus(himc, IME_CMODE_ALPHANUMERIC, 0)
                finally:
                    imm32.ImmReleaseContext(w, himc)

        scan = user32.MapVirtualKeyW(VK_SHIFT, 0) or 0x2A
        user32.keybd_event(VK_SHIFT, scan, 0, 0)
        time.sleep(0.05)
        user32.keybd_event(VK_SHIFT, scan, KEYEVENTF_KEYUP, 0)


    def close(self):
        self.stop_event.set()
        if self.hwnd:
            user32.PostMessageW(self.hwnd, 0x0010, 0, 0)  # WM_CLOSE


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

    # Configure Microsoft Bopomofo default mode to Chinese ('中 ㄅ') and enable Shift switching
    try:
        ime_key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\IME\15.0\IMETC")
        winreg.SetValueEx(ime_key, "Default Input Mode", 0, winreg.REG_DWORD, 1)
        winreg.SetValueEx(ime_key, "Left Shift Usage", 0, winreg.REG_DWORD, 1)
        winreg.SetValueEx(ime_key, "Right Shift Usage", 0, winreg.REG_DWORD, 1)
        winreg.CloseKey(ime_key)
    except Exception:
        pass

    subprocess.run(["taskkill", "/F", "/IM", "ImeModePersistence.exe"], capture_output=True)

    time.sleep(0.3)

    engine = subprocess.Popen([EXE])
    time.sleep(1.0)

    win_a = ThreadedEditorWindow("【視窗 A】繁體中文編輯區 (Window A)", x=40, y=80, w=480, h=360)
    win_b = ThreadedEditorWindow("【視窗 B】英數編輯區 (Window B)", x=550, y=80, w=480, h=360)
    time.sleep(0.5)

    recorder = ContinuousRecorder(fps=60)
    key_frames = []

    try:
        print("Starting continuous real-time desktop recording (60 FPS)...")
        recorder.start()

        # Step 1: Window A activated, set Chinese mode
        win_a.set_foreground()
        win_a.set_chinese()
        time.sleep(0.3)
        win_a.type_text("【視窗 A】已啟用微軟注音繁體中文模式...\r\n", delay_per_char=0.05)
        time.sleep(1.8)  # Dwell to let engine adopt Chinese mode
        key_frames.append(grab_real_screen())

        # Step 2: Switch to Window B -> Engine automatically maintains Chinese
        win_b.set_foreground()
        time.sleep(0.8)
        win_b.type_text("【視窗 B】切換至此視窗，ImeModePersistence 自動同步維持繁中模式！\r\n", delay_per_char=0.05)
        time.sleep(2.0)
        key_frames.append(grab_real_screen())

        # Step 3: Switch to English mode in Window B
        win_b.set_alphanumeric()
        time.sleep(0.3)
        win_b.type_text("【視窗 B】手動切換為英數模式 (Switch to English)\r\n", delay_per_char=0.04)
        time.sleep(1.8)  # Dwell to let engine adopt Alphanumeric mode
        key_frames.append(grab_real_screen())

        # Step 4: Switch back to Window A -> Engine restores English mode
        win_a.set_foreground()
        time.sleep(0.8)
        win_a.type_text("【視窗 A】切換回視窗 A，引擎自動還原為最新英數模式！\r\n", delay_per_char=0.05)
        time.sleep(2.0)
        key_frames.append(grab_real_screen())


        all_frames = recorder.stop()
        print(f"Recording finished! Total frames captured: {len(all_frames)}")

        for i, kf in enumerate(key_frames):
            kf.save(os.path.join(OUT, f"ime-frame-{i}.png"))

        # Save pristine 60 FPS H.264 MP4 video
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
                proc = subprocess.Popen(ffmpeg_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                for f in all_frames:
                    if f.size != (w, h):
                        f = f.resize((w, h), Image.Resampling.BILINEAR)
                    proc.stdin.write(f.tobytes())
                proc.stdin.close()
                proc.wait(timeout=30)
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
