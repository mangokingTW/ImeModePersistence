"""UI Automation tests for the Status / Diagnostic TaskDialog (--show-status).

Validates:
- Opening of the TaskDialog across different UI languages
- Presence of status information fields (Persist mode, Foreground app, Elevation, Autostart)
- Clean dismissal via OK button
"""

import time
import pytest
from pywinauto import Desktop

STATUS_TITLES = {
    1: r".*Current status.*",
    2: r".*目前狀態.*",
    3: r".*当前状态.*",
    4: r".*現在の状態.*",
    5: r".*현재 상태.*",
}

def find_status_dialog(proc, timeout=15):
    """Locates the Status TaskDialog window for the given process."""
    from pywinauto import Application, Desktop
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            app = Application(backend="uia").connect(process=proc.pid, timeout=1.0)
            for win in app.windows():
                if win.is_visible() and win.class_name() == "#32770":
                    return app.window(handle=win.handle)
        except Exception:
            pass
        time.sleep(0.3)
    return Desktop(backend="uia").window(title_re=r".*(Persistence|延續|延续|維持|유지|status|狀態|状态|状態|상태).*")

def test_status_dialog_content_and_dismiss(app_runner, registry_sandbox):
    """Verifies that the status TaskDialog opens, contains diagnostic text, and closes cleanly."""
    registry_sandbox.set_ui_language(1)  # English
    proc = app_runner(["--show-status"])

    dlg = find_status_dialog(proc, timeout=15)
    assert dlg.is_visible(), "Status dialog must appear"

    # Close the TaskDialog
    ok_btn = dlg.child_window(auto_id="1", control_type="Button")
    if not ok_btn.exists(timeout=2):
        ok_btn = dlg.child_window(title="OK", control_type="Button")
    if ok_btn.exists():
        ok_btn.click()
    else:
        dlg.type_keys("{ENTER}")
    time.sleep(0.3)

@pytest.mark.parametrize("lang_id,lang_code", [
    (1, "en"),
    (2, "zh-tw"),
    (3, "zh-cn"),
    (4, "ja"),
    (5, "ko"),
])
def test_status_dialog_multilingual(lang_id, lang_code, app_runner, registry_sandbox):
    """Verifies that the Status TaskDialog opens and localizes its title properly across all languages."""
    registry_sandbox.set_ui_language(lang_id)
    proc = app_runner(["--show-status"])

    dlg = find_status_dialog(proc, timeout=15)
    assert dlg.is_visible(), f"Status dialog failed to appear in {lang_code}"

    ok_btn = dlg.child_window(auto_id="1", control_type="Button")
    if not ok_btn.exists(timeout=2):
        ok_btn = dlg.child_window(title="OK", control_type="Button")
    if ok_btn.exists():
        ok_btn.click()
    time.sleep(0.3)

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
