#include "helper.h"
#include "autostart.h"
#include "diagnostic.h"

#include <imm.h>
#include <sddl.h>
#include <shellapi.h>

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

    HANDLE hParent = nullptr;
    if (parentPid != 0) {
        hParent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        if (!hParent) {
            diag::write(L"helper: could not open parent process %u (error %u)", parentPid, GetLastError());
        }
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

    HANDLE hPipe = CreateNamedPipeW(
        kPipeName,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        512,
        512,
        0,
        &sa);

    if (hPipe == INVALID_HANDLE_VALUE) {
        diag::write(L"helper: CreateNamedPipe failed with %u", GetLastError());
        if (pSD) LocalFree(pSD);
        if (hParent) CloseHandle(hParent);
        CloseHandle(hMutex);
        return 1;
    }

    HANDLE hConnectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hConnectEvent) {
        diag::write(L"helper: CreateEvent failed with %u", GetLastError());
        CloseHandle(hPipe);
        if (pSD) LocalFree(pSD);
        if (hParent) CloseHandle(hParent);
        CloseHandle(hMutex);
        return 1;
    }

    diag::write(L"helper: named pipe created and listening");

    OVERLAPPED ol{};
    ol.hEvent = hConnectEvent;

    bool running = true;
    while (running) {
        ResetEvent(hConnectEvent);
        const BOOL connected = ConnectNamedPipe(hPipe, &ol);
        if (!connected) {
            const DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                SetEvent(hConnectEvent);
            } else if (err != ERROR_IO_PENDING) {
                Sleep(10);
                continue;
            }
        }

        HANDLE waitHandles[2] = { hConnectEvent, hParent };
        const DWORD waitCount = hParent ? 2 : 1;
        const DWORD waitRes = WaitForMultipleObjects(waitCount, waitHandles, FALSE, INFINITE);

        if (waitRes == WAIT_OBJECT_0) {
            DWORD unused = 0;
            if (GetOverlappedResult(hPipe, &ol, &unused, TRUE)) {
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
                    }

                    DWORD written = 0;
                    WriteFile(hPipe, &resp, sizeof(resp), &written, nullptr);
                }
            }
            DisconnectNamedPipe(hPipe);
        } else if (hParent && waitRes == WAIT_OBJECT_0 + 1) {
            diag::write(L"helper: parent process %u terminated, exiting", parentPid);
            CancelIo(hPipe);
            running = false;
        }
    }

    CloseHandle(hConnectEvent);
    CloseHandle(hPipe);
    if (pSD) LocalFree(pSD);
    if (hParent) CloseHandle(hParent);
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

bool stop_server() {
    Request req{CommandType::Stop};
    Response resp{};
    return send_client_request(req, resp);
}

} // namespace helper
