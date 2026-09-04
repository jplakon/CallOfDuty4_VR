# Known issues

This list applies to `v0.10.0-beta.16`.

## Setup and compatibility

- The guided installer currently recognizes the complete classic-compatible
  layout and automatically searches Steam's registered and additional library
  folders. The Microsoft/Xbox PC app can expose a different raw layout;
  automatic normalization is intentionally disabled until a verified
  before/after map is available. Setup rejects that layout before writing and
  never guesses, downloads, or moves original COD4 assets.
- Release installers are not code-signed yet, so Windows may identify the
  publisher as unknown. Download only from the project's GitHub release (or
  the linked Patreon post) and verify the adjacent `.sha256` file. This is
  separate from the installer's internal payload-manifest verification.
- Registry/file detection is an offline preflight, not a synthetic VR session.
  The first scan correctly warns that headset/controller proof is missing.
  Connect and wake the headset, run **Save & Launch Diagnostics**, then rescan
  to import the live backend/runtime/system/interaction-profile receipt.
- KisakCOD and COD4 are 32-bit processes. A valid 64-bit OpenXR registration
  does not prove that the 32-bit loader can start. Beta.14 reports both registry
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
  Beta.14 never writes OpenXR registry values or silently starts SteamVR/VDXR.
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
- The 32-bit SteamVR/OpenVR fallback supplies gameplay input through SteamVR's
  legacy controller API. Some drivers alias face, menu, grip, and touch
  components. V105 disables the impossible thumbrest source, prevents the input
  mapper from capturing joystick contact as thumbrest touch, and adds an
  **OpenVR safe controls** preset plus secondary/menu conflict warnings. Native
  Apple Vision Pro/ALVR, PSVR2, Index, and Vive hardware still require
  confirmation. Include controller type/profile lines from `main\console.log`
  with reports. OpenXR remains the preferred backend.
- V107 stops the OpenVR adapter from presenting a raw-device or grip pose as a
  valid palm surface. A dedicated SteamVR `openxr_handmodel` component is still
  used when present; otherwise only the standalone glove selects the grip-frame
  fallback. Apple Vision Pro/ALVR with PSVR2 Sense emulation remains pending
  headset confirmation. If an earlier workaround saved large off-hand fit
  values, reset the six off-hand offsets and angles before evaluating V107.
- V108 replaces issue #61's absolute two-hand aim frame with an
  engagement-anchored steering delta and freezes that delta during release.
  The reporter confirmed that the original attach/release snap and tug are
  gone on PSVR2 through SteamVR/OpenVR. Smaller off-center roll/pivot feel and
  magazine-hand alignment concerns remain under investigation.
- V110 prevents stance input held through loading or a menu from crouching the
  player as gameplay begins. Automated edge-state coverage passes, but issue
  #65 still requires the reporter's seated Quest 3S/SteamVR confirmation.
- V111 corrects issue #66's OpenVR packed-width selection: Performance mode
  keeps two 1872-pixel gameplay eyes and excludes the dedicated 1024-pixel
  scope panel from their split. Automated layout and build checks pass, but
  the reporter's Quest 3S/SteamVR mission-start confirmation is still required.
- The beta.12 OpenVR projection, color, compositor, semantic pose, and
  controller-selector path was verified on Quest 3 through SteamVR. This does
  not by itself prove the same driver behavior on PSVR2 or Index.
- Beta.14 can select Pimax's `PiOpenXR_32.json` when Pimax is the active
  runtime, retains the Pimax-only grip-pose fallback, and adds the recommended
  Full FOV `7924x4082` / output-scale `0.80` layout. The earlier
  `7684x3128` / `1.00` cropped preset remains available. Real Pimax Crystal
  Light hardware validation is still pending; do not treat a passing desktop
  compatibility scan as headset proof.
- The default `6016x2688` / output-scale `1.0` mode is demanding. The supported
  lower preset is `4768x2016` / output-scale `0.75`.
- `3072x1536` is incompatible with the packed renderer because it cannot hold
  two rectangular eyes plus the dedicated scope panel; the launcher rejects it.

## Rendering

- Synchronized dynamic shadows can have a significant performance cost.
- V114 isolates COD4's saved-screen shellshock/flash feedback across packed VR
  views. Captures now occur only after the final view, and each eye samples its
  matching packed region instead of an incomplete or overwritten shared image.
  Source/build checks pass, but issue #48 still requires the reporter's Quest 3
  / VDXR confirmation at Crew Expendable's ending and, if practical, War Pig.
- V113 isolates packed stereo from COD4's fullscreen glow, depth-of-field, and
  blur filters, which could resample neighboring or uninitialized regions and
  stretch Ultimatum's sky into long strips. Eye-local film/color grading remains
  active, but issue #49 still requires the reporter's PICO 4/OpenVR confirmation.
- V112 prevents ordinary verbose-diagnostics launches from enabling the
  retired level-local weapon-slot-7 trace that can match both the Blackout AK
  and Bog Javelin. Source/build checks pass, but issue #64 still requires the
  reporter's Quest 3S/OpenVR A/B confirmation; unrelated baseline frame pacing
  may remain on the GTX 1660.
- V109 restores the server-driven material phase for single-player vehicles;
  this prevents stationary tank tracks from scrolling on their own. The
  reporter confirmed War Pig behavior through PSVR2/OpenVR and issue #67 is
  closed.
- Pimax Crystal Light Full FOV uses the runtime's uncropped `4312x5102`
  recommendation at output scale `0.80`, producing two `3450x4082` eyes plus
  the 1024-pixel scope panel in beta.14's `7924x4082` packed surface. Magnified
  M21/SVD scope output still needs confirmation on real Pimax hardware; report
  any crop, fallback, or missing scope output.
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
- V106 allows every HUD group scale and both safe-area dimensions down to
  `0.25` for wide or canted-FOV headsets. Direct finite environment overrides
  outside the range clamp to the nearest endpoint; malformed values still use
  the tested default. Real Apple Vision Pro/ALVR confirmation remains pending.
- Position-only, direction/level-only, full recenter, and player-height actions
  on the calibration page apply to the running SP game. Standing-height
  measurement preserves both recenter components; seated calibration recenters
  position only. The weapon editor's **Apply live** and guided capture
  also update a running SP mission; other settings take effect on next launch.
- Automatic standing measurement requires an OpenXR `STAGE` space or OpenVR's
  standing universe. If the runtime has no usable floor reference, beta.14 says so
  and applies the saved manual height; it does not guess a floor.
- Every beta.14 save performs an exact byte and 142-value read-back before it can
  report success. The launcher then records the effective profile under
  `%LOCALAPPDATA%\KisakCOD-VR\Active-VR-Settings.txt`, and the game appends
  `STATUS=RUNTIME_ACCEPTED` after parsing the inherited settings. The runtime appends
  `STATUS=RUNTIME_WEAPON_POSE_APPLIED` after the rendered weapon reaches its
  calibrated grip target, height and live-calibration receipts, accepted and
  saved/canceled HUD layouts, plus active weapon/profile and aim-capture
  receipts.
- Legacy beta.7 and V57/V58/V59/V60/V61 test profiles are accepted. The old
  first-gameplay recenter values `0` and `1` migrate to explicit Off and Full.
  Missing
  calibration or visual-HUD fields use tested defaults until the profile is
  saved once by beta.14. Profiles without a unit selector open in Metric mode; the
  underlying canonical calibration values are not rewritten unless edited.
- Press-to-bind briefly starts a separate black VR scene. COD4 must be closed,
  and the configured runtime and controllers must already be active.
- A controller profile may not expose every selectable component. Unsupported
  bindings remain inactive; use **Bind...** or choose a primary/secondary
  action and primary axis for portable profiles. With OpenVR selected,
  thumbrest choices are omitted because legacy SteamVR cannot distinguish them
  from joystick touch.
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

## Dynamic prompt labels

- Beta.14 supplies text labels, not controller-button artwork. Quest, PICO, Index,
  Vive, and Mixed Reality glyph packs remain a future controller-specific phase.
- Hybrid PC commands that combine two semantic actions, and physical grenade or
  magazine instructions whose grammar cannot be replaced safely inside a
  localized `%s` key slot, keep COD4's original keyboard text for now.
- A rare campaign message that contains a literal key name rather than using
  COD4's shared binding resolver cannot be changed automatically. Report the
  mission, checkpoint, exact text, controller profile, and a screenshot so that
  literal can be handled separately without weakening the shared resolver.

## Handedness, tracked hands, and physical reloading

- The mod swaps the functional weapon/off-hand roles, actual tracked weapon pose,
  muzzle, scope, reload/grenade interactions, pointer, and haptics. COD4's
  viewmodel glove/arm geometry was authored for the original right-handed
  layout and is not anatomically mirrored, so left-handed visuals remain
  experimental even when interaction sides are correct.
- The off hand uses authored free, rifle-grip, and magazine-grip poses.
  Continuous touch-driven finger curling is not implemented.
- Automatic proximity uses separate attach and release radii. A small amount
  of travel beyond the pickup radius before release is intentional hysteresis,
  not a stuck-hand condition.
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
- Safehouse and Heat air-support targeting now follows the tracked right
  controller and is functionally usable. The physical handheld targeting
  device is still invisible; beta.14 keeps the normal right glove and hides the
  broken canned arms, so this remains a cosmetic limitation.
- Beta.14 makes mounted-machine-gun visuals follow the right-controller firing
  ray inside the replicated mechanical arc. Automated contracts pass, but the
  Bog emplacement still needs headset confirmation; report whether the model,
  muzzle, and bullets remain aligned at the horizontal and vertical limits.
- Native grenade-class `hasDetonator` devices now accept the configured Fire
  action without a rendered firearm pose. Report any remaining scripted device
  that still requires a mouse click or weapon-cycle workaround.
- Bug reports should identify the mission, checkpoint, weapon, headset/runtime,
  and exact source commit from `SOURCE.txt`.

## Debug UI

- The normal launcher keeps FPS/stat performance overlays disabled. They are
  available only when their diagnostic dvars are explicitly enabled.
- The diagnostic launcher intentionally restores developer messages.
