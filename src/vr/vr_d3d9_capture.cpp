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

// An odd divider is essential: with alternating render eyes, an even divider
// would repeatedly capture only one eye. A divider of three captures each eye
// once every six presented desktop frames.
constexpr unsigned int kCaptureDivider = 3u;

std::atomic<bool> g_captureEnabled{false};
std::atomic<std::uint32_t> g_renderEye{0u};

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

std::array<bool, kStereoEyeCount>
    g_loggedFirstCapture = {};

bool g_loggedFirstFailure = false;
bool g_loggedStereoDiagnostic = false;

const char* VR_EyeName(const std::uint32_t eyeIndex)
{
    return eyeIndex == 0u ? "left" : "right";
}

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

    g_renderEye.store(
        0u,
        std::memory_order_release);

    g_captureCounter = 0u;

    if (enabled)
    {
        if (!g_loggedStereoDiagnostic)
        {
            Com_Printf(
                0,
                "[VR] Alternating-eye stereo diagnostic enabled.\n");

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
        g_loggedFirstCapture[eyeIndex] = false;
    }

    g_loggedFirstFailure = false;
}

std::uint32_t VR_D3D9GetRenderEye()
{
    return
        g_renderEye.load(
            std::memory_order_acquire) %
        kStereoEyeCount;
}

void VR_D3D9CaptureFrame(IDirect3DDevice9* device)
{
    if (device == nullptr ||
        !g_captureEnabled.load(
            std::memory_order_acquire))
    {
        return;
    }

    // The camera used this eye for the frame that has just completed.
    const std::uint32_t capturedEye =
        VR_D3D9GetRenderEye();

    // Select the opposite eye before the next game frame builds its camera.
    g_renderEye.store(
        capturedEye ^ 1u,
        std::memory_order_release);

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

    if (FAILED(hr))
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

    const std::size_t rowBytes =
        static_cast<std::size_t>(
            description.Width) * 4u;

    std::vector<std::uint8_t> capturedPixels(
        rowBytes *
        static_cast<std::size_t>(
            description.Height));

    const auto* sourceRow =
        static_cast<const std::uint8_t*>(
            lockedRect.pBits);

    auto* destinationRow =
        capturedPixels.data();

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

    readbackSurface->UnlockRect();

    VR_ReleaseSurface(readbackSurface);
    VR_ReleaseSurface(resolveSurface);
    VR_ReleaseSurface(backBuffer);

    {
        std::lock_guard<std::mutex> lock(g_captureMutex);

        g_latestPixels[capturedEye].swap(
            capturedPixels);

        g_latestWidth[capturedEye] =
            description.Width;

        g_latestHeight[capturedEye] =
            description.Height;

        ++g_latestSerial[capturedEye];
    }

    if (!g_loggedFirstCapture[capturedEye])
    {
        Com_Printf(
            0,
            "[VR] Captured first D3D9 %s-eye frame: "
            "%u x %u.\n",
            VR_EyeName(capturedEye),
            description.Width,
            description.Height);

        g_loggedFirstCapture[capturedEye] = true;
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
