#pragma once

#include <cstdint>
#include <vector>

struct IDirect3DDevice9;

// Enables or disables the alternating-eye D3D9 diagnostic readback.
void VR_D3D9CaptureSetEnabled(bool enabled);

// Returns the eye currently selected for the next desktop render.
// 0 = left eye, 1 = right eye.
std::uint32_t VR_D3D9GetRenderEye();

// Called on the renderer thread immediately before Present(). The function
// tags the completed backbuffer with the eye that CG_CalcViewValues rendered,
// then switches the requested eye for the next desktop frame.
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
