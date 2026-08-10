KisakCOD VR v@VERSION@
========================

REQUIREMENTS
------------

- Windows 10 or 11
- A legitimate installation of the original 2007 Call of Duty 4
- An active OpenXR runtime, or SteamVR for the compatibility backend
- A PC VR headset and motion controllers

The remastered game is not a substitute.

INSTALL
-------

1. In Steam, right-click the original COD4 and choose Manage > Browse local
   files.
2. Confirm that the folder contains iw3sp.exe.
3. Extract every file from this ZIP into that folder, beside iw3sp.exe.
4. Start your OpenXR runtime.
5. Run KisakCOD-VR-Configurator.exe, choose a preset, and click Save & Launch.

The configurator covers comfort, graphics, visual HUD/text/compass placement, weapon
and hand fit, belt/reload/grenade interactions, scope alignment, and
controller-neutral action bindings. Its Height & Recenter page can capture a
new forward pose and calibrate standing or seated eye height. Personal settings
are stored separately and survive future updates.
If you hand-edited VR-Settings.bat in an earlier beta, keep a copy and use the
configurator's Import button after installing. You can still run
Launch-KisakCOD-VR.bat directly with the last saved profile. Most settings need
a full game restart; beta.8 recenter/height actions and its in-headset HUD editor
can apply live to a running single-player game.

CONFIGURATOR
------------

- Tested comfort/performance/seated/minimal-HUD presets plus full custom mode.
- Snap or smooth turning, movement direction and stick deadzones.
- A draggable desktop HUD canvas plus live in-headset placement for five real
  HUD groups, with snap anchors and per-group resizing.
- Primary and alternate bindings for every action, using either controller.
- Directional stick/trackpad inputs and chords of up to four simultaneous
  inputs per slot.
- Press-to-bind capture through the configured OpenXR or SteamVR backend.
- Guided live recenter, automatic standing measurement, seated calibration,
  and safe 1-inch height adjustments.
- Safe validation, profile import/export, and automatic settings backups.
- Save & Launch and one-run diagnostic launch buttons.

PHYSICAL INTERACTIONS
---------------------

- Left grip at the left hip draws a frag grenade.
- Left grip at the right hip draws the equipped flashbang or smoke grenade.
- Hold/cook normally, physically swing, and release the grip to throw.
- Every standard campaign mission is unlocked in Mission Select.

KNOWN LIMITATION
----------------

Death From Above is not supported in this beta and must be skipped. Read
INSTALL.txt and KNOWN-ISSUES.txt before beginning that part of the campaign.

TESTED CONFIGURATION
--------------------

Meta Quest 3, Virtual Desktop OpenXR, and NVIDIA RTX 3080 Ti.
Additional OpenXR profiles and SteamVR controller compatibility are
experimental until confirmed on their native hardware.

SUPPORT AND SOURCE
------------------

Support/download page:
@PATREON_URL@

Source for this exact build:
@REPOSITORY_URL@/tree/@TAG@

Commit:
@COMMIT@

The complete source is available under GPLv3. See SOURCE.txt and
LICENSE-GPLv3.txt. This package contains no original COD4 game data or
proprietary game DLLs.
