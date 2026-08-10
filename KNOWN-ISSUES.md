# Known issues

This list applies to `v0.10.0-beta.9`.

## Setup and compatibility

- Registry/file detection is an offline preflight, not a synthetic VR session.
  The first scan correctly warns that headset/controller proof is missing.
  Connect and wake the headset, run **Save & Launch Diagnostics**, then rescan
  to import the live backend/runtime/system/interaction-profile receipt.
- KisakCOD and COD4 are 32-bit processes. A valid 64-bit OpenXR registration
  does not prove that the 32-bit loader can start. Beta.9 reports both registry
  views independently and blocks an OpenXR-only launch when the 32-bit manifest
  is absent or missing on disk.
- The automatic backend may continue through the experimental x86 OpenVR path
  when no usable 32-bit OpenXR runtime exists. The report labels that as a
  Warning; it does not present OpenVR as equivalent to the primary VDXR path.
- GPU memory provides only a conservative Native/Performance starting point.
  It is not a performance benchmark. Use the Performance profile if native
  rendering cannot maintain headset cadence even when the scan passes.
- **Apply recommended** changes only the runtime backend and the coupled
  graphics profile. Runtime registration remains owned by the headset software;
  Beta.9 never writes OpenXR registry values or silently starts SteamVR/VDXR.
- `Compatibility-Report.txt` contains mod-relevant paths and hardware/runtime
  identities for support. Review it before posting publicly if the Windows
  installation path itself is sensitive.

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
- One community PSVR2 test confirmed that the manually selected OpenVR backend
  reaches playable controller tracking and input. That setup also reported a
  90-degree weapon orientation error, backward-looking magazine insertion,
  Pause/weapon-switch overlap, and right-stick touch interfering with left-stick
  movement. PSVR2/OpenVR remains experimental until those driver-specific pose
  and default-binding problems are corrected.
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
  running SP game. The beta.9 weapon editor's **Apply live** and guided capture
  also update a running SP mission; other settings take effect on next launch.
- Automatic standing measurement requires an OpenXR `STAGE` space or OpenVR's
  standing universe. If the runtime has no usable floor reference, beta.9 says so
  and applies the saved manual height; it does not guess a floor.
- Every beta.9 save performs an exact byte and 142-value read-back before it can
  report success. The launcher then records the effective profile under
  `%LOCALAPPDATA%\KisakCOD-VR\Active-VR-Settings.txt`, and the game appends
  `STATUS=RUNTIME_ACCEPTED` after parsing the inherited settings. The runtime appends
  `STATUS=RUNTIME_WEAPON_POSE_APPLIED` after the rendered weapon reaches its
  calibrated grip target, height and live-calibration receipts, accepted and
  saved/canceled HUD layouts, plus active weapon/profile and aim-capture
  receipts.
- Legacy beta.7 and V57/V58/V59/V60/V61 test profiles are accepted. Missing
  calibration or visual-HUD fields use tested defaults until the profile is
  saved once by beta.9. Profiles without a unit selector open in Metric mode; the
  underlying canonical calibration values are not rewritten unless edited.
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
- Guided gunstock capture solves rotation, not physical position. Fine-tune
  Forward/Left/Up after capture and verify both hip fire and shouldering.
- Only one gunstock profile is active at a time. Its correction is shared by
  every shouldered weapon; keep unusual weapon geometry in that weapon's own
  shouldered/ADS delta.

## Handedness, tracked hands, and physical reloading

- Beta.9 swaps the functional weapon/off-hand roles, actual tracked weapon pose,
  muzzle, scope, reload/grenade interactions, pointer, and haptics. COD4's
  viewmodel glove/arm geometry was authored for the original right-handed
  layout and is not anatomically mirrored, so left-handed visuals remain
  experimental even when interaction sides are correct.
- The off hand uses authored free, rifle-grip, and magazine-grip poses.
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
- Manual reload owns the off-hand grip after a magazine has been ejected. Finish
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
