# Known issues

This list applies to the `v0.9.0-beta.1` release target.

## Unsupported mission

- **Death From Above is not playable in VR and must be skipped.**
- Skip instructions are documented in `INSTALL.md`; the next playable mission is **War Pig** (`bog_b`).

## Hardware and runtimes

- The primary tested configuration is Meta Quest 3 through Virtual Desktop's
  OpenXR runtime.
- Other OpenXR headsets, controller profiles, and runtimes are experimental.
- The default `6016x2688` mode is demanding. `3072x1536` is the documented
  lower development fallback.

## Rendering

- Synchronized dynamic shadows can have a significant performance cost.
- Physical scope alignment can require small headset-specific calibration
  changes in `VR-Settings.bat`.
- Some original flat-screen post-processing and camera animation has been
  suppressed because it is uncomfortable or incorrect in VR.

## Campaign scripting

- COD4 contains mission events that use the original flat-screen view ray,
  attack state, or scripted weapon state. Many known cases are bridged, but an
  untested checkpoint can still expose a mission-specific issue.
- Bug reports should identify the mission, checkpoint, weapon, headset/runtime,
  and exact source commit from `SOURCE.txt`.

## Debug UI

- The normal launcher hides intermittent engine warning messages and the red
  performance-monitor FPS marker from the headset.
- The diagnostic launcher intentionally restores developer messages.
