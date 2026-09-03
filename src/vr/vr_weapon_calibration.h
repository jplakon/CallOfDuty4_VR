#pragma once

#include <algorithm>
#include <cmath>

namespace kisak::vr::weapon_calibration
{

inline void MultiplyAxes(
    const float left[3][3],
    const float right[3][3],
    float product[3][3])
{
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            product[row][column] =
                left[row][0] * right[0][column] +
                left[row][1] * right[1][column] +
                left[row][2] * right[2][column];
        }
    }
}

inline bool BuildAxisFromForwardAndUpHint(
    const float forwardHint[3],
    const float upHint[3],
    const float leftFallback[3],
    float axis[3][3])
{
    float forward[3] = {
        forwardHint[0],
        forwardHint[1],
        forwardHint[2],
    };

    const float forwardLength = std::sqrt(
        forward[0] * forward[0] +
        forward[1] * forward[1] +
        forward[2] * forward[2]);
    if (forwardLength <= 0.0001f)
    {
        return false;
    }

    forward[0] /= forwardLength;
    forward[1] /= forwardLength;
    forward[2] /= forwardLength;

    const float upAlongForward =
        upHint[0] * forward[0] +
        upHint[1] * forward[1] +
        upHint[2] * forward[2];
    float up[3] = {
        upHint[0] - upAlongForward * forward[0],
        upHint[1] - upAlongForward * forward[1],
        upHint[2] - upAlongForward * forward[2],
    };

    float upLength = std::sqrt(
        up[0] * up[0] +
        up[1] * up[1] +
        up[2] * up[2]);
    if (upLength <= 0.0001f)
    {
        up[0] =
            forward[1] * leftFallback[2] -
            forward[2] * leftFallback[1];
        up[1] =
            forward[2] * leftFallback[0] -
            forward[0] * leftFallback[2];
        up[2] =
            forward[0] * leftFallback[1] -
            forward[1] * leftFallback[0];
        upLength = std::sqrt(
            up[0] * up[0] +
            up[1] * up[1] +
            up[2] * up[2]);
    }

    if (upLength <= 0.0001f)
    {
        return false;
    }

    up[0] /= upLength;
    up[1] /= upLength;
    up[2] /= upLength;

    float left[3] = {
        up[1] * forward[2] - up[2] * forward[1],
        up[2] * forward[0] - up[0] * forward[2],
        up[0] * forward[1] - up[1] * forward[0],
    };
    const float leftLength = std::sqrt(
        left[0] * left[0] +
        left[1] * left[1] +
        left[2] * left[2]);
    if (leftLength <= 0.0001f)
    {
        return false;
    }

    left[0] /= leftLength;
    left[1] /= leftLength;
    left[2] /= leftLength;

    up[0] = forward[1] * left[2] - forward[2] * left[1];
    up[1] = forward[2] * left[0] - forward[0] * left[2];
    up[2] = forward[0] * left[1] - forward[1] * left[0];

    for (int component = 0; component < 3; ++component)
    {
        axis[0][component] = forward[component];
        axis[1][component] = left[component];
        axis[2][component] = up[component];
    }
    return true;
}

// Builds the physical two-controller frame without applying any weapon-fit
// calibration. A separately captured relative transform makes this frame a
// steering delta instead of an absolute pose that can snap the weapon.
inline bool BuildTwoHandTargetAxis(
    const float weaponHandPosition[3],
    const float offHandPosition[3],
    const float weaponControllerAxis[3][3],
    float targetAxis[3][3])
{
    const float handLine[3] = {
        offHandPosition[0] - weaponHandPosition[0],
        offHandPosition[1] - weaponHandPosition[1],
        offHandPosition[2] - weaponHandPosition[2],
    };
    return BuildAxisFromForwardAndUpHint(
        handLine,
        weaponControllerAxis[2],
        weaponControllerAxis[1],
        targetAxis);
}

// Applies only the support hand's movement relative to the weapon controller.
// Moving both hands as a rigid pair therefore leaves the fully calibrated
// one-hand weapon basis unchanged, while moving the support hand adds a delta
// in that calibrated frame.
inline void RelativeTwoHandWeaponAxis(
    const float engagementTargetAxis[3][3],
    const float engagementControllerAxis[3][3],
    const float currentControllerAxis[3][3],
    const float calibratedOneHandWeaponAxis[3][3],
    const float currentTargetAxis[3][3],
    float targetWeaponAxis[3][3])
{
    float targetFromControllerAxis[3][3] = {};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            targetFromControllerAxis[row][column] =
                engagementTargetAxis[row][0] *
                    engagementControllerAxis[column][0] +
                engagementTargetAxis[row][1] *
                    engagementControllerAxis[column][1] +
                engagementTargetAxis[row][2] *
                    engagementControllerAxis[column][2];
        }
    }

    float expectedTargetAxis[3][3] = {};
    MultiplyAxes(
        targetFromControllerAxis,
        currentControllerAxis,
        expectedTargetAxis);

    float targetToWeaponAxis[3][3] = {};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            targetToWeaponAxis[row][column] =
                calibratedOneHandWeaponAxis[row][0] *
                    expectedTargetAxis[column][0] +
                calibratedOneHandWeaponAxis[row][1] *
                    expectedTargetAxis[column][1] +
                calibratedOneHandWeaponAxis[row][2] *
                    expectedTargetAxis[column][2];
        }
    }
    MultiplyAxes(
        targetToWeaponAxis,
        currentTargetAxis,
        targetWeaponAxis);
}

inline bool BlendWeaponAxes(
    const float oneHandWeaponAxis[3][3],
    const float twoHandWeaponAxis[3][3],
    const float blend,
    float blendedWeaponAxis[3][3])
{
    const float clampedBlend = std::clamp(blend, 0.0f, 1.0f);
    const float oneHandWeight = 1.0f - clampedBlend;
    const float forwardHint[3] = {
        oneHandWeaponAxis[0][0] * oneHandWeight +
            twoHandWeaponAxis[0][0] * clampedBlend,
        oneHandWeaponAxis[0][1] * oneHandWeight +
            twoHandWeaponAxis[0][1] * clampedBlend,
        oneHandWeaponAxis[0][2] * oneHandWeight +
            twoHandWeaponAxis[0][2] * clampedBlend,
    };
    const float upHint[3] = {
        oneHandWeaponAxis[2][0] * oneHandWeight +
            twoHandWeaponAxis[2][0] * clampedBlend,
        oneHandWeaponAxis[2][1] * oneHandWeight +
            twoHandWeaponAxis[2][1] * clampedBlend,
        oneHandWeaponAxis[2][2] * oneHandWeight +
            twoHandWeaponAxis[2][2] * clampedBlend,
    };
    return BuildAxisFromForwardAndUpHint(
        forwardHint,
        upHint,
        oneHandWeaponAxis[1],
        blendedWeaponAxis);
}

// The placement pipeline still needs a controller basis for its calibrated
// origin offset. Solve attachmentAxis * controllerAxis = weaponAxis so the
// final rendered weapon basis remains the weapon-space blend above.
inline void ControllerAxisForWeaponAxis(
    const float attachmentAxis[3][3],
    const float weaponAxis[3][3],
    float controllerAxis[3][3])
{
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            controllerAxis[row][column] =
                attachmentAxis[0][row] * weaponAxis[0][column] +
                attachmentAxis[1][row] * weaponAxis[1][column] +
                attachmentAxis[2][row] * weaponAxis[2][column];
        }
    }
}

// Converts a controller-local forward/left/up offset into the current
// camera-local basis. Keeping this small transform shared with the portable
// tests prevents the final grip-tag alignment from silently cancelling the
// requested weapon translation again.
inline void ControllerLocalOffsetToCameraLocal(
    const float controllerAxisCameraLocal[3][3],
    const float controllerLocalOffset[3],
    float cameraLocalDelta[3])
{
    for (int cameraComponent = 0;
         cameraComponent < 3;
         ++cameraComponent)
    {
        cameraLocalDelta[cameraComponent] =
            controllerLocalOffset[0] *
                controllerAxisCameraLocal[0][cameraComponent] +
            controllerLocalOffset[1] *
                controllerAxisCameraLocal[1][cameraComponent] +
            controllerLocalOffset[2] *
                controllerAxisCameraLocal[2][cameraComponent];
    }
}

// Produces the world-space grip target used by the viewmodel's final tag
// alignment. The calibration delta must be included in this target; aligning
// to the unmodified physical grip would exactly undo the position offset that
// was already applied to the weapon placement.
inline void CalibratedGripTargetWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    const float gripCameraLocal[3],
    const float calibrationCameraLocal[3],
    float targetWorld[3])
{
    const float calibratedGripCameraLocal[3] = {
        gripCameraLocal[0] + calibrationCameraLocal[0],
        gripCameraLocal[1] + calibrationCameraLocal[1],
        gripCameraLocal[2] + calibrationCameraLocal[2],
    };

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        targetWorld[worldComponent] =
            cameraOrigin[worldComponent] +
            calibratedGripCameraLocal[0] *
                cameraAxis[0][worldComponent] +
            calibratedGripCameraLocal[1] *
                cameraAxis[1][worldComponent] +
            calibratedGripCameraLocal[2] *
                cameraAxis[2][worldComponent];
    }
}

// Solves the absolute controller-local rotation that makes the final weapon
// basis match the HMD/camera basis for a deliberate gunstock capture.  The
// rendered basis is baseAttachment * effectiveRotation * controllerAxis, so
// the required rotation is transpose(baseAttachment) *
// transpose(controllerAxis).  This is an explicit user-triggered capture; it
// is never sampled automatically at startup.
inline void AimAlignedEffectiveRotation(
    const float baseAttachmentAxis[3][3],
    const float controllerAxis[3][3],
    float effectiveRotationAxis[3][3])
{
    float baseTranspose[3][3] = {};
    float controllerTranspose[3][3] = {};

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            baseTranspose[row][column] =
                baseAttachmentAxis[column][row];
            controllerTranspose[row][column] =
                controllerAxis[column][row];
        }
    }

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            effectiveRotationAxis[row][column] =
                baseTranspose[row][0] * controllerTranspose[0][column] +
                baseTranspose[row][1] * controllerTranspose[1][column] +
                baseTranspose[row][2] * controllerTranspose[2][column];
        }
    }
}

} // namespace kisak::vr::weapon_calibration
