#include "vr/vr_openvr_input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace kisak::vr::input
{
namespace
{

constexpr float kAnalogButtonThreshold = 0.55f;

std::size_t HandIndex(const Hand hand)
{
    return hand == Hand::Right ? 1u : 0u;
}

const OpenVrHandState* HandForSource(
    const std::array<OpenVrHandState, 2>& hands,
    const Source source)
{
    const Hand hand = GetSourceDefinition(source).hand;
    if (hand == Hand::None)
    {
        return nullptr;
    }

    const OpenVrHandState& state = hands[HandIndex(hand)];
    return state.stateValid ? &state : nullptr;
}

bool ContainsIgnoreCase(
    const std::string& value,
    const char* const fragment)
{
    if (fragment == nullptr)
    {
        return false;
    }

    std::string lowerValue(value);
    std::transform(
        lowerValue.begin(),
        lowerValue.end(),
        lowerValue.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

    std::string lowerFragment(fragment);
    std::transform(
        lowerFragment.begin(),
        lowerFragment.end(),
        lowerFragment.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

    return lowerValue.find(lowerFragment) != std::string::npos;
}

bool IsIndexController(const OpenVrHandState& state)
{
    return ContainsIgnoreCase(state.controllerType, "knuckles") ||
        ContainsIgnoreCase(state.controllerType, "index");
}

int FindAxis(
    const OpenVrHandState& state,
    const openvr::EVRControllerAxisType type,
    const unsigned int ordinal = 0u)
{
    unsigned int found = 0u;
    for (std::size_t index = 0u;
         index < state.axisTypes.size();
         ++index)
    {
        if (state.axisTypes[index] != type)
        {
            continue;
        }

        if (found == ordinal)
        {
            return static_cast<int>(index);
        }
        ++found;
    }

    return -1;
}

std::uint64_t AxisButtonMask(const int axisIndex)
{
    if (axisIndex < 0 ||
        axisIndex >=
            static_cast<int>(openvr::k_unControllerStateAxisCount))
    {
        return 0u;
    }

    return openvr::ButtonMaskFromId(
        static_cast<openvr::EVRButtonId>(
            static_cast<int>(openvr::k_EButton_Axis0) + axisIndex));
}

bool ButtonSupported(
    const OpenVrHandState& state,
    const std::uint64_t mask)
{
    return mask != 0u &&
        (!state.supportedButtonsKnown ||
         (state.supportedButtons & mask) != 0u);
}

bool ButtonPressed(
    const OpenVrHandState& state,
    const std::uint64_t mask)
{
    return ButtonSupported(state, mask) &&
        (state.controllerState.ulButtonPressed & mask) != 0u;
}

bool ButtonTouched(
    const OpenVrHandState& state,
    const std::uint64_t mask)
{
    return ButtonSupported(state, mask) &&
        (state.controllerState.ulButtonTouched & mask) != 0u;
}

std::string ReadStringProperty(
    openvr::IVRSystem* const system,
    const openvr::TrackedDeviceIndex_t deviceIndex,
    const openvr::ETrackedDeviceProperty property)
{
    std::array<char, openvr::k_unMaxPropertyStringSize> value = {};
    openvr::ETrackedPropertyError error = openvr::TrackedProp_Success;

    system->GetStringTrackedDeviceProperty(
        deviceIndex,
        property,
        value.data(),
        static_cast<std::uint32_t>(value.size()),
        &error);

    return error == openvr::TrackedProp_Success
        ? std::string(value.data())
        : std::string();
}

} // namespace

bool ReadOpenVrHandState(
    openvr::IVRSystem* const system,
    const Hand hand,
    OpenVrHandState* const state)
{
    if (system == nullptr || state == nullptr || hand == Hand::None)
    {
        return false;
    }

    *state = OpenVrHandState{};
    state->hand = hand;
    state->axisTypes.fill(openvr::k_eControllerAxis_None);

    const openvr::ETrackedControllerRole role = hand == Hand::Left
        ? openvr::TrackedControllerRole_LeftHand
        : openvr::TrackedControllerRole_RightHand;

    state->deviceIndex =
        system->GetTrackedDeviceIndexForControllerRole(role);

    if (state->deviceIndex == openvr::k_unTrackedDeviceIndexInvalid ||
        !system->IsTrackedDeviceConnected(state->deviceIndex))
    {
        return false;
    }

    state->connected = true;
    state->stateValid = system->GetControllerState(
        state->deviceIndex,
        &state->controllerState,
        sizeof(state->controllerState));

    for (std::size_t axisIndex = 0u;
         axisIndex < state->axisTypes.size();
         ++axisIndex)
    {
        openvr::ETrackedPropertyError propertyError =
            openvr::TrackedProp_Success;

        const auto property =
            static_cast<openvr::ETrackedDeviceProperty>(
                static_cast<int>(openvr::Prop_Axis0Type_Int32) +
                static_cast<int>(axisIndex));

        const std::int32_t axisType =
            system->GetInt32TrackedDeviceProperty(
                state->deviceIndex,
                property,
                &propertyError);

        if (propertyError == openvr::TrackedProp_Success &&
            axisType >= openvr::k_eControllerAxis_None &&
            axisType <= openvr::k_eControllerAxis_Trigger)
        {
            state->axisTypes[axisIndex] =
                static_cast<openvr::EVRControllerAxisType>(axisType);
        }
    }

    openvr::ETrackedPropertyError buttonsError =
        openvr::TrackedProp_Success;
    state->supportedButtons =
        system->GetUint64TrackedDeviceProperty(
            state->deviceIndex,
            openvr::Prop_SupportedButtons_Uint64,
            &buttonsError);
    state->supportedButtonsKnown =
        buttonsError == openvr::TrackedProp_Success &&
        state->supportedButtons != 0u;

    state->controllerType = ReadStringProperty(
        system,
        state->deviceIndex,
        openvr::Prop_ControllerType_String);
    state->inputProfilePath = ReadStringProperty(
        system,
        state->deviceIndex,
        openvr::Prop_InputProfilePath_String);

    return state->stateValid;
}

bool RefreshOpenVrHandState(
    openvr::IVRSystem* const system,
    const Hand hand,
    OpenVrHandState* const state)
{
    if (system == nullptr || state == nullptr || hand == Hand::None)
    {
        return false;
    }

    const openvr::ETrackedControllerRole role = hand == Hand::Left
        ? openvr::TrackedControllerRole_LeftHand
        : openvr::TrackedControllerRole_RightHand;
    const openvr::TrackedDeviceIndex_t deviceIndex =
        system->GetTrackedDeviceIndexForControllerRole(role);

    if (deviceIndex == openvr::k_unTrackedDeviceIndexInvalid ||
        !system->IsTrackedDeviceConnected(deviceIndex))
    {
        *state = OpenVrHandState{};
        state->hand = hand;
        return false;
    }

    if (!state->connected || state->deviceIndex != deviceIndex)
    {
        return ReadOpenVrHandState(system, hand, state);
    }

    state->stateValid = system->GetControllerState(
        deviceIndex,
        &state->controllerState,
        sizeof(state->controllerState));

    return state->stateValid;
}

bool GetOpenVrBooleanSourceState(
    const std::array<OpenVrHandState, 2>& hands,
    const Source source,
    bool* const active)
{
    if (active == nullptr)
    {
        return false;
    }

    *active = false;

    const OpenVrHandState* const state =
        HandForSource(hands, source);
    if (state == nullptr)
    {
        return false;
    }

    const std::uint64_t applicationMenu =
        openvr::ButtonMaskFromId(openvr::k_EButton_ApplicationMenu);
    const std::uint64_t grip =
        openvr::ButtonMaskFromId(openvr::k_EButton_Grip);
    const std::uint64_t facePrimary =
        openvr::ButtonMaskFromId(openvr::k_EButton_A);
    const std::uint64_t auxiliary =
        openvr::ButtonMaskFromId(openvr::k_EButton_DPad_Up);

    const int joystickAxis =
        FindAxis(*state, openvr::k_eControllerAxis_Joystick);
    const int trackpadAxis =
        FindAxis(*state, openvr::k_eControllerAxis_TrackPad);
    const int triggerAxis =
        FindAxis(*state, openvr::k_eControllerAxis_Trigger);
    const int squeezeAxis =
        FindAxis(*state, openvr::k_eControllerAxis_Trigger, 1u);

    const std::uint64_t joystick = AxisButtonMask(joystickAxis);
    const std::uint64_t trackpad = AxisButtonMask(trackpadAxis);
    const std::uint64_t trigger = AxisButtonMask(triggerAxis);
    const bool trackpadOnlyController =
        trackpadAxis >= 0 && joystickAxis < 0;
    const bool mixedRealityStyleController =
        trackpadAxis >= 0 &&
        joystickAxis >= 0 &&
        !IsIndexController(*state) &&
        (!state->supportedButtonsKnown ||
         !ButtonSupported(*state, facePrimary));

    switch (source)
    {
        case Source::LeftPrimary:
        case Source::RightPrimary:
        {
            const std::uint64_t mask =
                IsIndexController(*state)
                    ? grip
                    : trackpadOnlyController ||
                              mixedRealityStyleController
                        ? trackpad
                        : facePrimary;
            *active = ButtonSupported(*state, mask);
            return ButtonPressed(*state, mask);
        }

        case Source::LeftSecondary:
        case Source::RightSecondary:
        {
            const std::uint64_t mask = mixedRealityStyleController
                ? joystick
                : applicationMenu;
            *active = ButtonSupported(*state, mask);
            return ButtonPressed(*state, mask);
        }

        case Source::LeftMenu:
        case Source::RightMenu:
            // SteamVR reserves the system button. Legacy drivers consistently
            // expose their application/menu control through this bit; on
            // controllers that alias it to a face button the conflict is
            // surfaced by the configurator instead of silently dropping it.
            *active = ButtonSupported(*state, applicationMenu);
            return ButtonPressed(*state, applicationMenu);

        case Source::LeftAuxiliary:
        case Source::RightAuxiliary:
            *active = ButtonSupported(*state, auxiliary);
            return ButtonPressed(*state, auxiliary);

        case Source::LeftTrigger:
        case Source::RightTrigger:
            *active = triggerAxis >= 0;
            return triggerAxis >= 0 &&
                (state->controllerState.rAxis[triggerAxis].x >=
                     kAnalogButtonThreshold ||
                 ButtonPressed(*state, trigger));

        case Source::LeftSqueeze:
        case Source::RightSqueeze:
            if (squeezeAxis >= 0)
            {
                *active = true;
                return state->controllerState.rAxis[squeezeAxis].x >=
                    kAnalogButtonThreshold;
            }
            *active = ButtonSupported(*state, grip);
            return ButtonPressed(*state, grip);

        case Source::LeftThumbstickClick:
        case Source::RightThumbstickClick:
            *active = joystickAxis >= 0 &&
                ButtonSupported(*state, joystick);
            return ButtonPressed(*state, joystick);

        case Source::LeftTrackpadClick:
        case Source::RightTrackpadClick:
            *active = trackpadAxis >= 0 &&
                ButtonSupported(*state, trackpad);
            return ButtonPressed(*state, trackpad);

        case Source::LeftThumbrestTouch:
        case Source::RightThumbrestTouch:
            // The legacy state has no dedicated thumbrest component. Modern
            // drivers publish thumb contact through the joystick touch bit.
            *active = joystickAxis >= 0 &&
                ButtonSupported(*state, joystick);
            return ButtonTouched(*state, joystick);

        case Source::LeftTrackpadTouch:
        case Source::RightTrackpadTouch:
            *active = trackpadAxis >= 0 &&
                ButtonSupported(*state, trackpad);
            return ButtonTouched(*state, trackpad);

        case Source::LeftPrimaryAxisUp:
        case Source::LeftPrimaryAxisDown:
        case Source::LeftPrimaryAxisLeft:
        case Source::LeftPrimaryAxisRight:
        case Source::RightPrimaryAxisUp:
        case Source::RightPrimaryAxisDown:
        case Source::RightPrimaryAxisLeft:
        case Source::RightPrimaryAxisRight:
        {
            bool vectorActive = false;
            const OpenVrVector2 value =
                GetOpenVrVector2SourceState(
                    hands,
                    PhysicalSource(source),
                    &vectorActive);
            *active = vectorActive;
            return vectorActive &&
                DirectionalSourcePressed(
                    source,
                    value.x,
                    value.y);
        }

        case Source::Unbound:
        case Source::LeftPrimaryAxis:
        case Source::RightPrimaryAxis:
        case Source::LeftThumbstick:
        case Source::LeftTrackpad:
        case Source::RightThumbstick:
        case Source::RightTrackpad:
        case Source::Count:
            return false;
    }

    return false;
}

OpenVrVector2 GetOpenVrVector2SourceState(
    const std::array<OpenVrHandState, 2>& hands,
    const Source source,
    bool* const active)
{
    OpenVrVector2 value;
    if (active == nullptr)
    {
        return value;
    }

    *active = false;

    const OpenVrHandState* const state =
        HandForSource(hands, source);
    if (state == nullptr)
    {
        return value;
    }

    openvr::EVRControllerAxisType axisType =
        openvr::k_eControllerAxis_None;

    switch (source)
    {
        case Source::LeftPrimaryAxis:
        case Source::RightPrimaryAxis:
        {
            int axisIndex =
                FindAxis(*state, openvr::k_eControllerAxis_Joystick);
            if (axisIndex < 0)
            {
                axisIndex =
                    FindAxis(*state, openvr::k_eControllerAxis_TrackPad);
            }

            if (axisIndex < 0)
            {
                return value;
            }

            *active = true;
            value.x = state->controllerState.rAxis[axisIndex].x;
            value.y = state->controllerState.rAxis[axisIndex].y;
            break;
        }
        case Source::LeftThumbstick:
        case Source::RightThumbstick:
            axisType = openvr::k_eControllerAxis_Joystick;
            break;
        case Source::LeftTrackpad:
        case Source::RightTrackpad:
            axisType = openvr::k_eControllerAxis_TrackPad;
            break;
        default:
            return value;
    }

    if (!*active)
    {
        const int axisIndex = FindAxis(*state, axisType);
        if (axisIndex < 0)
        {
            return value;
        }

        *active = true;
        value.x = state->controllerState.rAxis[axisIndex].x;
        value.y = state->controllerState.rAxis[axisIndex].y;
    }

    if (!std::isfinite(value.x) || !std::isfinite(value.y))
    {
        *active = false;
        value = {};
    }

    return value;
}

std::string OpenVrHandDescription(const OpenVrHandState& state)
{
    const std::string hand = state.hand == Hand::Left
        ? "left"
        : state.hand == Hand::Right
            ? "right"
            : "unknown";

    std::string description = hand + " ";
    description += state.controllerType.empty()
        ? "SteamVR controller"
        : state.controllerType;

    if (!state.inputProfilePath.empty())
    {
        description += " [" + state.inputProfilePath + "]";
    }

    return description;
}

} // namespace kisak::vr::input
