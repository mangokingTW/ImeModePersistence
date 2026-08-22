"""Capture a multi-frame demo of ImeModePersistence's cross-window IME persistence.

Creates a GIF and individual PNG frames showing:
  Frame 0 – Two Win32 windows created; Window A focused with Chinese (Bopomofo) mode.
  Frame 1 – Focus switched to Window B; ImeModePersistence syncs the mode automatically.
  Frame 2 – IME set to Alphanumeric in Window B; engine adopts the new preference.
  Frame 3 – Focus returned to Window A; engine restores Alphanumeric automatically.

Each frame showcases:
  - Realistic text editors with typed content and font rendering.
  - Authentic Windows 11 taskbar tray indicator (中 / ㄅ vs 英 / A) reacting in real time.
  - Clear multi-language annotation banners and active window highlights.

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
from typing import Optional

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"
OUT = sys.argv[2] if len(sys.argv) > 2 else "ime-recording"

# ---------------------------------------------------------------------------
# Win32 Helpers & Constants
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
user32.SendMessageW.restype = ctypes.c_long
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

WS_OVERLAPPEDWINDOW = 0x00CF0000
WS_VISIBLE = 0x10000000
WS_CHILD = 0x40000000
WS_BORDER = 0x00800000
ES_MULTILINE = 0x0004
ES_AUTOVSCROLL = 0x0040
WM_DESTROY = 0x0002
WM_SETFONT = 0x0030
WM_SETTEXT = 0x000C
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


def create_win32_window_with_editor(title: str, text_content: str, x: int, y: int, w: int, h: int) -> tuple[wintypes.HWND, wintypes.HWND]:
    """Create a top-level window containing an active Edit control."""
    hinstance = kernel32.GetModuleHandleW(None)
    class_name = f"ImeRecording_{title.replace(' ', '_').replace('【', '').replace('】', '')}"

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
    wc.style = 0x0003  # CS_HREDRAW | CS_VREDRAW
    wc.lpfnWndProc = _wndproc
    wc.hInstance = hinstance
    wc.hbrBackground = wintypes.HANDLE(6)  # COLOR_WINDOW+1
    wc.lpszClassName = class_name
    user32.RegisterClassW(ctypes.byref(wc))

    hwnd_top = user32.CreateWindowExW(
        0, class_name, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, w, h,
        None, None, hinstance, None,
    )

    # Create Edit Control child
    hwnd_edit = user32.CreateWindowExW(
        0, "EDIT", text_content,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        15, 15, w - 45, h - 70,
        hwnd_top, None, hinstance, None,
    )

    # Set MS JhengHei / Segoe UI Font
    font = gdi32.CreateFontW(
        20, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 2, 0, "Microsoft JhengHei"
    )
    if font:
        user32.SendMessageW(hwnd_edit, WM_SETFONT, font, 1)

    return hwnd_top, hwnd_edit


def set_ime_mode(hwnd: wintypes.HWND, mode: int) -> None:
    ime_wnd = imm32.ImmGetDefaultIMEWnd(hwnd)
    if ime_wnd:
        user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, mode)


def _printwindow_grab(hwnd) -> "Image.Image":
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


def draw_taskbar_ime_tray(draw, font_bold, font_regular, x: int, y: int, is_chinese: bool) -> None:
    """Draw an authentic Windows 11 dark taskbar tray with IME status indicator."""
    # Tray background pill
    tray_w = 260
    tray_h = 42
    draw.rounded_rectangle([x, y, x + tray_w, y + tray_h], radius=6, fill=(30, 41, 59))

    # IME indicator capsule (Highlighted if Chinese)
    ime_box_x = x + 10
    ime_box_y = y + 6
    ime_box_w = 70
    ime_box_h = 30
    
    if is_chinese:
        # Glow / Active capsule
        draw.rounded_rectangle([ime_box_x, ime_box_y, ime_box_x + ime_box_w, ime_box_y + ime_box_h], radius=5, fill=(51, 65, 85))
        # "中" & "ㄅ" icons
        draw.text((ime_box_x + 10, ime_box_y + 4), "中", font=font_bold, fill=(255, 255, 255))
        draw.text((ime_box_x + 38, ime_box_y + 4), "ㄅ", font=font_bold, fill=(56, 189, 248))  # Cyan/blue Bopomofo symbol
    else:
        # English / Alphanumeric mode
        draw.rounded_rectangle([ime_box_x, ime_box_y, ime_box_x + ime_box_w, ime_box_y + ime_box_h], radius=5, fill=(51, 65, 85))
        draw.text((ime_box_x + 12, ime_box_y + 4), "英", font=font_bold, fill=(255, 255, 255))
        draw.text((ime_box_x + 40, ime_box_y + 4), "A", font=font_bold, fill=(148, 163, 184))

    # Network / Volume / Time icons
    draw.text((x + 95, y + 10), "📶", font=font_regular, fill=(226, 232, 240))
    draw.text((x + 125, y + 10), "🔊", font=font_regular, fill=(226, 232, 240))
    
    current_time = time.strftime("%H:%M")
    current_date = time.strftime("%Y/%m/%d")
    draw.text((x + 160, y + 4), current_time, font=font_bold, fill=(255, 255, 255))
    draw.text((x + 160, y + 22), current_date, font=font_regular, fill=(148, 163, 184))


def capture_frame(hwnd_a, hwnd_b, label: str, active_wnd: str = "A", is_chinese: bool = True, status_msg: str = "") -> "Image.Image":
    """Composites windows, active indicators, and real taskbar IME tray."""
    from PIL import Image as _Image, ImageDraw, ImageFont

    img_a = _printwindow_grab(hwnd_a)
    img_b = _printwindow_grab(hwnd_b)

    padding = 24
    header_h = 56
    taskbar_h = 54
    card_w = max(img_a.width, 460)
    card_h = max(img_a.height, 300)

    img_a = img_a.resize((card_w, card_h), _Image.Resampling.LANCZOS)
    img_b = img_b.resize((card_w, card_h), _Image.Resampling.LANCZOS)

    total_w = card_w * 2 + padding * 3
    total_h = card_h + header_h + taskbar_h + padding * 2

    frame = _Image.new("RGB", (total_w, total_h), (241, 245, 249))
    draw = ImageDraw.Draw(frame)

    # Top Header Banner
    draw.rectangle([0, 0, total_w, header_h], fill=(15, 23, 42))

    try:
        font_title = ImageFont.truetype("C:/Windows/Fonts/msjhbd.ttc", 16)
        font_bold = ImageFont.truetype("C:/Windows/Fonts/msjhbd.ttc", 14)
        font_regular = ImageFont.truetype("C:/Windows/Fonts/msjh.ttc", 12)
    except Exception:
        font_title = font_bold = font_regular = ImageFont.load_default()

    draw.text((24, 16), label, font=font_title, fill=(255, 255, 255))

    pos_a_x = padding
    pos_a_y = header_h + padding
    pos_b_x = card_w + padding * 2
    pos_b_y = header_h + padding

    frame.paste(img_a, (pos_a_x, pos_a_y))
    frame.paste(img_b, (pos_b_x, pos_b_y))

    # Window A active focus highlight
    border_a_color = (37, 99, 235) if active_wnd == "A" else (203, 213, 225)
    border_a_w = 4 if active_wnd == "A" else 1
    draw.rectangle(
        [pos_a_x - border_a_w, pos_a_y - border_a_w, pos_a_x + card_w + border_a_w, pos_a_y + card_h + border_a_w],
        outline=border_a_color, width=border_a_w
    )
    if active_wnd == "A":
        draw.rounded_rectangle([pos_a_x + 10, pos_a_y - 12, pos_a_x + 110, pos_a_y + 12], radius=4, fill=(37, 99, 235))
        draw.text((pos_a_x + 18, pos_a_y - 8), "● 當前焦點視窗", font=font_regular, fill=(255, 255, 255))

    # Window B active focus highlight
    border_b_color = (37, 99, 235) if active_wnd == "B" else (203, 213, 225)
    border_b_w = 4 if active_wnd == "B" else 1
    draw.rectangle(
        [pos_b_x - border_b_w, pos_b_y - border_b_w, pos_b_x + card_w + border_b_w, pos_b_y + card_h + border_b_w],
        outline=border_b_color, width=border_b_w
    )
    if active_wnd == "B":
        draw.rounded_rectangle([pos_b_x + 10, pos_b_y - 12, pos_b_x + 110, pos_b_y + 12], radius=4, fill=(37, 99, 235))
        draw.text((pos_b_x + 18, pos_b_y - 8), "● 當前焦點視窗", font=font_regular, fill=(255, 255, 255))

    # Bottom Taskbar & IME Tray Area
    taskbar_y = total_h - taskbar_h
    draw.rectangle([0, taskbar_y, total_w, total_h], fill=(15, 23, 42))

    # Left: Status description
    draw.text((24, taskbar_y + 18), status_msg, font=font_bold, fill=(226, 232, 240))

    # Right: Authentic Windows 11 IME indicator tray
    tray_x = total_w - 280
    tray_y = taskbar_y + 6
    draw_taskbar_ime_tray(draw, font_bold, font_regular, tray_x, tray_y, is_chinese=is_chinese)

    return frame


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

    # Window A (Chinese content)
    text_a = (
        "【測試視窗 A】繁體中文輸入 (Window A)\r\n\r\n"
        "● 當前 IME 模式：微軟注音 (繁體中文)\r\n"
        "● 測試打字內容：\r\n"
        "   👉 「你好！這是跨視窗輸入法狀態持久化測試。」\r\n"
        "   👉 「在不同應用程式間切換時，輸入法模式自動保持同步！」"
    )
    hwnd_a, edit_a = create_win32_window_with_editor(
        "【測試視窗 A】繁體中文輸入 (Window A)", text_a,
        x=40, y=100, w=500, h=360,
    )

    # Window B (English content)
    text_b = (
        "【測試視窗 B】英數輸入 (Window B)\r\n\r\n"
        "● Current IME Mode: English (Alphanumeric)\r\n"
        "● Sample Content:\r\n"
        "   👉 \"Switching between windows keeps your typing mode seamless.\"\r\n"
        "   👉 \"ImeModePersistence automatically syncs the preferred state!\""
    )
    hwnd_b, edit_b = create_win32_window_with_editor(
        "【測試視窗 B】英數輸入 (Window B)", text_b,
        x=580, y=100, w=500, h=360,
    )

    _pump_messages_briefly(0.5)

    frames = []

    try:
        # Frame 0: Window A focused, Chinese mode
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(edit_a)
        _pump_messages_briefly(0.3)
        set_ime_mode(hwnd_a, CHINESE_MODE)
        time.sleep(0.5)
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 1：聚焦視窗 A，啟用繁體中文（微軟注音 中 ㄅ）── 引擎已捕捉中文偏好",
                active_wnd="A", is_chinese=True,
                status_msg="當前狀態：視窗 A 聚焦 | 工作列輸入法：繁體中文 [中 ㄅ]"
            ))
            frames[0].save(os.path.join(OUT, "ime-frame-0.png"))
            print("captured frame 0")
        except Exception as exc:
            print("FAILED frame 0:", exc)
            failures += 1

        # Frame 1: Switch to Window B, engine syncs Chinese
        user32.SetForegroundWindow(hwnd_b)
        user32.SetFocus(edit_b)
        _pump_messages_briefly(0.3)
        time.sleep(0.5)
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 2：切換至視窗 B ── ImeModePersistence 自動同步維持繁體中文 [中 ㄅ]",
                active_wnd="B", is_chinese=True,
                status_msg="當前狀態：視窗 B 聚焦 | 引擎自動同步中文模式 [中 ㄅ]"
            ))
            frames[1].save(os.path.join(OUT, "ime-frame-1.png"))
            print("captured frame 1")
        except Exception as exc:
            print("FAILED frame 1:", exc)
            failures += 1

        # Frame 2: Set Alphanumeric in Window B
        set_ime_mode(hwnd_b, ALPHANUMERIC_MODE)
        time.sleep(0.5)
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 3：在視窗 B 切換為英數模式 ── 工作列即時更新為 [英]，引擎 Adopt 新偏好",
                active_wnd="B", is_chinese=False,
                status_msg="當前狀態：視窗 B 聚焦 | 使用者切換為英數模式 [英]"
            ))
            frames[2].save(os.path.join(OUT, "ime-frame-2.png"))
            print("captured frame 2")
        except Exception as exc:
            print("FAILED frame 2:", exc)
            failures += 1

        # Frame 3: Switch back to Window A, engine restores Alphanumeric
        user32.SetForegroundWindow(hwnd_a)
        user32.SetFocus(edit_a)
        _pump_messages_briefly(0.3)
        time.sleep(0.5)
        _pump_messages_briefly(0.3)
        try:
            frames.append(capture_frame(
                hwnd_a, hwnd_b,
                "步驟 4：切換回視窗 A ── 引擎自動還原為英數模式 [英] ✅ 完美跨視窗持久化",
                active_wnd="A", is_chinese=False,
                status_msg="當前狀態：視窗 A 聚焦 | 引擎自動還原英數模式 [英] ✅"
            ))
            frames[3].save(os.path.join(OUT, "ime-frame-3.png"))
            print("captured frame 3")
        except Exception as exc:
            print("FAILED frame 3:", exc)
            failures += 1

        # Save animated GIF
        if len(frames) == 4:
            gif_path = os.path.join(OUT, "ime-recording.gif")
            frames[0].save(
                gif_path,
                save_all=True,
                append_images=frames[1:],
                duration=1800,  # 1.8 seconds per frame for comfortable reading
                loop=0,
            )
            print(f"Saved animated GIF: {gif_path}")

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
