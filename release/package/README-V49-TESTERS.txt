KisakCOD VR V49 - private x86 SteamVR fallback proof
=====================================================

Purpose
-------
This build is for 32-bit COD4 installations where SteamVR or Pimax registers
only a 64-bit OpenXR runtime. V49 still tries OpenXR first. If the 32-bit
OpenXR loader reports that no compatible runtime is available, V49 starts
SteamVR through its installed 32-bit OpenVR client (vrclient.dll).

Do not copy, rename, or replace any SteamVR, Pimax, OpenXR, or DirectX DLL.

First-proof scope
-----------------
- included: SteamVR application registration, stereo D3D11 submission,
  per-eye projection, headset rotation/position, existing packed game capture;
- intentionally not included yet: OpenVR motion-controller input and haptics.

Use keyboard/mouse or a gamepad only to reach a mission for this first test.

Test
----
1. Extract every file into the original Call of Duty 4 folder, beside iw3sp.exe.
2. Start Pimax Play and SteamVR; confirm the headset is awake.
3. Run Launch-KisakCOD-VR.bat. Do not launch KisakCOD-sp.exe directly.
4. Confirm whether SteamVR shows KisakCOD as the active scene application.
5. Confirm whether both eyes show the game and whether head rotation tracks.
6. Close the game normally.

Return these files privately
----------------------------
- OpenXR-Startup.log
- main\console.log

The success markers are:

  [VR][STARTUP] Active V49 backend: OpenVR/SteamVR.
  [VR][OPENVR] Received the first valid predicted HMD pose from SteamVR.
  [VR][OPENVR] V49 submitted the first stereo D3D11 frame to SteamVR

If the game crashes, run Collect-KisakCOD-VR-Crash-Report.bat and share the
generated ZIP privately. Minidumps contain limited process memory.

Backend override (diagnostics only)
-----------------------------------
The default is automatic. To bypass OpenXR for one diagnostic launch, open a
Command Prompt in the game folder and run:

  set KISAK_VR_BACKEND=openvr
  Launch-KisakCOD-VR.bat

Remove that temporary Command Prompt or use KISAK_VR_BACKEND=auto afterward.

V49 is a private diagnostic proof, not a public release.
