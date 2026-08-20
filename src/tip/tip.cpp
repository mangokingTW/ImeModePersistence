// The in-process half of the mode-persistence engine: a Text Services Framework
// keyboard text service (a "TIP") that Windows loads inside another application's
// process. From there it can read and write that thread's conversion mode through
// the per-thread TSF compartment -- the one thing an out-of-process utility
// cannot do, and the reason Chromium and the packaged Notepad never reported or
// accepted a mode change through the IMM32/TSF interop the exe uses from outside.
//
// It is a plain COM in-process server, not linked against ime_core: it must be as
// small and as self-contained as possible, because it is mapped into every
// application that loads a keyboard service. It talks to the tray coordinator over
// a named pipe (see tip_protocol.h), reporting the mode it observes and applying
// the mode the tray asks for.
//
// FAIL-SAFE IS THE CONTRACT. This code runs inside other people's processes, so
// every entry point swallows its own failures and degrades to doing nothing: a
// missing pipe, a refused compartment, a COM error -- none of them may throw,
// hang, or disturb the host. The worst outcome allowed is "the mode is not
// synced for this app", which is exactly where the app stood before this existed.

#include <windows.h>
#include <msctf.h>
#include <olectl.h>

#include <cstdint>
#include <cstring>
#include <new>

#include "tip_protocol.h"

// After the SDK headers (which only declare the TSF GUIDs -- those resolve from
// the default uuid.lib), initguid.h switches DEFINE_GUID to *emit* definitions,
// so the two GUIDs below are instantiated in this translation unit and nowhere
// else. Order matters: including it before msctf.h would emit duplicate
// definitions of the framework's own GUIDs and collide at link time.
#include <initguid.h>

// This text service's coclass, and the language-profile GUID it registers under.
// Fixed for the life of the product: they are written into the registry at
// registration and matched at unregistration, so regenerating them would orphan
// an installed copy's entries. Generated once for this feature.
// {7B4A9E62-1F3C-4D8A-9C21-6E5F0A2B7C41}
DEFINE_GUID(CLSID_ImeModePersistenceTip,
            0x7b4a9e62, 0x1f3c, 0x4d8a, 0x9c, 0x21, 0x6e, 0x5f, 0x0a, 0x2b, 0x7c, 0x41);
// {D9E3F1A8-2B6C-4E90-8A17-3C4D5E6F7A82}
DEFINE_GUID(GUID_ImeModePersistenceProfile,
            0xd9e3f1a8, 0x2b6c, 0x4e90, 0x8a, 0x17, 0x3c, 0x4d, 0x5e, 0x6f, 0x7a, 0x82);

namespace {

// Module-wide state. A COM in-process server may be asked to unload the moment
// its last object and lock are gone; g_refs guards that, and g_instance is the
// mapped module base used both for the message-only window class and as the
// registered InprocServer32 path.
long g_refs = 0;
HINSTANCE g_instance = nullptr;

const wchar_t kProfileDescription[] = L"IME Mode Persistence (sync helper)";
const wchar_t kWindowClassName[] = L"ImeModePersistenceTipMarshal";

// The private window message that carries a tray SetMode request from the pipe
// reader thread onto the text service's own (UI) thread, where the compartment
// may legally be written. WPARAM is the tip_ipc::WireMode.
constexpr UINT WM_TIP_SET_MODE = WM_USER + 0x41;

// The languages the service registers a profile under. A TIP is loaded for the
// languages it declares; these are the IME languages the utility cares about, so
// the service has a chance of being present wherever one of them is in use. The
// English entry keeps it registered on a machine whose only "language" while the
// helper is selected is a plain Latin layout.
const LANGID kProfileLanguages[] = {
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL),  // zh-TW
    MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED),   // zh-CN
    MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT),             // ja
    MAKELANGID(LANG_KOREAN, SUBLANG_DEFAULT),               // ko
    MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),           // en-US
};

// ------------------------------------------------------------------ mode helpers

tip_ipc::WireMode classify(bool open, DWORD conversion) {
    if (!open) {
        return tip_ipc::WireMode::Alphanumeric;
    }
    // Mirrors ime::classify in the exe: an open IME is still alphanumeric unless
    // the native bit is set (Bopomofo clears it on Shift while staying open).
    return (conversion & TF_CONVERSIONMODE_NATIVE) ? tip_ipc::WireMode::Native
                                                   : tip_ipc::WireMode::Alphanumeric;
}

// ------------------------------------------------------------------ pipe client
//
// One connection per text-service activation. Sends are done from the service's
// thread (short, fixed-size, to a pipe the tray drains immediately); receives run
// on a dedicated reader thread that posts each SetMode onto the marshal window.

class PipeClient {
public:
    // hwnd is the marshal window that receives WM_TIP_SET_MODE. Best-effort: a
    // failure leaves the client disconnected and every later call a no-op.
    void connect(HWND hwnd) {
        marshal_ = hwnd;
        // A message-mode duplex client, so each 16-byte message is read whole.
        for (int attempt = 0; attempt < 2; ++attempt) {
            pipe_ = CreateFileW(tip_ipc::kPipeName, GENERIC_READ | GENERIC_WRITE, 0,
                                nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) {
                break;
            }
            if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(tip_ipc::kPipeName, 200)) {
                pipe_ = INVALID_HANDLE_VALUE;
                return;  // the tray is not listening; stay silent
            }
        }
        if (pipe_ == INVALID_HANDLE_VALUE) {
            return;
        }
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);

        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        reader_ = CreateThread(nullptr, 0, &PipeClient::reader_thunk, this, 0, nullptr);
    }

    bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    void send(tip_ipc::MsgType type, tip_ipc::WireMode mode) {
        if (pipe_ == INVALID_HANDLE_VALUE) {
            return;
        }
        tip_ipc::Message m{};
        m.type = static_cast<uint32_t>(type);
        m.pid = GetCurrentProcessId();
        m.tid = GetCurrentThreadId();
        m.mode = static_cast<int32_t>(mode);
        DWORD written = 0;
        if (!WriteFile(pipe_, &m, sizeof(m), &written, nullptr) || written != sizeof(m)) {
            // The tray went away; drop the connection so we stop trying.
            disconnect();
        }
    }

    void disconnect() {
        if (stop_) {
            SetEvent(stop_);
        }
        // Closing the handle unblocks a pending ReadFile in the reader thread.
        if (pipe_ != INVALID_HANDLE_VALUE) {
            HANDLE h = pipe_;
            pipe_ = INVALID_HANDLE_VALUE;
            CloseHandle(h);
        }
        if (reader_) {
            WaitForSingleObject(reader_, 1000);
            CloseHandle(reader_);
            reader_ = nullptr;
        }
        if (stop_) {
            CloseHandle(stop_);
            stop_ = nullptr;
        }
    }

private:
    static DWORD WINAPI reader_thunk(LPVOID self) {
        static_cast<PipeClient*>(self)->reader_loop();
        return 0;
    }

    void reader_loop() {
        for (;;) {
            tip_ipc::Message m{};
            DWORD read = 0;
            const HANDLE pipe = pipe_;
            if (pipe == INVALID_HANDLE_VALUE) {
                return;
            }
            if (!ReadFile(pipe, &m, sizeof(m), &read, nullptr) || read != sizeof(m)) {
                return;  // disconnected or shutting down
            }
            if (m.type == static_cast<uint32_t>(tip_ipc::MsgType::SetMode) && marshal_) {
                // Never touch the compartment from this thread; hand it to the
                // service's own thread, which owns the thread manager.
                PostMessageW(marshal_, WM_TIP_SET_MODE, static_cast<WPARAM>(m.mode), 0);
            }
        }
    }

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE reader_ = nullptr;
    HANDLE stop_ = nullptr;
    HWND marshal_ = nullptr;
};

// ------------------------------------------------------------------ the service

class Tip : public ITfTextInputProcessorEx, public ITfCompartmentEventSink {
public:
    Tip() : ref_(1) { InterlockedIncrement(&g_refs); }
    ~Tip() { InterlockedDecrement(&g_refs); }

    // IUnknown ----------------------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ITfTextInputProcessor) ||
            IsEqualIID(riid, IID_ITfTextInputProcessorEx)) {
            *ppv = static_cast<ITfTextInputProcessorEx*>(this);
        } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
            *ppv = static_cast<ITfCompartmentEventSink*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        const long c = InterlockedDecrement(&ref_);
        if (c == 0) {
            delete this;
        }
        return c;
    }

    // ITfTextInputProcessor / ...Ex ------------------------------------------
    STDMETHODIMP Activate(ITfThreadMgr* mgr, TfClientId id) override {
        return ActivateEx(mgr, id, 0);
    }

    STDMETHODIMP ActivateEx(ITfThreadMgr* mgr, TfClientId id, DWORD /*flags*/) override {
        // Anything that throws here would surface inside the host process, so the
        // whole method is best-effort and a partial setup simply reports nothing.
        thread_mgr_ = mgr;
        if (thread_mgr_) {
            thread_mgr_->AddRef();
        }
        client_id_ = id;

        create_marshal_window();
        open_compartments();
        advise();

        pipe_.connect(marshal_);
        if (pipe_.connected()) {
            pipe_.send(tip_ipc::MsgType::Hello, read_mode());
        }
        return S_OK;
    }

    STDMETHODIMP Deactivate() override {
        if (pipe_.connected()) {
            pipe_.send(tip_ipc::MsgType::Bye, tip_ipc::WireMode::Unknown);
        }
        pipe_.disconnect();
        unadvise();
        release_compartments();
        destroy_marshal_window();
        if (thread_mgr_) {
            thread_mgr_->Release();
            thread_mgr_ = nullptr;
        }
        return S_OK;
    }

    // ITfCompartmentEventSink -------------------------------------------------
    STDMETHODIMP OnChange(REFGUID /*rguid*/) override {
        // Either compartment changing (open/close or conversion bits) can move the
        // effective mode, so both are re-read and one report is sent.
        if (pipe_.connected()) {
            pipe_.send(tip_ipc::MsgType::ModeReport, read_mode());
        }
        return S_OK;
    }

private:
    // -- compartments ---------------------------------------------------------

    void open_compartments() {
        if (!thread_mgr_) {
            return;
        }
        ITfCompartmentMgr* cmgr = nullptr;
        if (FAILED(thread_mgr_->QueryInterface(IID_ITfCompartmentMgr,
                                               reinterpret_cast<void**>(&cmgr))) ||
            !cmgr) {
            return;
        }
        cmgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &conversion_);
        cmgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &openclose_);
        cmgr->Release();
    }

    void release_compartments() {
        if (conversion_) {
            conversion_->Release();
            conversion_ = nullptr;
        }
        if (openclose_) {
            openclose_->Release();
            openclose_ = nullptr;
        }
    }

    static bool read_i4(ITfCompartment* c, LONG* out) {
        if (!c) {
            return false;
        }
        VARIANT v;
        VariantInit(&v);
        const bool ok = SUCCEEDED(c->GetValue(&v)) && v.vt == VT_I4;
        if (ok) {
            *out = v.lVal;
        }
        VariantClear(&v);
        return ok;
    }

    tip_ipc::WireMode read_mode() {
        LONG open = 0;
        LONG conversion = 0;
        const bool haveOpen = read_i4(openclose_, &open);
        const bool haveConv = read_i4(conversion_, &conversion);
        if (!haveOpen && !haveConv) {
            return tip_ipc::WireMode::Unknown;
        }
        return classify(open != 0, static_cast<DWORD>(conversion));
    }

    void write_mode(tip_ipc::WireMode mode) {
        if (mode == tip_ipc::WireMode::Unknown || !conversion_) {
            return;
        }
        LONG conversion = 0;
        read_i4(conversion_, &conversion);
        DWORD bits = static_cast<DWORD>(conversion);
        if (mode == tip_ipc::WireMode::Native) {
            bits |= TF_CONVERSIONMODE_NATIVE;
            LONG open = 0;
            if (openclose_ && read_i4(openclose_, &open) && open == 0) {
                set_i4(openclose_, 1);  // a closed IME ignores the native bit
            }
        } else {
            bits &= ~static_cast<DWORD>(TF_CONVERSIONMODE_NATIVE);
        }
        set_i4(conversion_, static_cast<LONG>(bits));
    }

    void set_i4(ITfCompartment* c, LONG value) {
        if (!c) {
            return;
        }
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = value;
        c->SetValue(client_id_, &v);
        VariantClear(&v);
    }

    // -- event sink advises ---------------------------------------------------

    void advise() {
        conv_cookie_ = advise_one(conversion_, &conv_source_);
        open_cookie_ = advise_one(openclose_, &open_source_);
    }

    DWORD advise_one(ITfCompartment* c, ITfSource** sourceOut) {
        if (!c) {
            return TF_INVALID_COOKIE;
        }
        ITfSource* source = nullptr;
        if (FAILED(c->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(&source))) ||
            !source) {
            return TF_INVALID_COOKIE;
        }
        DWORD cookie = TF_INVALID_COOKIE;
        if (FAILED(source->AdviseSink(IID_ITfCompartmentEventSink,
                                      static_cast<ITfCompartmentEventSink*>(this), &cookie))) {
            source->Release();
            return TF_INVALID_COOKIE;
        }
        *sourceOut = source;  // kept so the sink can be removed in Deactivate
        return cookie;
    }

    void unadvise() {
        if (conv_source_) {
            if (conv_cookie_ != TF_INVALID_COOKIE) {
                conv_source_->UnadviseSink(conv_cookie_);
            }
            conv_source_->Release();
            conv_source_ = nullptr;
        }
        if (open_source_) {
            if (open_cookie_ != TF_INVALID_COOKIE) {
                open_source_->UnadviseSink(open_cookie_);
            }
            open_source_->Release();
            open_source_ = nullptr;
        }
        conv_cookie_ = open_cookie_ = TF_INVALID_COOKIE;
    }

    // -- marshal window -------------------------------------------------------

    void create_marshal_window() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &Tip::wnd_proc;
        wc.hInstance = g_instance;
        wc.lpszClassName = kWindowClassName;
        RegisterClassW(&wc);  // harmless if already registered by another activation
        marshal_ = CreateWindowExW(0, kWindowClassName, L"", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, g_instance, nullptr);
        if (marshal_) {
            SetWindowLongPtrW(marshal_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        }
    }

    void destroy_marshal_window() {
        if (marshal_) {
            DestroyWindow(marshal_);
            marshal_ = nullptr;
        }
    }

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_TIP_SET_MODE) {
            auto* self = reinterpret_cast<Tip*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self) {
                self->write_mode(static_cast<tip_ipc::WireMode>(static_cast<int32_t>(wParam)));
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    long ref_;
    ITfThreadMgr* thread_mgr_ = nullptr;
    TfClientId client_id_ = 0;
    ITfCompartment* conversion_ = nullptr;
    ITfCompartment* openclose_ = nullptr;
    ITfSource* conv_source_ = nullptr;
    ITfSource* open_source_ = nullptr;
    DWORD conv_cookie_ = TF_INVALID_COOKIE;
    DWORD open_cookie_ = TF_INVALID_COOKIE;
    HWND marshal_ = nullptr;
    PipeClient pipe_;
};

// ------------------------------------------------------------------ class factory

class Factory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }   // a static singleton
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (ppv) {
            *ppv = nullptr;
        }
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        Tip* tip = new (std::nothrow) Tip();
        if (!tip) {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = tip->QueryInterface(riid, ppv);
        tip->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) {
            InterlockedIncrement(&g_refs);
        } else {
            InterlockedDecrement(&g_refs);
        }
        return S_OK;
    }
};

Factory g_factory;

// ------------------------------------------------------------------ registration
//
// Two registries have to agree: COM (so CoCreateInstance can build the object)
// and TSF (so the framework knows the object is a keyboard text service worth
// loading). Both are machine-wide here, so registration needs an elevated caller;
// the exe checks that before invoking DllRegisterServer.

bool guid_to_string(REFGUID guid, wchar_t* out, int count) {
    return StringFromGUID2(guid, out, count) > 0;
}

HRESULT register_com() {
    wchar_t clsid[64];
    if (!guid_to_string(CLSID_ImeModePersistenceTip, clsid, 64)) {
        return E_FAIL;
    }
    wchar_t module[MAX_PATH];
    if (GetModuleFileNameW(g_instance, module, MAX_PATH) == 0) {
        return E_FAIL;
    }

    wchar_t key[128];
    wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid);
    HKEY hkey = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0, KEY_WRITE, nullptr, &hkey,
                        nullptr) != ERROR_SUCCESS) {
        return SELFREG_E_CLASS;
    }
    RegSetValueExW(hkey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(module),
                   static_cast<DWORD>((lstrlenW(module) + 1) * sizeof(wchar_t)));
    const wchar_t model[] = L"Apartment";
    RegSetValueExW(hkey, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(model),
                   sizeof(model));
    RegCloseKey(hkey);
    return S_OK;
}

void unregister_com() {
    wchar_t clsid[64];
    if (!guid_to_string(CLSID_ImeModePersistenceTip, clsid, 64)) {
        return;
    }
    wchar_t key[128];
    wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    wsprintfW(key, L"CLSID\\%s", clsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
}

HRESULT register_tsf() {
    ITfInputProcessorProfiles* profiles = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfiles,
                                reinterpret_cast<void**>(&profiles))) ||
        !profiles) {
        return E_FAIL;
    }
    HRESULT hr = profiles->Register(CLSID_ImeModePersistenceTip);
    if (SUCCEEDED(hr)) {
        for (LANGID lang : kProfileLanguages) {
            profiles->AddLanguageProfile(
                CLSID_ImeModePersistenceTip, lang, GUID_ImeModePersistenceProfile,
                kProfileDescription,
                static_cast<ULONG>(lstrlenW(kProfileDescription)), nullptr, 0, 0);
        }
    }
    profiles->Release();

    ITfCategoryMgr* categories = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr,
                                   reinterpret_cast<void**>(&categories))) &&
        categories) {
        categories->RegisterCategory(CLSID_ImeModePersistenceTip, GUID_TFCAT_TIP_KEYBOARD,
                                     CLSID_ImeModePersistenceTip);
        categories->Release();
    }
    return hr;
}

void unregister_tsf() {
    ITfCategoryMgr* categories = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfCategoryMgr,
                                   reinterpret_cast<void**>(&categories))) &&
        categories) {
        categories->UnregisterCategory(CLSID_ImeModePersistenceTip, GUID_TFCAT_TIP_KEYBOARD,
                                       CLSID_ImeModePersistenceTip);
        categories->Release();
    }
    ITfInputProcessorProfiles* profiles = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfiles,
                                   reinterpret_cast<void**>(&profiles))) &&
        profiles) {
        profiles->Unregister(CLSID_ImeModePersistenceTip);
        profiles->Release();
    }
}

} // namespace

// ------------------------------------------------------------------ DLL exports

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

// _Use_decl_annotations_ adopts the SAL contract combaseapi.h already declares
// for these exports, so the analyzer stops flagging the definition as an
// inconsistently annotated instance of the header's prototype.
_Use_decl_annotations_
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (IsEqualCLSID(rclsid, CLSID_ImeModePersistenceTip)) {
        return g_factory.QueryInterface(riid, ppv);
    }
    if (ppv) {
        *ppv = nullptr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

_Use_decl_annotations_
STDAPI DllCanUnloadNow() {
    return g_refs == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    HRESULT hr = register_com();
    if (FAILED(hr)) {
        return hr;
    }
    // TSF registration wants COM up on this thread; the caller (regsvr32 or the
    // exe) has not necessarily initialised it.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    hr = register_tsf();
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    if (FAILED(hr)) {
        unregister_com();
    }
    return hr;
}

STDAPI DllUnregisterServer() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    unregister_tsf();
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    unregister_com();
    return S_OK;
}
