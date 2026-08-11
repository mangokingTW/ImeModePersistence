#include "caret.h"

#include <oleauto.h>
#include <uiautomation.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace caret {
namespace {

std::thread g_worker;
std::atomic<bool> g_running{false};
HANDLE g_wake = nullptr;
HWND g_uiWindow = nullptr;
UINT g_resultMessage = 0;

std::mutex g_mutex;
bool g_pending = false;
DWORD g_thread = 0;
std::wstring g_text;

// A caret range is degenerate (empty), and GetBoundingRectangles returns nothing
// for it, so on the first empty result widen the range to the character at the
// caret and ask again -- that yields the glyph cell to sit the badge beside.
bool rect_from_range(IUIAutomationTextRange* range, RECT& out) {
    // Anchor at the insertion point. Some controls -- Chromium's omnibox among
    // them -- hand back a range spanning from the start of the text to the caret
    // rather than a degenerate caret, and its first bounding rectangle is then
    // the far-left start of the text. Collapsing the range onto its end endpoint
    // makes it degenerate at the caret; on a range that is already degenerate
    // this is a no-op.
    range->MoveEndpointByRange(TextPatternRangeEndpoint_Start, range,
                              TextPatternRangeEndpoint_End);
    for (int attempt = 0; attempt < 2; ++attempt) {
        SAFEARRAY* bounds = nullptr;
        if (SUCCEEDED(range->GetBoundingRectangles(&bounds)) && bounds) {
            LONG lower = 0;
            LONG upper = -1;
            SafeArrayGetLBound(bounds, 1, &lower);
            SafeArrayGetUBound(bounds, 1, &upper);
            double* values = nullptr;
            bool ok = false;
            if (SUCCEEDED(SafeArrayAccessData(bounds, reinterpret_cast<void**>(&values)))) {
                // Rectangles come as flat quads: left, top, width, height.
                if (upper - lower + 1 >= 4) {
                    const double left = values[0];
                    const double top = values[1];
                    const double w = values[2];
                    const double h = values[3];
                    if (h > 0) {
                        out.left = static_cast<LONG>(left);
                        out.top = static_cast<LONG>(top);
                        out.right = static_cast<LONG>(left + (w > 0 ? w : 1));
                        out.bottom = static_cast<LONG>(top + h);
                        ok = true;
                    }
                }
                SafeArrayUnaccessData(bounds);
            }
            SafeArrayDestroy(bounds);
            if (ok) {
                return true;
            }
        }
        range->ExpandToEnclosingUnit(TextUnit_Character);
    }
    return false;
}

// Whether the focused element actually accepts typed input, so the badge is not
// shown over a read-only page or a selectable label that merely happens to expose
// a caret. An explicit read-only state is trusted when present; otherwise only an
// edit-like control type counts.
bool is_editable(IUIAutomationElement* element) {
    CONTROLTYPEID controlType = 0;
    element->get_CurrentControlType(&controlType);

    VARIANT readOnly;
    VariantInit(&readOnly);
    if (SUCCEEDED(element->GetCurrentPropertyValue(UIA_ValueIsReadOnlyPropertyId, &readOnly))) {
        const bool hasState = readOnly.vt == VT_BOOL;
        const bool isReadOnly = hasState && readOnly.boolVal != VARIANT_FALSE;
        VariantClear(&readOnly);
        if (hasState) {
            return !isReadOnly; // a document or field that reports its state
        }
    }

    // No read-only information: trust an edit-like control type, and treat
    // everything else (documents, panes, the desktop) as non-input.
    return controlType == UIA_EditControlTypeId ||
           controlType == UIA_ComboBoxControlTypeId;
}

// Returns 0 if no caret, 2 if resolved from the caret range, 3 if from the
// selection fallback -- the distinction is logged to tell the two apart.
int uia_caret(IUIAutomation* automation, RECT& out) {
    if (!automation) {
        return 0;
    }
    IUIAutomationElement* element = nullptr;
    if (FAILED(automation->GetFocusedElement(&element)) || !element) {
        return 0;
    }
    if (!is_editable(element)) {
        element->Release();
        return 0;
    }
    int source = 0;

    IUnknown* unknown = nullptr;
    if (SUCCEEDED(element->GetCurrentPattern(UIA_TextPattern2Id, &unknown)) && unknown) {
        IUIAutomationTextPattern2* pattern = nullptr;
        if (SUCCEEDED(unknown->QueryInterface(__uuidof(IUIAutomationTextPattern2),
                                              reinterpret_cast<void**>(&pattern))) &&
            pattern) {
            BOOL active = FALSE;
            IUIAutomationTextRange* range = nullptr;
            if (SUCCEEDED(pattern->GetCaretRange(&active, &range)) && range) {
                if (rect_from_range(range, out)) {
                    source = 2;
                }
                range->Release();
            }
            pattern->Release();
        }
        unknown->Release();
    }

    if (source == 0) {
        // Where TextPattern2 is unavailable, the selection's start is the caret
        // when nothing is selected.
        IUnknown* textUnknown = nullptr;
        if (SUCCEEDED(element->GetCurrentPattern(UIA_TextPatternId, &textUnknown)) &&
            textUnknown) {
            IUIAutomationTextPattern* pattern = nullptr;
            if (SUCCEEDED(textUnknown->QueryInterface(__uuidof(IUIAutomationTextPattern),
                                                      reinterpret_cast<void**>(&pattern))) &&
                pattern) {
                IUIAutomationTextRangeArray* selection = nullptr;
                if (SUCCEEDED(pattern->GetSelection(&selection)) && selection) {
                    int length = 0;
                    selection->get_Length(&length);
                    if (length > 0) {
                        IUIAutomationTextRange* range = nullptr;
                        if (SUCCEEDED(selection->GetElement(0, &range)) && range) {
                            if (rect_from_range(range, out)) {
                                source = 3;
                            }
                            range->Release();
                        }
                    }
                    selection->Release();
                }
                pattern->Release();
            }
            textUnknown->Release();
        }
    }

    element->Release();
    return source;
}

// Tier 1: the classic OS caret, read without attaching to the target thread.
bool classic_caret(DWORD thread, RECT& out) {
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(thread, &info) || !info.hwndCaret) {
        return false;
    }
    if (info.rcCaret.bottom <= info.rcCaret.top) {
        return false; // no height means no caret
    }
    RECT rc = info.rcCaret;
    MapWindowPoints(info.hwndCaret, nullptr, reinterpret_cast<POINT*>(&rc), 2);
    out = rc;
    return true;
}

// Which path resolved the caret: 0 none, 1 classic caret, 2 UIA caret range,
// 3 UIA selection fallback.
int resolve(IUIAutomation* automation, DWORD thread, RECT& out) {
    if (classic_caret(thread, out)) {
        return 1;
    }
    return uia_caret(automation, out);
}

void worker_main() {
    const HRESULT initialised = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* automation = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IUIAutomation),
                                reinterpret_cast<void**>(&automation)))) {
        // Tier 2 is unavailable; classic_caret (Tier 1) still works, and every
        // UIA call is guarded by a null check on automation.
        automation = nullptr;
    }

    while (g_running.load()) {
        WaitForSingleObject(g_wake, INFINITE);
        if (!g_running.load()) {
            break;
        }

        DWORD thread = 0;
        std::wstring text;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_pending) {
                continue;
            }
            g_pending = false;
            thread = g_thread;
            text = g_text;
        }

        RECT rect{};
        const int tier = resolve(automation, thread, rect);

        Result* result = new Result{rect, std::move(text), tier != 0, tier};
        if (!PostMessageW(g_uiWindow, g_resultMessage, 0,
                          reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }

    if (automation) {
        automation->Release();
    }
    if (SUCCEEDED(initialised)) {
        CoUninitialize();
    }
}

} // namespace

bool start(HWND uiWindow, UINT resultMessage) {
    if (g_running.load()) {
        return true;
    }
    g_uiWindow = uiWindow;
    g_resultMessage = resultMessage;
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
    if (!g_wake) {
        return false;
    }
    g_running.store(true);
    g_worker = std::thread(worker_main);
    return true;
}

void stop() {
    if (!g_running.load()) {
        return;
    }
    g_running.store(false);
    if (g_wake) {
        SetEvent(g_wake);
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = nullptr;
    }
}

void request(DWORD thread, const std::wstring& text) {
    if (!g_running.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_thread = thread;
        g_text = text;
        g_pending = true;
    }
    SetEvent(g_wake);
}

} // namespace caret
