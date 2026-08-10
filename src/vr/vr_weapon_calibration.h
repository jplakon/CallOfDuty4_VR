#pragma once

namespace kisak::vr::weapon_calibration
{

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

} // namespace kisak::vr::weapon_calibration
