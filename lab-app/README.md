# ZT1 Lab (the app)

**Help:** the tab beside Mods contains searchable guides, illustrated tutorials
and nine bundled videos. Start with **Start with the Lab**, or search for an
animal, OBJ import, a tank, a map or publishing. Press **F1** from any Lab tab.
The library works offline and includes the earlier overview, labelled as an
earlier interface. Current lessons cover the redesigned Mods studio.

The friendly face of the Lab, built with Electron to the design in
`re/lab/design/`.

**Item builder:** Mods → Make something → Build an item creates separate
buildings, fences, staff, tank filters and scenery, or customizes a selected
game item. Draw with Blank and external PNG/OBJ import, or Remake existing art.
Guest appearances target the game's four existing guest kinds; staff inherit
existing duties. Check service, damage and work poses as well as idle/walk.
See [the modding guide](../docs/MODDING.md#build-buildings-fences-people-and-objects).

**From a checkout:** double-click **`ZT1 Lab.cmd`**, or run:

```bash
npm start
```

It needs `build\Release\zt1-engine.exe` built first. The first time, it
installs its packages (Electron) into `node_modules/`, which isn't
committed.

**Installed:** `dist/ZT1-Lab-Setup-<version>.exe` (a release's third
download), which is this app - Electron and
all - with the engine and what the Lab needs inside it, and no admin
prompt. It installs into `%LocalAppData%\Programs\ZT1-Lab`:

| | |
|---|---|
| `ZT1 Lab.exe` | the app (the page inside `resources/app.asar`) |
| `tree\engine\` | `zt1-engine.exe`, built for release, and the hook for the original game |
| `tree\re\` | the knowledge base and the Lab's Python tools |
| `tree\clangd\` | the code intelligence |
| `tree\ghidra\` | Ghidra itself, and `tree\java\` the JDK it runs on |
| `tree\python\` | a Python of its own, with what the Lab's tools need |

Nothing here asks the player to install Python or Java: `tree\python`
is an interpreter with `capstone`, `pefile` and `unicorn` beside it, and
`tree\ghidra` is Ghidra with the JDK it runs on. `tree\ghidra` is the
program, not a project of the game: a Ghidra project is built out of
`zoo.exe` itself - the game, which is nobody's to hand out - so the Lab
makes one from the player's own copy.

The installer asks for the engine's checkout (and remembers it in
`%LocalAppData%\zt1-engine\lab-roots.json`). With one, the Lab reads and
edits the real tree - Write Code, Build engine, Game Files, Sync, the
code intelligence - exactly as running from a checkout does. Without
one, it runs on what it carries: the lab zoo, Explore, Original Game's
functions and code, the game's own files, Play. What needs a checkout
says so when you click it.

**One thing it does not carry, and will not: the game.** *Zoo Tycoon* is
not free, and the Lab never ships it. It reads a copy you already own and
have installed - the original `zoo.exe` for Original Game, Play original
and the traces, and the game's own files for the Scene and Game Files. It
finds it by itself, or you point it at yours:

- `re/private/original.json` — `{ "exe": "<your zoo.exe>", "iso": "<a Marine Mania disc image, if your copy needs the disc>" }`
- `ZT1_EXE` for the Scene's **Try zoo.exe**
- Game Files and the Terrain painter find the install by themselves (the usual
  `Microsoft Games\Zoo Tycoon`, or the Complete Collection)

Everything the Lab *does* carry is ours or third-party under its own
licence: the engine (`tree\engine`), the Lab's own tools and its knowledge
base (`tree\re`), clangd, an embeddable Python with `capstone`, `pefile` and
`unicorn`, and Ghidra with the JDK it runs on. No part of the game is in
the installer, and `re/private` - your recordings, your game's location -
never leaves your machine.

Each of those carries its licence with it: the build writes
`THIRD-PARTY-NOTICES.txt` from the licence text each project ships (and says
so when one does not), and `SOURCE-OFFER.txt` is the written offer of
corresponding source for the GPL-covered ones — the Temurin JDK and unicorn —
because that source is not in the installer. Both sit beside `LICENSE.txt` in
the install; `engine-build-resources/notices.py` writes them, and
`fetch_licenses.py` keeps the one text no package provides (unicorn's GPLv2).

The installer is built on the release machine. Its packing and the
backend it carries (a Python with `capstone`, `pefile` and `unicorn`, and
Ghidra with its JDK) are part of the release tooling, which isn't in this
repository.

The **checkout** needs none of that: it runs on the machine's own
Python 3, so that still needs `unicorn`, `capstone` and `pefile`.

## The shared knowledge base

Everything the Lab works out about the original's `zoo.exe` - what
`0x4378df` is, a struct's fields, which functions ran in a recording - is
the same answer for every install, because it is all about one build of
one game. So it can be shared: `re/cloud/` is a Cloudflare Worker and a
database that moves findings between checkouts, and `re/knowledge.json`
stays the copy that ships and that you review in git.

```bash
python re/cloud/sync.py pull        # bring in what others have found
python re/cloud/sync.py contribute  # send what this checkout worked out
```

The released Lab and checkout include the shared endpoint, so no per-user
setup is needed. `re/private/cloud.json` or `ZT1_KB_API` can override it.
**Sharing** in the Lab's header turns it on (off until you do): the Lab then
receives and sends in the background - at start and every half hour, sending
only what changed - and its dialog shows when it last did and **Share now**.
The Lab can sync from its control port too, so an AI agent can
(`lab_kb_cloud`). Anything contributed lands **provisional** until it is
reviewed, and a contribution from a `zoo.exe` build other than the 2002
Complete Collection is refused - its addresses would land on the wrong
functions. Recordings never leave the machine; the addresses they found
do. See **[`re/cloud/README.md`](../re/cloud/README.md)**.

## Explore (start here)

The original game has no source code and no documentation. What's known
about it is spread across the knowledge base, the recordings, Ghidra and our
engine's comments. **Explore** joins all of it into one map you can ask in
plain words. Nothing is sent anywhere: the search runs in the Lab.

- **Ask** "selling animals", "show tank", "where is the zoo saved",
  "cHungerCheck" or an address like `0x4378df`. Each answer says what it is
  (a function, a recorded action, a config key, a struct, our engine's
  code), why it matched, and where to go next:
  - **Open its code** shows the function in Original Game.
  - **Open in Write Code** jumps to the engine line.
  - **Which game files set it** runs a `has:` search for a key.
- **What's missing** lists every action recorded in the original, against
  our engine:
  - **Copied from the original** and **Partly traced** come from what our
    engine's code cites, so they're reliable.
  - **Probably in our engine, not traced** and **Not found - check** are
    guesses from the action's words, until someone checks.
  - **Not in our engine** only ever comes from a person's checked verdict.

  Open an action's **Details** to see:
  - our engine's closest code;
  - the functions that run only for that action, green where our engine
    cites them (click one to read it);
  - everything that ran in the recording.

  Checked it? Press **Our engine does this** or **Not in our engine**. The
  verdict is saved in `re/lab/verdicts.json` with your name.
- **The map** is `re/private/atlas.json`, built by `re/lab/atlas.py` from
  the knowledge base, the recordings, Ghidra's function list and texts, and
  every comment and function in `src/`. When you record or name something,
  Explore says the map is stale; **Rebuild the map** takes a few seconds.
- From an AI agent (MCP): `lab_action exAsk "<question>"`, `exMissing [status]`,
  `exOpenAction "<label>"`, `exVerdict "<label>|<status>|<file:line>|<note>"`
  and `atlasBuild`.

The usual loop for a modder:
1. Play the original (Play › Play original) and record an action.
2. Rebuild the map, and look the action up in Explore.
3. Read its own functions (Open its code), write the feature (Write Code),
   and cite the addresses in a comment.
4. Rebuild: it turns green.

### Code 1:1 with zoo.exe (Explore's right side)

Our engine is meant to be the original, 1:1. **Code 1:1 with zoo.exe**
(next to Recorded actions) shows how far it is, both ways round. It's built
fresh each time by `re/lab/fidelity.py`, and the answer is saved to
`re/private/fidelity.json`.

- **Formula checks** (`re/lab/formulas.json`) run the original's function
  in zoorun and ours on the same numbers, case by case:
  - On the original's side, zoorun lays the objects out from each case's
    inputs, and reads back ST0 for a float, eax, or a field.
  - On our side, the engine's `formula` command calls the very function
    the game calls.
  - The checks run again by themselves after every build that works, and
    a toast says if any case differs.
  - To add one, make our formula a function the game and the check both
    call. Then add it to `formulas.json` and to the `formula` command in
    `src/Lab.cpp`.
- **The original's gameplay, by system**: every function the knowledge
  base names, grouped by system (money, tasks, jobs, guests, animals,
  staff…). Each is green if our engine cites its address and red if it
  doesn't. Click one to open it.
- **Our code, by file**: every function in our gameplay, UI and drawing
  code, and whether it says where it came from. A function counts as
  marked when its comment or body has one of:
  - a `zoo.exe 0x…` address (copied);
  - `measured:` (measured from the original's behaviour, saying how);
  - `ours:` (the original has nothing like it);
  - `addition:` or a `Features::` switch (a new feature).

  The rest are listed to go through. **#** counts the *bare numbers*: a
  number with a point in it on a line that doesn't say where it's from.
  Those belong in `src/OriginalConstants.hpp` with their address, or come
  from the game's files (economy.cfg's keys are read into
  `Original::economy()`).
- From an AI agent (MCP):
  - `lab_action exMode fidelity`
  - `exFidOpen "o:<system>"`, `"f:src/<file>"` or `"x:<check id>"`
  - `fxRun`

## The views

The tabs are named for what you do in each: **Original Game** studies the
original zoo.exe; **Write Code**, **Test Zoo** and **Play Game** are our
engine; then **Mods**.

| Tab | Was | What it's for |
|---|---|---|
| **Original Game** | Functions + Code | One window for the original zoo.exe. Left: find a function - search names, notes and the game's text (with the functions using each), `field:0x3d6`, filter by system. Middle: its code - decompiled C, or raw instructions where Ghidra made no function. Right: name it and note what it does (saved to the knowledge base and Ghidra), what calls it, the fields it uses, where our engine copies it. |
| **Write Code** | Engine code | Our engine's own source (and, on the left, the game's files): write the feature, **Build engine**. |
| **Test Zoo** | Scene | Our engine running a test zoo, as a scene editor and debugger (below). |
| **Play Game** | Play | **Play ZT1 Engine**: the whole game as our engine runs it now. **Play original**: the real zoo.exe, here in the same view, windowed and at the view's size by our hook (see *The original game in Play* below). |
| **Mods** | Experiments | Three sections, switched at the top of its left column. **Mods**: make, test and share mods (see *Mods* below). **Game files**: every file the game has. **Lab scripts**: small C++ scripts that run in the Test Zoo without changing the engine - checking one of the original's functions against ours, or making things such as the HD art mod (see *Making mods with Lab scripts*); edit, see what it will do in plain words, **Build & test**. |
| **Help** | New | Search the bundled guides, follow the current studio tutorials and play the recordings. Article history, section links and enlarged pictures keep longer lessons easy to navigate. |

**Laid out as you like:** Original Game (Find · Code · Name & details), Write
Code (Files · Editor · Build) and Test Zoo are made of panels. Drag a panel's
tab onto another's side to dock it there, onto its middle to stack them, or
out to float it; drag the edges to resize. **▦ Layout** (top right) brings a
closed panel back or resets the tab to its default. Each tab's layout is
remembered.

### Finding things the Lab couldn't before

- **Game files, `has:text`** looks *inside* the game's text files (.ai, .cfg,
  .lyt…), the copies the game really uses (a patch's over the original).
  Spaces and case don't count: `has:cpreattack=1` lists every animal with
  `cPreattack = 1`, with the matching lines under each file, and opening one
  jumps to that line. Combine it with words: `animals/trex.ai has:bpreattack]`.
  (A section is written `[m\BehaviorSet\bPreattack]`, so search the name with
  just its closing bracket.) An open game file's bar says what our engine
  makes of it: where our code names it, or - for a species' .ai, which goes
  through the one loader - how many of its keys our engine reads (`107 of
  its 193 keys: Class, Type…`, each a link to where it's read).
- **Functions, `field:0x3d6`** lists every instruction in zoo.exe that
  touches that offset - read or written, and the function it's in (or the
  bare code). It's how you find who uses a value the species loader filled.
  Add a size to keep to it: `field:0x3b0 byte` (or `word`, `dword`). The
  function each is in comes from Ghidra (a function's code can be in
  pieces). Above the list, a form names that offset in a struct
  (`ZTAnimal` + `0x3b0` = `rampaging`, bool) - saved to the knowledge base,
  then shown as Known and suggested in Write Code.
- **Where used on a global** (an address from 0x600000, a value in the
  game's data): the box has **Name it** - `0x641560` = `cDefenseRadius` -
  saved to the knowledge base's constants. From then on the decompiled C
  shows the name beside it: `DAT_00641560 cDefenseRadius`.
- **Find in the code**: the Code panel's header (it stays in view) has a Find
  box - the lines holding the words lit, Enter for the next, Shift+Enter
  back, `1 of 3`. It's kept as you open other functions, and a function
  newly opened goes to its first match.
- **Where used on text nobody uses**: a string marked "not used by any
  function (yet found)" has a **Where used** button. Its places are often in
  code Ghidra never made a function of (reached through a table).
- **Raw instructions**: open such an address and Code shows zoo.exe's
  instructions there (re/lab/zoobin.py, capstone), from where that code
  starts (a guess: just after the code before it ends), the address marked,
  and the text each instruction points at named (`"Started preattack"`).
  The page is titled `Code at 0x…` with **no function yet**, and
  **Make it a function** (in the header, always in view) turns that code into a Ghidra function from where it
  starts, and Original Game then shows its decompiled C, callers and all. The
  starts are kept in `re/made_functions.json`: the Lab's Ghidra bridge makes
  them again each time it starts (it opens zoo.exe read only), and
  `ZT1_Apply` makes them for good in your own Ghidra project.
- **Engine code suggests the binary**, beside clangd's C++ suggestions:
  inside `ZT_STEP("` or `ZT_FN("` type a name, part of what a function does
  or an address, and pick from zoo.exe's mapped functions (the knowledge
  base's first, then Ghidra's named ones) - the address goes in. After `+0x`,
  the mapped struct fields (`ZTAnimal+0x3` → zapCheck, egg, hatchIn…). Inside
  `num("` / `set(m, "` / `pushSet(m, "`, the game's own .ai keys and
  behaviour sets, with how many files use each (`num("cpre` → cPreattack in
  24 files). Hover an address for its name and summary, or a game key for
  where it's set. Name what you find (Functions › Name) and it's suggested
  from then on.
- **Scene › Watch** (a panel): follow animals and the engine logs a row each
  time one starts doing something else or goes a tile, in game time - paused,
  playing or skipping time. A hunt reads like a script: `roar_up_left`,
  `shake`, `snort`, `dustball`, `(gone)`. Pick an animal, then **Follow**.
- **Steps during a time skip**: skipping 200 s keeps the steps the engine
  reports on the way (the last 1500), so a rampage that starts at 140 s is
  there to read afterwards. A skip from an agent (`lab_scene_time`) waits
  till the engine says the skip is done.
- **Grow the database** (top of Find, in Original Game): what zoo.exe says
  about itself, beside what `re/knowledge.json` has, saved in one click. Each
  save is under the tool's name ("Lab loader mapper"), so it's told apart
  from what you name yourself.
  - **Config loaders**: every key the game reads (`cHungerCheck`) and where it
    goes, a struct's field or a global, grouped by the function reading them.
    The readers it looks for are in `re/lab/loaders.json` (int, string,
    float). Name the struct a loader fills (`ZTKeeperType`) and **Save**: each
    key becomes a field at its offset where nothing's named yet, each global
    is labelled with its key, and the loader itself is named
    (`ZTKeeperType::loadConfig`, or `loadAnimalSettings` for a loader of
    globals only). "N in the base" counts what's mapped already.
  - **Class tables**: the 223 tables of functions (vtables) zoo.exe's
    constructors store. Open one to see each slot's function and which named
    classes share it: shared slots are the class they come from (ZTAnimal and
    ZTGuest share 112 of 197). Name it and **Save**, and `kb.py show` then
    says which tables a function is in (`ZTAnimal (slot 0x2c)`).
  - **The game's text**: functions named by their own debug text
    (`GXVidMan::flip`) and source files from the developers' paths
    (`gx/gxvidman.cpp`). **Save all** gives provisional names where nothing's
    named; a function with only a file gets `gxvidman_4d64ab` till it has a
    name. One text in several functions is numbered (`unlockSurface2`).
  - **Refresh the Lab's Ghidra** (top right of the window): the Lab's Ghidra
    reads a copy of your project (`E:\zt1-ghidra-work`), so after ZT1_Apply,
    or naming things in your own Ghidra, this copies your project over it and
    restarts the bridge. Close your project in Ghidra first: it's locked while
    it's open.
  - **Naming a class table:** let the census (Play › the bar's readout) say
    what its objects are (`helicopter`, `herbchow`). A type's table has its
    config loader at slot 0x60, so `kbClassCompare` names the type tables.
    Comparing slots finds the base classes: a table no object uses that
    shares most of its slots with a whole family (ZTStaff, ZTUnit). The census
    uses the knowledge base's class names.
  - From an AI agent (MCP), `lab_action`: `kbTab loaders|classes|text`,
    `kbLoaderBrief all` (every loader's keys, a line each), `kbLoaderKeys`,
    `kbLoaderSave "0x4c34ae|ZTKeeperType"` (add `|likely` for a guess), `kbClassSlots`,
    `kbClassCompare "0x62ed84 0x62f0b4"` (each table's slots shared with every
    named class, its own, and its known functions), `ghidraRefresh`, `kbClassSave
    "0x62ff54|ZTAnimal"`, `kbTextSave`, and `kbOrphans` / `kbMakeOrphans` (the
    engine's zoo.exe references that fall where Ghidra made no function, each
    with where its function would start and what comes before it; check the
    raw code before making one, because a piece of a bigger function or a
    jump table can look like a start).
- **The Test Zoo survives a build**: Build engine restarts the engine, and
  the Lab then makes the zoo again - the map, every fence, animal and object
  placed with the mouse, the pins, the Watch follows - and runs it back to
  the same time. The engine log says so ("made again after the build: 17
  steps of set-up, back to 199 s"). Loading a map starts the list afresh.

## The Scene

The Scene is our engine's real world, the same code the game runs. You can
build in it, watch its code run step by step, and try zoo.exe's own code on
anything in it.

**Laid out as you like:** every piece of the Scene tab is a panel. The pieces
are the Scene view, Zoo, Inspector, Steps, Step, Our code, zoo.exe C, zoo.exe
binary, Compare, Errors, Try zoo.exe, Log and Console. You can:

- drag a panel's tab onto another panel's side to dock it there, onto its
  middle to put it in with that panel as a tab, or off to float it;
- drag the edges between panels to size them;
- close a panel with its ×.

**▦ Layout** (top bar) brings panels back and switches the Scene view's
floating pieces on and off: the tools, the map and view bar, play and time,
the status box (what's under the cursor), and the exhibit names. It also
saves layouts by name, loads them, and resets to the default. The layout is
kept between runs. **Classic** goes back to the fixed layout (`layout.js`,
using dockview-core).

**The zoo**

- **Maps:** the map name (top right, or top left) loads any of the game's
  freeform maps, as it is or made flat (all grass, nothing in it). The game's
  files are what the engine reads, expansions included. The Lab is where
  unfinished things get tried.
- **Save as a map** (beside the map name): the zoo as a freeform map of your
  own, in a mod - its name, description, size, and which mod. The engine
  writes the design (`SaveGame::saveMap`: its ground, paths, walkways,
  fences, exhibits and objects over the map it was loaded from, and the view
  it starts on) and re/lab/mapmaker.py packs it: `freeform/<id>.scn` (the map
  it's over's .scn, with `[start] design=` and `[freeform] title=`),
  `freeform/<id>.zt1map`, its description and a 367x276 picture of the view
  you're looking at. Switched on, the mod's map is in the game's freeform map
  list (the "custommaps" switch).
- **A blank map** (the map list's first entries): flat grass with nothing on
  it, Small 75 x 75, Medium 125 x 125 or Large 150 x 150 as the game's maps
  are, or any size from 16 to 512 a side. It starts in **Edit the map**
  (beside the map name; **✎ Editing the map** while on): walls, entrances
  and scenery go anywhere, not only inside the zoo. Paint the ground and
  paths, draw the zoo's boundary with the Zoo Fence, and put down an
  entrance - the Zoo Entrance, Dinosaur Digs' or Marine Mania's (`aqgate`:
  the game has no name for it), and their admission booths, all under
  Buildings - where guests should come in: the wall opens across its
  doorway. Switch editing off to see the zoo
  as the game will (its inside is what the wall and entrance close in; the
  Exhibits view and the Terrain view show the walls, the zoo wall in red).
  Saved as a map, it's packed with `[start] blank = W H` and no map of the
  game's under it; the game's map view draws what you made.
- **Terrain** (the view): the whole map from above - each tile its ground's
  colour, lit by its height, paths pale, objects as dots - to design a map
  by. The tools work in the Scene view (the Lab's own place tool switches
  back to it).
- **Zoo outline** (left): every exhibit, animal, staff member, guest,
  building and kind of scenery. Click one to select it and centre on it. The
  **Mods** tab beside it holds the mods.
- **Placing** (**＋ Place**, or P): the buy panels' items by tab, with the
  game's icons, plus terrain paint and height tools. Each is put down with
  the game's own tool, so the same rules apply. Fences and paths have the
  drag modes (Bend, Straight, Rectangle: Tab). Animals can be male or
  female, objects turn (R), and the terrain brush has a size.
- **Other tools:** **Bulldoze** (B), **Undo** (Ctrl+Z), and right-click to
  stop a tool.
- **Animal** (the view): a species' art on its own. Pick any of its
  animations; **Layers** draws a Marine Mania animation's layers together
  ("shadow" + its art, "under" + its art, then the animation, at one anchor:
  the red cross); ◀ ▶ hold every layer on one frame and step it. **Check**
  goes through the species in bulk: art missing or not loading, layers missing
  or with other frame counts, behaviour-set steps the engine skips (not done
  yet), moves to places it doesn't know, and animations or sets named but not
  there. **Check marine** does it for every tank animal.
- **Route** (T): can it get there? Pick **Guest** or **Staff** and click
  where from, then where to (each click after is a new where to). Or pick
  **Picked animal** and click where to. The search runs by that kind's own
  rules: guests on paths and not into exhibits, staff out through gates with
  paths cheaper, an animal on its exhibit's ground. It's drawn on the ground:
  - what it looked at, fainter first
  - what was still open (yellow)
  - tiles it couldn't go onto (red)
  - steps refused, such as a fence or cliff edge (red edges)
  - the route (green), the start (blue) and the goal

  The chip says how it went.
- **Routes** (an overlay): where everyone is walking (animals orange, guests
  blue, staff green, goals as diamonds), and the picked one's last route
  search drawn as above. Its inspector has a **Route** section: where it's
  going, and how its last search went. If it failed, it says why: no way
  there, gave up at its limit, or off the map. Each search is also a step
  ("Route search done") you can stop on. **Blocked** is the engine's older
  staff overlay: where staff can't walk.
- **Inspector** (right): click anything (an animal, guest, keeper, building,
  rock, exhibit, the ground) for its live values. Type a new value to change
  it. **ƒ** opens the zoo.exe code behind a stat.
- **Pins** (⊙ beside a stat): an animal's or guest's value held where it's
  set, after every update. Pin a lion's hunger at 100 and watch what the
  engine does about it. Its steps still show what the engine tries each
  tick, so you see the rule fire and the pin hold.
- **Actions** (the inspector): buttons for what's picked.
  - **animals:** hungry, tired, sick, heal, happy, unhappy, dung, dart,
    crate, plus any of its animations (**▶ Play**) or behaviour sets
    (**▶ Do**), on demand
  - **guests:** hungry, thirsty, bathroom, tired, leave, their animations
  - **staff:** their animations
  - **exhibits and tanks:** the game's console commands: food, no food,
    dung, wear and break fences, dirty water

  Each press is a step in the Steps dock.
- **Clock** (bottom left): run or pause (Space), 1–8×, or +1 s / +1 m of
  game time. A running mod shows its steps there instead.
- **Overlays** (top right): the grid, exhibits' outlines and names, routes,
  where staff are blocked, footprints. Footprints also marks each door (a
  building's `entrpos`, turned with it): green on a path, yellow off one. The
  console's `doors` lists them. The console's `filters` shows each filter's
  state and sound, and `drain` / `fill` empty or refill a tank. A selected
  tour guide shows its followers; a selected guest, the guide it follows. Under the cursor (bottom right) you see the tile, its
  height and ground.

**Steps** (the dock under the view): the engine's code as it runs.
The engine's code reports its steps through trace points (`src/LabTrace.hpp`:
`ZT_STEP`, `ZT_FN`, `ZT_WHO`). Each step gives:

- the file and line
- the zoo.exe function that code is ours of
- what it read, and what it changed (before → after)

The steps are listed on the left (drag the divider to widen it). Beside them,
the step picked: what, who, when, its zoo.exe function, and all its values,
then panes, one or two side by side (**◫ Split**; **⤢** makes the dock tall):

- **Our code:** the engine's line (lit, its values under it, what ran since
  the last step tinted).
- **zoo.exe C:** the function it copies, decompiled.
- **zoo.exe binary:** that function's own machine code run on what's picked
  (as **Try zoo.exe** runs it): every instruction, the registers, and what
  each read and wrote, by field name; red where it touched something nobody's
  mapped. It runs on the values as they are now (stopped at the step: as they
  were then).
- **Compare:** the step's values beside what the original read or wrote in
  the field of the same name: same, or differs.
- **Errors:** what the original reached for and couldn't find (fields not
  mapped, empty vtables, null pointers), each with **Name it** and **Give a
  value**; it runs again after.

The tabs show counts: values that differ, and errors.

- **Trace:** what's picked (an exhibit includes its animals, and using a tool
  traces the tool), everything, or nothing.
- **Stopping:** **Stop at each step**, **Stop at the next**, or a breakpoint
  (click a line number, or F9). The engine really stops there: the world
  frozen mid-update, still drawn, the one it's stopped on ringed in cyan.
  **Continue** (F8), **Over** (F10), **Step** (F11), **Out** (Shift+F11).
- What has trace points so far:
  - **animals:** status checks, behaviour choices, eating, adopting, exhibit
    ratings
  - **guests:** arriving, needs, deciding where to go, watching and what they
    think of it, donations, prices and buying, leaving, every thought
  - **staff:** looking for work, keepers' visits (food, healing, raking),
    maintenance jobs, catching escaped animals, tour guides
  - **the tools:** placing, fences, paths, bulldozing

  More code gets them as it's worked on.

**Try zoo.exe** (the dock): run one of the original game's functions on
what's picked. Find a function in Code, Functions or Steps and press **Try it
in the Scene** (or type its address or name). `re/lab/zoorun.py` runs zoo.exe's
own x86 code (Unicorn), sandboxed. What's picked is laid out as the knowledge
base says its struct is, filled with live values: the animal as ZTAnimal, its
species as ZTAnimalType from its `.ai`. You see:

- **in red, what it wanted and couldn't find:** an offset nobody's mapped,
  an empty pointer or vtable. Each says **where** (the instruction), with
  **Name it** (saved to the knowledge base; use the engine's name for it and
  it's filled live), **Give a value**, and **Game files**.
- **what it used:** known fields, with their values (read, or before →
  after), the text it used, the globals (as in the file), the functions it
  called (**Skip** them, 0 back, or **Run them** too).
- its instructions as they ran, with the registers and what each touched,
  and its C with the lines that touched known (green) or unknown (red)
  fields lit.

Run it again after naming or filling something, until it's all green. Then
**Apply what it wrote** puts its results into the scene, and **Make it a
mod** starts a mod for it. It needs `pip install unicorn capstone`.

**Log** and **Console** (the dock): the engine's log, and the game's own dev
console (`help` lists its commands). A command's answer always shows, even when the log was scrolled up. Ones for modders: `text <id> ...` (the game's words for ids - cNameID, cUsedThought), `doors` (each building's door tiles, on a path or not), `fences` (the map's fences by type, which are zoo wall, how many tiles are inside) and `fences map` (inside `#`, outside `.`, zoo wall `+`, drawn - when nothing will build, it shows where the wall leaks).

The place tool says when the game put nothing down (an iceberg off a tank's water, say) instead of saying nothing. The Watch panel empties when a new map or engine starts. A scene load sent while the engine restarts is sent again to the new one; a map name it doesn't know says so and what loaded instead (a mod's map is listed once the mod is on and the engine has started again). **Save as a map** puts the map's description in a new mod's About too.

## How it works

`main.js` starts our engine twice:

- **The Lab's engine** (`zt1-engine.exe --lab-host 18490`) is out of sight
  and silent. It builds and steps the mods, and sends its view to the app as
  frames.
- **The game** (`zt1-engine.exe --embed <window>`) is a borderless window
  kept over the Play view, moving and hiding with the Lab.

Both close when the Lab closes.

Ghidra comes in through the `ZT1_LabBridge` script (127.0.0.1:18489). The
Lab starts it for itself: when nothing answers there as it opens, it runs a
headless Ghidra on the project, read only (`ZT1_LabBridgeHeadless.py`; the
pill says **Ghidra starting** for a minute or two), and closes it with the
Lab. Where Ghidra and the project are: `re/private/ghidra.json`
(`{"home": "...ghidra_12.1.4_PUBLIC", "project": "E:\\zt1-ghidra-work",
"name": "zt1"}`) if you make one, else `E:\ghidra\ghidra_12.1.4_PUBLIC` and
`E:\zt1-ghidra-work` (a copy of `E:\zt1-ghidra`, so your own Ghidra's
project isn't locked) or `E:\zt1-ghidra`. Its output: `re/private/ghidra-bridge.log`.
Your own Ghidra with the bridge on is used instead (in Ghidra, with zoo.exe
open: Tools > zt1-engine > **Start Lab bridge**, or **Ctrl+Alt+L** on a
function). Everything fetched is saved in `re/private/ghidra/`, so code
you've seen still shows with Ghidra closed.

What the game prints in Play goes to `re/private/play.log`.

## Engine code

The **Write Code** tab is our engine's own source, edited and built in the
Lab. The editor is Monaco (VS Code's editor), and clangd (`clangd/`, the C++
language server) works on the code behind it.

### Scripts (＋ New script)

**＋ New script** (beside ＋ New file) makes a script in `src/scripts/`:
engine code, built in with the rest, with the zoo's parts set up for you
(`src/EngineScripts.hpp`). Pick its name, what it does and when it runs:

| When it runs | Its function | Run it |
|---|---|---|
| Run once | `void fn(Zoo &zoo)` | **▶ Build & run** on its bar |
| Every tick | `void fn(Zoo &zoo)` | **▶ Build & switch on** (off again on the same button) |
| Each animal, every tick | `void fn(Zoo &zoo, Animals::Member &m)` | the same |

`zoo` holds `world`, `animals`, `guests`, `staff`, `fences`, `map`, `clock`
and `dt`, and what's picked in the Test Zoo: `animal` (a pointer, or
nullptr), `guest`, `staffMember`, `exhibit` (ids, -1 for none), `tileX`,
`tileY`. `zoo.say("...")` writes a line in the engine log and a step in
Steps; `num(x)` writes a number as words. Each template starts with a small
working example and a list of all of these.

The bar over an open script shows its state (not built in yet / on) and
builds it first if anything changed. The engine restarts on the build, the
Test Zoo is made again as it was (with what was picked, and the scripts
that were on), then the script runs. Nothing runs unless the Lab switches it
on, so a script never runs in Play. A script is a place to try a piece of
game code on the live zoo, then move it where it belongs. Copying zoo.exe?
`ZT_STEP("0x…", …)` in a script counts as in our engine like anywhere else.

- **Editing:** highlighting, coloured brackets, folding, a minimap,
  multi-cursor, and undo.
- **Finding:** Ctrl+F finds in the file, Ctrl+H replaces, and Ctrl+G goes to
  a line. The box on the left searches the whole engine.
- **Code intelligence:** autocomplete of the engine's own classes, hover to
  see a type, F12 to go to a definition (in any file, opened in a new tab),
  and Shift+F12 for every use.
- **Errors:** they show as you type, as red squiggles with "did you mean…"
  fixes. Build errors show the same way.
- **Calls:** right-click in a function › **Who calls this** (Shift+Alt+H)
  shows its callers in the **Calls** tab under the editor; each opens to its
  own callers, and a click goes to the call. **What it calls** turns it the
  other way round.
- **Right-click** also has: search the engine for the word, **Scene: stop
  here** (a breakpoint, F9), **Open its zoo.exe function** (on a line whose
  `ZT_STEP`/`ZT_FN` names an address), **Copy file:line** and **Build engine**.
- **The libraries** (`vendor/`: SDL, libzip, the PE loader) open and change
  here too: type `vendor/` and a name in the search box.
- **＋ New file** (beside the search box) makes a new `.cpp`, `.hpp` or `.h`
  in `src/`; it's built with the rest on the next build.
- **Game mods.** In Game files, an animation open in the viewer has
  **✦ Make a mod from this…** (`re/lab/animaker.py`): pick one of its colours
  (and, ticked, its other shades), the colours it flickers through a frame
  each, and its speed, and it writes the new frames and palette in the
  game's own format as a mod: `build/Release/mods/<name>/mod.json` and
  `content.ztd`. **Switch it on** restarts the Lab engine with it loaded.
  The game loads the mods switched on (`mods.cfg`) before its own files
  (`src/Mods.cpp`), and players switch them in the main menu's **Get New
  Zoo Tycoon Items**, the Mod Menu (`src/ModMenu.cpp`). Both are switches
  (`mods`, `modmenu` in `src/Features.hpp`): off, the original's files only.
- **Testing menus in Play:** the Lab can click inside the game
  (`playCmd` action: `click <x> <y>` at the game window's pixels;
  `src/Embed.cpp`), and the game holds still while the Play tab isn't shown.
  `move <x> <y>` rests the mouse there (hovering: tooltips, lit pieces);
  `press <x> <y>` / `release <x> <y>` hold and let go of the left button, so
  `playSteps "press 870 337; move 920 337; wait 700; release 920 337"` is a
  drag at your own pace (the game follows the mouse's events, not the real
  mouse, for drags);
  `snap` saves the next frame that draws a tooltip as `tooltip.bmp` beside
  the game - its own frame, as a window grab of the GPU window can come
  back stale. `shot <file.bmp>` saves the next whole frame - the same line
  the original's hook answers, so `lab_play shot` (a .png) pictures either
  game in Play. The game holds still while the Play tab is hidden, so a
  picture needs the Play tab showing. The dev console's
  `tankwalls <ex> <height>` sets a tank's walls (a raised tank shows its
  staff climbing in and out).
- **Screen size in Play:** the game draws 1:1 at the Play view's size, as
  the original does at any size (menus centred, the in-game screen filling
  it). The Lab owns the window, so the game's own window-size and full
  screen buttons are greyed in Play. The game prints `[Embed] ready` once
  its window exists, and the Lab places it over the Play view then.
- **Game Options:** the main menu's Game Options (in Get New Zoo Tycoon
  Items' place) and the in-game disk button open the Options window
  (`src/ui/UiOptions.cpp`): Game (in a zoo), Graphics, Sound, Controls,
  Gameplay, Mods & Updates, every setting kept in zoo.ini. The
  `optionswindow` extra off brings back the original side panel. A
  checkout's loose `build/Release/ui/startup.lyt` overrides the game's;
  the engine edits its Game Options line by key, so either works.
- **Build engine** saves every file and builds the engine. The lab zoo
  restarts on the new build, and **Restart the game** in Play runs it.
- **Make release** builds the engine x64 Release in `build-x64/` (the C++
  runtime built in, today's date as its version) and its installer,
  `dist/ZT1-Engine-Setup-<version>.exe`, with its `.sha256` and
  `dist/latest.yml`, the in-game updater's feed. It runs the release
  tooling (`engine-build-resources/make_release.py`, which needs Inno
  Setup 6), which is private and only on the release machine; without it
  the button says so.
  - The installer holds the engine, its fonts, the licences and the
    player's README. It holds none of the original game's files: the engine
    reads the player's own install where it is.
  - A build made here isn't official, so it never updates itself. Only
    published releases are.
  - Nothing is uploaded: releases are published from the release machine
    (CONTRIBUTING.md).

- **Sync** (the tab under the editor) takes the engine to GitHub and CodeRook.
  GitHub (`AlexC1991/zt1-engine`) is the home: work goes on branches, and
  into main by pull request. CodeRook keeps a public mirror of main.
  1. **Get latest main** merges GitHub's main into your branch. Any conflicts
     are listed: **Take mine**, **Take theirs**, or open the file and fix it
     by hand, then **Fixed it**.
  2. **Commit** commits everything changed. `.gitignore` keeps `build/`, the
     game's files, `node_modules`, `.cache` and `dist/` out.
  3. **Push** pushes your branch.
  4. **Propose** opens a pull request into main on this repo, never on the
     upstream it was forked from.
  5. **Mirror main to CodeRook** (the owner only) sends main as it is on
     GitHub to CodeRook. It works from a copy of main in `.cache/mirror-main`
     using `cbx submit`.

  **▶ Sync** does 1 to 4 in one go, after showing what goes where. What's
  offered follows what GitHub says the person may do:
  - the owner: everything, including the mirror;
  - helpers with write access: push their branch, then propose;
  - anyone else: their fork is made and pushed to, and the proposal comes
    from there.

  Nothing is ever force-pushed (`lab-app/sync.js`).

`compile_flags.txt` at the repo's root tells clangd how the engine is
compiled. Each time the Lab starts clangd it writes
`.cache/lab-clangd/compile_commands.json` from it (every source in `src/`),
so clangd indexes the whole engine and **Who calls this** finds callers in
files that aren't open.

## Mods (the Mods tab's Mods section)

Making mods properly, and sharing them. The guide for modders is `docs/MODDING.md`.

The section is a **studio** (renderer/studio.js): one workspace for the mod being made.

- **The top bar**: the mod (click it for **the picker**: every mod in the game's mods folder, **Find a mod**, each one's on/off switch - switched on, it's in the Test Zoo and in Play when they next start - **New mod**, **Bring one in**, **Mods folder**), its switch, **All saved** / **Not saved**, **▶ Test in the zoo** (switched on, the Test Zoo's engine restarted with it), **Play with it**, **Publish** and **Folder**.
- **The tree** (left): everything the mod holds, by kind, read by re/lab/studio.py from its .ztd files - its **Animals** (its own, *new*, and the game's it changes, *changed*), **Buildings**, **Scenery**, **Foliage**, **Fences**, **Paths**, **Staff** and **Maps** (its `freeform/*.scn`) - with **＋** on a kind to add one; **＋ Make something**, **About this mod**, and under **Tools** **Paint and recolour art**, **Its files** and **Publish**. (Whether a file changes one of the game's is read from a list of the game's file names kept in `%TEMP%\zt1-lab-names.txt`, made again when the archives change.)
- **Make something**: cards, each starting from something that already works in the game - **New animal** (a name and an animal it behaves like), **New map** (a blank map of a size you pick, in the Test Zoo with editing on; **Save as a map** offers this mod), **Change a game item** (any of the game's animals, buildings, scenery, fences, paths or staff, copied in).
- **The middle** is the editor for what's picked: an animal's animations and the animation editor, a building's or a scenery item's picture and file, a map's preview and **Edit it in the Test Zoo**, and the About, Files and Publish pages below.
- **The Inspector** (right): the picked animal's, building's or item's values from its file as controls, grouped and named in plain words - an animal's price, food and rest, group and space, babies, what it can do (jumps, climbs, swims...), its habitat wishes, guests and health; a building's price and size, guests inside, what they pay and what it does for them - and **All its values** for the rest. A change is saved into the mod's file as you leave the box (its line changed in place, the file's line ends kept); the file open in Write Code is updated too. Nothing picked: the mod's own details.
- **An animal's Needs** (its **Needs** tab, renderer/needs.js; re/lab/animalmaker.py `needs`, `setneeds`, `copyneeds`): everything the game weighs for it, from its species file. **Kind and care**: its kind (`[Member]`: a zoo animal, Marine Mania's `aqua`, Dinosaur Digs' `dinosaur`), who looks after it (`cKeeper`: a keeper, the Scientist 9551, the Marine Specialist 9552), what it eats and which food (`cKeeperFoodType`: herbivore chow, meat, fruit, bamboo, grass, fish chow, insects, dinosaur meat and greens, krill, fish, chum, sea grass, sea urchins - food.cfg's order, the expansions' after), needs a shelter. **Home**: biome, continent (or era), family. **Ground it likes** (`[cCompatibleTerrain]`, a slider each), **biomes, plants, rocks and shelters it likes** (`[cSuitableObjects]`, with their icons; + Add, ×) and **animals it lives with** (`[cCompatibleAnimals]`, families and species). Names come from the game's own text (the engine's `texts`) and the catalog (its items' `nameId`). Saved into the mod a moment after the last change. **Copy needs from…** another animal: its food, home, ground, likes and neighbours at once (its kind and keeper too, if asked) - a preset. A game animal a mod changes has the Needs tab too.
- **New animal** asks its kind first - a zoo animal, a marine animal, a dinosaur - and then offers only animals of that kind to behave like (its behaviour, home, keeper and, for a dinosaur, its egg come with it). Problems says when a kind doesn't fit its behaviour: a dinosaur with no egg (`egg_idle`, `egg_hatch`, `egg_break`), a marine animal with no underwater behaviour.
- **The dock** (bottom): **Problems** - what's missing or wrong, each one a link to where it's fixed (an animal's animations that play nothing, an animal still all linked to another's, no picture, no About, a map without a description, a .ztd the game can't read) - and the mod's **Log**.
  - **New mod** starts one from a template: a starter kit, game files (a .ztd), a shader, or empty.
    The **starter kit** (re/lab/modstarter.py) is a working example of everything a mod can
    have, made from the player's own game so no game art ships with the Lab: a new rock (a
    gold copy of the Large Rock, as a `.ucs` with its map art, icons and palettes) in the
    Habitat panel's Rocks under the Mods filter, its HD art (`hd/`, 4x), an art shader (off
    at first), a mod.json setting the shader reads, and a README.md walking through each file.
  - **Bring one in** takes an old Zoo Tycoon download (.ztd), a .zip, or a mod's folder, and makes it a proper mod (dropping one on the page works too). This uses the engine's own importer (`zt1-engine --import-mod <path>`, the same as the game's Mod Menu › Add a mod...).
- **The pages** the tree opens:
  - **About this mod**: its mod.json details (name, id, author, version, about), and a short guide.
  - **Its files**: what it adds or changes - each game thing in its .ztd files (every `.ai`, `.uca`, `.ucs`, `.ucb`, by kind), its .ztd files and what kinds of file they hold, its shaders, its HD art, what its mod.json says it replaces, and its settings. **Click a thing** to open its file in Write Code, to change: it opens as config text with the mod's name on its tab, and **Save** (Ctrl+S) puts it back inside the mod's .ztd (re/lab/ztdedit.py). Its bar has **Open the mod** and **Try in the Test Zoo** (saved, switched on, the Test Zoo restarted).
  - **About this mod** › **Its picture**: the picture the game's Mod Menu shows beside the mod (367 x 276), set from an image you choose or the Test Zoo's view as it is (another shape is cut to fill it, from the middle). It's `picture.png` in the mod's folder, named by mod.json's `picture`; the mod list and the mod's header show it too.
  - **An animal** - the animal maker (renderer/pixeled.js, re/lab/animalmaker.py): a **New animal** from scratch - a name and an animal it behaves like (its needs, biome, behaviour and sounds, all in its species file `animals/<type>.uca` to change; **Its species file** opens it) - with no art of its own. Its animations are listed one a row: **Linked** (its line in `[m/Animations]` is a whole path to an animation that exists, the way the game's own `box_idle = objects/box/idle/idle.ani` is - at first the one it behaves like, so it works in the game straight away), **Drawn** (its own) or **Missing**. **Link…** points any row at any animation, the game's or one drawn here; **Draw** opens the editor; **+ A new animation** draws one of a new name (its behaviour sets play it once you add it to one).
    - **The editor**, as Piskel's: transparent layers (a layer marked **Shadow** is the ground shadow - the game's colour 1), frames on a strip with onion skin, one set of frames for each way it faces (N, NE, E, SE, S: the game mirrors the other three; **Copy from…** another direction to start), a faint **guide** - the linked animation, by its ground point - under the drawing, or start from a copy of it. Tools: pencil (1-4 px, mirror pen), eraser, fill, line, box, oval, picker, move, the ground point (where it stands) and the height band; keys B E G L R O I M A H, [ ], arrows, space, Ctrl+Z/Y. **Import PNG** (one picture, or a strip of frames) and **Export strip**.
    - **The direction ring** (over the drawing): this frame in all eight ways it faces - N, NE, E, SE and S as drawn, and SW, W and NW as the game mirrors them from SE, E and NE (about the ground point, dashed). Click one to draw that way; a mirrored one draws its source.
    - **Starting a drawing: Remake, Stencil or Blank.** A new drawing opens **the stencil browser**: anything in the game - **Animals**, **People** (staff, and the guests: man, woman, boy, girl), **Buildings**, **Trees and plants**, **Scenery**, **Fences** - with icons and a search across them all, its animations as chips, and the picked one playing in all its directions (the engine's own frames, so it's quick). It only sets what you see while drawing; it isn't the kind of thing you're making. **Remake** copies its art in, every frame and direction, to change and recolour. **Stencil** lays it over the canvas: its **outline** (cyan, over the drawing) and its **inside** as faint as the **Inside** slider says (0%: the outline alone, 100%: all of it), with as many blank frames as it has, each drawn over its own. **Blank** is an empty canvas. (animalmaker.py `stencils` lists any item's animations: an animal's by its `[x/Animations]`, anything else every animation in its art folder - `objects/<name>/`, `fences/<name>/`, `staff/<name>/`, `guests/<kind>/` - or the folders its file names.)
    - **The Stencil card**: the one in use (click it to choose another, or **Remake** from it - Ctrl+Z goes back), show or hide it, its outline on or off, **paint only inside** the outline (the eraser and the shadow layer go anywhere; a fill stops at the outline), **fill the stencil** (the pen colour on this layer, in every frame and direction - a solid body to shade and detail), and the **Inside** slider. The stencil is kept with the drawing (`stencil` in its project) and comes back when it's opened again. The mod keeps only your drawing, never the game's art.
    - **The panels**: on the right, cards of icon buttons (each says what it does on hover) - **Stencil**, **Layers** (new, up, down, rename, shadow, delete; the eye hides one), **Every direction**, **This direction** (copy from another, flip, import PNG, export strip) and **View** (onion skin, mirror pen). **Zoom**: the bar on the canvas (out, in, fit) or Ctrl + scroll, about the pixel under the mouse. **The frame strip**: play, the frame before and after, ms a frame, the frames (+ adds one), then duplicate, move earlier or later, delete.
    - **Every direction** (each changes every frame of every direction): **Size** (a tenth bigger or smaller, about the ground point), **Body** (longer or shorter along the way it faces in each direction, from the middle of the body), **Band** (the height band picked with ⇕ - a neck, legs, a shell - taller or shorter; heights are the same in every direction, so one band works for all of them, and the ground stays put) and **Recolour a colour…** (one colour and the shades near it turned into another, each keeping how much lighter or darker it was). The canvas grows to fit; Ctrl+Z undoes each in one step.
    - **Save**: what's drawn is kept as drawn (layers, full colour) in the mod's `.art/animals/<type>/<sub>/<anim>.json` (left out of uploads), and made into the game's art in content.ztd - every drawn animation of the animal on one 256-colour palette made from all of them (`animals/<type>/<type>.pal`: 0 clear, 1 the shadow, up to 254 colours; more are merged and it says so), each frame cut to what's drawn, its `.ani` box worked out - and its row linked to it. The `edStroke` action ("x,y;x,y") draws a stroke with the tool in hand, for an agent.
    - (A frame header's first offset is its ground point's **y**, the second its **x** - src/Animation.cpp; the art maker read them the other way round, now fixed.)
  - **Art** - the art maker (renderer/artmaker.js, re/lab/artmaker.py): the mod's animations, and for each thing it changes or adds, the game's own art for it (marked *game*: a change saved puts a copy in the mod). One plays in every direction, zoomable, on a checkered, grass or dark background. Its palette is down the right (one palette colours all of an animal's animations, so a change there recolours the whole animal):
    - **Pick colours**: click the art or a swatch (Shift adds; the picked colours blink magenta on the art). **By shade** picks every colour used that's **Darker** or **Lighter** than a slider - a whole mane, a set of stripes or a coat's bright shades in one go (Shift adds). **Set** gives them a colour (several keep their own shades), **Hue/Light** and **Shift** move them; after a change the blink pauses a few seconds so you see the result. Mind colour 1: it's the ground shadow under an animal (pure black), and picking dark shades takes it too - Shift-click it to drop it.
    - **Paint**: a pencil in the brush colour (Alt-click a colour), right button to clear, on the frame shown.
    - **Pattern: fade along the body**: the picked colours turn into a **To** colour from the head back - starting part way, all of them further back - in every frame of every animation of the animal, worked out for each way it faces, with a fixed dither so it doesn't crawl. The shadow (the picked colours touching the bottom of the frame) is left as it is.
    - **Pattern: stripes across the body**: bands of the picked colours, each made its own darker shade, across the back (From/To along the body, Stripes, Width, Darker, Down the body) in every frame of every animation, for each way it faces; a stripe never repeats in under 4 pixels, so it stays a band on a diagonal facing.
    - The game's art index is kept between steps (`%TEMP%\zt1-lab-artindex.json`, made again when the game's archives change), so each step takes under a second.
    - **Save into the mod**, **Undo**, **Frames out (PNG)** (to the mod's `.art` folder, left out of uploads, to paint in another program) and **Frames in** (each pixel the palette's nearest colour). A saved file with one of the game's paths is listed in mod.json's `replaces`.
  - **Publish**: below.
- **Write Code > Game files**, an animal's `.ai` open: **New species from this** (re/lab/newspecies.py) makes a new animal as a mod: the animal's whole art folder copied from your own game under the new name (animations pointed at the new folder, frames at the new palettes, palettes dyed a colour you give - white takes the colour, black stays black), its `.ai` as a `.uca` with its own type, number and `[1033]` name. Into a new mod, or one there is (a pack of animals; or this one made again - its species file kept). Any text file open there: **Change it in a mod** copies it into a mod's content.ztd at the same path (so it takes the game's place while the mod's on), adds it to the mod's `replaces` and opens the copy to change. Saving into a mod the Test Zoo has open stops its engine for the write and starts it again (Windows won't replace an open file).
- **Opening files by name (MCP)**: `lab_game_file` and `lab_open_engine_file` take a game path (`animals/tiger.ai`, opened read only at a line) or a mod's own file, `mod:<folder>|<ztd>|<entry>` (opened in the editor to change; Save puts it back in the .ztd).
- **Names in a mod**: a `[1033]` section with `cName` in any `.ai` names it in the game (as the original reads it, zoo.exe 0x597337); add `[defaultLCID]` `LCID = 1033` so players with another Windows language see it too.
- **Game files' search**: `type:` takes an extension too (`type:uca`), and a mod's files have their own **Mods** chip.
  - **Publish**: **Check it** applies the same rules the repository's checks use (game data only, no paths outside the mod, at most 200,000 files, a mod.json with an id and a name). **Upload it** zips the mod and sends it in chunks to the mod repository (`https://mods.coderook.com/v1/modrepo/zt1`); there it's checked again, including a malware scan, and approved automatically, then it's in every official game's Mod Menu › Mod repository. Uploading again updates it. Where it's up already, its status and **Take it down** show here too. **Your uploads** lists every mod you've uploaded and what their checks said. You sign in with your CodeRook account (`cbx sign-in`, or a pasted personal token, kept in the Lab's own state). Each mod belongs to whoever uploaded it.

## Making mods with Lab scripts (the toolkit)

A Lab script can make things as well as test them: mods, art packs, anything a program on your PC makes. Its `Lab *lab` has a small toolkit for this (`re/lab/lab_api.h`, "Making things"), so you can build your own without changing the Lab:

- `start(program, args, cwd)` runs a program in the background and returns a job number. Poll `running(job)` from `lab_step`, then read `exitCode(job)` when it's done; `stop(job)` ends it. Its output goes to the log, except lines that are only a percentage.
- `engineExe()` is the engine itself, for its tools:
  - `--hd-export <folder> [prefix…]` writes map art's frames out as PNGs.
  - `--hd-finish <frames> <upscaled> <out>` tidies upscaled frames into a mod's `hd/` folder.
  - `--hd-survey [prefix…]` logs how many frames and pixels there are.
- `makeDir`, `removeDir` (it never removes a drive or a top-level folder), `writeFile`, `exists` and `countFiles(dir, ".png")` handle files and folders.
- `writeMod(folder, name, author, about)` writes a mod's `mod.json`, unless it already has one. A mod's own settings can go in that file too; see `mods/zt1-toon/README.md`.
- `useModsFolder(folder)` points the game at a mods folder, so it finds what you made. `modsFolder()` says where it looks now.

### The HD art Lab script (`re/lab/scripts/hd_art.cpp`)

This makes a mod of AI-upscaled map art: the animals, people, fences and objects drawn while the map is zoomed in. The game shows the original art until a player switches the mod on in the Mod Menu or Game Options › Mods & Updates, and the switch takes effect at once. It's an ordinary Lab script, so copy it to make your own version with another upscaler, model or set of art.

- Its settings are at the top of the file: the mod's name and folder, the mods folder, the art to upscale (folder prefixes such as `animals/lion staff/keeper fences/chainlnk`, or empty for all of it), and the upscaler and its model.
- Running it does three steps:
  1. The engine writes out the original frames (`--hd-export`). Each frame's clear edge pixels are filled with neighbouring colours so the upscaler doesn't draw dark fringes.
  2. [Real-ESRGAN ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN) (BSD-3, works on any Vulkan graphics card) makes them 4x. The inspector shows how many frames are done.
  3. The engine puts them in the mod's `hd/` folder (`--hd-finish`). Sizes are checked against the originals, the original outline is kept, and the texture's fine detail is put back.
  The work folders are deleted afterwards.
- Recoloured art comes out in the right colours. A building's chosen colours, staff hair and skin, and guests' clothes are swapped in when the HD frame loads (`HdArt::recolour`), because the pack is made from each frame's own palette.
- **In the game's dev console:** `hdart` lists the HD art mods with their counts: frames in use, frames missing, and the zoom the map was last drawn at. `hdart on|off` switches them.
- **Size and time:** the lion, zookeeper and chain-link fence came to 20,630 frames and 792 MB, and took about 3 minutes on an RX 6600. The scientist's 1,785 frames took 64 s. All the map art is about a million frames.

## Driving the Lab from an AI agent (MCP)

`mcp/server.js` is an MCP server that drives the running Lab through its
control port (127.0.0.1:18492, this PC only). Every tool acts through the
Lab's own UI, so you watch it happen and your mouse and keyboard stay free.
It needs only Node, with no packages to install.

How it answers (its design follows [rea](https://github.com/morluto/rea),
MIT):
- **Errors say what to do.** Each has a `code` (`lab_not_running`,
  `timeout`, `invalid_args`, `lab_error`, ...), `retryable` and a `hint`. A
  slow Lab is reported as a timeout, not as a Lab that isn't running.
- **Inputs are checked first.** A missing required input, or a value that
  isn't one of a field's choices, is refused before the Lab is asked.
- **Long answers come a page at a time.** Lists take `offset`/`limit`, texts
  (decompiled C, a game file) `from`/`to`; `paging` gives the totals and the
  next page's arguments.
- **Each tool is marked** read-only, destructive (a reload, a sync, removing
  something) and whether it reaches the internet.
- **Text from zoo.exe, the game's files and the shared knowledge base** is
  marked as data (`_meta.untrusted_content`), never instructions.
- **Deeper questions about zoo.exe** (Ghidra, through the bridge's newer
  queries - `re/ghidra_scripts/ZT1_LabBridge.py`, ideas from rea's own
  bridge): `lab_data_type` (a struct's layout as Ghidra has it, to check
  against the knowledge base), `lab_function_blocks` (basic blocks and each
  switch table's cases), `lab_call_targets` (every call's target; a class-table
  call such as `call [eax+0x16c]` matched against the knowledge base's class
  tables), `lab_value_flow` (where a value goes or comes from inside a
  function), `lab_search_bytes` (a byte pattern with `??` wildcards) and
  `lab_noreturn_functions`.
- **Guided workflows** (MCP prompts): `reverse_a_function`,
  `copy_behaviour_into_engine`, `test_in_the_test_zoo`, `fix_build_errors`,
  `map_a_struct_field`.

Setting it up:
- **Claude Code:** it's registered for this repo in `.mcp.json` (`zt1-lab`),
  and Claude Code asks you to approve it the first time.
- **Any other MCP client** (Codex, Cursor, VS Code / Copilot, Gemini CLI,
  Claude Desktop…): add a stdio server to that client's MCP settings. It's
  the command `node` with the argument
  `<repo>/lab-app/mcp/server.js`:

  ```json
  { "mcpServers": { "zt1-lab": { "command": "node", "args": ["C:/path/to/zt1-engine/lab-app/mcp/server.js"] } } }
  ```

  (Each client keeps this in its own file, e.g. `.cursor/mcp.json`, or
  `.vscode/mcp.json` under `"servers"`. See the client's own docs.)

Start the Lab first (`ZT1 Lab.cmd`). Without it, every tool answers that the
Lab isn't running.

### Your own paths

The Lab's settings for your PC live in `re/private/`, which is never
committed:

- **`original.json`:** `{ "exe": "<your zoo.exe>", "iso": "<a Marine
  Mania disc image, if the original asks for one>" }`. The exe defaults to
  the standard install folder.
- **`ghidra.json`:** `{ "home": "<Ghidra's folder>", "projects": ["<your
  Ghidra project folder>"], "name": "zt1" }`. `home` can also come from the
  `GHIDRA_INSTALL_DIR` environment variable — and you can leave `home` out
  entirely: an installed Lab runs the Ghidra it carries. To make a project:
  import `zoo.exe` into a new Ghidra project, then run **ZT1_Apply** to bring
  in everything the knowledge base knows (see `re/README.md`). The views about
  the original's code need Ghidra. The engine, game files, test zoo and
  Play views don't.

  *The Lab does not make a project for you yet.* It ships Ghidra with the JDK
  it runs on, but a Ghidra project is built out of `zoo.exe` — the game, which
  is nobody's to hand out — so the first project has to come from your own
  copy. Until the Lab grows a button for it (a headless import of your
  `zoo.exe`), the decompiled views start working once you point `ghidra.json`
  at a project with `zoo.exe` in it, as above.

Two things that help when driving it:

- **lab_state** includes `pageErrors`: the Lab page's own errors (a script that
  threw, a promise nobody caught, console.error), newest last. An empty or
  half-drawn view usually has its reason there.
- **lab_scene_place** takes a short item name when only one item matches it
  (`dzbar` → `fences/dzbar.ai`); otherwise it says which ones it might mean.

The tools:

| Area | Tools |
|---|---|
| Looking at the Lab | `lab_state`, `lab_screenshot` (the window as an image), `lab_read` |
| Moving around | `lab_tab`, `lab_click`, `lab_type`, `lab_action` |
| zoo.exe's functions | `lab_search_functions` (names, plus the game's text), `lab_open_function` (decompiled C, callers), `lab_where_used`, `lab_name_function` |
| Game files | `lab_game_files`, `lab_game_file` |
| Engine code | `lab_search_engine`, `lab_open_engine_file`, `lab_read_engine_file`, `lab_edit_engine_file`, `lab_save_engine`, `lab_problems`, `lab_engine_calls` (who calls a function, or what it calls) |
| Sync | `lab_sync_status`, `lab_sync` (pull, resolve, commit, push, propose, mirror, branch: only when asked) |
| Building and running | `lab_build_engine` (waits; returns ok or the problems), `lab_make_release` (waits; the zip, its sha256, what was left out), `lab_run_mod`, `lab_engine_log`, `lab_play` |
| The Scene | `lab_scene` (its state and what's selected), `lab_scene_act` (an action on what's selected), `lab_scene_pin` (hold a value), `lab_scene_route` (can a guest, staff or the selected animal get from here to there, and why not), `lab_scene_maps`, `lab_scene_load`, `lab_scene_outline`, `lab_scene_items`, `lab_scene_place` (a tool at a tile, or dragged), `lab_scene_select`, `lab_scene_set`, `lab_scene_remove`, `lab_scene_time`, `lab_scene_view` (also the art viewer: a species' animation, its layers, held on a frame), `lab_scene_console`, `lab_species_check` (a species, or every tank animal, in bulk) |
| Stepping | `lab_scene_trace` (what's traced, stop at each step, breakpoints), `lab_scene_step` (step, over, out, continue: returns where it stopped), `lab_scene_steps` |
| zoo.exe's own code | `lab_try_function` (run a function on what's selected: what it used, what's missing, a value for a missing offset) |
| The Lab itself | `lab_reload` (the page, after changing `renderer/`), `lab_restart` (the whole app, after changing `main.js` or `preload.js`) |

The Lab has to be running for the tools to work.

## The original game in Play

**Play original** runs the real `zoo.exe` inside the Play view, the way
OpenZT runs it: a small launcher starts it paused and loads our DLL into it
before its first instruction, and the DLL replaces a few of the Windows calls
it makes. Both are built by **Build engine** (32-bit, like zoo.exe, from the
`build/` tree: CMakeLists' part 12), from `src/original/`:

- `Launch.cpp` → `zt1-original.exe --zoo <zoo.exe> --embed <Lab window> --size <w>x<h> --log <file>`
- Original Game's search also takes `value:10282` (or `value:0x282a`): every instruction using that number as itself - a message id, a key's number - by function (re/lab/zoobin.py `value`); code Ghidra has no function for opens at the line asked for.
- `Hook.cpp` → `zt1-hook.dll`, which:
  - answers zoo.ini's `[user] fullscreen` as 0 and `screenwidth`/`screenheight` as the Play view's size, so the game takes its own windowed path (`GXVidMan::setupWindowed`, 0x52f1d0) at that size, and never writes those back: your zoo.ini keeps its own;
  - keeps its window borderless, owned by the Lab's and over the Play view (CreateWindowExA, SetWindowPos, SetWindowLongA, AdjustWindowRectEx), with the same stdin lines as our engine (`rect`, `show`, `hide`, `focus`, `quit`);
  - stops the Windows "DirectPlay" prompt: the game's DirectX version check loads `dpnhpast.dll` only to tell 8.1 from 8.0, and it's refused, exactly as when the prompt is skipped;
  - hooks its DirectDraw (made with CoCreateInstance then Initialize, 0x533d0a) to see the surface it draws on: `shot <file.bmp>`, which the Lab turns into a .png (`lab_play` with `shot`);
  - logs every setting read, window, DLL and COM object, and any crash with its zoo.exe address and callers, to `re/private/original-hook.log`.
  - the recorder (`arm <file>`, `disarm`: "hit" lines) and the Lab's mouse, below.

**Windows' own dialogs.** Saving and loading open Windows' file dialog
("Save a zoo..."), which isn't drawn on the game's surface, and the game waits
on it. `lab_action origDialog` names the dialog that's open, `origDialog "text
my-zoo"` fills in the file name, and `origDialog ok` / `origDialog cancel`
presses its button, and `origDialog "press Yes"` presses a button by its text
(Windows' "Confirm Save As" has Yes/No with no fixed ids). `origKey 27` sends a key (a virtual-key code: Esc).

**Recording from an AI agent** comes in three calls, checked between:
`recDo "<label>|<playSteps lines>"` starts it and does the steps, then you
look at a `shot`, then `recKeep` saves it (with the names its debug text
gives) or `recDrop` sets it aside in `recordings/discarded/`. For a long one,
leave the steps out and wait outside the call: a `lab_action` gives up after
about a minute.

**Volume.** The Play bar's slider sets the running game's own volume (either
game; Windows' mixer has it on its own: `volume <0-100>`, src/SessionVolume.hpp).

**The Lab's mouse.** `click`/`rclick`/`move <x> <y>` and `drag <x0> <y0> <x1> <y1>`
(the game's own pixels, as in a `shot`) click in the original without anyone's
mouse: the hook answers GetCursorPos, GetAsyncKeyState and GetKeyState for the
moment it takes, then hands the mouse back (`lab_action playCmd "click 952 462"`).

### Recording what runs (the original's recorder)

While the original runs, the Play bar has **Learn idle**, **Record** and
**Recordings**. The hook puts a one-time breakpoint (int3) on the first byte of
every function Ghidra knows in zoo.exe (~7,300); the first time one runs, its
byte goes back and the game carries on, so it's full speed once warmed up.

1. **Learn idle** (at the screen you'll record on: the main menu, or in a zoo):
   what runs with nothing being done is "always running" and left out after
   (`re/private/original-quiet.json`).
2. **Record**: name what you're about to do ("Placing a fence"), do just that,
   **Stop recording**.
3. **What ran**: each function that ran, in order, with what's known - its
   name (the knowledge base's, Ghidra's), our engine's code citing it ("in our
   engine"), the game's own debug text naming it (`GXVidMan::createSurface -
   ...`: ticked to name it so), the original's source file it's from (the
   developers' paths were left in: `snd/dx8sound.cpp`), the text it uses, where
   it was called from, and the other recordings it ran in. Click an address to
   read it in Original Game.
4. **Save to knowledge base**: every function in it gets "Ran when: ..." in
   `re/knowledge.json`'s new `seen` (kb.py `seen` / `seen-file`), and the ticked
   names (provisional, by "Lab recorder"); **ZT1_Apply** shows them in Ghidra -
   plate comments and `zt1-seen` bookmarks. The more that's recorded, the more
   functions run in only one thing: that's what they're for.
5. **Only what ran in no other recording** (a tick box over the list): leaves
   out everything another recording has too, so what's left is this action's
   own code. Placing a zebra outside an exhibit ran 136 functions, and 8 of them
   ran nowhere else: one chain (0x49ccc3 → 0x49cd46 → 0x49c6d7, the red
   "Animals can only be placed in exhibits" message, → 0x41176c → 0x411797).

A label that turns out wrong (the zebra was refused, not adopted): **Close**
without saving and record it again under the right name.

**Stop the game** ends it even inside a zoo, where the original asks whether
to quit rather than closing: whatever's still running 4 seconds on is ended,
with its launcher.

Recordings are kept in `re/private/recordings/` and matched again when opened
(what's been learnt since shows). The bridge's `/debugnames` and `/fnstrings`
give the debug names and each function's text in one go.

### The original's steps, live (the Scene's step log, for zoo.exe)

**Steps** in the Play bar opens a panel beside the original, the way the
Scene shows our engine's steps. While the game runs it streams what zoo.exe
is doing.

How it watches: the hook puts a one-time breakpoint on every function except
the idle ones, and the Lab re-arms the ones that ran every second (the
hook's `rearm`). So each function shows each time it runs, at most once a
second.

- **Known** functions read in words: their name, what they do, and our
  engine's code for them (click to open it).
- **Unknown** ones say "unknown", with a guess and its evidence, strongest
  first:
  - a specific known caller (*high*);
  - the text it uses, such as `ui/sharedui/question` (the "are you sure?"
    dialog) (*medium*);
  - "ran for nothing else recorded": that action's own (*medium*);
  - what the few actions it ran in share, such as selling, bulldozing and
    undo: the refund (*medium*);
  - the known functions it calls (*medium*);
  - **the same class slot named in a related class** (*high*). Name
    `ZTFoodType::isKindOf` at slot 0x1c, and every other type's slot 0x1c
    is guessed `isKindOf`. "Related" means the two tables share at least 30%
    of their slots;
  - a function called from many places, in the window or program-wide
    (Ghidra's `/fncallers`), is "a helper", never "part of" one caller;
  - a neighbour is only a clue when it's specific: not a helper, not code
    seen in many recordings, and not a method every type has (className,
    isKindOf - called through the class table, so Ghidra counts no callers,
    yet they run for everything), nor a getter or setter, nor anything in
    three or more class tables. Ghidra's own labels (`caseD_1`, `switchD_`,
    `LAB_`) aren't names. A slot's name only spreads from a function that's
    in that one slot (not `returns1`, which fills dozens);
  - an unknown caller's guess, inherited a step weaker;
  - the known code that ran either side of it (*low*).
- Live, each function shows **once**, with how often it ran (×12) and who
  called it. **Every call** shows them all in order.
- **General code** (input, drawing, sound: anything that ran in half the
  recordings or more) is hidden unless you ask for it.
- **❚❚ Pause game and log** presses the game's own pause button and holds
  the list still. **▶ Resume** starts both again. It checks the game's clock
  first, so it stays right even if you used the game's button yourself.
- **Click any line** to open its form:
  - **Name** (the guess's name to start with), **Class**, **What it does**
    (the guess to start with), and **How sure** (sure, likely, a guess; a
    guess stays provisional).
  - **Save** puts it in the knowledge base under your name. The Lab asks
    your name the first time.
  - A name that's still a guess (marked *guess*) gets **Confirm**, which
    keeps it as sure.
  - **Read its code** opens it in Original Game.
  - While a line is open, the list holds still too.
  - Whatever you save shows as known in the panel straight away. ZT1_Apply
    puts it in Ghidra, and the map in Explore picks it up when rebuilt.
- **Record** switches the panel to that recording: each function the first
  time it runs, with the guesses using the action's name. A recording's
  **Its steps** button replays it in the panel.

### The original's state, live (and finding where it keeps things)

While a zoo is open in the original, the Play bar reads its own state from its
memory every 2 s: the date, cash, its animals (with their average happiness)
and guests, and how many things are in the zoo. Click it for the **Census**:
every entity by class and species. What it reads (re/knowledge.json, found
with the tools below):

| | Where | |
|---|---|---|
| game manager | global `0x638048` | cash: float at +0xc (spent by `spendCash` 0x41ef68); the date: a SYSTEMTIME at +0x1194 (a real calendar from Mon 1 Jan 2001, a day each 12 s) |
| world | global `0x638040` | map size +0x34/+0x38, tiles +0x40, every entity in a vector at +0x80/+0x84 |
| an entity | | its class = its function table (+0): animals `0x62ff54`, guests `0x630f88`, fences `0x62d808`, tank walls `0x62dbc0`, scenery `0x62d950`, paths `0x62da88`, buildings `0x62e0b0`, ambient animals `0x62d6ec`; its type +0x128 |
| a type | | short name +0xa4 (elm, zebra, boy), kind +0x98 (foliage, building, zoowall) |

**Find a value** (Play bar) finds the next one, the way a cheat finder does:
scan for a value you can see (`float 75000`), change it in the game, rescan
for the new one; then **Watch writes** (a hardware watchpoint) reports each
instruction that writes it, in its function, with the registers - the object
it's written through. That's how the cash and `spendCash` were found. The
hook's lines: `scan`, `rescan`, `peek <addr> <n>`, `each <list> <offsets>`
(a vector of objects' fields at once), `strs <addr> <n>` (the text an object
points at: its name), `watch`/`unwatch`. Lab actions for them: `memScan`,
`memRescan`, `memPeek`, `memEach`, `memStrs`, `memWatch`, `memWrites`,
`origLive`, `origCensus`, and `playSteps` ("click 952 462; wait 1500; ...")
to get somewhere in the game without the mouse. Actions now give back what
they return (`lab_action`).

`poke <addr> <value>` (Lab action `memPoke "0xADDRESS value"`) writes a 32-bit
value into the original, to push it somewhere for a test: births are proved by
setting a species' cReproductionChance (type +0x278), cReproductionInterval
(+0x27c) and cHappyReproduceThreshold (+0x378) - on the male's subtype, the
type at entity +0x128 whose +0xa4 is "m" - then putting them back.
`origFrames "<folder> [count] [ms]"` films the original: a picture every
`ms` (default 500), saved f000.png, f001.png... to look through frame by
frame. Popups (the adoption ban, new items) pause the original; OK is at
about 712,500 at the Play view's size.

More for reading what the code refers to:

- `memText "<address> [bytes]"`: the text stored right at an address (a name
  the code compares against: `fWalk`, `fRun`). `memStrs` follows pointers
  instead.
- `memTables "<slots> <address>..."`: class function tables, each entry as an
  address, to see which slots a class replaces; `memTables "uniq ..."` gives
  each different entry once.
- `gameText "10287-10290 0x2972"`: the game's own text by number, from its
  language files (the engine's `texts` command). The numbers in zoo.exe's
  code - thoughts, messages, status lines - say what the code is for:
  10287 is "I've never seen such a lousy show!".
- `numberUsers "10028"` (or `0x75`): every function with that number in an
  instruction - a message ("%s is not happy." is used by animalUpdate), a
  task, a UI element. Ghidra's bridge (`/scalar`) builds the index the first
  time, in a few seconds. Small numbers also turn up as other things (the
  shows reuse 10026-10029 as event codes), so read the match.
- `uiElement "0x11cb 7000"`: which layout and element a UI number is
  (`infocom:ItemCost_Cost`). The code finds elements by number
  (`uiFind(0x11cb)`). The map is `re/private/ui_ids.json`, made from the
  game's layouts by `python re/lab/uiids.py` (it also looks numbers up).

And for naming many at once:

- `makeFns "0x52d94f 0x43eb4f ..."`: makes functions where Ghidra's analysis
  missed them (class-table entries usually), kept in
  `re/made_functions.json` so ZT1_Apply makes them in your project too.
- `namesSave`: a list of `address|name|summary|sure/likely/guess|by|class`
  lines (a first line `by: <who>` sets everyone's by). Paste a family read off
  one table: every handler a dispatcher calls by name.

The families named this way (2026-10-12) are the quickest way into the code:

| What | Where it starts |
|---|---|
| Behaviour functions (`f = fWalk(...)` in the .ai files) | `behaviourRunFn` 0x436065, `behaviourRunFnMoves` 0x437ccd: each name to its handler (`fMoveSlow`, `fTargetBuilding`, ...) |
| Unit tasks (going to a building, eating, chasing, the helicopter's landing) | `unitMakeTask` 0x413acd: `Task67_GoToBuilding`, `Task74_Chase`...; their shared `Task::tick` 0x43679e |
| Staff jobs (the status line's number: placing food, sweeping, fixing fences) | `unitMakeJob` 0x413a23: `Job2_PutDownFood`, `Job17_FixFence`... |
| UI handlers (every button, panel shown / hidden, tab let go) | `uiRegisterHandler` 0x512687 from `register_<layout>_handlers`; `infocom_up_Spinner`'s is `onPriceUp` |
| A unit's class slots | 0x10 handleEvents, 0x2c tick, 0x38 chooseTask, 0x48 startBehaviour, 0x64 debugText, 0x68 save, 0x6c load, 0x10c setup, 0x1d8 setTask, 0x24c habitat, 0x2a4 sellValue, 0x318 statusText |

### Side by side (the original and our engine, day by day)

`lab_action sideStart "<days> <label>"` records both games every game day from
the same moment: the original's first animal (happiness, +0x2a8) and its
exhibit (+0xe8: its animals' average happiness), and the Test Zoo's selected
animal (happiness, hunger, suitability); it starts the Test Zoo's clock too.
`sideStop` ends it, `sideShow` charts it (the last 20 runs are kept). Put the
same thing in both first - an exhibit and an animal in each - and start it as
the original's goes in. The first run (a lone zebra, bare 5x5 chain-link,
Basic Grass Map): both from 75, the same within the original's random 20-40
tick check for 16 days; then the original paused for its adoption ban, which
the Test Zoo didn't have - it does now (below).

**The original keeps running** in the Play view when the Lab is the window
in front (left to itself it pauses in the background, as a fullscreen game
does): the hook tells its window it's still active, and its
GetForegroundWindow answers its own window - 12 s a game day, measured.

Since then it also records guests: their count and average happiness in
both, the original's first guest's happiness and needs (+0x2a8, +0x2b0
hunger, +0x2b8 thirst, +0x2c0 energy, +0x2c8 bathroom) beside a guest
selected in the Test Zoo, and the original's zoo rating (its game manager's
+0x2c), which it gives the Test Zoo as it goes (`economy`: the lab zoo keeps
no books) so both get their guests by the same rating. What it found: the
original's trash check takes 5 from every guest each cTrashCheck unless it's
off to a trash can, holding litter or not (zoo.exe 0x43aedc) - now ours too.
Put both exhibits the same distance from paths: guests gawk only from them.

**Goals in the Test Zoo.** The lab zoo runs its map's goals as the game does
(Lab.cpp `setupGoals`, the same rules as main.cpp's): freeform's donation,
award and unlock scripts - the adoption ban when the animals' average
happiness is under 25 of 100, lifted over 45. A popup is a note in the log,
and `pause=1` pauses the zoo (and stops a time skip) as the game does; the
UI elements a goal disables are noted. Goals on cash or the zoo rating are
left out: the lab zoo keeps no books.

The Marine Mania disc image is mounted first if no drive has it (paths in
`re/private/original.json`: `{ "exe": ..., "iso": ... }`). The game is drawn
at the Play view's size when it starts; it isn't resized after.
