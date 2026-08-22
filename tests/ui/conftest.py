"""Pytest fixtures and configuration for UI & E2E tests of ImeModePersistence.

Includes:
- Automatic registry sandboxing (backup & restore HKCU\\Software\\ImeModePersistence)
- Application process lifecycle management (clean start, terminate, single-instance hygiene)
- Per-test failure hooks: screenshot capture & UIA control tree dump on test failure
"""

import os
import sys
import time
import winreg
import ctypes
import subprocess
import pytest

REG_KEY_PATH = r"Software\ImeModePersistence"
DEFAULT_EXE = os.path.abspath(r"build-x64\Release\ImeModePersistence.exe")


def pytest_configure(config):
    """Report Chinese IME availability at session start so CI logs are easy to read."""
    try:
        result = subprocess.run(
            [
                "powershell", "-NonInteractive", "-Command",
                "(Get-WinUserLanguageList | "
                " Where-Object { $_.LanguageTag -like 'zh*' }).InputMethodTips "
                "-join ','",
            ],
            capture_output=True, text=True, timeout=15,
        )
        tips = result.stdout.strip()
        has_ime = "B2F9C502" in tips or "B115690A" in tips
        print(
            f"\n[IME-CONFIG] Chinese IME: {'INSTALLED (OK)' if has_ime else 'NOT INSTALLED (real-IME tests will SKIP)'}"
            f"\n[IME-CONFIG] zh-TW InputMethodTips: {tips or '(none)'}"
        )
    except Exception as exc:
        print(f"\n[IME-CONFIG] Could not query IME status: {exc}")


def pytest_addoption(parser):
    parser.addoption(
        "--exe",
        action="store",
        default=DEFAULT_EXE,
        help="Path to ImeModePersistence.exe binary",
    )


@pytest.fixture(scope="session")
def app_exe(request):
    exe_path = os.environ.get("IME_PERSISTENCE_EXE", request.config.getoption("--exe"))
    if not os.path.isabs(exe_path):
        exe_path = os.path.abspath(exe_path)
    if not os.path.exists(exe_path):
        pytest.fail(f"Executable not found at: {exe_path}. Build the project first.")
    return exe_path


@pytest.fixture(autouse=True)
def registry_sandbox():
    """Backs up the registry settings before each test and restores them on teardown."""
    backup_values = {}
    key_existed = False

    # 1. Read existing values
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH, 0, winreg.KEY_READ) as key:
            key_existed = True
            i = 0
            while True:
                try:
                    name, val, vtype = winreg.EnumValue(key, i)
                    backup_values[name] = (val, vtype)
                    i += 1
                except OSError:
                    break
    except FileNotFoundError:
        key_existed = False

    class RegHelper:
        @staticmethod
        def set_ui_language(lang_id: int):
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH) as key:
                winreg.SetValueEx(key, "UiLanguage", 0, winreg.REG_DWORD, lang_id)

        @staticmethod
        def set_persist_mode(enabled: bool):
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH) as key:
                winreg.SetValueEx(key, "PersistMode", 0, winreg.REG_DWORD, 1 if enabled else 0)

        @staticmethod
        def set_rules(rules_text: str):
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH) as key:
                winreg.SetValueEx(key, "Rules", 0, winreg.REG_SZ, rules_text)

        @staticmethod
        def clear():
            try:
                winreg.DeleteKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH)
            except FileNotFoundError:
                pass

    yield RegHelper

    # Teardown: Restore backed up registry values
    if key_existed:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH) as key:
            # Delete any extra keys created during test
            try:
                i = 0
                while True:
                    name, _, _ = winreg.EnumValue(key, 0)
                    winreg.DeleteValue(key, name)
            except OSError:
                pass

            for name, (val, vtype) in backup_values.items():
                winreg.SetValueEx(key, name, 0, vtype, val)
    else:
        # Delete if it didn't exist originally
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH)
        except OSError:
            pass


@pytest.fixture
def app_runner(app_exe):
    """Spawns an instance of ImeModePersistence and guarantees process termination."""
    active_procs = []

    def _cleanup():
        for p in active_procs:
            try:
                p.terminate()
                p.wait(timeout=1.5)
            except Exception:
                try:
                    p.kill()
                    p.wait(timeout=1.0)
                except Exception:
                    pass
        active_procs.clear()
        time.sleep(0.2)

    def _launch(args=None):
        _cleanup()  # Clean any previous instance before launching to avoid mutex collisions
        cmd = [app_exe] + (args or [])
        proc = subprocess.Popen(cmd)
        active_procs.append(proc)
        time.sleep(0.5)
        return proc

    _launch.cleanup = _cleanup

    yield _launch

    _cleanup()


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()

    if report.when == "call" and report.failed:
        test_name = item.name.replace("/", "_").replace("\\", "_").replace("[", "_").replace("]", "_")
        shot_path = f"ui-failure-{test_name}.png"
        log_path = f"ui-failure-{test_name}.log"

        # 1. Capture screen
        try:
            from PIL import ImageGrab
            img = ImageGrab.grab()
            if img:
                img.save(shot_path)
                print(f"\n[FAILURE] Saved screenshot -> {shot_path}")
        except Exception:
            pass

        # 2. Dump control tree
        try:
            from pywinauto import Desktop
            with open(log_path, "w", encoding="utf-8") as f:
                f.write(f"=== Failure Control Tree for {test_name} ===\n\n")
                windows = Desktop(backend="uia").windows()
                for w in windows:
                    try:
                        if w.is_visible():
                            f.write(f"Window: {w.window_text()!r} ({w.element_info.class_name})\n")
                    except Exception:
                        pass
            print(f"[FAILURE] Saved debug log -> {log_path}")
        except Exception as exc:
            print(f"[FAILURE] Log dump failed: {exc}")
