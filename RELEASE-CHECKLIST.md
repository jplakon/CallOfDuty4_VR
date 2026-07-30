# Release checklist

This checklist is for `v0.9.0-beta.1`.

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
- Test the physical sniper scope.
- Test fixed Barrett, Javelin, Stinger, mounted turret, and vehicle weapon.
- Test the One Shot, One Kill helicopter sequence.
- Confirm the normal launcher hides developer errors and the red FPS marker.
- Confirm shadows can be toggled in `VR-Settings.bat`.
- Confirm the documented Death From Above skip method reaches the next mission.

## Tag and package

- Create annotated tag `v0.9.0-beta.1` on the tested source commit.
- Run `tools/package_release.py`.
- The packager must refuse to run if any documentation placeholder or
  controller-map `VERIFY` marker remains.
- Inspect the ZIP inventory.
- Verify the ZIP on a second clean COD4 directory or Windows account.
- Confirm `SOURCE.txt` points to the exact tag and commit.
- Confirm `SHA256SUMS.txt` matches all package files.
- Confirm the package contains no `.dll`, `.pdb`, `.iwd`, `.ff`, save, log, or
  original game executable.

## Publish

- Push `main` and the annotated tag to the public GitHub repository.
- Create a GitHub release entry containing release notes, source tag, Patreon
  link, requirements, and known issues.
- Do not attach the patron-only Windows ZIP to the public GitHub release.
- Upload the ZIP and its `.sha256` sidecar to the paid Patreon post.
- Include the public source tag URL in the Patreon post.
- Keep an offline copy of the exact ZIP, checksum, commit, tag, and permission
  correspondence.
