"""Win32 & IME interop helpers for E2E and UI testing of ImeModePersistence.

Provides headless/automated creation of test windows with active IME contexts,
and native ctypes calls to query and manipulate IME status (open/conversion mode)
and keyboard layout (HKL) without simulating keyboard strokes.
"""

import ctypes
import subprocess
import threading
import time
from ctypes import wintypes

# Win32 Constants
WM_DESTROY = 0x0002
WM_SETFOCUS = 0x0007
WM_COMMAND = 0x0111
WM_IME_CONTROL = 0x0283
WM_INPUTLANGCHANGEREQUEST = 0x0050
WM_INPUTLANGCHANGE = 0x0051

IMC_GETCONVERSIONMODE = 0x0001
IMC_SETCONVERSIONMODE = 0x0002
IMC_GETOPENSTATUS = 0x0005
IMC_SETOPENSTATUS = 0x0006

# IME conversion modes
IME_CMODE_ALPHANUMERIC = 0x0000
IME_CMODE_NATIVE = 0x0001
IME_CMODE_FULLSHAPE = 0x0008

WS_OVERLAPPEDWINDOW = 0x00CF0000
WS_VISIBLE = 0x10000000
WS_CHILD = 0x40000000
WS_BORDER = 0x00800000
ES_AUTOHSCROLL = 0x0080
ES_MULTILINE = 0x0004

user32 = ctypes.windll.user32
imm32 = ctypes.windll.imm32
kernel32 = ctypes.windll.kernel32


def has_chinese_ime() -> bool:
    """Returns True if Microsoft Bopomofo or JhengHei IME is installed on this machine.

    Queries the Windows language list for zh-TW / zh-Hans entries and checks
    for the well-known Bopomofo IME GUID (B2F9C502) or JhengHei GUID (B115690A).
    This function is safe to call on non-Chinese machines; it returns False quickly.
    """
    try:
        result = subprocess.run(
            [
                "powershell", "-NonInteractive", "-Command",
                "(Get-WinUserLanguageList | "
                " Where-Object { $_.LanguageTag -like 'zh*' }).InputMethodTips "
                "-join ','",
            ],
            capture_output=True,
            text=True,
            timeout=15,
        )
        tips = result.stdout.strip()
        return "B2F9C502" in tips or "B115690A" in tips
    except Exception:
        return False


WNDPROC = ctypes.WINFUNCTYPE(
    wintypes.LPARAM, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
)


class WNDCLASSEXW(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.UINT),
        ("style", wintypes.UINT),
        ("lpfnWndProc", WNDPROC),
        ("cbClsExtra", ctypes.c_int),
        ("cbWndExtra", ctypes.c_int),
        ("hInstance", wintypes.HINSTANCE),
        ("hIcon", wintypes.HICON),
        ("hCursor", wintypes.HANDLE),
        ("hbrBackground", wintypes.HBRUSH),
        ("lpszMenuName", wintypes.LPCWSTR),
        ("lpszClassName", wintypes.LPCWSTR),
        ("hIconSm", wintypes.HICON),
    ]


kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HINSTANCE

kernel32.GetCurrentProcessId.restype = wintypes.DWORD
kernel32.GetCurrentThreadId.restype = wintypes.DWORD

user32.DefWindowProcW.argtypes = [
    wintypes.HWND,
    wintypes.UINT,
    wintypes.WPARAM,
    wintypes.LPARAM,
]
user32.DefWindowProcW.restype = wintypes.LPARAM

user32.RegisterClassExW.argtypes = [ctypes.POINTER(WNDCLASSEXW)]
user32.RegisterClassExW.restype = wintypes.ATOM

user32.CreateWindowExW.argtypes = [
    wintypes.DWORD,
    wintypes.LPCWSTR,
    wintypes.LPCWSTR,
    wintypes.DWORD,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.HWND,
    wintypes.HANDLE,
    wintypes.HINSTANCE,
    wintypes.LPVOID,
]
user32.CreateWindowExW.restype = wintypes.HWND

user32.DestroyWindow.argtypes = [wintypes.HWND]
user32.DestroyWindow.restype = wintypes.BOOL

user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.SetForegroundWindow.restype = wintypes.BOOL

user32.BringWindowToTop.argtypes = [wintypes.HWND]
user32.BringWindowToTop.restype = wintypes.BOOL

user32.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
user32.AttachThreadInput.restype = wintypes.BOOL

user32.GetForegroundWindow.restype = wintypes.HWND

user32.SendMessageW.argtypes = [
    wintypes.HWND,
    wintypes.UINT,
    wintypes.WPARAM,
    wintypes.LPARAM,
]
user32.SendMessageW.restype = wintypes.LPARAM

user32.PostMessageW.argtypes = [
    wintypes.HWND,
    wintypes.UINT,
    wintypes.WPARAM,
    wintypes.LPARAM,
]
user32.PostMessageW.restype = wintypes.BOOL

user32.UnregisterClassW.argtypes = [wintypes.LPCWSTR, wintypes.HINSTANCE]
user32.UnregisterClassW.restype = wintypes.BOOL

user32.PostQuitMessage.argtypes = [ctypes.c_int]
user32.PostQuitMessage.restype = None

user32.GetMessageW.argtypes = [
    ctypes.POINTER(wintypes.MSG),
    wintypes.HWND,
    wintypes.UINT,
    wintypes.UINT,
]
user32.GetMessageW.restype = wintypes.BOOL

user32.TranslateMessage.argtypes = [ctypes.POINTER(wintypes.MSG)]
user32.TranslateMessage.restype = wintypes.BOOL

user32.DispatchMessageW.argtypes = [ctypes.POINTER(wintypes.MSG)]
user32.DispatchMessageW.restype = wintypes.LPARAM

user32.GetKeyboardLayout.argtypes = [wintypes.DWORD]
user32.GetKeyboardLayout.restype = wintypes.HANDLE

user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD

imm32.ImmGetDefaultIMEWnd.argtypes = [wintypes.HWND]
imm32.ImmGetDefaultIMEWnd.restype = wintypes.HWND

imm32.ImmGetContext.argtypes = [wintypes.HWND]
imm32.ImmGetContext.restype = ctypes.c_void_p

imm32.ImmReleaseContext.argtypes = [wintypes.HWND, ctypes.c_void_p]
imm32.ImmReleaseContext.restype = wintypes.BOOL

imm32.ImmGetOpenStatus.argtypes = [ctypes.c_void_p]
imm32.ImmGetOpenStatus.restype = wintypes.BOOL

imm32.ImmSetOpenStatus.argtypes = [ctypes.c_void_p, wintypes.BOOL]
imm32.ImmSetOpenStatus.restype = wintypes.BOOL

imm32.ImmGetConversionStatus.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(wintypes.DWORD),
    ctypes.POINTER(wintypes.DWORD),
]
imm32.ImmGetConversionStatus.restype = wintypes.BOOL

imm32.ImmSetConversionStatus.argtypes = [
    ctypes.c_void_p,
    wintypes.DWORD,
    wintypes.DWORD,
]
imm32.ImmSetConversionStatus.restype = wintypes.BOOL


def get_window_thread_id(hwnd: int) -> int:
    """Returns the thread ID of the given window."""
    pid = wintypes.DWORD()
    return user32.GetWindowThreadProcessId(wintypes.HWND(hwnd), ctypes.byref(pid))


def get_keyboard_layout(hwnd: int) -> int:
    """Returns the HKL (as integer) of the given window's thread."""
    tid = get_window_thread_id(hwnd)
    return user32.GetKeyboardLayout(tid)


def get_ime_state(hwnd: int):
    """Reads the IME open status and conversion mode for a window.
    
    Returns (is_open: bool, conversion_mode: int).
    """
    ime_wnd = imm32.ImmGetDefaultIMEWnd(wintypes.HWND(hwnd))
    if ime_wnd:
        is_open = bool(user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0))
        conv_mode = int(user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0))
        return is_open, conv_mode

    # Direct context fallback
    himc = imm32.ImmGetContext(wintypes.HWND(hwnd))
    if himc:
        try:
            is_open = bool(imm32.ImmGetOpenStatus(himc))
            cm = wintypes.DWORD()
            sm = wintypes.DWORD()
            imm32.ImmGetConversionStatus(himc, ctypes.byref(cm), ctypes.byref(sm))
            return is_open, cm.value
        finally:
            imm32.ImmReleaseContext(wintypes.HWND(hwnd), himc)
    return False, 0


def set_ime_state(hwnd: int, is_open: bool, conversion_mode: int):
    """Sets the IME open status and conversion mode for a window."""
    ime_wnd = imm32.ImmGetDefaultIMEWnd(wintypes.HWND(hwnd))
    if ime_wnd:
        user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETOPENSTATUS, 1 if is_open else 0)
        user32.SendMessageW(ime_wnd, WM_IME_CONTROL, IMC_SETCONVERSIONMODE, conversion_mode)
        return

    himc = imm32.ImmGetContext(wintypes.HWND(hwnd))
    if himc:
        try:
            imm32.ImmSetOpenStatus(himc, 1 if is_open else 0)
            cm = wintypes.DWORD()
            sm = wintypes.DWORD()
            imm32.ImmGetConversionStatus(himc, ctypes.byref(cm), ctypes.byref(sm))
            imm32.ImmSetConversionStatus(himc, conversion_mode, sm.value)
        finally:
            imm32.ImmReleaseContext(wintypes.HWND(hwnd), himc)


@WNDPROC
def _test_window_wndproc(hwnd, msg, wparam, lparam):
    try:
        if msg == 0x0002:  # WM_DESTROY
            user32.PostQuitMessage(0)
            return 0
        if msg == 0x0006:  # WM_ACTIVATE
            if wparam & 0xFFFF != 0:  # WA_ACTIVE or WA_CLICKACTIVE
                user32.SetFocus(hwnd)
        return user32.DefWindowProcW(hwnd, msg, wparam, lparam)
    except Exception:
        return 0


_GLOBAL_WNDPROC = _test_window_wndproc


class ImeTestWindow:
    """Manages a live Win32 top-level window with an active Edit child control on an independent thread."""

    _class_registered = False
    _class_lock = threading.Lock()
    CLASS_NAME = "ImeTestWindowClass"

    def __init__(self, title: str = "IME Test Window"):
        self.title = title
        self.hwnd = None
        self.edit_hwnd = None
        self.thread = None
        self.ready_event = threading.Event()
        self.closed_event = threading.Event()
        self._wndproc_ref = None
        self._start_thread()

    def _start_thread(self):
        self.thread = threading.Thread(target=self._run_window, daemon=True)
        self.thread.start()
        if not self.ready_event.wait(timeout=10.0):
            raise TimeoutError(f"Test window '{self.title}' failed to initialize.")

    def _run_window(self):
        global _GLOBAL_WNDPROC
        hinst = kernel32.GetModuleHandleW(None)

        with ImeTestWindow._class_lock:
            if not ImeTestWindow._class_registered:
                wcex = WNDCLASSEXW()
                wcex.cbSize = ctypes.sizeof(WNDCLASSEXW)
                wcex.lpfnWndProc = _GLOBAL_WNDPROC
                wcex.hInstance = hinst
                wcex.lpszClassName = ImeTestWindow.CLASS_NAME
                user32.RegisterClassExW(ctypes.byref(wcex))
                ImeTestWindow._class_registered = True

        self.hwnd = user32.CreateWindowExW(
            0,
            ImeTestWindow.CLASS_NAME,
            self.title,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            100, 100, 400, 300,
            None, None, hinst, None
        )

        if not self.hwnd:
            self.ready_event.set()
            return

        self.edit_hwnd = self.hwnd
        user32.SetFocus(self.hwnd)
        self.ready_event.set()

        msg = wintypes.MSG()
        while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))

        self.closed_event.set()

    def set_foreground(self):
        """Brings the window to the foreground and focuses its edit control."""
        user32.keybd_event(0, 0, 0, 0)  # Standard Windows foreground lock bypass
        cur_thread = kernel32.GetCurrentThreadId()
        fg_wnd = user32.GetForegroundWindow()
        fg_thread = user32.GetWindowThreadProcessId(fg_wnd, None) if fg_wnd else 0
        target_thread = get_window_thread_id(self.hwnd)

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

        time.sleep(0.2)

    def is_foreground(self) -> bool:
        fg = user32.GetForegroundWindow()
        if fg == self.hwnd or fg == self.edit_hwnd:
            return True
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(fg, ctypes.byref(pid))
        return pid.value == kernel32.GetCurrentProcessId()

    def get_ime_state(self):
        return get_ime_state(self.edit_hwnd or self.hwnd)

    def set_ime_state(self, is_open: bool, conversion_mode: int):
        set_ime_state(self.edit_hwnd or self.hwnd, is_open, conversion_mode)

    def get_keyboard_layout(self) -> int:
        return get_keyboard_layout(self.hwnd)

    def destroy(self):
        if self.hwnd:
            user32.PostMessageW(self.hwnd, 0x0010, 0, 0)  # WM_CLOSE
            self.closed_event.wait(timeout=2.0)
            if self.thread and self.thread.is_alive():
                self.thread.join(timeout=2.0)
            self.hwnd = None
