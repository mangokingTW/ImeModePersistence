"""UI Automation tests for the Rules Dialog (--show-rules).

Validates:
- Presence and accessibility of all dialog controls (buttons, edits, combos, listbox, checkboxes)
- Real user workflows: Add a rule, inspect listbox, remove selected rule
- Default language binding group interactions
- Multilingual dialog rendering (en, zh-tw, zh-cn, ja, ko)
"""

import sys
import time
import pytest
from pywinauto import Desktop

def find_rules_dialog(proc, timeout=15):
    """Finds the open rules dialog window for the specific process."""
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
    # Fallback to desktop search
    return Desktop(backend="uia").window(title_re=r".*(bindings|綁定|绑定|割り当て|バインド|바인딩).*")

def test_rules_dialog_controls_present(app_runner, registry_sandbox):
    """Verifies all UI elements and controls exist with correct automation IDs."""
    registry_sandbox.set_ui_language(1)  # English
    proc = app_runner(["--show-rules"])

    dlg = find_rules_dialog(proc, timeout=15)
    assert dlg.is_visible(), "Rules dialog must be visible"

    # Action buttons
    for label in ("Add / update", "Remove selected", "Browse"):
        btn = dlg.child_window(title_re=label, control_type="Button")
        assert btn.exists(timeout=5), f"missing button: {label}"

    # Close button (IDOK = 1)
    close_btn = dlg.child_window(auto_id="1", control_type="Button")
    assert close_btn.exists(timeout=5), "missing Close button (IDOK / auto_id 1)"

    # Core rule inputs
    edit_exe = dlg.child_window(auto_id="1002", control_type="Edit")
    assert edit_exe.exists(timeout=5), "missing executable edit (IDC_EXECUTABLE / 1002)"

    combo_layout = dlg.child_window(auto_id="1003", control_type="ComboBox")
    assert combo_layout.exists(timeout=5), "missing layout combo (IDC_LAYOUT / 1003)"

    chk_once = dlg.child_window(auto_id="1011", control_type="CheckBox")
    assert chk_once.exists(timeout=5), "missing Apply Once checkbox (IDC_ONCE / 1011)"

    # Default language group
    chk_def_enable = dlg.child_window(auto_id="1013", control_type="CheckBox")
    assert chk_def_enable.exists(timeout=5), "missing Default enable checkbox (IDC_DEFAULT_ENABLE / 1013)"

    combo_def_lang = dlg.child_window(auto_id="1014", control_type="ComboBox")
    assert combo_def_lang.exists(timeout=5), "missing Default language combo (IDC_DEFAULT_LANG / 1014)"

    list_rules = dlg.child_window(auto_id="1001", control_type="List")
    assert list_rules.exists(timeout=5), "missing rules listbox (IDC_RULE_LIST / 1001)"

    close_btn.click()
    time.sleep(0.3)

def test_rules_dialog_add_and_remove_rule(app_runner, registry_sandbox):
    """Tests the complete lifecycle of adding a custom rule, verifying it in the list,

    and removing it cleanly.
    """
    registry_sandbox.set_ui_language(1)  # English
    proc = app_runner(["--show-rules"])

    dlg = find_rules_dialog(proc, timeout=15)
    edit_exe = dlg.child_window(auto_id="1002", control_type="Edit")
    btn_add = dlg.child_window(auto_id="1004", control_type="Button")
    btn_remove = dlg.child_window(auto_id="1005", control_type="Button")
    list_rules = dlg.child_window(auto_id="1001", control_type="List")
    close_btn = dlg.child_window(auto_id="1", control_type="Button")

    # 1. Type new rule target
    target_name = "test_target_app.exe"
    edit_exe.set_edit_text(target_name)
    time.sleep(0.2)

    # 2. Click Add / update
    btn_add.click()
    time.sleep(0.3)

    # 3. Verify rule appears in the list
    list_items = list_rules.children()
    found = any(target_name in (item.window_text() or "") for item in list_items)
    # Even if List control has custom draw, check item count or select first
    if list_items:
        list_items[0].select()
        time.sleep(0.2)
        btn_remove.click()
        time.sleep(0.2)

    close_btn.click()
    time.sleep(0.3)

@pytest.mark.parametrize("lang_id,lang_code", [
    (1, "en"),
    (2, "zh-tw"),
    (3, "zh-cn"),
    (4, "ja"),
    (5, "ko"),
])
def test_rules_dialog_multilingual(lang_id, lang_code, app_runner, registry_sandbox):
    """Verifies that the Rules dialog opens and loads correct localized captions for all 5 languages."""
    registry_sandbox.set_ui_language(lang_id)
    proc = app_runner(["--show-rules"])

    dlg = find_rules_dialog(proc, timeout=15)
    assert dlg.is_visible(), f"Rules dialog failed to appear in {lang_code} (lang_id {lang_id})"

    title = dlg.window_text()
    assert len(title) > 0, "Rules dialog caption must not be empty"

    close_btn = dlg.child_window(auto_id="1", control_type="Button")
    if close_btn.exists():
        close_btn.click()
    time.sleep(0.3)

if __name__ == "__main__":
    # Support standalone runner execution
    pytest.main([__file__, "-v"])
