Alpha candidate 2026.10.29 — changes since v2026.10.20. Local testing; not published.

Optional interface changes have Game Options switches. Original-game parity is still in progress.

# Known issues

- **Overcrowded exhibits:** animals may repeat animations and/or twitch while trying to change direction. This also affects crowded tanks. Give animals more space or reduce the number sharing the exhibit.
- Original-save import remains partial; original-format export and complete scenario parity are unfinished. New native saves use format 12 and cannot be read by older released engines. Keep backup copies of older saves.

# Marine shows, habitats and animal behavior

- **Marine shows:** a show controller, editable trick scripts, scheduled starts, linked home/show tanks, specialist assignments, grandstand spectators and admission income. Tricks check research, depth and required objects. Animals travel through the portal and return home; draining a tank cancels its show safely. This is the first release candidate containing this workflow; species and trick coverage still needs wider testing.
- **Tank portals:** gates open and close on their animation clock and sort against animals instead of covering them indiscriminately.
- **Animal awareness:** movement accounts for nearby animals, and large marine animals use body clearance while turning near tank walls. These changes reduce overlaps; they do not guarantee every species and enclosure combination is free of clipping.
- **Marine habitat needs:** species-specific plant and rock preferences, water conditions and mixed land/water habitats are evaluated. Walrus routing between land and a sunken pool has a dedicated regression check.
- **Behavior completion:** special animation sequences, final-frame holds, facing a target and prey capture now have explicit handlers. Rampaging dinosaurs approach eligible buildings before damaging them. Healthy and unhealthy egg hatching paths are covered by checks.
- **Guests and tours:** guests leave a tour when they need a building or are caught/leaving; tour guides clear followers after finishing or being fired. Time-in-park thoughts use the original's thresholds.

# Mod switches and Lab reliability

- **Download first, enable separately:** downloaded/imported mods start off. Main-menu Enable and in-game Settings use the same selection. Optional replacements can switch during play while preserving the zoo. A mod providing the current custom map or placed custom content stays required until that content is no longer in use; the UI explains why.
- **Cleaner mod pages:** compact left/right arrow buttons and a Page 1 of N indicator keep the list, controls, Required explanation and help footer apart.
- **Temporary Lab tests:** Test Zoo and Play with it use private mod profiles. Ending a test restores normal selections; a newly installed Lab carries no developer test selections.
- **Reliable Lab replies:** startup readiness and mod replies are read as complete lines, including split UTF-8 text. Test Zoo map loads and console commands wait for their own request acknowledgements, avoiding stale counts and incomplete check reports. Explicit map loading cancels a delayed startup rebuild.
- **Embedded Play:** minimize, restore and resizing update the embedded game's placement. The native window check restores the user's window bounds and maximized state.
- **Release packaging:** the installed Lab includes its new process-output helper. The CodeRook mirror exports the committed Git tree and preserves protected remote tooling; the verified mirror fix passed GitHub Actions. Release uploads now stop if the CodeRook save fails, before attaching files to an older save.

# Marine show fixes and performance — 29 October

- Scheduled shows automatically recruit an available marine specialist. Both adults can perform and return home. Full scheduled programs passed for orca, dolphin, great white shark, jellyfish, sea lion and sea otter.
- Specialists stand on the platform, spectators sit in separate grandstand seats, and dolphin jumps remain visible above the water. Performance animations stay restricted to shows.
- Interrupted shows preserve each animal's home and return through its portal. Old stranded performers recover when there is one unambiguous linked home; an unavailable portal leaves a closed transport capsule for relocation.
- Home tanks receive ordinary food, including when floor plants surround the ladder. Show tanks remove old food piles and refuse ordinary feeding; trick rewards still work.
- Platforms move to an unobstructed bank when another tank or building blocks their approach. The fence gate tool can also relocate them manually. Moving an active platform cancels and resets its show safely.
- Each grandstand links to the show in its facing direction. Editing that show's admission updates its stands. Several home tanks can supply the same show.
- Marine plants require a filled tank. Placement previews use the tank floor or water surface, and demolition picks visible object artwork consistently with its highlight.
- Large swimmers retain exact wall clipping through all four camera views, including platforms and filters. Tank drawing reuses prepared geometry within a frame; show readiness and attendance decisions avoid repeated per-frame work.
- On the development PC, the same 102-guest zoo improved from 28 FPS to 42–53 FPS in short 1904x897 captures. A running 1,000-guest show crowd measured 34 FPS. These are observed results, not a universal frame-rate guarantee. The accelerated OpenGL renderer uses the GPU; simulation and draw preparation also use the CPU.
- Show frequency includes the original Never option and preserves it through saves.

# Original saves and native save format

- **More original state:** preserve original entity identities, animal lifetime/status/timers, guest inventory/favorite species/arrival and need timers, and staff appearance, duties and assignments. Research funding, enabled categories and progress, awards and marketing are restored where mapped.
- **Finance history:** import the original rolling monthly finance rows with the original panel's income/expense signs. Annual and lifetime records are retained in the native save; unavailable older monthly rows are not invented.
- **Native format 12:** keeps the additional state and finance records through a full file save/load. The new reader retains older native-format branches. Back up saves before upgrading: older released engines cannot open a newly saved version-12 file.
- Original `.zoo` import remains a partial state conversion. Saved show scripts/schedules, active performances and spectators, original behavior/task stacks, some guest state and original camera/ground details remain incomplete. The engine still cannot export a save for the original game. Selecting All files does not convert a native file into an original save.

# Candidate verification and remaining limits

- Lab reports: **1,131/1,131** original-import checks across four fixtures; **43/43** behavior checks; **54/54** activity checks; **44/44** building/DRT checks; **30/30** marine management and **27/27** show polish checks, with **18/18** nested show-start checks. **21/21** Lab process/profile/reply tests pass. The show programs cover all six stock performance species. The all-species scan covers 119 definitions; the original Smilodon typo `fplaywithsoud` remains unsupported and reported.
- Some buildings have only static idle artwork; movement appears in their configured use state. The catalogue audit does not promise animation for art that has no animated frames.
- Scenarios, complete original save parity/export and wider marine/dinosaur compatibility remain unfinished. Prototype tutorial mods are examples, including the existing Amberhorn containment limitation.
- Publication is held for the owner's packaged-candidate test. Installer, updater and soak results are recorded separately in the current release acceptance document.

# What's new

- **Windows side by side:** opening a window no longer closes the others. Zoo Status, the Animal, Guest, Exhibit and Staff lists, information windows and a buy panel can all be open at once; one that would cover another goes beside it instead (above, left, below or right, whichever is nearest and clear of the toolbar and bottom bar). Only the left-hand panels (Construction, Creatures, Structures, Staff, Research) still replace each other, as they share one spot. *Switch: `smartstack` (off: one window at a time, where the original puts it).*
- **Drag any window** by its title bar, Game Options too; it stays where you leave it, next time you play too. *Switch: `dragwindows` (off: only the windows the original lets you drag, and only until the game closes).*
- **Window locks:** a little gold lock beside each window's close button. Every window starts locked, so nothing gets dragged by accident; click the lock to open it and drag the window, click again to lock it where it is. Each window remembers its lock. Made from the game's own close button art.
- **Reset window positions:** Game Options › Controls › Window Positions › Reset puts every dragged window back (locks are kept).
- *Switch: `infocloseslists` (off by default):* an information window closes the open list instead of standing beside it.
- The main menu shows the version number in its corner, as the original does.
- **Freeform maps from mods:** maps made in the ZT1 Lab come in mods and appear in the freeform map list with their own picture and description. *Switch: `custommaps` (off: the game's own maps only).*
- New animals from mods show their own name everywhere, as old fan-made animals do.

# For modders (the ZT1 Lab)

- **Help in the Lab:** a new tab beside Mods brings together twenty searchable guides, illustrated tutorials and nine videos. Learn the workspaces, create and import animals and objects, build habitats and maps, test behavior, make theme packs and publish or retrieve mods. The library works offline, and the earlier overview is labelled for its older interface. Press **F1** to open Help.
- **Published tutorial mods:** Wildlife Import Tutorial, Park Workshop Tutorial and Moonstone Habitat Kit are available separately, or together as Moonstone Zoo Theme. Their actual game downloads were verified, including custom map loading, dinosaur egg adoption and marine tank placement. These are prototype examples; Amberhorn containment still needs work.
- **Pictures before download:** the game's Mod repository shows each mod's cover before you install it. Lab Upload prepares the thumbnail automatically from its About picture, and it appears once repository checks pass. The four tutorial listings have been refreshed with covers; older listings gain one when their creators upload an update. Small downloads show KB instead of rounding down to 0 MB. *Existing Mod Menu switch.*
- **Theme packs:** Publish can combine several compatible local mods into one named pack with its own image, description and upload. Conflicting files are reported before a pack is created. Keep the original mods to edit later; the pack is a snapshot.
- **More item builders:** create separate or customized plants, rocks, shelters, enrichment, zoo entrances, tank walls, paths, ground textures and animal food. Paths support all twenty connection shapes; ground textures keep the existing terrain's gameplay properties. **Own copy** separates linked art for independent editing.
- **Custom food:** an animal can select food from an enabled mod in Needs, and its usual carer supplies it. Dinosaur egg, hatch and broken-shell art can be edited separately. These use the `mods` switch; mod maps use `custommaps`.

- **Build an item:** create separate buildings, fences, staff and tank filters, or customize an existing item, with original imported art or Remake. Guest appearances can replace one of the four existing guest kinds. Staff inherit a working duty; this does not invent a new job or guest AI role. The Inspector includes filter cleaning, wear and upkeep settings.
- **Complete building remakes:** static background frames and default building colours are included with the animated parts. Idle and in-use animations can both be remade. The editor opens a direction that contains art when the source has only diagonal views.
- **Import external animal art:** bring in a PNG picture, frame strip or directional sprite sheet, fit it to game size and preview its frames before saving. OBJ meshes can be rendered into five directions; select several exported poses to make a sequence. Include the model's material file and texture pictures for its colours. This converts supplied art and poses; it does not animate or rig a static model.
- **Pictures from a frame:** make an animal's buy-panel icon, list picture and plaque from one of its drawn frames, then edit each picture if needed. For prototypes, **Link other poses** can reuse a drawn animation across selected behavior slots.
- **New species from this:** make a new animal from any of the game's, with its art made from your own copy of the game in a colour you choose, and its own name, needs and behaviour to change.
- **Change it in a mod:** copy any of the game's files into a mod to change it there (a building's capacity, an animal's price), and edit files inside a mod directly.
- **A mod studio:** the Lab's Mods tab is now one workspace for making a mod: everything in it listed by kind, the editor for what you pick, an Inspector with each animal's or building's values as plain controls (saved as you change them), and a list of what's still missing. Start a new animal, a new map or a change to a game item from **Make something**.
- **Remake, Stencil or Blank:** start a drawing from a copy of anything in the game (an animal, a person, a building, a tree, a fence), from its outline as a stencil with its inside as faint as you like (paint can be kept inside the outline), or from nothing. Then make it bigger or smaller, its body longer or shorter, its neck or legs taller, or recolour it, in every frame and direction together. A ring shows all eight directions of the frame you're drawing.
- **An animal's needs:** a new animal is a zoo animal, a marine animal or a dinosaur, and its Needs page sets everything the game weighs for it - who looks after it, its food, its home, the ground, plants, rocks and shelters it likes and the animals it gets on with - or copies them from another animal.
- **An animal's pictures:** draw its buy-panel icon, its list picture and its plaque in the same editor (from a copy of the current one, its outline, or blank); the game shows them in its panels.
- **Water and dinosaur values:** a marine animal's Inspector has its water (depth, staying under water, keeping off the tank walls, murky and salty water) and a dinosaur's has its egg and hatching, egg eating, eating guests and breaking fences.
- **The editor:** zoom with Ctrl + scroll or the zoom bar, tidy icon panels with tooltips, and a stencil browser that plays any animal, person, building, tree or fence in all its directions before you pick it.
- **New animals from scratch:** make an animal with a name and an animal it behaves like, then draw its animations in the Lab's animation editor (layers, frames, onion skin, a guide to draw over) or link each one to any animation that exists - it works in the game from the start, and becomes all your own one animation at a time.
- **The Art page:** design an animal's look. Recolour parts of it (pick colours by shade, darker or lighter), paint frames, fade a pattern along its body or lay stripes across it in every frame, or work in another paint program through PNGs.
- **Save as a map:** design a freeform map in the Test Zoo (ground, water, hills, paths, trees) and save it into a mod. The Terrain view shows the whole map from above.
- **Maps from a blank map:** start from flat grass at the game's Small, Medium or Large size, or any size of your own. Draw the zoo's boundary wall yourself, choose the entrance and put it where guests should come in; the wall opens for it. The game's map view shows the map as you made it.
- **A picture for your mod:** set one on the mod's Overview page (an image of your own, or the Test Zoo's view); the game's Mod Menu shows it.
- A **starter kit** mod with one of everything a mod can have.
- Mods can rename what they change: a building or animal changed in a mod can carry its own name in the buy lists.

# Bug fixes

- The portable Game Build now includes the mod-download client and creates its own repository key on first startup. Existing keys are preserved; no developer key is included.

- Original `.zoo` imports now restore guests with their names, appearance, positions and needs, preserve young animals and fence condition, and restore tank filters as working equipment with their saved health. Newly converted native saves no longer depend on the original file remaining at its old path. Full original save-state parity and original-format export remain unfinished.
- Opening the in-game Game Options menu pauses the zoo. Closing it resumes play only if it was running beforehand; a manually paused zoo stays paused.
- Dinosaur eating poses such as head-down eating now consume food. Custom entrances can supply the zoo's guest entry path, and custom bins can use their own art and participate in rubbish collection.
- The Lab preserves its recorded Test Zoo setup before restarting to save an enabled mod, can save a reopened blank map again, reopens saved ground drawings correctly, and selects imported packs by their installed folder.

- Custom building animation paths are used for placement, saved maps and service states. Buildings and hired staff use their mod's own names.
- Staff definitions in enabled mod registries can be hired and perform their inherited duties. Multiple tank filter definitions keep their individual art, footprints, cleaning, wear, upkeep, servicing and refunds. Saved zoos and map designs now retain each filter's definition path (save format 9; earlier saves load as the original filter).
- Loading a saved zoo from inside a game, or going back to the main menu, could crash the game. Fixed.
- Tundra (Small): nothing could be built anywhere, because the zoo's entrance has side pieces that weren't counted as part of the wall. Every part of an entrance closes the wall now, so the whole zoo is yours to build on.
- Buildings with signs or animated parts (restaurants, shops, the DRT base and gift shop, stage pieces and more) sat off their spot by a different amount in each camera turn, so they seemed to jump when you turned the view, and the restaurant's sign fell to the ground. They now stand where the original draws them in every turn.
- Turning a held object with Q or E before placing it no longer makes it vanish until the mouse moves.
- Tank filters now animate while they run, and hum (a different sound once worn down), as in the original. A filter beside an empty tank shows as switched off and goes quiet; a broken one shows its broken look.
- A maintenance worker servicing a filter no longer restarts its wear countdown (the original lets it run on).
- Freeform starting cash works as in the original: it remembers what you last chose, and the difficulty beside it (Easy, Intermediate, Hard) follows the amount. Scenarios start with their own money.
- A map now opens on the view it was saved with, as in the original (Basic Grass Map and most others).
- The date stays on the bottom bar while the game is paused.
- Zoo Status, Recommendations, plaques, the message list, help and the other windows the original lets you drag can be dragged by their title again.
- Staff Information opens on its Status page, as in the original.
- The Staff List's pictures are full size again (they were drawn at half size).
- Button help shows after a click too, as in the original (it waited for the mouse to move first).
- The Game Options button on the sidebar closes the Game Options window again; the Game page's buttons sit in the middle of the page.
- Looping sounds (the zoo's ambience, buildings, filters) stop when you load another zoo from inside a game.
- Guests now go in through a building's door, as in the original: the door has to be on a path, and it turns with the building. Before, a path touching any side of a building would do, and turned buildings used the wrong footprint.
