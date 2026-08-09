# Changelog

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
