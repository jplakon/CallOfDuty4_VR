KisakCOD VR V51 - right-stick stance ladder

V51 makes each deliberate vertical right-stick flick move exactly one stance
step while preserving V50 snap/smooth horizontal turning.

Right-stick behavior:

- Flick down while standing: crouch.
- Return the stick to neutral, then flick down while crouched: prone.
- Further down flicks while prone do nothing.
- Flick up while prone: crouch.
- Return to neutral, then flick up while crouched: stand.
- Flick up while already standing: jump.
- Horizontal input continues to use the configured snap or smooth turn mode.
- A diagonal gesture is classified by axis dominance, so a deliberate vertical
  stance gesture does not also turn.

The stick must pass through the neutral release threshold between actions. This
prevents a held stick from rapidly cycling through multiple stances.

The right B button is unchanged: tap toggles crouch/stand and hold toggles
prone/stand. Left-trigger jump is also unchanged.

Expected console markers after the first vertical gesture:

  [VR][CONTROLS] V51 right-stick stance ladder: up raises one stance (or jumps from standing), down lowers one stance; horizontal turning preserved.
  [VR][STANCE] V51 right-stick ladder active: down stand/crouch/prone; up prone/crouch/stand/jump.

Test sequence:

1. Start a mission while standing.
2. Flick down and release to neutral: verify crouch.
3. Flick down and release again: verify prone.
4. Flick up and release: verify crouch.
5. Flick up and release again: verify standing without jumping.
6. Flick up once more while standing: verify one jump.
7. Verify right B still supports its existing tap/hold behavior.
8. Verify horizontal snap or smooth turning still works and diagonal vertical
   gestures do not cause unwanted turning.

V51 retains the V50 R2 smooth-turn option, V49 R2 OpenVR fallback, V48 crash
diagnostics, and the corrected Rank 2 R2 collector. OpenVR motion-controller
input remains deferred; this feature is currently testable through the OpenXR
controller path.

Do not distribute the matching private-symbol bundle publicly.
