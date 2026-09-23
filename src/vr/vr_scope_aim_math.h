#pragma once

#include <cmath>

namespace kisak::vr::scope_aim
{

// Turn the physical muzzle toward the target already selected by the scope
// trace. This does not move the muzzle, select a new target, or bypass cover.
// Keep the engine's existing 0.0001 game-unit degeneracy threshold and
// reciprocal-length normalization. Failed calculations leave forward intact.
inline bool BuildConvergedForward(
    const float muzzleOrigin[3],
    const float target[3],
    float forward[3])
{
    if (muzzleOrigin == nullptr || target == nullptr || forward == nullptr)
    {
        return false;
    }

    const float delta[3] = {
        target[0] - muzzleOrigin[0],
        target[1] - muzzleOrigin[1],
        target[2] - muzzleOrigin[2],
    };
    const float length = std::sqrt(
        delta[0] * delta[0] +
        delta[1] * delta[1] +
        delta[2] * delta[2]);

    if (!std::isfinite(length) || length <= 0.0001f)
    {
        return false;
    }

    const float inverseLength = 1.0f / length;
    forward[0] = delta[0] * inverseLength;
    forward[1] = delta[1] * inverseLength;
    forward[2] = delta[2] * inverseLength;
    return true;
}

} // namespace kisak::vr::scope_aim
