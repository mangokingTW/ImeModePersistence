#include "config_dialog.h"

#include <commdlg.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

// gdiplus.h needs COM types (interface, IStream, PROPID) that WIN32_LEAN_AND_MEAN
// trims from windows.h, and uses unqualified min/max that NOMINMAX removes -- so
// pull the COM headers in and bring std::min/std::max into scope before it.
// Contained to this translation unit.
#include <objbase.h>
#include <objidl.h>
using std::max;
using std::min;
#include <gdiplus.h>

#include "layout.h"
#include "resource.h"
#include "rules.h"
#include "strings.h"
#include "theme.h"

namespace config {
namespace {

struct State {
    std::wstring lastApplication;
    std::wstring lastWindowClass;
    std::vector<layout::Installed> layouts;
    std::vector<rules::Rule> rules;   // parallel to the list box items
};

// The dialog is modal to a hidden owner, which leaves the tray menu live and
// able to ask for a second copy. One is tracked so the request raises the
// existing window instead of nesting another modal loop.
HWND g_open;

State* state_of(HWND dialog) {
    return reinterpret_cast<State*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
}

void apply_icon(HWND dialog) {
    HINSTANCE module = GetModuleHandleW(nullptr);

    if (HICON large = static_cast<HICON>(LoadImageW(
            module, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0))) {
        SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
    }

    if (HICON smallIcon = static_cast<HICON>(LoadImageW(
            module, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0))) {
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
}

void set_hint(HWND dialog, const wchar_t* hint) {
    SetDlgItemTextW(dialog, IDC_HINT, hint);
}

void apply_language(HWND dialog) {
    const text::Strings& t = text::s();

    SetWindowTextW(dialog, t.rulesCaption);
    SetDlgItemTextW(dialog, IDC_GROUP, t.groupAddUpdate);
    SetDlgItemTextW(dialog, IDC_BROWSE, t.buttonBrowse);
    SetDlgItemTextW(dialog, IDC_USE_LAST, t.buttonUseLast);
    SetDlgItemTextW(dialog, IDC_USE_CLASS, t.buttonUseClass);
    SetDlgItemTextW(dialog, IDC_ADD, t.buttonAdd);
    SetDlgItemTextW(dialog, IDC_REMOVE, t.buttonRemove);
    SetDlgItemTextW(dialog, IDC_ONCE, t.ruleApplyOnce);
    SetDlgItemTextW(dialog, IDC_DEFAULT_GROUP, t.defaultGroup);
    SetDlgItemTextW(dialog, IDC_DEFAULT_ENABLE, t.defaultEnable);
    SetDlgItemTextW(dialog, IDC_DEFAULT_ONCE, t.ruleApplyOnce);
    SetDlgItemTextW(dialog, IDOK, t.buttonClose);

    // The static labels share IDC_STATIC, so they are addressed by position
    // rather than by control id.
    HWND child = GetWindow(dialog, GW_CHILD);
    int statics = 0;
    while (child) {
        wchar_t className[16]{};
        GetClassNameW(child, className, ARRAYSIZE(className));
        if (CompareStringOrdinal(className, -1, L"Static", -1, TRUE) == CSTR_EQUAL &&
            GetDlgCtrlID(child) == IDC_STATIC) {
            switch (statics++) {
            case 0: {
                std::wstring header = t.rulesHeader;
                header += L"    ";
                header += t.ruleReorderHint;
                SetWindowTextW(child, header.c_str());
                break;
            }
            case 1: SetWindowTextW(child, t.labelExecutable); break;
            case 2: SetWindowTextW(child, t.labelLayout); break;
            default: break;
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

// A full path easily outruns the list box, so the scrollable width has to be
// measured rather than guessed.
void update_horizontal_extent(HWND list) {
    HDC dc = GetDC(list);
    if (!dc) {
        return;
    }
    HGDIOBJ previous = SelectObject(dc, reinterpret_cast<HGDIOBJ>(
                                            SendMessageW(list, WM_GETFONT, 0, 0)));

    int widest = 0;
    const int count = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        const int chars = static_cast<int>(SendMessageW(list, LB_GETTEXTLEN, static_cast<WPARAM>(i), 0));
        if (chars <= 0) {
            continue;
        }

        std::vector<wchar_t> buffer(static_cast<size_t>(chars) + 1);
        SendMessageW(list, LB_GETTEXT, static_cast<WPARAM>(i),
                     reinterpret_cast<LPARAM>(buffer.data()));

        SIZE size{};
        if (GetTextExtentPoint32W(dc, buffer.data(), chars, &size) && size.cx > widest) {
            widest = size.cx;
        }
    }

    if (previous) {
        SelectObject(dc, previous);
    }
    ReleaseDC(list, dc);

    // Tab expansion is not accounted for by GetTextExtentPoint32, so leave room.
    SendMessageW(list, LB_SETHORIZONTALEXTENT, static_cast<WPARAM>(widest + 40), 0);
}

void fill_layouts(HWND dialog, State& state) {
    state.layouts = layout::installed();

    // Both the per-rule combo and the default-language combo offer the same list.
    for (const int id : {IDC_LAYOUT, IDC_DEFAULT_LANG}) {
        HWND combo = GetDlgItem(dialog, id);
        for (const layout::Installed& entry : state.layouts) {
            std::wstring label = entry.name;
            if (entry.is_ime) {
                label += text::s().suffixIme;
            }
            const int index = static_cast<int>(
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
            if (index >= 0) {
                SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), entry.language);
            }
        }
        if (!state.layouts.empty()) {
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
        }
    }
}

void select_language(HWND combo, LANGID language) {
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        if (static_cast<LANGID>(
                SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(i), 0)) == language) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
}

void update_default_enabled(HWND dialog) {
    const BOOL on = IsDlgButtonChecked(dialog, IDC_DEFAULT_ENABLE) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_DEFAULT_LANG), on);
    EnableWindow(GetDlgItem(dialog, IDC_DEFAULT_ONCE), on);
}

void load_default(HWND dialog) {
    const rules::Default d = rules::default_binding();
    CheckDlgButton(dialog, IDC_DEFAULT_ENABLE, d.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_DEFAULT_ONCE, d.applyOnce ? BST_CHECKED : BST_UNCHECKED);
    if (d.enabled) {
        select_language(GetDlgItem(dialog, IDC_DEFAULT_LANG), d.language);
    }
    update_default_enabled(dialog);
}

void save_default(HWND dialog) {
    const bool enabled = IsDlgButtonChecked(dialog, IDC_DEFAULT_ENABLE) == BST_CHECKED;
    HWND combo = GetDlgItem(dialog, IDC_DEFAULT_LANG);
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    const LANGID language = index == CB_ERR
        ? 0
        : static_cast<LANGID>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0));
    const bool once = IsDlgButtonChecked(dialog, IDC_DEFAULT_ONCE) == BST_CHECKED;
    rules::set_default(enabled, language, once);
}

void fill_rules(HWND dialog, State& state) {
    HWND list = GetDlgItem(dialog, IDC_RULE_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);

    state.rules = rules::load_ordered();

    for (const rules::Rule& rule : state.rules) {
        // Layout first, path second: layout names have a bounded length, so the
        // columns stay aligned however long the path is.
        std::wstring text = layout::describe(rule.language);
        // Against the list the dialog already fetched, not find_by_language:
        // that would re-enumerate every installed layout twice per rule.
        const bool installed = std::any_of(
            state.layouts.begin(), state.layouts.end(),
            [&rule](const layout::Installed& entry) {
                return entry.language == rule.language;
            });
        if (!installed) {
            // The rule names a layout the user has since removed.
            text += text::s().suffixNotInstalled;
        }
        text += L"\t";
        text += rule.executable;
        if (rule.applyOnce) {
            text += text::s().ruleOnceSuffix;
        }

        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    update_horizontal_extent(list);
}

// Classic subclass of the rule list so items can be dragged into a new order.
// One dialog is open at a time, so a file-scope original proc is safe.
WNDPROC g_listProc = nullptr;
int g_dragFrom = -1;

void perform_drag(HWND list, int from, int to) {
    HWND dialog = GetParent(list);
    State* state = state_of(dialog);
    if (!state) {
        return;
    }
    const int count = static_cast<int>(state->rules.size());
    if (from < 0 || from >= count) {
        return;
    }
    if (to >= count) {
        to = count - 1;
    }
    if (to < 0 || from == to) {
        return;
    }

    std::vector<rules::Rule> reordered = state->rules;
    const rules::Rule moved = reordered[static_cast<size_t>(from)];
    reordered.erase(reordered.begin() + from);
    reordered.insert(reordered.begin() + to, moved);

    std::vector<std::wstring> order;
    order.reserve(reordered.size());
    for (const rules::Rule& rule : reordered) {
        order.push_back(rule.executable);
    }
    rules::reorder(order);

    fill_rules(dialog, *state);
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(to), 0);
}

LRESULT CALLBACK list_proc(HWND list, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_LBUTTONDOWN: {
        const DWORD hit = static_cast<DWORD>(SendMessageW(list, LB_ITEMFROMPOINT, 0, lParam));
        if (HIWORD(hit) == 0) {
            g_dragFrom = static_cast<int>(LOWORD(hit));
            SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(g_dragFrom), 0);
            SetFocus(list);
            SetCapture(list);
            // Notify the dialog so the composer loads the picked rule, as a
            // normal click-selection would.
            SendMessageW(GetParent(list), WM_COMMAND,
                         MAKEWPARAM(static_cast<WORD>(GetDlgCtrlID(list)), LBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(list));
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (g_dragFrom >= 0 && GetCapture() == list) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_dragFrom >= 0) {
            ReleaseCapture();
            const DWORD hit = static_cast<DWORD>(SendMessageW(list, LB_ITEMFROMPOINT, 0, lParam));
            const int count = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
            int to = static_cast<int>(LOWORD(hit));
            if (HIWORD(hit) != 0) {
                // Dropped past the items: clamp to the nearer end.
                to = GET_Y_LPARAM(lParam) < 0 ? 0 : count - 1;
            }
            const int from = g_dragFrom;
            g_dragFrom = -1;
            perform_drag(list, from, to);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        g_dragFrom = -1;
        break;
    default:
        break;
    }
    return CallWindowProcW(g_listProc, list, message, wParam, lParam);
}

void subclass_list(HWND list) {
    // g_listProc holds the standard list box's own procedure. It is captured once
    // (the class is the same for every instance) so a re-open never chains
    // list_proc onto itself.
    WNDPROC previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(list, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(list_proc)));
    if (!g_listProc) {
        g_listProc = previous;
    }
}

// ---- Toggle switches -------------------------------------------------------
// The three checkboxes (apply-once, default-enable, default-once) are painted as
// Windows 11-style toggle switches. They stay AUTOCHECKBOX controls -- so they
// keep their checked state, keyboard toggle, and checkbox accessibility role --
// and are only subclassed to replace the painting.

ULONG_PTR g_gdiplusToken = 0;
WNDPROC g_toggleProc = nullptr;

void draw_toggle(HWND ctrl, HDC dc) {
    RECT rc{};
    GetClientRect(ctrl, &rc);
    const int width = rc.right;
    const int height = rc.bottom;

    const UINT dpi = GetDpiForWindow(ctrl);
    const auto px = [dpi](double dip) { return static_cast<int>(dip * dpi / 96.0 + 0.5); };

    const bool checked = SendMessageW(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool enabled = IsWindowEnabled(ctrl) != FALSE;
    const bool focused = GetFocus() == ctrl;

    // Paint the dialog's own background first, so the rounded track sits on the
    // real body colour with no grey box behind the corners.
    HBRUSH bg = reinterpret_cast<HBRUSH>(SendMessageW(GetParent(ctrl), WM_CTLCOLORSTATIC,
                    reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(ctrl)));
    if (!bg) {
        bg = GetSysColorBrush(COLOR_3DFACE);
    }
    FillRect(dc, &rc, bg);

    const bool dark = theme::dark_mode();
    const BYTE alpha = enabled ? 255 : 110;
    const auto col = [alpha](BYTE r, BYTE g, BYTE b) { return Gdiplus::Color(alpha, r, g, b); };

    const int trackW = px(40);
    const int trackH = px(20);
    const int tx = px(2);
    const int ty = (height - trackH) / 2;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::GraphicsPath capsule;
    capsule.AddArc(tx, ty, trackH, trackH, 90.0f, 180.0f);
    capsule.AddArc(tx + trackW - trackH, ty, trackH, trackH, 270.0f, 180.0f);
    capsule.CloseFigure();

    if (checked) {
        Gdiplus::SolidBrush track(dark ? col(76, 194, 255) : col(0, 95, 184));
        g.FillPath(&track, &capsule);
        const int d = px(16);
        const int cx = tx + trackW - px(3) - d;
        const int cy = ty + (trackH - d) / 2;
        Gdiplus::SolidBrush thumb(dark ? col(20, 20, 20) : col(255, 255, 255));
        g.FillEllipse(&thumb, cx, cy, d, d);
    } else {
        Gdiplus::SolidBrush fill(dark ? col(43, 43, 43) : col(255, 255, 255));
        g.FillPath(&fill, &capsule);
        Gdiplus::Pen border(dark ? col(157, 157, 157) : col(138, 138, 138),
                            static_cast<Gdiplus::REAL>(px(1.3)));
        g.DrawPath(&border, &capsule);
        const int d = px(12);
        const int cx = tx + px(3);
        const int cy = ty + (trackH - d) / 2;
        Gdiplus::SolidBrush thumb(dark ? col(208, 208, 208) : col(93, 93, 93));
        g.FillEllipse(&thumb, cx, cy, d, d);
    }

    // The label, in the control's font, to the right of the toggle.
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(ctrl, WM_GETFONT, 0, 0));
    HFONT old = font ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    if (dark) {
        SetTextColor(dc, enabled ? theme::text() : RGB(140, 140, 140));
    } else {
        SetTextColor(dc, GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
    }
    wchar_t label[256]{};
    GetWindowTextW(ctrl, label, ARRAYSIZE(label));
    RECT text{tx + trackW + px(8), 0, width, height};
    DrawTextW(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (old) {
        SelectObject(dc, old);
    }

    if (focused) {
        RECT ring{tx + trackW + px(6), (height - px(14)) / 2, width, (height + px(14)) / 2};
        DrawFocusRect(dc, &ring);
    }
}

LRESULT CALLBACK toggle_proc(HWND ctrl, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(ctrl, &ps);
        RECT rc{};
        GetClientRect(ctrl, &rc);
        // Double-buffered so the state flip does not flicker.
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP oldBmp = reinterpret_cast<HBITMAP>(SelectObject(mem, bmp));
        draw_toggle(ctrl, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(ctrl, &ps);
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(ctrl, nullptr, FALSE);
        break;
    default:
        break;
    }
    return CallWindowProcW(g_toggleProc, ctrl, message, wParam, lParam);
}

void subclass_toggle(HWND ctrl) {
    WNDPROC previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(ctrl, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(toggle_proc)));
    if (!g_toggleProc) {
        g_toggleProc = previous;
    }
}

LANGID selected_language(HWND dialog) {
    HWND combo = GetDlgItem(dialog, IDC_LAYOUT);
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index == CB_ERR) {
        return 0;
    }
    return static_cast<LANGID>(SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0));
}

void on_browse(HWND dialog) {
    wchar_t path[1024]{};
    GetDlgItemTextW(dialog, IDC_EXECUTABLE, path, ARRAYSIZE(path));

    // A bare file name in the field is not a valid initial path, and passing one
    // makes the dialog open somewhere arbitrary.
    if (rules::file_name_of(path) == path) {
        path[0] = L'\0';
    }

    OPENFILENAMEW open{};
    open.lStructSize = sizeof(open);
    open.hwndOwner = dialog;
    open.lpstrFilter = text::s().browseFilter;
    open.lpstrFile = path;
    open.nMaxFile = ARRAYSIZE(path);
    open.lpstrTitle = text::s().browseTitle;
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&open)) {
        // Cancelling is not an error, and CommDlgExtendedError only distinguishes
        // the two for callers that care.
        return;
    }

    SetDlgItemTextW(dialog, IDC_EXECUTABLE, path);
    set_hint(dialog, text::s().hintPickLayout);
}

void on_add(HWND dialog, State& state) {
    wchar_t executable[1024]{};
    GetDlgItemTextW(dialog, IDC_EXECUTABLE, executable, ARRAYSIZE(executable));

    if (executable[0] == L'\0') {
        set_hint(dialog, text::s().hintNeedExecutable);
        return;
    }

    const LANGID language = selected_language(dialog);
    if (language == 0) {
        set_hint(dialog, text::s().hintNeedLayout);
        return;
    }

    // Keep an existing rule where it is; send a brand-new rule to the bottom of a
    // hand-ordered list (or leave it unplaced when nothing has been dragged yet).
    unsigned order = 0;
    unsigned maxOrder = 0;
    bool existing = false;
    for (const rules::Rule& rule : state.rules) {
        maxOrder = std::max(maxOrder, rule.order);
        if (CompareStringOrdinal(rule.executable.c_str(), -1, executable, -1, TRUE) == CSTR_EQUAL) {
            order = rule.order;
            existing = true;
        }
    }
    if (!existing && maxOrder > 0) {
        order = maxOrder + 1;
    }

    const bool applyOnce = IsDlgButtonChecked(dialog, IDC_ONCE) == BST_CHECKED;
    if (!rules::set(executable, language, applyOnce, order)) {
        set_hint(dialog, text::s().hintWriteFailed);
        return;
    }

    fill_rules(dialog, state);

    wchar_t message[1280]{};
    StringCchPrintfW(message, ARRAYSIZE(message), text::s().hintBoundFormat,
                     executable, layout::describe(language).c_str());
    set_hint(dialog, message);
}

void on_remove(HWND dialog, State& state) {
    HWND list = GetDlgItem(dialog, IDC_RULE_LIST);
    const int index = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR || static_cast<size_t>(index) >= state.rules.size()) {
        set_hint(dialog, text::s().hintSelectRule);
        return;
    }

    const std::wstring executable = state.rules[static_cast<size_t>(index)].executable;
    if (!rules::clear(executable)) {
        set_hint(dialog, text::s().hintRemoveFailed);
        return;
    }

    fill_rules(dialog, state);

    wchar_t message[1280]{};
    StringCchPrintfW(message, ARRAYSIZE(message), text::s().hintRemovedFormat,
                     executable.c_str());
    set_hint(dialog, message);
}

void on_use_last(HWND dialog, const State& state) {
    if (state.lastApplication.empty()) {
        set_hint(dialog, text::s().hintNoLastApp);
        return;
    }
    SetDlgItemTextW(dialog, IDC_EXECUTABLE, state.lastApplication.c_str());
    set_hint(dialog, text::s().hintPickLayout);
}

void on_use_class(HWND dialog, const State& state) {
    if (state.lastWindowClass.empty()) {
        set_hint(dialog, text::s().hintNoLastClass);
        return;
    }

    // The stored key carries the prefix, so what goes in the field is what gets
    // written -- no hidden transformation between the two.
    const std::wstring key = rules::kClassPrefix + state.lastWindowClass;
    SetDlgItemTextW(dialog, IDC_EXECUTABLE, key.c_str());
    set_hint(dialog, text::s().hintClassRule);
}

// Selecting a rule loads it into the composer so it can be edited: the
// executable, its language, and its apply-once choice. Re-adding overwrites the
// rule, since the executable key is the same.
void on_select(HWND dialog, const State& state) {
    HWND list = GetDlgItem(dialog, IDC_RULE_LIST);
    const int index = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR || static_cast<size_t>(index) >= state.rules.size()) {
        return;
    }
    const rules::Rule& rule = state.rules[static_cast<size_t>(index)];

    SetDlgItemTextW(dialog, IDC_EXECUTABLE, rule.executable.c_str());
    CheckDlgButton(dialog, IDC_ONCE, rule.applyOnce ? BST_CHECKED : BST_UNCHECKED);

    HWND combo = GetDlgItem(dialog, IDC_LAYOUT);
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        if (static_cast<LANGID>(
                SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(i), 0)) == rule.language) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            break;
        }
    }
}

void on_init(HWND dialog, State& state) {
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);

    apply_language(dialog);
    apply_icon(dialog);
    theme::apply_titlebar(dialog);

    // One tab stop so the path column lines up; the units are quarters of the
    // dialog font's average character width.
    const int tabStop = 200;
    SendMessageW(GetDlgItem(dialog, IDC_RULE_LIST), LB_SETTABSTOPS, 1,
                 reinterpret_cast<LPARAM>(&tabStop));

    fill_layouts(dialog, state);
    fill_rules(dialog, state);
    load_default(dialog);
    subclass_list(GetDlgItem(dialog, IDC_RULE_LIST));
    for (const int id : {IDC_ONCE, IDC_DEFAULT_ENABLE, IDC_DEFAULT_ONCE}) {
        subclass_toggle(GetDlgItem(dialog, id));
    }

    if (!state.lastApplication.empty()) {
        SetDlgItemTextW(dialog, IDC_EXECUTABLE, state.lastApplication.c_str());
    }

    set_hint(dialog, text::s().hintIntro);

    // Dark mode: opt the dialog and its controls into the app's dark visuals when
    // the user is in dark mode. A no-op (light body) on Windows before 1809.
    theme::allow_dark_window(dialog);
    theme::apply_dark_controls(dialog);
}

INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        g_open = dialog;
        SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
        State* state = reinterpret_cast<State*>(lParam);
        if (state) {
            on_init(dialog, *state);
        }
        return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (theme::dark_mode()) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, theme::bg());
            SetTextColor(dc, theme::text());
            return reinterpret_cast<INT_PTR>(theme::bg_brush());
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (theme::dark_mode()) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, theme::control_bg());
            SetTextColor(dc, theme::text());
            return reinterpret_cast<INT_PTR>(theme::control_bg_brush());
        }
        break;

    case WM_SETTINGCHANGE:
        // Follow a light/dark switch made while the dialog is open: re-theme the
        // title bar, controls and toggles, and repaint the body.
        if (theme::is_colour_change(lParam)) {
            theme::free_brushes();
            theme::apply_titlebar(dialog);
            theme::allow_dark_window(dialog);
            theme::apply_dark_controls(dialog);
            InvalidateRect(dialog, nullptr, TRUE);
        }
        break;

    case WM_DESTROY:
        theme::free_brushes();
        if (g_gdiplusToken) {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        break;

    case WM_COMMAND: {
        State* state = state_of(dialog);
        if (!state) {
            break;
        }

        switch (LOWORD(wParam)) {
        case IDC_RULE_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                on_select(dialog, *state);
            }
            return TRUE;
        case IDC_BROWSE:
            on_browse(dialog);
            return TRUE;
        case IDC_ADD:
            on_add(dialog, *state);
            return TRUE;
        case IDC_REMOVE:
            on_remove(dialog, *state);
            return TRUE;
        case IDC_USE_LAST:
            on_use_last(dialog, *state);
            return TRUE;
        case IDC_USE_CLASS:
            on_use_class(dialog, *state);
            return TRUE;
        case IDC_DEFAULT_ENABLE:
            update_default_enabled(dialog);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            save_default(dialog);
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        default:
            break;
        }
        break;
    }

    default:
        break;
    }

    return FALSE;
}

} // namespace

void show_rules(HINSTANCE instance,
                HWND owner,
                const std::wstring& lastApplication,
                const std::wstring& lastWindowClass) {
    if (g_open) {
        if (IsIconic(g_open)) {
            ShowWindow(g_open, SW_RESTORE);
        }
        SetForegroundWindow(g_open);
        return;
    }

    State state;
    state.lastApplication = lastApplication;
    state.lastWindowClass = lastWindowClass;

    DialogBoxParamW(
        instance,
        MAKEINTRESOURCEW(IDD_RULES),
        owner,
        dialog_proc,
        reinterpret_cast<LPARAM>(&state));

    g_open = nullptr;
}

} // namespace config
