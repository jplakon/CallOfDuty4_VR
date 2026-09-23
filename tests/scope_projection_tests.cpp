#include "vr/vr_scope_projection.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
using namespace kisak::vr::scope_projection;
int checks = 0;
int failures = 0;

void Check(bool condition, const char* name)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        if (failures < 20)
            std::fprintf(stderr, "FAIL: %s\n", name);
    }
}

bool Near(float a, float b, float tolerance = 0.00015f)
{
    return std::isfinite(a) && std::abs(a - b) <= tolerance;
}

Vec3 Rotate(Vec3 p, float yaw, float pitch, float roll)
{
    constexpr double degrees = 3.14159265358979323846 / 180.0;
    const double cr = std::cos(roll * degrees), sr = std::sin(roll * degrees);
    const double cp = std::cos(pitch * degrees), sp = std::sin(pitch * degrees);
    const double cy = std::cos(yaw * degrees), sy = std::sin(yaw * degrees);
    const double x1 = p.x * cr - p.y * sr;
    const double y1 = p.x * sr + p.y * cr;
    const double y2 = y1 * cp - p.z * sp;
    const double z2 = y1 * sp + p.z * cp;
    return {static_cast<float>(x1 * cy + z2 * sy),
            static_cast<float>(y2),
            static_cast<float>(-x1 * sy + z2 * cy)};
}

void PhysicalRoundTrip(
    float yaw, float pitch, float roll, float ipdEyeX, float relief,
    float radius, bool asymmetric)
{
    const Vec3 right = Rotate({1, 0, 0}, yaw, pitch, roll);
    const Vec3 up = Rotate({0, 1, 0}, yaw, pitch, roll);
    const Vec3 forward = Rotate({0, 0, -1}, yaw, pitch, roll);
    // The eye moves laterally (IPD) and vertically relative to a fixed lens.
    const Vec3 center = {forward.x * relief - ipdEyeX,
                         forward.y * relief + 0.013f,
                         forward.z * relief};
    const float left = asymmetric ? -1.7983f : -1.0f;
    const float rightTan = asymmetric ? 1.0322f : 1.0f;
    const float down = asymmetric ? -1.0240f : -1.0f;
    const float top = asymmetric ? 1.2242f : 1.0f;
    Mapping mapping;
    const bool built = Build(center, right, up, forward, radius,
        left, rightTan, down, top, mapping);
    Check(built, "finite tilted front lens builds");
    if (!built)
        return;
    Check(mapping.x[3] == 0 && mapping.y[3] == 0 && mapping.z[3] == 0,
          "shader constant padding is zero");

    // Forward-project actual 3D points on the aperture, then recover them.
    // This oracle is independent of the helper's inverse-plane construction.
    for (float radial : {0.0f, 0.2f, 0.7f, 1.0f, 1.3f})
    {
        for (int angle = 0; angle < 24; ++angle)
        {
            const double theta = angle * (2.0 * 3.14159265358979323846 / 24.0);
            const float expectedX = radial * static_cast<float>(std::cos(theta));
            const float expectedY = radial * static_cast<float>(std::sin(theta));
            const Vec3 point = {
                center.x + radius * (right.x * expectedX + up.x * expectedY),
                center.y + radius * (right.y * expectedX + up.y * expectedY),
                center.z + radius * (right.z * expectedX + up.z * expectedY)};
            Check(point.z < 0, "round-trip fixture is in front of eye");
            const float u = (point.x / -point.z - left) / (rightTan - left);
            const float v = (top - point.y / -point.z) / (top - down);
            float x = 99, y = 99;
            Check(Evaluate(mapping, u, v, x, y), "forward ray intersects lens");
            Check(Near(x, expectedX) && Near(y, expectedY),
                  "physical lens coordinates survive projective round-trip");
            if (radial == 0.0f)
                Check(Near(x, 0) && Near(y, 0), "projected lens center is zero");
        }
    }
}

void InvalidInput()
{
    const Vec3 center{0, 0, -0.3f}, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, -1};
    Mapping output;
    output.x[0] = 42;
    const auto build = [&](Vec3 c, Vec3 r, Vec3 u, Vec3 f,
                           float radius, float l, float rt, float b, float t)
    {
        return Build(c, r, u, f, radius, l, rt, b, t, output);
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float radius : {0.0f, -1.0f, nan, inf})
        Check(!build(center, right, up, forward, radius, -1, 1, -1, 1),
              "invalid physical radius rejected");
    Check(!build({0, 0, 0.3f}, right, up, forward, 0.02f, -1, 1, -1, 1),
          "lens behind eye rejected");
    Check(!build({0, 0, 0}, right, up, forward, 0.02f, -1, 1, -1, 1),
          "eye in lens plane rejected");
    Check(!build(center, right, up, {0, 0, 1}, 0.02f, -1, 1, -1, 1),
          "eye on barrel side rejected");
    Check(!build({nan, 0, -1}, right, up, forward, 0.02f, -1, 1, -1, 1),
          "nonfinite center rejected");
    Check(!build(center, {0, 0, 0}, up, forward, 0.02f, -1, 1, -1, 1),
          "collapsed basis rejected");
    Check(!build(center, {2, 0, 0}, up, forward, 0.02f, -1, 1, -1, 1),
          "nonunit basis rejected");
    Check(!build(center, right, right, forward, 0.02f, -1, 1, -1, 1),
          "nonorthogonal basis rejected");
    Check(!build(center, {inf, 0, 0}, up, forward, 0.02f, -1, 1, -1, 1),
          "nonfinite basis rejected");
    Check(!build(center, right, up, forward, 0.02f, 1, -1, -1, 1),
          "reversed horizontal FOV rejected");
    Check(!build(center, right, up, forward, 0.02f, -1, 1, 1, 1),
          "collapsed vertical FOV rejected");
    Check(!build(center, right, up, forward, 0.02f, nan, 1, -1, 1),
          "nonfinite FOV rejected");
    Check(output.x[0] == 42, "failed build preserves previous output");

    Mapping mapping;
    Check(Build(center, right, up, forward, 0.02f, -1, 1, -1, 1, mapping),
          "valid mapping for rejection tests");
    float x = 13, y = 14;
    Check(!Evaluate(mapping, nan, 0.5f, x, y), "nonfinite UV rejected");
    mapping.z[2] = 0;
    Check(!Evaluate(mapping, 0.5f, 0.5f, x, y), "parallel ray rejected");
    mapping.z[2] = -1;
    Check(!Evaluate(mapping, 0.5f, 0.5f, x, y), "backward intersection rejected");
    Check(x == 13 && y == 14, "failed evaluation preserves previous output");
}
} // namespace

int main()
{
    for (float yaw : {-55.0f, -20.0f, 0.0f, 25.0f, 55.0f})
        for (float pitch : {-40.0f, 0.0f, 35.0f})
            for (float roll : {0.0f, 35.0f, 90.0f, 170.0f})
                for (float eye : {-0.032f, 0.0f, 0.032f})
                    for (float relief : {0.08f, 0.3f, 0.8f})
                        for (bool asym : {false, true})
                            PhysicalRoundTrip(yaw, pitch, roll, eye, relief, 0.015f, asym);
    // Tiny and large projected lenses use their actual geometry unchanged.
    PhysicalRoundTrip(30, -20, 45, -0.032f, 0.8f, 0.002f, true);
    PhysicalRoundTrip(0, 0, 0, 0.032f, 0.08f, 0.06f, true);
    InvalidInput();
    std::printf("Scope projection: %d checks, %d failures.\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
