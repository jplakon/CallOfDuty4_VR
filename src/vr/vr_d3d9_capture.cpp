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

// Both eyes are now captured together. One readback every three desktop
// frames is still intentionally conservative because GetRenderTargetData is
// a synchronous CPU/GPU stall.
constexpr unsigned int kCaptureDivider = 3u;

std::atomic<bool> g_captureEnabled{false};

unsigned int g_captureCounter = 0u;

std::mutex g_captureMutex;

std::array<std::vector<std::uint8_t>, kStereoEyeCount>
    g_latestPixels;

std::array<std::uint32_t, kStereoEyeCount>
    g_latestWidth = {};

std::array<std::uint32_t, kStereoEyeCount>
    g_latestHeight = {};

std::array<std::uint64_t, kStereoEyeCount>
    g_latestSerial = {};

bool g_loggedFirstCapture = false;
bool g_loggedFirstFailure = false;
bool g_loggedStereoDiagnostic = false;

void VR_ReleaseSurface(IDirect3DSurface9*& surface)
{
    if (surface != nullptr)
    {
        surface->Release();
        surface = nullptr;
    }
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
                "[VR] Same-frame side-by-side stereo "
                "diagnostic enabled.\n");

            g_loggedStereoDiagnostic = true;
        }

        return;
    }

    std::lock_guard<std::mutex> lock(g_captureMutex);

    for (std::uint32_t eyeIndex = 0;
         eyeIndex < kStereoEyeCount;
         ++eyeIndex)
    {
        g_latestPixels[eyeIndex].clear();
        g_latestWidth[eyeIndex] = 0u;
        g_latestHeight[eyeIndex] = 0u;
        g_latestSerial[eyeIndex] = 0u;
    }

    g_loggedFirstCapture = false;
    g_loggedFirstFailure = false;
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

    IDirect3DSurface9* backBuffer = nullptr;
    IDirect3DSurface9* resolveSurface = nullptr;
    IDirect3DSurface9* readbackSurface = nullptr;

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

    IDirect3DSurface9* captureSource = backBuffer;

    if (description.MultiSampleType != D3DMULTISAMPLE_NONE)
    {
        hr =
            device->CreateRenderTarget(
                description.Width,
                description.Height,
                description.Format,
                D3DMULTISAMPLE_NONE,
                0,
                FALSE,
                &resolveSurface,
                nullptr);

        if (SUCCEEDED(hr))
        {
            hr =
                device->StretchRect(
                    backBuffer,
                    nullptr,
                    resolveSurface,
                    nullptr,
                    D3DTEXF_NONE);
        }

        if (FAILED(hr))
        {
            if (!g_loggedFirstFailure)
            {
                Com_PrintWarning(
                    0,
                    "[VR] D3D9 multisample resolve failed: "
                    "0x%08lX.\n",
                    static_cast<unsigned long>(hr));

                g_loggedFirstFailure = true;
            }

            VR_ReleaseSurface(resolveSurface);
            VR_ReleaseSurface(backBuffer);
            return;
        }

        captureSource = resolveSurface;
    }

    hr =
        device->CreateOffscreenPlainSurface(
            description.Width,
            description.Height,
            description.Format,
            D3DPOOL_SYSTEMMEM,
            &readbackSurface,
            nullptr);

    if (SUCCEEDED(hr))
    {
        hr =
            device->GetRenderTargetData(
                captureSource,
                readbackSurface);
    }

    if (FAILED(hr))
    {
        if (!g_loggedFirstFailure)
        {
            Com_PrintWarning(
                0,
                "[VR] D3D9 GetRenderTargetData failed: "
                "0x%08lX.\n",
                static_cast<unsigned long>(hr));

            g_loggedFirstFailure = true;
        }

        VR_ReleaseSurface(readbackSurface);
        VR_ReleaseSurface(resolveSurface);
        VR_ReleaseSurface(backBuffer);
        return;
    }

    D3DLOCKED_RECT lockedRect = {};

    hr =
        readbackSurface->LockRect(
            &lockedRect,
            nullptr,
            D3DLOCK_READONLY);

    if (FAILED(hr))
    {
        VR_ReleaseSurface(readbackSurface);
        VR_ReleaseSurface(resolveSurface);
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

    std::array<std::vector<std::uint8_t>, kStereoEyeCount>
        capturedPixels = {
            std::vector<std::uint8_t>(eyeImageBytes),
            std::vector<std::uint8_t>(eyeImageBytes),
        };

    const auto* sourceRow =
        static_cast<const std::uint8_t*>(
            lockedRect.pBits);

    auto* leftDestination =
        capturedPixels[0].data();

    auto* rightDestination =
        capturedPixels[1].data();

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

    readbackSurface->UnlockRect();

    VR_ReleaseSurface(readbackSurface);
    VR_ReleaseSurface(resolveSurface);
    VR_ReleaseSurface(backBuffer);

    {
        std::lock_guard<std::mutex> lock(g_captureMutex);

        for (std::uint32_t eyeIndex = 0;
             eyeIndex < kStereoEyeCount;
             ++eyeIndex)
        {
            g_latestPixels[eyeIndex].swap(
                capturedPixels[eyeIndex]);

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

    std::lock_guard<std::mutex> lock(g_captureMutex);

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
