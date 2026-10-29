# Candidate 2026.10.29: owner test

Use the newly rebuilt files in dist, identified by candidate-2026.10.29-inventory.json.
The earlier files with the same version predate shared saves. This candidate
writes native format 14; keep copies of pre-update saves for rollback.

## In Labs

1. Open copies of a stock land zoo, a marine/show zoo, a dinosaur zoo and a
   custom theme-pack zoo. Confirm counts, names, cash and the camera view.
2. Let two adult dolphins run a continuous show automatically. Check the
   specialist's platform/ladder, both performers, complete airborne tricks,
   home-tank return, home feeding and pump servicing. Rotate all four views.
3. Pan past busy stands and shops at close and wide zoom. Place animals,
   plants, rocks, shops, paths and show toys. Check their previews and committed
   positions, animations and correct tank demolition highlights.
4. Load a stock save with downloaded mods off. Then load the custom zoo with
   its required pack off: the error must name missing content and preserve the
   open zoo. Enable the required pack and load the same file successfully.
5. Save once as Shared zoo (.zoo) and once as Engine-only (.zt1save). Reload
   both; check actors, jobs, guest preferences and custom content. Cancel one
   Save and confirm no extra file was created.
6. Open a COPY of the shared file in the original MM/DD game. Check its stock
   world, gates, pumps, dolphin show program, money and view. Save to another
   name, then import that original resave in the engine. The original removes
   engine extras when it saves; keep the engine's shared file separately.

## Laptop test

The owner chose laptop testing and asked to avoid repeating a full installation
and updater matrix for unchanged installer/updater code. The previous public
release installed and updated successfully for the owner. Package hashes,
payloads, signatures and updater download/rejection checks have passed here;
the actual installer/relaunch sequence has not been rerun for this candidate.
That remaining check is waived rather than reported as a pass.

- Install the private candidate normally on the laptop and focus on the gameplay
  and Save/Load checks above. The supplied Marine-Show-Test.zoo needs stock MM/DD
  assets, with no custom mods. Use copies of personal saves.
- An installed Lab linked to an older checkout can run that checkout's engine.
  Use the bundled engine for this packaged test. Its SHA-256 is
  73011c28d0dfcbf7b9cbc866f058f49837a2d14f43a85ab844ac6ef8a638804c.
- The portable ZIP is optional: extract it and run start.bat with the laptop's
  own Zoo Tycoon files. Public update feeds continue to serve the last release.

## Known limits to assess

Crowded exhibits can twitch or repeat turns. Animals can share a food-pile
position while eating. Dense marine views can fall below 60 FPS; earlier
intermittent long frames remain unresolved even though the latest short pans
did not reproduce them. Scenario parity and exact original AI continuation
remain unfinished. Original runtime verification covers MM/DD 12.04.00.0009.

Record each failure with the save copy, view, steps and screenshot. Acceptance
is for the exact inventory hashes. Publication stays held until owner review
is complete. A specialist access warning in the original game's diagnostic
tank layout remains recorded for investigation; original show/task parity is
not being claimed complete.
