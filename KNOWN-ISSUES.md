# Known issues

This list applies to `v0.10.0-beta.3`.

## Unsupported mission

- **Death From Above is not playable in VR and must be skipped.**
- Skip instructions are documented in `INSTALL.md`; the next playable mission is **War Pig** (`bog_b`).

## Hardware and runtimes

- The primary tested configuration is Meta Quest 3 through Virtual Desktop's
  OpenXR runtime.
- Other OpenXR headsets, controller profiles, and runtimes are experimental.
- The experimental 32-bit SteamVR/OpenVR fallback does not yet map motion
  controllers and may show projection/pose distortion or incorrect tilt on
  some systems. Use a working OpenXR runtime for normal play.
- The default `6016x2688` / output-scale `1.0` mode is demanding. The supported
  lower preset is `4768x2016` / output-scale `0.75`.
- `3072x1536` is incompatible with the packed renderer because it cannot hold
  two rectangular eyes plus the dedicated scope panel; the launcher rejects it.

## Rendering

- Synchronized dynamic shadows can have a significant performance cost.
- Physical scope alignment can require small headset-specific calibration
  changes in `VR-Settings.bat`.
- Some original flat-screen post-processing and camera animation has been
  suppressed because it is uncomfortable or incorrect in VR.
- The exact-pose capture path substantially reduces frame reuse and
  head-turn judder, but occasional runtime- or performance-dependent
  judder may still occur. Include `[VR][PERF]` lines with bug reports.

## Tracked hands and physical reloading

- The left hand uses authored free, rifle-grip, and magazine-grip poses.
  Continuous touch-driven finger curling is not implemented.
- Physical reloading applies only to supported rifles, SMGs, and pistols with
  a usable detachable clip model. Other weapons retain COD4's native reload.
- Meta Quest 3 with Virtual Desktop OpenXR is the tested palm-pose path. Hand
  alignment on other controller profiles and runtimes is experimental.

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
