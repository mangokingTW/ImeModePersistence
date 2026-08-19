#include "config_dialog.h"

#include <commdlg.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

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

    if (HICON small = static_cast<HICON>(LoadImageW(
            module, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0))) {
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));
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

    if (!state.lastApplication.empty()) {
        SetDlgItemTextW(dialog, IDC_EXECUTABLE, state.lastApplication.c_str());
    }

    set_hint(dialog, text::s().hintIntro);
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

    case WM_SETTINGCHANGE:
        // Following the user switching light/dark while the dialog is open costs
        // one comparison and avoids a title bar that contradicts the rest of the
        // desktop until the dialog is reopened.
        if (theme::is_colour_change(lParam)) {
            theme::apply_titlebar(dialog);
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
