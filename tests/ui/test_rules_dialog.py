"""Proof-of-concept GUI automation test for the rules dialog.

Launches the built executable with --show-rules (a test hook that opens the
rules dialog straight away, bypassing the system tray) and drives the dialog
through UI Automation with pywinauto. It asserts the dialog opens, the key
controls are present, and the executable field accepts text.

Usage:
    python tests/ui/test_rules_dialog.py [path-to-ImeModePersistence.exe]

Exit code 0 = pass, 1 = fail. On failure it saves a full-screen screenshot to
ui-failure.png for the CI artifact and prints the control tree for debugging.

The runner's UI language is en-US, so the dialog title and control labels are
the English strings from src/strings.cpp.
"""

import sys
import time
import subprocess

EXE = sys.argv[1] if len(sys.argv) > 1 else r"build-x64\Release\ImeModePersistence.exe"
SHOT = "ui-failure.png"
TITLE_RE = r".*app language bindings.*"  # English rulesCaption


def save_screenshot():
    try:
        from PIL import ImageGrab
        ImageGrab.grab().save(SHOT)
        print(f"saved screenshot -> {SHOT}")
    except Exception as exc:  # best-effort only
        print(f"screenshot failed: {exc}")


def run():
    print(f"launching: {EXE} --show-rules")
    proc = subprocess.Popen([EXE, "--show-rules"])
    try:
        from pywinauto import Desktop

        dlg = Desktop(backend="uia").window(title_re=TITLE_RE)
        dlg.wait("visible ready", timeout=30)
        print("rules dialog is up:", dlg.window_text())

        # The action buttons, matched by their visible label (& access keys are
        # dropped from the UIA Name).
        for label in ("Add / update", "Remove selected", "Browse"):
            btn = dlg.child_window(title_re=label, control_type="Button")
            assert btn.exists(timeout=5), f"missing button: {label}"
            print("found button:", label)

        # Close is IDOK (auto_id "1"); match by id, not title -- the window's
        # title-bar [X] is also a UIA Button named "Close", which the first PoC
        # run tripped on (ElementAmbiguousError).
        close_btn = dlg.child_window(auto_id="1", control_type="Button")
        assert close_btn.exists(timeout=5), "missing Close button (IDOK)"
        print("found button: Close (IDOK)")

        # The executable field and language combo, by their Win32 control ids
        # (IDC_EXECUTABLE = 1002, IDC_LAYOUT = 1003), which UIA exposes as
        # AutomationId.
        edit = dlg.child_window(auto_id="1002", control_type="Edit")
        assert edit.exists(timeout=5), "missing executable edit (id 1002)"
        combo = dlg.child_window(auto_id="1003", control_type="ComboBox")
        assert combo.exists(timeout=5), "missing language combo (id 1003)"
        print("found executable edit + language combo")

        # Real input: type into the field and read it back.
        edit.set_edit_text("notepad.exe")
        time.sleep(0.3)
        try:
            value = edit.get_value()
        except Exception:
            value = edit.window_text()
        print("edit value after typing:", repr(value))
        assert "notepad.exe" in value, f"edit did not accept text: {value!r}"

        # Close cleanly through the dialog's own Close button.
        close_btn.click()
        print("clicked Close -- PoC PASSED")
        return 0
    except Exception as exc:
        print("UI TEST FAILED:", repr(exc))
        save_screenshot()
        try:
            from pywinauto import Desktop
            Desktop(backend="uia").window(title_re=TITLE_RE).print_control_identifiers(depth=3)
        except Exception:
            pass
        return 1
    finally:
        try:
            proc.terminate()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(run())
