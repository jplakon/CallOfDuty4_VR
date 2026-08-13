#pragma once

#include <cmath>
#include <cstdint>

namespace kisak::vr::gestures
{

// Positions use OpenXR's head-local metric basis: +X right, +Y up, and
// -Z forward. Keeping the detector in this runtime-neutral basis lets OpenXR
// and OpenVR feed the same gesture state machine.
struct HeadLocalPosition
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class NightVisionVisorDirection
{
    None,
    Lower,
    Raise,
};

struct NightVisionVisorState
{
    bool gripWasHeld = false;
    bool consumeUntilRelease = false;
    NightVisionVisorDirection direction =
        NightVisionVisorDirection::None;
    std::uint32_t pressStartedMilliseconds = 0u;
    HeadLocalPosition start = {};
    HeadLocalPosition last = {};
    float minimumY = 0.0f;
    float maximumY = 0.0f;
    bool destinationReached = false;
};

struct NightVisionVisorUpdate
{
    bool available = false;
    bool consumeLeftGrip = false;
    bool armedThisFrame = false;
    bool cancelledThisFrame = false;
    bool toggledThisFrame = false;
    NightVisionVisorDirection completedDirection =
        NightVisionVisorDirection::None;
};

constexpr float kNightVisionVisorMinimumTravelMeters = 0.12f;
constexpr std::uint32_t kNightVisionVisorMaximumHoldMilliseconds = 3500u;

inline bool IsFinite(const HeadLocalPosition position)
{
    return std::isfinite(position.x) &&
        std::isfinite(position.y) &&
        std::isfinite(position.z);
}

inline bool IsNightVisionCrownZone(
    const HeadLocalPosition position)
{
    return IsFinite(position) &&
        std::abs(position.x) <= 0.34f &&
        position.y >= 0.08f &&
        position.y <= 0.36f &&
        position.z >= -0.25f &&
        position.z <= 0.18f;
}

inline bool IsNightVisionVisorZone(
    const HeadLocalPosition position)
{
    return IsFinite(position) &&
        std::abs(position.x) <= 0.34f &&
        position.y >= -0.22f &&
        position.y <= 0.06f &&
        position.z >= -0.42f &&
        position.z <= -0.06f;
}

// Raising the visor begins close to the face. Keep this arming zone tighter
// than the lowering destination so a rifle foregrip below and farther in
// front of the headset cannot reserve the physical left grip. The ellipsoid
// is centered about 10 cm in front of the eyes.
inline bool IsNightVisionVisorStartZone(
    const HeadLocalPosition position)
{
    if (!IsFinite(position) ||
        position.z < -0.22f ||
        position.z > 0.02f)
    {
        return false;
    }

    const float normalizedX =
        position.x / 0.30f;
    const float normalizedY =
        (position.y + 0.01f) / 0.11f;
    const float normalizedZ =
        (position.z + 0.10f) / 0.12f;

    return normalizedX * normalizedX +
        normalizedY * normalizedY +
        normalizedZ * normalizedZ <= 1.0f;
}

inline bool IsNightVisionTravelEnvelope(
    const HeadLocalPosition position)
{
    return IsFinite(position) &&
        std::abs(position.x) <= 0.55f &&
        position.y >= -0.45f &&
        position.y <= 0.55f &&
        position.z >= -0.70f &&
        position.z <= 0.40f;
}

inline void ResetNightVisionVisorMotion(
    NightVisionVisorState* const state)
{
    if (state == nullptr)
    {
        return;
    }

    state->direction = NightVisionVisorDirection::None;
    state->pressStartedMilliseconds = 0u;
    state->start = {};
    state->last = {};
    state->minimumY = 0.0f;
    state->maximumY = 0.0f;
    state->destinationReached = false;
}

inline void SampleNightVisionVisorMotion(
    NightVisionVisorState* const state,
    const HeadLocalPosition position)
{
    if (state == nullptr ||
        state->direction == NightVisionVisorDirection::None)
    {
        return;
    }

    state->last = position;
    state->minimumY =
        position.y < state->minimumY
            ? position.y
            : state->minimumY;
    state->maximumY =
        position.y > state->maximumY
            ? position.y
            : state->maximumY;

    if (state->direction ==
        NightVisionVisorDirection::Lower)
    {
        state->destinationReached =
            state->destinationReached ||
            IsNightVisionVisorZone(position);
    }
    else if (state->direction ==
             NightVisionVisorDirection::Raise)
    {
        state->destinationReached =
            state->destinationReached ||
            IsNightVisionCrownZone(position);
    }
}

inline NightVisionVisorUpdate UpdateNightVisionVisorGesture(
    NightVisionVisorState* const state,
    const bool gameplayAllowed,
    const bool gripAvailable,
    const bool gripHeld,
    const bool poseValid,
    const HeadLocalPosition position,
    const std::uint32_t nowMilliseconds)
{
    NightVisionVisorUpdate update;
    if (state == nullptr)
    {
        return update;
    }

    const bool contactHeld = gripAvailable && gripHeld;
    update.available = gameplayAllowed &&
        gripAvailable &&
        poseValid &&
        IsFinite(position);

    if (!update.available)
    {
        if (state->direction !=
            NightVisionVisorDirection::None)
        {
            update.cancelledThisFrame = true;
            ResetNightVisionVisorMotion(state);
        }

        if (!contactHeld)
        {
            state->consumeUntilRelease = false;
        }

        // A grip that began while gameplay or tracking was unavailable must
        // be released before it can become a gesture after tracking returns.
        state->gripWasHeld = contactHeld;
        update.consumeLeftGrip =
            state->consumeUntilRelease && contactHeld;
        return update;
    }

    const bool contactBegan =
        contactHeld && !state->gripWasHeld;

    if (!contactHeld)
    {
        if (state->direction !=
            NightVisionVisorDirection::None)
        {
            SampleNightVisionVisorMotion(state, position);

            const std::uint32_t heldMilliseconds =
                nowMilliseconds -
                state->pressStartedMilliseconds;

            const bool withinTime =
                heldMilliseconds <=
                kNightVisionVisorMaximumHoldMilliseconds;

            const bool traveledEnough =
                state->direction ==
                        NightVisionVisorDirection::Lower
                    ? state->start.y - state->minimumY >=
                          kNightVisionVisorMinimumTravelMeters
                    : state->maximumY - state->start.y >=
                          kNightVisionVisorMinimumTravelMeters;

            if (withinTime &&
                state->destinationReached &&
                traveledEnough)
            {
                update.toggledThisFrame = true;
                update.completedDirection =
                    state->direction;
            }
            else
            {
                update.cancelledThisFrame = true;
            }
        }

        ResetNightVisionVisorMotion(state);
        state->consumeUntilRelease = false;
        state->gripWasHeld = false;
        return update;
    }

    if (contactBegan)
    {
        NightVisionVisorDirection direction =
            NightVisionVisorDirection::None;

        if (IsNightVisionCrownZone(position))
        {
            direction = NightVisionVisorDirection::Lower;
        }
        else if (IsNightVisionVisorStartZone(position))
        {
            direction = NightVisionVisorDirection::Raise;
        }

        if (direction !=
            NightVisionVisorDirection::None)
        {
            state->direction = direction;
            state->consumeUntilRelease = true;
            state->pressStartedMilliseconds =
                nowMilliseconds;
            state->start = position;
            state->last = position;
            state->minimumY = position.y;
            state->maximumY = position.y;
            state->destinationReached = false;
            update.armedThisFrame = true;
        }
    }
    else if (state->direction !=
             NightVisionVisorDirection::None)
    {
        const std::uint32_t heldMilliseconds =
            nowMilliseconds -
            state->pressStartedMilliseconds;

        if (heldMilliseconds >
                kNightVisionVisorMaximumHoldMilliseconds ||
            !IsNightVisionTravelEnvelope(position))
        {
            update.cancelledThisFrame = true;
            ResetNightVisionVisorMotion(state);
        }
        else
        {
            SampleNightVisionVisorMotion(state, position);
        }
    }

    state->gripWasHeld = contactHeld;
    update.consumeLeftGrip =
        state->consumeUntilRelease && contactHeld;
    return update;
}

} // namespace kisak::vr::gestures
