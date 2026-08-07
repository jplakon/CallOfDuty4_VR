# Release checklist

This checklist is for `v0.10.0-beta.3`.

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
- Build `KisakCOD-sp` in Release configuration from the committed source.
- Confirm `git status --porcelain --untracked-files=no` remains empty.
- Copy nothing from `bin/Debug` into the player package.
- Test `bin/Release/KisakCOD-sp.exe` in a clean COD4 installation.

## Runtime test

- Start a new campaign.
- Test save/resume and at least one checkpoint reload.
- Test ordinary rifles, grenades, use, reload, sprint, crouch/prone, and melee.
- Test both 45-degree snap and analog smooth turning, partial/full stick
  deflection, configured speed, neutral behavior, and right-stick-click melee.
- Test down stand -> crouch -> prone and up prone -> crouch -> stand -> jump,
  returning the stick to neutral between every stance step.
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
- Confirm the normal launcher hides developer errors and the red FPS marker.
- Confirm frontend and active-mission quit confirmations appear once, centered,
  and monoscopic in the headset.
- Run the intentional crash-recorder self-test, collect its Desktop ZIP, and
  confirm the report, minidump, and system summary are present.
- Confirm shadows can be toggled in `VR-Settings.bat`.
- Confirm the documented Death From Above skip method reaches the next mission.

## Tag and package

- Create annotated tag `v0.10.0-beta.3` only after the cumulative menu, turning, stance,
  crash-diagnostic, frame, camera, audio, tracked-hand, and reload state is validated.
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
