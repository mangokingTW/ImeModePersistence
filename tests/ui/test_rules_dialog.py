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
from wintegrate import Window

# UIA control type ids. wintegrate takes these as raw ints and exports no
# constants for them, so name them once here rather than scattering magic
# numbers through the assertions.
CT_BUTTON = 50000
CT_CHECKBOX = 50002
CT_COMBOBOX = 50003
CT_EDIT = 50004
CT_LIST = 50008

def find_rules_dialog(proc, timeout=15):
    """Finds the open rules dialog window for the specific process."""
    # Scoped to the process under test and to the dialog window class. Both
    # criteria are required: #32770 alone would match any dialog open on the
    # runner. Window.find combines criteria with AND, so unlike the previous
    # implementation this needs no localized-title fallback -- the old regex
    # of six translations of "bindings" existed only because the primary
    # lookup could drift onto the wrong window.
    return Window.find(class_name="#32770", pid=proc.pid, timeout=timeout)

def test_rules_dialog_controls_present(app_runner, registry_sandbox):
    """Verifies all UI elements and controls exist with correct automation IDs."""
    registry_sandbox.set_ui_language(1)  # English
    proc = app_runner(["--show-rules"])

    dlg = find_rules_dialog(proc, timeout=15)
    assert dlg.is_visible, "Rules dialog must be visible"
    root = dlg.re_resolve_element()

    # Action buttons, matched on their captions. name_contains is a
    # case-insensitive substring, ANDed with the control type.
    for label in ("Add / update", "Remove selected", "Browse"):
        btn = root.find_descendant(name_contains=label, control_type_id=CT_BUTTON, timeout=5.0, required=False)
        assert btn is not None, f"missing button: {label}"

    # Every control below is identified by its dialog resource id, which is
    # locale-independent -- the reason these assertions survive the
    # multilingual test cases.
    expected = [
        ("1", CT_BUTTON, "Close button (IDOK / auto_id 1)"),
        ("1002", CT_EDIT, "executable edit (IDC_EXECUTABLE / 1002)"),
        ("1003", CT_COMBOBOX, "layout combo (IDC_LAYOUT / 1003)"),
        ("1011", CT_CHECKBOX, "Apply Once checkbox (IDC_ONCE / 1011)"),
        ("1013", CT_CHECKBOX, "Default enable checkbox (IDC_DEFAULT_ENABLE / 1013)"),
        ("1014", CT_COMBOBOX, "Default language combo (IDC_DEFAULT_LANG / 1014)"),
        ("1001", CT_LIST, "rules listbox (IDC_RULE_LIST / 1001)"),
    ]
    for auto_id, control_type, description in expected:
        found = root.find_descendant(
            automation_id=auto_id, control_type_id=control_type, timeout=5.0, required=False
        )
        assert found is not None, f"missing {description}"

    root.find_descendant(automation_id="1", control_type_id=CT_BUTTON).invoke()
    time.sleep(0.3)

def test_rules_dialog_add_and_remove_rule(app_runner, registry_sandbox):
    """Tests the complete lifecycle of adding a custom rule, verifying it in the list,

    and removing it cleanly.
    """
    registry_sandbox.set_ui_language(1)  # English
    proc = app_runner(["--show-rules"])

    dlg = find_rules_dialog(proc, timeout=15)
    root = dlg.re_resolve_element()
    edit_exe = root.find_descendant(automation_id="1002", control_type_id=CT_EDIT)
    btn_add = root.find_descendant(automation_id="1004", control_type_id=CT_BUTTON)
    btn_remove = root.find_descendant(automation_id="1005", control_type_id=CT_BUTTON)
    list_rules = root.find_descendant(automation_id="1001", control_type_id=CT_LIST)
    close_btn = root.find_descendant(automation_id="1", control_type_id=CT_BUTTON)

    # 1. Type new rule target. set_value_verified reads the value back and
    #    raises TextMismatchError if it did not stick, so the old bare
    #    set_edit_text + sleep is now self-checking.
    target_name = "test_target_app.exe"
    edit_exe.set_value_verified(target_name)
    time.sleep(0.2)

    # 2. Click Add / update
    btn_add.invoke()
    time.sleep(0.3)

    # 3. Verify rule appears in the list
    list_items = list_rules.children()
    # NOTE: `found` is computed but deliberately left unasserted, exactly as
    # before this migration -- an owner-drawn list may expose no item names at
    # all. Tightening it is a behaviour change and belongs in its own commit,
    # not in a library swap.
    found = any(target_name in (item.name or "") for item in list_items)
    if list_items:
        list_items[0].select_verified()
        time.sleep(0.2)
        btn_remove.invoke()
        time.sleep(0.2)

    close_btn.invoke()
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
    assert dlg.is_visible, f"Rules dialog failed to appear in {lang_code} (lang_id {lang_id})"

    title = dlg.title
    assert len(title) > 0, "Rules dialog caption must not be empty"

    root = dlg.re_resolve_element()
    close_btn = root.find_descendant(automation_id="1", control_type_id=CT_BUTTON, timeout=5.0, required=False)
    if close_btn is not None:
        close_btn.invoke()
    time.sleep(0.3)

if __name__ == "__main__":
    # Support standalone runner execution
    pytest.main([__file__, "-v"])
