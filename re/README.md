# Reverse engineering zoo.exe

The original game is the spec. Every rule the engine copies is read out of
zoo.exe, and its source says where it came from:

```cpp
// Hunger (zoo.exe 0x438a79): ...
```

This folder is where what we know about zoo.exe is kept: **the knowledge
base**, `knowledge.json`. Ghidra scripts read it, add to it, and send
questions on, so the work can be done, checked and kept by anyone, with or
without an AI helping.

| File | What it is | Committed |
|---|---|---|
| `lab/` | **ZT1 Lab**: the live view on our engine, its scripts and their interface (below) | yes |
| `knowledge.json` | **The knowledge base**: functions, struct fields, numbers, notes, questions and answers, and a log of who added what | yes |
| `engine_refs.csv` | Every zoo.exe address the engine's source cites (made by `scan_engine.py`) | yes |
| `ghidra_scripts/` | The Ghidra side (below) | yes |
| `kb.py` | The knowledge base from the command line | yes |
| `scan_engine.py` | Rebuilds `engine_refs.csv` from `src/` | yes |
| `check.py` | ZT1 Check: struct fields checked against zoo.exe's loader code, without the game (below) | yes |
| `rd.py` | Disassembles one function, its calls labelled with known names | yes |
| `msg.py` | The game's text: a message's id from its words, or the reverse | yes |
| `private/` | Other people's names, game text, your report, your name. **Never committed** | no |

## ZT1 Lab

ZT1 Lab is an editor in the style of Unity, built on our own engine, with
Ghidra as its window into zoo.exe. Start it with `zt1-engine.exe --lab`
(from `build\Release`), or from Ghidra with **Ctrl+Alt+L** (Show in Lab).

| Part of the window | What it shows |
|---|---|
| **Left: Scripts** | Your scripts, in `re/lab/scripts/`. Double-click one to build and run it. |
| **Left: Binary** | What's in the knowledge base (functions, structs, numbers), and every one of zoo.exe's 7,304 functions straight from Ghidra. Pick one to inspect it. |
| **Middle: the live view** | `scene`: the lab zoo, our engine's real World with a fenced exhibit and its animals (drag to pan, wheel to zoom). `heightmap`: the map's heights from above. `animal` / `sprite`: a species or an animation, drawn and playing. |
| **Right: Inspector** | The picked function or struct. What your script watches (each value labelled with the offset it maps to), its checks in green or red, its graphs, and the lab animals' state in the engine. |
| **Bottom tabs** | **Log** (your script's and the build's). **Decompiled** and **Assembly**, both from Ghidra; the decompiled view lists the mapped fields and known numbers it uses beside it. **Calls & uses**: what calls it and what it calls (click to go there), and the strings and numbers it uses. **Engine code**: our source that implements it, with Open in code. |
| **Toolbar** | **Build & Run**, **Step**, **Auto**/**Pause**, Stop, steps per second, Play world (the engine running freely), the view, and whether Ghidra is connected. |

**Ghidra as the backend.** With zoo.exe open in Ghidra, run Tools >
zt1-engine > **Start Lab bridge** (Show in Lab starts it too). The Lab then
asks Ghidra for code, calls and references on 127.0.0.1:18489, which is this
PC only. Everything it fetches is saved in `re/private/ghidra/`, so code
you've looked at still shows when Ghidra is closed. **Show in Ghidra** moves
Ghidra's cursor to what you're looking at in the Lab.

**Scripts.** A script is one small C++ file. **Build & Run** compiles it into
a DLL with Visual Studio. The first build of a session sets up the compiler
(about 30 s); after that a build takes about 1 s. The Lab then runs it:
`lab_start` once, then `lab_step` each step until it returns false. A crash in
a script stops the script, not the Lab. What a script can do is all in
[lab/lab_api.h](lab/lab_api.h):
- build an exhibit, put animals and objects in it
- run the engine for so many seconds
- read and set an animal's state
- read a species' `.ai` values, zoo.exe's own values and the game's text
- look up a mapped offset
- log, watch, plot, and `expect` (a check)

Here is the happiness script, cut down:

```cpp
#include "lab_api.h"
// zoo.exe 0x4378df          <- what it's about: Show in Lab and the Binary list find it
LAB_EXPORT void lab_start(Lab *lab) {
  lab->exhibit(5, 5, "chainlnk");
  zebra = lab->animal("zebra", false);
}
LAB_EXPORT bool lab_step(Lab *lab, int step) {
  double before = lab->get(zebra, "happiness"), pending = lab->get(zebra, "pending");
  lab->runEngine(1.0f);                                    // one tick of the engine
  double mine = clamp(before + pending, -100, 100);         // my reading of 0x4378df
  lab->expect("engine == my reading", lab->get(zebra, "happiness") == mine);
  lab->plot("happiness", lab->get(zebra, "happiness"));
  return step < 90;
}
```

To start one for any function, pick it in Binary and press **New script for
it**. That writes a skeleton and opens it in your editor.

The three that come with it:

| Script | What it checks |
|---|---|
| `happiness` | `0x4378df`: happiness += pending, kept to -100..100, every tick. It passes on all 90 ticks; a lone zebra drops from 75 to 20. |
| `hunger` | `0x438a79`: hunger rises by the species' `cHungerIncrement` every 30 ticks. On its first run this caught an engine bug: lions got 10, not their 5, because the loader read only the male `m/` section and the lion's values are under `f/`. Fixed. |
| `species_tour` | Each species drawn, with its `.ai` values and the offsets they're mapped at, then the map's heights. |

## What's in the knowledge base

| Section | Example |
|---|---|
| functions | `0x4378df`: `ZTAnimal::updateHappiness`, what it does, the engine code implementing it |
| structs | `ZTAnimalType` + `0x20c` = `cHungerThreshold` (int): an animal species' data |
| constants | `10560` = the message "This object must be placed in a deeper tank." |
| notes | `0x5cda56`: "the deeper-tank refusal" |
| questions | yours for your AI assistant, or leads it leaves for you, and their answers |
| log | 2026-10-10 Batty251: Field ZTAnimal+0x2a8 = happiness |

Everything in it says **who** added it (`by`) and **how sure** they were
(`sure`, `likely`, `guess`).

It started from the engine's earlier work, credited as
`zt1-engine (batty251, earlier sessions)`. That's 74 functions whose behaviour
is known and implemented, about 130 fields of the animal species data (from
its .ai loader), the tank and scenery offsets, and the known message numbers.

Those 74 functions have **provisional** names: we know what they do, but
not what the original programmers called them. A provisional name gives way
to a real one, either yours (rename it) or OpenZT's (import their names).

## The Ghidra scripts

They're under **Tools > zt1-engine**, once you've put them there. This is a
one-time step in each Ghidra (each user profile):

1. Open Window > Script Manager.
2. Click the **zt1-engine** category on the left.
3. Tick the **In Tool** box of each of the scripts.

Ghidra only gives a script its menu item and its key once that box is ticked.
If you change a script's key, press the Script Manager's Refresh button (the
green arrows).

**Ctrl+Alt+M**, not Ctrl+Shift+M: another program on this PC has
Ctrl+Shift+M as a global hotkey (as Teams and Discord do, for mute), so
Ghidra never gets it.

| Script | What it does |
|---|---|
| **ZT1_ShowInLab** (`Ctrl+Alt+L`) | Sends the function under the cursor to the ZT1 Lab, starting the Lab and the bridge if needed. |
| **ZT1_LabBridge** | Starts (or, run again, stops) the bridge the Lab reads Ghidra through. |
| **ZT1_Check** | Opens ZT1 Check: struct fields against the loader code. |
| **ZT1_Map** (`Ctrl+Alt+M`) | Records what's under the cursor, in the listing or the decompiler, and puts it in Ghidra at once. See below. |
| **ZT1_Apply** | Puts the whole knowledge base into Ghidra: names, plate comments, structs (Data Type Manager > zoo.exe > zt1), notes, questions as bookmarks, and optionally each known number marked where it's used. Run it after pulling the repo, or after your assistant answers you. |
| **ZT1_Sync** | Brings in what you did straight in Ghidra instead of through Map: renames (`L`), plate comments, signatures, fields edited in the /zt1 structs. |
| **ZT1_Report** | Writes `re/private/report.md` for your AI assistant (or anyone helping). |
| **ZT1_EngineRefs** | Bookmarks every address the engine cites (category zt1-engine) and tags those functions `zt1:implemented`. Run `python re/scan_engine.py` first. |
| **ZT1_ImportNames** | Names functions from someone else's CSV, e.g. OpenZT's, tagged `src:openzt`. |

### Map this (Ctrl+Alt+M)

Put the cursor on something and press `Ctrl+Alt+M`, then pick one:

- **Name this function**: its name, its class (e.g. `ZTAnimal`), what it does,
  and how sure you are. It becomes yours (`by:` you), even if it had a
  provisional name.
- **A struct field**: put the cursor on an offset (`param_1 + 0x2a8`, on the
  `0x2a8`). Pick the struct, name the field and give its type. It goes into
  the /zt1 struct in Ghidra at once.

  Then click the variable holding that kind of thing (`param_1`), press
  `Ctrl+L` and type e.g. `ZTAnimal *`. The decompiler then shows
  `param_1->happiness` in place of `*(int *)(param_1 + 0x2a8)`, in every
  function you do that in. This is how the C gets readable.
- **What a number means**: put the cursor on a number (`10560`) and say what it
  is. `python re/msg.py 10560` gives a message's text.
- **A note here**: anything worth remembering about this spot.
- **Ask your AI assistant about this**: your question, kept with where you asked it.
- **Answer a question or lead here**: answer one of the leads left for you, or close
  one of your own questions.
- **Show what's known here**: everything about this function.

## ZT1 Check: is what's mapped right?

To open it, use Tools > zt1-engine > Open checker, or run
`python re/check.py`. It has three tabs:

- **Loader check** (no game needed). It checks every struct field named for a
  config key (`cHungerThreshold`...) against zoo.exe's own loading code. The
  loader takes the field's address (`lea ecx, [ebp + 0x20c]`) just before it
  pushes the key's name, so the code itself says where each value goes.
  - **right**: the mapped offset is where the code stores it.
  - **WRONG**: the code stores it somewhere else, and it shows where.
  - **can't tell**: the value goes into a temporary first, so the code
    doesn't settle it.

  On 10 Oct 2026: 123 right, 0 wrong, 6 can't tell.
- **Species data**: a species' values, straight from its .ai (its default
  subtype, with `f`/`y` over it), at each mapped offset.
- **Live** reads the original game's memory while it runs. You don't need it:
  the ZT1 Lab is the place to see what's mapped working. It's there if you
  ever want to compare against the 2001 game itself.

## Working with an AI assistant (or anyone)

1. When you're stuck, use **Map > Ask your AI assistant about this** on the spot.
2. Run **ZT1_Report**. It writes `re/private/report.md`, with each open
   question, its function's decompiled C and what's known about it, plus
   everything added since the last report.
3. Give your assistant the report, or just say "read the report" (it's in
   the repo folder). With the Lab's MCP server it can read the original's
   code itself.
4. It answers in the knowledge base (`kb.py answer`), may leave you leads
   (`kb.py lead`), and may add what it works out, under your name.
5. Run **ZT1_Apply**. Answers show as bookmarks (Window > Bookmarks, category
   zt1-answer), and leads as zt1-lead.

## The loop (how a rule gets from zoo.exe into the engine)

1. **Pick something**: a lead (Bookmarks > zt1-lead), something the engine
   doesn't do like the original yet, or `re/private/todo.txt` (named functions
   no engine code cites yet, from ZT1_EngineRefs).
2. **Find it.** You can start from:
   - a config key: Search > Program Text > `cKeeperFoodType`
   - a message: `python re/msg.py deeper tank` gives 10560; then
     Search > For Scalars > 10560
   - a struct field: once a variable is typed (`ZTAnimalType *`), right-click
     a field in the decompiler, e.g. `cHungerThreshold`, and choose Find Uses
     of. That lists every function reading it, as long as the variables
     there are typed too.
3. **Read the decompiler.** Name variables (`L`), retype them (`Ctrl+L`), and
   map the offsets and numbers you work out with `Ctrl+Alt+M`.
4. **Name the function** (Map > Name this function).
5. **Implement it** in `src/` and cite it: `// Hunger (zoo.exe 0x438a79): ...`.
   Any 6-digit address after `0x` in a `//` comment is picked up.
6. **Test it.** Add a check to the panel-shots suite in `src/main.cpp`, run
   `zt1-engine-dev.exe --panel-shots E:/zt1-shots/shots`, then look at
   `timings.txt`.
7. **Record it.** Run `python re/scan_engine.py`, then in Ghidra run
   **ZT1_EngineRefs**. If you renamed anything with `L`, also run
   **ZT1_Sync**.
8. **Commit** `src/`, `re/knowledge.json` and `re/engine_refs.csv` together.

Save the Ghidra project as you go (`Ctrl+S`), and back up `E:\zt1-ghidra`
now and then. The knowledge base is the part that matters, though: with it
and `ZT1_Apply`, a fresh project gets everything back.

## Doing it alone (a playbook)

Everything below works without an AI assistant. Start in the Lab's **Explore** tab:
ask where something is in plain words, and see which recorded actions our
engine does, partly does, or doesn't do yet (lab-app/README.md, Explore). Each step's results go into
`re/knowledge.json` under your name; run **ZT1_Apply** in Ghidra afterwards to
see them there.

1. **What the game reads.** Original Game › **Grow the database** › Config
   loaders. Open a loader with no struct name yet and read its keys. They say
   what it fills (`cCleanTrashRadius`, `cFixFenceModifier` → maintenance
   workers). Look at the offsets too: a type's own fields start past its
   base's. ZTEntityType has `cIconZoom` at +0x50, ZTUnitType has `cSlowRate`
   at +0x100, and an animal or guest type's own keys start around +0x1b4.
   Type a struct name and press **Save**. A loader that reads only globals
   takes a plain name (`AnimalSettings`).
   - A loader may read a key and then store it by hand
     (`cSlowAnimSpeed` into a local, then copied). Its row shows "a local":
     read the raw code and add the field with `kb.py field`.
   - A key whose reader isn't listed won't show at all. If a loader's C calls
     another function with ("file", "cKey", this + 0x..), add that function to
     `re/lab/loaders.json`'s readers. That's how the float reader 0x4b37c3
     was found (cPctSick, the admission prices).
2. **Values no key reads.** In Find, search `field:0x22c`. Look for a write
   of a plain number beside the loaders (`mov [ebp+0x22c], 0xffffff9c`). That's
   the type's defaults, set before its file is read. Then see what reads the
   field (animalUpdateHappiness clamps to it), and name it with `kb.py field`.
3. **Classes.** Grow the database › Class tables. Name a table from what the
   live census (Play › the bar's readout) says its objects are, and press
   **Save**. Put things in the zoo first (staff, buildings, an exhibit and
   animals) so the census has them. Use `kbClassCompare` for tables no
   object uses: a type's config loader at slot 0x60 names it, and a base
   class shares most of its slots with its whole family. When two tables
   look the same (both have the staff loader), leave them unnamed rather
   than guess. Slots shared with a named class come from their common base class.
   A function's slot often names it: slot 0x30 is "apply pending happiness"
   for both animals and guests.
4. **The game's text.** Grow the database › The game's text › **Save all**.
   Do it again whenever Ghidra has new functions.
5. **Record.** Play › Play original. Use the checklist below.
6. **Close the gaps.** `kbOrphans` lists the engine's zoo.exe references
   that have no Ghidra function. Open each one's raw code first. If something
   that ends code (`ret`, `jmp` away, padding) comes right before it, and it
   starts like a function (`sub esp`, pushes, `mov eax, [esp+4]`), press
   **Make it a function**. If it jumps back into code before it, it's a piece
   of a bigger function. If it disassembles into nonsense, it's a table. Leave
   both alone.
7. **Questions.** Run `python re/kb.py questions --all`. A lead "for user" is
   yours to chase. Answer it with `kb.py answer`, saying how you found it.
8. **OpenZT's names**, when crusavor sends them (below). The import path was
   tested on a scratch copy of the project (2026-10-12). It named functions
   that had no name and replaced provisional names, recording each in the
   knowledge base as `name from openzt`. It kept a name already set, and listed
   an address that isn't code. A ZT1_Apply afterwards didn't put the old
   provisional name back.

After **ZT1_Apply** (or naming things) in your own Ghidra, press **Refresh the
Lab's Ghidra** in Grow the database, so the Lab shows the same.

To test a script without touching your project, copy `E:\zt1-ghidra`
(zt1.gpr and zt1.rep) and `re/` (ghidra_scripts, knowledge.json,
made_functions.json) into a scratch folder, and run it headless there.
Each `ask` takes the next script argument:

```bash
analyzeHeadless.bat <scratch>\project zt1 -process zoo.exe -noanalysis -scriptPath <scratch>\repo\re\ghidra_scripts -postScript ZT1_Apply.py false -postScript ZT1_ImportNames.py <csv> openzt false
```

### Recording checklist

1. Play original and get to the screen you'll record on: the main menu, or a
   zoo. The game takes the Play view's size, so click by what a `shot` shows.
2. Press **Learn idle** there, and leave the game alone for the 10 seconds.
3. Record one action at a time, named as it'll be read later. Say what
   happened, including when the game refused: "Placing a zebra outside an
   exhibit (refused: Animals can only be placed in exhibits)".
4. Before you save, check the game did what the label says (take a shot). If
   it didn't, press **Close**, and record it again under the right name.
5. **Save to knowledge base**, then tick **Only what ran in no other
   recording**: that's the action's own code. Put a `kb.py note` on the best
   lead.
6. Record pairs that differ by one thing: opening a panel against using it,
   placing that works against placing that's refused, one animal against
   another. Each pair narrows the other.
7. Watch the date while you record. If a month ends during a recording,
   wages and the month's reports run too: discard it and record again.
8. Already recorded, 57 actions (re/private/recordings/):
   - **Menus and files:** main menu → Freeform, starting a zoo, saving
     (60 functions of its own), loading a save (112), Options;
   - **Panels:** Construction, Hire Staff, Adopt Animal, Research and
     Conservation, Scenario information, the message list, the Animal and
     Exhibit/Show lists, Zoo Status with all six of its tabs (raising the
     admission price and marketing too), an animal's info, an exhibit's info
     and donations tabs, Modify tank;
   - **Building:** a path, a tree, a rock, painting terrain, raising land,
     fences (inside the zoo, and refused outside the wall), a dinosaur
     paddock, a tank (one wall, and closing it: the tank forms and fills, 142
     of its own), a tank toy, the show grandstand, buildings, raising a tank's
     wall and lowering its base, deleting (bulldozing) an object;
   - **Animals and staff:** a zebra into an exhibit and refused outside one,
     a porpoise into a tank and refused on land, an allosaurus into a paddock
     (it arrives as an egg), hiring a keeper, marine specialist and dinosaur
     scientist, a DRT base;
   - **Running:** the zoo running a minute, the adoption-ban popup's OK, an
     escaped dinosaur loose (it broke its paddock), the DRT base while it's
     loose, a month ending (exhibitBooksMonthly), pausing, and a show object
     refused in a shallow tank ("This object must be placed in a deeper
     tank").
9. Added since (82 actions in all):
   - **Shows (from Marine Mania Tutorial 2):** a show animal into its exhibit
     tank, building its **show tank** beside it (show tank walls: the gate
     forms), grandstands facing it, opening the Show Script, adding tricks,
     changing the show frequency, the orca ball in the show tank (it unlocks
     Play with Ball), funding trick research;
   - **Combined exhibits (Tutorial 3):** closing a land exhibit beside a tank;
   - **Guests:** the Guest List, a guest's info, thoughts and favourites
     tabs, guests buying from vending machines (22 of its own), raising a
     building's price (priceAdjustB);
   - **Running:** two and a half minutes with idle learnt while running
     (keeperPickFoodSpot), draining a tank;
   - **Scenarios:** the scenario list, starting a scenario (123 of its own),
     a tutorial popup's OK, a goal completing, going on to the next tutorial,
     undo (undoLastAction).
10. Rules learnt the hard way:
    - **Show objects only go in a show tank**, built with the show tank walls
      (blue, starred) beside the animal's own exhibit tank, at the same height.
      In a plain tank they're refused as "must be placed in a deeper tank",
      however deep it is.
    - **A combined land and tank exhibit forms when the tank wall's top is
      level with the land.** The shared wall turns grey-and-white striped.
    - A tutorial blocks clicks on anything but its current step.
    - **The show tank comes first.** Built alone it's named "Show Tank"; an
      exhibit tank then built against it gets the gate between them. Built
      the other way round, the second one is just another tank.
    - **A show needs a marine specialist assigned** (Staff Information's
      assign button, then click the animal's tank): the show tank's info says
      "Staff Assigned: No" until then. Marine specialists can only be
      assigned to tanks with marine animals.
    - **Trick depth:** each trick has a minimumdepth (orca.ai: Play with Ball
      8, Wave to Crowd 2). That's where "This object must be placed in a
      deeper tank" comes from (loadTricks, 0x4b9690).
    - An escaped animal can't be sold (its Sell button greys out).
    - Shows run every shows.cfg interval: continuous 65, frequent 90,
      infrequent 120.
11. Also recorded: building a show tank alone and an exhibit tank against
    it, selling an animal ("Refund: $320", 40%), assigning a marine
    specialist to a tank, and a trainer waiting at a show platform for an
    unhappy lone orca (no show came).
12. Next to record: a show being performed. It needs a happy show animal:
    an orca wants 2-3 of its kind, and that zoo (Saved Games\lab-show-zoo)
    is under the adoption ban. Start a zoo, adopt two orcas at once, assign
    the specialist, and record. Also dung, rubble (a rampage), and a
    scenario won or lost.

## Whose work is whose

- Everything you add is under your name: Ghidra asks for it once and keeps
  it in `re/private/me.txt`. The log says when.
- The starting set says `zt1-engine (batty251, earlier sessions)`.
- **OpenZT's names** come in tagged `src:openzt`. When one replaces a
  provisional name, the knowledge base keeps the behaviour (ours) and notes
  the name is theirs (`name from openzt`). Their full list only goes to
  `re/private/`. Don't publish it in this repo unless crusavor says that's
  fine, and credit OpenZT when it helps.
- Report's "Who's done what" section counts each person's functions, fields
  and numbers.

## Getting OpenZT's names

You can get them two ways:

- **Through crusavor's Ghidra server** (they offered an account). Choose
  File > New Project > Shared, enter their server, and open their zoo.exe.
  Check it's the same build: the scripts warn you if it isn't. Run
  **ZT1_Sync** there and say **yes** to "Everything too?". That writes
  `re/private/all_names.csv`. Back in your own project, run
  **ZT1_ImportNames** on that file and label it `openzt`. Leave their project
  as it is unless they ask you to add to it.
- **From a CSV they send you** (their `function_exporter.py` writes
  `Class, Name, Address, ...`). Put it in `re/private/`, then run
  **ZT1_ImportNames** on it.

Either way, ImportNames never overwrites a name you set yourself unless you
say so. It only replaces provisional names.

## Without Ghidra open

```bash
python re/kb.py stats
```

The `kb.py` commands:

| Command | What it does |
|---|---|
| `python re/kb.py stats` | Counts of everything in the knowledge base |
| `python re/kb.py show 0x4378df` | Everything known about a function |
| `python re/kb.py show ZTAnimalType` | A struct's fields |
| `python re/kb.py show 10560` | What a number means |
| `python re/kb.py show hunger` | Searches names and fields |
| `python re/kb.py questions` | Open questions and leads |
| `python re/kb.py field ZTAnimal 0x2b0 energy int "Its energy"` | Adds a field |
| `python re/kb.py log` | Recent additions, with who and when |

`python re/kb.py` with no arguments lists every command.

The other tools:

| Command | What it does |
|---|---|
| `python re/rd.py 4df688` | One function's disassembly, calls labelled with known names |
| `python re/msg.py deeper tank` | A message's id from its words, or the text of an id |

Ghidra can also run the scripts with no window (headless). A script's
questions are answered by the arguments after its name, in order, and
yes/no questions take `true` or `false`. Close the project in the Ghidra
window first. For example:

```bash
E:/ghidra/ghidra_12.1.4_PUBLIC/support/analyzeHeadless.bat E:/zt1-ghidra zt1 -process zoo.exe -noanalysis -postScript ZT1_Apply.py false
```

## One-time setup

On this PC, all of this was done on 10 Oct 2026:

- Ghidra 12.1.4 is in `E:\ghidra\ghidra_12.1.4_PUBLIC`. Start it with
  `ghidraRun.bat` there.
- The project is `E:\zt1-ghidra\zt1.gpr`, with zoo.exe analysed.

These steps are for a new PC:

1. **Ghidra.** Download the latest release from
   https://github.com/NationalSecurityAgency/ghidra/releases and unzip it on
   E: (C: is nearly full). It needs a 64-bit JDK 21 or newer; Temurin 25
   works. Ghidra 12 no longer has Jython built in, and the scripts need it.
   To install it, unzip `Extensions\Ghidra\ghidra_*_Jython.zip` into
   `Ghidra\Extensions\` (make that folder).
2. **Project.** File > New Project > Non-Shared, in `E:\zt1-ghidra`.
3. **Import zoo.exe.** Use the 2002 Complete Collection build
   (2,449,432 bytes). The scripts check its SHA-256, because other builds'
   addresses don't match. Say yes to Analyze, and keep the defaults.
4. **Scripts.** Ghidra reads scripts from `%USERPROFILE%\ghidra_scripts`. On
   this PC that's a link to `<repo>\re\ghidra_scripts`, made with
   `mklink /J %USERPROFILE%\ghidra_scripts <repo>\re\ghidra_scripts`.
   You can instead add the folder in Script Manager > Manage Script
   Directories.
5. **Put the scripts in the menu.** In Window > Script Manager > zt1-engine,
   tick **In Tool** for each.
6. **Fill it in.** Run `python re/scan_engine.py`, then in Ghidra run
   **ZT1_EngineRefs** and **ZT1_Apply**.
7. **Python tools.** Run `pip install pefile capstone`. Copy
   `E:\zt1-shots\tools\strings.pkl` into `re\private\`, then run
   `python re/msg.py 1` once, so Map can fill in a message's text.
