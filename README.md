# KisakCOD VR

KisakCOD VR is a single-player OpenXR VR conversion for the original 2007
Windows release of Call of Duty 4: Modern Warfare. It adds stereoscopic
rendering, 6DoF headset tracking, motion-controller weapon aiming, physical
scope support, VR HUD placement, and campaign-specific compatibility fixes.

This project is based on [KisakCOD](https://github.com/SwagSoftware/KisakCOD).
It contains no Call of Duty game data and requires a legitimately installed
copy of the original game.

[Get the current precompiled beta and supporter updates](https://www.patreon.com/c/J_Play)

## Current status

The first public target is `v0.9.0-beta.1`.

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
- A working OpenXR runtime
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
5. Run `Launch-KisakCOD-VR.bat`.
6. Edit `VR-Settings.bat` if the default Quest 3 settings are too demanding or
   the HUD/scope needs calibration.

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
cmake --build build --config Release --target KisakCOD-sp --parallel 8
```

The compiled executable is written to:

```text
bin/Release/KisakCOD-sp.exe
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
