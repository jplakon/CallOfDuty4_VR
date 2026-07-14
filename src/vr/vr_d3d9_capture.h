#pragma once

#include <cstdint>
#include <vector>

struct IDirect3DDevice9;

// Enables or disables the reset-safe, full-rate stereo readback.
void VR_D3D9CaptureSetEnabled(bool enabled);

// Used by the renderer frontend/backend to gate same-frame stereo.
bool VR_D3D9IsSameFrameStereoEnabled();

// Called immediately before Present(). Captures one complete side-by-side
// D3D9 backbuffer while remaining safe across lost-device transitions.
void VR_D3D9CaptureFrame(IDirect3DDevice9* device);

// Copies a newly captured complete side-by-side frame when its serial differs
// from lastSerial.
bool VR_D3D9CopyLatestStereoFrame(
    std::uint64_t lastSerial,
    std::vector<std::uint8_t>& pixels,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint64_t& serial);
