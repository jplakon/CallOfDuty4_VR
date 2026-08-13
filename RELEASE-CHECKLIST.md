# Release checklist

This checklist is for `v0.10.0-beta.12` over `v0.10.0-beta.11`.

## Documentation blockers

- Replace `GITHUB_USERNAME_HERE`.
- Replace `PATREON_URL_HERE`.
- Replace `DEATH_FROM_ABOVE_SKIP_INSTRUCTIONS_HERE`.
- Replace `CONTROLLER_MAPPING_MUST_BE_VERIFIED_HERE` and every `VERIFY` control
  cell.
- Add public Infinity Ward permission wording only if the written permission
  expressly allows that wording. Keep the original correspondence archived
  privately.
- Run this from the repository root and require no results:

  ```bash
  rg -n \
    'GITHUB_USERNAME_HERE|PATREON_URL_HERE|DEATH_FROM_ABOVE_SKIP_INSTRUCTIONS_HERE|CONTROLLER_MAPPING_MUST_BE_VERIFIED_HERE|\bVERIFY\b' \
    README.md INSTALL.md CONTROLS.md
  ```

## Source checkpoint

- Save a binary diff backup of the uncommitted source.
- Preserve the original KisakCOD README under
  `docs/KISAKCOD-UPSTREAM-README.md`.
- Run `python tools/apply_local_excludes.py` to hide local diagnostic artifacts
  without deleting them or changing the public `.gitignore`.
- Stage tracked source with `git add -u`, then add only the named release files.
- Do not run `git add .`.
- Review `git diff --cached --stat`, `git diff --cached --check`, and
  `git status --short`.
- Commit the source and release files.

## Reproducible build

- Confirm both OpenXR submodules show a leading blank status character, not
  `-`, `+`, or `U`.
- Build `KisakCOD-sp`, `KisakCOD-VR-Configurator`,
  `KisakCOD-VR-Input-Mapper`, and `KisakCOD-VR-Configurator-Tests` in Win32
  Release configuration from the committed source.
- Run the settings tests against `release/package/VR-Settings.bat` and require
  all 142 catalog settings plus beta.12 compatibility, launcher, runtime, and
  configurator receipts to pass.
- Confirm `git status --porcelain --untracked-files=no` remains empty.
- Copy nothing from `bin/Debug` into the player package.
- Test `bin/Release/KisakCOD-sp.exe` in a clean COD4 installation.

## Runtime test

- Open **Setup & Compatibility** on the primary VDXR/Quest 3/RTX 3080 Ti
  system. Require file, DirectX, OS, GPU, and 32-bit OpenXR checks to pass;
  before the first beta.12 run, require honest headset/controller warnings.
- Apply the recommendation only after verifying its confirmation lists the
  exact backend/graphics delta. Snapshot all other settings before/after and
  require handedness, units, comfort, input, HUD, height, interactions, and
  weapon/gunstock calibration to remain identical.
- Run diagnostics, rescan, and require the runtime/headset/controller receipt
  to replace the offline warnings with live proof. Test Copy/Open report and
  inspect every report field for support relevance and bounded privacy.
- Test the compatibility matrix: VDXR OpenXR, another valid 32-bit OpenXR
  runtime, 64-bit-only OpenXR with and without OpenVR, forced OpenXR missing,
  forced OpenVR missing, missing DirectX, and an incomplete game/mod folder.
- Launch from the batch file with one Warning and one Blocked configuration.
  Require warnings to continue, blockers to stop before KisakCOD-sp.exe, and
  both paths to produce `Compatibility-Report.txt`.
- Start a new campaign.
- Test save/resume and at least one checkpoint reload.
- Test ordinary rifles, grenades, use, reload, sprint, crouch/prone, and melee.
- Switch from right- to left-handed and verify the actual weapon pose, muzzle,
  scope, support/reload/grenade roles, HUD pointer, movement/turn axes, and
  haptics change physical sides in both OpenXR and OpenVR.
- Customize a multi-input chord, change handedness, and confirm every binding
  mirrors exactly once. Switch back and require an exact round trip.
- Test semantic weapon/off-hand and fixed physical-left/right movement
  directions.
- Test hold, toggle, and proximity support grip plus hold/toggle object grip.
- Test button and physical-pull magazine ejection, release and contact
  insertion, all magazine-hip choices, and fixed/handed grenade belt layouts.
- Test button, gesture, and combined melee modes. Require a forward thrust over
  the speed threshold to trigger and reject a similarly fast sideways swing.
- Disable haptics, vary haptic strength, and disable muzzle obstruction; verify
  each accepted value appears in `Active-VR-Settings.txt`.
- Test both snap and analog smooth turning with the Turn action bound to the
  right and left axes, including partial/full deflection and neutral behavior.
- Test jump and tap/hold stance on several face, trigger, grip, click, and
  directional inputs across both hands. Confirm right-axis up appears under
  Jump, right-axis down appears under Lower stance, and left trigger is
  the alternate Jump default.
- Test primary and alternate bindings, deliberate duplicate warnings, clearing
  an alternate, profile import/export, and beta.7 value migration.
- Test the Chord editor with two through four simultaneous inputs, all eight
  left/right primary-axis directions, and V2/V3-to-V4 profile migration.
- Confirm a fresh profile binds Grenade launcher / slot 5 to physical Right
  grip / squeeze while an existing V4 profile retains its previously saved
  binding. Confirm the native Off-hand action remains unbound.
- In both OpenXR and OpenVR, lower and raise the night-vision visor with the
  physical left-grip gesture. Confirm a press begun on the rifle foregrip never
  arms later in the same hold and ordinary two-hand aiming remains unchanged.
- In OpenVR, confirm the remaining right-thumbrest + left-direction mission
  chords require neutral entry and cannot fire while walking and turning.
- Test **Bind...** for boolean and vector actions through OpenXR, Escape cancel,
  timeout/error reporting, and missing-helper handling.
- On available native hardware, test Touch, Index, Vive/trackpad, PICO, and
  Mixed Reality profile mappings; record the active interaction-profile log.
- Force `KISAK_VR_BACKEND=openvr` and test movement, turning, fire, ADS,
  reload, use, support grip, menu input, controller poses, and haptics.
- In OpenVR, test two-hand stabilization with Hold and Automatic proximity.
  Require the weapon to follow both controllers, then return cleanly to
  one-handed aim when the support action or distance no longer qualifies.
- With Automatic proximity, repeatedly cross the configured pickup radius.
  Require attachment on entry, no boundary flicker, and rendered-hand plus
  weapon-solver release only after crossing the four-unit larger exit radius.
- In The Bog, plant the ZPU anti-aircraft charge and use the mission-presented
  detonator with the configured Fire action. Do not click the mouse or cycle a
  rifle first. Require the native detonation sequence and the
  `[VR][DETONATOR]` route marker.
- After the detonator test, recheck rifle/pistol fire, muzzle obstruction,
  ordinary grenades, manual reload, Hold-mode two-hand aim, and OpenXR.
- Test all four first-gameplay modes: Off, Position only, Direction / level
  only, and Full. Verify legacy `0` and `1` profiles load as Off and Full.
- In a running OpenXR mission, deliberately offset both physical position and
  facing. Verify **Recenter position only** zeros translation without changing
  facing/level, **Recenter direction / level only** changes facing/level without
  moving the positional origin, and **Full recenter** changes both.
- In F.N.G./Killhouse, verify movement, sprint, stance, lower-stance,
  jump/mantle, melee, use, reload, and fire tutorials show the configured VR
  text rather than PC keys wherever COD4 uses a binding token.
- At Station One, enter physical/configured VR ADS and require the initial ADS
  tutorial to continue without right-click. After the hip-fire targets rise,
  fire at least one accepted VR shot and require a V74 `+attack` marker with
  one or more matched script listeners; after every target falls, require the
  plywood instruction without touching the mouse. At the course endpoint, use
  the configured VR Sprint action and require completion without Shift.
  Confirm the V73 native `+speed`, V74 virtual `+attack`, and V72 native
  `+sprint` markers.
- Remap Use from the Touch default X to B, restart, and require the same
  interaction prompt to change to B. Restore the original binding afterward.
- Give one mapped action identical primary and alternate bindings and require
  one label; then use two different slots and require both with the localized
  OR conjunction. Verify a cross-hand chord preserves every term.
- Open COD4's full-screen keyboard controls menu and confirm it still displays
  keyboard bindings. Verify an unbound or ambiguous VR action also retains its
  keyboard fallback.
- Repeat one prompt through forced OpenVR and confirm its label describes the
  legacy component mapping rather than assuming the OpenXR profile.
- Repeat the component-isolation test through OpenVR. Confirm no action changes
  the OpenXR or SteamVR system origin and that a failed Full request changes
  neither component when either required tracked component is unavailable.
- Test standing height measurement with an OpenXR `STAGE` space and OpenVR's
  standing universe. Require position and direction/level to remain unchanged,
  then verify the explicit manual-height fallback on a runtime without a usable
  floor reference.
- Test seated calibration plus all three fine-height buttons. Verify that each
  action applies live, seated calibration recenters position only, crouch/prone
  steps survive, and every receipt records request, height, backend, floor
  availability, explicit recenter mode, and result.
- Open the desktop visual HUD editor. Drag and resize all five groups, exercise
  snapping plus Shift/free movement, cycle selection without clicking a group,
  center and reset one group, restore defaults, apply, save, restart, and
  verify the exact placement round-trip.
- In a running mission, open **Edit live in headset**. Test ray selection,
  trigger drag, right-stick resizing, left-grip snap bypass, A/pointed Save,
  and B/pointed Cancel. Put the compass fully outside the canvas, select it via
  X/Y and Shift+Tab/Tab, recover it via L3/Home, reset only it via R3/End, and
  verify the fixed selected-group banner. Confirm gameplay input is suppressed
  while active.
- Trigger ammo/equipment, compass objective icons, normal notifications, a bold
  objective/status banner, and subtitles. Verify each group moves independently
  and that the crosshair remains at optical center.
- Confirm `Active-VR-Settings.txt` records all five accepted groups and a
  `STATUS=RUNTIME_HUD_EDITOR_SAVED` or canceled receipt with the request ID.
- Create separate MP5 and pistol profiles, verify their hip corrections do not
  cross-contaminate, and confirm deleting either falls back to the global fit.
- Create two gunstock profiles, switch the active stock, export/import one as
  `.vrstock`, and verify exact file read-back plus live runtime revision.
- Capture a shouldered aim rotation with the controller held normally, restart
  with the controller pointed upward, and confirm startup pose never changes
  the saved calibration or absolute live aim.
- Exercise hip-to-two-hand, ADS, release, weapon switching, muzzle obstruction,
  bullets, haptics, and grip-tag alignment with nonzero profile translations.
- Test ejecting, hip-drawing, rotating, inserting, and dropping a physical
  magazine on supported rifles, SMGs, and pistols.
- Test the tracked off hand in free, rifle-grip, and magazine-grip states on
  both dominant-hand selections. Record the known non-mirrored authored glove
  geometry separately from functional hand-routing defects.
- Confirm physical body turns and snap turns do not change hand alignment.
- Test the physical sniper scope.
- Confirm `3072x1536` is rejected and both documented packed presets retain the
  dedicated physical-scope source.
- Confirm gameplay `[VR][PERF]` lines report exact-pose matches without fallback
  and identify any bridge misses, consumer skips, or long reuse streaks.
- Confirm normal play produces no per-stage Javelin trace or periodic
  controller/hand diagnostics unless `KISAK_VR_VERBOSE_DIAGNOSTICS=1` is set.
- Test fixed Barrett, Javelin, Stinger, mounted turret, and vehicle weapon.
- Test Blackout rappel and One Shot, One Kill sprint-direction changes for a
  level horizon while confirming physical HMD pitch and roll still work.
- Test the One Shot, One Kill helicopter sequence.
- Test the Airlift helicopter sequence and other long looping sounds for
  scratchy, metallic, doubled, or corrupted playback.
- Confirm the normal launcher shows no FPS/stat marker and that the overlays
  appear only after their diagnostic dvars are explicitly enabled.
- Confirm frontend and active-mission quit confirmations appear once, centered,
  and monoscopic in the headset.
- Run the intentional crash-recorder self-test, collect its Desktop ZIP, and
  confirm the report, minidump, and system summary are present.
- Confirm shadows can be toggled in `VR-Settings.bat`.
- Confirm the documented Death From Above skip method reaches the next mission.

## Tag and package

- Create annotated tag `v0.10.0-beta.12` only after the compatibility,
  Metric/Imperial, per-weapon/gunstock, handed-interaction, controller, menu,
  rendering, crash-diagnostic, tracked-hand, reload, grenade, OpenVR two-hand,
  Automatic-proximity release, detonator, and campaign state is validated.
- Run `tools/package_release.py`.
- The packager must refuse to run if any documentation placeholder or
  controller-map `VERIFY` marker remains.
- Inspect the ZIP inventory.
- Verify the ZIP on a second clean COD4 directory or Windows account.
- Confirm `SOURCE.txt` points to the exact tag and commit.
- Confirm `SHA256SUMS.txt` matches all package files.
- Confirm the package contains only the allowlisted Steamworks, Bink, and
  Miles runtime files from the KisakCOD dependency tree, the corrected crash
  collector, and required OpenVR license; confirm it contains no `.pdb`, `.iwd`,
  `.ff`, save, log, dump, or original game executable.

## Publish

- Push `main` and the annotated tag to the public GitHub repository.
- Create a GitHub release entry containing release notes, source tag, Patreon
  link, requirements, and known issues.
- Attach the verified Windows ZIP and `.sha256` sidecar to the GitHub
  release.
- Link the GitHub release and exact public source tag from Discord and Patreon.
- Keep an offline copy of the exact ZIP, checksum, commit, tag, and permission
  correspondence.
