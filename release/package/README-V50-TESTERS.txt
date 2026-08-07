KisakCOD VR V50 - configurable snap and smooth turning

V50 adds an opt-in analog smooth-turn mode while preserving the existing
45-degree snap turn as the default.

Configure VR-Settings.bat before launching:

  set "KISAK_VR_TURN_MODE=snap"

or:

  set "KISAK_VR_TURN_MODE=smooth"
  set "KISAK_VR_SMOOTH_TURN_SPEED=120"

KISAK_VR_SMOOTH_TURN_SPEED is the full-stick speed in degrees per second.
Valid values are 30 through 360. The default is 120.

Behavior:

- Snap mode retains the prior 45-degree latched turn and release threshold.
- Smooth mode uses the analog right-stick X magnitude outside a 0.25 deadzone.
- Right-stick right turns right and left turns left.
- Right-stick up/down remains jump/stand and crouch.
- Vertically dominant gestures never turn in either mode.
- Turning is disabled while the game UI owns input.
- A frame hitch contributes at most 50 ms of smooth rotation, preventing a
  large one-frame camera jump.

The setting is read once per game process. Fully close and restart COD4 after
changing either turn setting.

Expected console markers:

  [VR][CONTROLS] V50 turn mode: 45-degree snap.

or:

  [VR][CONTROLS] V50 turn mode: smooth analog at 120 degrees/second; right-stick deadzone 0.25.
  [VR][CONTROLS] Applied configured right-thumbstick turning.

Current backend limitation:

V50 consumes the shared VR right-stick state, so future backends can use the
same turn implementation. The V49 x86 OpenVR fallback still intentionally has
motion-controller input disabled; therefore turning remains keyboard/mouse or
gamepad-only on that fallback until OpenVR controller support is implemented.
The existing OpenXR controller path supports both V50 turn modes.

V50 retains:

- OpenXR first with the V49 R2 x86 OpenVR/SteamVR fallback.
- V48 crash diagnostics and the corrected R2 crash-report collector.
- V47 active-mission quit-confirmation rendering.
- V44 right-stick up/down jump/crouch and click melee controls.

Test smooth mode:

1. Close COD4.
2. Set KISAK_VR_TURN_MODE=smooth in VR-Settings.bat.
3. Leave KISAK_VR_SMOOTH_TURN_SPEED=120 for the first run.
4. Start the normal OpenXR headset path and use Launch-KisakCOD-VR.bat.
5. Enter a mission and slowly move the right stick left/right.
6. Verify partial deflection turns more slowly than full deflection.
7. Verify holding the stick produces continuous rotation with no repeated
   45-degree jumps.
8. Verify right-stick up/down still jumps/stands and crouches without turning.
9. Switch KISAK_VR_TURN_MODE back to snap, restart, and confirm one 45-degree
   turn per stick flick.

Do not distribute the matching private-symbol bundle publicly.
