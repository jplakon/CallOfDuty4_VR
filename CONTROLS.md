# Controls

This controller map applies to Meta Quest Touch controllers. Other OpenXR
controllers may use different runtime bindings.

The experimental tracked-hands build hides COD4's original weapon-attached
hands and places separate stock glove surfaces on the left and right OpenXR
grip poses. Set `KISAK_VR_TRACKED_HANDS=0` in `VR-Settings.bat` to restore the
original attached hands. This first pass does not yet include forearm IK or
finger curling.

| Action | Controller binding |
|---|---|
| Move | Left thumbstick; movement follows HMD direction |
| Snap turn | Right thumbstick left or right |
| Fire | Right index trigger |
| Aim / scope | Hold the left grip and physically shoulder the weapon near the headset sight line |
| Jump | Right A button |
| Manual magazine reload | Left trigger to eject; squeeze at left hip to draw; release at magazine well to insert |
| Use / interact | Left X button |
| Sprint | Click left thumbstick |
| Crouch | Tap right B button |
| Prone | Hold right B button |
| Melee | Click right thumbstick |
| Frag grenade | Hold left Y for at least 0.3 seconds, then release to throw |
| Flashbang / tactical grenade | Right grip |
| Next weapon | Tap left Y |
| Pause / menu | Left controller menu button |
| Night vision | Right thumbstick down |
| Rifle grenade launcher | Right thumbstick up |

## Right-thumbrest mission controls

Touch and hold the right controller's thumbrest sensor while moving the left
thumbstick. Normal locomotion is suspended while this modifier is active.

| Direction | Action |
|---|---|
| Left stick up | Rifle grenade launcher / weapon slot 5 |
| Left stick down | Night vision |
| Left stick left | Airstrike / mission action slot 6 |
| Left stick right | C4 / mission action slot 7 |

## Mission-specific weapons

- Supported detachable-magazine rifles, SMGs, and pistols use physical
  reloading. Press the left trigger to eject the magazine, move the left
  controller to your left hip and squeeze to draw a fresh magazine, keep
  squeezing while moving it to the weapon, then release inside the magazine
  well. Shotguns, launchers, bolt-action rifles, belt-fed weapons, and weapons
  without a usable clip model retain left-trigger native reload.

- For the Javelin and Stinger, aim with the right controller, hold the left
  grip, bring the weapon into the eye-level aiming pose, wait for target lock,
  and fire with the right trigger.
- Mounted and vehicle weapons aim with the right controller and fire with the
  right trigger.
- **Death From Above** is unsupported in this beta. Follow the skip procedure
  in `INSTALL.md`.
