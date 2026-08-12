#include "vr/vr_prompt_labels.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace kisak::vr::prompts
{
namespace
{

namespace VrInput = kisak::vr::input;

enum class ControllerFamily
{
    Generic,
    Touch,
    Index,
    Vive,
    MixedReality,
    Simple,
};

struct CommandAction
{
    const char* command;
    VrInput::Action action;
};

// These aliases cover the shared SP HUD resolver and getKeyBinding() script
// path. They intentionally exclude hybrid commands such as +usereload and
// +melee_breath because one keyboard command represents two distinct VR
// actions and would otherwise advertise a control that is not always valid.
constexpr std::array<CommandAction, 25> kCommandActions = {{
    {"+attack", VrInput::Action::Attack},
    {"+activate", VrInput::Action::Use},
    {"+reload", VrInput::Action::Reload},
    {"+gostand", VrInput::Action::Jump},
    {"+moveup", VrInput::Action::Jump},
    {"raisestance", VrInput::Action::Jump},
    {"+melee", VrInput::Action::Melee},
    {"+sprint", VrInput::Action::Sprint},
    {"+speed", VrInput::Action::Sprint},
    {"gocrouch", VrInput::Action::Stance},
    {"togglecrouch", VrInput::Action::Stance},
    {"goprone", VrInput::Action::Stance},
    {"toggleprone", VrInput::Action::Stance},
    {"+prone", VrInput::Action::Stance},
    {"+stance", VrInput::Action::Stance},
    {"lowerstance", VrInput::Action::LowerStance},
    {"+movedown", VrInput::Action::LowerStance},
    {"weapnext", VrInput::Action::NextWeapon},
    {"+nightvision", VrInput::Action::NightVision},
    {"+forward", VrInput::Action::Move},
    {"+back", VrInput::Action::Move},
    {"+moveleft", VrInput::Action::Move},
    {"+moveright", VrInput::Action::Move},
    {"+left", VrInput::Action::Turn},
    {"+right", VrInput::Action::Turn},
}};

std::string_view Trim(const std::string_view value)
{
    std::size_t first = 0u;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])))
    {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1u])))
    {
        --last;
    }

    return value.substr(first, last - first);
}

bool EqualsIgnoreCase(
    const std::string_view left,
    const std::string_view right)
{
    return left.size() == right.size() &&
        std::equal(
            left.begin(),
            left.end(),
            right.begin(),
            [](const char leftCharacter, const char rightCharacter)
            {
                return std::tolower(
                           static_cast<unsigned char>(leftCharacter)) ==
                    std::tolower(
                           static_cast<unsigned char>(rightCharacter));
            });
}

std::string Lower(const std::string_view value)
{
    std::string lowered(value);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return lowered;
}

bool Contains(
    const std::string& value,
    const char* const fragment)
{
    return fragment != nullptr &&
        value.find(fragment) != std::string::npos;
}

ControllerFamily DetectControllerFamily(
    const std::string_view activeProfile)
{
    const std::string profile = Lower(activeProfile);

    // Test the specialized Vive variants before the original wand profile.
    if (Contains(profile, "oculus") ||
        Contains(profile, "/meta/") ||
        Contains(profile, "/facebook/") ||
        Contains(profile, "touch_controller") ||
        Contains(profile, "touch_profile") ||
        Contains(profile, "pico") ||
        Contains(profile, "vive_cosmos") ||
        Contains(profile, "vive_focus") ||
        Contains(profile, "/hp/mixed_reality_controller"))
    {
        return ControllerFamily::Touch;
    }

    if (Contains(profile, "valve/index") ||
        Contains(profile, "index_controller") ||
        Contains(profile, "knuckles"))
    {
        return ControllerFamily::Index;
    }

    if (Contains(profile, "microsoft/motion") ||
        Contains(profile, "mixed_reality") ||
        Contains(profile, "windowsmr") ||
        Contains(profile, "holographic") ||
        Contains(profile, "odyssey"))
    {
        return ControllerFamily::MixedReality;
    }

    if (Contains(profile, "htc/vive_controller") ||
        Contains(profile, "vive_controller"))
    {
        return ControllerFamily::Vive;
    }

    if (Contains(profile, "simple_controller"))
    {
        return ControllerFamily::Simple;
    }

    return ControllerFamily::Generic;
}

bool IsLeft(const VrInput::Source source)
{
    return VrInput::GetSourceDefinition(source).hand ==
        VrInput::Hand::Left;
}

const char* HandName(const bool left)
{
    return left ? "Left" : "Right";
}

std::string HandControl(
    const bool left,
    const char* const control)
{
    return std::string(HandName(left)) + " " + control;
}

const char* DirectionName(const VrInput::Source source)
{
    switch (source)
    {
        case VrInput::Source::LeftPrimaryAxisUp:
        case VrInput::Source::RightPrimaryAxisUp:
            return "up";
        case VrInput::Source::LeftPrimaryAxisDown:
        case VrInput::Source::RightPrimaryAxisDown:
            return "down";
        case VrInput::Source::LeftPrimaryAxisLeft:
        case VrInput::Source::RightPrimaryAxisLeft:
            return "left";
        case VrInput::Source::LeftPrimaryAxisRight:
        case VrInput::Source::RightPrimaryAxisRight:
            return "right";
        default:
            return nullptr;
    }
}

std::string PrimaryAxisLabel(
    const VrInput::Source source,
    const ControllerFamily family)
{
    const bool left = IsLeft(source);
    const char* const axis = family == ControllerFamily::Vive
        ? "trackpad"
        : family == ControllerFamily::Simple
            ? "axis"
            : "stick";
    const char* const direction = DirectionName(source);

    std::string label = HandControl(left, axis);
    if (direction != nullptr)
    {
        label += " ";
        label += direction;
    }
    return label;
}

std::string OpenXrActionLabel(
    const VrInput::Source source,
    const ControllerFamily family)
{
    const bool left = IsLeft(source);

    switch (source)
    {
        case VrInput::Source::LeftPrimary:
        case VrInput::Source::RightPrimary:
            switch (family)
            {
                case ControllerFamily::Touch:
                    return left ? "X" : "A";
                case ControllerFamily::Index:
                    return HandControl(left, "A");
                case ControllerFamily::Vive:
                case ControllerFamily::MixedReality:
                    return HandControl(left, "trackpad press");
                case ControllerFamily::Simple:
                    return HandControl(left, "select");
                case ControllerFamily::Generic:
                    return HandControl(left, "primary");
            }
            break;

        case VrInput::Source::LeftSecondary:
        case VrInput::Source::RightSecondary:
            switch (family)
            {
                case ControllerFamily::Touch:
                    return left ? "Y" : "B";
                case ControllerFamily::Index:
                    return HandControl(left, "B");
                case ControllerFamily::Vive:
                case ControllerFamily::Simple:
                    return HandControl(left, "menu");
                case ControllerFamily::MixedReality:
                    return HandControl(left, "stick click");
                case ControllerFamily::Generic:
                    return HandControl(left, "secondary");
            }
            break;

        default:
            break;
    }

    return {};
}

std::string OpenVrActionLabel(
    const VrInput::Source source,
    const ControllerFamily family)
{
    const bool left = IsLeft(source);

    switch (source)
    {
        case VrInput::Source::LeftPrimary:
        case VrInput::Source::RightPrimary:
            if (family == ControllerFamily::Index)
            {
                // The legacy OpenVR adapter deliberately resolves Index's
                // primary action through the grip bit, unlike OpenXR's A.
                return HandControl(left, "grip");
            }
            if (family == ControllerFamily::Vive ||
                family == ControllerFamily::MixedReality)
            {
                return HandControl(left, "trackpad press");
            }
            if (family == ControllerFamily::Touch)
            {
                return left ? "X" : "A";
            }
            return HandControl(left, "primary");

        case VrInput::Source::LeftSecondary:
        case VrInput::Source::RightSecondary:
            if (family == ControllerFamily::MixedReality)
            {
                return HandControl(left, "stick click");
            }
            // Legacy OpenVR maps the portable secondary action to the
            // application-menu bit for every other known controller family.
            return HandControl(left, "menu");

        default:
            return {};
    }
}

const char* PromptDirection(const std::string_view rawCommand)
{
    const std::string_view command = Trim(rawCommand);
    if (EqualsIgnoreCase(command, "+forward"))
    {
        return "up";
    }
    if (EqualsIgnoreCase(command, "+back"))
    {
        return "down";
    }
    if (EqualsIgnoreCase(command, "+moveleft") ||
        EqualsIgnoreCase(command, "+left"))
    {
        return "left";
    }
    if (EqualsIgnoreCase(command, "+moveright") ||
        EqualsIgnoreCase(command, "+right"))
    {
        return "right";
    }
    return nullptr;
}

bool IsVectorSource(const VrInput::Source source)
{
    return source == VrInput::Source::LeftPrimaryAxis ||
        source == VrInput::Source::RightPrimaryAxis ||
        source == VrInput::Source::LeftThumbstick ||
        source == VrInput::Source::RightThumbstick ||
        source == VrInput::Source::LeftTrackpad ||
        source == VrInput::Source::RightTrackpad;
}

} // namespace

const VrInput::ActionDefinition* FindPromptAction(
    const std::string_view rawCommand)
{
    const std::string_view command = Trim(rawCommand);
    for (const CommandAction& candidate : kCommandActions)
    {
        if (EqualsIgnoreCase(command, candidate.command))
        {
            return &VrInput::GetActionDefinition(candidate.action);
        }
    }
    return nullptr;
}

std::string SourcePromptLabel(
    const VrInput::Source source,
    const std::string_view activeProfile,
    const Backend backend)
{
    if (source == VrInput::Source::Unbound ||
        source == VrInput::Source::Count)
    {
        return {};
    }

    const ControllerFamily family =
        DetectControllerFamily(activeProfile);
    const bool left = IsLeft(source);

    std::string actionLabel = backend == Backend::OpenVr
        ? OpenVrActionLabel(source, family)
        : OpenXrActionLabel(source, family);
    if (!actionLabel.empty())
    {
        return actionLabel;
    }

    if (VrInput::IsDirectionalSource(source) ||
        source == VrInput::Source::LeftPrimaryAxis ||
        source == VrInput::Source::RightPrimaryAxis)
    {
        return PrimaryAxisLabel(source, family);
    }

    switch (source)
    {
        case VrInput::Source::LeftMenu:
        case VrInput::Source::RightMenu:
            return HandControl(left, "menu");
        case VrInput::Source::LeftAuxiliary:
        case VrInput::Source::RightAuxiliary:
            return HandControl(left, "auxiliary");
        case VrInput::Source::LeftTrigger:
        case VrInput::Source::RightTrigger:
            return HandControl(left, "trigger");
        case VrInput::Source::LeftSqueeze:
        case VrInput::Source::RightSqueeze:
            return HandControl(left, "grip");
        case VrInput::Source::LeftThumbstickClick:
            return "L3";
        case VrInput::Source::RightThumbstickClick:
            return "R3";
        case VrInput::Source::LeftTrackpadClick:
        case VrInput::Source::RightTrackpadClick:
            return HandControl(left, "trackpad press");
        case VrInput::Source::LeftThumbrestTouch:
        case VrInput::Source::RightThumbrestTouch:
            return HandControl(left, "thumbrest");
        case VrInput::Source::LeftTrackpadTouch:
        case VrInput::Source::RightTrackpadTouch:
            return HandControl(left, "trackpad touch");
        case VrInput::Source::LeftThumbstick:
        case VrInput::Source::RightThumbstick:
            return HandControl(left, "stick");
        case VrInput::Source::LeftTrackpad:
        case VrInput::Source::RightTrackpad:
            return HandControl(left, "trackpad");
        case VrInput::Source::LeftPrimary:
        case VrInput::Source::RightPrimary:
            return HandControl(left, "primary");
        case VrInput::Source::LeftSecondary:
        case VrInput::Source::RightSecondary:
            return HandControl(left, "secondary");
        case VrInput::Source::Unbound:
        case VrInput::Source::LeftPrimaryAxisUp:
        case VrInput::Source::LeftPrimaryAxisDown:
        case VrInput::Source::LeftPrimaryAxisLeft:
        case VrInput::Source::LeftPrimaryAxisRight:
        case VrInput::Source::RightPrimaryAxisUp:
        case VrInput::Source::RightPrimaryAxisDown:
        case VrInput::Source::RightPrimaryAxisLeft:
        case VrInput::Source::RightPrimaryAxisRight:
        case VrInput::Source::LeftPrimaryAxis:
        case VrInput::Source::RightPrimaryAxis:
        case VrInput::Source::Count:
            break;
    }

    return {};
}

BindingLabels BuildBindingLabels(
    const std::array<VrInput::Binding, 2>& bindings,
    const std::array<std::string_view, 2>& activeProfiles,
    const Backend backend,
    const std::string_view command)
{
    BindingLabels result;
    const char* const promptDirection = PromptDirection(command);

    for (const VrInput::Binding& binding : bindings)
    {
        if (binding.sourceCount == 0u)
        {
            continue;
        }

        std::string slotLabel;
        for (std::size_t termIndex = 0u;
             termIndex < binding.sourceCount;
             ++termIndex)
        {
            const VrInput::Source source =
                binding.sources[termIndex];
            const VrInput::Hand hand =
                VrInput::GetSourceDefinition(source).hand;
            const std::size_t profileIndex =
                hand == VrInput::Hand::Right ? 1u : 0u;
            std::string termLabel = SourcePromptLabel(
                source,
                activeProfiles[profileIndex],
                backend);

            if (promptDirection != nullptr && IsVectorSource(source))
            {
                termLabel += " ";
                termLabel += promptDirection;
            }

            if (termLabel.empty())
            {
                slotLabel.clear();
                break;
            }

            if (!slotLabel.empty())
            {
                slotLabel += " + ";
            }
            slotLabel += termLabel;
        }

        if (slotLabel.empty() ||
            (result.count > 0u && result.values[0] == slotLabel))
        {
            continue;
        }

        result.values[result.count++] = slotLabel;
        if (result.count == result.values.size())
        {
            break;
        }
    }

    return result;
}

} // namespace kisak::vr::prompts
