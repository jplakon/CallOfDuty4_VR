#pragma once

#include <cmath>
#include <cstdint>

namespace kisak::vr::pose_ads
{

struct Geometry
{
    bool valid = false;
    bool engage = false;
    bool retain = false;
    float headAlignment = -1.0f;
    float eyeLineDistance = 999.0f;
    float weaponHandForward = 0.0f;
    float weaponHandHeight = 0.0f;
};

// Inputs must be a coherent pair of current camera-local CoD positions
// (+forward, +left, +up), in game units/inches. The hand-to-hand line is only
// a shoulder-intent approximation, not the calibrated rendered weapon axis.
// Do not mix these positions with a previous frame's calibrated aim basis.
inline Geometry EvaluatePose(
    const float weaponGrip[3],
    const float supportGrip[3])
{
    Geometry result;
    if (weaponGrip == nullptr || supportGrip == nullptr)
    {
        return result;
    }
    for (int component = 0; component < 3; ++component)
    {
        if (!std::isfinite(weaponGrip[component]) ||
            !std::isfinite(supportGrip[component]))
        {
            return result;
        }
    }

    float forward[3] = {
        supportGrip[0] - weaponGrip[0],
        supportGrip[1] - weaponGrip[1],
        supportGrip[2] - weaponGrip[2],
    };
    const float length = std::sqrt(
        forward[0] * forward[0] +
        forward[1] * forward[1] +
        forward[2] * forward[2]);
    if (!std::isfinite(length) || length <= 0.0001f)
    {
        return result;
    }
    for (float& component : forward)
    {
        component /= length;
    }
    const float along =
        weaponGrip[0] * forward[0] +
        weaponGrip[1] * forward[1] +
        weaponGrip[2] * forward[2];
    const float perpendicular[3] = {
        weaponGrip[0] - along * forward[0],
        weaponGrip[1] - along * forward[1],
        weaponGrip[2] - along * forward[2],
    };
    result.headAlignment = forward[0];
    result.eyeLineDistance = std::sqrt(
        perpendicular[0] * perpendicular[0] +
        perpendicular[1] * perpendicular[1] +
        perpendicular[2] * perpendicular[2]);
    result.weaponHandForward = weaponGrip[0];
    result.weaponHandHeight = weaponGrip[2];
    if (!std::isfinite(result.eyeLineDistance))
    {
        return result;
    }

    result.valid = true;
    // The former 15/20-inch eye-line radius admitted ordinary chest carry.
    // Separate a deliberate eye-level shoulder pose from support grip alone.
    result.engage =
        result.headAlignment >= 0.80f &&
        result.eyeLineDistance <= 6.0f &&
        result.weaponHandForward >= 0.0f &&
        result.weaponHandForward <= 24.0f &&
        result.weaponHandHeight >= -7.0f &&
        result.weaponHandHeight <= 6.0f;
    result.retain =
        result.headAlignment >= 0.68f &&
        result.eyeLineDistance <= 8.0f &&
        result.weaponHandForward >= -2.0f &&
        result.weaponHandForward <= 30.0f &&
        result.weaponHandHeight >= -9.0f &&
        result.weaponHandHeight <= 8.0f;
    return result;
}

struct Settings
{
    std::uint32_t engageMilliseconds = 250u;
    std::uint32_t releaseMilliseconds = 180u;
};

struct Inputs
{
    bool enabled = true;
    bool gameplayAllowed = true;
    bool supportHeld = false;
    bool poseAvailable = false;
    bool sprintRequested = false;
    bool sprintActive = false;
    std::uint32_t nowMilliseconds = 0u;
    Geometry geometry;
};

struct State
{
    bool held = false;
    bool engagePending = false;
    bool releasePending = false;
    std::uint32_t engageStartedMilliseconds = 0u;
    std::uint32_t releaseStartedMilliseconds = 0u;
    bool sprintSuppressed = false;
    bool sprintPriorityActive = false;
    bool lowerPending = false;
    std::uint32_t lowerStartedMilliseconds = 0u;
};

inline void ClearPose(State* state)
{
    state->held = false;
    state->engagePending = false;
    state->releasePending = false;
}

// Resolves automatic pose ADS only. The caller keeps the explicit Aim action
// separate and combines it afterward; this helper never claims locomotion.
inline bool Update(
    State* state,
    const Inputs& input,
    const Settings& settings = {})
{
    if (state == nullptr)
    {
        return false;
    }
    if (!input.enabled || !input.gameplayAllowed || !input.supportHeld)
    {
        *state = {};
        return false;
    }

    const bool sprintPriority = input.sprintRequested || input.sprintActive;
    if (sprintPriority && !state->sprintPriorityActive)
    {
        state->sprintSuppressed = true;
        state->lowerPending = false;
    }
    state->sprintPriorityActive = sprintPriority;
    if (sprintPriority)
    {
        ClearPose(state);
    }

    if (!input.poseAvailable || !input.geometry.valid)
    {
        ClearPose(state);
        // Tracking loss is not a deliberate lowering gesture. Preserve an
        // existing sprint veto even after the initiating click was released.
        state->lowerPending = false;
        return false;
    }

    if (state->sprintSuppressed)
    {
        ClearPose(state);
        if (input.geometry.retain)
        {
            state->lowerPending = false;
        }
        else
        {
            if (!state->lowerPending)
            {
                state->lowerPending = true;
                state->lowerStartedMilliseconds = input.nowMilliseconds;
            }
            if (input.nowMilliseconds - state->lowerStartedMilliseconds >=
                settings.releaseMilliseconds)
            {
                state->sprintSuppressed = false;
                state->lowerPending = false;
            }
        }
        return false;
    }
    // Lowering may rearm during native sprint, but never enter ADS until the
    // native sprint/request ends and the complete shoulder dwell has elapsed.
    if (sprintPriority)
    {
        return false;
    }

    if (!state->held)
    {
        state->releasePending = false;
        if (!input.geometry.engage)
        {
            state->engagePending = false;
            return false;
        }
        if (!state->engagePending)
        {
            state->engagePending = true;
            state->engageStartedMilliseconds = input.nowMilliseconds;
        }
        if (input.nowMilliseconds - state->engageStartedMilliseconds >=
            settings.engageMilliseconds)
        {
            state->held = true;
            state->engagePending = false;
        }
    }
    else
    {
        state->engagePending = false;
        if (input.geometry.retain)
        {
            state->releasePending = false;
        }
        else
        {
            if (!state->releasePending)
            {
                state->releasePending = true;
                state->releaseStartedMilliseconds = input.nowMilliseconds;
            }
            if (input.nowMilliseconds - state->releaseStartedMilliseconds >=
                settings.releaseMilliseconds)
            {
                state->held = false;
                state->releasePending = false;
            }
        }
    }
    return state->held;
}

} // namespace kisak::vr::pose_ads
