#include "vr/vr_d3d9_capture.h"

#include "qcommon/qcommon.h"

#include <d3d9.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
constexpr unsigned int kCaptureDivider = 1u;

std::atomic<bool> g_captureEnabled{false};

unsigned int g_captureCounter = 0u;

std::mutex g_captureMutex;
std::mutex g_captureResourceMutex;

std::vector<std::uint8_t> g_latestPixels;
std::vector<std::uint8_t> g_captureScratchPixels;

std::uint32_t g_latestWidth = 0u;
std::uint32_t g_latestHeight = 0u;
std::uint64_t g_latestSerial = 0u;

// This is D3DPOOL_SYSTEMMEM and therefore does not block Reset().
IDirect3DSurface9* g_readbackSurface = nullptr;

std::uint32_t g_readbackWidth = 0u;
std::uint32_t g_readbackHeight = 0u;
D3DFORMAT g_readbackFormat = D3DFMT_UNKNOWN;

bool g_loggedFirstCapture = false;
bool g_loggedFirstFailure = false;
bool g_loggedStereoDiagnostic = false;
bool g_loggedResourceCreation = false;
bool g_loggedDeviceLostPause = false;

void VR_ReleaseSurface(IDirect3DSurface9*& surface)
{
    if (surface != nullptr)
    {
        surface->Release();
        surface = nullptr;
    }
}

void VR_ReleaseReadbackSurface()
{
    VR_ReleaseSurface(g_readbackSurface);

    g_readbackWidth = 0u;
    g_readbackHeight = 0u;
    g_readbackFormat = D3DFMT_UNKNOWN;
}

bool VR_IsLostDeviceResult(const HRESULT hr)
{
    return
        hr == D3DERR_DEVICELOST ||
        hr == D3DERR_DEVICENOTRESET;
}

void VR_LogCaptureFailure(
    const char* operation,
    const HRESULT hr)
{
    if (VR_IsLostDeviceResult(hr))
    {
        VR_ReleaseReadbackSurface();

        if (!g_loggedDeviceLostPause)
        {
            Com_PrintWarning(
                0,
                "[VR] D3D9 capture paused for device "
                "reset after %s: 0x%08lX.\n",
                operation,
                static_cast<unsigned long>(hr));

            g_loggedDeviceLostPause = true;
        }

        return;
    }

    if (!g_loggedFirstFailure)
    {
        Com_PrintWarning(
            0,
            "[VR] %s failed: 0x%08lX.\n",
            operation,
            static_cast<unsigned long>(hr));

        g_loggedFirstFailure = true;
    }
}

bool VR_EnsureReadbackSurface(
    IDirect3DDevice9* device,
    const D3DSURFACE_DESC& description)
{
    if (device == nullptr)
    {
        return false;
    }

    if (g_readbackSurface != nullptr &&
        g_readbackWidth == description.Width &&
        g_readbackHeight == description.Height &&
        g_readbackFormat == description.Format)
    {
        return true;
    }

    VR_ReleaseReadbackSurface();

    const HRESULT hr =
        device->CreateOffscreenPlainSurface(
            description.Width,
            description.Height,
            description.Format,
            D3DPOOL_SYSTEMMEM,
            &g_readbackSurface,
            nullptr);

    if (FAILED(hr))
    {
        VR_LogCaptureFailure(
            "CreateOffscreenPlainSurface",
            hr);

        return false;
    }

    g_readbackWidth = description.Width;
    g_readbackHeight = description.Height;
    g_readbackFormat = description.Format;

    if (!g_loggedResourceCreation)
    {
        Com_Printf(
            0,
            "[VR] Created reusable reset-safe D3D9 "
            "readback surface: %u x %u.\n",
            description.Width,
            description.Height);

        g_loggedResourceCreation = true;
    }

    return true;
}
}

void VR_D3D9CaptureSetEnabled(const bool enabled)
{
    g_captureEnabled.store(
        enabled,
        std::memory_order_release);

    g_captureCounter = 0u;

    if (enabled)
    {
        if (!g_loggedStereoDiagnostic)
        {
            Com_Printf(
                0,
                "[VR] Single-buffer reset-safe full-rate "
                "D3D9 stereo capture enabled.\n");

            g_loggedStereoDiagnostic = true;
        }

        return;
    }

    {
        std::lock_guard<std::mutex> resourceLock(
            g_captureResourceMutex);

        VR_ReleaseReadbackSurface();
    }

    {
        std::lock_guard<std::mutex> captureLock(
            g_captureMutex);

        g_latestPixels.clear();
        g_captureScratchPixels.clear();
        g_latestWidth = 0u;
        g_latestHeight = 0u;
        g_latestSerial = 0u;
    }

    g_loggedFirstCapture = false;
    g_loggedFirstFailure = false;
    g_loggedResourceCreation = false;
    g_loggedDeviceLostPause = false;
}

bool VR_D3D9IsSameFrameStereoEnabled()
{
    return
        g_captureEnabled.load(
            std::memory_order_acquire);
}

void VR_D3D9CaptureFrame(IDirect3DDevice9* device)
{
    if (device == nullptr ||
        !VR_D3D9IsSameFrameStereoEnabled())
    {
        return;
    }

    ++g_captureCounter;

    if ((g_captureCounter % kCaptureDivider) != 0u)
    {
        return;
    }

    const HRESULT cooperativeLevel =
        device->TestCooperativeLevel();

    if (FAILED(cooperativeLevel))
    {
        {
            std::lock_guard<std::mutex> resourceLock(
                g_captureResourceMutex);

            VR_LogCaptureFailure(
                "TestCooperativeLevel",
                cooperativeLevel);
        }

        return;
    }

    g_loggedDeviceLostPause = false;

    IDirect3DSurface9* backBuffer = nullptr;
    IDirect3DSurface9* transientResolveSurface =
        nullptr;

    HRESULT hr =
        device->GetBackBuffer(
            0,
            0,
            D3DBACKBUFFER_TYPE_MONO,
            &backBuffer);

    if (FAILED(hr))
    {
        std::lock_guard<std::mutex> resourceLock(
            g_captureResourceMutex);

        VR_LogCaptureFailure(
            "GetBackBuffer",
            hr);

        return;
    }

    D3DSURFACE_DESC description = {};

    hr = backBuffer->GetDesc(&description);

    if (FAILED(hr) ||
        description.Width < 2u ||
        description.Height == 0u)
    {
        VR_ReleaseSurface(backBuffer);
        return;
    }

    const std::size_t rowBytes =
        static_cast<std::size_t>(
            description.Width) * 4u;

    const std::size_t imageBytes =
        rowBytes *
        static_cast<std::size_t>(
            description.Height);

    g_captureScratchPixels.resize(imageBytes);

    {
        std::lock_guard<std::mutex> resourceLock(
            g_captureResourceMutex);

        if (!VR_EnsureReadbackSurface(
                device,
                description))
        {
            VR_ReleaseSurface(backBuffer);
            return;
        }

        IDirect3DSurface9* captureSource =
            backBuffer;

        if (description.MultiSampleType !=
            D3DMULTISAMPLE_NONE)
        {
            hr =
                device->CreateRenderTarget(
                    description.Width,
                    description.Height,
                    description.Format,
                    D3DMULTISAMPLE_NONE,
                    0,
                    FALSE,
                    &transientResolveSurface,
                    nullptr);

            if (SUCCEEDED(hr))
            {
                hr =
                    device->StretchRect(
                        backBuffer,
                        nullptr,
                        transientResolveSurface,
                        nullptr,
                        D3DTEXF_NONE);
            }

            if (FAILED(hr))
            {
                VR_LogCaptureFailure(
                    "multisample resolve",
                    hr);

                VR_ReleaseSurface(
                    transientResolveSurface);

                VR_ReleaseSurface(backBuffer);
                return;
            }

            captureSource =
                transientResolveSurface;
        }

        hr =
            device->GetRenderTargetData(
                captureSource,
                g_readbackSurface);

        VR_ReleaseSurface(
            transientResolveSurface);

        if (FAILED(hr))
        {
            VR_LogCaptureFailure(
                "GetRenderTargetData",
                hr);

            VR_ReleaseSurface(backBuffer);
            return;
        }

        D3DLOCKED_RECT lockedRect = {};

        hr =
            g_readbackSurface->LockRect(
                &lockedRect,
                nullptr,
                D3DLOCK_READONLY |
                    D3DLOCK_NOSYSLOCK);

        if (FAILED(hr))
        {
            VR_LogCaptureFailure(
                "LockRect",
                hr);

            VR_ReleaseSurface(backBuffer);
            return;
        }

        const auto* sourceRow =
            static_cast<const std::uint8_t*>(
                lockedRect.pBits);

        auto* destinationRow =
            g_captureScratchPixels.data();

        for (UINT row = 0;
             row < description.Height;
             ++row)
        {
            std::memcpy(
                destinationRow,
                sourceRow,
                rowBytes);

            sourceRow += lockedRect.Pitch;
            destinationRow += rowBytes;
        }

        g_readbackSurface->UnlockRect();
    }

    VR_ReleaseSurface(backBuffer);

    {
        std::lock_guard<std::mutex> captureLock(
            g_captureMutex);

        g_latestPixels.swap(
            g_captureScratchPixels);

        g_latestWidth =
            description.Width;

        g_latestHeight =
            description.Height;

        ++g_latestSerial;
    }

    if (!g_loggedFirstCapture)
    {
        Com_Printf(
            0,
            "[VR] Captured first complete side-by-side "
            "stereo frame: %u x %u.\n",
            description.Width,
            description.Height);

        g_loggedFirstCapture = true;
    }
}

bool VR_D3D9CopyLatestStereoFrame(
    const std::uint64_t lastSerial,
    std::vector<std::uint8_t>& pixels,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint64_t& serial)
{
    std::lock_guard<std::mutex> captureLock(
        g_captureMutex);

    if (g_latestSerial == 0u ||
        g_latestSerial == lastSerial ||
        g_latestPixels.empty())
    {
        return false;
    }

    pixels = g_latestPixels;
    width = g_latestWidth;
    height = g_latestHeight;
    serial = g_latestSerial;

    return true;
}
