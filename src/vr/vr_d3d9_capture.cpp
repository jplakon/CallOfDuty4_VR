#include "vr/vr_d3d9_capture.h"
#include <chrono>

#include "qcommon/qcommon.h"

#include <Windows.h>
#include <d3d9.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
// KISAK_SP_VR_GPU_SHARED_BRIDGE_V1
constexpr unsigned int kCaptureDivider = 1u;

enum class VrSharedSlotState : std::uint8_t
{
    Free,
    Producing,
    Ready,
    Acquired,
};

enum class VrSharedCaptureResult : std::uint8_t
{
    Captured,
    Busy,
    Unavailable,
};

struct VrSharedCaptureSlot
{
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* surface = nullptr;
    IDirect3DQuery9* producerQuery = nullptr;
    HANDLE sharedHandle = nullptr;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint64_t generation = 0u;
    std::uint64_t serial = 0u;
    VrSharedSlotState state =
        VrSharedSlotState::Free;
};

std::atomic<bool> g_captureEnabled{false};
std::atomic<bool> g_sharedBridgeActive{false};

unsigned int g_captureCounter = 0u;

std::mutex g_captureMutex;
std::mutex g_captureResourceMutex;

std::vector<std::uint8_t> g_latestPixels;
std::vector<std::uint8_t> g_captureScratchPixels;

std::uint32_t g_latestWidth = 0u;
std::uint32_t g_latestHeight = 0u;
std::uint64_t g_latestSerial = 0u;
std::uint64_t g_captureSerialCounter = 0u;

// CPU fallback.  This is D3DPOOL_SYSTEMMEM and therefore does not block
// legacy IDirect3DDevice9::Reset().
IDirect3DSurface9* g_readbackSurface = nullptr;

std::uint32_t g_readbackWidth = 0u;
std::uint32_t g_readbackHeight = 0u;
D3DFORMAT g_readbackFormat = D3DFMT_UNKNOWN;

std::array<
    VrSharedCaptureSlot,
    kVrD3D9SharedFrameSlotCount>
    g_sharedSlots = {};

std::atomic<bool> g_sharedBridgeUnavailable{false};

bool g_loggedFirstCapture = false;
bool g_loggedFirstSharedCapture = false;
bool g_loggedFirstFailure = false;
bool g_loggedStereoDiagnostic = false;
bool g_loggedResourceCreation = false;
bool g_loggedSharedBackpressure = false;
bool g_loggedSharedFallback = false;
bool g_loggedDeviceLostPause = false;

template <typename T>
void VR_ReleaseObject(T*& object)
{
    if (object != nullptr)
    {
        object->Release();
        object = nullptr;
    }
}

void VR_ReleaseSurface(IDirect3DSurface9*& surface)
{
    VR_ReleaseObject(surface);
}

void VR_ReleaseReadbackSurface()
{
    VR_ReleaseSurface(g_readbackSurface);

    g_readbackWidth = 0u;
    g_readbackHeight = 0u;
    g_readbackFormat = D3DFMT_UNKNOWN;
}

void VR_ReleaseSharedSlotResources(
    VrSharedCaptureSlot& slot)
{
    const std::uint64_t generation =
        slot.generation;

    VR_ReleaseObject(slot.producerQuery);
    VR_ReleaseSurface(slot.surface);
    VR_ReleaseObject(slot.texture);

    slot = VrSharedCaptureSlot{};
    slot.generation = generation;
}

void VR_ReleaseAllSharedResources()
{
    for (VrSharedCaptureSlot& slot :
         g_sharedSlots)
    {
        VR_ReleaseSharedSlotResources(slot);
    }

    g_sharedBridgeActive.store(
        false,
        std::memory_order_release);
}

bool VR_SharedBridgeRequested()
{
    char setting[2] = {};

    const DWORD settingLength =
        GetEnvironmentVariableA(
            "KISAK_VR_GPU_BRIDGE",
            setting,
            sizeof(setting));

    return
        settingLength == 1u &&
        setting[0] == '1';
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

void VR_PollSharedProducerSlots(
    const bool flush)
{
    const DWORD flags =
        flush
            ? D3DGETDATA_FLUSH
            : 0u;

    for (VrSharedCaptureSlot& slot :
         g_sharedSlots)
    {
        if (slot.state !=
                VrSharedSlotState::Producing ||
            slot.producerQuery == nullptr)
        {
            continue;
        }

        const HRESULT hr =
            slot.producerQuery->GetData(
                nullptr,
                0u,
                flags);

        if (hr == S_OK)
        {
            slot.state =
                VrSharedSlotState::Ready;
        }
        else if (FAILED(hr))
        {
            VR_LogCaptureFailure(
                "D3D9 shared producer query",
                hr);

            VR_ReleaseSharedSlotResources(slot);
        }
    }
}

bool VR_EnsureSharedSlot(
    IDirect3DDevice9* device,
    VrSharedCaptureSlot& slot,
    const D3DSURFACE_DESC& description)
{
    if (slot.texture != nullptr &&
        slot.surface != nullptr &&
        slot.producerQuery != nullptr &&
        slot.sharedHandle != nullptr &&
        slot.width == description.Width &&
        slot.height == description.Height)
    {
        return true;
    }

    VR_ReleaseSharedSlotResources(slot);

    IDirect3DDevice9Ex* device9Ex = nullptr;

    HRESULT hr =
        device->QueryInterface(
            __uuidof(IDirect3DDevice9Ex),
            reinterpret_cast<void**>(
                &device9Ex));

    if (FAILED(hr) ||
        device9Ex == nullptr)
    {
        g_sharedBridgeUnavailable.store(
            true,
            std::memory_order_release);
        g_sharedBridgeActive.store(
            false,
            std::memory_order_release);

        if (!g_loggedSharedFallback)
        {
            Com_PrintWarning(
                0,
                "[VR] The game device is not D3D9Ex; "
                "using the CPU frame bridge.\n");

            g_loggedSharedFallback = true;
        }

        return false;
    }

    HANDLE sharedHandle = nullptr;
    IDirect3DTexture9* texture = nullptr;

    hr =
        device9Ex->CreateTexture(
            description.Width,
            description.Height,
            1u,
            D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &texture,
            &sharedHandle);

    device9Ex->Release();

    if (FAILED(hr) ||
        texture == nullptr ||
        sharedHandle == nullptr)
    {
        VR_ReleaseObject(texture);

        g_sharedBridgeUnavailable.store(
            true,
            std::memory_order_release);
        g_sharedBridgeActive.store(
            false,
            std::memory_order_release);

        VR_LogCaptureFailure(
            "CreateTexture(D3D9Ex shared frame)",
            hr);

        return false;
    }

    IDirect3DSurface9* surface = nullptr;

    hr =
        texture->GetSurfaceLevel(
            0u,
            &surface);

    if (FAILED(hr) ||
        surface == nullptr)
    {
        VR_ReleaseObject(texture);

        g_sharedBridgeUnavailable.store(
            true,
            std::memory_order_release);
        g_sharedBridgeActive.store(
            false,
            std::memory_order_release);

        VR_LogCaptureFailure(
            "GetSurfaceLevel(D3D9Ex shared frame)",
            hr);

        return false;
    }

    IDirect3DQuery9* producerQuery = nullptr;

    hr =
        device->CreateQuery(
            D3DQUERYTYPE_EVENT,
            &producerQuery);

    if (FAILED(hr) ||
        producerQuery == nullptr)
    {
        VR_ReleaseSurface(surface);
        VR_ReleaseObject(texture);

        g_sharedBridgeUnavailable.store(
            true,
            std::memory_order_release);
        g_sharedBridgeActive.store(
            false,
            std::memory_order_release);

        VR_LogCaptureFailure(
            "CreateQuery(D3D9 shared producer)",
            hr);

        return false;
    }

    slot.texture = texture;
    slot.surface = surface;
    slot.producerQuery = producerQuery;
    slot.sharedHandle = sharedHandle;
    slot.width = description.Width;
    slot.height = description.Height;
    ++slot.generation;
    slot.serial = 0u;
    slot.state = VrSharedSlotState::Free;

    return true;
}

VrSharedCaptureResult VR_TryCaptureSharedFrame(
    IDirect3DDevice9* device,
    IDirect3DSurface9* backBuffer,
    const D3DSURFACE_DESC& description)
{
    if (!VR_SharedBridgeRequested() ||
        g_sharedBridgeUnavailable.load(
            std::memory_order_acquire))
    {
        return VrSharedCaptureResult::Unavailable;
    }

    std::lock_guard<std::mutex> resourceLock(
        g_captureResourceMutex);

    VR_PollSharedProducerSlots(false);

    std::uint32_t slotIndex =
        kVrD3D9SharedFrameSlotCount;

    for (std::uint32_t index = 0u;
         index < kVrD3D9SharedFrameSlotCount;
         ++index)
    {
        if (g_sharedSlots[index].state ==
            VrSharedSlotState::Free)
        {
            slotIndex = index;
            break;
        }
    }

    // A completed frame that D3D11 never acquired may be dropped safely.
    if (slotIndex ==
        kVrD3D9SharedFrameSlotCount)
    {
        std::uint64_t oldestSerial =
            ~static_cast<std::uint64_t>(0u);

        for (std::uint32_t index = 0u;
             index < kVrD3D9SharedFrameSlotCount;
             ++index)
        {
            const VrSharedCaptureSlot& slot =
                g_sharedSlots[index];

            if (slot.state ==
                    VrSharedSlotState::Ready &&
                slot.serial < oldestSerial)
            {
                oldestSerial = slot.serial;
                slotIndex = index;
            }
        }

        if (slotIndex <
            kVrD3D9SharedFrameSlotCount)
        {
            g_sharedSlots[slotIndex].state =
                VrSharedSlotState::Free;
        }
    }

    if (slotIndex ==
        kVrD3D9SharedFrameSlotCount)
    {
        if (!g_loggedSharedBackpressure)
        {
            Com_PrintWarning(
                0,
                "[VR] GPU-shared capture is waiting for "
                "a consumer-safe texture slot.\n");

            g_loggedSharedBackpressure = true;
        }

        return VrSharedCaptureResult::Busy;
    }

    VrSharedCaptureSlot& slot =
        g_sharedSlots[slotIndex];

    if (!VR_EnsureSharedSlot(
            device,
            slot,
            description))
    {
        return VrSharedCaptureResult::Unavailable;
    }

    HRESULT hr =
        device->StretchRect(
            backBuffer,
            nullptr,
            slot.surface,
            nullptr,
            D3DTEXF_NONE);

    if (SUCCEEDED(hr))
    {
        hr =
            slot.producerQuery->Issue(
                D3DISSUE_END);
    }

    if (FAILED(hr))
    {
        VR_LogCaptureFailure(
            "GPU-shared backbuffer resolve",
            hr);

        VR_ReleaseSharedSlotResources(slot);
        g_sharedBridgeUnavailable.store(
            true,
            std::memory_order_release);
        g_sharedBridgeActive.store(
            false,
            std::memory_order_release);

        return VrSharedCaptureResult::Unavailable;
    }

    slot.serial =
        ++g_captureSerialCounter;
    slot.state =
        VrSharedSlotState::Producing;

    g_sharedBridgeActive.store(
        true,
        std::memory_order_release);

    if (!g_loggedFirstSharedCapture)
    {
        Com_Printf(
            0,
            "[VR] GPU-shared D3D9Ex capture active: "
            "%u x %u, three fenced textures, "
            "no CPU pixel readback.\n",
            description.Width,
            description.Height);

        g_loggedFirstSharedCapture = true;
    }

    return VrSharedCaptureResult::Captured;
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
        g_sharedBridgeUnavailable.store(
            false,
            std::memory_order_release);
        g_sharedBridgeActive.store(
            false,
            std::memory_order_release);

        if (!g_loggedStereoDiagnostic)
        {
            Com_Printf(
                0,
                "[VR] Reset-safe full-rate D3D9 stereo "
                "capture enabled; GPU sharing is opt-in.\n");

            g_loggedStereoDiagnostic = true;
        }

        return;
    }

    {
        std::lock_guard<std::mutex> resourceLock(
            g_captureResourceMutex);

        VR_ReleaseReadbackSurface();
        VR_ReleaseAllSharedResources();
    }

    {
        std::lock_guard<std::mutex> captureLock(
            g_captureMutex);

        g_latestPixels.clear();
        g_captureScratchPixels.clear();
        g_latestWidth = 0u;
        g_latestHeight = 0u;
        g_latestSerial = 0u;
        g_captureSerialCounter = 0u;
    }

    g_sharedBridgeUnavailable.store(
        false,
        std::memory_order_release);
    g_loggedFirstCapture = false;
    g_loggedFirstSharedCapture = false;
    g_loggedFirstFailure = false;
    g_loggedResourceCreation = false;
    g_loggedSharedBackpressure = false;
    g_loggedSharedFallback = false;
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

    const VrSharedCaptureResult sharedResult =
        VR_TryCaptureSharedFrame(
            device,
            backBuffer,
            description);

    if (sharedResult ==
        VrSharedCaptureResult::Captured)
    {
        VR_ReleaseSurface(backBuffer);
        return;
    }

    if (sharedResult ==
        VrSharedCaptureResult::Busy)
    {
        VR_ReleaseSurface(backBuffer);
        return;
    }

    // The original system-memory path remains the automatic fallback.
    const auto captureTimingStart =
        std::chrono::steady_clock::now();

    double readbackMilliseconds = 0.0;

    IDirect3DSurface9* transientResolveSurface =
        nullptr;

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

        const auto readbackStart =
            std::chrono::steady_clock::now();

        hr =
            device->GetRenderTargetData(
                captureSource,
                g_readbackSurface);

        const auto readbackEnd =
            std::chrono::steady_clock::now();

        readbackMilliseconds =
            std::chrono::duration<double, std::milli>(
                readbackEnd - readbackStart).count();

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

        if (static_cast<std::size_t>(
                lockedRect.Pitch) == rowBytes)
        {
            std::memcpy(
                destinationRow,
                sourceRow,
                imageBytes);
        }
        else
        {
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
        }

        g_readbackSurface->UnlockRect();
    }

    VR_ReleaseSurface(backBuffer);

    const std::uint64_t serial =
        ++g_captureSerialCounter;

    {
        std::lock_guard<std::mutex> captureLock(
            g_captureMutex);

        g_latestPixels.swap(
            g_captureScratchPixels);

        g_latestWidth =
            description.Width;

        g_latestHeight =
            description.Height;

        g_latestSerial = serial;
    }

    const auto captureTimingEnd =
        std::chrono::steady_clock::now();

    const double captureMilliseconds =
        std::chrono::duration<double, std::milli>(
            captureTimingEnd - captureTimingStart).count();

    static unsigned int captureTimingSampleCount = 0u;
    static double captureTimingTotal = 0.0;
    static double captureReadbackTotal = 0.0;
    static double captureTimingMaximum = 0.0;
    static double captureReadbackMaximum = 0.0;

    ++captureTimingSampleCount;
    captureTimingTotal += captureMilliseconds;
    captureReadbackTotal += readbackMilliseconds;

    if (captureMilliseconds > captureTimingMaximum)
    {
        captureTimingMaximum =
            captureMilliseconds;
    }

    if (readbackMilliseconds > captureReadbackMaximum)
    {
        captureReadbackMaximum =
            readbackMilliseconds;
    }

    if (captureTimingSampleCount >= 120u)
    {
        const double sampleScale =
            1.0 /
            static_cast<double>(
                captureTimingSampleCount);

        Com_Printf(
            0,
            "[VR] CPU fallback capture timing: "
            "readback %.2f ms, total %.2f ms, "
            "readback max %.2f ms, total max %.2f ms.\n",
            captureReadbackTotal * sampleScale,
            captureTimingTotal * sampleScale,
            captureReadbackMaximum,
            captureTimingMaximum);

        captureTimingSampleCount = 0u;
        captureTimingTotal = 0.0;
        captureReadbackTotal = 0.0;
        captureTimingMaximum = 0.0;
        captureReadbackMaximum = 0.0;
    }

    if (!g_loggedFirstCapture)
    {
        Com_Printf(
            0,
            "[VR] CPU fallback captured the first complete "
            "side-by-side frame: %u x %u.\n",
            description.Width,
            description.Height);

        g_loggedFirstCapture = true;
    }
}

bool VR_D3D9AcquireLatestSharedFrame(
    const std::uint64_t lastSerial,
    VrD3D9SharedFrame& frame)
{
    std::lock_guard<std::mutex> resourceLock(
        g_captureResourceMutex);

    if (!g_sharedBridgeActive.load(
            std::memory_order_acquire))
    {
        return false;
    }

    VR_PollSharedProducerSlots(true);

    std::uint32_t newestIndex =
        kVrD3D9SharedFrameSlotCount;
    std::uint64_t newestSerial =
        lastSerial;

    for (std::uint32_t index = 0u;
         index < kVrD3D9SharedFrameSlotCount;
         ++index)
    {
        const VrSharedCaptureSlot& slot =
            g_sharedSlots[index];

        if (slot.state ==
                VrSharedSlotState::Ready &&
            slot.serial > newestSerial)
        {
            newestSerial = slot.serial;
            newestIndex = index;
        }
    }

    for (std::uint32_t index = 0u;
         index < kVrD3D9SharedFrameSlotCount;
         ++index)
    {
        VrSharedCaptureSlot& slot =
            g_sharedSlots[index];

        if (slot.state ==
                VrSharedSlotState::Ready &&
            index != newestIndex)
        {
            slot.state =
                VrSharedSlotState::Free;
        }
    }

    if (newestIndex ==
        kVrD3D9SharedFrameSlotCount)
    {
        return false;
    }

    VrSharedCaptureSlot& newest =
        g_sharedSlots[newestIndex];

    newest.state =
        VrSharedSlotState::Acquired;

    frame.sharedHandle =
        newest.sharedHandle;
    frame.width = newest.width;
    frame.height = newest.height;
    frame.slotIndex = newestIndex;
    frame.generation = newest.generation;
    frame.serial = newest.serial;

    return true;
}

void VR_D3D9ReleaseSharedFrame(
    const std::uint32_t slotIndex,
    const std::uint64_t serial)
{
    if (slotIndex >=
        kVrD3D9SharedFrameSlotCount)
    {
        return;
    }

    std::lock_guard<std::mutex> resourceLock(
        g_captureResourceMutex);

    VrSharedCaptureSlot& slot =
        g_sharedSlots[slotIndex];

    if (slot.state ==
            VrSharedSlotState::Acquired &&
        slot.serial == serial)
    {
        slot.state =
            VrSharedSlotState::Free;
    }
}

bool VR_D3D9SharedBridgeActive()
{
    return
        g_sharedBridgeActive.load(
            std::memory_order_acquire);
}

void VR_D3D9DisableSharedBridge()
{
    std::lock_guard<std::mutex> resourceLock(
        g_captureResourceMutex);

    g_sharedBridgeUnavailable.store(
        true,
        std::memory_order_release);

    g_sharedBridgeActive.store(
        false,
        std::memory_order_release);

    if (!g_loggedSharedFallback)
    {
        Com_PrintWarning(
            0,
            "[VR] D3D11 rejected the GPU-shared "
            "capture path; using the CPU frame bridge.\n");

        g_loggedSharedFallback = true;
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

    pixels.swap(
        g_latestPixels);

    width = g_latestWidth;
    height = g_latestHeight;
    serial = g_latestSerial;

    return true;
}
