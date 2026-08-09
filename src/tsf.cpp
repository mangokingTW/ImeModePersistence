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
    if (FAILED(com)) {
        return false;
    }
    // RPC_E_CHANGED_MODE means someone already initialised this thread with a
    // different model; there is then nothing of ours to uninitialise.
    g_comInitialised = com != RPC_E_CHANGED_MODE;

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
