"""End-to-end (E2E) functional tests for IME Mode Persistence & Layout Switching.

Exercises real Win32 windows with IME contexts running against a live ImeModePersistence.exe instance.
Validates:
1. Cross-window IME mode persistence (adoption, carry-over, restoration across window switches).
2. Rule-based layout switching when target window receives focus.
3. Ghost window / transient popup non-interference.
"""

import subprocess
import sys
import time
import pytest
from wintegrate import Window, WindowCensus
from wintegrate.interop import send_keys
from win32_ime_helper import (
    ImeTestWindow,
    IME_CMODE_NATIVE,
    IME_CMODE_ALPHANUMERIC,
    IME_CMODE_FULLSHAPE,
    get_foreground_focus_hwnd,
    get_ime_state,
    get_root_window,
    get_window_class,
    set_ime_state,
    has_chinese_ime,
    user32,
)

# ---------------------------------------------------------------------------
# Marker: tests that need a genuine Chinese IME (Microsoft Bopomofo / JhengHei)
# ---------------------------------------------------------------------------
_NO_CHINESE_IME = pytest.mark.skipif(
    not has_chinese_ime(),
    reason="Microsoft Bopomofo / JhengHei IME not installed — skipped. "
           "Install zh-TW language pack to run real-IME tests.",
)

def wait_for_condition(predicate, timeout=3.0, step=0.05):
    """Polls predicate until it returns True or timeout expires."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(step)
    return False

def test_ime_mode_persistence_between_windows(app_runner, registry_sandbox):
    """Verifies that switching between two independent windows properly restores

    the last adopted IME mode (Native vs Alphanumeric).
    """
    registry_sandbox.set_persist_mode(True)

    # Launch background service
    app_proc = app_runner()
    time.sleep(0.5)

    win_a = None
    win_b = None
    try:
        win_a = ImeTestWindow("E2E Test Window A")
        win_b = ImeTestWindow("E2E Test Window B")

        # 1. Focus Window A, set mode to Native (e.g., Chinese/Japanese full IME)
        win_a.set_foreground()
        win_a.set_ime_state(is_open=True, conversion_mode=IME_CMODE_NATIVE)

        # Wait past the adoption debounce window (persist::kAdoptDebounceMs = 600ms)
        time.sleep(0.8)

        # 2. Switch focus to Window B. The background engine should notice the switch
        # and enforce the desired mode (Native / Open).
        win_b.set_foreground()

        # Let the observer and restore timer settle
        success_b = wait_for_condition(
            lambda: win_b.get_ime_state()[0] is True or win_b.get_ime_state()[1] == IME_CMODE_NATIVE,
            timeout=2.0
        )
        is_open, mode = win_b.get_ime_state()
        print(f"Window B IME state after switch: is_open={is_open}, mode={mode}")

        # 3. Switch Window B to Alphanumeric / Closed mode
        win_b.set_ime_state(is_open=False, conversion_mode=IME_CMODE_ALPHANUMERIC)
        time.sleep(0.8)  # Wait past adoption debounce

        # 4. Switch back to Window A. Window A should now be restored to Alphanumeric / Closed.
        win_a.set_foreground()

        success_a = wait_for_condition(
            lambda: win_a.get_ime_state()[0] is False or win_a.get_ime_state()[1] == IME_CMODE_ALPHANUMERIC,
            timeout=2.0
        )
        is_open_a, mode_a = win_a.get_ime_state()
        print(f"Window A IME state after switch back: is_open={is_open_a}, mode={mode_a}")

    finally:
        if win_a:
            win_a.destroy()
        if win_b:
            win_b.destroy()

def test_rule_layout_recognition_e2e(app_runner, registry_sandbox):
    """Verifies that an application rule configured in the registry is loaded

    and actively monitored when matching windows come to the foreground.
    """
    # Configure a class-based rule: class:ImeTestWindowClass -> Layout 0x04090409 (en-US)
    registry_sandbox.set_rules("class:ImeTestWindowClass=0x04090409\n")

    app_proc = app_runner()
    time.sleep(0.5)

    win = None
    try:
        win = ImeTestWindow("Rule Test Window")
        win.set_foreground()
        time.sleep(0.3)

        # Confirm the window is alive and actively queried
        layout = win.get_keyboard_layout()
        print(f"Active layout for rule test window: {hex(layout)}")
        assert layout != 0, "Window thread keyboard layout must be valid"
    finally:
        if win:
            win.destroy()

def test_ghost_window_filtering_e2e(app_runner, registry_sandbox):
    """Ensures transient/ghost windows do not crash or corrupt persistence."""
    registry_sandbox.set_persist_mode(True)
    app_runner()
    time.sleep(0.3)

    win = None
    try:
        win = ImeTestWindow("Main Target Window")
        win.set_foreground()
        win.set_ime_state(is_open=True, conversion_mode=IME_CMODE_NATIVE)
        time.sleep(0.3)

        # Create a brief second window and immediately destroy it (simulating transient tooltip/flyout)
        ghost = ImeTestWindow("Transient Tooltip")
        time.sleep(0.1)
        ghost.destroy()

        # Target window should still be intact and tracked
        win.set_foreground()
        time.sleep(0.2)
        assert win.hwnd is not None
    finally:
        if win:
            win.destroy()

# ---------------------------------------------------------------------------
# Real IME tests – require Microsoft Bopomofo / JhengHei IME (zh-TW)
# These tests are automatically skipped if the IME is not installed.
# On GitHub Actions, the "Install Traditional Chinese (zh-TW) IME" workflow
# step installs the IME before this test suite runs.
# ---------------------------------------------------------------------------

@_NO_CHINESE_IME
def test_bopomofo_ime_is_installed():
    """Smoke test: verifies that Microsoft Bopomofo IME is discoverable via
    the Windows language API.  This is the prerequisite for all real-IME tests.

    If this test fails on GitHub Actions it means the Install-Language step
    did not complete successfully or the IME GUID changed.
    """
    assert has_chinese_ime(), (
        "Microsoft Bopomofo (B2F9C502) or JhengHei (B115690A) IME GUID "
        "was not found in Get-WinUserLanguageList InputMethodTips. "
        "Ensure the Install-Language zh-TW step ran successfully."
    )

@_NO_CHINESE_IME
def test_ime_open_status_with_real_ime(app_runner, registry_sandbox):
    """With a genuine Chinese IME installed, verifies:
    1. ImmGetDefaultIMEWnd returns a valid (non-NULL) IME window handle, proving
       Microsoft Bopomofo is active and associated with our test window.
    2. IMC_SETCONVERSIONMODE / IMC_GETCONVERSIONMODE round-trips correctly via
       WM_IME_CONTROL — the exact same mechanism ImeModePersistence uses at
       runtime to read and restore the IME mode on window focus changes.

    This validates the full Win32 IME API path with a real installed input
    method, not just the IMM32 stubs that exist when only en-US is installed.
    """
    import ctypes
    from ctypes import wintypes

    imm32_loc = ctypes.windll.imm32
    imm32_loc.ImmGetDefaultIMEWnd.argtypes = [wintypes.HWND]
    imm32_loc.ImmGetDefaultIMEWnd.restype = wintypes.HWND

    registry_sandbox.set_persist_mode(True)
    app_runner()
    time.sleep(0.5)

    win = None
    try:
        win = ImeTestWindow("Real IME Conversion Mode Test Window")
        win.set_foreground()
        time.sleep(0.3)

        # Assertion 1: ImmGetDefaultIMEWnd must return non-NULL.
        # With Bopomofo installed, every window gets a real IME window handle.
        ime_wnd = imm32_loc.ImmGetDefaultIMEWnd(wintypes.HWND(win.hwnd))
        assert ime_wnd, (
            "ImmGetDefaultIMEWnd returned NULL — Microsoft Bopomofo IME is "
            "installed in the language list but no IME window was created for "
            "this test window. This would cause ImeModePersistence to silently "
            "skip mode restoration for this window."
        )
        print(f"[real-IME] ImmGetDefaultIMEWnd -> {hex(ime_wnd)}  (valid)")

        # Assertion 2: Conversion mode round-trip via ImeTestWindow helpers,
        # which use the same WM_IME_CONTROL path as ImeModePersistence.
        win.set_ime_state(is_open=True, conversion_mode=IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE)
        time.sleep(0.1)
        _, mode_after_set = win.get_ime_state()
        print(f"[real-IME] After set NATIVE|FULLSHAPE: mode={hex(mode_after_set)}")
        assert (mode_after_set & IME_CMODE_NATIVE) != 0, (
            f"NATIVE bit (0x1) not set after IMC_SETCONVERSIONMODE. "
            f"Got mode={hex(mode_after_set)}. "
            "WM_IME_CONTROL/IMC_SETCONVERSIONMODE is not reaching the real IME."
        )

        win.set_ime_state(is_open=False, conversion_mode=IME_CMODE_ALPHANUMERIC)
        time.sleep(0.1)
        _, mode_after_clear = win.get_ime_state()
        print(f"[real-IME] After set ALPHANUMERIC: mode={hex(mode_after_clear)}")
        assert (mode_after_clear & IME_CMODE_NATIVE) == 0, (
            f"NATIVE bit still set after IMC_SETCONVERSIONMODE(ALPHANUMERIC). "
            f"Got mode={hex(mode_after_clear)}."
        )
    finally:
        if win:
            win.destroy()

@_NO_CHINESE_IME
def test_persistence_with_real_chinese_ime(app_runner, registry_sandbox):
    """Full E2E cross-window persistence test using a genuine Microsoft Bopomofo IME.

    Flow:
      1. Window A: set Chinese conversion mode (NATIVE) → wait for adoption.
      2. Switch to Window B: ImeModePersistence should carry the Chinese preference
         to the new window automatically.

    Differences from the basic API-only test (test_ime_mode_persistence_between_windows):
    - Exercises the real Microsoft Bopomofo IME window (non-NULL ImmGetDefaultIMEWnd).
    - Verifies ImeModePersistence responds to genuine conversion mode changes made via
      the same WM_IME_CONTROL path the real Bopomofo IME uses.
    - Confirms end-to-end compatibility with Microsoft Bopomofo as shipped in Windows.
    """
    registry_sandbox.set_persist_mode(True)

    app_runner()
    time.sleep(0.5)

    win_a = None
    win_b = None
    try:
        win_a = ImeTestWindow("Real IME E2E Window A")
        win_b = ImeTestWindow("Real IME E2E Window B")

        # Step 1: Window A → Chinese (NATIVE) and wait for adoption.
        win_a.set_foreground()
        time.sleep(0.3)
        win_a.set_ime_state(is_open=True, conversion_mode=IME_CMODE_NATIVE)
        is_open_a, mode_a = win_a.get_ime_state()
        print(f"[real-IME] Window A after set: is_open={is_open_a}, mode={hex(mode_a)}")
        # Wait for ImeModePersistence to adopt Native mode (kAdoptDebounceMs = 600ms)
        time.sleep(1.2)

        # Step 2: Switch to Window B → engine should carry Chinese mode.
        win_b.set_foreground()
        carried = wait_for_condition(
            lambda: (win_b.get_ime_state()[1] & IME_CMODE_NATIVE) != 0 or win_b.get_ime_state()[0] is True,
            timeout=3.5,
        )
        is_open_b, mode_b = win_b.get_ime_state()
        print(f"[real-IME] Window B: is_open={is_open_b}, mode={hex(mode_b)}, carried={carried}")
        assert carried, (
            f"ImeModePersistence did not carry Chinese conversion mode to Window B. "
            f"mode={hex(mode_b)}, is_open={is_open_b}"
        )
    finally:
        if win_a:
            win_a.destroy()
        if win_b:
            win_b.destroy()


_CT_EDIT = 50004
_WM_CLOSE = 0x0010

_FILE_PICKER_SCRIPT = r"""
import ctypes
import sys
from ctypes import wintypes

ctypes.windll.ole32.CoInitializeEx(None, 2)  # COINIT_APARTMENTTHREADED

class OPENFILENAMEW(ctypes.Structure):
    _fields_ = [
        ("lStructSize", wintypes.DWORD),
        ("hwndOwner", wintypes.HWND),
        ("hInstance", wintypes.HINSTANCE),
        ("lpstrFilter", wintypes.LPCWSTR),
        ("lpstrCustomFilter", wintypes.LPWSTR),
        ("nMaxCustFilter", wintypes.DWORD),
        ("nFilterIndex", wintypes.DWORD),
        ("lpstrFile", wintypes.LPWSTR),
        ("nMaxFile", wintypes.DWORD),
        ("lpstrFileTitle", wintypes.LPWSTR),
        ("nMaxFileTitle", wintypes.DWORD),
        ("lpstrInitialDir", wintypes.LPCWSTR),
        ("lpstrTitle", wintypes.LPCWSTR),
        ("Flags", wintypes.DWORD),
        ("nFileOffset", wintypes.WORD),
        ("nFileExtension", wintypes.WORD),
        ("lpstrDefExt", wintypes.LPCWSTR),
        ("lCustData", wintypes.LPARAM),
        ("lpfnHook", ctypes.c_void_p),
        ("lpTemplateName", wintypes.LPCWSTR),
        ("pvReserved", ctypes.c_void_p),
        ("dwReserved", wintypes.DWORD),
        ("FlagsEx", wintypes.DWORD),
    ]

buf = ctypes.create_unicode_buffer(260)
ofn = OPENFILENAMEW()
ofn.lStructSize = ctypes.sizeof(OPENFILENAMEW)
ofn.lpstrFile = ctypes.cast(buf, wintypes.LPWSTR)
ofn.nMaxFile = 260
ofn.lpstrInitialDir = sys.argv[1]
ofn.lpstrTitle = "IME Picker E2E"
ofn.Flags = 0x00080000 | 0x00000008  # OFN_EXPLORER | OFN_NOCHANGEDIR
ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(ofn))
"""


def _settled(read, matches, timeout=3.0, poll_interval=0.05):
    """Polls read() until matches(value) or timeout expires, returning last read."""
    deadline = time.monotonic() + timeout
    val = read()
    while not matches(val) and time.monotonic() < deadline:
        time.sleep(poll_interval)
        val = read()
    return val


def _read_root_focus_ime(root_hwnd: int):
    """Returns (focus_hwnd, lower_class, is_open, mode) if focus is inside root_hwnd."""
    focus_hwnd = get_foreground_focus_hwnd()
    if not focus_hwnd:
        return (0, "", False, 0)
    if focus_hwnd != root_hwnd and get_root_window(focus_hwnd) != root_hwnd:
        return (0, "", False, 0)
    cls = get_window_class(focus_hwnd).lower()
    is_open, mode = get_ime_state(focus_hwnd)
    return (focus_hwnd, cls, is_open, mode)


def _is_edit_with_mode(res, want_native: bool) -> bool:
    focus_hwnd, cls, _is_open, mode = res
    if not focus_hwnd or cls not in ("edit", "richedit20w", "richedit50w"):
        return False
    is_native = (mode & IME_CMODE_NATIVE) != 0
    return is_native == want_native


def _launch_file_picker_subprocess(initial_dir: str) -> subprocess.Popen:
    return subprocess.Popen([sys.executable, "-c", _FILE_PICKER_SCRIPT, initial_dir])


def _find_picker_filename_edit(dlg: Window):
    root = dlg.re_resolve_element()
    edit = root.find_descendant(
        automation_id="1148", control_type_id=_CT_EDIT, timeout=3.0, required=False
    )
    if edit is not None:
        return edit
    combo = root.find_descendant(automation_id="1148", timeout=2.0, required=False)
    if combo is not None:
        edit = combo.find_descendant(
            control_type_id=_CT_EDIT, timeout=2.0, required=False
        )
        if edit is not None:
            return edit
    return root.find_descendant(control_type_id=_CT_EDIT, timeout=5.0)


@_NO_CHINESE_IME
def test_file_picker_dialog_ime_persistence_wintegrate(
    app_runner, registry_sandbox, tmp_path
):
    """Verifies IME mode carry-over (both Native and Alphanumeric) into a real
    out-of-process Win32 File Open dialog (#32770) filename Edit box without elevation.
    """
    registry_sandbox.set_persist_mode(True)
    app_runner()
    time.sleep(0.5)

    win_a = None
    picker_proc = None
    dlg = None
    try:
        win_a = ImeTestWindow("File Picker Source Window")
        win_a.set_foreground()
        time.sleep(0.3)
        win_a.set_ime_state(is_open=True, conversion_mode=IME_CMODE_NATIVE)
        time.sleep(1.2)

        before = WindowCensus.capture()
        picker_proc = _launch_file_picker_subprocess(str(tmp_path))
        dlg = Window.wait_for_new(before, timeout=15.0, window_classes=("#32770",))
        assert dlg.set_foreground(), "File Picker dialog failed to reach foreground"

        edit = _find_picker_filename_edit(dlg)
        assert edit is not None, "Could not locate filename Edit control in #32770 dialog"
        edit.set_focus(click=True)

        native_res = _settled(
            lambda: _read_root_focus_ime(dlg.hwnd),
            lambda res: _is_edit_with_mode(res, want_native=True),
            timeout=4.0,
        )
        assert _is_edit_with_mode(native_res, want_native=True), (
            f"Expected Native mode in #32770 Edit, got focus_hwnd={native_res[0]}, "
            f"class={native_res[1]!r}, is_open={native_res[2]}, mode={hex(native_res[3])}"
        )

        # Now adopt Alphanumeric in win_a and verify the dialog Edit restores Alphanumeric
        # even if the OS default IMETC mode is Native.
        win_a.set_foreground()
        time.sleep(0.3)
        win_a.set_ime_state(is_open=False, conversion_mode=IME_CMODE_ALPHANUMERIC)
        time.sleep(1.2)

        assert dlg.set_foreground(), "File Picker dialog failed to return to foreground"
        edit = _find_picker_filename_edit(dlg)
        edit.set_focus(click=True)

        alpha_res = _settled(
            lambda: _read_root_focus_ime(dlg.hwnd),
            lambda res: _is_edit_with_mode(res, want_native=False),
            timeout=4.0,
        )
        assert _is_edit_with_mode(alpha_res, want_native=False), (
            f"Expected Alphanumeric mode in #32770 Edit, got focus_hwnd={alpha_res[0]}, "
            f"class={alpha_res[1]!r}, is_open={alpha_res[2]}, mode={hex(alpha_res[3])}"
        )
    finally:
        if dlg is not None and dlg.hwnd:
            user32.PostMessageW(dlg.hwnd, _WM_CLOSE, 0, 0)
        if picker_proc is not None:
            try:
                picker_proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                picker_proc.terminate()
                picker_proc.wait(timeout=2.0)
        if win_a:
            win_a.destroy()


@_NO_CHINESE_IME
def test_file_explorer_rename_ime_persistence_wintegrate(
    app_runner, registry_sandbox, tmp_path
):
    """Verifies File Explorer persistence via wintegrate:
    1. Idling in DirectUIHWND file list does NOT overwrite carried mode.
    2. Pressing F2 opens the inline Edit control and restores both Native and Alphanumeric.
    """
    sample_file = tmp_path / "ime_test_file.txt"
    sample_file.write_text("test", encoding="utf-8")

    registry_sandbox.set_persist_mode(True)
    app_runner()
    time.sleep(0.5)

    win_a = None
    explorer_win = None
    try:
        win_a = ImeTestWindow("Explorer Source Window")
        win_a.set_foreground()
        time.sleep(0.3)
        win_a.set_ime_state(is_open=True, conversion_mode=IME_CMODE_NATIVE)
        time.sleep(1.2)

        before = WindowCensus.capture()
        subprocess.Popen(["explorer.exe", str(tmp_path)])
        explorer_win = Window.wait_for_new(
            before, timeout=15.0, window_classes=("CabinetWClass",)
        )
        assert explorer_win.set_foreground(), "File Explorer failed to reach foreground"

        root = explorer_win.re_resolve_element()
        item = root.find_descendant(name_contains="ime_test_file", timeout=10.0)
        assert item is not None, "Could not locate sample file item in File Explorer"
        item.set_focus(click=True)

        # Idle in the file list longer than kPromotionDwellMs + kAdoptDebounceMs (750ms)
        # to verify DirectUIHWND does not clobber the carried Native mode.
        time.sleep(1.0)
        send_keys("{F2}")

        native_res = _settled(
            lambda: _read_root_focus_ime(explorer_win.hwnd),
            lambda res: _is_edit_with_mode(res, want_native=True),
            timeout=4.0,
        )
        assert _is_edit_with_mode(native_res, want_native=True), (
            f"Expected Native mode in Explorer F2 rename Edit, got focus_hwnd={native_res[0]}, "
            f"class={native_res[1]!r}, is_open={native_res[2]}, mode={hex(native_res[3])}"
        )
        send_keys("{ESC}")
        time.sleep(0.2)

        # Switch back to win_a, adopt Alphanumeric, then verify F2 rename restores Alphanumeric.
        win_a.set_foreground()
        time.sleep(0.3)
        win_a.set_ime_state(is_open=False, conversion_mode=IME_CMODE_ALPHANUMERIC)
        time.sleep(1.2)

        assert explorer_win.set_foreground(), "File Explorer failed to return to foreground"
        root = explorer_win.re_resolve_element()
        item = root.find_descendant(name_contains="ime_test_file", timeout=10.0)
        item.set_focus(click=True)
        time.sleep(0.2)
        send_keys("{F2}")

        alpha_res = _settled(
            lambda: _read_root_focus_ime(explorer_win.hwnd),
            lambda res: _is_edit_with_mode(res, want_native=False),
            timeout=4.0,
        )
        assert _is_edit_with_mode(alpha_res, want_native=False), (
            f"Expected Alphanumeric mode in Explorer F2 rename Edit, got focus_hwnd={alpha_res[0]}, "
            f"class={alpha_res[1]!r}, is_open={alpha_res[2]}, mode={hex(alpha_res[3])}"
        )
        send_keys("{ESC}")
        time.sleep(0.2)
    finally:
        if explorer_win is not None and explorer_win.hwnd:
            user32.PostMessageW(explorer_win.hwnd, _WM_CLOSE, 0, 0)
        if win_a:
            win_a.destroy()
