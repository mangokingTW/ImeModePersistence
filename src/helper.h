#pragma once

#include <windows.h>
#include <string>

namespace helper {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\ImeModePersistence.Sidecar";
constexpr wchar_t kHelperMutex[] = L"Local\\ImeModePersistence.Helper.SingleInstance";

enum class CommandType : DWORD {
    Ping = 1,
    WriteConversion = 2,
    WriteOpen = 3,
    Stop = 4,
    Read = 5,
};

struct Request {
    CommandType type{CommandType::Ping};
    HWND targetTopHwnd{nullptr};
    DWORD conversionBits{0};
    BOOL openStatus{FALSE};
};

struct Response {
    BOOL success{FALSE};
    DWORD writtenToHwnd{0};
    BOOL openStatus{FALSE};
    DWORD conversionBits{0};
};

// Server (Runs in elevated helper process via --helper)
int run_server();

// Client (Used by main Store / unelevated process)
bool is_running();
bool launch_elevated();
bool try_write_conversion(HWND targetTopHwnd, DWORD bits);
bool try_write_open(HWND targetTopHwnd, bool open);
bool try_read(HWND targetTopHwnd, bool& open, DWORD& bits);
bool stop_server();

} // namespace helper
