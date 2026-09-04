#ifndef KISAK_DEDICATED
#error This file is for dedicated-server builds only
#endif

#include "vr_d3d9_capture.h"
#include "vr_openxr.h"

bool VR_IsInitialized()
{
    return false;
}

bool VR_ApplyHeadPosition(float[3], const float[3][3], float)
{
    return false;
}

bool VR_ApplyHeadOrientation(float[3][3])
{
    return false;
}

bool VR_ApplyStereoEyeOffsetForEye(float[3], const float[3][3], unsigned int)
{
    return false;
}

void VR_BeginStereoEyeRender(unsigned int)
{
}

void VR_EndStereoEyeRender()
{
}

bool VR_GetStereoEyeFovBounds(unsigned int, float*, float*)
{
    return false;
}

bool VR_GetCurrentRenderEyeProjection(float*, float*, float*, float*)
{
    return false;
}

bool VR_ApplyRightControllerToWeaponPlacement(
    int,
    const char*,
    const char*,
    float,
    const float[3],
    const float[3][3],
    float[3],
    float[3][3])
{
    return false;
}

bool VR_GetRightControllerWeaponGripWorld(
    const float[3],
    const float[3][3],
    float[3])
{
    return false;
}

void VR_ReportRightControllerWeaponGripAlignment(
    const char*,
    const float[3],
    const float[3])
{
}

bool VR_GetRightControllerWeaponCommand(float*, float*, bool*)
{
    return false;
}

bool VR_ApplyRightControllerWeaponHaptic(float, float)
{
    return false;
}

void VR_PublishRightControllerWeaponMuzzleWorld(const float[3])
{
}

bool VR_GetRightControllerWeaponMuzzleWorld(float[3])
{
    return false;
}

void VR_SetRightControllerWeaponMuzzleBlocked(bool)
{
}

bool VR_D3D9IsSameFrameStereoEnabled()
{
    return false;
}

void VR_D3D9CaptureFrame(IDirect3DDevice9*, std::uint64_t)
{
}
