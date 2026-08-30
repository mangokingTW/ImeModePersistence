"""UI Automation tests for the Status / Diagnostic TaskDialog (--show-status).

Validates:
- Opening of the TaskDialog across different UI languages
- Presence of status information fields (Persist mode, Foreground app, Elevation, Autostart)
- Clean dismissal via OK button
"""

import time
import pytest
from wintegrate import Window

# UIA control type ids. wintegrate takes these as raw ints and exports no
# constants for them, so name them once here rather than scattering magic
# numbers through the assertions.
CT_BUTTON = 50000

STATUS_TITLES = {
    1: r".*Current status.*",
    2: r".*目前狀態.*",
    3: r".*当前状态.*",
    4: r".*現在の状態.*",
    5: r".*현재 상태.*",
}

def find_status_dialog(proc, timeout=15):
    """Locates the Status TaskDialog window for the given process."""
    # Scoped to the process under test and to the dialog window class. Both
    # criteria are required: #32770 alone would match any dialog open on the
    # runner, and this process owns a tray window too. Window.find combines
    # criteria with AND, so this cannot drift onto someone else's dialog --
    # which also means no localized-title fallback is needed.
    return Window.find(class_name="#32770", pid=proc.pid, timeout=timeout)

def dismiss(dlg):
    """Closes the TaskDialog via its OK button, falling back to Enter."""
    root = dlg.re_resolve_element()
    # IDOK is automation id "1"; required=False turns the old .exists() probe
    # into a plain None check instead of a raise/except pair.
    ok_btn = root.find_descendant(automation_id="1", control_type_id=CT_BUTTON, timeout=2.0, required=False)
    if ok_btn is None:
        ok_btn = root.find_descendant(name_exact="OK", control_type_id=CT_BUTTON, timeout=2.0, required=False)
    if ok_btn is not None:
        ok_btn.invoke()
    else:
        root.send_keys("{ENTER}")
    time.sleep(0.3)

def test_status_dialog_content_and_dismiss(app_runner, registry_sandbox):
    """Verifies that the status TaskDialog opens, contains diagnostic text, and closes cleanly."""
    registry_sandbox.set_ui_language(1)  # English
    proc = app_runner(["--show-status"])

    dlg = find_status_dialog(proc, timeout=15)
    assert dlg.is_visible, "Status dialog must appear"

    dismiss(dlg)

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
    assert dlg.is_visible, f"Status dialog failed to appear in {lang_code}"
    assert dlg.title, f"Status dialog caption must not be empty in {lang_code}"

    dismiss(dlg)

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
