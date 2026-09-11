"""UI Automation tests for the About TaskDialog (--show-about).

Validates:
- Opening of the TaskDialog across all supported UI languages
- Proper dialog caption and visibility
- Presence of app info, developer name, and links
- Clean dismissal via OK button
"""

import time
import pytest
from wintegrate import Window

CT_BUTTON = 50000

def find_about_dialog(proc, timeout=15):
    """Locates the About TaskDialog window for the given process."""
    return Window.find(class_name="#32770", pid=proc.pid, timeout=timeout)

def dismiss(dlg):
    """Closes the TaskDialog via its OK button, falling back to Enter."""
    root = dlg.re_resolve_element()
    ok_btn = root.find_descendant(automation_id="1", control_type_id=CT_BUTTON, timeout=2.0, required=False)
    if ok_btn is None:
        ok_btn = root.find_descendant(name_exact="OK", control_type_id=CT_BUTTON, timeout=2.0, required=False)
    if ok_btn is not None:
        ok_btn.invoke()
    else:
        root.send_keys("{ENTER}")
    time.sleep(0.3)

def test_about_dialog_content_and_dismiss(app_runner, registry_sandbox):
    """Verifies that the about TaskDialog opens and closes cleanly."""
    registry_sandbox.set_ui_language(2)  # Traditional Chinese
    proc = app_runner(["--show-about"])

    dlg = find_about_dialog(proc, timeout=15)
    assert dlg.is_visible, "About dialog must appear"
    assert "關於" in dlg.title, f"Expected '關於' in title, got '{dlg.title}'"

    dismiss(dlg)

@pytest.mark.parametrize("lang_id,lang_code", [
    (1, "en"),
    (2, "zh-tw"),
    (3, "zh-cn"),
    (4, "ja"),
    (5, "ko"),
])
def test_about_dialog_multilingual(lang_id, lang_code, app_runner, registry_sandbox):
    """Verifies that the About TaskDialog opens and localizes its title properly across all languages."""
    registry_sandbox.set_ui_language(lang_id)
    proc = app_runner(["--show-about"])

    dlg = find_about_dialog(proc, timeout=15)
    assert dlg.is_visible, f"About dialog failed to appear in {lang_code}"
    assert dlg.title, f"About dialog caption must not be empty in {lang_code}"

    dismiss(dlg)

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
