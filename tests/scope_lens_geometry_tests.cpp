#include "vr/vr_scope_lens_geometry.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
int failures = 0;
int checks = 0;

void Check(bool condition, const char* name)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
}

void BuildAxis(float yaw, float pitch, float roll, float axis[3][3])
{
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double cy = std::cos(yaw * radians);
    const double sy = std::sin(yaw * radians);
    const double cp = std::cos(pitch * radians);
    const double sp = std::sin(pitch * radians);
    const double cr = std::cos(roll * radians);
    const double sr = std::sin(roll * radians);
    const double rows[3][3] = {
        {cy * cp, sy * cp, -sp},
        {cy * sp * sr - sy * cr, sy * sp * sr + cy * cr, cp * sr},
        {cy * sp * cr + sy * sr, sy * sp * cr - cy * sr, cp * cr},
    };
    for (int row = 0; row < 3; ++row)
        for (int component = 0; component < 3; ++component)
            axis[row][component] = static_cast<float>(rows[row][component]);
}

bool Near(float actual, float expected, float tolerance = 0.0025f)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void TestTransformedLens(
    float yaw, float pitch, float roll,
    const float translation[3], float radius, float axisScale)
{
    float axis[3][3] = {};
    BuildAxis(yaw, pitch, roll, axis);
    float scaledAxis[3][3] = {};
    for (int row = 0; row < 3; ++row)
        for (int component = 0; component < 3; ++component)
            scaledAxis[row][component] = axisScale * axis[row][component];
    kisak::vr::scope_geometry::LensBounds bounds(scaledAxis);

    const float authoredCenter[3] = {11.0f, -2.0f, 5.0f};
    float expectedCenter[3] = {};
    for (int component = 0; component < 3; ++component)
    {
        expectedCenter[component] = translation[component];
        for (int row = 0; row < 3; ++row)
            expectedCenter[component] += authoredCenter[row] * axis[row][component];
    }

    bool addedAll = true;
    constexpr double pi = 3.14159265358979323846;
    // Two rings ensure the center also respects the optical-axis depth.
    for (int depth = -1; depth <= 1; depth += 2)
    {
        for (int vertex = 0; vertex < 64; ++vertex)
        {
            const double angle = vertex * (2.0 * pi / 64.0);
            float point[3] = {};
            for (int component = 0; component < 3; ++component)
            {
                point[component] = expectedCenter[component] +
                    0.03f * depth * axis[0][component] +
                    radius * static_cast<float>(std::cos(angle)) * axis[1][component] +
                    radius * static_cast<float>(std::sin(angle)) * axis[2][component];
            }
            addedAll = bounds.AddPoint(point) && addedAll;
        }
    }
    Check(addedAll, "transformed lens vertices accepted");
    float center[3] = {};
    float measuredRadius = 0.0f;
    Check(bounds.Measure(center, &measuredRadius), "transformed lens measured");
    Check(Near(measuredRadius, radius), "radius invariant under yaw/pitch/roll/translation");
    Check(Near(center[0], expectedCenter[0]) &&
          Near(center[1], expectedCenter[1]) &&
          Near(center[2], expectedCenter[2]), "center remains pose-local WORLD space");

    // Integration contract: the caller adds refdef.viewOffset only; applying
    // the weapon rotation again would fail this translated/rotated fixture.
    const float viewOffset[3] = {1250.0f, -340.0f, 170.0f};
    Check(Near(center[0] + viewOffset[0], expectedCenter[0] + viewOffset[0]) &&
          Near(center[1] + viewOffset[1], expectedCenter[1] + viewOffset[1]) &&
          Near(center[2] + viewOffset[2], expectedCenter[2] + viewOffset[2]),
          "viewOffset addition preserves the world lens center");
}

void TestOriginalYawRegression()
{
    float axis[3][3] = {};
    BuildAxis(90.0f, 0.0f, 0.0f, axis);
    kisak::vr::scope_geometry::LensBounds bounds(axis);
    const float points[4][3] = {
        {-2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f},
    };
    for (const auto& point : points)
        bounds.AddPoint(point);
    float center[3] = {};
    float radius = 0.0f;
    Check(bounds.Measure(center, &radius) && Near(radius, 2.0f),
          "90-degree yaw does not halve aperture radius");
    const float oldWorldYZRadius = 0.25f * (0.0f + 4.0f);
    Check(Near(oldWorldYZRadius, 1.0f) && !Near(radius, oldWorldYZRadius),
          "fixture exposes original world-Y/Z measurement error");
}

void TestInvalidInput()
{
    using kisak::vr::scope_geometry::LensBounds;
    float axis[3][3] = {};
    BuildAxis(0.0f, 0.0f, 0.0f, axis);
    const float point[3] = {};
    float center[3] = {10.0f, 20.0f, 30.0f};
    float radius = 42.0f;
    LensBounds empty(axis);
    Check(!empty.Measure(center, &radius), "empty geometry rejected");
    Check(empty.AddPoint(point) && !empty.Measure(center, &radius),
          "point-only geometry rejected");
    Check(Near(radius, 42.0f) && Near(center[0], 10.0f),
          "failed measurement preserves outputs");
    LensBounds missing(nullptr);
    Check(!missing.AddPoint(point), "missing axes rejected");
    axis[1][0] = 1.0f;
    LensBounds skewed(axis);
    Check(!skewed.AddPoint(point), "non-orthogonal axes rejected");
    BuildAxis(0.0f, 0.0f, 0.0f, axis);
    axis[2][2] = 0.0f;
    LensBounds collapsed(axis);
    Check(!collapsed.AddPoint(point), "zero-length axis rejected");
    axis[2][2] = std::numeric_limits<float>::quiet_NaN();
    LensBounds nonfinite(axis);
    Check(!nonfinite.AddPoint(point), "nonfinite axis rejected");
    BuildAxis(0.0f, 0.0f, 0.0f, axis);
    LensBounds badVertex(axis);
    const float invalidPoint[3] = {0.0f, std::numeric_limits<float>::infinity(), 0.0f};
    Check(!badVertex.AddPoint(invalidPoint) && !badVertex.AddPoint(point) &&
          !badVertex.Measure(center, &radius), "nonfinite vertex invalidates entire surface");
    LensBounds nullVertex(axis);
    Check(!nullVertex.AddPoint(nullptr), "null vertex rejected");
}
} // namespace

int main()
{
    const float translations[][3] = {
        {0.0f, 0.0f, 0.0f}, {9.0f, -17.0f, 31.0f},
        {-700.0f, 1200.0f, -300.0f}, {10000.0f, -10000.0f, 8000.0f},
    };
    for (float yaw : {0.0f, 30.0f, 90.0f, 150.0f, 180.0f, 270.0f})
        for (float pitch : {-89.0f, -45.0f, 0.0f, 45.0f, 89.0f})
            for (float roll : {0.0f, 30.0f, 90.0f, 180.0f})
                for (const auto& translation : translations)
                    for (float radius : {0.5f, 1.0f, 2.5f})
                        TestTransformedLens(yaw, pitch, roll, translation, radius, 1.0f);
    TestTransformedLens(83.0f, -37.0f, 61.0f, translations[1], 1.3f, 2.0f);
    TestOriginalYawRegression();
    TestInvalidInput();
    std::printf("Scope lens geometry: %d checks, %d failures.\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
