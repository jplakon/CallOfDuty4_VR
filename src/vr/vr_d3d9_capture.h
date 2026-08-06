#pragma once

#include <cstdint>
#include <vector>

struct IDirect3DDevice9;

// KISAK_SP_VR_GPU_SHARED_BRIDGE_V1
// KISAK_SP_VR_SEQUENTIAL_CAPTURE_QUEUE_V33
// Four slots leave room for the displayed frame, its D3D11 retirement fence,
// one queued producer frame, and the frame currently being produced.
constexpr std::uint32_t
    kVrD3D9SharedFrameSlotCount = 4u;

// KISAK_SP_VR_CAPTURE_POSE_METADATA_V32
// Plain integer metadata can cross the D3D9 render thread / D3D11 OpenXR
// consumer boundary without introducing OpenXR types into the legacy renderer.
// renderFrameId identifies the exact GfxBackEndData that produced the pixels.
struct VrD3D9FrameMetadata
{
    std::uint64_t renderFrameId = 0u;
    std::uint64_t producerSequence = 0u;
    std::uint64_t captureSubmittedNanoseconds = 0u;
    std::uint64_t producerReadyNanoseconds = 0u;
};

struct VrD3D9SharedFrame
{
    void* sharedHandle = nullptr;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t slotIndex = 0u;
    std::uint64_t generation = 0u;
    std::uint64_t serial = 0u;
    VrD3D9FrameMetadata metadata = {};
};

// Enables or disables the reset-safe, full-rate stereo capture.
void VR_D3D9CaptureSetEnabled(bool enabled);

// Used by the renderer frontend/backend to gate same-frame stereo.
bool VR_D3D9IsSameFrameStereoEnabled();

// Called immediately before Present(). Captures one complete side-by-side
// D3D9 backbuffer while remaining safe across lost-device transitions.
void VR_D3D9CaptureFrame(
    IDirect3DDevice9* device,
    std::uint64_t renderFrameId);

// Acquires the next undisplayed producer-complete D3D9Ex shared frame. Keeping
// completed frames in serial order avoids alternating reuse/drop cadence when
// the 72 Hz producer and OpenXR consumer cross phases. The caller must release
// the matching slot/serial only after D3D11 has finished sampling it.
bool VR_D3D9AcquireLatestSharedFrame(
    std::uint64_t lastSerial,
    VrD3D9SharedFrame& frame);

void VR_D3D9ReleaseSharedFrame(
    std::uint32_t slotIndex,
    std::uint64_t serial);

bool VR_D3D9SharedBridgeActive();

// Permanently selects the CPU fallback for this capture session after the
// D3D11 consumer rejects or cannot fence a shared resource.
void VR_D3D9DisableSharedBridge();

// CPU fallback: copies a newly captured complete side-by-side frame when its
// serial differs from lastSerial.
bool VR_D3D9CopyLatestStereoFrame(
    std::uint64_t lastSerial,
    std::vector<std::uint8_t>& pixels,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint64_t& serial,
    VrD3D9FrameMetadata& metadata);
