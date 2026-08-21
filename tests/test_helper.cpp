#include "check.h"
#include "helper.h"

void run_helper_tests() {
    // 1. Packet structure integrity
    CHECK(sizeof(helper::Request) >= 16);
    CHECK(sizeof(helper::Response) >= 16);

    helper::Request req{};
    req.type = helper::CommandType::WriteConversion;
    req.targetTopHwnd = reinterpret_cast<HWND>(0x1234);
    req.conversionBits = 1;
    CHECK(req.type == helper::CommandType::WriteConversion);
    CHECK(req.targetTopHwnd == reinterpret_cast<HWND>(0x1234));
    CHECK(req.conversionBits == 1);

    helper::Response resp{};
    resp.success = TRUE;
    resp.targetHwnd = reinterpret_cast<HWND>(0x5678);
    CHECK(resp.success);
    CHECK(resp.targetHwnd == reinterpret_cast<HWND>(0x5678));

    // 2. Client fallback when helper is offline
    // When helper server is not running, client calls must return false quickly without blocking or crashing.
    if (!helper::is_running()) {
        bool open = false;
        DWORD bits = 0;
        CHECK(!helper::try_read(nullptr, open, bits));
        CHECK(!helper::try_write_conversion(nullptr, 1));
        CHECK(!helper::try_write_open(nullptr, true));
    }
}
