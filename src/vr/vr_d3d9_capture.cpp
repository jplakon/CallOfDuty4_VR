#include "vr/vr_d3d9_capture.h"

#include "qcommon/qcommon.h"

#include <d3d9.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
std::atomic<bool> g_captureEnabled{false};

std::mutex g_captureMutex;
std::vector<std::uint8_t> g_latestPixels;

std::uint32_t g_latestWidth = 0;
std::uint32_t g_latestHeight = 0;
std::uint64_t g_latestSerial = 0;

bool g_loggedFirstCapture = false;
bool g_loggedFirstFailure = false;

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

    if (!enabled)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);

        g_latestPixels.clear();
        g_latestWidth = 0;
        g_latestHeight = 0;
        g_latestSerial = 0;
    }
}

void VR_D3D9CaptureFrame(IDirect3DDevice9* device)
{
    if (device == nullptr ||
        !g_captureEnabled.load(
            std::memory_order_acquire))
    {
        return;
    }

    // This diagnostic intentionally uses a slow CPU readback. Restrict it to
    // roughly one out of every six presented frames to keep the game usable.
    static unsigned int captureDivider = 0;

    ++captureDivider;

    if ((captureDivider % 6u) != 0u)
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

        g_latestPixels.swap(capturedPixels);
        g_latestWidth = description.Width;
        g_latestHeight = description.Height;
        ++g_latestSerial;
    }

    if (!g_loggedFirstCapture)
    {
        Com_Printf(
            0,
            "[VR] Captured first D3D9 backbuffer: %u x %u.\n",
            description.Width,
            description.Height);

        g_loggedFirstCapture = true;
    }
}

bool VR_D3D9CopyLatestFrame(
    const std::uint64_t lastSerial,
    std::vector<std::uint8_t>& pixels,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint64_t& serial)
{
    std::lock_guard<std::mutex> lock(g_captureMutex);

    if (g_latestSerial == 0 ||
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
