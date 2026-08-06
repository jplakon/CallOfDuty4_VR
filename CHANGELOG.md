# Changelog

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
