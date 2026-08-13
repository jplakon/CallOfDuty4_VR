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

The current public beta is `v0.10.0-beta.12`.

Beta.12 adds one physical night-vision visor gesture shared by OpenXR and
OpenVR, repairs the tested Quest 3 SteamVR/OpenVR rendering and controller-pose
path, and makes the physical right grip the default grenade-launcher shortcut
for fresh profiles. Existing personal settings and Controller Input V4
bindings remain unchanged during an update.

### Beta.12 fixes

- Corrects direct OpenVR projection, color transfer, compositor submission,
  and semantic grip/aim controller poses. The complete path was verified on
  Quest 3 through SteamVR without changing the primary VDXR/OpenXR route.
- Guards legacy OpenVR thumbrest mission chords behind a neutral-entry
  selector, preventing ordinary walking plus right-stick turning from firing
  mission shortcuts.
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

The precompiled package is an overlay for an existing COD4 installation. It
includes the exact 32-bit Steamworks, Bink, and Miles runtime files linked by
KisakCOD. It does not include `iw3sp.exe`, COD4 maps, fastfiles, saves, or
other Call of Duty game data.

## Install a precompiled build

1. Install and launch the original COD4 once.
2. In Steam, open **Manage → Browse local files**.
3. Extract the contents of the KisakCOD VR ZIP into that folder, beside
   `iw3sp.exe`.
4. Start the OpenXR runtime you intend to use.
5. Run `KisakCOD-VR-Configurator.exe`.
6. On **Setup & Compatibility**, run the scan, resolve every Blocked item, and
   review the exact delta before applying its recommendation.
7. Choose any remaining comfort/input settings, then click **Save & Launch**.
   The batch launcher reruns the same preflight before starting the game.

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
