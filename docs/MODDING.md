# Making mods for ZT1-Engine

A mod changes or adds to Zoo Tycoon without touching the game's own files. Players switch mods on and off in the **Mod Menu** (main menu › Game Options › Mods & Updates › Mod Menu), and every mod is off until they switch it on.

There are three ways to get a mod into the game:

| | Who can use it | How |
|---|---|---|
| **Add a mod** | Only you, on your own PC | Mod Menu › **Add a mod...** in the game, or the Lab's **Your mods** › **Bring in a mod** |
| **The mods folder** | Only you | Put the mod's folder in the mods folder (Mod Menu › **Folder...** shows where) |
| **The mod repository** | Everyone with the official game | The Lab's **Your mods** › **Upload to the mod repository**. Players download it from Mod Menu › **Mod repository**. |

## What a mod is

A mod is a folder with a `mod.json` and its files:

```
My Plasma Fence/
  mod.json          what it is (below)
  content.ztd       game files: a zip with the game's own paths inside
  shaders/          optional: shaders (a look for the art)
  hd/               optional: HD art (bigger frames for the zoomed-in map)
```

`mod.json`:

```json
{
  "id": "plasma-fence",
  "name": "Plasma Fence",
  "author": "Your name",
  "version": "1.0",
  "about": "A glowing fence for the dinosaur exhibits.",
  "replaces": ["fences/dzbar"],
  "settings": []
}
```

- **id** is the mod's name in the mod repository. Use lower-case letters, numbers, `.`, `_` and `-`. Once you've uploaded a mod, its id is yours.
- **version**: raise it each time you change the mod, so players see there's an update.
- **replaces**: the game files or folders it changes, which the Mod Menu lists under "It changes".
- **settings**: the mod's own options, shown in Game Options. See `mods/zt1-toon/README.md`.

## Kinds of mod

**Game files: new animals, objects, scenery, fences, buildings.** These go in a `.ztd`, which is a zip with the game's own paths inside: `animals/<name>/...`, `objects/<name>/...`, and each one's `.ai` that says how it works. This is how every official download and every fan download for the 2001 game worked, so the same files load here.

In game, the buy panels' filter (All, Zoo Tycoon, Dinosaur Digs, Marine Mania) has a **Mods** choice too. It lists only what your switched-on mods add or change, so a new animal or rock is easy to find. Turn it off with the `modfilter` switch in Game Options.

The Lab helps:
- **Game files** lists every path the game has, with each `.ai` readable.
- Its **animation maker** makes a recoloured copy of an animal or object as a mod.

**Shaders** change how the zoomed-in art looks. `mods/zt1-toon` is a complete example with a guide.

**HD art** adds bigger frames for the zoomed-in map. The Lab's **HD art** experiment makes them with an AI upscaler.

## The rules

These are the same in the Lab's **Check it** and in the mod repository's own checks:

- **Game data only.** That means `.ztd .ai .ani .pal .tga .bmp .png .webp .jpg .wav .ogg .mp3 .json .ini .txt .md .frag .lyt .uca .ucs .ucb .scn .zoo .lng`, and the extension-less frames inside animation folders. No programs or scripts (`.exe .dll .bat .js .ps1` ...), not even inside a `.ztd`.
- No paths that point outside the mod, and no links.
- At most 200,000 files.
- A `mod.json` with an id and a name.

## Sharing it

In the Lab: **Experiments › Your mods**, pick your mod, then **Upload to the mod repository**.

- You need a CodeRook account. Sign in with `cbx sign-in`, or paste a personal access token into the Lab's **Your uploads** panel.
- The upload is checked automatically. This includes the rules above and a malware scan. Once the checks pass, it's offered in every official game's Mod Menu.
- **To update it**, raise its version and upload it again.
- **To take it down**, use **Your uploads › Take it down**. You can only change or remove mods you uploaded yourself.

## Old mods from the 2001 game

Fan-made downloads from 2001 to 2010 (new animals, objects, expansion-style packs) are `.ztd` files, the same kind the game itself uses. To use one you already have:

- **In the game:** Mod Menu › **Add a mod...**, then pick the `.ztd`, or a `.zip` of several.
- **In the Lab:** Your mods › **Bring in a mod**.

It becomes a mod of its own, switched off, with a `mod.json` that says what it adds. It loads exactly as it did in the original game. We don't re-host other people's old downloads; bring in the ones you have.
