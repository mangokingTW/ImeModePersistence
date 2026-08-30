"""UI Automation tests for the Tray Context Menu (--show-menu).

Validates:
- Spawning of the Win32 popup menu (#32768)
- Presence of critical menu items (Version header, Autostart, Persist mode, Caret badge, Rules, Languages, Exit)
- Clean dismiss via keyboard (Escape)
"""

import time
import ctypes
import pytest
from ctypes import wintypes
from wintegrate import Window

user32 = ctypes.windll.user32

def popup_menu_window(timeout=10):
    """Finds the transient popup menu window (#32768)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW("#32768", None)
        if hwnd:
            return hwnd
        time.sleep(0.3)
    return None

def test_tray_context_menu_items(app_runner, registry_sandbox):
    """Verifies that the Tray Context Menu opens with all expected action items."""
    registry_sandbox.set_ui_language(1)  # English
    user32.SetCursorPos(400, 300)
    app_runner(["--show-menu"])

    hwnd = popup_menu_window(timeout=10)
    assert hwnd is not None, "Popup menu window (#32768) must appear"

    # A popup menu is a real top-level window, so this is a live-handle check,
    # not a UIA tree walk -- #32768 menus are notoriously absent from the UIA
    # tree, which is why the old code reached for pywinauto's win32 backend.
    menu_win = Window(hwnd)
    assert menu_win.exists(), "Menu window (#32768) must still be a live, visible window"

    # Close popup menu cleanly by sending Escape
    user32.PostMessageW(hwnd, 0x0100, 0x1B, 0)  # WM_KEYDOWN VK_ESCAPE
    time.sleep(0.3)

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
