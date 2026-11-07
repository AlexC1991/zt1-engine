# Working on ZT1-Engine (read this first)

This file is for any AI agent or new contributor working in this repository.
It holds the owner's rules, how the project fits together, and how to build and
test. Code is the truth: if a doc and the code disagree, believe the code.

On the owner's PC, the local `.ai/` folder (gitignored) goes deeper: the
milestone task list, how the engine works, a map of every script, saves, the
Lab and its MCP tools, and reverse engineering. Start at `.ai/README.md`.

## What this project is

An OpenRCT2-style remake of Zoo Tycoon 1 (2001) with Marine Mania and Dinosaur
Digs. The original `zoo.exe` and its data files are the specification: the
engine copies what the original does, number for number, and then lifts its
2001-era limits (resolution, UI scale, memory) without changing the game.
Players need their own copy of Zoo Tycoon; none of its files are shipped.

## The owner's rules (non-negotiable)

1. **Never commit or push unless the owner asks for that push.** Leave changes
   uncommitted on `main` and say what changed.
2. **No AI credit anywhere:** no `Co-Authored-By` or "Generated with" lines, and
   no "the AI did this" wording in commits, PRs, issues, docs or release notes.
3. **Never read, print, copy or upload secrets:** release signing keys, the mod
   repository secret, tokens. `engine-build-resources/HANDOFF.md` (local only)
   lists where they are, so you can stay away from them.
4. **Never commit or redistribute the original game's files or art.**
5. **Copy the original; don't invent.** Every gameplay, UI or drawing rule says
   where it comes from, in a comment on the function:
   - `zoo.exe 0x…` - the original function it copies (the usual case)
   - `measured:` - how it was measured against the running original
   - `ours:` - engine plumbing with no original counterpart
   - `addition:` - something beyond the original, behind a switch (rule 6)
   The original's numbers live in `src/OriginalConstants.hpp` or are read from
   the game's own `.ai`/`.cfg` files, not typed in bare.
6. **Every addition is a switch.** Anything beyond the original is an on/off
   switch in `src/Features.hpp`, shown in Game Options. Off means exactly the
   original's behaviour. Never change original behaviour without a switch.
7. **Keep `engine-build-resources/NEXT_RELEASE_NOTES.md` up to date** as fixes
   land (it is local, gitignored, and becomes the next tag message). It is
   player-facing: plain words, What's new and Bug fixes only, no internals.
8. **Mute test runs:** set `SDL_AUDIODRIVER=dummy` for every engine run you
   start, and mute the original game when the Lab runs it.
9. **Test output and screenshots go to `E:\zt1-shots`**, never C:. C: is nearly
   full, and when it fills, builds and the engine crash at random. Check free
   space before blaming code.
10. **Don't leave processes running** that you started (engine, original game).
    The ZT1 Lab is the owner's; leave it open.

## How previous agents broke things (don't repeat it)

- **They rewrote working systems wholesale.** Make the smallest change that
  fixes the problem, in the style of the code around it. Don't reorganise,
  rename or "clean up" code you weren't asked to touch.
- **They guessed and called it a port.** Earlier code held invented formulas
  presented as the original's (refund rules, price caps, rounding). If you don't
  know what the original does, find out (see "Asking the original" below) or
  say so plainly. Never present a guess as verified.
- **They fixed the wrong bug.** When the owner reports something, reproduce it
  first and make sure you understand which object and which interaction they
  mean. If it's ambiguous, ask.
- **They reported success without checking.** Build it, run the matching check
  below, and for anything touching saves, prove the original game still loads
  the file. Report failures with the output; never claim "verified" for a step
  you skipped.
- **Mouse-feel changes** (dragging, hover, picking): set up the situation and
  ask the owner to try it. Scripted clicks don't judge feel.

## Layout

| Path | What it is |
|---|---|
| `src/main.cpp` | Startup, menus, the game loop, load/save glue, and the `--panel-shots` check modes |
| `src/World.*`, `WorldMap.*`, `WorldRenderer.*` | The map, its terrain and drawing |
| `src/Animals.*`, `Guests.*`, `Staff.*`, `Shows.*`, `Fences.*`, `PlacedObjects.*`, `Walkways.*` | The simulation's things |
| `src/ZooSim.*` | Money, books, rating, the calendar |
| `src/Goals.*`, `Research.*` | Scenario goals, awards, grants; research |
| `src/ZooReader.*`, `SaveGame.*` | Reading and writing the original's `.zoo` format; our saves |
| `src/OriginalConstants.hpp`, `Features.hpp` | The original's constants; the addition switches |
| `src/ui/` | The original's UI layouts (`.lyt`) and panels |
| `src/Lab.*`, `LabTrace.*` | The engine side of the ZT1 Lab (test zoo, traces, formula checks) |
| `src/original/` | The hook that runs the real `zoo.exe` windowed inside the Lab |
| `src/*Checks.cpp`, `EverythingZoo.cpp` | Self-checks run by the check modes |
| `lab-app/` | The ZT1 Lab (Electron): research, testing, mod making, release tools. `lab-app/README.md` |
| `lab-app/mcp/server.js` | The Lab's MCP server (`.mcp.json`), so an agent can drive the Lab |
| `re/` | Reverse engineering: `knowledge.json` (named functions/fields of `zoo.exe`), `kb.py`, `rd.py`, Ghidra scripts, `re/lab/` helpers. `re/README.md` |
| `engine-build-resources/` | Player setup scripts run by `start.bat`, licences, and (gitignored) the private release tooling |
| `docs/` | File-format references (`MAP_TILE_FORMAT.md`, `TERRAIN_TILETEX_CONFIG.md`), `MODDING.md`, `MARINE_SHOWS.md`, `ORIGINAL_SAVE_COMPATIBILITY.md` |
| `mods/` | Example mods |
| `vendor/` | Third-party libraries (SDL2 and friends, libzip, zlib). Don't edit |

## Building (Windows)

Needs Visual Studio 2022 Build Tools (C++), CMake 3.16+ (use
`C:\Program Files\CMake\bin\cmake.exe`; an older one may be first on PATH),
Python 3, and the owner's Zoo Tycoon install.

- **First time on a fresh clone:** `start.bat` finds the game, imports its
  files, builds the 32-bit engine into `build/Release/` and launches it.
- **Day to day, the 64-bit build** (what the Lab and releases use):
  ```
  "C:\Program Files\CMake\bin\cmake.exe" --build build-x64 --config Release --target zt1-engine
  ```
  A freshly configured `build-x64` must build all targets once first (the
  vendored SDL libraries), or linking fails with LNK1181.
- The Lab runs its own copy of the engine (`zt1-engine-lab.exe`) and refreshes
  it from `build-x64/Release/zt1-engine.exe` whenever that's newer, so building
  is enough. Restart the engine in the Lab to pick up a new build.

## Testing

All check modes run without a window you need to touch:

```
SDL_AUDIODRIVER=dummy ZT_FLAT_LAB=1 <mode>=1 build-x64/Release/zt1-engine.exe --panel-shots E:/zt1-shots/<topic> [map]
```

- No mode: the full panel walk-through. Every line of `<out>/timings.txt`
  should end in `ok`. Run it on the real map, **without** `ZT_FLAT_LAB`: its
  checks need the map's paths and its measured rating (on the flat lab, six
  of them fail by design).
- `ZT_FLAT_LAB=1`: makes the zoo flat grass (keep it on for the other checks).
- `ZT_FINANCE_CHECKS`: money rules, emergency grant, goal states in saves, the
  advisor's messages.
- `ZT_SHOW_CHECKS`, `ZT_SHOW_ALL`: Marine Mania shows.
- `ZT_ACTIVITY_CHECKS`: tanks and animal activity.
- `ZT_MODMENU_CHECK`: the Mod Menu.
- `ZT_TANK_LAB=<a>,<b>` (+ `_GAP`, `_ROT`, `_RAISE`): two test tanks.
- `ZT_VIEW_SAVE=<file>`: load a saved zoo, capture its views and log what's in it.
- `ZT_EVERYTHING_ZOO` (+ `ZT_EVERYTHING_PART=units`): a zoo with every item
  type, saved as a shared `.zoo`. **Save-format gate:** any change to the save
  writer must be followed by having the original game load these zoos (the
  Lab's `origCheckSave` action) before a release.

Write a check for what you fix where one of these modes fits, in the same style.

## Asking the original

- **Its data first:** the `.ai`, `.cfg`, `.scn`, `.lyt` files in the game's
  `.ztd` archives (zip files) hold most numbers. Resources resolve in zoo.ini's
  `[resource] path=` order; the first copy of a path wins.
- **Its code:** `re/knowledge.json` names what's been worked out. Look things up
  with `python re/kb.py show <address|name>`; record findings with
  `python re/kb.py --by <you> func|note|field ...`. `python re/rd.py <hex>`
  disassembles a function offline (with names). Ghidra, through the Lab, gives C.
- **The running original:** the Lab's Play Game > Play original runs `zoo.exe`
  windowed through our hook. It can load a save (`playOriginal <file.zoo>`),
  read and write its memory (`memPeek`, `memPoke`, `origLive`) and take pictures.
  Use it to prove behaviour and save compatibility. Ask the owner before driving
  it for exploratory research.

## Saves

A shared `.zoo` is the original's own format, readable by the 2001 game, with
the engine's full state appended after it (the original ignores the extra part).
`ZooReader::writeOriginal` rebuilds the original part from the current zoo over
a template, replacing only sections it understands. Learned the hard way:
the original checks some blocks exactly (for example, a scenario block in a zoo
of version 85 or before can't hold states of composite goals), and it rejects
the whole file as "corrupted" otherwise. See `docs/ORIGINAL_SAVE_COMPATIBILITY.md`.

## Releases

Only when the owner asks. The private steps are in
`engine-build-resources/HANDOFF.md` (local only).

## Shell traps on this PC

- In Git Bash heredocs, backslashes get mangled (`\\` becomes `\`, `\n` inside
  a Python string can become a real newline). For code with backslashes or
  regexes, write a script file and run it, or use an editor tool.
- Most source files use CRLF line endings; keep them as they are.
