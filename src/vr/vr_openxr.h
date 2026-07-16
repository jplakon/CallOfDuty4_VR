#pragma once

// Initializes OpenXR, creates a dedicated D3D11 device, creates an OpenXR
// session, and prepares one color swapchain for each eye.
//
// The normal Call of Duty Direct3D 9 renderer remains untouched.
bool VR_Init();

// Polls OpenXR events and, while the session is running, renders a stereo, head-tracked cube and floor grid
// to the headset. Call once per game frame.
void VR_Frame();

// Releases swapchains, session, D3D11 resources, and the OpenXR instance.
void VR_Shutdown();

// Returns true after OpenXR and its D3D11 session have initialized.
bool VR_IsInitialized();
// Applies the latest recentered OpenXR headset orientation to an
// existing CoD camera axis. Returns false until a valid pose exists.
bool VR_ApplyHeadPosition(
    float viewOrigin[3],
    const float viewAxis[3][3]);
bool VR_ApplyHeadOrientation(float viewAxis[3][3]);
bool VR_ApplyStereoEyeOffsetForEye(
    float viewOrigin[3],
    const float viewAxis[3][3],
    unsigned int eyeIndex);

// Publishes which eye the following CL_RenderScene call is building.
// The renderer uses this only while constructing that eye's camera view.
void VR_BeginStereoEyeRender(unsigned int eyeIndex);
void VR_EndStereoEyeRender();

// Returns a conservative symmetric FOV bound for legacy CoD systems such as
// LOD selection. The actual render projection remains asymmetric.
bool VR_GetStereoEyeFovBounds(
    unsigned int eyeIndex,
    float* tanHalfFovX,
    float* tanHalfFovY);

// Returns the active eye's exact OpenXR frustum tangents. Values use the
// OpenXR convention: left/down negative, right/up positive.
bool VR_GetCurrentRenderEyeProjection(
    float* tanLeft,
    float* tanRight,
    float* tanDown,
    float* tanUp);

// Applies the tracked right-controller motion delta to the already-built
// first-person weapon placement. The first valid viewmodel render becomes
// the neutral calibration pose, so installing this does not make the gun
// jump when tracking first becomes available.
bool VR_ApplyRightControllerToWeaponPlacement(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float weaponOrigin[3],
    float weaponAxis[3][3]);

// Returns the final rendered right-hand weapon direction as CoD pitch/yaw
// and the current right index-trigger state. The direction is taken from
// the transformed viewmodel axis, keeping shots aligned with the visible gun.
bool VR_GetRightControllerWeaponCommand(
    float* gunPitch,
    float* gunYaw,
    bool* attackPressed);


// Applies a short vibration pulse to the right-hand OpenXR controller.
// Intended for confirmed local firearm events.
bool VR_ApplyRightControllerWeaponHaptic(
    float amplitude,
    float durationSeconds);


// Publishes and retrieves the latest transformed viewmodel tag_flash world
// position. The client render path publishes it every frame; the local
// listen-server weapon path consumes it for authoritative bullet origins.
void VR_PublishRightControllerWeaponMuzzleWorld(
    const float muzzleOrigin[3]);

bool VR_GetRightControllerWeaponMuzzleWorld(
    float muzzleOrigin[3]);


// Records whether geometry lies between the player's view origin and the
// tracked physical muzzle. The local listen-server bullet path uses this to
// hard-suppress the shot instead of allowing normal wall penetration.
void VR_SetRightControllerWeaponMuzzleBlocked(
    bool blocked);

bool VR_ShouldSuppressRightControllerBlockedMuzzleShot();
