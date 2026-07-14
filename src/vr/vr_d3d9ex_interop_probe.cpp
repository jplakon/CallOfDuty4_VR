#include "vr/vr_d3d9ex_interop_probe.h"

#include "qcommon/qcommon.h"

#include <Windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

namespace
{
constexpr UINT kProbeWidth = 64u;
constexpr UINT kProbeHeight = 64u;

constexpr std::uint8_t kExpectedBlue = 0xE0u;
constexpr std::uint8_t kExpectedGreen = 0x80u;
constexpr std::uint8_t kExpectedRed = 0x20u;
constexpr std::uint8_t kExpectedAlpha = 0xFFu;

using Direct3DCreate9ExFn =
    HRESULT (WINAPI*)(
        UINT sdkVersion,
        IDirect3D9Ex** direct3D9Ex);

template <typename T>
void VR_ProbeRelease(T*& object)
{
    if (object != nullptr)
    {
        object->Release();
        object = nullptr;
    }
}

bool VR_ProbeLuidMatches(
    const LUID& left,
    const LUID& right)
{
    return
        left.LowPart == right.LowPart &&
        left.HighPart == right.HighPart;
}

void VR_ProbeLogHr(
    const char* operation,
    const HRESULT hr)
{
    Com_PrintWarning(
        0,
        "[VR] D3D9Ex interop probe: %s failed: "
        "0x%08lX.\n",
        operation,
        static_cast<unsigned long>(hr));
}

bool VR_ProbeWaitForD3D9(
    IDirect3DDevice9Ex* device)
{
    IDirect3DQuery9* eventQuery = nullptr;

    HRESULT hr =
        device->CreateQuery(
            D3DQUERYTYPE_EVENT,
            &eventQuery);

    if (FAILED(hr) ||
        eventQuery == nullptr)
    {
        VR_ProbeLogHr(
            "CreateQuery(D3DQUERYTYPE_EVENT)",
            hr);

        return false;
    }

    hr = eventQuery->Issue(D3DISSUE_END);

    if (FAILED(hr))
    {
        VR_ProbeLogHr(
            "IDirect3DQuery9::Issue",
            hr);

        VR_ProbeRelease(eventQuery);
        return false;
    }

    constexpr unsigned int kMaximumWaitIterations =
        4000u;

    bool completed = false;

    for (unsigned int iteration = 0u;
         iteration < kMaximumWaitIterations;
         ++iteration)
    {
        hr =
            eventQuery->GetData(
                nullptr,
                0u,
                D3DGETDATA_FLUSH);

        if (hr == S_OK)
        {
            completed = true;
            break;
        }

        if (hr != S_FALSE)
        {
            VR_ProbeLogHr(
                "IDirect3DQuery9::GetData",
                hr);

            break;
        }

        Sleep(1u);
    }

    VR_ProbeRelease(eventQuery);

    if (!completed)
    {
        Com_PrintWarning(
            0,
            "[VR] D3D9Ex interop probe timed out "
            "waiting for the D3D9Ex producer.\n");
    }

    return completed;
}

bool VR_ProbeGameAdapterMatches(
    IDirect3DDevice9* gameDevice,
    IDirect3D9Ex* direct3D9Ex,
    const UINT exAdapterIndex)
{
    if (gameDevice == nullptr ||
        direct3D9Ex == nullptr)
    {
        return false;
    }

    D3DDEVICE_CREATION_PARAMETERS creation = {};

    HRESULT hr =
        gameDevice->GetCreationParameters(
            &creation);

    if (FAILED(hr))
    {
        VR_ProbeLogHr(
            "game IDirect3DDevice9::"
            "GetCreationParameters",
            hr);

        return false;
    }

    IDirect3D9* gameDirect3D = nullptr;

    hr =
        gameDevice->GetDirect3D(
            &gameDirect3D);

    if (FAILED(hr) ||
        gameDirect3D == nullptr)
    {
        VR_ProbeLogHr(
            "game IDirect3DDevice9::GetDirect3D",
            hr);

        return false;
    }

    D3DADAPTER_IDENTIFIER9 gameIdentifier = {};
    D3DADAPTER_IDENTIFIER9 exIdentifier = {};

    const HRESULT gameIdentifierResult =
        gameDirect3D->GetAdapterIdentifier(
            creation.AdapterOrdinal,
            0u,
            &gameIdentifier);

    const HRESULT exIdentifierResult =
        direct3D9Ex->GetAdapterIdentifier(
            exAdapterIndex,
            0u,
            &exIdentifier);

    VR_ProbeRelease(gameDirect3D);

    if (FAILED(gameIdentifierResult) ||
        FAILED(exIdentifierResult))
    {
        VR_ProbeLogHr(
            "GetAdapterIdentifier",
            FAILED(gameIdentifierResult)
                ? gameIdentifierResult
                : exIdentifierResult);

        return false;
    }

    const bool matches =
        gameIdentifier.VendorId ==
            exIdentifier.VendorId &&
        gameIdentifier.DeviceId ==
            exIdentifier.DeviceId &&
        gameIdentifier.SubSysId ==
            exIdentifier.SubSysId &&
        gameIdentifier.Revision ==
            exIdentifier.Revision;

    Com_Printf(
        0,
        "[VR] Game D3D9 adapter: %s "
        "(VEN %04X DEV %04X).\n",
        gameIdentifier.Description,
        gameIdentifier.VendorId,
        gameIdentifier.DeviceId);

    Com_Printf(
        0,
        "[VR] OpenXR-matched D3D9Ex adapter: %s "
        "(VEN %04X DEV %04X).\n",
        exIdentifier.Description,
        exIdentifier.VendorId,
        exIdentifier.DeviceId);

    Com_Printf(
        0,
        "[VR] Game D3D9/OpenXR D3D9Ex adapter "
        "identity match: %s.\n",
        matches ? "yes" : "no");

    return matches;
}

bool VR_ProbeD3D11DeviceLuid(
    ID3D11Device* device,
    const LUID& expectedLuid)
{
    IDXGIDevice* dxgiDevice = nullptr;

    HRESULT hr =
        device->QueryInterface(
            __uuidof(IDXGIDevice),
            reinterpret_cast<void**>(
                &dxgiDevice));

    if (FAILED(hr) ||
        dxgiDevice == nullptr)
    {
        VR_ProbeLogHr(
            "ID3D11Device::QueryInterface("
            "IDXGIDevice)",
            hr);

        return false;
    }

    IDXGIAdapter* adapter = nullptr;

    hr =
        dxgiDevice->GetAdapter(
            &adapter);

    VR_ProbeRelease(dxgiDevice);

    if (FAILED(hr) ||
        adapter == nullptr)
    {
        VR_ProbeLogHr(
            "IDXGIDevice::GetAdapter",
            hr);

        return false;
    }

    DXGI_ADAPTER_DESC description = {};

    hr = adapter->GetDesc(&description);

    VR_ProbeRelease(adapter);

    if (FAILED(hr))
    {
        VR_ProbeLogHr(
            "IDXGIAdapter::GetDesc",
            hr);

        return false;
    }

    const bool matches =
        VR_ProbeLuidMatches(
            description.AdapterLuid,
            expectedLuid);

    Com_Printf(
        0,
        "[VR] OpenXR D3D11 adapter LUID "
        "match: %s.\n",
        matches ? "yes" : "no");

    return matches;
}
}

bool VR_ProbeD3D9ExD3D11Interop(
    IDirect3DDevice9* gameD3D9Device,
    ID3D11Device* openXrD3D11Device,
    const LUID& openXrAdapterLuid)
{
    if (openXrD3D11Device == nullptr)
    {
        return false;
    }

    Com_Printf(
        0,
        "[VR] Beginning non-invasive D3D9Ex/D3D11 "
        "shared-texture interop probe...\n");

    if (!VR_ProbeD3D11DeviceLuid(
            openXrD3D11Device,
            openXrAdapterLuid))
    {
        return false;
    }

    HMODULE d3d9Module =
        GetModuleHandleA("d3d9.dll");

    bool loadedD3D9Module = false;

    if (d3d9Module == nullptr)
    {
        d3d9Module =
            LoadLibraryA("d3d9.dll");

        loadedD3D9Module =
            d3d9Module != nullptr;
    }

    if (d3d9Module == nullptr)
    {
        return false;
    }

    const auto createDirect3D9Ex =
        reinterpret_cast<Direct3DCreate9ExFn>(
            GetProcAddress(
                d3d9Module,
                "Direct3DCreate9Ex"));

    if (createDirect3D9Ex == nullptr)
    {
        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    IDirect3D9Ex* direct3D9Ex = nullptr;

    HRESULT hr =
        createDirect3D9Ex(
            D3D_SDK_VERSION,
            &direct3D9Ex);

    if (FAILED(hr) ||
        direct3D9Ex == nullptr)
    {
        VR_ProbeLogHr(
            "Direct3DCreate9Ex",
            hr);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    UINT matchingAdapter = D3DADAPTER_DEFAULT;
    bool foundMatchingAdapter = false;

    const UINT adapterCount =
        direct3D9Ex->GetAdapterCount();

    for (UINT adapterIndex = 0u;
         adapterIndex < adapterCount;
         ++adapterIndex)
    {
        LUID candidateLuid = {};

        hr =
            direct3D9Ex->GetAdapterLUID(
                adapterIndex,
                &candidateLuid);

        if (SUCCEEDED(hr) &&
            VR_ProbeLuidMatches(
                candidateLuid,
                openXrAdapterLuid))
        {
            matchingAdapter = adapterIndex;
            foundMatchingAdapter = true;
            break;
        }
    }

    if (!foundMatchingAdapter)
    {
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    Com_Printf(
        0,
        "[VR] Found OpenXR-matching D3D9Ex adapter "
        "ordinal %u.\n",
        matchingAdapter);

    const bool gameAdapterMatches =
        VR_ProbeGameAdapterMatches(
            gameD3D9Device,
            direct3D9Ex,
            matchingAdapter);

    HWND probeWindow =
        CreateWindowExA(
            0u,
            "STATIC",
            "KisakCOD VR D3D9Ex Probe",
            WS_POPUP,
            0,
            0,
            16,
            16,
            nullptr,
            nullptr,
            GetModuleHandleA(nullptr),
            nullptr);

    if (probeWindow == nullptr)
    {
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    D3DPRESENT_PARAMETERS present = {};
    present.BackBufferWidth = 16u;
    present.BackBufferHeight = 16u;
    present.BackBufferFormat = D3DFMT_UNKNOWN;
    present.BackBufferCount = 1u;
    present.MultiSampleType =
        D3DMULTISAMPLE_NONE;
    present.SwapEffect =
        D3DSWAPEFFECT_DISCARD;
    present.hDeviceWindow = probeWindow;
    present.Windowed = TRUE;
    present.EnableAutoDepthStencil = FALSE;
    present.PresentationInterval =
        D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex* device9Ex = nullptr;

    DWORD behavior =
        D3DCREATE_HARDWARE_VERTEXPROCESSING |
        D3DCREATE_MULTITHREADED |
        D3DCREATE_FPU_PRESERVE;

    hr =
        direct3D9Ex->CreateDeviceEx(
            matchingAdapter,
            D3DDEVTYPE_HAL,
            probeWindow,
            behavior,
            &present,
            nullptr,
            &device9Ex);

    if (FAILED(hr))
    {
        behavior =
            D3DCREATE_SOFTWARE_VERTEXPROCESSING |
            D3DCREATE_MULTITHREADED |
            D3DCREATE_FPU_PRESERVE;

        hr =
            direct3D9Ex->CreateDeviceEx(
                matchingAdapter,
                D3DDEVTYPE_HAL,
                probeWindow,
                behavior,
                &present,
                nullptr,
                &device9Ex);
    }

    if (FAILED(hr) ||
        device9Ex == nullptr)
    {
        VR_ProbeLogHr(
            "IDirect3D9Ex::CreateDeviceEx",
            hr);

        DestroyWindow(probeWindow);
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    HANDLE sharedHandle = nullptr;
    IDirect3DTexture9* texture9 = nullptr;

    hr =
        device9Ex->CreateTexture(
            kProbeWidth,
            kProbeHeight,
            1u,
            D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &texture9,
            &sharedHandle);

    if (FAILED(hr) ||
        texture9 == nullptr ||
        sharedHandle == nullptr)
    {
        VR_ProbeLogHr(
            "D3D9Ex CreateTexture(shared)",
            hr);

        VR_ProbeRelease(device9Ex);
        DestroyWindow(probeWindow);
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    IDirect3DSurface9* surface9 = nullptr;

    hr =
        texture9->GetSurfaceLevel(
            0u,
            &surface9);

    if (SUCCEEDED(hr))
    {
        hr =
            device9Ex->ColorFill(
                surface9,
                nullptr,
                D3DCOLOR_ARGB(
                    kExpectedAlpha,
                    kExpectedRed,
                    kExpectedGreen,
                    kExpectedBlue));
    }

    VR_ProbeRelease(surface9);

    if (FAILED(hr) ||
        !VR_ProbeWaitForD3D9(device9Ex))
    {
        VR_ProbeRelease(texture9);
        VR_ProbeRelease(device9Ex);
        DestroyWindow(probeWindow);
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    ID3D11Texture2D* texture11 = nullptr;

    hr =
        openXrD3D11Device->OpenSharedResource(
            sharedHandle,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(
                &texture11));

    if (FAILED(hr) ||
        texture11 == nullptr)
    {
        VR_ProbeLogHr(
            "ID3D11Device::OpenSharedResource",
            hr);

        VR_ProbeRelease(texture9);
        VR_ProbeRelease(device9Ex);
        DestroyWindow(probeWindow);
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    D3D11_TEXTURE2D_DESC sharedDescription = {};
    texture11->GetDesc(&sharedDescription);

    ID3D11ShaderResourceView* sharedView = nullptr;

    hr =
        openXrD3D11Device->
            CreateShaderResourceView(
                texture11,
                nullptr,
                &sharedView);

    if (FAILED(hr) ||
        sharedView == nullptr)
    {
        VR_ProbeLogHr(
            "CreateShaderResourceView("
            "D3D9Ex shared texture)",
            hr);

        VR_ProbeRelease(texture11);
        VR_ProbeRelease(texture9);
        VR_ProbeRelease(device9Ex);
        DestroyWindow(probeWindow);
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDescription =
        sharedDescription;

    stagingDescription.Usage =
        D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0u;
    stagingDescription.CPUAccessFlags =
        D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0u;

    ID3D11Texture2D* stagingTexture = nullptr;

    hr =
        openXrD3D11Device->CreateTexture2D(
            &stagingDescription,
            nullptr,
            &stagingTexture);

    if (FAILED(hr) ||
        stagingTexture == nullptr)
    {
        VR_ProbeLogHr(
            "CreateTexture2D(staging probe)",
            hr);

        VR_ProbeRelease(sharedView);
        VR_ProbeRelease(texture11);
        VR_ProbeRelease(texture9);
        VR_ProbeRelease(device9Ex);
        DestroyWindow(probeWindow);
        VR_ProbeRelease(direct3D9Ex);

        if (loadedD3D9Module)
        {
            FreeLibrary(d3d9Module);
        }

        return false;
    }

    ID3D11DeviceContext* context11 = nullptr;

    openXrD3D11Device->GetImmediateContext(
        &context11);

    if (context11 == nullptr)
    {
        return false;
    }

    context11->CopyResource(
        stagingTexture,
        texture11);

    context11->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped = {};

    hr =
        context11->Map(
            stagingTexture,
            0u,
            D3D11_MAP_READ,
            0u,
            &mapped);

    bool pixelMatches = false;

    if (SUCCEEDED(hr) &&
        mapped.pData != nullptr)
    {
        const auto* pixel =
            static_cast<const std::uint8_t*>(
                mapped.pData);

        pixelMatches =
            pixel[0] == kExpectedBlue &&
            pixel[1] == kExpectedGreen &&
            pixel[2] == kExpectedRed &&
            pixel[3] == kExpectedAlpha;

        Com_Printf(
            0,
            "[VR] Shared-texture verification pixel "
            "BGRA = %02X %02X %02X %02X.\n",
            pixel[0],
            pixel[1],
            pixel[2],
            pixel[3]);

        context11->Unmap(
            stagingTexture,
            0u);
    }

    VR_ProbeRelease(context11);
    VR_ProbeRelease(stagingTexture);
    VR_ProbeRelease(sharedView);
    VR_ProbeRelease(texture11);
    VR_ProbeRelease(texture9);
    VR_ProbeRelease(device9Ex);
    DestroyWindow(probeWindow);
    VR_ProbeRelease(direct3D9Ex);

    if (loadedD3D9Module)
    {
        FreeLibrary(d3d9Module);
    }

    if (!pixelMatches)
    {
        return false;
    }

    Com_Printf(
        0,
        "[VR] D3D9Ex/D3D11 shared-texture "
        "interop probe passed.\n");

    if (!gameAdapterMatches)
    {
        Com_PrintWarning(
            0,
            "[VR] Shared interop works, but the game "
            "D3D9 adapter identity did not match the "
            "OpenXR adapter.\n");
    }

    return true;
}
