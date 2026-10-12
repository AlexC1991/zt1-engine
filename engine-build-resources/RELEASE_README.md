# ZT1-Engine — Alpha Build

**An open-source engine for Zoo Tycoon 1, for 64-bit Windows.**

> ### ⚠️ This is a taste of what's being made, not a finished game
> This alpha is an early look at work in progress. **Please don't judge it as a finished product,** and especially not by how much is available right now. A lot is missing, some things are rough, and some things will break. It's here so you can try it, see where it's heading, and help by reporting bugs.
>
> **Back up any saves you care about.**

### 📅 New alpha every week

**A new alpha build will be released every week.** Each one brings fixes and more of the game. Check back for the latest.

> **You need your own copy of Zoo Tycoon.** This download has the engine only, with **no** Zoo Tycoon art, sound, maps or game files. The first time you run it, it imports them from your own install.

---

## Which Zoo Tycoon do I need?

**The base Zoo Tycoon game is all you need.** You do **not** need the Complete Collection.

- **Base Zoo Tycoon only:** works. This alpha is built around the base game, and everything that's ready uses only base-game content. (Tested with just the base game: setup and play work.)
- **Complete Collection (with Dinosaur Digs and Marine Mania):** also works, and setup imports the expansion files too. But expansion content **isn't ready in this alpha** (see below), so you don't gain much from it yet.

If you only have the base game, setup shows a warning that Dinosaur Digs and Marine Mania content is missing. **That's expected and safe to ignore.**

---

## Requirements

| | |
|---|---|
| **Windows** | 10 or 11, 64-bit |
| **Zoo Tycoon 1** | The **base game** is enough (installed or extracted). The Complete Collection works too, but isn't needed |
| **Python 3** | Used by `start.bat` each time it runs: the first time it imports your game files, after that it checks the setup before the game starts. [python.org/downloads](https://www.python.org/downloads/). **Tick "Add python.exe to PATH"** when you install it |
| **Visual C++ Redistributable 2015–2022 (x64)** | Most PCs already have it. If you get an error about `MSVCP140.dll` or `VCRUNTIME140.dll`, install it from [aka.ms/vs/17/release/vc_redist.x64.exe](https://aka.ms/vs/17/release/vc_redist.x64.exe) |
| **Disk space** | About **0.5 GB** after setup with the base game, about **1.1 GB** with the Complete Collection (the imported game files) |

---

## Installing and playing

1. **Unzip** the folder anywhere you like, such as your Desktop or `Documents`. Avoid `Program Files`, because Windows blocks writing there.
2. Double-click **`start.bat`**.
3. **First run only:** it checks the usual install folders (`Program Files (x86)\Microsoft Games\Zoo Tycoon` and similar). If your game isn't in one of them, a window asks you to pick the folder that contains **`animals.ztd`** and **`zoo.exe`**. The folder is remembered.
4. It imports the game files and sets everything up (about a minute), then **the game starts**.

**After the first run,** double-click `start.bat` again to play. You can also run the game directly with
`build\Release\zt1-engine.exe`. If you make a shortcut to it, keep **"Start in"** set to that `build\Release` folder.

### Where your files go

- **Saved zoos:** `Documents\zt1-engine\Saved Games`
- **Snapshots:** `Documents\zt1-engine\screenshots`
- **Scenario progress:** the `saves` folder in the unzipped folder

---

## What you can try in this alpha

This alpha covers the **base Zoo Tycoon game's freeform mode**: its **animals and items**. **If you find a bug in any of it, please report it** (see [Reporting bugs](#reporting-bugs)).

- **Menus:** loading screen, main menu, freeform map selection, credits, the in-game HUD, panels, tooltips and messages
- **Base-game animals:** adopting and selling, eating, drinking, sleeping, swimming, happiness and exhibit suitability, breeding, illness and old age
- **Base-game items:** terraforming (raise, lower, smooth, paint), paths, fences and gates, exhibits, scenery, foliage, rocks, shelters and toys, the bulldozer and undo
- **Escapes and predators:** animals jump, climb or break out; loose meat-eaters chase guests, and keepers dart and crate them
- **Guests:** needs, tours, donations, thoughts, litter, and running from escaped animals
- **Staff:** zookeepers, maintenance workers and tour guides; hiring, firing and duties
- **Running the zoo:** zoo rating, admissions, finances, graphs, research and conservation
- **Saving and loading:** Save Game, Load Game and Continue Saved Game
- **Sound:** music, ambience, crowds, guests and animals

**Camera:** arrow keys or `W` `A` `S` `D` move · `[` / `]` zoom · `Q` / `E` rotate the view.

---

## Known bugs and what's not ready

### 🐞 Buildings: placement is broken (known bug)

**Buildings** (shops, restaurants, restrooms and other guest buildings) are in the game, but **placing them is broken in this alpha.** They may go in the wrong spot or not place properly, and the behaviour **isn't the same from one PC to another.** It's a known bug being worked on, so **you don't need to report it.** Please **avoid relying on buildings** for now. A fix is planned for an upcoming weekly build.

### Not in this alpha yet

- **Dinosaur Digs and Marine Mania:** expansion animals, tanks, shows and dinosaurs are still being built. Their content may show up in menus and buy panels, but **it isn't ready to play.**
- **Scenarios:** they open, but their goals and events aren't implemented yet. Stick to **freeform**.
- **Original `.zoo` saves:** can't be loaded yet. The engine uses its own save format, so saves from the original game won't open.
- **Linux and macOS:** Windows only for now; other platforms are planned.

---

## Troubleshooting

**"Python was not found"**
Install Python 3 from python.org, tick **"Add python.exe to PATH"**, then run `start.bat` again.

**`MSVCP140.dll` or `VCRUNTIME140.dll` is missing**
Install the [Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

**Setup warns that Dinosaur Digs / Marine Mania content is missing**
That's normal if you only have the base game. Ignore it; this alpha doesn't need the expansions.

**It used the wrong Zoo Tycoon folder, or I want to import from a different one**
The files are only imported once, so start again from scratch. Open a Command Prompt in the unzipped folder and run:
```
start.bat --clean
set ZT1_SOURCE_DIR=D:\Games\Zoo Tycoon
start.bat
```
Replace the path with the folder that has `animals.ztd` and `zoo.exe` in it.

**Sound is slow, scratchy or muffled**
The engine uses DirectSound by default, because Windows' newer WASAPI audio sounded wrong on some PCs. To try a different audio driver, open a Command Prompt in the `build\Release` folder and run:
```
set SDL_AUDIODRIVER=wasapi
zt1-engine.exe
```
This only affects that one launch. It doesn't change anything else on your PC.

**Something went wrong during setup**
Run `start.bat --clean`, then `start.bat` again for a fresh setup. `--clean` removes the imported game files (they're imported again on the next run) but keeps the engine. Your saved zoos in `Documents` are not touched.

### `start.bat` options

| Command | What it does |
|---|---|
| `start.bat` | Set up (first time) and play |
| `start.bat --setup` | Only import the game files, then stop |
| `start.bat --clean` | Remove the imported game files and settings for a fresh setup (keeps the engine) |

To import from a particular folder, `set ZT1_SOURCE_DIR=<folder>` before running `start.bat`.

---

## Reporting bugs

This alpha exists to find problems, so **if you find a bug, please report it.** Every report helps the weekly builds get better. **The most useful report shows where the engine differs from the original game:**

1. What you did
2. What the original Zoo Tycoon does
3. What the engine did instead

Add a **screenshot** or your **saved zoo** (from `Documents\zt1-engine\Saved Games`) if you can. Saying which **alpha build** (the date in the folder and zip name) you were playing helps too.

Report issues at **https://github.com/AlexC1991/zt1-engine/issues**.

*Building placement is already known (see [Known bugs](#known-bugs-and-whats-not-ready)), so there's no need to report it.*

---

## Legal and credits

**Zoo Tycoon** is © Microsoft Corporation, developed by Blue Fang Games. This project is **not affiliated with or endorsed by Microsoft or Blue Fang Games**. It ships **none** of the original game's files. You need your own legitimately obtained copy.

**ZT1-Engine** is free and open source under the **MIT License** (see `LICENSE.txt`), © 2025 Wouter (sharkwouter) Wijsman and contributors.
Source code: https://github.com/AlexC1991/zt1-engine

It uses these open-source libraries: SDL2, SDL2_image, SDL2_mixer, SDL2_ttf, FreeType, libzip, zlib, pe-resource-loader and Dear ImGui (MIT License, © Omar Cornut). Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.

The icon uses [a photo by Magda Ehlers from Pexels](https://www.pexels.com/photo/zebra-s-eye-760958/).

**This software is provided "as is", without warranty of any kind. It's an alpha: use it at your own risk.**
