#include "vr/vr_scope_aim_math.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using Vec3 = std::array<double, 3>;

int failures = 0;
int checks = 0;

void Expect(const bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

Vec3 Rotate(const Vec3& value, const Vec3& angles)
{
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double yaw = angles[0] * radians;
    const double pitch = angles[1] * radians;
    const double roll = angles[2] * radians;
    const Vec3 rolled = {
        value[0],
        value[1] * std::cos(roll) - value[2] * std::sin(roll),
        value[1] * std::sin(roll) + value[2] * std::cos(roll),
    };
    const Vec3 pitched = {
        rolled[0] * std::cos(pitch) + rolled[2] * std::sin(pitch),
        rolled[1],
        -rolled[0] * std::sin(pitch) + rolled[2] * std::cos(pitch),
    };
    return {
        pitched[0] * std::cos(yaw) - pitched[1] * std::sin(yaw),
        pitched[0] * std::sin(yaw) + pitched[1] * std::cos(yaw),
        pitched[2],
    };
}

void CheckTargetIntersection(
    const double rangeMeters,
    const Vec3& scopeOrigin,
    const Vec3& angles,
    const Vec3& muzzleOffsetMeters)
{
    constexpr double gameUnitsPerMeter = 39.37007874015748;
    const Vec3 opticalForward = Rotate({1.0, 0.0, 0.0}, angles);
    const Vec3 rotatedOffset = Rotate(muzzleOffsetMeters, angles);
    float target[3] = {};
    float muzzle[3] = {};
    for (int component = 0; component < 3; ++component)
    {
        target[component] = static_cast<float>(
            scopeOrigin[component] + opticalForward[component] *
                rangeMeters * gameUnitsPerMeter);
        muzzle[component] = static_cast<float>(
            scopeOrigin[component] + rotatedOffset[component] *
                gameUnitsPerMeter);
    }

    float shotForward[3] = {};
    Expect(kisak::vr::scope_aim::BuildConvergedForward(
               muzzle, target, shotForward),
           "valid muzzle and scoped target must produce a shot direction");

    const double lengthSquared =
        static_cast<double>(shotForward[0]) * shotForward[0] +
        static_cast<double>(shotForward[1]) * shotForward[1] +
        static_cast<double>(shotForward[2]) * shotForward[2];
    Expect(std::abs(lengthSquared - 1.0) < 0.000002,
           "converged shot direction must be unit length");

    // Independent geometric oracle: intersect the returned shot ray with the
    // target plane whose normal is the optical axis, then measure the miss.
    double targetPlaneDistance = 0.0;
    double shotPlaneRate = 0.0;
    for (int component = 0; component < 3; ++component)
    {
        targetPlaneDistance +=
            (static_cast<double>(target[component]) - muzzle[component]) *
            opticalForward[component];
        shotPlaneRate += shotForward[component] * opticalForward[component];
    }
    Expect(shotPlaneRate > 0.0,
           "ordinary scoped target must remain in front of the muzzle");
    const double distanceAlongShot = targetPlaneDistance / shotPlaneRate;
    double missSquared = 0.0;
    for (int component = 0; component < 3; ++component)
    {
        const double miss = muzzle[component] +
            shotForward[component] * distanceAlongShot - target[component];
        missSquared += miss * miss;
    }
    Expect(std::sqrt(missSquared) < 0.01,
           "shot must intersect the scoped target within 0.01 game units");
}

void CheckRejectedTarget(const float muzzle[3], const float target[3])
{
    float forward[3] = {0.0f, 1.0f, 0.0f};
    Expect(!kisak::vr::scope_aim::BuildConvergedForward(
               muzzle, target, forward),
           "degenerate or nonfinite target must be rejected");
    Expect(forward[0] == 0.0f && forward[1] == 1.0f && forward[2] == 0.0f,
           "rejected target must preserve the existing firing direction");
}
} // namespace

int main()
{
    constexpr double rangesMeters[] = {2.0, 5.0, 20.0, 100.0};
    constexpr Vec3 rotations[] = {
        {0.0, 0.0, 0.0},
        {37.0, -23.0, 19.0},
        {-147.0, 61.0, -44.0},
        {91.0, -88.0, 127.0},
    };
    constexpr Vec3 muzzleOffsetsMeters[] = {
        {0.0, 0.0, 0.0},
        {0.6, 0.02, -0.06},
        {0.9, 0.12, -0.18},
        {-0.2, -0.08, 0.04},
    };
    constexpr Vec3 scopeOrigins[] = {
        {0.0, 0.0, 0.0},
        {1250.0, -740.0, 240.0},
    };

    int geometryCases = 0;
    for (const double range : rangesMeters)
    {
        for (const Vec3& rotation : rotations)
        {
            for (const Vec3& offset : muzzleOffsetsMeters)
            {
                for (const Vec3& origin : scopeOrigins)
                {
                    for (const double handedness : {-1.0, 1.0})
                    {
                        Vec3 mirroredOffset = offset;
                        mirroredOffset[1] *= handedness;
                        CheckTargetIntersection(
                            range, origin, rotation, mirroredOffset);
                        ++geometryCases;
                    }
                }
            }
        }
    }

    const float zero[3] = {};
    const float sameNonzero[3] = {24.0f, -8.0f, 16.0f};
    const float tooClose[3] = {0.00001f, 0.0f, 0.0f};
    const float atThreshold[3] = {0.0001f, 0.0f, 0.0f};
    const float aboveThreshold[3] = {0.00011f, 0.0f, 0.0f};
    const float infinity[3] = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
    const float nan[3] = {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
    CheckRejectedTarget(zero, zero);
    CheckRejectedTarget(sameNonzero, sameNonzero);
    CheckRejectedTarget(zero, tooClose);
    CheckRejectedTarget(zero, atThreshold);
    CheckRejectedTarget(zero, infinity);
    CheckRejectedTarget(zero, nan);

    float forward[3] = {};
    Expect(kisak::vr::scope_aim::BuildConvergedForward(
               zero, aboveThreshold, forward) &&
               std::abs(forward[0] - 1.0f) < 0.000001f,
           "target immediately above the existing threshold must remain valid");
    Expect(!kisak::vr::scope_aim::BuildConvergedForward(nullptr, zero, forward),
           "missing muzzle must be rejected");
    Expect(!kisak::vr::scope_aim::BuildConvergedForward(zero, nullptr, forward),
           "missing target must be rejected");
    Expect(!kisak::vr::scope_aim::BuildConvergedForward(zero, aboveThreshold, nullptr),
           "missing output must be rejected");

    if (failures != 0)
    {
        std::fprintf(stderr, "%d of %d scope aim checks failed.\n", failures, checks);
        return 1;
    }
    std::printf("All %d scope aim checks passed (%d geometry cases).\n",
                checks, geometryCases);
    return 0;
}
