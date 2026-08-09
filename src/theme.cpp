#include "theme.h"

#include <dwmapi.h>

namespace theme {
namespace {

// DWMWA_USE_IMMERSIVE_DARK_MODE settled on 20 in Windows 10 20H1. Builds 18985
// to 19041 used 19, and passing the wrong one simply fails, so both are tried.
constexpr DWORD kDarkModeAttribute = 20;
constexpr DWORD kDarkModeAttributeBefore20H1 = 19;

} // namespace

bool dark_mode() {
    DWORD appsUseLightTheme = 1;
    DWORD bytes = sizeof(appsUseLightTheme);

    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &appsUseLightTheme,
                     &bytes) != ERROR_SUCCESS) {
        // The value is absent on editions that never had the setting; light is
        // the right assumption there.
        return false;
    }

    return appsUseLightTheme == 0;
}

void apply_titlebar(HWND window) {
    if (!window) {
        return;
    }

    const BOOL dark = dark_mode() ? TRUE : FALSE;

    if (FAILED(DwmSetWindowAttribute(window, kDarkModeAttribute, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(window, kDarkModeAttributeBefore20H1, &dark, sizeof(dark));
    }
}

bool is_colour_change(LPARAM lParam) {
    const wchar_t* section = reinterpret_cast<const wchar_t*>(lParam);
    if (!section) {
        return false;
    }
    return CompareStringOrdinal(section, -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL;
}

} // namespace theme
