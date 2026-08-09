#include "config_dialog.h"

#include <strsafe.h>

#include <vector>

#include "layout.h"
#include "resource.h"
#include "rules.h"

namespace config {
namespace {

struct State {
    std::wstring lastApplication;
    std::vector<layout::Installed> layouts;
    std::vector<rules::Rule> rules;   // parallel to the list box items
};

State* state_of(HWND dialog) {
    return reinterpret_cast<State*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
}

void set_hint(HWND dialog, const wchar_t* text) {
    SetDlgItemTextW(dialog, IDC_HINT, text);
}

void fill_layouts(HWND dialog, State& state) {
    HWND combo = GetDlgItem(dialog, IDC_LAYOUT);
    state.layouts = layout::installed();

    for (const layout::Installed& entry : state.layouts) {
        std::wstring label = entry.name;
        if (entry.is_ime) {
            label += L" (IME)";
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

void fill_rules(HWND dialog, State& state) {
    HWND list = GetDlgItem(dialog, IDC_RULE_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);

    state.rules = rules::load();

    for (const rules::Rule& rule : state.rules) {
        // A rule can name a layout that has since been uninstalled; say so
        // rather than showing a blank column.
        const bool present = layout::find_by_language(rule.language) != nullptr;

        std::wstring text = rule.executable;
        text += L"\t";
        text += layout::describe(rule.language);
        if (!present) {
            text += L"  (not installed)";
        }

        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
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

void on_add(HWND dialog, State& state) {
    wchar_t executable[MAX_PATH]{};
    GetDlgItemTextW(dialog, IDC_EXECUTABLE, executable, ARRAYSIZE(executable));

    if (executable[0] == L'\0') {
        set_hint(dialog, L"Type an executable name such as notepad.exe, or press Use last app.");
        return;
    }

    const LANGID language = selected_language(dialog);
    if (language == 0) {
        set_hint(dialog, L"No keyboard layout is selected.");
        return;
    }

    if (!rules::set(executable, language)) {
        set_hint(dialog, L"Could not write the rule to the registry.");
        return;
    }

    fill_rules(dialog, state);

    wchar_t message[256]{};
    StringCchPrintfW(message, ARRAYSIZE(message), L"%s is now bound to %s.",
                     executable, layout::describe(language).c_str());
    set_hint(dialog, message);
}

void on_remove(HWND dialog, State& state) {
    HWND list = GetDlgItem(dialog, IDC_RULE_LIST);
    const int index = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR || static_cast<size_t>(index) >= state.rules.size()) {
        set_hint(dialog, L"Select a rule to remove.");
        return;
    }

    const std::wstring executable = state.rules[static_cast<size_t>(index)].executable;
    if (!rules::clear(executable)) {
        set_hint(dialog, L"Could not remove the rule from the registry.");
        return;
    }

    fill_rules(dialog, state);

    wchar_t message[256]{};
    StringCchPrintfW(message, ARRAYSIZE(message), L"Removed the rule for %s.", executable.c_str());
    set_hint(dialog, message);
}

void on_use_last(HWND dialog, const State& state) {
    if (state.lastApplication.empty()) {
        set_hint(dialog, L"No other application has been in the foreground yet.");
        return;
    }
    SetDlgItemTextW(dialog, IDC_EXECUTABLE, state.lastApplication.c_str());
    set_hint(dialog, L"Pick a layout, then press Add / update.");
}

void on_init(HWND dialog, State& state) {
    // One tab stop so the layout column lines up; the units are quarters of the
    // dialog font's average character width.
    const int tabStop = 440;
    SendMessageW(GetDlgItem(dialog, IDC_RULE_LIST), LB_SETTABSTOPS, 1,
                 reinterpret_cast<LPARAM>(&tabStop));

    fill_layouts(dialog, state);
    fill_rules(dialog, state);

    if (!state.lastApplication.empty()) {
        SetDlgItemTextW(dialog, IDC_EXECUTABLE, state.lastApplication.c_str());
    }

    set_hint(dialog,
             L"A rule binds an application to a language. Where one language has "
             L"several IMEs, the first installed one is used.");
}

INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
        State* state = reinterpret_cast<State*>(lParam);
        if (state) {
            on_init(dialog, *state);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        State* state = state_of(dialog);
        if (!state) {
            break;
        }

        switch (LOWORD(wParam)) {
        case IDC_ADD:
            on_add(dialog, *state);
            return TRUE;
        case IDC_REMOVE:
            on_remove(dialog, *state);
            return TRUE;
        case IDC_USE_LAST:
            on_use_last(dialog, *state);
            return TRUE;
        case IDOK:
        case IDCANCEL:
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

void show_rules(HINSTANCE instance, HWND owner, const std::wstring& lastApplication) {
    State state;
    state.lastApplication = lastApplication;

    DialogBoxParamW(
        instance,
        MAKEINTRESOURCEW(IDD_RULES),
        owner,
        dialog_proc,
        reinterpret_cast<LPARAM>(&state));
}

} // namespace config
