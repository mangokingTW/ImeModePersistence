#include "layout.h"

#include <imm.h>
#include <strsafe.h>

#include <algorithm>

#include "tsf.h"

namespace layout {

std::vector<Installed> installed() {
    std::vector<Installed> result;

    const int count = GetKeyboardLayoutList(0, nullptr);
    if (count <= 0) {
        return result;
    }

    std::vector<HKL> handles(static_cast<size_t>(count));
    const int written = GetKeyboardLayoutList(count, handles.data());
    if (written <= 0) {
        return result;
    }
    handles.resize(static_cast<size_t>(written));

    for (const HKL hkl : handles) {
        const LANGID language = language_of(hkl);

        const bool seen = std::find_if(
                              result.begin(), result.end(),
                              [language](const Installed& i) { return i.language == language; }) !=
                          result.end();
        if (seen) {
            continue;
        }

        Installed entry;
        entry.hkl = hkl;
        entry.language = language;
        entry.is_ime = ImmIsIME(hkl) != FALSE;
        entry.name = describe(language);
        result.push_back(entry);
    }

    return result;
}

HKL find_by_language(LANGID language) {
    const int count = GetKeyboardLayoutList(0, nullptr);
    if (count <= 0) {
        return nullptr;
    }

    std::vector<HKL> handles(static_cast<size_t>(count));
    const int written = GetKeyboardLayoutList(count, handles.data());
    for (int i = 0; i < written; ++i) {
        if (language_of(handles[static_cast<size_t>(i)]) == language) {
            return handles[static_cast<size_t>(i)];
        }
    }
    return nullptr;
}

LANGID language_of(HKL hkl) {
    // The low word of an HKL is the language identifier; the high word is the
    // device handle, which is what makes the whole HKL unsuitable for storage.
    return static_cast<LANGID>(reinterpret_cast<UINT_PTR>(hkl) & 0xFFFF);
}

std::wstring describe(LANGID language) {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (LCIDToLocaleName(MAKELCID(language, SORT_DEFAULT), locale, ARRAYSIZE(locale), 0) != 0) {
        wchar_t name[128]{};
        if (GetLocaleInfoEx(locale, LOCALE_SLOCALIZEDDISPLAYNAME, name, ARRAYSIZE(name)) != 0) {
            return name;
        }
    }

    wchar_t fallback[16]{};
    StringCchPrintfW(fallback, ARRAYSIZE(fallback), L"0x%04X", language);
    return fallback;
}

HKL current(HWND hwnd) {
    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    return thread ? GetKeyboardLayout(thread) : nullptr;
}

namespace {

// wParam is deliberately 0. INPUTLANGCHANGE_SYSCHARSET, which this used to pass,
// asks the window to switch only if the new layout matches the system character
// set -- a condition that has nothing to do with an explicit request and that
// some windows honour by refusing.
bool post_request(HWND window, HKL hkl) {
    return window && PostMessageW(window, WM_INPUTLANGCHANGEREQUEST, 0,
                                  reinterpret_cast<LPARAM>(hkl)) != FALSE;
}

struct Broadcast {
    HKL hkl;
    bool posted;
};

BOOL CALLBACK post_to_window(HWND window, LPARAM parameter) {
    Broadcast* state = reinterpret_cast<Broadcast*>(parameter);
    if (post_request(window, state->hkl)) {
        state->posted = true;
    }
    return TRUE;
}

// The window holding keyboard focus, which is what actually owns the input
// language. The top-level window often forwards nothing.
HWND focus_window(DWORD thread) {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(thread, &info)) {
        return nullptr;
    }
    return info.hwndFocus;
}

} // namespace

const wchar_t* method_name(Method method) {
    switch (method) {
    case Method::FocusWindow: return L"focus window";
    case Method::ThreadWindows: return L"thread windows";
    case Method::TsfSession: return L"TSF session";
    }
    return L"unknown";
}

bool request(HWND hwnd, HKL hkl, Method method) {
    if (!hwnd || !hkl || !IsWindow(hwnd)) {
        return false;
    }

    const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (!thread) {
        return false;
    }

    switch (method) {
    case Method::FocusWindow: {
        HWND focus = focus_window(thread);
        return post_request(focus ? focus : hwnd, hkl);
    }

    case Method::ThreadWindows: {
        // Some applications keep a separate message-handling window that honours
        // the request even when the visible one ignores it.
        Broadcast state{hkl, false};
        EnumThreadWindows(thread, post_to_window, reinterpret_cast<LPARAM>(&state));
        return state.posted;
    }

    case Method::TsfSession:
        // Nothing about the target is read, opened or attached to: the framework
        // is asked to move the session's input language and the target follows.
        // This replaced AttachThreadInput, which was never shown to work and was
        // the one technique here that anti-cheat is built to notice.
        return tsf::activate_language(language_of(hkl));
    }

    return false;
}

} // namespace layout
