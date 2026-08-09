#include "tsf.h"

#include <msctf.h>
#include <objbase.h>

namespace tsf {
namespace {

ITfInputProcessorProfileMgr* g_manager = nullptr;
bool g_comInitialised = false;

} // namespace

bool initialise() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (com == RPC_E_CHANGED_MODE) {
        // Someone already initialised this thread with a different model. COM is
        // usable -- there is just nothing of ours to uninitialise. This case has
        // to be tested before FAILED(): RPC_E_CHANGED_MODE is a failure HRESULT,
        // and treating it as fatal would silently disable the TSF mechanism for
        // the whole session whenever a loaded DLL touched COM first.
        g_comInitialised = false;
    } else if (FAILED(com)) {
        return false;
    } else {
        g_comInitialised = true;
    }

    ITfInputProcessorProfiles* profiles = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)))) {
        return false;
    }

    // ITfInputProcessorProfileMgr is the interface carrying ActivateProfile; it is
    // reached through the older object rather than created directly.
    const HRESULT hr = profiles->QueryInterface(IID_PPV_ARGS(&g_manager));
    profiles->Release();

    return SUCCEEDED(hr);
}

void shutdown() {
    if (g_manager) {
        g_manager->Release();
        g_manager = nullptr;
    }
    if (g_comInitialised) {
        CoUninitialize();
        g_comInitialised = false;
    }
}

bool activate_language(LANGID language) {
    if (!g_manager || language == 0) {
        return false;
    }

    IEnumTfInputProcessorProfiles* enumerator = nullptr;
    if (FAILED(g_manager->EnumProfiles(language, &enumerator)) || !enumerator) {
        return false;
    }

    bool activated = false;
    TF_INPUTPROCESSORPROFILE profile{};
    ULONG fetched = 0;

    while (!activated && enumerator->Next(1, &profile, &fetched) == S_OK && fetched == 1) {
        if (profile.langid != language || (profile.dwFlags & TF_IPP_FLAG_ENABLED) == 0) {
            continue;
        }

        activated = SUCCEEDED(g_manager->ActivateProfile(
            profile.dwProfileType,
            profile.langid,
            profile.clsid,
            profile.guidProfile,
            profile.hkl,
            TF_IPPMF_FORSESSION));
    }

    enumerator->Release();
    return activated;
}

} // namespace tsf
