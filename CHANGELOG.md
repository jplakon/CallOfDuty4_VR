# Changelog

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
