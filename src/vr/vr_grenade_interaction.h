#pragma once

#include <cmath>
#include <cstdint>

namespace kisak::vr::grenade_interaction
{
// OpenXR tracking space is gravity-aligned (+Y up, -Z forward). Only the
// horizontal head heading defines belt forward/left; head pitch/roll must not
// lift or swing a stationary hip outside its pickup zone. Output is COD units.
inline bool ProjectLevelBeltPosition(
    const float trackingOffset[3], const float headForward[3],
    const float unitsPerMeter, float position[3])
{
    if (position == nullptr)
        return false;
    position[0] = position[1] = position[2] = 0.0f;
    if (trackingOffset == nullptr || headForward == nullptr ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0f)
        return false;
    for (int component = 0; component < 3; ++component)
        if (!std::isfinite(trackingOffset[component]) ||
            !std::isfinite(headForward[component]))
            return false;
    const float horizontalSquared = headForward[0] * headForward[0] +
        headForward[2] * headForward[2];
    if (!std::isfinite(horizontalSquared) || horizontalSquared < 0.0001f)
        return false; // Looking straight up/down has no reliable heading.
    const float inverseLength = 1.0f / std::sqrt(horizontalSquared);
    const float forwardX = headForward[0] * inverseLength;
    const float forwardZ = headForward[2] * inverseLength;
    position[0] = (trackingOffset[0] * forwardX +
                   trackingOffset[2] * forwardZ) * unitsPerMeter;
    position[1] = (trackingOffset[0] * forwardZ -
                   trackingOffset[2] * forwardX) * unitsPerMeter;
    position[2] = trackingOffset[1] * unitsPerMeter;
    return std::isfinite(position[0]) && std::isfinite(position[1]) &&
        std::isfinite(position[2]);
}

struct BeltSettings
{
    float forwardOffset = 0.0f;
    float height = -28.0f;
    float hipDistance = 13.0f;
    float radius = 11.0f;
};

struct BeltZones
{
    bool left = false;
    bool right = false;
};

inline BeltZones FindBeltZones(
    const float position[3], const bool poseValid,
    const BeltSettings& settings = {})
{
    if (!poseValid || position == nullptr ||
        !std::isfinite(settings.forwardOffset) ||
        !std::isfinite(settings.height) ||
        !std::isfinite(settings.hipDistance) ||
        !std::isfinite(settings.radius) || settings.radius < 0.0f ||
        settings.hipDistance <= settings.radius)
        return {};
    for (int component = 0; component < 3; ++component)
        if (!std::isfinite(position[component]))
            return {};
    if (position[0] < settings.forwardOffset - 18.0f ||
        position[0] > settings.forwardOffset + 18.0f ||
        position[2] < settings.height - 14.0f ||
        position[2] > settings.height + 14.0f)
        return {};
    const float minimum = settings.hipDistance - settings.radius;
    const float maximum = settings.hipDistance + settings.radius;
    return {position[1] >= minimum && position[1] <= maximum,
            position[1] <= -minimum && position[1] >= -maximum};
}

struct GripState
{
    bool initialized = false;
    bool wasHeld = false;
    bool inputUnavailable = false;
    bool releasePending = false;
    std::uint32_t releasedAt = 0u;
};

// Chords and alternative bindings need three-valued input: an unavailable
// source is not a released source. Any known-false chord term proves false;
// otherwise every term must be available before its state is known.
struct ChordAvailability
{
    bool allAvailable = true;
    bool knownFalse = false;
    void Observe(const bool available, const bool held)
    {
        allAvailable = allAvailable && available;
        knownFalse = knownFalse || (available && !held);
    }
    bool Known() const { return allAvailable || knownFalse; }
};

struct BindingState
{
    std::uint32_t armed = 0u;
    std::uint32_t acceptedHeld = 0u;
};

struct BindingUpdate
{
    bool held = false;
    bool available = false;
};

inline BindingUpdate ResolveObjectBindings(
    BindingState* state, const std::uint32_t configuredBindings,
    const std::uint32_t knownBindings, const std::uint32_t heldBindings)
{
    if (state == nullptr)
        return {};
    const std::uint32_t configured = configuredBindings & 3u;
    const std::uint32_t known = knownBindings & configured;
    const std::uint32_t held = heldBindings & known;
    state->armed &= configured;
    state->acceptedHeld &= configured;
    for (std::uint32_t bit = 1u; bit <= 2u; bit <<= 1u)
    {
        if (!(known & bit))
        {
            // A held owner remains held through uncertainty; an idle source
            // must be seen released again before recovery can start a grab.
            state->armed &= ~bit;
        }
        else if (!(held & bit))
        {
            state->acceptedHeld &= ~bit;
            state->armed |= bit;
        }
        else if (state->armed & bit)
        {
            state->acceptedHeld |= bit;
        }
    }
    const bool knownOwnerHeld = (state->acceptedHeld & known) != 0u;
    const bool unknownOwnerHeld = (state->acceptedHeld & ~known) != 0u;
    return {state->acceptedHeld != 0u,
            knownOwnerHeld || (!unknownOwnerHeld && known != 0u)};
}

struct GripUpdate
{
    bool grabPressed = false;
    bool release = false;
    bool lastValidPoseFallback = false;
};

inline void ObserveDisabledGrip(
    GripState* state, const bool held, const bool available)
{
    if (state == nullptr)
        return;
    *state = {};
    state->initialized = available;
    state->wasHeld = held;
    state->inputUnavailable = !available;
}

// Input availability is independent of pose availability. Losing tracking or
// an inactive action must never manufacture a release/press edge. A confirmed
// release waits briefly for a fresh pose; if tracking does not recover, use
// the existing last valid held origin instead of holding a live grenade forever.
inline GripUpdate UpdateGrip(
    GripState* state, const bool held, const bool inputAvailable,
    const bool poseValid, const bool holdingGrenade,
    const std::uint32_t nowMilliseconds)
{
    GripUpdate result;
    if (state == nullptr)
        return result;
    bool pressed = false;
    if (inputAvailable)
    {
        pressed = state->initialized && !state->inputUnavailable &&
            held && !state->wasHeld;
        state->wasHeld = held;
        state->initialized = true;
        state->inputUnavailable = false;
    }
    else
    {
        state->inputUnavailable = true;
    }
    if (!holdingGrenade)
    {
        state->releasePending = false;
        result.grabPressed = pressed && poseValid;
        return result;
    }
    if (inputAvailable && !held && !state->releasePending)
    {
        state->releasePending = true;
        state->releasedAt = nowMilliseconds;
    }
    constexpr std::uint32_t maximumReleasePoseWaitMilliseconds = 150u;
    if (state->releasePending &&
        (poseValid || nowMilliseconds - state->releasedAt >=
                          maximumReleasePoseWaitMilliseconds))
    {
        result.release = true;
        result.lastValidPoseFallback = !poseValid;
        state->releasePending = false;
    }
    return result;
}
} // namespace kisak::vr::grenade_interaction
