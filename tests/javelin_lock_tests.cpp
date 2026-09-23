#include "vr/vr_javelin_lock.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
using kisak::vr::javelin_lock::ResolveScriptAdsFraction;

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

bool SameBits(const float lhs, const float rhs)
{
    std::uint32_t lhsBits = 0;
    std::uint32_t rhsBits = 0;
    static_assert(sizeof(lhsBits) == sizeof(lhs), "32-bit float required");
    std::memcpy(&lhsBits, &lhs, sizeof(lhsBits));
    std::memcpy(&rhsBits, &rhs, sizeof(rhsBits));
    return lhsBits == rhsBits;
}

void TestNativeTransitionAndGates()
{
    float previous = 0.0f;
    for (int step = 0; step <= 10000; ++step)
    {
        const float fraction = static_cast<float>(step) / 10000.0f;
        const float expected = step == 0 ? 0.0f : 1.0f;
        const float resolved = ResolveScriptAdsFraction(
            fraction, true, true, true);
        Expect(resolved == expected,
               "confirmed VR Javelin ADS is full only after native ADS starts");
        Expect(resolved >= previous,
               "continuous native ADS progress must remain monotonic");
        previous = resolved;

        Expect(SameBits(ResolveScriptAdsFraction(
                            fraction, true, false, true), fraction),
               "released VR intent preserves the native ADS fraction");
        Expect(SameBits(ResolveScriptAdsFraction(
                            fraction, true, true, false), fraction),
               "native ADS prohibition preserves the native ADS fraction");
        Expect(SameBits(ResolveScriptAdsFraction(
                            fraction, true, false, false), fraction),
               "both absent gates preserve the native ADS fraction");

        // The caller combines active VR and the semantic Javelin weapon
        // identity into isVrJavelin. Either missing condition takes this path.
        for (int intent = 0; intent != 2; ++intent)
        {
            for (int allowed = 0; allowed != 2; ++allowed)
            {
                Expect(SameBits(ResolveScriptAdsFraction(
                                    fraction, false, intent != 0,
                                    allowed != 0), fraction),
                       "non-VR/non-Javelin fractions remain unchanged");
            }
        }
    }
}

void TestThresholdAndInterruptedAcquisition()
{
    const float thresholdFractions[] = {
        std::numeric_limits<float>::min(),
        0.49f,
        std::nextafter(0.5f, 0.0f),
        0.5f,
        std::nextafter(0.5f, 1.0f),
        0.51f,
        std::nextafter(1.0f, 0.0f),
        1.0f,
    };
    for (const float fraction : thresholdFractions)
    {
        Expect(ResolveScriptAdsFraction(fraction, true, true, true) == 1.0f,
               "no script ADS dip exists around the old 0.5 threshold");
    }

    Expect(ResolveScriptAdsFraction(0.0f, true, true, true) == 0.0f,
           "intent alone cannot start ADS when native fraction is zero");
    Expect(SameBits(ResolveScriptAdsFraction(-0.0f, true, true, true), -0.0f),
           "zero native ADS is not promoted, including signed zero");

    struct Sample
    {
        float nativeFraction;
        bool held;
        bool allowed;
        float expected;
    };
    const Sample acquisition[] = {
        {0.0f, true, true, 0.0f},
        {0.2f, true, true, 1.0f},
        {0.49f, true, true, 1.0f},
        {0.51f, true, true, 1.0f},
        {0.75f, true, true, 1.0f},
        {0.75f, false, true, 0.75f},
        {0.6f, false, true, 0.6f},
        {0.4f, false, false, 0.4f},
        {0.0f, false, false, 0.0f},
        {0.2f, true, false, 0.2f},
        {0.2f, true, true, 1.0f},
        {0.6f, true, false, 0.6f},
        {0.0f, true, false, 0.0f},
        {0.0f, true, true, 0.0f},
        {0.1f, true, true, 1.0f},
        {1.0f, true, true, 1.0f},
        {1.0f, false, false, 1.0f},
        {0.9f, false, false, 0.9f},
    };
    for (const Sample& sample : acquisition)
    {
        Expect(ResolveScriptAdsFraction(sample.nativeFraction, true,
                                        sample.held, sample.allowed) ==
                   sample.expected,
               "release and native-gate interruption do not latch synthetic ADS");
    }

    // Model the script's 50 ms watchdog across a continuous two-second
    // acquisition. Native ADS can straddle 0.5 without resetting the lock.
    int acquiredMilliseconds = 0;
    for (int sample = 0; sample < 40; ++sample)
    {
        const float fraction = 0.01f +
            static_cast<float>(sample) * 0.99f / 39.0f;
        const float scriptFraction = ResolveScriptAdsFraction(
            fraction, true, true, true);
        acquiredMilliseconds = scriptFraction >= 1.0f ?
            acquiredMilliseconds + 50 : 0;
    }
    Expect(acquiredMilliseconds == 2000,
           "the ADS prerequisite survives a continuous two-second acquisition");
    const float interrupted = ResolveScriptAdsFraction(
        0.75f, true, false, true);
    acquiredMilliseconds = interrupted >= 1.0f ?
        acquiredMilliseconds + 50 : 0;
    Expect(acquiredMilliseconds == 0,
           "an interrupted partial ADS sample still fails the script watchdog");
}

void TestInvalidInput()
{
    const float invalidFractions[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -1.0f,
        std::nextafter(0.0f, -1.0f),
        std::nextafter(1.0f, 2.0f),
        2.0f,
        std::numeric_limits<float>::max(),
    };
    for (const float fraction : invalidFractions)
    {
        for (int intent = 0; intent != 2; ++intent)
        {
            for (int allowed = 0; allowed != 2; ++allowed)
            {
                const float result = ResolveScriptAdsFraction(
                    fraction, true, intent != 0, allowed != 0);
                Expect(std::isfinite(result) && result == 0.0f,
                       "invalid VR Javelin ADS fractions fail closed");
                Expect(SameBits(ResolveScriptAdsFraction(
                                    fraction, false, intent != 0,
                                    allowed != 0), fraction),
                       "out-of-scope values are passed through verbatim");
            }
        }
    }
}
} // namespace

int main()
{
    TestNativeTransitionAndGates();
    TestThresholdAndInterruptedAcquisition();
    TestInvalidInput();
    std::printf("Javelin lock ADS tests: %d checks, %d failures\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
