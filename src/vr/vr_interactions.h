#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace kisak::vr::interactions
{

// Physical controller indices intentionally match the runtime's stable
// OpenXR/OpenVR left/right arrays. Gameplay code can therefore work in
// weapon-hand/off-hand roles without renaming the proven pose pipeline.
enum class DominantHand
{
    Right,
    Left,
};

enum class SupportGripMode
{
    Hold,
    Toggle,
    Proximity,
};

enum class ObjectGripMode
{
    Hold,
    Toggle,
};

enum class ReloadEjectMode
{
    Button,
    Pull,
};

enum class ReloadInsertMode
{
    Release,
    Contact,
};

enum class MagazineHip
{
    OffHand,
    Left,
    Right,
};

enum class GrenadeBeltLayout
{
    Fixed,
    Handed,
};

enum class MeleeMode
{
    Button,
    Gesture,
    Both,
};

constexpr std::uint32_t kLeftControllerIndex = 0u;
constexpr std::uint32_t kRightControllerIndex = 1u;

inline constexpr const char* DominantHandId(const DominantHand hand)
{
    return hand == DominantHand::Left ? "left" : "right";
}

inline constexpr const char* SupportGripModeId(const SupportGripMode mode)
{
    switch (mode)
    {
    case SupportGripMode::Toggle: return "toggle";
    case SupportGripMode::Proximity: return "proximity";
    case SupportGripMode::Hold:
    default: return "hold";
    }
}

inline constexpr const char* ObjectGripModeId(const ObjectGripMode mode)
{
    return mode == ObjectGripMode::Toggle ? "toggle" : "hold";
}

inline constexpr const char* ReloadEjectModeId(const ReloadEjectMode mode)
{
    return mode == ReloadEjectMode::Pull ? "pull" : "button";
}

inline constexpr const char* ReloadInsertModeId(const ReloadInsertMode mode)
{
    return mode == ReloadInsertMode::Contact ? "contact" : "release";
}

inline constexpr const char* MagazineHipId(const MagazineHip hip)
{
    switch (hip)
    {
    case MagazineHip::Left: return "left";
    case MagazineHip::Right: return "right";
    case MagazineHip::OffHand:
    default: return "off_hand";
    }
}

inline constexpr const char* GrenadeBeltLayoutId(
    const GrenadeBeltLayout layout)
{
    return layout == GrenadeBeltLayout::Fixed ? "fixed" : "handed";
}

inline constexpr const char* MeleeModeId(const MeleeMode mode)
{
    switch (mode)
    {
    case MeleeMode::Gesture: return "gesture";
    case MeleeMode::Both: return "both";
    case MeleeMode::Button:
    default: return "button";
    }
}

inline constexpr std::uint32_t WeaponControllerIndex(
    const DominantHand hand)
{
    return hand == DominantHand::Left
        ? kLeftControllerIndex
        : kRightControllerIndex;
}

inline constexpr std::uint32_t OffHandControllerIndex(
    const DominantHand hand)
{
    return hand == DominantHand::Left
        ? kRightControllerIndex
        : kLeftControllerIndex;
}

inline bool ParseDominantHand(
    const std::string_view value,
    DominantHand* const hand)
{
    if (hand == nullptr)
    {
        return false;
    }
    if (value == "right")
    {
        *hand = DominantHand::Right;
        return true;
    }
    if (value == "left")
    {
        *hand = DominantHand::Left;
        return true;
    }
    return false;
}

inline bool ParseSupportGripMode(
    const std::string_view value,
    SupportGripMode* const mode)
{
    if (mode == nullptr)
    {
        return false;
    }
    if (value == "hold")
    {
        *mode = SupportGripMode::Hold;
        return true;
    }
    if (value == "toggle")
    {
        *mode = SupportGripMode::Toggle;
        return true;
    }
    if (value == "proximity")
    {
        *mode = SupportGripMode::Proximity;
        return true;
    }
    return false;
}

inline bool ParseObjectGripMode(
    const std::string_view value,
    ObjectGripMode* const mode)
{
    if (mode == nullptr)
    {
        return false;
    }
    if (value == "hold")
    {
        *mode = ObjectGripMode::Hold;
        return true;
    }
    if (value == "toggle")
    {
        *mode = ObjectGripMode::Toggle;
        return true;
    }
    return false;
}

inline bool ParseReloadEjectMode(
    const std::string_view value,
    ReloadEjectMode* const mode)
{
    if (mode == nullptr)
    {
        return false;
    }
    if (value == "button")
    {
        *mode = ReloadEjectMode::Button;
        return true;
    }
    if (value == "pull")
    {
        *mode = ReloadEjectMode::Pull;
        return true;
    }
    return false;
}

inline bool ParseReloadInsertMode(
    const std::string_view value,
    ReloadInsertMode* const mode)
{
    if (mode == nullptr)
    {
        return false;
    }
    if (value == "release")
    {
        *mode = ReloadInsertMode::Release;
        return true;
    }
    if (value == "contact")
    {
        *mode = ReloadInsertMode::Contact;
        return true;
    }
    return false;
}

inline bool ParseMagazineHip(
    const std::string_view value,
    MagazineHip* const hip)
{
    if (hip == nullptr)
    {
        return false;
    }
    if (value == "off_hand")
    {
        *hip = MagazineHip::OffHand;
        return true;
    }
    if (value == "left")
    {
        *hip = MagazineHip::Left;
        return true;
    }
    if (value == "right")
    {
        *hip = MagazineHip::Right;
        return true;
    }
    return false;
}

inline bool ParseGrenadeBeltLayout(
    const std::string_view value,
    GrenadeBeltLayout* const layout)
{
    if (layout == nullptr)
    {
        return false;
    }
    if (value == "fixed")
    {
        *layout = GrenadeBeltLayout::Fixed;
        return true;
    }
    if (value == "handed")
    {
        *layout = GrenadeBeltLayout::Handed;
        return true;
    }
    return false;
}

inline bool ParseMeleeMode(
    const std::string_view value,
    MeleeMode* const mode)
{
    if (mode == nullptr)
    {
        return false;
    }
    if (value == "button")
    {
        *mode = MeleeMode::Button;
        return true;
    }
    if (value == "gesture")
    {
        *mode = MeleeMode::Gesture;
        return true;
    }
    if (value == "both")
    {
        *mode = MeleeMode::Both;
        return true;
    }
    return false;
}

inline std::string MirrorBindingHands(const std::string_view binding)
{
    std::string mirrored(binding);
    std::size_t position = 0u;
    while (position < mirrored.size())
    {
        const std::size_t termEnd = mirrored.find('+', position);
        const std::size_t length =
            termEnd == std::string::npos
                ? mirrored.size() - position
                : termEnd - position;

        if (length >= 5u &&
            mirrored.compare(position, 5u, "left.") == 0)
        {
            mirrored.replace(position, 5u, "right.");
            position += 6u;
        }
        else if (length >= 6u &&
                 mirrored.compare(position, 6u, "right.") == 0)
        {
            mirrored.replace(position, 6u, "left.");
            position += 5u;
        }
        else
        {
            position += length;
        }

        if (termEnd == std::string::npos)
        {
            break;
        }
        position = mirrored.find('+', position);
        if (position == std::string::npos)
        {
            break;
        }
        ++position;
    }
    return mirrored;
}

inline float MagazineHipCenter(
    const DominantHand dominantHand,
    const MagazineHip hip,
    const float distanceFromCenter)
{
    switch (hip)
    {
    case MagazineHip::Left:
        return distanceFromCenter;
    case MagazineHip::Right:
        return -distanceFromCenter;
    case MagazineHip::OffHand:
    default:
        return dominantHand == DominantHand::Left
            ? -distanceFromCenter
            : distanceFromCenter;
    }
}

inline bool FragUsesLeftHip(
    const DominantHand dominantHand,
    const GrenadeBeltLayout layout)
{
    return layout == GrenadeBeltLayout::Fixed ||
        dominantHand == DominantHand::Right;
}

inline bool MeleeGestureQualifies(
    const float velocity[3],
    const float weaponForward[3],
    const float minimumSpeed,
    const float minimumForwardFraction,
    float* const measuredSpeed = nullptr,
    float* const measuredForwardFraction = nullptr)
{
    if (velocity == nullptr || weaponForward == nullptr ||
        !std::isfinite(minimumSpeed) || minimumSpeed <= 0.0f ||
        !std::isfinite(minimumForwardFraction) ||
        minimumForwardFraction < 0.0f || minimumForwardFraction > 1.0f)
    {
        return false;
    }

    const float speed = std::sqrt(
        velocity[0] * velocity[0] +
        velocity[1] * velocity[1] +
        velocity[2] * velocity[2]);
    const float forwardLength = std::sqrt(
        weaponForward[0] * weaponForward[0] +
        weaponForward[1] * weaponForward[1] +
        weaponForward[2] * weaponForward[2]);

    if (measuredSpeed != nullptr)
    {
        *measuredSpeed = speed;
    }
    if (!std::isfinite(speed) || !std::isfinite(forwardLength) ||
        speed <= 0.0001f || forwardLength <= 0.0001f)
    {
        if (measuredForwardFraction != nullptr)
        {
            *measuredForwardFraction = 0.0f;
        }
        return false;
    }

    const float forwardFraction =
        (velocity[0] * weaponForward[0] +
         velocity[1] * weaponForward[1] +
         velocity[2] * weaponForward[2]) /
        (speed * forwardLength);
    if (measuredForwardFraction != nullptr)
    {
        *measuredForwardFraction = forwardFraction;
    }
    return speed >= minimumSpeed &&
        forwardFraction >= minimumForwardFraction;
}

inline float EffectiveHapticAmplitude(
    const bool enabled,
    const float eventAmplitude,
    const float configuredStrength)
{
    if (!enabled || !std::isfinite(eventAmplitude) ||
        !std::isfinite(configuredStrength))
    {
        return 0.0f;
    }
    return std::clamp(eventAmplitude * configuredStrength, 0.0f, 1.0f);
}

} // namespace kisak::vr::interactions
