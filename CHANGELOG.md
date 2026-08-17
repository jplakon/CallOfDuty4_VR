# Changelog

## Unreleased

_No changes yet._

## v0.10.0-beta.15

Cumulative post-beta.14 VR fixes from the V98-V104 validation chain.

- Index/OpenVR hand and squeeze-path work, right-safe ammo/grenade HUD counters, suppressed in-headset error overlay, and canonical 4:3 menus.
- V103 guarded the native OpenXR mission selector with neutral-entry behavior.
- V104 uses a level-safe HMD yaw-only base for startup and direction/full recenter, with a near-vertical pose guard.
- Quest 3/OpenXR headset validation and all 142 configurator tests passed.


## v0.10.0-beta.14

Stereo UI/crosshair repair, Pimax Full FOV scope support, controller-driven
air support, a resizable Configurator, and guarded Windows Setup over
`v0.10.0-beta.13`.

### Stereo menus and crosshair

- Uses the same eye-local geometry for Mission Select artwork and text.
- Centers Quit Game and Quit Mission confirmation dialogs in both eyes.
- Suppresses the legacy flat COD4 crosshair whenever VR is active, including
  profiles that explicitly retained `cg_drawCrosshair 1` from an older beta.

### Pimax Crystal Light Full FOV

- Adds a `7924x4082` packed preset for the runtime's uncropped `4312x5102`
  recommendation at output scale `0.80`: two `3450x4082` eyes plus the existing
  1024-pixel physical-scope panel.
- Retains the earlier `7684x3128` / output-scale `1.00` cropped preset and leaves
  Quest Native and Performance modes unchanged.
- Keeps Pimax runtime selection and hand-interaction guards from beta.13. Real
  Pimax Crystal Light Full FOV and magnified-scope confirmation remain pending.

### Safehouse and Heat air support

- Routes the air-support targeting ray, target marker, and strike placement to
  the tracked right controller instead of the original flat-screen view ray.
- Keeps the normal right glove stable and suppresses the broken canned
  binocular/device arms that produced severe visual garbling.
- Functional controller targeting was confirmed. The physical handheld device
  model remains invisible and is documented as a cosmetic limitation.

### Resizable Configurator

- Requests a full `1160x750` client area rather than assuming an outer-window
  size that can clip controls under Windows DPI and theme metrics.
- Adds resize, maximize, and restore support plus a minimum tracking size that
  keeps every rightmost and bottom control accessible.

### Guided Windows installer

- Adds `KisakCOD-VR-v…-Setup.exe` as the recommended artifact while retaining
  the portable ZIP. Both come from one deterministic case-insensitive
  allowlisted payload and receive SHA-256 sidecars.
- Finds Steam App 7940 in the registered/default location and additional
  `libraryfolders.vdf` libraries, with a manual Browse fallback.
- Requires `iw3sp.exe`, `localization.txt`, `main\*.iwd`, and the matching
  `zone\<language>\code_post_gfx.ff` before it writes anything. Unsupported
  Microsoft/Xbox raw layouts are rejected safely rather than rearranged.
- Before first overwrite, backs up and SHA-256-verifies every existing managed
  file. Update/repair preserves those originals; uninstall restores them and
  leaves game data, saves, and LocalAppData settings untouched.
- Adds Start Menu shortcuts, an optional desktop shortcut, and optional
  post-install Configurator launch.

### Publisher and validation corrections

- Makes manifest ordering deterministic and case-insensitive.
- Corrects Inno preprocessor line breaks, literal `/DName=Value` path
  definitions, Inno 6/7 close-app API compatibility, and smoke AppId brace
  escaping.
- Preserves native Setup switches under Git Bash with
  `MSYS2_ARG_CONV_EXCL=*`, verifies exact compact-JSON argument transport, and
  waits for Inno's second uninstall phase and `Log closed.` before validating
  restoration.
- Passes Python 3.13.13, Inno Setup 6.7.3, all 142 settings/source checks, all
  seven installer-builder tests, and the final R8 disposable
  reject/install/repair/uninstall suite with exact sentinel restoration and
  fake game-data retention.

## v0.10.0-beta.13

HUD/menu alignment, SteamVR and Pimax compatibility, and mounted-gun visual
aiming update over `v0.10.0-beta.12`.

### HUD and centered menus

- Authors the shared 2D command list in one-eye coordinates and replays it into
  both headset eyes.
- Routes the real SP compass ticker/objective ownerdraws and the editor handle
  through one canonical transform, while correcting normal-notification bounds.
- Samples frontend and pause menus from one completed eye so they appear once,
  centered, instead of as a squeezed side-by-side duplicate.
- Keeps controller menu-cursor hit testing in eye-local `ScreenPlacement`
  coordinates and defaults the normal COD4 crosshair to Off for new/reset
  profiles without overwriting existing LocalAppData settings.

### SteamVR OpenXR and Pimax compatibility

- Creates the OpenXR D3D11 device through `CreateDXGIFactory1`,
  `IDXGIFactory1`, and `IDXGIAdapter1`, retaining the adapter-LUID match and
  D3D9Ex/D3D11 interop probe used by the SteamVR x86 sync-texture path.
- Adds a dedicated Pimax Crystal Light `7684x3128` packed mode: two
  `3330x3128` eyes plus the existing 1024-pixel physical-scope panel. Quest
  Native `6016x2688` and Performance `4768x2016` remain unchanged.
- Selects Pimax's `PiOpenXR_32.json` only when Pimax is the active runtime.
  Explicit `XR_RUNTIME_JSON` choices and active Quest, VDXR, SteamVR, or Oculus
  runtime selections remain authoritative.
- Adds a Pimax-only free-hand grip-basis fallback when palm pose data is absent.
  Support/weapon pose behavior is unchanged.
- Makes off-hand interaction ownership explicit: magazine/reload first, then a
  valid support grip, then a belt grenade.

### Mounted-machine-gun visual aiming

- Drives client-side `tag_aim` and `tag_aim_animated` from the tracked
  right-controller ray for mounted guns such as the emplacement in Bog.
- Uses the replicated `playerState.viewAngleClampBase` and
  `playerState.viewAngleClampRange`, keeping the visible model and server-fired
  bullets inside the same mechanical pitch/yaw arc.
- Keeps HMD look independent, preserves the fixed scoped Barrett's HMD-centered
  route, and retains the native non-VR camera fallback.

### Validation

- Retains all 142 settings/catalog checks and adds focused HUD, menu, DXGI 1.1,
  Pimax runtime/hand/interaction, and mounted-gun source contracts.
- The automated suite, Windows Release builds, package inventory, and guarded
  install/rollback fixtures pass.
- Real Pimax Crystal Light testing and Bog mounted-gun headset confirmation are
  still pending and are called out in the release notes.

## v0.10.0-beta.12

OpenVR compatibility, physical night-vision visor control, and controller
default update over `v0.10.0-beta.11`.

### OpenVR rendering and controller compatibility

- Corrects the direct OpenVR projection convention used for gameplay eyes.
- Preserves COD4's display-referred color bytes through the SteamVR-compatible
  BGRA8/Auto submission path, avoiding the dark image and compositor rejection
  produced by the earlier linear-color experiment.
- Resolves semantic grip and aim components from SteamVR render models instead
  of assuming each driver's raw controller origin is already a portable weapon
  pose.
- Adds a neutral-entry guard for legacy OpenVR thumbrest mission chords. Both
  sticks must begin centered, right-stick movement cancels the selector, and
  custom non-matching bindings are left alone.

### Physical night-vision visor gesture

- Adds one head-relative visor gesture state machine used by both OpenXR and
  OpenVR.
- Lowering begins with a new physical left-grip press at the crown; raising
  begins with a new press close to the visor. A completed pull toggles night
  vision exactly once when the grip is released.
- Requires at least 12 cm of vertical travel, a valid tracked pose, and a
  completed destination within 3.5 seconds; menus and tracking loss cancel the
  motion safely.
- Uses a compact face-close raising zone so a rifle foregrip below and farther
  in front of the headset remains normal support-hand input. A grip that began
  on the rifle cannot become a visor gesture mid-hold.

### Default binding and compatibility

- Makes physical Right grip / squeeze the fresh-profile default for Grenade
  launcher / weapon slot 5 in both the configurator and packaged defaults.
- Keeps Controller Input at schema V4. Existing profiles retain their saved
  grenade-launcher binding instead of being silently rewritten.
- Retains the complete beta.11 package, configurator, F.N.G., HUD, recenter,
  prompt, campaign, calibration, and interaction work.

### Validation

- The OpenVR geometry, brightness, compositor delivery, and controller angle
  path was verified on Meta Quest 3 through SteamVR.
- The shared visor state machine and both backend integration routes are
  covered by regression tests; the foregrip-safe gesture behavior was
  confirmed on Quest 3 through SteamVR/OpenVR.
- Retains all 142 settings/catalog checks and adds focused regression cases for
  the OpenVR selector, visor state machine, foregrip isolation, new default,
  and preservation of existing V4 bindings.

## v0.10.0-beta.11

HUD recovery, separated recentering, dynamic VR prompts, and F.N.G. campaign
repair over `v0.10.0-beta.10`.

### HUD-editor recovery

- Adds previous/next group selection that does not depend on hitting the
  group's current rectangle, so covered and off-screen HUD groups remain
  reachable.
- Adds Center selected and Reset selected actions. Center preserves scale;
  Reset restores only that group to the tested defaults.
- Shows the selected group in a fixed in-headset banner and exposes matching
  controller and keyboard recovery controls while preserving transactional
  Save and full-layout Cancel.

### Separated recenter controls

- Splits live recenter into Position only, Direction / level only, and Full.
  Position-only preserves the orientation baseline; direction/level-only
  preserves the positional origin; Full validates both before changing either.
- Replaces the ambiguous first-gameplay toggle with Off, Position only,
  Direction / level only, and Full. Legacy `0` and `1` profiles migrate to Off
  and Full without changing their previous behavior.
- Makes standing-height measurement preserve both recenter components and
  seated calibration recenter position only.

### Dynamic VR prompt labels

- Routes normal HUD binding prompts and single-player script `getKeyBinding()`
  lookups through one controller-aware label resolver while VR is active.
- Uses the configured primary and alternate slots, remaps, chords, directional
  inputs, active controller profile, and selected OpenXR/OpenVR backend instead
  of assuming Quest defaults.
- Keeps full-screen keyboard menus and unknown, ambiguous, or unbound actions
  on COD4's original keyboard resolver. This release uses text labels rather
  than controller-button artwork.

### F.N.G. campaign input and menu repair

- Mirrors physical/configured VR ADS and Sprint through isolated native command
  state so the scripted training and finish-line listeners receive the same
  semantics as the corresponding PC controls.
- Notifies native `+attack` listeners only for a newly accepted VR shot after
  the existing muzzle-obstruction check, without changing mouse state, fire
  cadence, or weapon simulation.
- Recognizes `select_difficulty` and its `diff_con_*` confirmations as centered
  active-mission dialogs, paints them once for both eyes, and gives their VR
  cursor the full selectable canvas.

### Validation

- Retains all 142 settings and compatibility/configurator checks and adds
  focused regression coverage for every beta.11 path.
- Verified on Meta Quest 3 through Virtual Desktop VDXR: HUD recovery,
  separated recentering, controller-aware prompts, the complete F.N.G. training
  flow, Sprint finish, and the fused controller-selectable difficulty menu.

## v0.10.0-beta.10

Focused OpenVR two-hand, Automatic-proximity, and campaign detonator repair
over `v0.10.0-beta.9`.

### OpenVR two-hand stabilization

- Moves the two-hand weapon target/blend update into a backend-shared step that
  runs after both OpenXR and OpenVR publish semantic weapon-hand and off-hand
  poses.
- Makes Hold and Automatic proximity rotate the weapon from the line between
  both controllers on OpenVR instead of attaching only the support-hand model.
- Preserves one-handed aiming, normal firearm input, handedness, haptics,
  calibration, and the existing OpenXR route.

### Automatic-proximity release

- Replaces the rendered support hand's permanent proximity latch with a
  hysteresis policy: attach at the configured grip radius and release four game
  units farther out.
- Keeps the rendered hand and two-hand weapon solver synchronized while
  avoiding rapid attach/release flicker at the boundary.
- Leaves Hold and Toggle behavior unchanged and preserves reload/grenade
  ownership of the off hand.

### Scripted C4 detonators

- Routes the configured Attack action directly to COD4's native detonation
  state machine only for grenade-class weapons whose definition has
  `hasDetonator` enabled.
- Fixes The Bog's ZPU anti-aircraft C4 charge without requiring a mouse click
  or the previous rifle-first weapon-cycle workaround.
- Keeps ordinary firearms on the tracked weapon-pose and muzzle-obstruction
  route and keeps ordinary grenades on their existing interaction path.

### Validation

- Retains the exact 142-setting compatibility/configurator suite and adds
  source and boundary guards for all three repairs.
- Verified on Meta Quest 3 through Virtual Desktop VDXR for Automatic
  proximity, the Bog detonator, firing, reloading, grenades, and two-hand aim.
- OpenVR two-hand motion was independently confirmed by a PICO 4 tester in both
  Hold and Automatic-proximity modes.

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
