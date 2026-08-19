#include "theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace theme {
namespace {

// DWMWA_USE_IMMERSIVE_DARK_MODE settled on 20 in Windows 10 20H1. Builds 18985
// to 19041 used 19, and passing the wrong one simply fails, so both are tried.
constexpr DWORD kDarkModeAttribute = 20;
constexpr DWORD kDarkModeAttributeBefore20H1 = 19;

// The undocumented uxtheme dark-mode entry points, by ordinal. Stable since
// Windows 10 1809 (build 17763) and what the shell / Terminal / Notepad++ use.
// Guarded by the build number because ordinals are reused across versions, so on
// an older OS resolving 133/135 could bind an unrelated export.
enum PreferredAppMode { AppModeDefault, AppModeAllowDark, AppModeForceDark, AppModeForceLight, AppModeMax };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);

SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
AllowDarkModeForWindowFn g_allowDarkModeForWindow = nullptr;
bool g_resolved = false;
bool g_supported = false;

HBRUSH g_bgBrush = nullptr;
HBRUSH g_controlBgBrush = nullptr;

DWORD build_number() {
    // RtlGetVersion is the un-shimmed source; OSVERSIONINFOW shares the layout of
    // RTL_OSVERSIONINFOW, so no winternl.h is needed.
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) {
                return vi.dwBuildNumber;
            }
        }
    }
    return 0;
}

void resolve() {
    if (g_resolved) {
        return;
    }
    g_resolved = true;
    if (build_number() < 17763) {
        return;
    }
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) {
        return;
    }
    g_setPreferredAppMode =
        reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    g_allowDarkModeForWindow =
        reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133)));
    if (g_setPreferredAppMode && g_allowDarkModeForWindow) {
        g_supported = true;
        g_setPreferredAppMode(AppModeAllowDark);
    }
}

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

void allow_dark_window(HWND window) {
    resolve();
    if (g_supported && window) {
        g_allowDarkModeForWindow(window, dark_mode() ? TRUE : FALSE);
    }
}

COLORREF bg() {
    return dark_mode() ? RGB(32, 32, 32) : GetSysColor(COLOR_3DFACE);
}

COLORREF control_bg() {
    return dark_mode() ? RGB(43, 43, 43) : GetSysColor(COLOR_WINDOW);
}

COLORREF text() {
    return dark_mode() ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT);
}

HBRUSH bg_brush() {
    if (!dark_mode()) {
        return GetSysColorBrush(COLOR_3DFACE);
    }
    if (!g_bgBrush) {
        g_bgBrush = CreateSolidBrush(RGB(32, 32, 32));
    }
    return g_bgBrush;
}

HBRUSH control_bg_brush() {
    if (!dark_mode()) {
        return GetSysColorBrush(COLOR_WINDOW);
    }
    if (!g_controlBgBrush) {
        g_controlBgBrush = CreateSolidBrush(RGB(43, 43, 43));
    }
    return g_controlBgBrush;
}

void free_brushes() {
    if (g_bgBrush) {
        DeleteObject(g_bgBrush);
        g_bgBrush = nullptr;
    }
    if (g_controlBgBrush) {
        DeleteObject(g_controlBgBrush);
        g_controlBgBrush = nullptr;
    }
}

void apply_dark_controls(HWND parent) {
    resolve();
    if (!parent) {
        return;
    }
    allow_dark_window(parent);

    const bool dark = g_supported && dark_mode();
    EnumChildWindows(
        parent,
        [](HWND child, LPARAM lp) -> BOOL {
            const bool dark = *reinterpret_cast<bool*>(lp);
            wchar_t cls[64]{};
            GetClassNameW(child, cls, ARRAYSIZE(cls));
            const wchar_t* themeClass = nullptr;  // nullptr resets to the default theme
            if (dark) {
                if (CompareStringOrdinal(cls, -1, L"Edit", -1, TRUE) == CSTR_EQUAL ||
                    CompareStringOrdinal(cls, -1, L"ComboBox", -1, TRUE) == CSTR_EQUAL) {
                    themeClass = L"DarkMode_CFD";
                } else {
                    themeClass = L"DarkMode_Explorer";
                }
            }
            SetWindowTheme(child, themeClass, nullptr);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(const_cast<bool*>(&dark)));

    RedrawWindow(parent, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

} // namespace theme
