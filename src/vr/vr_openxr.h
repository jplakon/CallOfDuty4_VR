#pragma once

// Initializes the persistent OpenXR runtime connection.
//
// This currently creates an instance and selects the HMD, but it does not
// create a graphics session or submit images yet.
bool VR_Init();

// Polls OpenXR runtime events. Call once per game frame.
void VR_Frame();

// Releases all OpenXR resources.
void VR_Shutdown();

// Returns true while the OpenXR instance is active.
bool VR_IsInitialized();
