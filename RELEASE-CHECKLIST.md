# Release checklist

This checklist is for the `v0.10.0-beta.8` release candidate.

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
  all 125 catalog settings plus the launcher/runtime/configurator receipts to
  pass.
- Confirm `git status --porcelain --untracked-files=no` remains empty.
- Copy nothing from `bin/Debug` into the player package.
- Test `bin/Release/KisakCOD-sp.exe` in a clean COD4 installation.

## Runtime test

- Start a new campaign.
- Test save/resume and at least one checkpoint reload.
- Test ordinary rifles, grenades, use, reload, sprint, crouch/prone, and melee.
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
- Confirm the default right grip performs no action and every default mission
  shortcut is displayed as its right-thumbrest + left-direction chord.
- Test **Bind...** for boolean and vector actions through OpenXR, Escape cancel,
  timeout/error reporting, and missing-helper handling.
- On available native hardware, test Touch, Index, Vive/trackpad, PICO, and
  Mixed Reality profile mappings; record the active interaction-profile log.
- Force `KISAK_VR_BACKEND=openvr` and test movement, turning, fire, ADS,
  reload, use, support grip, menu input, controller poses, and haptics.
- Test first-gameplay recenter both enabled and disabled. In a running mission,
  verify **Recenter now** captures position and forward/level after its
  countdown without changing the OpenXR or SteamVR system origin.
- Test standing height measurement with an OpenXR `STAGE` space and OpenVR's
  standing universe, then verify the explicit manual-height fallback on a
  runtime without a usable floor reference.
- Test seated calibration plus all three fine-height buttons. Verify that each
  action applies live, preserves crouch/prone steps, survives restart, and
  records its request, height, backend, floor availability, and recenter result.
- Open the desktop visual HUD editor. Drag and resize all five groups, exercise
  snapping plus Shift/free movement, reset one group, restore defaults, apply,
  save, restart, and verify the exact placement round-trip.
- In a running mission, open **Edit live in headset**. Test ray selection,
  trigger drag, right-stick resizing, left-grip snap bypass, A/pointed Save,
  and B/pointed Cancel. Confirm gameplay input is suppressed while active.
- Trigger ammo/equipment, compass objective icons, normal notifications, a bold
  objective/status banner, and subtitles. Verify each group moves independently
  and that the crosshair remains at optical center.
- Confirm `Active-VR-Settings.txt` records all five accepted groups and a
  `STATUS=RUNTIME_HUD_EDITOR_SAVED` or canceled receipt with the request ID.
- Test ejecting, hip-drawing, rotating, inserting, and dropping a physical
  magazine on supported rifles, SMGs, and pistols.
- Test the tracked left hand in free, rifle-grip, and magazine-grip states.
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

- Create annotated tag `v0.10.0-beta.8` only after Controller Input V4 and the
  cumulative menu, rendering, crash-diagnostic, tracked-hand, reload, grenade,
  and campaign state are validated.
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
