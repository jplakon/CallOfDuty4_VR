# Controls

This controller map applies to Meta Quest Touch controllers. Other OpenXR
controllers may use different runtime bindings.

Right-stick vertical gestures move one native stance step and must return near
neutral before they can trigger again. Vertically dominant input prevents a
stance gesture from also turning. Horizontal turning can use the default
45-degree snap mode or optional analog smooth mode.

The tracked-hands build keeps COD4's original combined hand mesh
and uses two-bone shoulder/elbow IK to place the left wrist at the left OpenXR
grip pose without stretching the stock arm. The right hand follows the rifle,
which is already rigidly attached to the right controller. Set
`KISAK_VR_TRACKED_HANDS=0` in `VR-Settings.bat` to restore the original weapon
animation. Touch-driven finger curling is not implemented yet.

| Action | Controller binding |
|---|---|
| Move | Left thumbstick; movement follows HMD direction |
| Turn | Right thumbstick left or right; 45-degree snap by default or configurable smooth turn |
| Fire | Right index trigger |
| Aim / scope | Hold the left grip and physically shoulder the weapon near the headset sight line |
| Raise stance / jump | Flick right thumbstick up: prone -> crouch -> stand -> jump; left index trigger also remains available |
| Manual magazine reload | Right A to eject or reload; squeeze the left grip at the left hip to draw; release at the magazine well to insert |
| Use / interact | Left X button |
| Sprint | Click left thumbstick |
| Lower stance | Flick right thumbstick down: stand -> crouch -> prone; tapping right B also remains available |
| Prone | Flick right thumbstick down while crouched, or hold right B button |
| Melee | Click right thumbstick |
| Frag grenade | With the left grip released, reach to the left hip and squeeze; hold/cook, physically swing, then release to throw |
| Flashbang / smoke grenade | With the left grip released, reach to the right hip and squeeze; the mission-equipped tactical type is selected |
| Next weapon | Press left Y |
| Pause / menu | Left controller menu button |

## Right-thumbrest mission controls

Touch and hold the right controller's thumbrest sensor while moving the left
thumbstick. Normal locomotion is suspended while this modifier is active.

| Direction | Action |
|---|---|
| Left stick up | Rifle grenade launcher / weapon slot 5 |
| Left stick down | Night vision |
| Left stick left | Airstrike / mission action slot 6 |
| Left stick right | C4 / mission action slot 7 |

## Manual grenade interaction

- The virtual belt follows headset yaw only, so looking down does not rotate
  the hip zones. A grenade requires a new left-grip press inside a zone.
- Interaction ownership is manual magazine reload first, an already-held
  grenade second, a new hip grab third, and the two-handed rifle grip last.
- A stationary release deliberately drops the grenade. A throwing motion uses
  recent controller velocity and the grenade asset's native speed. Release
  position is limited to arm's reach and traced against nearby geometry.
- Set `KISAK_VR_MANUAL_GRENADES=0` in `VR-Settings.bat` to restore beta.5's
  right-grip tactical grenade and left-Y hold-frag/tap-cycle controls.

## Mission-specific weapons

- Supported detachable-magazine rifles, SMGs, and pistols use physical
  reloading. Press right A to eject the magazine, move the left controller to
  your left hip and squeeze the left grip to draw a fresh magazine, keep the
  grip held while moving it to the weapon, then release inside the magazine
  well. Shotguns, launchers, bolt-action rifles, belt-fed weapons, and weapons
  without a usable clip model retain right-A native reload.

- For the Javelin and Stinger, aim with the right controller, hold the left
  grip, bring the weapon into the eye-level aiming pose, wait for target lock,
  and fire with the right trigger.
- Mounted and vehicle weapons aim with the right controller and fire with the
  right trigger.
- **Death From Above** is unsupported in this beta. Follow the skip procedure
  in `INSTALL.md`.
