# Building and DRT polish — 27 October 2026

The DRT base now uses a fixed world footprint, grid-aligned placement and camera-aware artwork. The helicopter has its own position while flying, so the base, collision footprint and saved location stay put. Its roof and aircraft sort correctly against nearby scenery. Picking up the base cancels its flight and validates the entire footprint when it is placed again.

The helicopter plays the original spin-up, ascent, flight, weapon, return, descent and reversed spin-down sequences. Rotor audio stops on landing, removal or relocation. Removed targets cancel safely.

Capture roles follow the original executable's eligibility check at `0x50cd14`: keepers catch zoo animals; the DRT tranquilizes escaped Dinosaur Digs animals; scientists contain those animals after tranquilization. The original helicopter status function at `0x48c9ef` supplies takeoff, landing, return and waiting-at-base status text. Its behavior sequences and pad location come from `staff/helicopt.ai` and `scenery/building/helibase.ai`.

Placed scenery now advances its original idle and used animation frames using simulation time, including every render strip, background layer and underwater pass. Customer-use sequences start when a building becomes occupied. Pausing or advancing by zero freezes these frames. This restores the vendors, smoke and other effects already present in the game's building assets. The restaurant's idle art intentionally has one frame; its occupied art has eleven frames and includes chimney smoke.

Saved-game format 11 retains tranquilized state. Versions 1–10 remain readable. Flight assignments restart at the preserved base after loading.

## Lab verification

Use the Lab's Test Zoo, load **Basic Grass (ff01)** with **Flat** enabled, then run `polishcheck` in its console. It replaces the test fixture and refuses to run in an open player zoo. `polishdemo` sets up a live escaped-adult fixture, keeper, scientist, DRT, nine stores and hungry/thirsty guests; advance time to watch it.

The integration checks pass **44/44**, covering four camera rotations, fixed and nonoverlapping footprints, all flight phases, safe cancellation, scientist containment, ordinary keeper capture, and idle/use/pause animation behavior across nine stock and fancy stores.

The catalogue audit found **116 drawable building definitions**, including **48 with animated idle art** and **64 with animated used art**. Live simulation also confirmed customer visits and the full DRT-to-scientist hand-off.

Normal Play loaded the preserved version-10 session, saved it as version 11, then successfully loaded that version-11 copy. Play is left paused on the updated build. SHA-256 checks confirmed the original save, normal mod switches and settings file were unchanged.

The native Lab report is saved at `E:/zt1-shots/building-polish-2026-10-27/checks.txt`. Player-session backup and save/load evidence are kept in the same directory; the original Something.zt1save is not overwritten. Normal mod switches remain off. No release is created by this work.

## Follow-up: remaining building, gate and ladder animations

The full building audit exposed an idle-clock bug when idle and used shared the same cached animation. Unoccupied buildings such as the aviary and several Marine Mania attractions could therefore remain on frame zero. The clock now follows the active state. Buildings imported from original maps also load their used/background layers and saved colours.

Geysers, volcanoes and the steam vent now run their configured random-use cycles, including their use animation count and sound. The occupied DRT base plays its original nine-frame beacon animation on a separate clock. Buildings whose original idle artwork is static still wait for the appropriate use state.

Land gates now use their own animation frame count and timing instead of fixed 12-frame/20-fps limits. Sloped art follows the same opening progress, open and closed endpoints are held, and zero elapsed time does not advance gates or trigger portal audio. The marine show portal was checked separately.

Descending specialists now face the ladder while moving backwards down it. Their existing reverse playback follows the original `trainer.ai` behaviour, `bClimbDown = fFlyReverse(climb, 33)`; ascent plays forwards.

In the Lab Test Zoo, load flat **Basic Grass (ff01)** and run `activitycheck`. It replaces the test fixture and is refused in normal Play. **51/51 checks passed**, covering all 116 drawable building definitions, five timed scenery types, 26 land-gate definitions, the marine show portal, DRT beacons and complete male/female specialist visits on every tank side. The previous `polishcheck` suite also passed **44/44** on this build. The detailed catalogue and results are in [ACTIVITY_POLISH_AUDIT.txt](ACTIVITY_POLISH_AUDIT.txt).

`activitydemo` creates a visible tank, specialist, DRT, carousel, restaurant, aviary, stands and geysers, with paths and guests. Live testing confirmed restaurant visits and chimney smoke while the specialist fed and cleaned underwater. The original player save, mod switches and settings retained their SHA-256 hashes.
