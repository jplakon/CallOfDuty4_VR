# Known issues

This list applies to `v0.10.0-beta.8`.

## Unsupported mission

- **Death From Above is not playable in VR and must be skipped.**
- Skip instructions are documented in `INSTALL.md`; the next playable mission is **War Pig** (`bog_b`).

## Hardware and runtimes

- The primary tested configuration is Meta Quest 3 through Virtual Desktop's
  VDXR OpenXR runtime. VDXR is recommended for Quest headsets.
- The additional OpenXR controller profiles are registry-validated but require
  hardware testing. PICO, Index, Vive, Cosmos, Focus 3, Windows Mixed Reality,
  HP, and Samsung behavior should still be considered experimental.
- Meta Quest Link's 32-bit OpenXR runtime can crash inside `xrCreateSession`
  after D3D11 initialization. Because this terminates the process,
  `KISAK_VR_BACKEND=auto` cannot recover by falling back to OpenVR. A
  per-launch `XR_RUNTIME_JSON` override to Virtual Desktop's 32-bit VDXR
  manifest (`C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr-32.json`)
  is documented in `INSTALL.md`.
- The 32-bit SteamVR/OpenVR fallback now supplies gameplay input and tracked
  controller poses through SteamVR's legacy controller API. Some drivers alias
  face, menu, grip, and touch components, and grip/aim use one shared device
  pose. Remap conflicting controls and report the controller type/profile lines
  from `main\console.log`. OpenXR remains the preferred backend.
- The default `6016x2688` / output-scale `1.0` mode is demanding. The supported
  lower preset is `4768x2016` / output-scale `0.75`.
- `3072x1536` is incompatible with the packed renderer because it cannot hold
  two rectangular eyes plus the dedicated scope panel; the launcher rejects it.

## Rendering

- Synchronized dynamic shadows can have a significant performance cost.
- Physical scope alignment can require small headset-specific calibration
  changes in `KisakCOD-VR-Configurator.exe`.
- Some original flat-screen post-processing and camera animation has been
  suppressed because it is uncomfortable or incorrect in VR.
- The exact-pose capture path substantially reduces frame reuse and
  head-turn judder, but occasional runtime- or performance-dependent
  judder may still occur. Include `[VR][PERF]` lines with bug reports.

## Configurator

- The desktop HUD editor uses authored group bounds as visual handles. The
  in-headset editor is the authoritative placement check because it overlays
  those handles while the actual mission HUD is drawing and updates it live.
- Ammo/equipment, compass/objective icons, normal notifications, bold
  objective/status banners, and subtitles can move independently. The native
  crosshair remains locked to optical center by design.
- Recenter and player-height actions on the calibration page apply to the
  running SP game. Other setting changes still take effect on the next launch.
- Automatic standing measurement requires an OpenXR `STAGE` space or OpenVR's
  standing universe. If the runtime has no usable floor reference, beta.8 says so
  and applies the saved manual height; it does not guess a floor.
- Every beta.8 save performs an exact byte and 125-value read-back before it can
  report success. The launcher then records the effective profile under
  `%LOCALAPPDATA%\KisakCOD-VR\Active-VR-Settings.txt`, and the game appends
  `STATUS=RUNTIME_ACCEPTED` after parsing the inherited settings. The runtime appends
  `STATUS=RUNTIME_WEAPON_POSE_APPLIED` after the rendered weapon reaches its
  calibrated grip target, height and live-calibration receipts, and the accepted
  and saved/canceled HUD layouts.
- Legacy beta.7 and V57/V58/V59/V60 test profiles are accepted. Missing
  calibration or visual-HUD fields use tested defaults until the profile is
  saved once by beta.8.
- Press-to-bind briefly starts a separate black VR scene. COD4 must be closed,
  and the configured runtime and controllers must already be active.
- A controller profile may not expose every selectable component. Unsupported
  bindings remain inactive; use **Bind...** or choose a primary/secondary
  action and primary axis for portable profiles.
- Input conflicts are warnings rather than errors. This permits intentional
  overlaps, but an accidental overlap activates both gameplay actions.
- OpenXR runtimes that terminate the mapper during `xrCreateSession` cannot be
  recovered inside that process. Select `openvr`, or use a working 32-bit
  OpenXR runtime such as VDXR, before capturing.
- The active profile is stored under `%LOCALAPPDATA%\KisakCOD-VR`; use Restore
  Defaults or a saved backup if manual edits make a profile invalid.

## Tracked hands and physical reloading

- The left hand uses authored free, rifle-grip, and magazine-grip poses.
  Continuous touch-driven finger curling is not implemented.
- Physical reloading applies only to supported rifles, SMGs, and pistols with
  a usable detachable clip model. Other weapons retain COD4's native reload.
- Meta Quest 3 with Virtual Desktop OpenXR is the tested palm-pose path. Hand
  alignment on other controller profiles and runtimes is experimental.

## Manual grenades

- Grenade belt placement, palm fit, and throw calibration were developed on
  Meta Quest 3 with Virtual Desktop OpenXR. Other controllers may require
  alignment or strength tuning.
- A completely still grip release is intentionally treated as a drop rather
  than a throw.
- Manual reload owns the left grip after a magazine has been ejected. Finish
  or cancel that interaction before drawing a grenade.

## Campaign scripting

- COD4 contains mission events that use the original flat-screen view ray,
  attack state, or scripted weapon state. Many known cases are bridged, but an
  untested checkpoint can still expose a mission-specific issue.
- Bug reports should identify the mission, checkpoint, weapon, headset/runtime,
  and exact source commit from `SOURCE.txt`.

## Debug UI

- The normal launcher keeps FPS/stat performance overlays disabled. They are
  available only when their diagnostic dvars are explicitly enabled.
- The diagnostic launcher intentionally restores developer messages.
