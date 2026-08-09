#include "layout.h"

#include <imm.h>
#include <strsafe.h>

#include <algorithm>

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

bool request(HWND hwnd, HKL hkl) {
    if (!hwnd || !hkl || !IsWindow(hwnd)) {
        return false;
    }
    return PostMessageW(
               hwnd,
               WM_INPUTLANGCHANGEREQUEST,
               INPUTLANGCHANGE_SYSCHARSET,
               reinterpret_cast<LPARAM>(hkl)) != FALSE;
}

} // namespace layout
