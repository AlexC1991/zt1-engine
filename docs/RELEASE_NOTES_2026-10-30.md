Everything since v2026.10.20.

Optional interface additions have switches in Game Options. This is still an alpha, and original-game parity remains in progress. Back up saved zoos before updating.

# What's new

## Marine shows

- **Build a working show:** connect home tanks to a show tank, add show objects and grandstands, choose the animals' tricks and set a schedule or continuous performances.
- **Automatic staffing:** shows recruit an available marine specialist. Eligible adults travel through the tank gate, perform their program and return home afterwards. Several home tanks can supply one show tank.
- **Show programs:** tricks check research, required objects and water depth. Orcas, dolphins, great white sharks, jellyfish, sea lions and sea otters have working scheduled programs.
- **Spectators and admission:** guests choose shows and occupy separate grandstand seats. Each stand links to the show in its facing direction; changing the show's admission updates its linked stands.
- **Show controls:** edit the program, frequency and admission. The original Never frequency is available and survives saving.

## Shared saves and original Zoo Tycoon compatibility

- **One shared save:** Save Game now defaults to **Shared zoo (.zoo)**. The same file contains a compatible world for the original game and the full ZT1-Engine state. **Engine-only save (.zt1save)** is also available.
- **Original saves:** import substantially more animal, guest, staff, exhibit, research, finance and manager state, including identities, appearance, inventory, needs, duties, assignments, awards and marketing where mapped.
- **Show programs:** import original tricks, species, frequency and admission. Compatible engine show programs also export to the original game.
- **Engine extras stay with the save:** custom animals, buildings, staff, scenery, filters, food, elevated paths and other engine content remain in its engine section. The original loads the compatible stock world, with stock counterparts for supported custom paths and boundaries.
- **Native format 14:** retains original records, recent exhibit visits and favourite species by file. Existing older native save formats still have read paths; older released engines cannot read new format-14 snapshots.
- **Safer saving:** shared files validate both sections, use atomic replacement when written and avoid nesting old snapshots on repeated saves.
- **Important:** saving the file again in the original game removes its engine-only section. Keep that resave separate. Required engine mods still need to be installed and enabled. Renaming an old native file to .zoo does not convert it.

## Animal behaviour and habitats

- **Awareness of other animals:** movement accounts for nearby animals. Large marine animals account for body clearance while turning near tank walls.
- **Marine preferences:** species evaluate their own plant, rock, depth, cleanliness and salinity requirements. Mixed land/water habitats support animals such as walruses.
- **Dinosaur care:** egg hatching, broken eggs, feeding, rampaging and DRT capture have expanded behaviour handling. The helicopter team handles dinosaur capture.
- **Behaviour sequences:** special animations, final-frame holds, facing targets and prey capture have explicit handlers.

## Mods and the ZT1 Lab

- **A mod studio:** create and edit animals, buildings, fences, gates, staff, tank filters, plants, rocks, shelters, enrichment, paths, ground, food, dinosaur eggs and custom maps. Inspect each item's properties in plain controls.
- **Create or customise:** start with a new item or change an existing one. Draw from a copy, use an outline as a stencil or start blank. Own copy separates linked artwork for independent editing.
- **External artwork:** import PNG pictures, frame strips and directional sprite sheets. Render OBJ meshes into game directions; several supplied poses can make an animation sequence. Include material and texture files for model colours. A static model is not automatically rigged or animated.
- **Animal needs and pictures:** choose Zoo, Marine or Dinosaur behaviour, edit food and habitat needs, and create buy-panel, list and plaque pictures. Dinosaur egg, hatch and broken-shell art can be edited separately.
- **Animation tools:** layers, frames, onion skin, direction previews, recolouring, patterns and PNG exchange. Buildings can have separate idle and in-use artwork, including their static backgrounds and colours.
- **Custom maps:** start with a stock map or a blank map at a standard or custom size. Set the boundary, entrance, terrain, water, hills, paths and scenery, then save it into a mod.
- **Theme packs:** combine several compatible mods into one upload with a custom name, image and description. Conflicting files are reported before creating the pack; keep the original mods for later editing.
- **Download pictures:** repository listings show cover images before installation. Upload prepares a thumbnail from the mod's About picture, and small downloads show KB rather than rounding down to zero.
- **Mod switches:** downloaded and imported mods start disabled. Main-menu Enable and in-game Settings use the same selection. Optional replacements switch during play; content required by the current map or placed custom items stays enabled with an explanation.
- **Separate Lab tests:** Test Zoo and Play with it use temporary mod profiles, keeping experiments separate from normal game selections.
- **Help beside Mods:** 21 searchable guides, illustrated tutorials and nine videos cover the Lab, imports, animals, objects, maps, theme packs and publishing. Help works offline; press **F1** to open it.
- **Tutorial packs:** Wildlife Import Tutorial, Park Workshop Tutorial and Moonstone Habitat Kit are available separately or together as Moonstone Zoo Theme. These are examples and still have prototype limitations.

## Interface

- **Windows side by side:** compatible information windows and lists can stay open together. New windows find a clear position beside existing ones. *Switch: `smartstack`.*
- **Drag and lock windows:** move windows by their titles, including Game Options, and keep their positions between sessions. The gold lock controls accidental dragging. *Switch: `dragwindows`.*
- **Reset window positions:** Game Options > Controls > Window Positions > Reset restores their positions and keeps their lock settings.
- **Optional list closing:** an information window can close its source list. *Switch: `infocloseslists`, off by default.*
- **Cleaner mod pages:** left/right arrow buttons and Page 1 of N keep mod names, required-content explanations and buttons apart.

# Bug fixes

## Marine animals, shows and tanks

- Marine specialists stand on their platforms, and spectators sit at the correct grandstand height. Ladder animations face the right direction when climbing up or down.
- Dolphins remain visible through airborne flips. Waving, water-spitting and other show-only animations no longer play randomly in home tanks.
- Both eligible adult dolphins can participate. Interrupted performances preserve the animals' home tanks and return them through the correct gate.
- Stranded performers recover when there is one unambiguous linked home. An unavailable route leaves a transport capsule for manual relocation.
- Home tanks receive ordinary food. Show tanks remove old food piles and refuse ordinary feeding; show rewards still work.
- Platforms move to a clear bank when another tank or building blocks access. The fence gate tool can relocate them manually. Moving an active platform resets its show safely.
- Show objects are recognised by the schedule. Placement previews for toys, animals, rocks and plants use the appropriate tank floor or water surface.
- Marine plants require a filled tank. Demolition targets visible artwork consistently with its highlight, making tank objects and walls easier to select.
- Large swimmers clip correctly against tank walls in all four camera views, including corners, platforms and filters. Gates animate and sort against animals correctly.
- Tank filters animate and make the appropriate running, worn or broken sounds. Empty tanks switch them off; maintenance servicing preserves the wear countdown.
- Imported tank floors retain their terrain. Demolishing an imported tank restores surrounding ground when the original save has no earlier ground snapshot.

## Saving and loading

- Loading validates the incoming zoo before replacing the open session. Missing custom maps and definitions are named, and a rejected load keeps the current zoo.
- First-load research state waits for background catalogue and research loading before taking a snapshot. Load-stage timings are recorded for diagnosing slow or interrupted loads.
- Converted saves no longer depend on the original save remaining at its old path. Older generic-food imports receive the appropriate migration.
- Guests retain their names, appearance, positions and needs. Young animals, fence condition, working tank filters, service countdowns and mapped financial history survive conversion and reload.
- Original species IDs remain correct when mods replace stock animals. Unsupported custom tricks stay in the engine section; custom tank walls use a stock equivalent in the original section.
- Stock entrance pieces survive export, and old custom-map objects without IDs receive valid original records.
- Submerged generic dung stays in the engine section so the original game can fill its tanks. Dry-ground and raised-bank dung still export.
- Camera rotation, scroll and original zoom levels map across all four views. A zoo reopens at its saved view.
- Loading another zoo and returning to the main menu have corrected lifecycle handling. Looping zoo and building sounds stop when switching zoos.

## Buildings, guests and interface

- DRT buildings, signs and animated building parts stay aligned when rotating the map. Shops and attractions use their configured idle and in-use animations.
- Guests enter through a building's door, using its rotated footprint, rather than through any side touching a path.
- Staff added by mod registries can be hired and use their inherited duties. Custom buildings and workers display their own names.
- Individual filter types retain their own artwork, footprint, upkeep, wear, cleaning and servicing settings.
- Game Options pauses the zoo. Closing it resumes a running zoo and leaves a manually paused zoo paused.
- Tundra (Small) recognises the full entrance boundary, allowing construction inside the zoo.
- Rotating a held object no longer makes it disappear until the mouse moves.
- Freeform starting cash and its difficulty label stay in sync. The date remains visible while paused.
- Information windows drag correctly; Staff Information opens on Status, staff-list pictures have the proper size, and button help also appears after clicking.
- Guests leave tours when they need a building or must leave; tour guides clear followers when finished or fired.
- Dinosaur eating poses consume food. Custom entrances supply guest entry points, and custom bins participate in rubbish collection.
- Terrain traversal and off-screen sprite submissions are reduced. Tank geometry is reused within each frame, and show readiness and crowd decisions avoid repeated per-frame work.

## Lab and downloads

- The portable Game Build includes the mod-download client and creates an install-owned key on first use, preserving existing keys.
- Embedded Play follows minimise, restore and resize operations. Closing a native game dialog or stopping Play no longer leaves the Lab's pointer controls disabled.
- Lab startup and command replies handle split output and UTF-8 text. Map loads wait for their own acknowledgements, and an explicit load cancels a delayed startup rebuild.
- Recorded Test Zoo setups survive rebuilding. Blank maps can be reopened and saved again, ground drawings restore correctly and imported packs select their installed folder.
- Custom building animation paths work in placement, saved maps and service states.
- Release packaging carries the required process-output helper. Source mirroring preserves protected build tooling and stops failed saves before attaching downloads to the wrong version.

# Known issues

- **Overcrowded exhibits and tanks:** animals may repeat animations or twitch while trying to change direction. Increase the space or reduce the number of animals.
- Animals sharing a food pile can overlap while eating.
- Crowded marine views can still dip in frame rate or produce occasional long frames while panning.
- An intermittent first-load white-window freeze and closure was reported and then worked on retry. The startup ordering fix is included, but the reported closure has not been conclusively traced.
- Original save compatibility is substantial but incomplete. Exact AI/task continuation, some guest and manager fields, active performances and spectators are not fully preserved. Original runtime checks cover Marine Mania/Dinosaur Digs 12.04.00.0009; complete compatibility with every original version is not claimed.
- The original game can report marine specialist access trouble in some tank layouts. Wider marine, dinosaur and custom-mod compatibility remains in progress.
- Full scenario goals and events remain unfinished.
- Some buildings only have static idle art and animate while in use. The original Smilodon `fplaywithsoud` command and prototype Amberhorn containment remain known limitations.
- A new shared or engine-only save uses native format 14. Keep older backups if you may return to an older engine. An original-game resave removes engine-only content from that copy.
