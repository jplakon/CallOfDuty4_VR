# KisakCOD VR

KisakCOD VR is a single-player OpenXR VR conversion for the original 2007
Windows release of Call of Duty 4: Modern Warfare. It adds stereoscopic
rendering, 6DoF headset tracking, motion-controller weapon aiming, physical
scope support, VR HUD placement, and campaign-specific compatibility fixes.

This project is based on [KisakCOD](https://github.com/SwagSoftware/KisakCOD).
It contains no Call of Duty game data and requires a legitimately installed
copy of the original game.

- [Download the current beta from GitHub](https://github.com/jplakon/CallOfDuty4_VR/releases)
- [Read supporter updates on Patreon](https://www.patreon.com/c/J_Play)

## Current status

The current public beta is `v0.10.0-beta.18`.

Beta.18 rebuilds rifle-attached sniper scopes around the weapon model's actual
lens surface. The magnified image and reticle now stay centered on that lens,
the dedicated scope camera renders at full panel resolution, near weapon/hand
geometry is excluded from the magnified view, and scope-camera visibility no
longer removes NPCs from the normal stereo eyes. This addresses the scope
rendering and transition failures in issues #41 and #83. The final scope path
passed a focused Quest 3 / Virtual Desktop VDXR headset test; Pimax hardware
confirmation is still welcome.

Controller Input V5 moves the default mission shortcuts away from the movement
stick, suppresses SteamVR's synthetic Valve Index Axis0 press so full stick
deflection cannot eject a magazine, and repairs the Configurator's Chord editor.
Only exact former defaults are migrated, so custom chords are preserved. These
changes address issues #52, #85, and #90 and passed the full automated settings
suite; native Index/SteamVR confirmation remains welcome.

Beta.18 also repairs issue #80's Bog Javelin view. The full optic is centered
per eye and the green lock-on boxes are projected in eye-local HUD coordinates
instead of the entire packed stereo surface. The complete sight and lock-on
flow passed a focused Quest 3 / Virtual Desktop VDXR headset test.

Beta.17 fixes issue #78's continuous cyan/green/blue world corruption in the
packed stereo Float-Z depth-clear path. Once an eye viewport is installed, the
clear quad now uses viewport-local coordinates instead of applying the packed
render-target origin a second time. Automated checks, the OpenXR Simulator,
and a focused Quest 3 / Virtual Desktop VDXR headset test with `r_zFeather 1`
and repeated two-hand/ADS transitions passed. Desktop/non-VR behavior is
unchanged; confirmation on the original reporter's Quest 3S system is still
welcome.

Beta.16 addresses issue #46's distant dog tumbling and
ground clipping. COD4's reconstructed ground-orientation path had swapped its
roll-angle and vertical-height outputs, treated a small negative slope as an
almost complete positive turn, and doubled the final pitch/roll rotation. The
complete corrected path matches current upstream KisakCOD and also repairs the
same ground-orientation errors for planted actor corpses.

It addresses issue #48's brief yellow/pink NPC and
vehicle frames during scripted screen flashes. Packed VR now saves one coherent
scope/stereo shellshock snapshot after the final view and blends only the
current eye's matching region, instead of capturing and resampling shared
feedback midway through each eye replay. Desktop/non-VR behavior is unchanged.

It addresses issue #49's stretched Ultimatum sky. The
packed stereo renderer now keeps eye-local film/color grading but isolates the
retail fullscreen glow, depth-of-field, and blur passes that can read across
packed view boundaries. Desktop/non-VR rendering is unchanged.

It addresses issue #64's weapon-triggered FPS collapse.
An old per-frame Javelin trace used level-local weapon slot `7`, which can also
identify the Blackout AK, and could synchronously write hundreds of diagnostic
lines per frame. Normal verbose diagnostics no longer activate that retired
trace; a separate hidden developer flag is required.

It addresses issue #66's wide or stretched gameplay on
SteamVR/OpenVR Performance mode. Packed rendering now takes each eye width from
the active backend, so `4768x2016` remains two `1872x2016` gameplay eyes plus
the dedicated 1024-pixel scope panel when a mission begins.

It addresses issue #65's mission-start crouch. Stance
inputs carried through a loading screen or menu must now return to neutral
before they can lower the player, while deliberate crouch and prone actions
continue normally after that release.

It fixes issue #67's continuously scrolling tank
tracks. Single-player vehicles now interpolate and submit the server-owned
material phase, keeping stopped tracks still while preserving smooth movement.

It fixes issue #61's original calibrated two-hand attach/release snap. The
support grip now anchors to the visible one-hand weapon pose at engagement and
applies later off-hand movement as a relative steering delta. Nonzero global,
per-weapon, and gunstock Pitch/Yaw/Roll remain in the blend origin, and release
fades from the last held steering delta instead of following the departing
off hand. Smaller off-center roll/pivot and magazine-hand alignment concerns
remain under investigation.

It addresses the legacy OpenVR floating off-hand pose in issue #76:
SteamVR raw/grip poses now use grip-frame anatomy instead of being mislabeled
as `palm_ext/pose`, while a real `openxr_handmodel` component remains the
preferred visual-palm source. Support grip, reload, aim, gestures, and native
OpenXR behavior are unchanged.

It addresses issue #75 by lowering HUD group scales and horizontal/vertical
safe areas to `0.25` for wide or canted-FOV headsets. Finite manual values
outside the supported range clamp to the nearest endpoint instead of making a
group unexpectedly revert to its larger default.

It also addresses issue #74 on the legacy OpenVR controller path. Joystick
contact no longer masquerades as an independent thumbrest touch, and the new
OpenVR-safe control preset separates guarded mission actions, Pause, and
weapon cycling.

Issue #67 and the original #61 snapping defect have reporter hardware
confirmation. Issues #46 and #75 also passed focused Quest 3 / VDXR headset
tests during beta.16 release validation; #75's original Apple Vision Pro / ALVR
configuration still awaits reporter confirmation. The other beta.16 changes
remain candidate fixes pending validation on their reported headset/runtime
combinations.

Beta.14 fixes the remaining stereo-menu and legacy-crosshair defects, adds
a full-FOV Pimax Crystal Light scope layout, routes Safehouse and Heat
air-support targeting through the right controller, makes the Configurator
fully resizable, and adds a guarded guided installer beside the portable ZIP.
Existing LocalAppData profiles and custom Controller Input V5 bindings remain
unchanged during an update.

### Beta.15 fixes

- Carries the post-beta.14 V98-V104 repairs: Index/OpenVR hand and squeeze-path work, right-safe ammo and grenade counters, the suppressed in-headset error overlay, canonical 4:3 menus, guarded OpenXR mission selection, and level-safe HMD yaw-only startup/recenter.
- Quest 3/OpenXR validation passed; the V104 near-vertical pose guard rejects unstable yaw instead of inventing one.

### Beta.14 fixes

- Keeps Mission Select artwork and text in the same eye-local geometry, centers
  Quit Game and Quit Mission dialogs in both eyes, and suppresses COD4's legacy
  flat crosshair in VR even when an older profile has `cg_drawCrosshair 1`.
- Adds the recommended Pimax Crystal Light Full FOV preset: the runtime's
  uncropped `4312x5102` recommendation at output scale `0.80` becomes two
  `3450x4082` eyes plus the 1024-pixel scope panel in a `7924x4082` packed
  surface. The older `7684x3128` / `1.00` cropped preset remains available;
  Quest modes are unchanged.
- Makes the Safehouse and Heat air-support ray, target marker, and strike
  placement follow the tracked right controller. It keeps the normal right
  glove stable and suppresses broken canned arms; the handheld device model is
  still invisible, which is cosmetic rather than a targeting blocker.
- Requests a true `1160x750` Configurator client area and adds resize,
  maximize, restore, and a minimum tracking size so rightmost and bottom
  controls cannot be clipped by Windows/DPI non-client metrics.
- Adds guided Windows Setup as the recommended download while retaining the
  portable ZIP. Setup finds Steam libraries, accepts manual Browse, validates a
  classic COD4 layout before writing, and never guesses how to rearrange an
  unsupported Microsoft/Xbox raw layout.
- Gives install/update/repair a stable identity, backs up and SHA-256-verifies
  every pre-existing managed file, restores those originals on uninstall, and
  preserves COD4 data, saves, and `%LOCALAPPDATA%\KisakCOD-VR` settings.
- Builds Setup and ZIP from one deterministic case-insensitive allowlisted
  payload with matching SHA-256 sidecars. Publishing fixes include Inno
  preprocessor line-break handling, literal `/DName=Value` definitions, Inno
  6/7 close-app compatibility, and escaped smoke-test AppIds.
- Preserves native Windows Setup switches under Git Bash, validates their exact
  JSON argument transport, and waits for Inno's second uninstall phase to write
  `Log closed.` before checking restoration. The final R8 lifecycle test passed
  incomplete-layout rejection, install, repair, uninstall, exact sentinel
  restoration, and game-data retention.
- Retains all 142 settings checks plus the installer-builder suite and focused
  source contracts. Real Pimax Full FOV and Bog mounted-gun headset confirmation
  are still requested from testers.

### Beta.13 fixes retained

- Uses one canonical HUD transform for the real compass ticker/objectives and
  their editor rectangle, and corrects normal-notification bounds so saved
  layouts and live artwork stay aligned.
- Samples frontend and pause menus once into the centered headset view while
  keeping controller cursor hit testing in eye-local coordinates. The normal
  COD4 crosshair now defaults to Off only for new or reset profiles.
- Uses the DXGI 1.1 factory/adapter interfaces required by the SteamVR OpenXR
  D3D11 interoperability path while retaining adapter-LUID and sync-texture
  guards.
- Adds a `7684x3128` Pimax Crystal Light layout: two `3330x3128` eyes plus the
  existing 1024-pixel physical-scope panel. Quest Native and Performance modes
  are unchanged.
- Selects Pimax's 32-bit OpenXR manifest only when Pimax is the active runtime,
  preserves an explicit `XR_RUNTIME_JSON`, and adds a Pimax-only grip-pose
  fallback plus magazine/support/grenade interaction ownership guards.
- Drives mounted-machine-gun `tag_aim` and `tag_aim_animated` from the tracked
  right-controller ray, clamped by COD4's replicated mechanical pitch/yaw
  limits. HMD look remains independent and the fixed scoped Barrett keeps its
  HMD-centered path.
- Retains all 142 configurator settings checks. The new source/runtime contracts
  pass; real Pimax Crystal Light and Bog mounted-gun headset confirmation are
  still requested from testers.

### Beta.12 fixes retained

- Corrects direct OpenVR projection, color transfer, compositor submission,
  and semantic grip/aim controller poses. The complete path was verified on
  Quest 3 through SteamVR without changing the primary VDXR/OpenXR route.
- Keeps legacy OpenVR controls safe when SteamVR lacks an independent
  thumbrest component: V105 disables the joystick-touch alias and provides a
  neutral-entry off-hand trigger + stick preset that also separates Pause from
  Next weapon.
- Adds a physical left-hand night-vision gesture on both backends: grip at the
  crown and pull the visor down, or grip close to the visor and pull it up,
  then release to toggle.
- Restricts visor arming to head/face start zones so a normal rifle-foregrip
  press is never converted into a gesture after the grip is already held.
- Changes the fresh-profile grenade-launcher default to the physical right
  grip. Existing saved bindings, including the earlier chord, are preserved.

### Beta.11 fixes retained

- Adds previous/next selection, center-selected, and reset-selected controls to
  the live in-headset HUD editor, so covered or off-screen groups can always be
  recovered without editing settings by hand.
- Separates position-only, direction/level-only, and full recenter actions.
  First-gameplay recenter exposes the same explicit modes and safely migrates
  the old Off/On values.
- Replaces common PC-key HUD prompts with text derived from the active VR
  controller profile and the user's configured primary, alternate, directional,
  and chord bindings. Keyboard menus and unknown actions retain keyboard text.
- Bridges accepted VR attack, ADS, and Sprint actions into the native command
  notifications used by F.N.G., allowing the training sequence and finish line
  to advance without a mouse or keyboard.
- Renders the F.N.G. difficulty recommendation and confirmation dialogs once as
  a fused centered image, with controller cursor access to the full menu.
- Retains all 142 exact settings checks and adds regression coverage for HUD
  recovery, split recentering, dynamic prompts, campaign input, and centered
  modal menus.

### Beta.10 fixes retained

- Runs the same two-hand weapon target update after either OpenXR or OpenVR
  publishes controller poses. On OpenVR, the weapon now follows the support
  hand instead of remaining driven only by the weapon hand.
- Makes Automatic proximity release the rendered support hand after it leaves
  a slightly larger exit radius. The separate enter/exit thresholds prevent
  flicker and keep the hand model synchronized with the two-hand weapon solver.
- Routes the configured Fire action to COD4's native detonation state machine
  for grenade-class `hasDetonator` weapons even when a scripted auto-equip has
  no rendered firearm aim pose. This fixes the C4 detonator at The Bog's ZPU
  anti-aircraft objective without changing ordinary firearm or grenade input.
- Retains all 142 exact settings checks and adds regression guards for the
  OpenVR two-hand path, Automatic-proximity hysteresis, and semantic detonator
  routing.

### Beta.9 features retained

- Adds one **Setup & Compatibility** page for the installed game/mod files,
  DirectX June 2010, GPU, 32-bit and 64-bit OpenXR registration, OpenVR
  fallback, and the last proven headset/controller session.
- Classifies each check as Pass, Warning, or Blocked, recommends a backend and
  Native/Performance graphics profile, and shows the exact changes before
  applying them. Personal controls, comfort, HUD, handedness, units, height,
  interactions, and calibration are preserved.
- Runs the same compatibility evaluator before launch and writes a
  support-ready `%LOCALAPPDATA%\KisakCOD-VR\Compatibility-Report.txt`.
- Adds Metric or Imperial presentation for all 20 physical measurements while
  preserving the exact game-compatible values underneath. Height refinement is
  1 cm in Metric mode and 1 in in Imperial mode.
- Adds persistent six-axis hip-fire and shouldered/ADS calibration for each
  equipped weapon, smoothly layered over the global weapon fit.
- Adds physical-gunstock profiles with explicit guided aim capture, live apply,
  reset/delete controls, and portable `.vrstock` import/export.
- Adds true right- or left-handed functional routing for the weapon, muzzle,
  scope, support hand, reload, grenades, HUD pointer, and haptics, with one-time
  mirroring of primary, alternate, directional, and chord bindings.
- Adds hold/toggle/proximity support grip, hold/toggle object grip, button or
  physical-pull magazine ejection, release/contact insertion, handed or fixed
  belts, gesture/button melee, haptic strength, and muzzle-obstruction options.
- Expands exact settings verification from 125 to 142 values and adds runtime
  receipts for compatibility, units, handedness, interactions, equipped weapon,
  gunstock, and effective weapon pose.

### Beta.9 fixes retained

- Caches the controller-independent native attachment separately for each
  weapon, preventing one gun's model alignment from being reused by another.
- Keeps per-weapon and gunstock translation through final grip-tag correction,
  allowing MP5, pistol, rifle, and launcher alignment to be tuned independently.
- Removes remaining hard-coded right-weapon/left-support assumptions from both
  OpenXR and OpenVR pose, interaction, pointer, and haptic paths.
- Prevents unit switching or unchanged saves from accumulating conversion
  drift, including safe round trips at the minimum and maximum height limits.
- Detects a missing or broken 32-bit runtime separately from a valid 64-bit
  OpenXR registration, so an unusable forced backend is blocked before launch.
- Uses one evaluator for the configurator, launcher, support report, and tests,
  preventing their Ready/Warning/Blocked decisions from drifting apart.
- Records actual runtime, headset, and controller evidence after a successful
  session instead of presenting registry-level detection as hardware proof.

Personal settings remain under LocalAppData so package updates preserve them.

- The single-player campaign is playable from beginning to end when
  **Death From Above** is skipped.
- **Death From Above is not supported in this beta.**
- Primary test configuration: Meta Quest 3, Virtual Desktop's OpenXR runtime,
  and an NVIDIA RTX 3080 Ti.
- Other OpenXR headsets and runtimes should be considered experimental until
  users confirm them.

See [KNOWN-ISSUES.md](KNOWN-ISSUES.md) before downloading.

## Requirements

- Windows 10 or Windows 11
- The original 2007 Call of Duty 4: Modern Warfare for Windows
- A working OpenXR runtime, or SteamVR for the compatibility backend
- A PC VR headset and motion controllers
- A VR-capable GPU

The precompiled Setup and portable ZIP are overlays for an existing COD4
installation. They include the exact 32-bit Steamworks, Bink, and Miles runtime
files linked by KisakCOD. Neither artifact includes `iw3sp.exe`, COD4 maps,
fastfiles, saves, or other Call of Duty game data.

## Install a precompiled build

1. Install and launch the original 2007 COD4 once.
2. Download `KisakCOD-VR-v…-Setup.exe` and its `.sha256` sidecar from the same
   release, then run Setup.
3. Confirm the detected COD4 folder, or browse to the folder containing
   `iw3sp.exe`, `localization.txt`, `main`, and `zone`.
4. Let Setup open `KisakCOD-VR-Configurator.exe`, then start the OpenXR runtime
   you intend to use.
5. On **Setup & Compatibility**, run the scan, resolve every Blocked item, and
   review the exact delta before applying its recommendation.
6. Choose any remaining comfort/input settings, then click **Save & Launch**.
   The batch launcher reruns the same preflight before starting the game.

Setup detects the registered Steam installation and additional Steam library
folders. It validates the game executable, localization, archives, and matching
language fastfile before writing anything. Update/repair uses the same stable
installer identity. Uninstall restores files that existed before Setup first
managed each path; LocalAppData settings and original game data are left alone.

The ZIP remains available as a portable/manual alternative: extract all of it
beside `iw3sp.exe`. Automatic conversion of the Microsoft/Xbox app's different
raw layout is not enabled until that edition's before/after file map has been
verified. Setup safely rejects an unrecognized layout instead of guessing or
moving licensed game files.

Full instructions are in [INSTALL.md](INSTALL.md).

## Build from source

Requirements:

- Visual Studio 2022 with C++ desktop development tools
- CMake 3.16 or newer
- DirectX SDK (June 2010)
- A legitimate COD4 installation for runtime data

Clone the repository and both OpenXR submodules:

```bash
git clone --recurse-submodules \
  https://github.com/jplakon/CallOfDuty4_VR.git

cd CallOfDuty4_VR
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release \
  --target KisakCOD-sp KisakCOD-VR-Configurator \
  KisakCOD-VR-Input-Mapper --parallel 8
```

The compiled executables are written to:

```text
bin/Release/KisakCOD-sp.exe
bin/Release/KisakCOD-VR-Configurator.exe
bin/Release/KisakCOD-VR-Input-Mapper.exe
```

It must be run from a directory containing the files supplied by the user's
own COD4 installation. See the upstream
[KisakCOD build notes](docs/KISAKCOD-UPSTREAM-README.md) for the underlying
runtime layout.

## Source and binary releases

The complete source for every distributed binary is published under the Git
tag named in that binary package's `SOURCE.txt`. GitHub contains source,
documentation, tags, and issue tracking. Patreon provides convenient
precompiled early-access packages and supporter updates.

KisakCOD and this derivative are distributed under the GNU General Public
License version 3. Recipients may copy and redistribute the GPL-covered source
and binaries under that license. See [LICENSE](LICENSE).

## Reporting bugs

Use the GitHub bug-report form and include:

- Mod version and source commit from `SOURCE.txt`
- Headset and OpenXR runtime
- GPU and CPU
- Mission and checkpoint
- Reproduction steps
- Relevant lines from `main/console.log`

Do not report the unsupported Death From Above mission as a new bug.

## Credits

- The KisakCOD contributors
- Infinity Ward and the original Call of Duty 4 development team
- The Khronos OpenXR project
- Tracy Profiler and the other upstream dependencies retained by KisakCOD
- Testers and Patreon supporters

Call of Duty, Call of Duty 4, and related names and assets belong to their
respective owners. This is an independently developed mod and is not an
official Call of Duty product.
