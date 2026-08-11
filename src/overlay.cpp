#include "overlay.h"

#include <algorithm>

namespace overlay {
namespace {

constexpr wchar_t kClass[] = L"ImeModePersistenceIndicator";

// Padding around the text and the gap between the caret and the badge.
constexpr int kPadX = 4;
constexpr int kPadY = 2;
constexpr int kGap = 3;

// Layered-window opacity, out of 255. Well below opaque so the badge reads as a
// faint overlay rather than covering what is under it.
constexpr BYTE kAlpha = 160;

HWND g_hwnd = nullptr;
std::wstring g_text;
HFONT g_font = nullptr;
int g_fontHeight = 0;

// Last shown state, so a steady stream of identical positions -- the common case
// while the caret sits still -- does not repaint 10 times a second.
bool g_shown = false;
bool g_lastAbove = false;
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
    lf.lfCharSet = DEFAULT_CHARSET;
    // A CJK-capable UI font. Segoe UI has no Han glyphs, so GetTextExtentPoint32W
    // measured the substituted glyph with the wrong metrics and the badge came
    // out squashed; this face carries the glyphs the badge actually shows.
    wcscpy_s(lf.lfFaceName, L"Microsoft JhengHei UI");
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
    SetLayeredWindowAttributes(g_hwnd, 0, kAlpha, LWA_ALPHA);
    return true;
}

void show(const RECT& caretScreen, const std::wstring& text, bool placeAbove) {
    if (!g_hwnd) {
        return;
    }

    // Nothing moved and the text and placement are the same and it is already up:
    // skip the reposition and repaint entirely.
    if (g_shown && text == g_text && placeAbove == g_lastAbove &&
        EqualRect(&caretScreen, &g_lastCaret)) {
        return;
    }
    g_text = text;
    g_lastCaret = caretScreen;
    g_lastAbove = placeAbove;

    const int caretHeight = caretScreen.bottom - caretScreen.top;
    const int basis = caretHeight > 0 ? caretHeight : 18;
    // ~50% of the caret height, so the badge sits unobtrusively beside the text.
    ensure_font(std::clamp(basis / 2, 11, 22));

    HDC screen = GetDC(nullptr);
    HGDIOBJ previous = SelectObject(screen, g_font);
    SIZE size{};
    GetTextExtentPoint32W(screen, g_text.c_str(), static_cast<int>(g_text.size()), &size);
    SelectObject(screen, previous);
    ReleaseDC(nullptr, screen);

    const int width = size.cx + kPadX * 2;
    const int height = size.cy + kPadY * 2;

    int x;
    int y;
    if (placeAbove) {
        // Above the line, aligned to the caret's left edge, so it does not cover
        // the text in a field whose caret position cannot be tracked.
        x = caretScreen.left;
        y = caretScreen.top - kGap - height;
    } else {
        // Directly to the right of the caret, vertically centred on it and nudged
        // up a little so it reads as sitting beside the insertion point.
        const int caretMid = (caretScreen.top + caretScreen.bottom) / 2;
        x = caretScreen.right + kGap;
        y = caretMid - height / 2 - basis / 5;
    }

    // Keep the badge on the monitor the caret is on, flipping to the other side
    // when there is no room.
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromRect(&caretScreen, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (x + width > mi.rcWork.right) {
            x = placeAbove ? (mi.rcWork.right - width) : (caretScreen.left - kGap - width);
        }
        if (x < mi.rcWork.left) {
            x = mi.rcWork.left;
        }
        if (y < mi.rcWork.top) {
            // No room above: drop below the line instead of off the screen.
            y = caretScreen.bottom + kGap;
        }
        if (y + height > mi.rcWork.bottom) {
            y = mi.rcWork.bottom - height;
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
