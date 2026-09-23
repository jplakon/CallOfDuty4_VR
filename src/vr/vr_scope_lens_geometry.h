#pragma once

#include <algorithm>
#include <cmath>

namespace kisak::vr::scope_geometry
{

// Input vertices are already skinned into pose-local WORLD space. Measure
// their extents along the weapon's forward/left/up axes, not world Y/Z: the
// latter makes a fixed-size lens shrink as the player turns the rifle.
class LensBounds
{
public:
    explicit LensBounds(const float weaponAxis[3][3])
    {
        if (weaponAxis == nullptr)
        {
            return;
        }

        for (int row = 0; row < 3; ++row)
        {
            double lengthSquared = 0.0;
            for (int component = 0; component < 3; ++component)
            {
                const double value = weaponAxis[row][component];
                if (!std::isfinite(value))
                {
                    return;
                }
                lengthSquared += value * value;
            }
            if (lengthSquared <= 0.000001)
            {
                return;
            }
            const double inverseLength = 1.0 / std::sqrt(lengthSquared);
            for (int component = 0; component < 3; ++component)
            {
                axis_[row][component] =
                    weaponAxis[row][component] * inverseLength;
            }
        }

        // A rotation basis is required to reconstruct the center. Reject an
        // unusable pose rather than manufacturing an incorrectly placed lens.
        for (int row = 0; row < 3; ++row)
        {
            for (int other = row + 1; other < 3; ++other)
            {
                double dot = 0.0;
                for (int component = 0; component < 3; ++component)
                {
                    dot += axis_[row][component] * axis_[other][component];
                }
                if (std::abs(dot) > 0.001)
                {
                    return;
                }
            }
        }
        valid_ = true;
    }

    bool AddPoint(const float point[3])
    {
        if (!valid_ || point == nullptr)
        {
            valid_ = false;
            return false;
        }
        for (int component = 0; component < 3; ++component)
        {
            if (!std::isfinite(point[component]))
            {
                valid_ = false;
                return false;
            }
        }
        if (!hasPoints_)
        {
            for (int component = 0; component < 3; ++component)
            {
                reference_[component] = point[component];
            }
            hasPoints_ = true;
            return true;
        }

        // Subtract a nearby reference before the projection to avoid losing
        // small lens extents to cancellation far from the map's world origin.
        for (int row = 0; row < 3; ++row)
        {
            double projected = 0.0;
            for (int component = 0; component < 3; ++component)
            {
                projected +=
                    (static_cast<double>(point[component]) - reference_[component]) *
                    axis_[row][component];
            }
            minimum_[row] = (std::min)(minimum_[row], projected);
            maximum_[row] = (std::max)(maximum_[row], projected);
        }
        return true;
    }

    bool Measure(float centerPoseLocal[3], float* radiusGameUnits) const
    {
        if (!valid_ || !hasPoints_ || centerPoseLocal == nullptr ||
            radiusGameUnits == nullptr)
        {
            return false;
        }
        const double radius = 0.25 *
            (maximum_[1] - minimum_[1] + maximum_[2] - minimum_[2]);
        if (!std::isfinite(radius) || radius <= 0.000001)
        {
            return false;
        }
        for (int component = 0; component < 3; ++component)
        {
            double center = reference_[component];
            for (int row = 0; row < 3; ++row)
            {
                center += 0.5 * (minimum_[row] + maximum_[row]) *
                    axis_[row][component];
            }
            centerPoseLocal[component] = static_cast<float>(center);
        }
        *radiusGameUnits = static_cast<float>(radius);
        return true;
    }

private:
    double axis_[3][3] = {};
    double reference_[3] = {};
    double minimum_[3] = {};
    double maximum_[3] = {};
    bool valid_ = false;
    bool hasPoints_ = false;
};

} // namespace kisak::vr::scope_geometry
