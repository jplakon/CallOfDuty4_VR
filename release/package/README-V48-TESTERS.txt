KisakCOD VR V49 R2 private crash-diagnostics update
===================================================

Purpose
-------
This is a private diagnostic update for the Rank 2 crash investigation. It is
not a public release and it does not claim to fix the crash yet.

This package includes crash-collector revision
V49-R2-crash-collector-hotfix-R2. It accepts intentional blank separator lines
and reports each physical-memory field once with the correct byte conversion.

Install
-------
1. Start from a clean KisakCOD VR v0.10.0-beta.2 installation.
2. Close COD4.
3. Extract every file from this update into the original Call of Duty 4 game
   folder and allow Windows to replace KisakCOD-sp.exe and
   Launch-KisakCOD-VR.bat.
4. Continue launching only through Launch-KisakCOD-VR.bat.

After a crash
-------------
1. Do not relaunch the game first.
2. Double-click Collect-KisakCOD-VR-Crash-Report.bat.
3. Attach the complete KisakCOD-VR-Crash-Report-*.zip it creates on the
   Desktop. A screenshot or console.log alone is not enough.

The collector includes the newest crash text/minidump pair, console and OpenXR
startup logs, VR settings, executable identity, and basic OS/GPU/runtime
details. It does not deliberately collect saves, COD4 profile/config files, or
browser data. A minidump contains limited game-process memory and can include
incidental strings, so share the ZIP privately. Logs and dumps can contain
absolute paths, including the Windows account-folder name.

Virtual Desktop is not required to run the collector. It is required only when
the crash being reproduced occurs during an actual Virtual Desktop VR session.

If no crash dump appears
------------------------
Run the collector anyway. KisakCOD-VR-Last-Session.txt and the other logs can
still distinguish a normal exit, fatal engine error, abrupt termination, and a
failure before the recorder was installed.
