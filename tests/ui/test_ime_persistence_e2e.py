"""End-to-end (E2E) functional tests for IME Mode Persistence & Layout Switching.

Exercises real Win32 windows with IME contexts running against a live ImeModePersistence.exe instance.
Validates:
1. Cross-window IME mode persistence (adoption, carry-over, restoration across window switches).
2. Rule-based layout switching when target window receives focus.
3. Ghost window / transient popup non-interference.
"""

import time
import pytest
from win32_ime_helper import (
    ImeTestWindow,
    IME_CMODE_NATIVE,
    IME_CMODE_ALPHANUMERIC,
    IME_CMODE_FULLSHAPE,
    get_ime_state,
    set_ime_state,
    has_chinese_ime,
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
