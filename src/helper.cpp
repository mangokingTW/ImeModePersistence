#include "helper.h"
#include "autostart.h"
#include "diagnostic.h"

#include <imm.h>
#include <sddl.h>
#include <shellapi.h>

#include <thread>

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif
#ifndef IMC_SETCONVERSIONMODE
#define IMC_SETCONVERSIONMODE 0x0002
#endif
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif
#ifndef IMC_SETOPENSTATUS
#define IMC_SETOPENSTATUS 0x0006
#endif

namespace helper {
namespace {

constexpr UINT kSendTimeoutMs = 120;
constexpr DWORD kClientConnectTimeoutMs = 50;

struct Targets {
    HWND wnd[2];
    int count;
};

Targets targets_for(HWND hwnd) {
    Targets t{};
    if (!hwnd || !IsWindow(hwnd)) {
        return t;
    }
    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (thread && GetGUIThreadInfo(thread, &gti) && gti.hwndFocus &&
        gti.hwndFocus != hwnd && IsWindow(gti.hwndFocus)) {
        t.wnd[t.count++] = gti.hwndFocus;
    }
    t.wnd[t.count++] = hwnd;
    return t;
}

HWND default_ime_window(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return nullptr;
    }
    HWND imeWnd = ImmGetDefaultIMEWnd(hwnd);
    return (imeWnd && IsWindow(imeWnd)) ? imeWnd : nullptr;
}

bool send_client_request(const Request& req, Response& resp) {
    for (int retry = 0; retry < 3; ++retry) {
        HANDLE hPipe = CreateFileW(
            kPipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_PIPE_BUSY) {
                if (WaitNamedPipeW(kPipeName, kClientConnectTimeoutMs)) {
                    continue;
                }
            }
            return false;
        }

        DWORD written = 0;
        if (!WriteFile(hPipe, &req, sizeof(req), &written, nullptr) || written != sizeof(req)) {
            CloseHandle(hPipe);
            return false;
        }

        DWORD readBytes = 0;
        const BOOL readOk =
            ReadFile(hPipe, &resp, sizeof(resp), &readBytes, nullptr) && readBytes == sizeof(resp);

        CloseHandle(hPipe);
        return readOk && resp.success;
    }
    return false;
}

} // namespace

int run_server(DWORD parentPid) {
    diag::initialise();
    diag::write(L"helper: server starting, elevated=%d, parentPid=%u", autostart::elevated(), parentPid);

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kHelperMutex);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        diag::write(L"helper: another instance already running, exiting");
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Set SDDL allowing Medium-IL interactive user and admin clients to connect:
    // D:(A;;GA;;;BA)(A;;GA;;;IU) -> Built-in Admins & Interactive Users Generic All
    // S:(ML;;NW;;;ME) -> Low/Medium Mandatory Integrity Label
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    PSECURITY_DESCRIPTOR pSD = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;BA)(A;;GA;;;IU)S:(ML;;NW;;;ME)",
            SDDL_REVISION_1,
            &pSD,
            nullptr)) {
        sa.lpSecurityDescriptor = pSD;
    }

    // Synchronous pipe: no FILE_FLAG_OVERLAPPED so ReadFile/WriteFile are always
    // reliable. Parent-process monitoring is handled by a separate watchdog thread
    // that posts a Stop command to break the server loop cleanly.
    HANDLE hPipe = CreateNamedPipeW(
        kPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        512,
        512,
        0,
        &sa);

    if (hPipe == INVALID_HANDLE_VALUE) {
        diag::write(L"helper: CreateNamedPipe failed with %u", GetLastError());
        if (pSD) LocalFree(pSD);
        CloseHandle(hMutex);
        return 1;
    }

    diag::write(L"helper: named pipe created and listening");

    // Watchdog thread: waits on both the parent process handle and an internal
    // shutdown event. If parent terminates first, it sends a Stop command to
    // unblock the synchronous ConnectNamedPipe loop. If the server loop exits
    // due to another command, setting hShutdownEvent immediately wakes and joins
    // the watchdog thread without blocking.
    HANDLE hShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread watchdog;
    if (parentPid != 0 && hShutdownEvent) {
        HANDLE hParent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        if (!hParent) {
            diag::write(L"helper: could not open parent process %u (error %u)", parentPid, GetLastError());
        } else {
            watchdog = std::thread([hParent, hShutdownEvent, parentPid] {
                HANDLE waitHandles[2] = { hParent, hShutdownEvent };
                const DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitRes == WAIT_OBJECT_0) {
                    diag::write(L"helper: parent process %u terminated, sending stop", parentPid);
                    Request req{CommandType::Stop};
                    Response resp{};
                    send_client_request(req, resp);
                }
                CloseHandle(hParent);
            });
        }
    }

    bool running = true;
    while (running) {
        const BOOL connected =
            ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected) {
            Request req{};
            DWORD bytesRead = 0;
            if (ReadFile(hPipe, &req, sizeof(req), &bytesRead, nullptr) && bytesRead == sizeof(req)) {
                Response resp{};

                if (req.type == CommandType::Ping) {
                    resp.success = TRUE;
                } else if (req.type == CommandType::Stop) {
                    resp.success = TRUE;
                    running = false;
                } else if (req.type == CommandType::Read) {
                    const Targets targets = targets_for(req.targetTopHwnd);
                    for (int i = 0; i < targets.count; ++i) {
                        HWND imeWnd = default_ime_window(targets.wnd[i]);
                        if (!imeWnd) continue;
                        DWORD_PTR open = 0;
                        if (!SendMessageTimeoutW(
                                imeWnd,
                                WM_IME_CONTROL,
                                IMC_GETOPENSTATUS,
                                0,
                                SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                                kSendTimeoutMs,
                                &open)) {
                            continue;
                        }
                        DWORD_PTR bits = 0;
                        if (!SendMessageTimeoutW(
                                imeWnd,
                                WM_IME_CONTROL,
                                IMC_GETCONVERSIONMODE,
                                0,
                                SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                                kSendTimeoutMs,
                                &bits)) {
                            continue;
                        }
                        resp.success = TRUE;
                        resp.openStatus = (open != 0);
                        resp.conversionBits = static_cast<DWORD>(bits);
                        resp.targetHwnd = imeWnd;
                        break;
                    }
                } else if (req.type == CommandType::WriteConversion) {
                    const Targets targets = targets_for(req.targetTopHwnd);
                    for (int i = 0; i < targets.count; ++i) {
                        HWND imeWnd = default_ime_window(targets.wnd[i]);
                        if (!imeWnd) continue;
                        DWORD_PTR result = 0;
                        if (SendMessageTimeoutW(
                                imeWnd,
                                WM_IME_CONTROL,
                                IMC_SETCONVERSIONMODE,
                                static_cast<LPARAM>(req.conversionBits),
                                SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                                kSendTimeoutMs,
                                &result) != 0) {
                            resp.success = TRUE;
                            resp.targetHwnd = imeWnd;
                            break;
                        }
                    }
                } else if (req.type == CommandType::WriteOpen) {
                    const Targets targets = targets_for(req.targetTopHwnd);
                    for (int i = 0; i < targets.count; ++i) {
                        HWND imeWnd = default_ime_window(targets.wnd[i]);
                        if (!imeWnd) continue;
                        DWORD_PTR result = 0;
                        if (SendMessageTimeoutW(
                                imeWnd,
                                WM_IME_CONTROL,
                                IMC_SETOPENSTATUS,
                                req.openStatus ? TRUE : FALSE,
                                SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                                kSendTimeoutMs,
                                &result) != 0) {
                            resp.success = TRUE;
                            resp.targetHwnd = imeWnd;
                            break;
                        }
                    }
                } else if (req.type == CommandType::SwitchLayout) {
                    if (req.targetTopHwnd && req.layoutHkl && IsWindow(req.targetTopHwnd)) {
                        const DWORD thread = GetWindowThreadProcessId(req.targetTopHwnd, nullptr);
                        if (thread) {
                            GUITHREADINFO info{};
                            info.cbSize = sizeof(info);
                            HWND target = (GetGUIThreadInfo(thread, &info) && info.hwndFocus && IsWindow(info.hwndFocus))
                                              ? info.hwndFocus
                                              : req.targetTopHwnd;
                            if (PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(req.layoutHkl))) {
                                resp.success = TRUE;
                                resp.targetHwnd = target;
                            } else {
                                struct Broadcast {
                                    HKL hkl;
                                    bool posted;
                                } state{req.layoutHkl, false};
                                EnumThreadWindows(
                                    thread,
                                    [](HWND w, LPARAM p) -> BOOL {
                                        auto* s = reinterpret_cast<Broadcast*>(p);
                                        if (PostMessageW(w, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(s->hkl))) {
                                            s->posted = true;
                                        }
                                        return TRUE;
                                    },
                                    reinterpret_cast<LPARAM>(&state));
                                if (state.posted) {
                                    resp.success = TRUE;
                                    resp.targetHwnd = req.targetTopHwnd;
                                }
                            }
                        }
                    }
                }

                DWORD written = 0;
                WriteFile(hPipe, &resp, sizeof(resp), &written, nullptr);
            }
            DisconnectNamedPipe(hPipe);
        }
    }

    // Wake and cleanly join watchdog thread before cleaning up resources.
    if (hShutdownEvent) {
        SetEvent(hShutdownEvent);
    }
    if (watchdog.joinable()) {
        watchdog.join();
    }
    if (hShutdownEvent) {
        CloseHandle(hShutdownEvent);
    }

    CloseHandle(hPipe);
    if (pSD) LocalFree(pSD);
    CloseHandle(hMutex);
    diag::write(L"helper: server stopped");
    return 0;
}

bool is_running() {
    Request req{CommandType::Ping};
    Response resp{};
    return send_client_request(req, resp);
}

bool launch_elevated() {
    const std::wstring srcPath = autostart::module_path();
    if (srcPath.empty()) {
        return false;
    }

    const std::wstring params = L"--helper " + std::to_wstring(GetCurrentProcessId());

    SHELLEXECUTEINFOW execInfo{};
    execInfo.cbSize = sizeof(execInfo);
    execInfo.lpVerb = L"runas";
    execInfo.lpFile = srcPath.c_str();
    execInfo.lpParameters = params.c_str();
    execInfo.nShow = SW_HIDE;

    if (!ShellExecuteExW(&execInfo)) {
        diag::write(L"helper: ShellExecuteEx runas failed with %u", GetLastError());
        return false;
    }

    // Wait up to 1.5s for helper to initialize and answer ping
    for (int i = 0; i < 15; ++i) {
        Sleep(100);
        if (is_running()) {
            diag::write(L"helper: launch succeeded, helper is answering ping");
            return true;
        }
    }

    return false;
}

bool try_write_conversion(HWND targetTopHwnd, DWORD bits) {
    Request req{};
    req.type = CommandType::WriteConversion;
    req.targetTopHwnd = targetTopHwnd;
    req.conversionBits = bits;

    Response resp{};
    return send_client_request(req, resp);
}

bool try_write_open(HWND targetTopHwnd, bool open) {
    Request req{};
    req.type = CommandType::WriteOpen;
    req.targetTopHwnd = targetTopHwnd;
    req.openStatus = open ? TRUE : FALSE;

    Response resp{};
    return send_client_request(req, resp);
}

bool try_read(HWND targetTopHwnd, bool& open, DWORD& bits) {
    Request req{};
    req.type = CommandType::Read;
    req.targetTopHwnd = targetTopHwnd;

    Response resp{};
    if (send_client_request(req, resp)) {
        open = resp.openStatus != 0;
        bits = resp.conversionBits;
        return true;
    }
    return false;
}

bool try_switch_layout(HWND targetTopHwnd, HKL hkl) {
    if (!targetTopHwnd || !hkl) {
        return false;
    }
    Request req{};
    req.type = CommandType::SwitchLayout;
    req.targetTopHwnd = targetTopHwnd;
    req.layoutHkl = hkl;

    Response resp{};
    return send_client_request(req, resp);
}

bool stop_server() {
    Request req{CommandType::Stop};
    Response resp{};
    return send_client_request(req, resp);
}

} // namespace helper
