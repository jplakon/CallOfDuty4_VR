#pragma once

#include <cmath>
#include <limits>

namespace kisak::vr::scope_projection
{

struct Vec3
{
    float x;
    float y;
    float z;
};

// dot(float3(eyeU, eyeV, 1), x/y/z.xyz) produces homogeneous lens
// coordinates. Divide X and Y by Z to obtain physical right/up offsets
// measured in lens radii. The fourth elements are padding for shader float4s.
struct Mapping
{
    float x[4] = {};
    float y[4] = {};
    float z[4] = {};
};

namespace detail
{
inline double Dot(Vec3 a, Vec3 b)
{
    return static_cast<double>(a.x) * b.x +
        static_cast<double>(a.y) * b.y +
        static_cast<double>(a.z) * b.z;
}

inline bool Finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

inline bool Unit(Vec3 value)
{
    return Finite(value) && std::abs(Dot(value, value) - 1.0) <= 0.002;
}
} // namespace detail

// Eye-local coordinates use +X right, +Y up, and -Z into the scene.
// Tangents are signed: left < right and down < up. scopeForward points
// through the lens away from its usable rear/eye side. All lengths use the
// same units. No projected-radius clamp is applied: a tilted circle is the
// projective image of its physical plane, rather than a screen-space ellipse.
// A failed build leaves output unchanged.
inline bool Build(
    Vec3 eyeToLens, Vec3 scopeRight, Vec3 scopeUp, Vec3 scopeForward,
    float radius, float tanLeft, float tanRight, float tanDown, float tanUp,
    Mapping& output)
{
    constexpr double epsilon = 0.000001;
    if (!detail::Finite(eyeToLens) || !detail::Unit(scopeRight) ||
        !detail::Unit(scopeUp) || !detail::Unit(scopeForward) ||
        !std::isfinite(radius) || radius <= epsilon ||
        !std::isfinite(tanLeft) || !std::isfinite(tanRight) ||
        !std::isfinite(tanDown) || !std::isfinite(tanUp) ||
        static_cast<double>(tanRight) - tanLeft <= epsilon ||
        static_cast<double>(tanUp) - tanDown <= epsilon ||
        eyeToLens.z >= -epsilon ||
        std::abs(detail::Dot(scopeRight, scopeUp)) > 0.002 ||
        std::abs(detail::Dot(scopeRight, scopeForward)) > 0.002 ||
        std::abs(detail::Dot(scopeUp, scopeForward)) > 0.002)
    {
        return false;
    }

    const double centerForward = detail::Dot(eyeToLens, scopeForward);
    if (!std::isfinite(centerForward) || centerForward <= epsilon)
    {
        // Eye lies in the lens plane or on its front/barrel side.
        return false;
    }

    const double eyeRight = -detail::Dot(eyeToLens, scopeRight);
    const double eyeUp = -detail::Dot(eyeToLens, scopeUp);
    const double eyeForward = -centerForward;
    const double spanX = static_cast<double>(tanRight) - tanLeft;
    const double spanY = static_cast<double>(tanDown) - tanUp;
    const auto rayDot = [=](Vec3 axis, int component)
    {
        if (component == 0)
            return spanX * axis.x;
        if (component == 1)
            return spanY * axis.y;
        return static_cast<double>(tanLeft) * axis.x +
            static_cast<double>(tanUp) * axis.y - axis.z;
    };

    Mapping candidate;
    for (int component = 0; component < 3; ++component)
    {
        const double forward = rayDot(scopeForward, component);
        const double right = (eyeRight * forward -
            eyeForward * rayDot(scopeRight, component)) / radius;
        const double up = (eyeUp * forward -
            eyeForward * rayDot(scopeUp, component)) / radius;
        const double maxFloat = (std::numeric_limits<float>::max)();
        if (!std::isfinite(right) || !std::isfinite(up) ||
            !std::isfinite(forward) || std::abs(right) > maxFloat ||
            std::abs(up) > maxFloat || std::abs(forward) > maxFloat)
        {
            return false;
        }
        candidate.x[component] = static_cast<float>(right);
        candidate.y[component] = static_cast<float>(up);
        candidate.z[component] = static_cast<float>(forward);
    }
    output = candidate;
    return true;
}

// CPU equivalent of the shader's physical lens intersection. Reject rays
// parallel to the plane or aimed away from it, including a nonfinite result.
// UVs outside [0,1] remain valid for tests and for partially visible lenses.
inline bool Evaluate(
    const Mapping& mapping, float eyeU, float eyeV,
    float& deltaRight, float& deltaUp)
{
    if (!std::isfinite(eyeU) || !std::isfinite(eyeV))
        return false;
    const auto affine = [=](const float value[4])
    {
        return value[0] * eyeU + value[1] * eyeV + value[2];
    };
    const float denominator = affine(mapping.z);
    if (!std::isfinite(denominator) || denominator <= 0.000001f)
        return false;
    const float x = affine(mapping.x) / denominator;
    const float y = affine(mapping.y) / denominator;
    if (!std::isfinite(x) || !std::isfinite(y))
        return false;
    deltaRight = x;
    deltaUp = y;
    return true;
}

} // namespace kisak::vr::scope_projection
