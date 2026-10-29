# Check that a mod works

Use several small tests. **Problems**, a local package check, repository approval and ordinary gameplay each answer different questions.

## Check the files and art

- Save edits and wait for compilation. Resolve the studio's Problems links.
- Preview every direction and relevant frame. Look at shadows, anchors and layers.
- Check idle, used, empty, full, worn, broken and off states where the item has them.
- Run the species check for missing animation paths, layer counts and unsupported behavior steps. Compare warnings with the stock base to distinguish inherited gaps.

## Check an animal's home

- Give it suitable terrain, water, space, group size and preferred objects.
- Confirm the correct staff can reach it and supply its food.
- Observe normal eating, resting and movement over time, not one good screenshot.
- For swimming zoo animals, observe both land-to-water and water-to-land movement.
- For marine animals, observe surface and submerged movement, depth and water conditions; reject land and shallow placement where appropriate.
- For dinosaurs, observe egg placement, hatch and shell states, feeding and containment after hatching.

## Check objects with real users

Let guests buy from a building or use furniture, and let staff perform their inherited work. Check footprint, orientation, path access, price and service effects. For filters, compare water purity over a timed interval and test off, worn, broken and maintenance states. For fences and tank walls, check corners, heights, slopes and gates rather than only a straight section.

## Check ordinary Play

Find the mod in its expected buy category. Confirm its name, image, price and allowed placement. Start from a newly loaded session after changing mods. Reopen a saved map and test again. If the mod replaces an existing item, compare with the mod off to confirm the replacement is scoped correctly.

## Check saved zoos in both games

In ordinary Play, **Save a zoo...** defaults to **Shared zoo (.zoo)**. This
stores a compatible original-game world and the engine's own snapshot in one
file. **Engine-only save (.zt1save)** remains available. Enter a new filename
without an extension to use the selected format's extension.

Open the shared file in the engine and check custom animals, objects, shows and
paths with the required mods enabled. Then open a separate copy in the original.
Unique engine mod items stay in the engine snapshot; the original sees the
compatible stock world. Custom paths and boundaries may use stock equivalents,
and elevated engine paths are absent there. The receiving original needs the
appropriate installed expansions and assets.

**Saving again in the original discards the engine snapshot.** Keep the engine
copy if you want to retain its custom features. The original resave can still be
imported by the engine, with the state that the original understands.

Current native saves use version 14; older released engines cannot read them.
Keep pre-update backups. Original thoughts about named units and habitats are
mapped, and view conversion covers the original's rotations and zooms. Exact
task/visit-history continuation and scenario parity still have limits; a
successful load is not proof that every in-progress action resumed unchanged.

## Check repository sharing separately

**Publish → Check it** validates the local package. **Upload it** sends it for server rules and malware checks. Confirm the listing is live, inspect its cover in the game's repository before download, then download into a clean test setup and enable it. A theme pack should work with its separate components off.

## Keep evidence and limits

Record what you changed, the steps, elapsed game time, expected result and observed result. Retain failed attempts alongside the correction. A prototype that shares poses or inherits work states should say so. The tutorial examples demonstrate workflows, not complete animation or long-term balance certification.

The current Amberhorn example escaped an earlier low-glass enclosure after hatching; containment still needs a dedicated test. Marine show and animation diagnostics can also expose inherited engine gaps. Use those findings to guide testing rather than treating repository approval as proof of finished gameplay.
