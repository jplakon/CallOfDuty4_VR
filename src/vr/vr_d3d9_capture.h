#pragma once

#include <cstdint>
#include <vector>

struct IDirect3DDevice9;

// Enables or disables the low-rate D3D9 diagnostic readback.
void VR_D3D9CaptureSetEnabled(bool enabled);

// Called on the renderer thread immediately before Present().
void VR_D3D9CaptureFrame(IDirect3DDevice9* device);

// Copies a newly captured frame when its serial differs from lastSerial.
bool VR_D3D9CopyLatestFrame(
    std::uint64_t lastSerial,
    std::vector<std::uint8_t>& pixels,
    std::uint32_t& width,
    std::uint32_t& height,
    std::uint64_t& serial);
