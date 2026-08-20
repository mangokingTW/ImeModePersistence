#include "tipbridge.h"

#include <sddl.h>

#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "tip/tip_protocol.h"

namespace tipbridge {
namespace {

ime::Mode from_wire(int32_t mode) {
    switch (static_cast<tip_ipc::WireMode>(mode)) {
    case tip_ipc::WireMode::Alphanumeric: return ime::Mode::Alphanumeric;
    case tip_ipc::WireMode::Native: return ime::Mode::Native;
    default: return ime::Mode::Unknown;
    }
}

tip_ipc::WireMode to_wire(ime::Mode mode) {
    switch (mode) {
    case ime::Mode::Alphanumeric: return tip_ipc::WireMode::Alphanumeric;
    case ime::Mode::Native: return tip_ipc::WireMode::Native;
    default: return tip_ipc::WireMode::Unknown;
    }
}

// What a connected thread last told us, plus the pipe to reach its TIP. Keyed by
// thread id; a Chromium-style process runs many, each its own text service.
struct Record {
    ime::Mode mode{ime::Mode::Unknown};
    HANDLE pipe{INVALID_HANDLE_VALUE};
};

// One mutex guards the whole table and every use of a client's pipe handle: a
// client handler thread only closes its handle under this lock, and set_mode only
// writes under it, so a write can never race a close. The reads and writes
// themselves are tiny fixed-size messages, so holding the lock across a WriteFile
// is cheap.
std::mutex g_mutex;
std::unordered_map<DWORD, Record> g_byTid;

HANDLE g_stopEvent = nullptr;
std::thread g_server;
bool g_running = false;

// A permissive descriptor so the TIP can connect from wherever Windows loaded it:
// GA to Everyone and to all application packages (AppContainer processes such as
// the packaged Notepad and browser renderers), plus a low-integrity label so a
// low-IL sandbox can still write. The pipe carries only a mode enum, never
// anything sensitive, so breadth of access costs nothing here.
SECURITY_ATTRIBUTES* pipe_security() {
    static SECURITY_ATTRIBUTES sa{};
    static PSECURITY_DESCRIPTOR sd = nullptr;
    if (!sd) {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;WD)(A;;GA;;;AC)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr)) {
            return nullptr;
        }
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
    }
    return &sa;
}

void drop_pipe(HANDLE pipe) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_byTid.begin(); it != g_byTid.end();) {
        if (it->second.pipe == pipe) {
            it = g_byTid.erase(it);
        } else {
            ++it;
        }
    }
    CloseHandle(pipe);
}

// Owns one connected TIP: reads its messages until the pipe closes. Blocking reads
// are fine here because each client has its own thread; shutdown closes the stop
// event and the pipe, which unblocks the read.
void client_loop(HANDLE pipe) {
    for (;;) {
        tip_ipc::Message m{};
        DWORD read = 0;
        if (!ReadFile(pipe, &m, sizeof(m), &read, nullptr) || read != sizeof(m)) {
            break;
        }
        const auto type = static_cast<tip_ipc::MsgType>(m.type);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (type == tip_ipc::MsgType::Hello || type == tip_ipc::MsgType::ModeReport) {
            g_byTid[m.tid] = Record{from_wire(m.mode), pipe};
        } else if (type == tip_ipc::MsgType::Bye) {
            g_byTid.erase(m.tid);
        }
    }
    drop_pipe(pipe);
}

void server_loop() {
    while (true) {
        HANDLE pipe = CreateNamedPipeW(
            tip_ipc::kPipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
            sizeof(tip_ipc::Message), sizeof(tip_ipc::Message), 0, pipe_security());
        if (pipe == INVALID_HANDLE_VALUE) {
            // Nothing to serve; wait a beat and retry unless we are stopping.
            if (WaitForSingleObject(g_stopEvent, 500) == WAIT_OBJECT_0) {
                return;
            }
            continue;
        }

        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            // No event to wait the overlapped connect on; drop this instance and
            // retry unless we are stopping. (Also settles the analyzer, which
            // otherwise sees a possibly-null handle reach CloseHandle below.)
            CloseHandle(pipe);
            if (WaitForSingleObject(g_stopEvent, 500) == WAIT_OBJECT_0) {
                return;
            }
            continue;
        }
        const BOOL pending = ConnectNamedPipe(pipe, &ov);
        const DWORD err = GetLastError();

        bool connected = false;
        if (!pending && err == ERROR_PIPE_CONNECTED) {
            connected = true;  // a client beat us to it between create and connect
        } else if (err == ERROR_IO_PENDING) {
            HANDLE waits[2] = {ov.hEvent, g_stopEvent};
            const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0) {
                DWORD moved = 0;
                connected = GetOverlappedResult(pipe, &ov, &moved, FALSE) != 0;
            } else {
                // Stopping: abandon the pending connect and the pipe.
                CancelIo(pipe);
                CloseHandle(ov.hEvent);
                CloseHandle(pipe);
                return;
            }
        }
        CloseHandle(ov.hEvent);

        if (connected) {
            std::thread(client_loop, pipe).detach();
        } else {
            CloseHandle(pipe);
        }
    }
}

} // namespace

void start() {
    if (g_running) {
        return;
    }
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        return;
    }
    g_running = true;
    g_server = std::thread(server_loop);
}

void stop() {
    if (!g_running) {
        return;
    }
    SetEvent(g_stopEvent);
    if (g_server.joinable()) {
        g_server.join();
    }
    // Close remaining client pipes; their handler threads are detached and exit
    // when the read fails. Left to process teardown otherwise.
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<HANDLE> pipes;
    for (auto& kv : g_byTid) {
        pipes.push_back(kv.second.pipe);
    }
    g_byTid.clear();
    for (HANDLE p : pipes) {
        CloseHandle(p);
    }
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    g_running = false;
}

std::optional<ime::Mode> mode_for(DWORD tid) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_byTid.find(tid);
    if (it == g_byTid.end()) {
        return std::nullopt;
    }
    return it->second.mode;
}

bool set_mode(DWORD tid, ime::Mode mode) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_byTid.find(tid);
    if (it == g_byTid.end() || it->second.pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    tip_ipc::Message m{};
    m.type = static_cast<uint32_t>(tip_ipc::MsgType::SetMode);
    m.pid = 0;
    m.tid = tid;
    m.mode = static_cast<int32_t>(to_wire(mode));
    DWORD written = 0;
    return WriteFile(it->second.pipe, &m, sizeof(m), &written, nullptr) && written == sizeof(m);
}

namespace {

bool call_self_reg(const std::wstring& dllPath, const char* entry) {
    HMODULE dll = LoadLibraryW(dllPath.c_str());
    if (!dll) {
        return false;
    }
    // DllRegisterServer/DllUnregisterServer are STDAPI == __stdcall (WINAPI).
    using Fn = HRESULT(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(GetProcAddress(dll, entry));
    const bool ok = fn && SUCCEEDED(fn());
    FreeLibrary(dll);
    return ok;
}

} // namespace

bool register_tip(const std::wstring& dllPath) {
    return call_self_reg(dllPath, "DllRegisterServer");
}

bool unregister_tip(const std::wstring& dllPath) {
    return call_self_reg(dllPath, "DllUnregisterServer");
}

} // namespace tipbridge
