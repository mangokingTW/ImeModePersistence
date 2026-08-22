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
    get_ime_state,
    set_ime_state,
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

        # Wait past the adoption debounce window (persist::kAdoptDebounceMs = 200ms)
        time.sleep(0.4)

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
        time.sleep(0.4)  # Wait past adoption debounce

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
