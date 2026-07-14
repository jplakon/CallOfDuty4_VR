#include "vr/vr_d3d9_capture.h"

#include "qcommon/qcommon.h"

#include <d3d9.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
constexpr std::uint32_t kStereoEyeCount = 2u;

// This remains a synchronous CPU readback diagnostic. The system-memory
// readback surface and CPU buffers are reusable, but D3DPOOL_DEFAULT resolve
// targets are transient so they cannot block a lost-device Reset.
constexpr unsigned int kCaptureDivider = 1u;

std::atomic<bool> g_captureEnabled{false};

unsigned int g_captureCounter = 0u;

std::mutex g_captureMutex;
std::mutex g_captureResourceMutex;

std::array<std::vector<std::uint8_t>, kStereoEyeCount>
    g_latestPixels;

std::array<std::vector<std::uint8_t>, kStereoEyeCount>
    g_captureScratchPixels;

std::array<std::uint32_t, kStereoEyeCount>
    g_latestWidth = {};

std::array<std::uint32_t, kStereoEyeCount>
    g_latestHeight = {};

std::array<std::uint64_t, kStereoEyeCount>
    g_latestSerial = {};

IDirect3DSurface9* g_readbackSurface = nullptr;

D3DSURFACE_DESC g_captureDescription = {};
bool g_captureDescriptionValid = false;

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

void VR_ReleaseCaptureResources()
{
    VR_ReleaseSurface(g_readbackSurface);

    g_captureDescription = {};
    g_captureDescriptionValid = false;
}

bool VR_CaptureDescriptionMatches(
    const D3DSURFACE_DESC& description)
{
    return
        g_captureDescriptionValid &&
        g_captureDescription.Width ==
            description.Width &&
        g_captureDescription.Height ==
            description.Height &&
        g_captureDescription.Format ==
            description.Format;
}

bool VR_EnsureCaptureResources(
    IDirect3DDevice9* device,
    const D3DSURFACE_DESC& description)
{
    if (device == nullptr)
    {
        return false;
    }

    if (VR_CaptureDescriptionMatches(description) &&
        g_readbackSurface != nullptr)
    {
        return true;
    }

    VR_ReleaseCaptureResources();

    HRESULT hr =
        device->CreateOffscreenPlainSurface(
            description.Width,
            description.Height,
            description.Format,
            D3DPOOL_SYSTEMMEM,
            &g_readbackSurface,
            nullptr);

    if (FAILED(hr))
    {
        if (!g_loggedFirstFailure)
        {
            Com_PrintWarning(
                0,
                "[VR] CreateOffscreenPlainSurface failed: "
                "0x%08lX.\n",
                static_cast<unsigned long>(hr));

            g_loggedFirstFailure = true;
        }

        VR_ReleaseCaptureResources();
        return false;
    }

    g_captureDescription = description;
    g_captureDescriptionValid = true;

    if (!g_loggedResourceCreation)
    {
        Com_Printf(
            0,
            "[VR] Created reusable reset-safe D3D9 readback "
            "surfaces: %u x %u.\n",
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
                "[VR] Reset-safe full-rate D3D9 stereo "
                "capture enabled.\n");

            g_loggedStereoDiagnostic = true;
        }

        return;
    }

    {
        std::lock_guard<std::mutex> resourceLock(
            g_captureResourceMutex);

        VR_ReleaseCaptureResources();
    }

    {
        std::lock_guard<std::mutex> captureLock(
            g_captureMutex);

        for (std::uint32_t eyeIndex = 0;
             eyeIndex < kStereoEyeCount;
             ++eyeIndex)
        {
            g_latestPixels[eyeIndex].clear();
            g_captureScratchPixels[eyeIndex].clear();
            g_latestWidth[eyeIndex] = 0u;
            g_latestHeight[eyeIndex] = 0u;
            g_latestSerial[eyeIndex] = 0u;
        }
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

            VR_ReleaseCaptureResources();
        }

        if (!g_loggedDeviceLostPause)
        {
            Com_PrintWarning(
                0,
                "[VR] D3D9 capture paused for device "
                "reset: 0x%08lX.\n",
                static_cast<unsigned long>(
                    cooperativeLevel));

            g_loggedDeviceLostPause = true;
        }

        return;
    }

    g_loggedDeviceLostPause = false;

    IDirect3DSurface9* backBuffer = nullptr;
    IDirect3DSurface9* transientResolveSurface = nullptr;

    HRESULT hr =
        device->GetBackBuffer(
            0,
            0,
            D3DBACKBUFFER_TYPE_MONO,
            &backBuffer);

    if (FAILED(hr))
    {
        if (!g_loggedFirstFailure)
        {
            Com_PrintWarning(
                0,
                "[VR] D3D9 GetBackBuffer failed: 0x%08lX.\n",
                static_cast<unsigned long>(hr));

            g_loggedFirstFailure = true;
        }

        return;
    }

    D3DSURFACE_DESC description = {};

    hr = backBuffer->GetDesc(&description);

    if (FAILED(hr) || description.Width < 2u)
    {
        VR_ReleaseSurface(backBuffer);
        return;
    }

    const std::uint32_t eyeWidth =
        description.Width / 2u;

    const std::size_t eyeRowBytes =
        static_cast<std::size_t>(eyeWidth) * 4u;

    const std::size_t eyeImageBytes =
        eyeRowBytes *
        static_cast<std::size_t>(
            description.Height);

    for (std::uint32_t eyeIndex = 0;
         eyeIndex < kStereoEyeCount;
         ++eyeIndex)
    {
        g_captureScratchPixels[eyeIndex].resize(
            eyeImageBytes);
    }

    {
        std::lock_guard<std::mutex> resourceLock(
            g_captureResourceMutex);

        if (!VR_EnsureCaptureResources(
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
                if (!g_loggedFirstFailure)
                {
                    Com_PrintWarning(
                        0,
                        "[VR] Transient D3D9 multisample "
                        "resolve failed: 0x%08lX.\n",
                        static_cast<unsigned long>(hr));

                    g_loggedFirstFailure = true;
                }

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

        // A D3DPOOL_DEFAULT resolve target must not survive this frame.
        VR_ReleaseSurface(
            transientResolveSurface);

        if (FAILED(hr))
        {
            if (hr == D3DERR_DEVICELOST ||
                hr == D3DERR_DEVICENOTRESET)
            {
                VR_ReleaseCaptureResources();

                if (!g_loggedDeviceLostPause)
                {
                    Com_PrintWarning(
                        0,
                        "[VR] D3D9 capture paused for device "
                        "reset after GetRenderTargetData: "
                        "0x%08lX.\n",
                        static_cast<unsigned long>(hr));

                    g_loggedDeviceLostPause = true;
                }
            }
            else if (!g_loggedFirstFailure)
            {
                Com_PrintWarning(
                    0,
                    "[VR] D3D9 GetRenderTargetData failed: "
                    "0x%08lX.\n",
                    static_cast<unsigned long>(hr));

                g_loggedFirstFailure = true;
            }

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
            VR_ReleaseSurface(backBuffer);
            return;
        }

        const auto* sourceRow =
            static_cast<const std::uint8_t*>(
                lockedRect.pBits);

        auto* leftDestination =
            g_captureScratchPixels[0].data();

        auto* rightDestination =
            g_captureScratchPixels[1].data();

        for (UINT row = 0;
             row < description.Height;
             ++row)
        {
            std::memcpy(
                leftDestination,
                sourceRow,
                eyeRowBytes);

            std::memcpy(
                rightDestination,
                sourceRow + eyeRowBytes,
                eyeRowBytes);

            sourceRow += lockedRect.Pitch;
            leftDestination += eyeRowBytes;
            rightDestination += eyeRowBytes;
        }

        g_readbackSurface->UnlockRect();
    }

    VR_ReleaseSurface(backBuffer);

    {
        std::lock_guard<std::mutex> captureLock(
            g_captureMutex);

        for (std::uint32_t eyeIndex = 0;
             eyeIndex < kStereoEyeCount;
             ++eyeIndex)
        {
            g_latestPixels[eyeIndex].swap(
                g_captureScratchPixels[eyeIndex]);

            g_latestWidth[eyeIndex] = eyeWidth;
            g_latestHeight[eyeIndex] =
                description.Height;

            ++g_latestSerial[eyeIndex];
        }
    }

    if (!g_loggedFirstCapture)
    {
        Com_Printf(
            0,
            "[VR] Captured first same-frame stereo pair: "
            "%u x %u per eye.\n",
            eyeWidth,
            description.Height);

        g_loggedFirstCapture = true;
    }
}

bool VR_D3D9CopyLatestEyeFrame(
    const std::uint32_t eyeIndex,
    const std::uint64_t lastSerial,
    std::vector<std::uint8_t>& pixels,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint64_t& serial)
{
    if (eyeIndex >= kStereoEyeCount)
    {
        return false;
    }

    std::lock_guard<std::mutex> captureLock(
        g_captureMutex);

    if (g_latestSerial[eyeIndex] == 0u ||
        g_latestSerial[eyeIndex] == lastSerial ||
        g_latestPixels[eyeIndex].empty())
    {
        return false;
    }

    pixels = g_latestPixels[eyeIndex];
    width = g_latestWidth[eyeIndex];
    height = g_latestHeight[eyeIndex];
    serial = g_latestSerial[eyeIndex];

    return true;
}
