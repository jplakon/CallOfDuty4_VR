#pragma once

#include <cmath>

namespace kisak::vr::javelin_lock
{

// The retail Javelin script requires playerads() == 1 throughout acquisition.
// Once native aiming has actually started, report confirmed VR ADS intent
// consistently across the native transition instead of dropping from 1 back
// to a partial fraction at the older generic VR bridge's 0.5 threshold.
// This changes only the script-facing fraction, not native ADS, ammo, target
// selection, lock timing, or any rifle/non-VR behavior. The caller must supply
// the real native PMF_SIGHT_AIMING gate as nativeAdsAllowed.
inline float ResolveScriptAdsFraction(
    const float nativeFraction,
    const bool isVrJavelin,
    const bool adsIntentHeld,
    const bool nativeAdsAllowed)
{
    if (!isVrJavelin)
        return nativeFraction;

    // Corrupt or out-of-domain values must not manufacture a script lock.
    if (!std::isfinite(nativeFraction) ||
        nativeFraction < 0.0f || nativeFraction > 1.0f)
    {
        return 0.0f;
    }

    if (adsIntentHeld && nativeAdsAllowed && nativeFraction > 0.0f)
        return 1.0f;

    return nativeFraction;
}

} // namespace kisak::vr::javelin_lock
