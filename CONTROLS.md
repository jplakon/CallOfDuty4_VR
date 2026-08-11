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
| Move | Off-hand primary axis; movement follows the configured head/body/hand direction |
| Turn | Weapon-hand primary axis; configurable snap or smooth turning |
| Fire | Weapon-hand trigger |
| Aim / ADS | Physical two-hand shouldering; optional button override is unbound |
| Jump | Right primary axis up; left trigger is the alternate default |
| Use / interact | Off-hand primary action |
| Reload / eject magazine | Weapon-hand primary action |
| Sprint | Off-hand thumbstick click |
| Melee | Weapon-hand thumbstick click |
| Stance | Weapon-hand secondary action; tap changes stance and hold toggles prone |
| Lower stance | Weapon-hand primary axis down; lowers one step |
| Next weapon | Off-hand secondary action |
| Native off-hand action | Unbound; physical grenades use the off-hand grip |
| Support hand / physical interaction | Off-hand squeeze |
| Pause | Off-hand menu |
| Menu confirm / back | Weapon-hand primary / weapon-hand secondary |
| Menu cursor | Off-hand primary axis |
| Mission shortcuts | Weapon-hand thumbrest touch + off-hand primary-axis direction |
| Mounted-scope zoom | Off-hand primary axis |

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

## Per-weapon and gunstock calibration

Open **Weapons & Hands** and choose **Open calibration editor**. Keep the six
global weapon-hand values as the baseline that works for most weapons. The mod
then resolves the rendered pose in this order:

1. global Forward/Left/Up and Pitch/Yaw/Roll baseline;
2. the equipped weapon's hip-fire delta;
3. the active physical-gunstock delta, blended while shouldering/ADS;
4. the equipped weapon's shouldered/ADS delta, blended over the same interval.

Use **Use equipped weapon** after entering a mission. This records COD4's
stable internal weapon id rather than relying on a display name. Adjust the
hip-fire layer first, then select the shouldered/ADS or gunstock layer and use
**Apply live**. Moving the support hand onto or off the foregrip and entering
or leaving ADS should transition without a pose snap.

For a physical stock, create or select a gunstock and choose **Guided aim
capture** from its layer. Shoulder it normally and aim at a fixed point ahead
during the five-second countdown. The capture solves rotation only; use
Forward/Left/Up for the final sight and cheek-weld position. The capture is
explicit and uses the current absolute controller orientation—it does not
derive a hidden calibration from the pose held at game startup.

Use **Export gunstock** to share one guarded `.vrstock` profile and **Import
gunstock** to add it on another installation. Weapon overrides stay local
because weapon fit can depend on the player's hands and preferred stance.

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

The **Dominant hand** setting assigns semantic weapon-hand and off-hand roles
to the physical controllers. Changing it in the configurator also mirrors all
controller bindings once. Switching back restores the original sides, including
custom multi-input chords. The default is right-handed.

The tracked-hands build keeps COD4's authored combined hand mesh and uses
two-bone shoulder/elbow IK to place the off-hand wrist at its controller pose.
Set `KISAK_VR_TRACKED_HANDS=0` to restore the original weapon animation.
Continuous touch-driven finger curling is not implemented, and the authored
glove/arm geometry is not anatomically mirrored in left-handed mode.

- Supported detachable-magazine weapons can eject with the Reload action or by
  gripping the loaded magazine and pulling it clear of the well. Draw a fresh
  magazine from the off-hand or fixed hip, then either release it or touch it
  to the well according to the selected insertion mode.
- The support grip can require a held squeeze, toggle on each squeeze, or engage
  by proximity. Object grabs can use hold or toggle behavior independently.
- With the off hand free, grip inside either hip zone to draw a grenade. The
  handed layout places frag on the off-hand side and tactical on the weapon-hand
  side; the fixed layout always keeps frag left and tactical right. Hold/cook,
  physically swing, and release to throw.
- Physical melee recognizes a sufficiently fast forward weapon-hand thrust. A
  sideways swing alone is rejected. It can replace or supplement the button.
- A stationary grenade release deliberately drops it. Release position is
  limited to arm's reach and traced against nearby geometry.
- The virtual belt follows headset yaw only. Manual magazine reload takes
  interaction priority over a held grenade, a new hip grab, and the two-hand
  rifle grip.
- Set `KISAK_VR_MANUAL_GRENADES=0` or
  `KISAK_VR_MANUAL_MAGAZINE_RELOAD=0` to restore the corresponding native
  interaction.

Javelin, Stinger, mounted, and vehicle weapons continue to aim from the tracked
weapon controller. **Death From Above** remains unsupported; follow the skip
procedure in `INSTALL.md`.
