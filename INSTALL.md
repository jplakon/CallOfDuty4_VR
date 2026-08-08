# Installing KisakCOD VR

## Before you start

You need the original 2007 Windows version of Call of Duty 4: Modern Warfare.
The remastered game is not a substitute. The KisakCOD VR package includes
the matching 32-bit Steamworks, Bink, and Miles runtime files required by the
rebuilt executable, but it does not contain COD4 maps, fastfiles, saves, or
other Call of Duty game data.

This beta is tested primarily with:

- Meta Quest 3
- Virtual Desktop using VDXR, its OpenXR runtime
- NVIDIA RTX 3080 Ti

For Quest headsets, VDXR is the recommended and known-working runtime. Meta
Quest Link's 32-bit OpenXR runtime can crash inside `xrCreateSession` on
affected systems. Other headset and runtime configurations may work but are
experimental.

## Installation

1. Install COD4 through Steam and launch its single-player mode once.
2. In the Steam library, right-click the game.
3. Choose **Manage → Browse local files**.
4. Confirm that the opened folder contains `iw3sp.exe` and a `main` folder.
5. Extract every file from the KisakCOD VR ZIP directly into this folder.
6. Start the intended runtime. Quest users should use Virtual Desktop with
   VDXR; the SteamVR/OpenVR backend is experimental.
7. Double-click `Launch-KisakCOD-VR.bat`.

The mod executable must remain beside `iw3sp.exe`. Do not replace or rename
`iw3sp.exe`.

## Adjusting VR settings

Open `VR-Settings.bat` in Notepad. The supplied defaults reproduce the
developer's current Quest 3 configuration:

- Automatic backend selection (`auto`)
- Render/window mode: `6016x2688`
- GPU bridge enabled
- Native output scale
- FSR disabled
- Brightness: `1.00` (neutral after sRGB correction)
- Synchronized shadows enabled
- Physical scope capture: `1024`
- Physical magazine reloading enabled
- Physical hip-drawn grenades enabled
- Controller-tracked hands enabled
- 45-degree snap turning enabled by default

If performance is poor, change both packed-render settings to the supported
lower preset:

```bat
set "VR_CUSTOM_MODE=4768x2016"
set "KISAK_VR_OUTPUT_SCALE=0.75"
```

Do not use `3072x1536`. It cannot contain both rectangular eye images and the
dedicated physical-scope panel. Fully close and restart the game after changing
environment settings.


### Turning mode

Snap turning remains the default:

```bat
set "KISAK_VR_TURN_MODE=snap"
```

To use analog smooth turning, set:

```bat
set "KISAK_VR_TURN_MODE=smooth"
set "KISAK_VR_SMOOTH_TURN_SPEED=120"
```

The smooth-turn speed is full-stick degrees per second and accepts values from
30 through 360. Fully close and restart COD4 after changing either value.

### Manual grenades

Manual grenades are enabled by default:

```bat
set "KISAK_VR_MANUAL_GRENADES=1"
```

With the left grip released, squeeze at the left hip for a frag or the right
hip for the mission-equipped flashbang/smoke grenade. Keep holding to hold or
cook it, then physically swing and release the grip. Set the value to `0` to
restore beta.5's right-grip tactical and left-Y hold-frag controls.

### Runtime backend

The default setting is:

```bat
set "KISAK_VR_BACKEND=auto"
```

Available values are:

- `auto` keeps the normal behavior: OpenXR remains primary, with the
  experimental 32-bit SteamVR/OpenVR client used only when no compatible
  32-bit OpenXR runtime can be enumerated.
- `openxr` forces OpenXR. Use this with Virtual Desktop's VDXR or Pimax
  OpenXR.
- `openvr` forces the experimental 32-bit SteamVR/OpenVR fallback. It currently
  provides headset tracking and stereo rendering but no motion-controller
  input.

Changing this setting does not make SteamVR a 32-bit OpenXR runtime. Fully
close and restart COD4 after changing it.

The HUD controls are also in `VR-Settings.bat`. Lowering
`KISAK_VR_HUD_SAFE_X` moves the left-side ammo/action information farther
right. Scope offsets are measured in meters.

## Normal and diagnostic launchers

- `Launch-KisakCOD-VR.bat` hides development warnings from the headset while
  retaining a log.
- `Launch-KisakCOD-VR-Diagnostics.bat` enables developer messages when a bug
  must be investigated.

Logs are normally written below the game directory in:

```text
main\console.log
```


## Crash reports

If the game crashes, do not reopen it. Run
`Collect-KisakCOD-VR-Crash-Report.bat` from the COD4 folder and send the ZIP
created on the Windows Desktop privately. A minidump can contain limited
process memory and should not be posted publicly.

## Known campaign limitation

Every standard campaign mission is unlocked in Mission Select at startup,
including on a new profile. Developer mode remains disabled.

**Death From Above is not supported in `v0.10.0-beta.6`, even though it appears
unlocked.**

To skip **Death From Above**:

1. From the main menu, open **Options -> Game Options** and set
   **Enable Console** to **Yes**.
2. Press `~` to open the console. If necessary, use `Shift+~`.
3. Enter `/spmap bog_b` and press Enter.
4. This loads **War Pig**, the mission immediately following
   **Death From Above**.

Loading `bog_b` directly does not mark Death From Above as completed in the
original profile. If the campaign menu later returns to the unsupported
mission, repeat `/spmap bog_b`.

## Common problems

### The launcher says it is in the wrong folder

Move the extracted files beside `iw3sp.exe`. Do not run the launcher from a
Downloads subfolder.

### A required DLL is missing

Confirm that `steam_api.dll`, `binkw32.dll`, and `mss32.dll` were extracted
beside `KisakCOD-sp.exe`, and that the package's `miles` folder was merged into
the game's `miles` folder. Do not download individual DLLs from third-party
sites. For a missing DirectX DLL, install Microsoft's official legacy DirectX
runtime.

### The game opens but no image appears in the headset

With `auto` or `openxr`, start the intended OpenXR runtime first and make it the
active system OpenXR runtime. With `openvr`, start SteamVR and connect the
headset before launching the mod. Then restart both the runtime and the game.

### Meta Quest Link crashes during OpenXR startup

Meta's 32-bit OpenXR runtime can crash inside `xrCreateSession` after the mod
has successfully created its D3D11 device. Because the runtime terminates the
process instead of returning an OpenXR error, `KISAK_VR_BACKEND=auto` cannot
continue to the OpenVR fallback.

For Quest headsets, use Virtual Desktop with VDXR. To select VDXR only for
KisakCOD VR without changing the system-wide OpenXR runtime, add this to
`VR-Settings.bat` using Virtual Desktop's default installation path:

```bat
set "XR_RUNTIME_JSON=C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr-32.json"
```

Leave `KISAK_VR_BACKEND=auto` selected. Fully close COD4 and restart it through
`Launch-KisakCOD-VR.bat`.

### Performance is poor

Edit `VR-Settings.bat`, change `VR_CUSTOM_MODE` from `6016x2688` to
`4768x2016`, change `KISAK_VR_OUTPUT_SCALE` from `1.0` to `0.75`, save, and
fully restart the game. Shadows can also be disabled by changing:

```bat
set "KISAK_VR_SHADOWS=1"
```

to:

```bat
set "KISAK_VR_SHADOWS=0"
```

### The scope or HUD is misplaced

Adjust the commented scope and HUD variables in `VR-Settings.bat` in small
increments, then fully restart the game.
