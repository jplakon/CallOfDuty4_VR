#include "vr/vr_input_bindings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace kisak::vr::input
{
namespace
{

constexpr std::array<SourceDefinition, kSourceCount> kSources = {{
    {Source::Unbound, "unbound", "Unbound", ValueType::Boolean, Hand::None},

    {Source::LeftPrimary, "left.primary", "Left primary action (X/A/select/pad)", ValueType::Boolean, Hand::Left},
    {Source::LeftSecondary, "left.secondary", "Left secondary action (Y/B/menu/stick)", ValueType::Boolean, Hand::Left},
    {Source::LeftMenu, "left.menu", "Left menu", ValueType::Boolean, Hand::Left},
    {Source::LeftAuxiliary, "left.auxiliary", "Left auxiliary / shoulder", ValueType::Boolean, Hand::Left},
    {Source::LeftTrigger, "left.trigger", "Left trigger", ValueType::Boolean, Hand::Left},
    {Source::LeftSqueeze, "left.squeeze", "Left grip / squeeze", ValueType::Boolean, Hand::Left},
    {Source::LeftThumbstickClick, "left.thumbstick_click", "Left thumbstick click", ValueType::Boolean, Hand::Left},
    {Source::LeftTrackpadClick, "left.trackpad_click", "Left trackpad press", ValueType::Boolean, Hand::Left},
    {Source::LeftThumbrestTouch, "left.thumbrest_touch", "Left thumbrest touch", ValueType::Boolean, Hand::Left},
    {Source::LeftTrackpadTouch, "left.trackpad_touch", "Left trackpad touch", ValueType::Boolean, Hand::Left},

    {Source::RightPrimary, "right.primary", "Right primary action (A/X/select/pad)", ValueType::Boolean, Hand::Right},
    {Source::RightSecondary, "right.secondary", "Right secondary action (B/Y/menu/stick)", ValueType::Boolean, Hand::Right},
    {Source::RightMenu, "right.menu", "Right menu", ValueType::Boolean, Hand::Right},
    {Source::RightAuxiliary, "right.auxiliary", "Right auxiliary / shoulder", ValueType::Boolean, Hand::Right},
    {Source::RightTrigger, "right.trigger", "Right trigger", ValueType::Boolean, Hand::Right},
    {Source::RightSqueeze, "right.squeeze", "Right grip / squeeze", ValueType::Boolean, Hand::Right},
    {Source::RightThumbstickClick, "right.thumbstick_click", "Right thumbstick click", ValueType::Boolean, Hand::Right},
    {Source::RightTrackpadClick, "right.trackpad_click", "Right trackpad press", ValueType::Boolean, Hand::Right},
    {Source::RightThumbrestTouch, "right.thumbrest_touch", "Right thumbrest touch", ValueType::Boolean, Hand::Right},
    {Source::RightTrackpadTouch, "right.trackpad_touch", "Right trackpad touch", ValueType::Boolean, Hand::Right},

    {Source::LeftPrimaryAxisUp, "left.primary_axis.up", "Left primary stick / trackpad up", ValueType::Boolean, Hand::Left},
    {Source::LeftPrimaryAxisDown, "left.primary_axis.down", "Left primary stick / trackpad down", ValueType::Boolean, Hand::Left},
    {Source::LeftPrimaryAxisLeft, "left.primary_axis.left", "Left primary stick / trackpad left", ValueType::Boolean, Hand::Left},
    {Source::LeftPrimaryAxisRight, "left.primary_axis.right", "Left primary stick / trackpad right", ValueType::Boolean, Hand::Left},
    {Source::RightPrimaryAxisUp, "right.primary_axis.up", "Right primary stick / trackpad up", ValueType::Boolean, Hand::Right},
    {Source::RightPrimaryAxisDown, "right.primary_axis.down", "Right primary stick / trackpad down", ValueType::Boolean, Hand::Right},
    {Source::RightPrimaryAxisLeft, "right.primary_axis.left", "Right primary stick / trackpad left", ValueType::Boolean, Hand::Right},
    {Source::RightPrimaryAxisRight, "right.primary_axis.right", "Right primary stick / trackpad right", ValueType::Boolean, Hand::Right},

    {Source::LeftPrimaryAxis, "left.primary_axis", "Left primary stick / trackpad", ValueType::Vector2, Hand::Left},
    {Source::RightPrimaryAxis, "right.primary_axis", "Right primary stick / trackpad", ValueType::Vector2, Hand::Right},
    {Source::LeftThumbstick, "left.thumbstick", "Left thumbstick", ValueType::Vector2, Hand::Left},
    {Source::LeftTrackpad, "left.trackpad", "Left trackpad", ValueType::Vector2, Hand::Left},
    {Source::RightThumbstick, "right.thumbstick", "Right thumbstick", ValueType::Vector2, Hand::Right},
    {Source::RightTrackpad, "right.trackpad", "Right trackpad", ValueType::Vector2, Hand::Right},
}};

constexpr std::array<ActionDefinition, kActionCount> kActions = {{
    {Action::Attack, "KISAK_VR_BIND_ATTACK", "KISAK_VR_BIND_ATTACK_ALT", "attack", "Fire weapon", "Fire the equipped weapon or activate its trigger.", ValueType::Boolean, "right.trigger", "unbound", true},
    {Action::Aim, "KISAK_VR_BIND_AIM", "KISAK_VR_BIND_AIM_ALT", "aim", "Aim / ADS override", "Optionally hold a controller input to aim down sights; physical two-hand shouldering remains available when this is unbound.", ValueType::Boolean, "unbound", "unbound", true},
    {Action::Jump, "KISAK_VR_BIND_JUMP", "KISAK_VR_BIND_JUMP_ALT", "jump", "Jump", "Jump or mantle while standing, or raise one stance step while crouched or prone. The primary default is moving the right stick / trackpad up; left trigger remains the legacy alternate.", ValueType::Boolean, "right.primary_axis.up", "left.trigger", true},
    {Action::Use, "KISAK_VR_BIND_USE", "KISAK_VR_BIND_USE_ALT", "use", "Use / interact", "Pick up weapons, open doors, and activate prompts.", ValueType::Boolean, "left.primary", "unbound", true},
    {Action::Reload, "KISAK_VR_BIND_RELOAD", "KISAK_VR_BIND_RELOAD_ALT", "reload", "Reload / eject magazine", "Reload natively or eject a physical magazine.", ValueType::Boolean, "right.primary", "unbound", true},
    {Action::Sprint, "KISAK_VR_BIND_SPRINT", "KISAK_VR_BIND_SPRINT_ALT", "sprint", "Sprint", "Sprint while moving forward.", ValueType::Boolean, "left.thumbstick_click", "unbound", true},
    {Action::Melee, "KISAK_VR_BIND_MELEE", "KISAK_VR_BIND_MELEE_ALT", "melee", "Melee", "Perform a melee attack.", ValueType::Boolean, "right.thumbstick_click", "unbound", true},
    {Action::Stance, "KISAK_VR_BIND_STANCE", "KISAK_VR_BIND_STANCE_ALT", "stance", "Stance toggle (legacy B button)", "Optional COD4 native stance control: tap to crouch or stand and hold to go prone.", ValueType::Boolean, "right.secondary", "unbound", true},
    {Action::LowerStance, "KISAK_VR_BIND_LOWER_STANCE", "KISAK_VR_BIND_LOWER_STANCE_ALT", "lower_stance", "Lower stance", "Lower one stance step. The primary default is moving the right stick / trackpad down; release it before triggering another step.", ValueType::Boolean, "right.primary_axis.down", "unbound", true},
    {Action::NextWeapon, "KISAK_VR_BIND_NEXT_WEAPON", "KISAK_VR_BIND_NEXT_WEAPON_ALT", "next_weapon", "Next weapon", "Cycle to the next carried weapon.", ValueType::Boolean, "left.secondary", "unbound", true},
    {Action::Offhand, "KISAK_VR_BIND_OFFHAND", "KISAK_VR_BIND_OFFHAND_ALT", "offhand", "Native off-hand action", "Optional native off-hand grenade or mission-equipment action. Physical grenades use the left grip, so the tested default leaves this unbound.", ValueType::Boolean, "unbound", "unbound", true},
    {Action::SupportGrip, "KISAK_VR_BIND_SUPPORT_GRIP", "KISAK_VR_BIND_SUPPORT_GRIP_ALT", "support_grip", "Support-hand grip", "Grip a two-handed weapon, grenade, or physical magazine with the support hand.", ValueType::Boolean, "left.squeeze", "unbound", true},
    {Action::PauseMenu, "KISAK_VR_BIND_MENU", "KISAK_VR_BIND_MENU_ALT", "pause_menu", "Pause / Escape", "Open or close COD4 menus.", ValueType::Boolean, "left.menu", "unbound", false},
    {Action::MenuConfirm, "KISAK_VR_BIND_MENU_CONFIRM", "KISAK_VR_BIND_MENU_CONFIRM_ALT", "menu_confirm", "Menu confirm", "Activate the selected menu item.", ValueType::Boolean, "right.primary", "unbound", false},
    {Action::MenuBack, "KISAK_VR_BIND_MENU_BACK", "KISAK_VR_BIND_MENU_BACK_ALT", "menu_back", "Menu back", "Return from the current menu.", ValueType::Boolean, "right.secondary", "unbound", false},
    {Action::MenuNavigate, "KISAK_VR_BIND_MENU_AXIS", "KISAK_VR_BIND_MENU_AXIS_ALT", "menu_axis", "Menu cursor axis", "Analog axis used to move the VR menu cursor.", ValueType::Vector2, "left.primary_axis", "unbound", false},
    {Action::GrenadeLauncher, "KISAK_VR_BIND_GRENADE_LAUNCHER", "KISAK_VR_BIND_GRENADE_LAUNCHER_ALT", "grenade_launcher", "Grenade launcher (slot 5)", "Shortcut for COD4 weapon slot 5. The fresh-profile default is the physical right grip; multiple inputs in one slot form a chord and must be held together.", ValueType::Boolean, "right.squeeze", "unbound", true},
    {Action::NightVision, "KISAK_VR_BIND_NIGHT_VISION", "KISAK_VR_BIND_NIGHT_VISION_ALT", "night_vision", "Night vision", "Toggle night vision. Multiple inputs in one slot form a chord and must be held together.", ValueType::Boolean, "right.thumbrest_touch+left.primary_axis.down", "unbound", true},
    {Action::Airstrike, "KISAK_VR_BIND_AIRSTRIKE", "KISAK_VR_BIND_AIRSTRIKE_ALT", "airstrike", "Airstrike / mission slot 6", "Shortcut for COD4 mission slot 6. Multiple inputs in one slot form a chord and must be held together.", ValueType::Boolean, "right.thumbrest_touch+left.primary_axis.left", "unbound", true},
    {Action::C4, "KISAK_VR_BIND_C4", "KISAK_VR_BIND_C4_ALT", "c4", "C4 / mission slot 7", "Shortcut for COD4 mission slot 7 and its detonator. Multiple inputs in one slot form a chord and must be held together.", ValueType::Boolean, "right.thumbrest_touch+left.primary_axis.right", "unbound", true},
    {Action::ScopeZoom, "KISAK_VR_BIND_SCOPE_ZOOM_AXIS", "KISAK_VR_BIND_SCOPE_ZOOM_AXIS_ALT", "scope_zoom", "Mounted-scope zoom axis", "Vertical analog axis used to zoom fixed scoped turrets.", ValueType::Vector2, "left.primary_axis", "unbound", false},
    {Action::Move, "KISAK_VR_BIND_MOVE_AXIS", "KISAK_VR_BIND_MOVE_AXIS_ALT", "move", "Movement axis", "Analog axis used for walking.", ValueType::Vector2, "left.primary_axis", "unbound", false},
    {Action::Turn, "KISAK_VR_BIND_TURN_AXIS", "KISAK_VR_BIND_TURN_AXIS_ALT", "turn", "Turning axis", "Analog axis used for snap or smooth turning.", ValueType::Vector2, "right.primary_axis", "unbound", false},
}};

bool EqualsIgnoreCase(
    const std::string_view left,
    const std::string_view right)
{
    return left.size() == right.size() &&
        std::equal(
            left.begin(),
            left.end(),
            right.begin(),
            [](const char a, const char b)
            {
                return std::tolower(static_cast<unsigned char>(a)) ==
                    std::tolower(static_cast<unsigned char>(b));
            });
}

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

bool IsOneOf(
    const std::string_view value,
    const std::string_view first,
    const std::string_view second)
{
    return EqualsIgnoreCase(value, first) ||
        EqualsIgnoreCase(value, second);
}

std::string CanonicalizeLegacyToken(
    const std::string_view settingKey,
    const std::string_view rawValue)
{
    const std::string_view value = Trim(rawValue);
    const bool leftLegacy =
        settingKey == "KISAK_VR_BIND_USE" ||
        settingKey == "KISAK_VR_BIND_SPRINT" ||
        settingKey == "KISAK_VR_BIND_NEXT_WEAPON";

    if (leftLegacy)
    {
        if (EqualsIgnoreCase(value, "x"))
        {
            return "left.primary";
        }
        if (EqualsIgnoreCase(value, "y"))
        {
            return "left.secondary";
        }
        if (EqualsIgnoreCase(value, "stick"))
        {
            return "left.thumbstick_click";
        }
    }

    const bool rightLegacy =
        settingKey == "KISAK_VR_BIND_RELOAD" ||
        settingKey == "KISAK_VR_BIND_MELEE" ||
        settingKey == "KISAK_VR_BIND_STANCE";

    if (rightLegacy)
    {
        if (EqualsIgnoreCase(value, "a"))
        {
            return "right.primary";
        }
        if (EqualsIgnoreCase(value, "b"))
        {
            return "right.secondary";
        }
        if (EqualsIgnoreCase(value, "stick"))
        {
            return "right.thumbstick_click";
        }
    }

    if (IsOneOf(value, "left.x", "left.a"))
    {
        return "left.primary";
    }
    if (IsOneOf(value, "left.y", "left.b"))
    {
        return "left.secondary";
    }
    if (IsOneOf(value, "right.a", "right.x"))
    {
        return "right.primary";
    }
    if (IsOneOf(value, "right.b", "right.y"))
    {
        return "right.secondary";
    }

    return std::string(value);
}

} // namespace

const std::array<SourceDefinition, kSourceCount>& SourceDefinitions()
{
    return kSources;
}

const std::array<ActionDefinition, kActionCount>& ActionDefinitions()
{
    return kActions;
}

const SourceDefinition& GetSourceDefinition(const Source source)
{
    const std::size_t index = static_cast<std::size_t>(source);
    return index < kSources.size() ? kSources[index] : kSources[0];
}

const ActionDefinition& GetActionDefinition(const Action action)
{
    const std::size_t index = static_cast<std::size_t>(action);
    return index < kActions.size() ? kActions[index] : kActions[0];
}

const ActionDefinition* FindActionDefinition(
    const std::string_view settingKey,
    bool* const alternate)
{
    for (const ActionDefinition& definition : kActions)
    {
        if (EqualsIgnoreCase(settingKey, definition.settingKey))
        {
            if (alternate != nullptr)
            {
                *alternate = false;
            }
            return &definition;
        }

        if (EqualsIgnoreCase(settingKey, definition.alternateSettingKey))
        {
            if (alternate != nullptr)
            {
                *alternate = true;
            }
            return &definition;
        }
    }

    return nullptr;
}

bool ParseSource(const std::string_view value, Source* const source)
{
    if (source == nullptr)
    {
        return false;
    }

    for (const SourceDefinition& definition : kSources)
    {
        if (EqualsIgnoreCase(Trim(value), definition.id))
        {
            *source = definition.source;
            return true;
        }
    }

    return false;
}

std::string_view SourceId(const Source source)
{
    return GetSourceDefinition(source).id;
}

bool IsSourceCompatible(const Action action, const Source source)
{
    if (source == Source::Unbound)
    {
        return true;
    }

    return GetActionDefinition(action).valueType ==
        GetSourceDefinition(source).valueType;
}

bool ParseBinding(
    const Action action,
    const std::string_view rawValue,
    Binding* const binding,
    std::string* const error)
{
    if (binding == nullptr)
    {
        return false;
    }

    *binding = Binding{};
    const std::string_view value = Trim(rawValue);
    if (value.empty() || EqualsIgnoreCase(value, "unbound"))
    {
        return true;
    }

    std::size_t offset = 0u;
    while (offset <= value.size())
    {
        const std::size_t separator = value.find('+', offset);
        const std::size_t length = separator == std::string_view::npos
            ? value.size() - offset
            : separator - offset;
        const std::string_view token = Trim(value.substr(offset, length));

        Source source = Source::Unbound;
        if (token.empty() || !ParseSource(token, &source) ||
            source == Source::Unbound)
        {
            if (error != nullptr)
            {
                *error = "A chord contains an unknown or unbound controller input.";
            }
            return false;
        }

        if (!IsSourceCompatible(action, source))
        {
            if (error != nullptr)
            {
                *error = "A chord contains an input that is incompatible with this action.";
            }
            return false;
        }

        if (std::find(
                binding->sources.begin(),
                binding->sources.begin() + binding->sourceCount,
                source) != binding->sources.begin() + binding->sourceCount)
        {
            if (error != nullptr)
            {
                *error = "A chord cannot contain the same controller input twice.";
            }
            return false;
        }

        if (binding->sourceCount >= kMaxBindingSources)
        {
            if (error != nullptr)
            {
                *error = "A binding may combine at most four controller inputs.";
            }
            return false;
        }

        binding->sources[binding->sourceCount++] = source;

        if (separator == std::string_view::npos)
        {
            break;
        }
        offset = separator + 1u;
    }

    // Vector actions represent an analog value rather than buttons. Keep
    // their primary/alternate slots as single-axis alternatives.
    if (GetActionDefinition(action).valueType == ValueType::Vector2 &&
        binding->sourceCount > 1u)
    {
        if (error != nullptr)
        {
            *error = "Analog-axis actions accept one axis per primary or alternate slot.";
        }
        return false;
    }

    return true;
}

std::string BindingId(const Binding& binding)
{
    if (binding.sourceCount == 0u)
    {
        return "unbound";
    }

    std::ostringstream value;
    for (std::size_t index = 0u; index < binding.sourceCount; ++index)
    {
        if (index != 0u)
        {
            value << '+';
        }
        value << SourceId(binding.sources[index]);
    }
    return value.str();
}

std::string BindingLabel(const Binding& binding)
{
    if (binding.sourceCount == 0u)
    {
        return "Unbound";
    }

    std::ostringstream value;
    for (std::size_t index = 0u; index < binding.sourceCount; ++index)
    {
        if (index != 0u)
        {
            value << " + ";
        }
        value << GetSourceDefinition(binding.sources[index]).label;
    }
    return value.str();
}

bool IsDirectionalSource(const Source source)
{
    return source >= Source::LeftPrimaryAxisUp &&
        source <= Source::RightPrimaryAxisRight;
}

Source PhysicalSource(const Source source)
{
    switch (source)
    {
        case Source::LeftPrimaryAxisUp:
        case Source::LeftPrimaryAxisDown:
        case Source::LeftPrimaryAxisLeft:
        case Source::LeftPrimaryAxisRight:
            return Source::LeftPrimaryAxis;
        case Source::RightPrimaryAxisUp:
        case Source::RightPrimaryAxisDown:
        case Source::RightPrimaryAxisLeft:
        case Source::RightPrimaryAxisRight:
            return Source::RightPrimaryAxis;
        default:
            return source;
    }
}

ValueType PhysicalSourceValueType(const Source source)
{
    return GetSourceDefinition(PhysicalSource(source)).valueType;
}

Source DirectionalSource(
    const Source rawVectorSource,
    const float x,
    const float y,
    const float engageThreshold,
    const float dominanceMargin)
{
    const Source vectorSource = PhysicalSource(rawVectorSource);
    if ((vectorSource != Source::LeftPrimaryAxis &&
         vectorSource != Source::RightPrimaryAxis) ||
        !std::isfinite(x) || !std::isfinite(y))
    {
        return Source::Unbound;
    }

    const float absoluteX = std::abs(x);
    const float absoluteY = std::abs(y);
    const bool left = vectorSource == Source::LeftPrimaryAxis;

    if (absoluteY >= engageThreshold &&
        absoluteY >= absoluteX + dominanceMargin)
    {
        if (y > 0.0f)
        {
            return left
                ? Source::LeftPrimaryAxisUp
                : Source::RightPrimaryAxisUp;
        }
        return left
            ? Source::LeftPrimaryAxisDown
            : Source::RightPrimaryAxisDown;
    }

    if (absoluteX >= engageThreshold &&
        absoluteX >= absoluteY + dominanceMargin)
    {
        if (x < 0.0f)
        {
            return left
                ? Source::LeftPrimaryAxisLeft
                : Source::RightPrimaryAxisLeft;
        }
        return left
            ? Source::LeftPrimaryAxisRight
            : Source::RightPrimaryAxisRight;
    }

    return Source::Unbound;
}

bool DirectionalSourcePressed(
    const Source source,
    const float x,
    const float y,
    const float engageThreshold,
    const float dominanceMargin)
{
    return IsDirectionalSource(source) &&
        DirectionalSource(
            PhysicalSource(source),
            x,
            y,
            engageThreshold,
        dominanceMargin) == source;
}

bool DirectionalSourceReleased(
    const Source source,
    const float x,
    const float y,
    const float releaseThreshold)
{
    if (!IsDirectionalSource(source) ||
        !std::isfinite(releaseThreshold) ||
        releaseThreshold < 0.0f)
    {
        return false;
    }

    if (!std::isfinite(x) || !std::isfinite(y))
    {
        return true;
    }

    return std::abs(x) <= releaseThreshold &&
        std::abs(y) <= releaseThreshold;
}

std::string CanonicalizeLegacyValue(
    const std::string_view settingKey,
    const std::string_view value)
{
    if (value.find('+') == std::string_view::npos)
    {
        return CanonicalizeLegacyToken(settingKey, value);
    }

    std::ostringstream canonical;
    std::size_t offset = 0u;
    bool first = true;
    while (offset <= value.size())
    {
        const std::size_t separator = value.find('+', offset);
        const std::size_t length = separator == std::string_view::npos
            ? value.size() - offset
            : separator - offset;

        if (!first)
        {
            canonical << '+';
        }
        first = false;
        canonical << CanonicalizeLegacyToken(
            settingKey,
            value.substr(offset, length));

        if (separator == std::string_view::npos)
        {
            break;
        }
        offset = separator + 1u;
    }

    return canonical.str();
}

} // namespace kisak::vr::input
