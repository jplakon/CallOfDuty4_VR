#include "vr/vr_pose_ads.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
namespace ads = kisak::vr::pose_ads;
int checks = 0;
int failures = 0;

void Expect(const bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

ads::Geometry ParallelPose(float forward, float left, float height)
{
    const float weapon[3] = {forward, left, height};
    const float support[3] = {forward + 12.0f, left, height};
    return ads::EvaluatePose(weapon, support);
}

ads::Geometry TiltedHeightPose(float height)
{
    const float weapon[3] = {12.0f, 0.0f, height};
    const float support[3] = {
        21.0f, 0.0f, height + (height < 0.0f ? -4.358899f : 4.358899f)};
    return ads::EvaluatePose(weapon, support);
}

ads::Geometry AlignmentPose(float alignment)
{
    const float lateral = std::sqrt(1.0f - alignment * alignment);
    const float weapon[3] = {12.0f * alignment, 12.0f * lateral, 0.0f};
    const float support[3] = {24.0f * alignment, 24.0f * lateral, 0.0f};
    return ads::EvaluatePose(weapon, support);
}

bool OldEngageWindow(const ads::Geometry& pose)
{
    return pose.valid && pose.headAlignment >= 0.80f &&
        pose.eyeLineDistance <= 15.0f &&
        pose.weaponHandForward >= 0.0f && pose.weaponHandForward <= 32.0f &&
        pose.weaponHandHeight >= -17.0f && pose.weaponHandHeight <= 10.0f;
}

ads::Inputs ShoulderInput()
{
    ads::Inputs input;
    input.supportHeld = true;
    input.poseAvailable = true;
    input.geometry = ParallelPose(12.0f, 1.0f, -4.0f);
    return input;
}

bool At(ads::State& state, ads::Inputs& input, std::uint32_t now,
        const ads::Settings& settings = {})
{
    input.nowMilliseconds = now;
    return ads::Update(&state, input, settings);
}

void Engage(ads::State& state, ads::Inputs& input, std::uint32_t start)
{
    Expect(!At(state, input, start), "shoulder dwell must not engage immediately");
    Expect(At(state, input, start + 250u), "stable shoulder must engage at 250 ms");
}
} // namespace

int main()
{
    const ads::Geometry chest = ParallelPose(12.0f, 0.0f, -12.0f);
    Expect(OldEngageWindow(chest), "regression chest pose must demonstrate old false positive");
    Expect(!chest.engage && !chest.retain, "chest carry must not be a shoulder pose");
    Expect(!ParallelPose(12.0f, 0.0f, -20.0f).retain, "hip carry must not retain ADS");
    Expect(ParallelPose(12.0f, 0.0f, -6.0f).engage, "six-inch eye-line boundary should engage");
    Expect(!ParallelPose(12.0f, 0.0f, -6.01f).engage, "outside six-inch radius must not engage");
    Expect(ParallelPose(12.0f, 0.0f, -8.0f).retain, "eight-inch retention boundary should retain");
    Expect(!ParallelPose(12.0f, 0.0f, -8.01f).retain, "outside eight-inch radius must release");
    Expect(ParallelPose(24.0f, 0.0f, -4.0f).engage, "forward engagement boundary should qualify");
    Expect(!ParallelPose(24.01f, 0.0f, -4.0f).engage, "too-far forward carry must not engage");
    Expect(ParallelPose(30.0f, 0.0f, -4.0f).retain, "forward retention boundary should qualify");
    Expect(!ParallelPose(30.01f, 0.0f, -4.0f).retain, "too-far forward carry must release");
    Expect(!ParallelPose(-0.01f, 0.0f, -4.0f).engage, "grip behind head must not newly engage");
    Expect(!ParallelPose(-2.01f, 0.0f, -4.0f).retain, "grip too far behind head must release");
    Expect(TiltedHeightPose(-7.0f).engage && !TiltedHeightPose(-7.01f).engage,
           "low grip-height engagement bound must apply even when hand line crosses eye");
    Expect(TiltedHeightPose(6.0f).engage && !TiltedHeightPose(6.01f).engage,
           "high grip-height engagement bound must apply even when hand line crosses eye");
    Expect(TiltedHeightPose(-9.0f).retain && !TiltedHeightPose(-9.01f).retain,
           "low grip-height retention bound must apply independently of eye-line radius");
    Expect(TiltedHeightPose(8.0f).retain && !TiltedHeightPose(8.01f).retain,
           "high grip-height retention bound must apply independently of eye-line radius");
    Expect(AlignmentPose(0.81f).engage && !AlignmentPose(0.79f).engage,
           "gaze-alignment engagement threshold must reject sideways shoulder poses");
    Expect(AlignmentPose(0.69f).retain && !AlignmentPose(0.67f).retain,
           "gaze-alignment retention threshold must allow only its wider window");
    for (float left : {-3.0f, 3.0f})
    {
        const ads::Geometry pose = ParallelPose(12.0f, left, -4.0f);
        Expect(pose.valid && pose.engage && pose.retain,
               "mirrored shoulder geometry must have equivalent qualification");
    }
    const float zero[3] = {};
    const float invalid[3] = {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
    Expect(!ads::EvaluatePose(zero, zero).valid, "coincident hands must be rejected");
    Expect(!ads::EvaluatePose(zero, invalid).valid, "nonfinite hands must be rejected");
    Expect(!ads::EvaluatePose(nullptr, zero).valid, "missing hand must be rejected");

    ads::State state;
    ads::Inputs input = ShoulderInput();
    Expect(!At(state, input, 0u), "time zero must start a real engagement timer");
    Expect(!At(state, input, 249u), "engagement must wait the full configured dwell");
    Expect(At(state, input, 250u), "time-zero engagement timer must not restart");

    state = {};
    input.geometry = chest;
    for (std::uint32_t time = 0u; time <= 30000u; time += 100u)
    {
        Expect(!At(state, input, time), "prolonged chest carry must not slow movement via pose ADS");
    }

    state = {};
    input = ShoulderInput();
    Expect(!At(state, input, 1000u), "qualifying jitter sequence starts pending");
    input.geometry = chest;
    Expect(!At(state, input, 1240u), "leaving engage window resets pending dwell");
    input = ShoulderInput();
    Expect(!At(state, input, 1500u), "reentry must start fresh dwell");
    Expect(!At(state, input, 1749u), "earlier partial dwell cannot accumulate");
    Expect(At(state, input, 1750u), "fresh continuous dwell must engage");
    input.geometry = ParallelPose(12.0f, 0.0f, -7.0f);
    Expect(!input.geometry.engage && input.geometry.retain,
           "test pose must lie between engage and retain thresholds");
    Expect(At(state, input, 5000u), "small shoulder jitter must retain active ADS");
    input.geometry = chest;
    Expect(At(state, input, 6000u), "first outside-retain sample starts grace");
    Expect(At(state, input, 6179u), "release must allow configured boundary grace");
    input = ShoulderInput();
    Expect(At(state, input, 6180u), "returning inside retention cancels pending release");
    input.geometry = chest;
    Expect(At(state, input, 7000u), "later lowering starts a fresh release timer");
    Expect(!At(state, input, 7180u), "continuous lowering must release at 180 ms");

    state = {};
    input = ShoulderInput();
    const std::uint32_t wrapStart = (std::numeric_limits<std::uint32_t>::max)() - 100u;
    Engage(state, input, wrapStart);
    input.geometry = chest;
    Expect(At(state, input, wrapStart + 300u), "release timer may start after unsigned wrap");
    Expect(!At(state, input, wrapStart + 480u), "release arithmetic must survive unsigned wrap");
    state = {};
    state.held = true;
    Expect(At(state, input, wrapStart), "release timer can begin before unsigned wrap");
    Expect(At(state, input, wrapStart + 179u), "wrapped release timer must retain full grace");
    Expect(!At(state, input, wrapStart + 180u), "release spanning unsigned wrap must finish on time");
    state = {};
    state.held = true;
    Expect(At(state, input, 0u) && !At(state, input, 180u),
           "time-zero release timer must not restart");

    for (int clearReason = 0; clearReason < 4; ++clearReason)
    {
        state = {};
        input = ShoulderInput();
        Engage(state, input, 1000u);
        if (clearReason == 0) input.supportHeld = false;
        if (clearReason == 1) input.enabled = false;
        if (clearReason == 2) input.gameplayAllowed = false;
        if (clearReason == 3) input.poseAvailable = false;
        Expect(!At(state, input, 1251u), "support/feature/gameplay/pose loss must release immediately");
        input = ShoulderInput();
        Expect(!At(state, input, 1500u), "restored availability needs a fresh shoulder dwell");
        Expect(At(state, input, 1750u), "restored availability may engage after full dwell");
    }

    state = {};
    input = ShoulderInput();
    Engage(state, input, 1000u);
    input.sprintRequested = true;
    Expect(!At(state, input, 1251u) && state.sprintSuppressed,
           "sprint press must immediately cancel automatic ADS and latch suppression");
    input.sprintRequested = false;
    Expect(!At(state, input, 2000u), "click release cannot reacquire ADS while still shouldered");
    Expect(!At(state, input, 10000u), "held shoulder cannot silently time out sprint suppression");
    input.poseAvailable = false;
    input.sprintActive = true;
    Expect(!At(state, input, 10001u) && state.sprintSuppressed,
           "tracking loss during native sprint must preserve suppression");
    input.poseAvailable = true;
    input.sprintActive = false;
    Expect(!At(state, input, 11000u), "tracking restoration cannot count as intentional lowering");
    input.geometry = chest;
    Expect(!At(state, input, 12000u), "lowering starts sprint-rearm dwell");
    Expect(!At(state, input, 12179u) && state.sprintSuppressed,
           "brief lower jitter must not rearm automatic ADS");
    input = ShoulderInput();
    Expect(!At(state, input, 12180u) && state.sprintSuppressed,
           "return to shoulder resets incomplete lowering gesture");
    input.geometry = chest;
    At(state, input, 13000u);
    Expect(!At(state, input, 13180u) && !state.sprintSuppressed,
           "continuous deliberate lowering must rearm automatic ADS");
    input = ShoulderInput();
    Engage(state, input, 14000u);
    input.sprintRequested = true;
    At(state, input, 14251u);
    input.sprintRequested = false;
    input.supportHeld = false;
    Expect(!At(state, input, 14252u) && !state.sprintSuppressed,
           "support release deliberately clears sprint suppression");
    input = ShoulderInput();
    Engage(state, input, 15000u);

    state = {};
    input = ShoulderInput();
    input.sprintActive = true;
    Expect(!At(state, input, 1000u) && state.sprintSuppressed,
           "native sprint without held click must veto automatic ADS");
    input.geometry = chest;
    At(state, input, 1100u);
    Expect(!At(state, input, 1280u) && !state.sprintSuppressed,
           "intentional lowering may rearm during native sprint");
    input.geometry = ShoulderInput().geometry;
    Expect(!At(state, input, 2000u), "rearmed pose still cannot engage during native sprint");
    input.sprintActive = false;
    Engage(state, input, 3000u);

    state = {};
    state.sprintSuppressed = true;
    input.geometry = chest;
    Expect(!At(state, input, 0u) && state.sprintSuppressed,
           "time-zero lowering must start a real rearm timer");
    Expect(!At(state, input, 180u) && !state.sprintSuppressed,
           "time-zero lowering timer must not restart");
    state = {};
    state.sprintSuppressed = true;
    At(state, input, wrapStart);
    Expect(!At(state, input, wrapStart + 180u) && !state.sprintSuppressed,
           "sprint lowering/rearm timer must survive unsigned wrap");

    state = {};
    input = ShoulderInput();
    ads::Settings custom;
    custom.engageMilliseconds = 400u;
    custom.releaseMilliseconds = 320u;
    Expect(!At(state, input, 0u, custom) && !At(state, input, 399u, custom),
           "custom engagement delay must be honored");
    Expect(At(state, input, 400u, custom), "custom engagement boundary must engage");
    input.geometry = chest;
    Expect(At(state, input, 500u, custom) && At(state, input, 819u, custom),
           "custom release grace must be honored");
    Expect(!At(state, input, 820u, custom), "custom release boundary must release");
    Expect(!ads::Update(nullptr, input), "missing state must fail safely");

    if (failures != 0)
    {
        std::fprintf(stderr, "%d of %d pose ADS checks failed.\n", failures, checks);
        return 1;
    }
    std::printf("All %d pose ADS checks passed.\n", checks);
    return 0;
}
