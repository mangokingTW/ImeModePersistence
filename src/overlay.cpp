#include "overlay.h"

#include <algorithm>

namespace overlay {
namespace {

constexpr wchar_t kClass[] = L"ImeModePersistenceIndicator";

// Padding around the text and the gap between the caret and the badge.
constexpr int kPadX = 7;
constexpr int kPadY = 3;
constexpr int kGap = 2;

HWND g_hwnd = nullptr;
std::wstring g_text;
HFONT g_font = nullptr;
int g_fontHeight = 0;

// Last shown state, so a steady stream of identical positions -- the common case
// while the caret sits still -- does not repaint 10 times a second.
bool g_shown = false;
RECT g_lastCaret{};

// A font sized to the caret so the badge tracks the target's text size and DPI.
// Recreated only when the height changes.
HFONT ensure_font(int height) {
    if (g_font && height == g_fontHeight) {
        return g_font;
    }
    if (g_font) {
        DeleteObject(g_font);
        g_font = nullptr;
    }
    LOGFONTW lf{};
    lf.lfHeight = -height;
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfQuality = CLEARTYPE_QUALITY;
    // Segoe UI has no Han glyphs; the font mapper substitutes one that does for
    // the CJK badge characters, which is fine for a single glyph.
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_font = CreateFontIndirectW(&lf);
    g_fontHeight = height;
    return g_font;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        // Never the target of a hit; clicks fall through to whatever is beneath.
        return HTTRANSPARENT;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        HBRUSH background = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(dc, &rc, background);
        DeleteObject(background);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 245, 245));
        HGDIOBJ previous = SelectObject(dc, ensure_font(g_fontHeight));
        DrawTextW(dc, g_text.c_str(), -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);

        EndPaint(hwnd, &ps);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

bool init(HINSTANCE instance, HWND owner) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc); // a second registration is a harmless no-op

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kClass, L"", WS_POPUP,
        0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (!g_hwnd) {
        return false;
    }
    SetLayeredWindowAttributes(g_hwnd, 0, 225, LWA_ALPHA);
    return true;
}

void show(const RECT& caretScreen, const std::wstring& text) {
    if (!g_hwnd) {
        return;
    }

    // Nothing moved and the text is the same and it is already up: skip the
    // reposition and repaint entirely.
    if (g_shown && text == g_text &&
        EqualRect(&caretScreen, &g_lastCaret)) {
        return;
    }
    g_text = text;
    g_lastCaret = caretScreen;

    const int caretHeight = caretScreen.bottom - caretScreen.top;
    ensure_font(std::clamp(caretHeight > 0 ? caretHeight : 18, 14, 36));

    HDC screen = GetDC(nullptr);
    HGDIOBJ previous = SelectObject(screen, g_font);
    SIZE size{};
    GetTextExtentPoint32W(screen, g_text.c_str(), static_cast<int>(g_text.size()), &size);
    SelectObject(screen, previous);
    ReleaseDC(nullptr, screen);

    const int width = size.cx + kPadX * 2;
    const int height = size.cy + kPadY * 2;

    int x = caretScreen.left;
    int y = caretScreen.bottom + kGap;

    // Keep the badge on the monitor the caret is on, flipping above the caret
    // when there is no room below.
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromRect(&caretScreen, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (x + width > mi.rcWork.right) {
            x = mi.rcWork.right - width;
        }
        if (x < mi.rcWork.left) {
            x = mi.rcWork.left;
        }
        if (y + height > mi.rcWork.bottom) {
            y = caretScreen.top - kGap - height;
        }
        if (y < mi.rcWork.top) {
            y = mi.rcWork.top;
        }
    }

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hwnd, nullptr, TRUE);
    UpdateWindow(g_hwnd);
    g_shown = true;
}

void hide() {
    if (g_hwnd && g_shown) {
        ShowWindow(g_hwnd, SW_HIDE);
        g_shown = false;
    }
}

void destroy() {
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    if (g_font) {
        DeleteObject(g_font);
        g_font = nullptr;
        g_fontHeight = 0;
    }
}

} // namespace overlay
