#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace kisak::vr::input
{

// Controller Input V4 deliberately stores semantic, controller-neutral source
// identifiers.  The OpenXR and OpenVR adapters translate these identifiers to
// the active controller's concrete component paths at runtime.
enum class ValueType
{
    Boolean,
    Vector2,
};

enum class Hand
{
    None,
    Left,
    Right,
};

enum class Source
{
    Unbound,

    LeftPrimary,
    LeftSecondary,
    LeftMenu,
    LeftAuxiliary,
    LeftTrigger,
    LeftSqueeze,
    LeftThumbstickClick,
    LeftTrackpadClick,
    LeftThumbrestTouch,
    LeftTrackpadTouch,

    RightPrimary,
    RightSecondary,
    RightMenu,
    RightAuxiliary,
    RightTrigger,
    RightSqueeze,
    RightThumbstickClick,
    RightTrackpadClick,
    RightThumbrestTouch,
    RightTrackpadTouch,

    LeftPrimaryAxisUp,
    LeftPrimaryAxisDown,
    LeftPrimaryAxisLeft,
    LeftPrimaryAxisRight,
    RightPrimaryAxisUp,
    RightPrimaryAxisDown,
    RightPrimaryAxisLeft,
    RightPrimaryAxisRight,

    LeftPrimaryAxis,
    RightPrimaryAxis,
    LeftThumbstick,
    LeftTrackpad,
    RightThumbstick,
    RightTrackpad,

    Count,
};

enum class Action
{
    Attack,
    Aim,
    Jump,
    Use,
    Reload,
    Sprint,
    Melee,
    Stance,
    LowerStance,
    NextWeapon,
    Offhand,
    SupportGrip,
    PauseMenu,
    MenuConfirm,
    MenuBack,
    MenuNavigate,
    GrenadeLauncher,
    NightVision,
    Airstrike,
    C4,
    ScopeZoom,
    Move,
    Turn,

    Count,
};

constexpr std::size_t kSourceCount =
    static_cast<std::size_t>(Source::Count);

constexpr std::size_t kActionCount =
    static_cast<std::size_t>(Action::Count);

constexpr std::size_t kMaxBindingSources = 4u;

struct SourceDefinition
{
    Source source = Source::Unbound;
    const char* id = "unbound";
    const char* label = "Unbound";
    ValueType valueType = ValueType::Boolean;
    Hand hand = Hand::None;
};

struct ActionDefinition
{
    Action action = Action::Attack;
    const char* settingKey = "";
    const char* alternateSettingKey = "";
    const char* openXrName = "";
    const char* label = "";
    const char* description = "";
    ValueType valueType = ValueType::Boolean;
    const char* defaultBinding = "unbound";
    const char* defaultAlternateBinding = "unbound";
    bool gameplayConflictGroup = true;
};

// A slot is an AND-chord: every listed source must be active at once. The
// primary and alternate slots remain OR alternatives for the action.
struct Binding
{
    std::array<Source, kMaxBindingSources> sources = {};
    std::size_t sourceCount = 0u;
};

const std::array<SourceDefinition, kSourceCount>& SourceDefinitions();
const std::array<ActionDefinition, kActionCount>& ActionDefinitions();

const SourceDefinition& GetSourceDefinition(Source source);
const ActionDefinition& GetActionDefinition(Action action);
const ActionDefinition* FindActionDefinition(
    std::string_view settingKey,
    bool* alternate = nullptr);

bool ParseSource(std::string_view value, Source* source);
std::string_view SourceId(Source source);
bool IsSourceCompatible(Action action, Source source);

bool ParseBinding(
    Action action,
    std::string_view value,
    Binding* binding,
    std::string* error = nullptr);
std::string BindingId(const Binding& binding);
std::string BindingLabel(const Binding& binding);

bool IsDirectionalSource(Source source);
Source PhysicalSource(Source source);
ValueType PhysicalSourceValueType(Source source);
Source DirectionalSource(
    Source vectorSource,
    float x,
    float y,
    float engageThreshold = 0.75f,
    float dominanceMargin = 0.12f);
bool DirectionalSourcePressed(
    Source source,
    float x,
    float y,
    float engageThreshold = 0.75f,
    float dominanceMargin = 0.12f);
bool DirectionalSourceReleased(
    Source source,
    float x,
    float y,
    float releaseThreshold = 0.35f);

// Converts the six beta.7 Quest-only values (x/y/a/b/stick) to the V3
// controller-neutral identifiers. Unknown values are returned unchanged so
// normal validation can report them precisely.
std::string CanonicalizeLegacyValue(
    std::string_view settingKey,
    std::string_view value);

} // namespace kisak::vr::input
