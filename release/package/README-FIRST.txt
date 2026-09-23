KisakCOD VR v@VERSION@
========================

REQUIREMENTS
------------

- Windows 10 or 11
- A legitimate installation of the original 2007 Call of Duty 4
- An active OpenXR runtime, or SteamVR for the compatibility backend
- A PC VR headset and motion controllers

The remastered game is not a substitute.

GUIDED SETUP (RECOMMENDED)
--------------------------

1. Download KisakCOD-VR-v@VERSION@-Setup.exe and its .sha256 sidecar from the
   same release.
2. Run Setup and confirm the detected COD4 folder, or browse to the folder
   containing iw3sp.exe, localization.txt, main, and zone.
   Microsoft/Xbox users should select that edition's Content folder; the
   experimental guarded fallback below handles the recognized raw layout.
3. Setup validates the language fastfiles before writing, preserves every
   pre-existing file it will replace, and opens the Configurator when done.
4. Start your OpenXR runtime. On Setup & Compatibility, rescan and resolve
   every Blocked item; review any recommendation before applying it.
5. Choose remaining personal settings and click Save & Launch. The launcher
   reruns the same preflight before starting the game.

Running Setup again performs an update/repair. Windows Installed apps and the
Start Menu uninstall shortcut remove managed mod files and restore the exact
files that existed before Setup first managed those paths. Personal settings
under LocalAppData are preserved.

PORTABLE ZIP (MANUAL ALTERNATIVE)
---------------------------------

1. In Steam, right-click the original COD4 and choose Manage > Browse local
   files.
2. Confirm that the folder contains iw3sp.exe, localization.txt, main, and
   zone.
3. Extract every file from this ZIP into that folder, beside iw3sp.exe.
4. Start your OpenXR runtime and run KisakCOD-VR-Configurator.exe.

MICROSOFT/XBOX PC LAYOUT (EXPERIMENTAL)
-------------------------------------

Guided Setup can copy missing .ff files from zone\(null) into the language
folder named by localization.txt when the normal code_post_gfx.ff is missing.
It requires the original executable, localization file and main game archives;
verifies copied files by SHA-256; preserves existing destinations and all raw
source files; and records created copies for guarded uninstall. Allow extra
disk space. The portable ZIP does not perform this conversion.

This is an installation candidate, not confirmed Microsoft/Xbox campaign
support. The affected edition still needs installation and campaign testing.
Setup rejects missing or unrecognized required files instead of downloading
or inventing game data. See INSTALL.txt for the exact checks and uninstall
behavior.

SETTINGS AND UPGRADES
--------------------

The configurator covers compatibility, comfort, graphics, visual
HUD/text/compass placement, weapon and hand fit, belt/reload/grenade
interactions, scope alignment, and controller-neutral action bindings.
Physical measurements can be shown in Metric or Imperial units. Per-weapon
hip/ADS fit and shareable physical-gunstock profiles layer over the global
weapon fit. Right- and left-handed functional routing, mirrored bindings, and
configurable grip, reload, belt, melee, haptic, and muzzle-obstruction behavior
are included. The Height & Recenter page separates position, direction/level,
and full recenter actions and calibrates standing or seated eye height. VR
gameplay prompts use the configured controller bindings instead of common PC
key labels, with profile-aware text and keyboard fallback for ambiguous cases.
Personal settings are stored separately and survive future updates. The setup
scan and launcher preflight write a Compatibility-Report.txt support receipt.
If you hand-edited VR-Settings.bat in an earlier beta, keep a copy and use the
configurator's Import button after installing. You can still run
Launch-KisakCOD-VR.bat directly with the last saved profile. Most settings need
a full game restart; recenter/height actions, the in-headset HUD editor, and
weapon/gunstock Apply live or guided capture can update a running
single-player mission.

Updating from a focused customer test build: the public launcher ignores any
leftover FOCUSED-OVERRIDES.bat unless KISAK_VR_ENABLE_FOCUSED_OVERRIDES=1 is
explicitly set. Leave this diagnostic opt-in unset for normal play. The old
file is left untouched; public packages include no focused overrides. Your
saved personal settings still load normally.

CONFIGURATOR
------------

- One Setup & Compatibility page for installation, DirectX, GPU, 32/64-bit
  OpenXR, OpenVR, and previous live headset/controller evidence.
- Explicit backend/graphics recommendations that preserve every unrelated
  personal setting, plus Copy/Open support-report actions.
- Tested comfort/performance/seated/minimal-HUD presets plus full custom mode.
- Snap or smooth turning, movement direction and stick deadzones.
- Optional physical shoulder ADS with configurable engage/release timing;
  sprint cancels automatic ADS until the weapon is lowered or support released.
- A draggable desktop HUD canvas plus live in-headset placement for five real
  HUD groups, with snap anchors and per-group resizing.
- Primary and alternate bindings for every action, using either controller.
- Directional stick/trackpad inputs and chords of up to four simultaneous
  inputs per slot.
- Press-to-bind capture through the configured OpenXR or SteamVR backend.
- Separate position-only, direction/level-only, and full live recenter,
  automatic standing measurement, position-safe seated calibration, and safe
  1 cm Metric or 1 in Imperial height adjustments.
- Per-weapon hip-fire and shouldered/ADS deltas over the global weapon fit.
- Active gunstock profiles with explicit aim capture and .vrstock sharing.
- Right- or left-handed weapon control with one-time binding mirroring.
- Configurable physical support/object grip, reload, belt, melee, haptics, and
  muzzle-obstruction behavior.
- Independent support-grip and magazine/object-grab bindings, plus a separate
  enemy-grenade throw-back action.
- Separate hurt-warning and death-quote text scales.
- Safe validation, profile import/export, and automatic settings backups.
- Save & Launch and one-run diagnostic launch buttons.

PHYSICAL INTERACTIONS
---------------------

- In the handed belt layout, Magazine / object grab at the off-hand hip draws
  a frag; at the weapon-hand hip it draws the equipped tactical grenade.
  The default object control is the off-hand grip, independently remappable
  from Support-hand grip. Looking down does not tilt the belt away from the hips.
- Supported magazines can eject by button or a guarded physical pull and
  insert on release or contact.
- Hold/cook normally and physically swing. Release the object control to throw
  in hold mode, or press it again in toggle mode.
- The Throw back enemy grenade binding defaults to the Reload button. It
  requests a native grenade return only while COD4's enemy-grenade prompt is
  active; outside that prompt the same button still reloads.
- Toggle night vision by gripping with the physical left controller at the
  crown and pulling the visor down, or gripping close to the visor and pulling
  it up, then releasing. Foregrip presses remain normal rifle support input.
- Fresh profiles use the physical right grip for the grenade-launcher shortcut;
  existing saved bindings are preserved.
- Mounted-machine-gun visuals follow the right-controller firing ray inside the
  weapon's mechanical pitch/yaw limits while HMD look remains independent.
- Every standard campaign mission is unlocked in Mission Select.

KNOWN LIMITATION
----------------

Death From Above is not supported in this beta and must be skipped. Left-handed
functional routing is implemented, but the authored glove/arm geometry is not
anatomically mirrored. Read
INSTALL.txt and KNOWN-ISSUES.txt before beginning that part of the campaign.

TESTED CONFIGURATION
--------------------

Meta Quest 3, Virtual Desktop OpenXR, and NVIDIA RTX 3080 Ti.
Additional OpenXR profiles and SteamVR controller compatibility remain
experimental on native hardware. A community PSVR2/OpenVR test reached
gameplay but reported weapon-orientation, magazine-visual, and binding issues;
see KNOWN-ISSUES.txt. Pimax Crystal Light has Full FOV and legacy cropped
render presets and guarded 32-bit runtime handling, but real Pimax hardware
confirmation of the remaining fixes is still pending.

SUPPORT AND SOURCE
------------------

Support/download page:
@PATREON_URL@

Source for this exact build:
@REPOSITORY_URL@/tree/@TAG@

Commit:
@COMMIT@

The complete KisakCOD VR source is available under GPLv3. See SOURCE.txt,
LICENSE-GPLv3.txt and THIRD-PARTY-NOTICES.txt. This package contains no original
COD4 maps, fastfiles or other game content. It includes the matching Steamworks,
Bink and Miles runtime files required by the rebuilt executable.
