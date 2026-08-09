#include "autostart.h"

#include <wtsapi32.h>

#include <string>

namespace autostart {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"ImeModePersistence";

// Same name the installer registers, so the two manage one task rather than each
// leaving the other's behind.
constexpr wchar_t kTaskName[] = L"ImeModePersistence";

// Quoted so a path containing spaces survives the shell's command-line parsing.
std::wstring launch_command() {
    const std::wstring path = module_path();
    if (path.empty()) {
        return {};
    }
    return L'"' + path + L'"';
}

// schtasks.exe rather than the Task Scheduler COM API: one documented command line
// against several interfaces and a great deal of boilerplate, for a task this
// simple. CREATE_NO_WINDOW because it would otherwise flash a console.
bool run_schtasks(std::wstring arguments, DWORD& exitCode) {
    std::wstring command = L"schtasks.exe " + arguments;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return false;
    }

    // Bounded: this runs on the UI thread, and a wedged schtasks must not take the
    // tray menu with it.
    const bool finished = WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0;
    const bool read = finished && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return read;
}

// One string of session information, freed before returning.
std::wstring session_string(WTS_INFO_CLASS what) {
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
                                     what, &buffer, &bytes) ||
        !buffer) {
        return {};
    }

    std::wstring result(buffer);
    WTSFreeMemory(buffer);
    return result;
}

// The user of the interactive session, not of this process's token. The two
// differ exactly when it matters: elevation approved with another account's
// credentials leaves this process running as that account, and a logon task
// registered under it would never fire for the user who actually logs in.
// Elevation stays inside the session, so the session's user is always the one
// the task is for.
std::wstring current_user() {
    const std::wstring user = session_string(WTSUserName);
    if (!user.empty()) {
        // Domain-qualified so schtasks resolves the same account on a
        // domain-joined machine. For a local account the domain is the machine
        // name, which schtasks accepts just as well.
        const std::wstring domain = session_string(WTSDomainName);
        return domain.empty() ? user : domain + L"\\" + user;
    }

    // No session information (a service-like context); the token is all there is.
    wchar_t name[256]{};
    DWORD chars = ARRAYSIZE(name);
    return GetUserNameW(name, &chars) ? std::wstring(name, chars - 1) : std::wstring{};
}

bool task_exists() {
    DWORD exitCode = 1;
    return run_schtasks(std::wstring(L"/Query /TN \"") + kTaskName + L"\"", exitCode) &&
           exitCode == 0;
}

bool create_task() {
    const std::wstring path = module_path();
    const std::wstring user = current_user();
    if (path.empty() || user.empty()) {
        return false;
    }

    // /RL HIGHEST is what makes it elevated, /IT keeps it interactive so the tray
    // icon appears, and /TR is a command line of its own, hence the inner quotes.
    std::wstring arguments = L"/Create /F /TN \"";
    arguments += kTaskName;
    arguments += L"\" /SC ONLOGON /RL HIGHEST /IT /RU \"";
    arguments += user;
    arguments += L"\" /TR \"\\\"";
    arguments += path;
    arguments += L"\\\"\"";

    DWORD exitCode = 1;
    return run_schtasks(arguments, exitCode) && exitCode == 0;
}

bool delete_task() {
    // The exit code is deliberately ignored: schtasks fails when there is no such
    // task, which is already the state this is trying to reach.
    DWORD exitCode = 1;
    return run_schtasks(std::wstring(L"/Delete /F /TN \"") + kTaskName + L"\"", exitCode);
}

std::wstring read_value() {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                     nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return {};
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ,
                     nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }

    // RegGetValueW reports the size including the terminating null.
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

} // namespace

std::wstring module_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (written == 0) {
            return {};
        }
        if (written < path.size()) {
            path.resize(written);
            return path;
        }
        if (path.size() >= 32768) {
            return {};
        }
        path.resize(path.size() * 2);
    }
}

bool elevated() {
    // Static, not queried per call: elevation cannot change for the life of a
    // process, and this is on the tooltip's 50 ms path.
    static const bool result = [] {
        HANDLE token{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }

        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        const BOOL ok =
            GetTokenInformation(token, TokenElevation, &elevation, size, &size);
        CloseHandle(token);

        return ok && elevation.TokenIsElevated != 0;
    }();
    return result;
}

Kind current() {
    // Task first: it is the mechanism an elevated copy uses, and if both somehow
    // exist the task is the one that actually starts elevated.
    if (task_exists()) {
        return Kind::ScheduledTask;
    }

    const std::wstring expected = launch_command();
    const std::wstring actual = read_value();
    if (expected.empty() || actual.empty()) {
        return Kind::None;
    }

    const bool matches =
        CompareStringOrdinal(actual.c_str(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
    return matches ? Kind::Registry : Kind::None;
}

bool set_enabled(bool enable) {
    if (!enable) {
        // Both mechanisms, because leaving the other one behind would keep starting
        // the utility after the user turned autostart off.
        const bool removedTask = delete_task();
        const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        return removedTask && (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    }

    if (elevated()) {
        // A Run entry cannot start an elevated copy, so it is not merely redundant
        // here -- it would start a second, unelevated one.
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        return create_task();
    }

    delete_task();

    const std::wstring command = launch_command();
    if (command.empty()) {
        return false;
    }

    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(
        key, kValueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), bytes);
    RegCloseKey(key);

    return status == ERROR_SUCCESS;
}

} // namespace autostart
