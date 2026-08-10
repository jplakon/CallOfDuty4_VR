#pragma once

#include "vr/vr_input_bindings.h"

#include <array>
#include <cstddef>

namespace kisak::vr::input
{

enum class OpenXrProfile
{
    KhronosGeneric,
    OculusTouch,
    MetaTouchPlus,
    MetaTouchPro,
    Pico4,
    PicoNeo3,
    ValveIndex,
    HtcVive,
    HtcViveCosmos,
    HtcViveFocus3,
    MicrosoftMotion,
    HpMixedReality,
    SamsungOdyssey,
    KhronosSimple,

    Count,
};

constexpr std::size_t kOpenXrProfileCount =
    static_cast<std::size_t>(OpenXrProfile::Count);

struct OpenXrProfileDefinition
{
    OpenXrProfile profile = OpenXrProfile::KhronosSimple;
    const char* path = "";
    const char* label = "";
    const char* requiredExtension = nullptr;
};

const std::array<OpenXrProfileDefinition, kOpenXrProfileCount>&
    OpenXrProfileDefinitions();

// Returns a component subpath such as /input/trigger/value. The caller adds
// /user/hand/left or /user/hand/right from the Source definition.
const char* ResolveOpenXrComponent(
    OpenXrProfile profile,
    Source source);

bool OpenXrProfileHasHaptics(OpenXrProfile profile);

// True when XR_EXT_palm_pose extends the interaction profile with
// /input/palm_ext/pose.
bool OpenXrProfileHasPalmPose(OpenXrProfile profile);

} // namespace kisak::vr::input
