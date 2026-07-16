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

// Makes the latest physical headset center the zero-translation pose.
// Orientation is not changed. Returns false until a valid head pose exists.
bool VR_RecenterHeadPosition();
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

// Applies the tracked right-controller pose to the already-built
// first-person weapon placement. The viewmodel attachment is calibrated
// against a canonical controller basis, so startup controller orientation
// cannot become a permanent neutral-pose offset.
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

// Returns left-thumbstick movement rotated by the horizontal HMD yaw.
// Values are normalized to [-1, 1] after a circular remapped deadzone.
// Forward and right are expressed in the current CoD usercmd/body basis.
bool VR_GetHmdOrientedMovement(
    float* forward,
    float* right);

// Consumes one latched 45-degree right-stick snap turn.
// Positive CoD yaw turns left, so right-stick right returns -45 degrees.
bool VR_ConsumeSnapTurn(
    float* yawDeltaDegrees);

// Returns the first Touch gameplay-button set:
// left trigger = ADS, right A = jump, left X = use/reload.
bool VR_GetBasicGameplayButtons(
    bool* adsHeld,
    bool* jumpHeld,
    bool* useReloadHeld);


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
