#include "autostart.h"

#include <appmodel.h>
#include <comdef.h>
#include <shellapi.h>
#include <taskschd.h>

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>

#include <string>
#include <thread>

namespace autostart {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"ImeModePersistence";

// The name older versions gave the elevated logon task. Kept only so that task
// can be found and removed; this version never creates one.
constexpr wchar_t kTaskName[] = L"ImeModePersistence";

// Quoted so a path containing spaces survives the shell's command-line parsing.
std::wstring launch_command() {
    const std::wstring path = module_path();
    if (path.empty()) {
        return {};
    }
    return L'"' + path + L'"';
}

// Task Scheduler is reached through the COM API rather than by spawning
// schtasks.exe. This version only ever *reads and deletes* a task (to clean up
// the elevated logon task older versions created), never creates one -- but even
// for that, an unsigned process launching schtasks.exe is a signal worth not
// sending, and the COM interfaces are managed with _com_ptr_t smart pointers.
_COM_SMARTPTR_TYPEDEF(ITaskService, __uuidof(ITaskService));
_COM_SMARTPTR_TYPEDEF(ITaskFolder, __uuidof(ITaskFolder));
_COM_SMARTPTR_TYPEDEF(IRegisteredTask, __uuidof(IRegisteredTask));

// COM initialised per operation: these run rarely (startup, and the tray
// toggle), and autostart::current() can run before the TSF path brings COM up.
// Balanced, so it composes with a thread that already has COM initialised.
struct ComScope {
    bool owned;
    ComScope() { owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)); }
    ~ComScope() {
        if (owned) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

// Matches uap5:StartupTask TaskId in packaging/msix/AppxManifest.xml.
constexpr wchar_t kStartupTaskId[] = L"ImeModePersistenceStartup";

// Reads the package StartupTask state (MSIX build). StartupTask when enabled,
// None otherwise -- including any failure, so an unexpected error reads as off
// rather than throwing out of a menu handler.
Kind startup_task_kind() {
    ComScope com;
    try {
        using namespace winrt::Windows::ApplicationModel;
        const StartupTask task = StartupTask::GetAsync(kStartupTaskId).get();
        const StartupTaskState state = task.State();
        if (state == StartupTaskState::Enabled || state == StartupTaskState::EnabledByPolicy) {
            return Kind::StartupTask;
        }
    } catch (...) {
    }
    return Kind::None;
}

// Enables or disables the StartupTask (MSIX build). True when the resulting state
// matches the request. A task the user disabled from Settings (DisabledByUser)
// or by policy cannot be re-enabled here -- returns false so the caller can open
// Settings.
//
// The work runs on a dedicated multithreaded-apartment thread. RequestEnableAsync
// can show a one-time consent dialog and complete asynchronously; blocking on its
// .get() from the tray's single-threaded apartment deadlocks (the STA that must
// pump for the async to finish is the very thread blocked on it), which surfaced
// as a crash when enabling. On an MTA thread .get() blocks safely and the UI STA
// stays free to pump.
bool set_startup_task(bool enable) {
    bool result = false;
    std::thread worker([enable, &result] {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            return;
        }
        try {
            using namespace winrt::Windows::ApplicationModel;
            const StartupTask task = StartupTask::GetAsync(kStartupTaskId).get();
            if (!enable) {
                task.Disable();
                result = true;
            } else {
                StartupTaskState state = task.State();
                if (state != StartupTaskState::Enabled &&
                    state != StartupTaskState::EnabledByPolicy) {
                    state = task.RequestEnableAsync().get();
                }
                result = state == StartupTaskState::Enabled ||
                         state == StartupTaskState::EnabledByPolicy;
            }
        } catch (...) {
        }
        CoUninitialize();
    });
    worker.join();
    return result;
}

// Connects to the local Task Scheduler and returns its root folder, or nullptr.
ITaskFolderPtr task_root() {
    ITaskServicePtr service;
    if (FAILED(service.CreateInstance(__uuidof(TaskScheduler)))) {
        return nullptr;
    }
    if (FAILED(service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) {
        return nullptr;
    }
    ITaskFolderPtr folder;
    if (FAILED(service->GetFolder(_bstr_t(L"\\"), &folder))) {
        return nullptr;
    }
    return folder;
}

// Whether an older version's elevated logon task is still registered.
bool task_exists() {
    ComScope com;
    ITaskFolderPtr folder = task_root();
    if (!folder) {
        return false;
    }
    IRegisteredTaskPtr task;
    return SUCCEEDED(folder->GetTask(_bstr_t(kTaskName), &task)) && task != nullptr;
}

// Removes the legacy logon task if present. "Not found" is the state this wants,
// so it counts as success.
bool delete_task() {
    ComScope com;
    ITaskFolderPtr folder = task_root();
    if (!folder) {
        return false;
    }
    const HRESULT hr = folder->DeleteTask(_bstr_t(kTaskName), 0);
    return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
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

bool packaged() {
    // Static, not queried per call: package identity is fixed for the life of the
    // process. GetCurrentPackageFullName returns APPMODEL_ERROR_NO_PACKAGE when
    // unpackaged, and ERROR_INSUFFICIENT_BUFFER (a package exists) otherwise.
    static const bool result = [] {
        UINT32 length = 0;
        return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
    }();
    return result;
}

void open_startup_settings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:startupapps", nullptr, nullptr, SW_SHOWNORMAL);
}

Kind current() {
    // In the MSIX build, autostart is the package's StartupTask; the Run key
    // below is virtualized and meaningless.
    if (packaged()) {
        return startup_task_kind();
    }

    // A leftover elevated task from an older version still counts as "on", so the
    // status box shows it and toggling autostart off removes it. New installs
    // only ever use the Run entry below.
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
    // The MSIX build drives its package StartupTask, not the Run key (which it
    // virtualizes to no effect).
    if (packaged()) {
        return set_startup_task(enable);
    }

    // Autostart is always the unelevated Run entry. The utility no longer creates
    // an elevated logon task: a silent elevated task started at logon is exactly
    // the persistence pattern Defender's behaviour ML flags, and it is not worth
    // it when the tray's "Restart as administrator" covers the occasional
    // elevated session. Any task left by an older version is removed here.
    delete_task();

    if (!enable) {
        const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

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
