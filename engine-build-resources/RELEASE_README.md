# ZT1-Engine — Alpha Build

**An open-source engine for Zoo Tycoon 1, for 64-bit Windows.**

> ### ⚠️ This is a taste of what's being made, not a finished game
> This alpha is an early look at work in progress. **Please don't judge it as a finished product,** and especially not by how much is available right now. A lot is missing, some things are rough, and some things will break. It's here so you can try it, see where it's heading, and help by reporting bugs.
>
> **Back up any saves you care about.**

### 📅 New alpha every week

**A new alpha build will be released every week.** Each one brings fixes and more of the game. Check back for the latest.


> **You need your own copy of Zoo Tycoon.** ZT1-Engine comes with **no** Zoo Tycoon art, sound, maps or game files: it plays the copy you have installed, reading it where it is (it never changes your game's folder).

---

## Which Zoo Tycoon do I need?

**The base Zoo Tycoon game is all you need.** You do **not** need the Complete Collection.

- **Base Zoo Tycoon only:** works. This alpha is built around the base game, and everything that's ready uses only base-game content.
- **Complete Collection (with Dinosaur Digs and Marine Mania):** also works, but expansion content **isn't ready in this alpha** (see below), so you don't gain much from it yet.

---

## Requirements

| | |
|---|---|
| **Windows** | 10 or 11, 64-bit |
| **Zoo Tycoon 1** | Installed (the base game is enough), or copied into a folder of its own |
| **Disk space** | About **20 MB**: the engine reads the game from where it's installed, so nothing is copied |

---

## Installing and playing

1. Run **`ZT1-Engine-Setup-<version>.exe`**. It installs for you alone, with no admin prompt.
2. Start **ZT1-Engine** from the Start menu (or the desktop, if you asked for a shortcut).
3. **First run only:** it looks for Zoo Tycoon where its installer put it. If it can't find it (say, the game was copied to another drive), it asks you to pick the folder that has **`animals.ztd`** and **`zoo.exe`** in it, and remembers it.

That's it: no setup scripts, nothing else to install.

### Updates

When a new alpha is out, ZT1-Engine offers it at the main menu: **Update now** downloads it, closes the game, installs it and starts it again. Your saved zoos, mods and settings are kept. You can also choose **Later**, or **Skip this version**.

### Where your files go

- **Saved zoos:** `Documents\zt1-engine\Saved Games`
- **Snapshots:** `Documents\zt1-engine\screenshots`
- **Settings, mods and scenario progress:** `%LocalAppData%\zt1-engine`
- **The engine itself:** `%LocalAppData%\Programs\ZT1-Engine` (uninstall it from Windows' Settings › Apps; your saves and settings stay)

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
- **Linux and macOS:** this engine is Windows only. For Linux, see [sharkwouter's zt1-engine](https://github.com/sharkwouter/zt1-engine), which this project grew from.

---


## Troubleshooting

**It can't find Zoo Tycoon, or it uses the wrong copy**
It remembers the folder in `%LocalAppData%\zt1-engine\game-folder.txt`. Delete that file and start ZT1-Engine again: it looks again, and asks if it has to.

**Windows says "Windows protected your PC"**
The installer isn't signed yet, so Windows doesn't know it. Click **More info**, then **Run anyway**.

**Sound is slow, scratchy or muffled**
The engine uses DirectSound by default, because Windows' newer WASAPI audio sounded wrong on some PCs. To try a different audio driver, open a Command Prompt and run:
```
set SDL_AUDIODRIVER=wasapi
"%LocalAppData%\Programs\ZT1-Engine\zt1-engine.exe"
```
This only affects that one launch. It doesn't change anything else on your PC.

**Settings went wrong**
Delete `%LocalAppData%\zt1-engine\zoo.ini`: the next start makes a fresh one. Your saved zoos in `Documents` are not touched.

---

## Reporting bugs

This alpha exists to find problems, so **if you find a bug, please report it.** Every report helps the weekly builds get better. **The most useful report shows where the engine differs from the original game:**

1. What you did
2. What the original Zoo Tycoon does
3. What the engine did instead

Add a **screenshot** or your **saved zoo** (from `Documents\zt1-engine\Saved Games`) if you can. Saying which **version** you were playing helps too (the installer's name, or the update message).

Report issues at **https://github.com/AlexC1991/zt1-engine/issues**.

*Building placement is already known (see [Known bugs](#known-bugs-and-whats-not-ready)), so there's no need to report it.*

---

## Legal and credits

**Zoo Tycoon** is © Microsoft Corporation, developed by Blue Fang Games. This project is **not affiliated with or endorsed by Microsoft or Blue Fang Games**. It ships **none** of the original game's files. You need your own legitimately obtained copy.

**ZT1-Engine** is free and open source under the **MIT License** (see `LICENSE.txt`), © 2025 Wouter (sharkwouter) Wijsman, © 2026 batty251 and the ZT1-Engine contributors.
Source code: https://github.com/AlexC1991/zt1-engine

It uses these open-source libraries: SDL2, SDL2_image, SDL2_mixer, SDL2_ttf, FreeType, libzip, zlib, pe-resource-loader and Dear ImGui (MIT License, © Omar Cornut). Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.

The icon uses [a photo by Magda Ehlers from Pexels](https://www.pexels.com/photo/zebra-s-eye-760958/).

**This software is provided "as is", without warranty of any kind. It's an alpha: use it at your own risk.**
