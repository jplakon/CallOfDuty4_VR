#include "vr/vr_openxr_profiles.h"

namespace kisak::vr::input
{
namespace
{

constexpr std::array<OpenXrProfileDefinition, kOpenXrProfileCount>
    kProfiles = {{
        {OpenXrProfile::KhronosGeneric, "/interaction_profiles/khr/generic_controller", "Khronos Generic Controller", "XR_KHR_generic_controller"},
        {OpenXrProfile::OculusTouch, "/interaction_profiles/oculus/touch_controller", "Oculus / Meta Touch", nullptr},
        {OpenXrProfile::MetaTouchPlus, "/interaction_profiles/meta/touch_controller_plus", "Meta Quest Touch Plus", "XR_META_touch_controller_plus"},
        {OpenXrProfile::MetaTouchPro, "/interaction_profiles/facebook/touch_controller_pro", "Meta Quest Touch Pro", "XR_FB_touch_controller_pro"},
        {OpenXrProfile::Pico4, "/interaction_profiles/bytedance/pico4_controller", "PICO 4 Controller", "XR_BD_controller_interaction"},
        {OpenXrProfile::PicoNeo3, "/interaction_profiles/bytedance/pico_neo3_controller", "PICO Neo3 Controller", "XR_BD_controller_interaction"},
        {OpenXrProfile::ValveIndex, "/interaction_profiles/valve/index_controller", "Valve Index Controller", nullptr},
        {OpenXrProfile::HtcVive, "/interaction_profiles/htc/vive_controller", "HTC Vive Controller", nullptr},
        {OpenXrProfile::HtcViveCosmos, "/interaction_profiles/htc/vive_cosmos_controller", "HTC Vive Cosmos Controller", "XR_HTC_vive_cosmos_controller_interaction"},
        {OpenXrProfile::HtcViveFocus3, "/interaction_profiles/htc/vive_focus3_controller", "HTC Vive Focus 3 Controller", "XR_HTC_vive_focus3_controller_interaction"},
        {OpenXrProfile::MicrosoftMotion, "/interaction_profiles/microsoft/motion_controller", "Microsoft Mixed Reality Controller", nullptr},
        {OpenXrProfile::HpMixedReality, "/interaction_profiles/hp/mixed_reality_controller", "HP Mixed Reality Controller", "XR_EXT_hp_mixed_reality_controller"},
        {OpenXrProfile::SamsungOdyssey, "/interaction_profiles/samsung/odyssey_controller", "Samsung Odyssey Controller", "XR_EXT_samsung_odyssey_controller"},
        {OpenXrProfile::KhronosSimple, "/interaction_profiles/khr/simple_controller", "Khronos Simple Controller", nullptr},
    }};

enum class Component
{
    Primary,
    Secondary,
    Menu,
    Auxiliary,
    Trigger,
    Squeeze,
    ThumbstickClick,
    TrackpadClick,
    ThumbrestTouch,
    TrackpadTouch,
    PrimaryAxis,
    Thumbstick,
    Trackpad,
    None,
};

Component ComponentForSource(const Source source)
{
    switch (PhysicalSource(source))
    {
        case Source::LeftPrimary:
        case Source::RightPrimary:
            return Component::Primary;
        case Source::LeftSecondary:
        case Source::RightSecondary:
            return Component::Secondary;
        case Source::LeftMenu:
        case Source::RightMenu:
            return Component::Menu;
        case Source::LeftAuxiliary:
        case Source::RightAuxiliary:
            return Component::Auxiliary;
        case Source::LeftTrigger:
        case Source::RightTrigger:
            return Component::Trigger;
        case Source::LeftSqueeze:
        case Source::RightSqueeze:
            return Component::Squeeze;
        case Source::LeftThumbstickClick:
        case Source::RightThumbstickClick:
            return Component::ThumbstickClick;
        case Source::LeftTrackpadClick:
        case Source::RightTrackpadClick:
            return Component::TrackpadClick;
        case Source::LeftThumbrestTouch:
        case Source::RightThumbrestTouch:
            return Component::ThumbrestTouch;
        case Source::LeftTrackpadTouch:
        case Source::RightTrackpadTouch:
            return Component::TrackpadTouch;
        case Source::LeftPrimaryAxis:
        case Source::RightPrimaryAxis:
        case Source::LeftPrimaryAxisUp:
        case Source::LeftPrimaryAxisDown:
        case Source::LeftPrimaryAxisLeft:
        case Source::LeftPrimaryAxisRight:
        case Source::RightPrimaryAxisUp:
        case Source::RightPrimaryAxisDown:
        case Source::RightPrimaryAxisLeft:
        case Source::RightPrimaryAxisRight:
            return Component::PrimaryAxis;
        case Source::LeftThumbstick:
        case Source::RightThumbstick:
            return Component::Thumbstick;
        case Source::LeftTrackpad:
        case Source::RightTrackpad:
            return Component::Trackpad;
        case Source::Unbound:
        case Source::Count:
            return Component::None;
    }

    return Component::None;
}

bool IsLeft(const Source source)
{
    return GetSourceDefinition(source).hand == Hand::Left;
}

const char* ResolveTouchFamily(
    const OpenXrProfile profile,
    const Source source,
    const Component component)
{
    switch (component)
    {
        case Component::Primary:
            return IsLeft(source) ? "/input/x/click" : "/input/a/click";
        case Component::Secondary:
            return IsLeft(source) ? "/input/y/click" : "/input/b/click";
        case Component::Menu:
            if (IsLeft(source) ||
                profile == OpenXrProfile::HpMixedReality ||
                profile == OpenXrProfile::PicoNeo3)
            {
                return "/input/menu/click";
            }
            return nullptr;
        case Component::Auxiliary:
            return profile == OpenXrProfile::HtcViveCosmos
                ? "/input/shoulder/click"
                : nullptr;
        case Component::Trigger:
            return "/input/trigger/value";
        case Component::Squeeze:
            return profile == OpenXrProfile::HtcViveCosmos
                ? "/input/squeeze/click"
                : "/input/squeeze/value";
        case Component::ThumbstickClick:
            return "/input/thumbstick/click";
        case Component::ThumbrestTouch:
            if (profile == OpenXrProfile::OculusTouch ||
                profile == OpenXrProfile::MetaTouchPlus ||
                profile == OpenXrProfile::MetaTouchPro ||
                profile == OpenXrProfile::HtcViveFocus3)
            {
                return "/input/thumbrest/touch";
            }
            return nullptr;
        case Component::Thumbstick:
        case Component::PrimaryAxis:
            return "/input/thumbstick";
        case Component::TrackpadClick:
        case Component::TrackpadTouch:
        case Component::Trackpad:
        case Component::None:
            return nullptr;
    }

    return nullptr;
}

const char* ResolveGeneric(const Component component)
{
    switch (component)
    {
        case Component::Primary:
            return "/input/primary/click";
        case Component::Secondary:
            return "/input/secondary/click";
        case Component::Trigger:
            return "/input/trigger/value";
        case Component::Squeeze:
            return "/input/squeeze/value";
        case Component::ThumbstickClick:
            return "/input/thumbstick/click";
        case Component::Thumbstick:
        case Component::PrimaryAxis:
            return "/input/thumbstick";
        default:
            return nullptr;
    }
}

const char* ResolveIndex(const Component component)
{
    switch (component)
    {
        case Component::Primary:
            return "/input/a/click";
        case Component::Secondary:
            return "/input/b/click";
        case Component::Trigger:
            return "/input/trigger/value";
        case Component::Squeeze:
            return "/input/squeeze/value";
        case Component::ThumbstickClick:
            return "/input/thumbstick/click";
        case Component::TrackpadClick:
            return "/input/trackpad/force";
        case Component::TrackpadTouch:
            return "/input/trackpad/touch";
        case Component::Thumbstick:
        case Component::PrimaryAxis:
            return "/input/thumbstick";
        case Component::Trackpad:
            return "/input/trackpad";
        default:
            return nullptr;
    }
}

const char* ResolveVive(const Component component)
{
    switch (component)
    {
        case Component::Primary:
            return "/input/trackpad/click";
        case Component::Secondary:
            return "/input/menu/click";
        case Component::Menu:
            return "/input/menu/click";
        case Component::Trigger:
            return "/input/trigger/value";
        case Component::Squeeze:
            return "/input/squeeze/click";
        case Component::TrackpadClick:
            return "/input/trackpad/click";
        case Component::TrackpadTouch:
            return "/input/trackpad/touch";
        case Component::Trackpad:
        case Component::PrimaryAxis:
            return "/input/trackpad";
        default:
            return nullptr;
    }
}

const char* ResolveMicrosoft(const Component component)
{
    switch (component)
    {
        case Component::Primary:
            return "/input/trackpad/click";
        case Component::Secondary:
            return "/input/thumbstick/click";
        case Component::Menu:
            return "/input/menu/click";
        case Component::Trigger:
            return "/input/trigger/value";
        case Component::Squeeze:
            return "/input/squeeze/click";
        case Component::ThumbstickClick:
            return "/input/thumbstick/click";
        case Component::TrackpadClick:
            return "/input/trackpad/click";
        case Component::TrackpadTouch:
            return "/input/trackpad/touch";
        case Component::Thumbstick:
        case Component::PrimaryAxis:
            return "/input/thumbstick";
        case Component::Trackpad:
            return "/input/trackpad";
        default:
            return nullptr;
    }
}

const char* ResolveSimple(const Component component)
{
    switch (component)
    {
        case Component::Primary:
            return "/input/select/click";
        case Component::Secondary:
            return "/input/menu/click";
        case Component::Menu:
            return "/input/menu/click";
        default:
            return nullptr;
    }
}

} // namespace

const std::array<OpenXrProfileDefinition, kOpenXrProfileCount>&
OpenXrProfileDefinitions()
{
    return kProfiles;
}

const char* ResolveOpenXrComponent(
    const OpenXrProfile profile,
    const Source source)
{
    const Component component = ComponentForSource(source);

    switch (profile)
    {
        case OpenXrProfile::KhronosGeneric:
            return ResolveGeneric(component);
        case OpenXrProfile::OculusTouch:
        case OpenXrProfile::MetaTouchPlus:
        case OpenXrProfile::MetaTouchPro:
        case OpenXrProfile::Pico4:
        case OpenXrProfile::PicoNeo3:
        case OpenXrProfile::HtcViveCosmos:
        case OpenXrProfile::HtcViveFocus3:
        case OpenXrProfile::HpMixedReality:
            return ResolveTouchFamily(profile, source, component);
        case OpenXrProfile::ValveIndex:
            return ResolveIndex(component);
        case OpenXrProfile::HtcVive:
            return ResolveVive(component);
        case OpenXrProfile::MicrosoftMotion:
        case OpenXrProfile::SamsungOdyssey:
            return ResolveMicrosoft(component);
        case OpenXrProfile::KhronosSimple:
            return ResolveSimple(component);
        case OpenXrProfile::Count:
            return nullptr;
    }

    return nullptr;
}

bool OpenXrProfileHasHaptics(const OpenXrProfile profile)
{
    return profile != OpenXrProfile::Count;
}

bool OpenXrProfileHasPalmPose(const OpenXrProfile profile)
{
    // XR_EXT_palm_pose extends every interaction profile in kProfiles.
    return profile != OpenXrProfile::Count;
}

} // namespace kisak::vr::input
