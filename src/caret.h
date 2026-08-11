#pragma once

#include <windows.h>

#include <string>

// Resolves the screen position of the text caret in the foreground application,
// on a background thread, and hands the result back to the UI thread. Two tiers:
// GetGUIThreadInfo for classic Win32 carets, then UI Automation's text pattern
// for applications (Chromium, modern editors) that draw their own caret and
// expose no OS caret.
//
// UI Automation calls can block, so they must not run on the UI thread; that is
// the whole reason this is a worker rather than a synchronous call.
namespace caret {

// Posted to the UI thread as the result message; lParam is a heap-allocated
// Result* the handler takes ownership of and must delete.
struct Result {
    RECT rect;          // caret rectangle in screen pixels, valid when found
    std::wstring text;  // badge text carried through from the request
    bool found;
    int tier;           // which path resolved it: 0 none, 1 classic caret, 2 UIA
};

// Starts the worker thread. Results are posted to uiWindow as resultMessage.
bool start(HWND uiWindow, UINT resultMessage);

// Signals the worker to stop and joins it.
void stop();

// Requests the caret rectangle for the given GUI thread, to be shown with the
// given badge text. Coalesced: only the most recent request is served.
void request(DWORD thread, const std::wstring& text);

} // namespace caret
