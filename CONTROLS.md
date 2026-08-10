# Controls

Controller Input V4 uses controller-neutral bindings instead of assuming Meta
Quest button names. The same saved profile works through OpenXR and the
SteamVR/OpenVR compatibility backend. Every action has a primary and optional
alternate slot, and either may use one input or an AND-chord of up to four
compatible inputs across both controllers.

## Default layout

The default keeps the beta.7 Quest Touch layout where the active controller
has the corresponding component. “Primary” and “secondary” mean that
controller profile's first and second action buttons; “primary axis” means its
main thumbstick, or the trackpad on a Vive wand.

| Action | Default binding |
|---|---|
| Move | Left primary axis; movement follows the configured head/body/hand direction |
| Turn | Right primary axis; configurable snap or smooth turning |
| Fire | Right trigger |
| Aim / ADS | Physical two-hand shouldering; optional button override is unbound |
| Jump | Right primary axis up; left trigger is the alternate default |
| Use / interact | Left primary action |
| Reload / eject magazine | Right primary action |
| Sprint | Left thumbstick click |
| Melee | Right thumbstick click |
| Stance | Right secondary action; tap changes stance and hold toggles prone |
| Lower stance | Right primary axis down; lowers one step |
| Next weapon | Left secondary action |
| Native off-hand action | Unbound; physical grenades use the left grip |
| Support hand / physical interaction | Left squeeze |
| Pause | Left menu |
| Menu confirm / back | Right primary / right secondary |
| Menu cursor | Left primary axis |
| Grenade launcher / slot 5 | Right thumbrest touch + left primary axis up |
| Night vision | Right thumbrest touch + left primary axis down |
| Airstrike / slot 6 | Right thumbrest touch + left primary axis left |
| C4 / slot 7 | Right thumbrest touch + left primary axis right |
| Mounted-scope zoom | Left primary axis |

The upward gesture is shown directly as the primary binding for **Jump**. It
jumps while standing and uses COD4's native `+gostand` behavior to
raise from crouch or prone. The left trigger is the alternate binding for that
same action. **Lower stance** remains right primary axis down. The Turn
action consumes horizontally dominant motion, so these default vertical
gestures do not also turn.

## Remapping in the configurator

Open the **Controls** page in `KisakCOD-VR-Configurator.exe`, select an action,
and set its primary and alternate slots. Boolean actions can use buttons,
triggers, squeezes, clicks, supported touch sensors, and the up/down/left/right
direction of either primary stick or trackpad. Axis actions can use either
hand's primary axis, explicit thumbstick, or explicit trackpad. A boolean
input cannot be assigned to an axis action, or vice versa.

Choose **Chord...** to select two through four inputs that must be held at the
same time. For example, the tested night-vision default is **Right thumbrest
touch + Left primary stick / trackpad down**. The primary and alternate slots
are OR alternatives; inputs inside either slot are an AND-chord. Selecting a
single dropdown item replaces that slot with a normal one-input binding.

The configurator warns when two gameplay actions share an input, but allows the
choice because intentional overlaps are useful. Menu and context-specific axis
bindings may overlap gameplay controls without a warning.

The four mission shortcuts—grenade launcher, night vision, airstrike, and C4—
are ordinary visible chord bindings. They can be replaced with single inputs
on controllers that do not expose a thumbrest touch sensor. The default layout
remains:

| Direction | Action |
|---|---|
| Up | Grenade launcher / weapon slot 5 |
| Down | Night vision |
| Left | Airstrike / mission slot 6 |
| Right | C4 / mission slot 7 |

### Press-to-bind capture

Use **Bind...** beside the primary or alternate dropdown to capture a physical
control:

1. Fully close COD4 and start the runtime selected under **Runtime backend**.
2. Release both controllers, choose **Bind...**, and accept the prompt.
3. Put on the headset. The mapper briefly owns a black VR session.
4. Press the desired control, or move the desired stick/trackpad in the exact
   up/down/left/right direction to bind.
5. Save the profile and restart the game.

Press Escape to cancel. Capture times out after 45 seconds. With `auto`, the
mapper uses OpenXR when it can create an OpenXR session and otherwise tries the
SteamVR/OpenVR compatibility path.

## Controller profiles

The OpenXR backend suggests bindings for these interaction-profile families:

- Khronos Generic and Simple controllers
- Oculus/Meta Touch, Touch Plus, and Touch Pro
- PICO 4 and PICO Neo3
- Valve Index
- HTC Vive wand, Vive Cosmos, and Vive Focus 3
- Microsoft Mixed Reality, HP Mixed Reality, and Samsung Odyssey

Not every controller exposes every component. An explicit thumbstick binding,
for example, is inactive on a Vive wand; use its primary axis or trackpad
instead. Press-to-bind is the safest way to learn what the active runtime and
driver expose.

SteamVR/OpenVR uses the same saved action model through legacy controller-state
discovery. Some drivers alias face, menu, grip, and touch components, so inspect
the configurator's conflict warnings and test the result in-game.

## Physical interactions

The tracked-hands build keeps COD4's combined hand mesh and uses two-bone
shoulder/elbow IK to place the left wrist at the controller pose. The right hand
follows the rifle. Set `KISAK_VR_TRACKED_HANDS=0` to restore the original weapon
animation. Continuous touch-driven finger curling is not implemented.

- Supported detachable-magazine weapons use physical reloading. Activate the
  configured Reload action to eject, use the configured Support-hand action at
  the left hip to draw, then release at the magazine well to insert.
- With the support hand released, press its configured binding inside the left
  hip zone for a frag or the right hip zone for the equipped tactical grenade.
  Hold/cook, physically swing, and release to throw.
- A stationary grenade release deliberately drops it. Release position is
  limited to arm's reach and traced against nearby geometry.
- The virtual belt follows headset yaw only. Manual magazine reload takes
  interaction priority over a held grenade, a new hip grab, and the two-hand
  rifle grip.
- Set `KISAK_VR_MANUAL_GRENADES=0` or
  `KISAK_VR_MANUAL_MAGAZINE_RELOAD=0` to restore the corresponding native
  interaction.

Javelin, Stinger, mounted, and vehicle weapons continue to aim from the tracked
right controller. **Death From Above** remains unsupported; follow the skip
procedure in `INSTALL.md`.
