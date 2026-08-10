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

std::string OpenVrHandDescription(const OpenVrHandState& state);

} // namespace kisak::vr::input
