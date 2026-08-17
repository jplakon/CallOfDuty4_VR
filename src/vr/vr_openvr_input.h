#pragma once

#include "vr/vr_input_bindings.h"

#include <openvr.h>

#include <array>
#include <cstdint>
#include <string>

namespace kisak::vr::input
{

namespace openvr = ::vr;

struct OpenVrVector2
{
    float x = 0.0f;
    float y = 0.0f;
};

// SteamVR's legacy Oculus controller state does not expose the capacitive
// thumbrest as a separate component. It reports joystick contact instead.
// Guard the default thumbrest + off-hand-direction mission chords so normal
// walking and turning cannot accidentally invoke them.
struct OpenVrMissionSelectorState
{
    bool touchWasHeld = false;
    bool armed = false;
};

struct OpenVrMissionSelectorUpdate
{
    bool available = false;
    bool modifierHeld = false;
    bool armedThisFrame = false;
    bool cancelledThisFrame = false;
};

// A controller snapshot from OpenVR's legacy controller API. SteamVR still
// exposes this API for architecture-matched fallback clients when a 32-bit
// OpenXR runtime is unavailable. The mapper and game use the same resolver so
// a captured source has identical meaning in both processes.
struct OpenVrHandState
{
    Hand hand = Hand::None;
    openvr::TrackedDeviceIndex_t deviceIndex =
        openvr::k_unTrackedDeviceIndexInvalid;
    bool connected = false;
    bool stateValid = false;
    bool supportedButtonsKnown = false;
    std::uint64_t supportedButtons = 0u;
    openvr::VRControllerState_t controllerState = {};
    std::array<
        openvr::EVRControllerAxisType,
        openvr::k_unControllerStateAxisCount> axisTypes = {};
    std::string controllerType;
    std::string inputProfilePath;
};

// SteamVR's legacy Index emulation has a controller-specific layout: A uses
// the historical grip button id while the physical squeeze is exposed as the
// second trigger-style axis (and may be digital-only). Keep that identity
// check shared by the game and the press-to-bind mapper.
bool IsOpenVrIndexController(const OpenVrHandState& state);

bool ReadOpenVrHandState(
    openvr::IVRSystem* system,
    Hand hand,
    OpenVrHandState* state);

bool RefreshOpenVrHandState(
    openvr::IVRSystem* system,
    Hand hand,
    OpenVrHandState* state);

bool GetOpenVrBooleanSourceState(
    const std::array<OpenVrHandState, 2>& hands,
    Source source,
    bool* active);

OpenVrVector2 GetOpenVrVector2SourceState(
    const std::array<OpenVrHandState, 2>& hands,
    Source source,
    bool* active);

bool UsesOpenVrMissionSelector(const Binding& binding);

OpenVrMissionSelectorUpdate UpdateOpenVrMissionSelector(
    OpenVrMissionSelectorState* state,
    bool touchAvailable,
    bool touchHeld,
    OpenVrVector2 leftPrimaryAxis,
    bool leftPrimaryAxisActive,
    OpenVrVector2 rightPrimaryAxis,
    bool rightPrimaryAxisActive,
    float neutralThreshold = 0.20f);

std::string OpenVrHandDescription(const OpenVrHandState& state);

} // namespace kisak::vr::input
