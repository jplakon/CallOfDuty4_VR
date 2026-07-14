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
