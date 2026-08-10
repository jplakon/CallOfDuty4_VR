# Changelog

## v0.10.0-beta.9

Unified setup, per-weapon/gunstock calibration, Metric/Imperial presentation,
and handed physical-interaction update over `v0.10.0-beta.8`.

### Unified setup and compatibility

- Adds one Setup & Compatibility page with read-only detection of required
  game/mod files, DirectX June 2010, Windows architecture, GPU/dedicated
  memory, both registry views of OpenXR, OpenVR fallback, and the last live
  headset/controller result.
- Uses one deterministic evaluator for the desktop page, launcher preflight,
  support report, and regression tests so their Ready/Warning/Blocked decisions
  cannot drift independently.
- Adds explicit backend and Native/Performance recommendations. Applying one
  previews the exact changes and preserves every personal comfort, hand,
  control, HUD, height, interaction, and weapon/gunstock value.
- Adds an atomic `Compatibility-Report.txt` with Copy/Open actions and precise
  remediation for each failed or unproven check.
- Adds live runtime receipts for the initialized backend, OpenXR/OpenVR runtime,
  headset/system, and controller profiles, separating registry-level detection
  from proven session compatibility.

### Metric and Imperial calibration

- Adds a Metric/Imperial measurement selector with Metric as the default.
- Converts all 20 physical configurator fields: eye height, weapon and hand
  offsets, grip/reload/belt distances, grenade hand speeds, and scope geometry.
- Keeps HUD pixels, angles, normalized scales, and gameplay response values in
  their native non-physical forms.
- Preserves exact canonical runtime values through unit switching and unchanged
  saves, avoiding conversion drift, and uses 1 cm or 1 in height refinement.

### Per-weapon and gunstock calibration

- Adds persistent six-axis hip-fire and shouldered/ADS deltas keyed by COD4's
  stable internal weapon id.
- Adds an active physical-gunstock layer shared across weapons, with portable
  `.vrstock` import/export and a guarded 32-profile limit.
- Blends the global baseline, per-weapon hip, gunstock, and per-weapon shoulder
  layers without replacing the controller's absolute live orientation.
- Adds an editor with equipped-weapon discovery, live apply, reset/delete,
  guarded ranges, and deliberate guided rotation capture. Startup pose is never
  sampled as a hidden neutral orientation.
- Replaces the single shared native attachment cache with a per-weapon
  baseline, preventing one gun's viewmodel geometry from becoming another's.

### Handedness and physical interactions

- Adds a right/left dominant-hand selector that swaps the physical weapon and
  off-hand controller roles across OpenXR and OpenVR.
- Mirrors primary and alternate bindings once when handedness changes while
  preserving directional inputs and multi-input chords.
- Routes weapon aiming, muzzle, scope, gunstock, support grip, reload,
  grenades, HUD-editor pointing, controller proxies, and haptics through
  semantic weapon-hand/off-hand roles.
- Adds hold/toggle/proximity support grip, hold/toggle object grip, button or
  physical-pull magazine ejection, release/contact insertion, handed/fixed
  belts, guarded gesture melee, haptic strength, and muzzle-obstruction options.

### Validation and fixes

- Expands strict settings coverage from 125 to 142 values and adds profile,
  compatibility, launcher, runtime, and effective-pose receipts.
- Preserves per-weapon/gunstock translation through final grip-tag correction.
- Prevents physical-unit round-trip drift at normal and boundary values.
- Detects unusable forced backends—including a missing 32-bit OpenXR runtime—
  before process creation while allowing explicit warning-only headset tests.

## v0.10.0-beta.8

Visual HUD editing, guided calibration, verified settings application, and
Controller Input V4 update over `v0.10.0-beta.7`.

### Visual HUD editor

- Replaces the HUD page's tiny non-interactive preview as the primary workflow
  with a dedicated 640x480 visual canvas containing draggable group bounds,
  resize handles, safe-area guides, snap anchors, keyboard refinement, and
  per-group/default reset actions.
- Adds independent visual placement for ammo/equipment, compass plus objective
  icons, normal notifications, bold objective/status banners, and subtitles.
  The crosshair remains locked to the optical center.
- Adds a live in-headset editor driven by the right-controller ray: trigger
  drags, right-stick up/down resizes, left grip bypasses snapping, A saves, and
  B cancels. Gameplay inputs are suppressed until editing ends.
- Applies live edits through the same runtime layout used by screen placement,
  compass drawing, and all three message channels, then atomically imports the
  saved layout back into the configurator.
- Adds request/status receipts for all five groups plus strict protocol,
  snapping, hit-test, settings round-trip, and render-path regression checks.

### Guided view and height calibration

- Adds a dedicated **Height & Recenter** page with live **Recenter now**,
  **Measure standing height**, and **Apply seated calibration** actions plus
  one-inch height adjustment buttons.
- Replaces the hidden translation-only startup recenter with an optional
  first-gameplay capture of position and forward/level orientation.
- Measures standing eye height through an OpenXR `STAGE` reference space or
  OpenVR `TrackingUniverseStanding` without changing the runtime's guardian,
  boundary, or compositor origin.
- Keeps separate standing and seated virtual eye heights. Both default to
  COD4's native 60-inch standing camera so the existing scale is unchanged.
- Applies height as a persistent correction relative to the native standing
  camera, preserving COD4's crouch and prone transitions.
- Makes live calibration requests transactional and records their backend,
  floor availability, requested height, and recenter result.

### Controller-neutral actions

- Replaces Quest-specific fixed controls with primary and alternate bindings
  for every gameplay, menu, mission, movement, turning, and scope-zoom action
- Allows any type-compatible input on either controller and adds an optional
  conventional Aim/ADS binding without removing physical two-hand shouldering
- Separates locomotion, turning, menu navigation, mission selection, and
  mounted-scope zoom axes so remapping one role does not silently move another
- Restores right primary-axis up directly under Jump, keeps
  left trigger as its alternate default, and keeps right primary-axis down as
  the separate Lower stance action
- Migrates beta.7 `x`, `y`, `a`, `b`, and `stick` values to portable semantic
  source identifiers
- Adds up/down/left/right primary-stick or trackpad inputs to either hand
- Allows up to four simultaneous inputs in each primary or alternate slot;
  inputs inside a slot form an AND-chord and the two slots remain alternatives
- Restores the visible beta.7 mission defaults as ordinary right-thumbrest +
  left-direction chords for slot 5, night vision, slot 6, and C4
- Leaves the redundant native right-grip/off-hand action unbound by default;
  physical grenade interaction remains on the left support grip
- Migrates V3 profiles by folding the retired separate Raise stance action
  into Jump without discarding customized bindings

### Controller profiles and capture

- Adds OpenXR interaction-profile mappings for Khronos Generic/Simple,
  Oculus/Meta Touch, Touch Plus/Pro, PICO 4/Neo3, Valve Index, HTC Vive,
  Cosmos/Focus 3, Microsoft/HP Mixed Reality, and Samsung Odyssey controllers
- Adds **Bind...** capture to the configurator: press a button/trigger/grip or
  move an axis/direction in a temporary VR session to assign it directly
- Adds **Chord...** editing with click-to-toggle selection for simultaneous
  cross-controller bindings
- Warns about duplicate gameplay bindings while preserving intentional
  overlaps and rejects boolean/axis type mismatches

### SteamVR compatibility

- Adds controller input to the 32-bit OpenVR fallback through legacy
  controller-state and axis discovery using the same saved action profile
- Publishes SteamVR controller poses for weapon aiming, support-hand
  interactions, physical ADS, manual reload, and grenade throwing
- Adds OpenVR capture fallback to the input mapper and legacy haptic recoil

### Settings and weapon-placement fixes

- Requires exact on-disk bytes, metadata, and 125-setting read-back before the
  configurator reports a successful save.
- Preserves and displays the active profile, destination file, revision, and
  save time instead of resetting the UI identity to Custom on restart.
- Validates portable and LocalAppData overrides before launch, records the
  effective environment, and appends a runtime acceptance receipt from the
  game process.
- Routes physical magazine reload and physical grenade toggles through the same
  validated runtime settings object as weapon positional calibration.
- Preserves forward/left/up weapon calibration through final viewmodel grip-tag
  alignment. The previous alignment translated the model back onto the raw
  controller grip and canceled the requested positional change.
- Records the applied offset, camera-local displacement, selected grip tag,
  and residual alignment error after the corrected weapon pose is rendered.
- Expands strict configurator and runtime coverage from 113 to 125 settings.

### Display and release fixes

- Defaults the normal FPS counter and red performance-warning overlay off and
  requires their diagnostic dvars to be explicitly enabled before they draw.
- Runs Windows CI on the repository's `main` branch and builds the game,
  configurator, input mapper, and settings tests for release validation.

## v0.10.0-beta.7

VR configurator and customization update over `v0.10.0-beta.6`.

### VR Configurator

- Adds the portable `KisakCOD-VR-Configurator.exe` with Tested Quest 3,
  Performance, Comfort Snap, Smooth Turn, Seated, and Minimal HUD presets
- Adds full custom pages and visual previews for comfort, rendering,
  HUD/text/compass placement, weapon/hand/belt fit, physical interactions,
  scope alignment, and supported controller roles
- Validates settings before saving and provides Save & Launch plus a one-run
  diagnostic launch
- Stores personal settings separately under LocalAppData, keeps automatic
  backups, and supports profile import/export so package upgrades preserve the
  active profile

### Runtime customization

- Makes snap angle, smooth-turn speed, stick deadzones, and head/body/left-hand
  movement direction configurable
- Adds configurable compass visibility, scale, rotation, inset, mission-text
  placement, crosshair, subtitles, camera shake, and weapon bob
- Adds right-weapon and left-hand position/orientation calibration, tracking
  response, two-hand strength, belt geometry, reload insertion radius, grenade
  throw calibration, and physical-scope placement/quality controls
- Allows automatic/native or physical magazine reloading and remapping of the
  supported face-button roles while preserving fixed pose-sensitive inputs
- Adds automated configurator settings tests to the Windows build workflow

## v0.10.0-beta.6

Manual-grenade and campaign-access update over `v0.10.0-beta.5`.

### Manual grenades

- Adds physical grenade selection: squeeze the left grip at the left hip for a
  frag or at the right hip for the mission-equipped flashbang/smoke grenade
- Keeps COD4's native offhand state machine for ammunition, pin/pullback,
  cooking, fuse timing, sounds, damage, AI reactions, and mission scripting
- Renders the projectile grenade model in the tracked left hand while keeping
  the firearm visible in the tracked right hand
- Releases from the physical left-controller position and maps a short recent
  motion history onto the selected grenade's native launch-speed scale
- Preserves intentional stationary drops, adds a controlled upward arc, and
  clamps/traces the release point so reaching through a wall cannot throw
  through it
- Gives manual magazine insertion priority over grenade grabs, disables the
  two-hand rifle grip while a grenade is held, and requires a fresh grip press
  inside a hip zone
- Frees left Y for immediate weapon cycling in manual mode; set
  `KISAK_VR_MANUAL_GRENADES=0` for beta.5's legacy grenade controls

### Mission Select

- Starts the normal launcher with COD4's built-in `mis_cheat 1` campaign
  unlock, making every standard campaign mission selectable on a new profile
- Leaves developer mode and the intermittent DEV mission category disabled
- Does not alter saves, campaign completion, difficulty records, or profiles
- Still requires **Death From Above** to be skipped because that mission is not
  supported in VR

## v0.10.0-beta.5

Snap-turn input hotfix over `v0.10.0-beta.4`.

### Turning controls

- Restores reliable repeated 45-degree snap turns after the smooth-turn
  refactor
- Re-arms the snap-turn latch at true stick neutral before the
  horizontal/vertical dominance filter runs
- Preserves the 0.75 engagement threshold, 0.35 release threshold, 45-degree
  angle, analog smooth turning, and right-stick stance gestures

## v0.10.0-beta.4

Launcher compatibility and crash-report privacy hotfix over
`v0.10.0-beta.3`. The game executable and VR gameplay code are unchanged.

### Launcher

- Fixes the batch launcher exiting before starting the game when COD4 is
  installed in a path containing parentheses, including Steam's default
  `C:\Program Files (x86)\...` location
- Restores normal launcher settings and diagnostics for affected installs,
  avoiding the blurry low-resolution result caused by starting
  `KisakCOD-sp.exe` directly

### Crash-report privacy

- Clarifies throughout the packaged collector that minidump ZIPs must be sent
  privately and must not be attached to public GitHub issues

## v0.10.0-beta.3

VR interface, turning, stance-control, and diagnostic update over
`v0.10.0-beta.2`.

### VR menus

- Renders frontend quit confirmations once as a centered monoscopic dialog
  instead of attaching separated copies to both stereo eye command lists
- Applies the same correction to the active-mission Pause -> Quit flow

### Turning and stance controls

- Adds configurable `snap` and `smooth` right-stick turn modes while preserving
  45-degree snap as the default
- Adds analog smooth-turn speed control from 30 through 360 degrees per second,
  proportional stick response, deadzone handling, and hitch rotation limiting
- Routes vertical right-stick gestures through COD4's native stance state:
  down lowers stand -> crouch -> prone, while up raises prone -> crouch -> stand
  and jumps only when already standing
- Requires a return to neutral between stance steps and preserves right-stick
  click melee plus right B's original tap/hold behavior

### Diagnostics and compatibility

- Adds early main/worker-thread crash recording, minidumps, session-stage
  markers, and a corrected crash-report collector
- Adds an experimental statically linked x86 OpenVR client fallback when a
  usable 32-bit OpenXR runtime is unavailable; normal gameplay remains OpenXR
  first

## v0.10.0-beta.2

Frame-pacing, comfort, audio, and controls update over `v0.10.0-beta.1`.

### Performance and stability

- Associates each captured stereo image with the exact OpenXR render pose used
  to create it, preventing a delayed GPU capture from using a newer head pose
- Replaces alternating capture reuse/drop behavior with an ordered four-slot
  capture queue and improved retirement-fence polling
- Substantially improves fresh-frame delivery and reduces head-turn judder,
  consumer skips, and reused-frame streaks
- Runs the renderer sound update once per game frame instead of once per eye
  and scope view, fixing scratchy or corrupted looping audio such as the
  Airlift helicopter sequence

### Camera and controls

- Reduces the game-authored VR camera base to yaw only, preventing sprint,
  rappel, and scripted pitch/roll from banking the horizon while preserving
  full physical HMD motion
- Adds right-stick up jump/stand and right-stick down crouch gestures with
  neutral re-arming and vertical-dominance protection
- Preserves horizontal 45-degree snap turning and right-stick-click melee

### Configuration

- Replaces the incompatible `3072x1536` recommendation with the supported
  `4768x2016` lower preset and output scale `0.75`
- Retires high-volume frame, camera, audio, and controller diagnostics during
  normal play

## v0.10.0-beta.1

Tracked-hands and physical-reloading beta.

### Added

- Controller-tracked left hand with free, rifle-support, and magazine-grip poses
- Physical manual reloading for detachable-magazine rifles, SMGs, and
  pistols: eject with right A, draw from the left hip with the left grip,
  and release at the magazine well to insert

### Changed

- Moved jump to the left index trigger and magazine ejection/native reload to
  the right A button
- Removed the colored controller-axis diagnostic overlay

## v0.9.0-beta.1

Initial public beta release target.

### Added

- OpenXR single-player runtime initialization and lifecycle
- Same-frame stereoscopic rendering and per-eye projection
- 6DoF headset position and orientation
- Right-controller weapon aiming, firing, recoil haptics, and physical muzzle
  alignment
- Two-hand weapon support
- Physical and fixed-scope rendering paths
- VR controller bindings for core campaign actions and mission weapons
- VR-safe HUD, compass, prompt, message, and menu placement
- Opt-in synchronized dynamic shadow maps
- Configurable compositor scale, brightness, and FSR path

### Campaign and comfort fixes

- Script bridges for VR-fired bullets and several scripted set pieces
- Mounted-turret, vehicle-weapon, Javelin, Stinger, and fixed-rifle support
- Mission-specific AI/navigation and checkpoint/save fixes
- Mouse-look suppression during VR gameplay
- Weapon-authored sprint camera animation suppression
- Flat-screen behavior retained when OpenXR is inactive

### Known limitation

- Death From Above is not supported and must be skipped.
