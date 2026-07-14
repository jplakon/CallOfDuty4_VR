#pragma once

#include <cstdint>
#include <vector>

struct IDirect3DDevice9;

// Enables or disables the same-frame side-by-side stereo diagnostic.
void VR_D3D9CaptureSetEnabled(bool enabled);

// Used by the renderer frontend/backend to gate the diagnostic path.
bool VR_D3D9IsSameFrameStereoEnabled();

// Called on the renderer thread immediately before Present(). One completed
// side-by-side D3D9 backbuffer is read back and split into left/right images.
void VR_D3D9CaptureFrame(IDirect3DDevice9* device);

// Copies a newly captured frame for one eye when its serial differs from
// lastSerial.
bool VR_D3D9CopyLatestEyeFrame(
    std::uint32_t eyeIndex,
    std::uint64_t lastSerial,
    std::vector<std::uint8_t>& pixels,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint64_t& serial);
