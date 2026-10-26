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
  picture.png       optional: its picture in the Mod Menu (367 x 276)
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
  "picture": "picture.png",
  "settings": []
}
```

- **id** is the mod's name in the mod repository. Use lower-case letters, numbers, `.`, `_` and `-`. Once you've uploaded a mod, its id is yours.
- **version**: raise it each time you change the mod, so players see there's an update.
- **replaces**: the game files or folders it changes, which the Mod Menu lists under "It changes".
- **picture**: an image in the mod's folder that the Mod Menu shows beside it, 367 x 276 (the Lab's mod Overview sets one for you, cut to that size).

Lab Upload automatically prepares a small `repository-preview.png` from this
picture for browsing in **Mod repository**, before the player downloads the mod.
It is included in the ordinary upload and appears after the package passes the
repository's checks and malware scan. Keep the original `picture.png` for the
installed mod. No separate image upload is needed. Older published packages need
a version update uploaded with the current Lab to gain this preview. A missing
or invalid cover leaves the listing usable without a picture.
- **settings**: the mod's own options, shown in Game Options. See `mods/zt1-toon/README.md`.

## Kinds of mod

**Game files: new animals, objects, scenery, fences, buildings.** These go in a `.ztd`, which is a zip with the game's own paths inside: `animals/<name>/...`, `objects/<name>/...`, and each one's `.ai` that says how it works. This is how every official download and every fan download for the 2001 game worked, so the same files load here.

In game, the buy panels' filter (All, Zoo Tycoon, Dinosaur Digs, Marine Mania) has a **Mods** choice too. It lists only what your switched-on mods add or change, so a new animal or rock is easy to find. Turn it off with the `modfilter` switch in Game Options.

The Lab helps:
- **Game files** lists every path the game has, with each `.ai` readable.
- Its **animation maker** makes a recoloured copy of an animal or object as a mod.

**A new animal.** In the Lab, open an animal's `.ai` in Write Code › Game files and press **New species from this**. Give it a name and a colour: you get a new mod with the animal's art copied from your own game under the new name, dyed, and its `.ai` as a `.uca` species file with the new name in `[1033] cName`. That file opens to change: its cost, herd size, what it likes, the line the buy panel shows, and its behaviour sets (a list of animations and sounds; `fPlaySetProb` picks one by chance). Then design its look in the mod's **Art** page.

**The Lab's mod studio.** The Mods tab is one workspace for a mod: its contents down the left by kind (animals, buildings, scenery, maps), the editor for what you pick in the middle, an **Inspector** on the right with the picked item's values as plain controls (price, food and rest, group size, what it can do; a building's capacity and prices), saved into the mod as you change them, and a **Problems** list at the bottom of what's still missing. **＋ Make something** starts a new animal, a new map or a change to one of the game's items.

**A new animal from scratch.** In the studio, **New animal**: its kind (a zoo animal, a Marine Mania animal or a dinosaur), a name, and an animal it behaves like (its needs, biome, behaviour and sounds, all yours to change in its species file). It starts with no art of its own: each of its animations is *linked* - its line in `[m/Animations]` is the whole path of an animation that exists (`walk = animals/zebra/m/walk/walk.ani`, the way the game's own files share art) - so it works in the game at once. **Draw** any of them in the editor (layers, frames, onion skin, the linked animation as a faint guide; a Shadow layer is its ground shadow) for the five ways it faces, N, NE, E, SE and S - the game mirrors the other three. Saving makes the game's art: one 256-colour palette for all the animal's drawn animations, made from the colours you used. **Link…** points any line at any animation, yours or the game's. Its **Needs** tab holds everything else the game weighs: its kind and keeper, its food, its biome and continent, the ground, plants, rocks and shelters it likes and the animals it lives with - or **Copy needs from…** another animal as a preset. A dinosaur hatches from an egg: its `egg_idle`, `egg_hatch` and `egg_break` animations are drawn or linked like any other. You needn't draw from nothing. Every new drawing starts with **Remake** (an existing animal's, person's, building's, tree's or fence's art copied in to change), **Stencil** (its outline over your canvas, its inside as faint as you like - draw your own in it, paint kept inside the outline if you want, or fill the outline as a base) or **Blank**. Then reshape it for all of them at once (**Size**, a **Body** longer or shorter along the way it faces, a **Band** of its height taller or shorter for a neck or legs) and **Recolour** a colour and its shades everywhere. The **direction ring** shows the frame in all eight ways it faces while you work.

**Changing the game's own things** (a building's capacity, an animal's price, its colours). Open the file in Game files and press **Change it in a mod**: the mod gets a copy at the same path, which takes the game's place while the mod is on. For art, the mod's **Art** page lists the game's art for what the mod changes. Change the palette or paint frames, and saving puts the copies in the mod.

**The Art page** (Mods › a mod › Art): pick colours on the animal or by shade, change them (one palette colours all of an animal's animations), paint frames, fade a pattern along the body in every frame (the quagga's stripes fading out towards its tail), or send the frames out as PNGs to paint in another program and bring them back.

**A freeform map.** In the Lab's Test Zoo, start from a **blank map** (flat grass, the game's Small, Medium or Large size or your own, 16 to 512 tiles a side) or one of the game's maps. A blank map starts in **Edit the map**: draw the zoo's boundary with the Zoo Fence and put the entrance where guests should come in (the wall opens across its doorway). Then paint the ground and water, shape hills, lay paths and place trees and buildings. The **Terrain** view shows the whole map from above. Then **Save as a map**: it goes into a mod as `freeform/<id>.scn` (the map it's over's `.scn`, with `[start] design=` and `[freeform] title=`) plus the design (`.zt1map`), a description and a preview. A blank map is saved with `[start] blank = W H` and no map of the game's under it. Switched on, it's in the game's freeform map list. Players turn mod maps off with the `custommaps` switch.

**Shaders** change how the zoomed-in art looks. `mods/zt1-toon` is a complete example with a guide.

**HD art** adds bigger frames for the zoomed-in map. The Lab's **HD art** experiment makes them with an AI upscaler.

## Build buildings, fences, people and objects

In **Mods → Make something → Build an item**, choose Building, Fence, Staff
member, Guest appearance, Tank filter, or Scenery/object. Choose **Create a
separate item** for its own identity, or **Customize an existing item** to replace
the selected definition while this mod is enabled. Start from a working role;
the Inspector changes its prices, size and supported properties.

Draw an existing animation slot with **Blank → Import** for original PNG or
OBJ art, or use **Remake → Recolour** for a customized game asset. Select the
OBJ, its MTL and any texture pictures together in the file picker. Multiple
exported poses form a sequence; a single mesh is not automatically rigged or
animated. Items can use eight supplied views; a remake keeps its source views.
Remaking a building includes its static background and default colour slots.
Check both idle and used states, not just its first frame.

**Link…** and **Link other poses…** explicitly reuse an animation. Reusing a
walk drawing for work or damage slots makes a prototype, not a finished set of
motions. Staff retain an existing duty such as keeper or maintenance. Guest
appearances replace man, woman, boy or girl; they do not add a fifth guest role.
Icons and poses that have not been drawn continue to use their inherited art.

**Test in the zoo** reloads enabled mods; **Play with it** starts the normal
game. Check the normal buy panel's **Mods** filter, prices, placement, guest
service, enclosure gates and staff duties. Different tank filter definitions
keep their own footprint, cleaning delay, cleaning strength, wear, upkeep and
service settings; placed filter identities are saved with the zoo or map.

If a replacement looks unchanged, check other enabled mods that change the
same file. An earlier mod folder can take precedence. For example, disable
**Lemonade Stands** when testing **Park Workshop Tutorial**'s replacement of
`scenery/building/drkstnd.ai`. Local **Publish → Check it** is a package rules
check, not an upload or a complete behavior test.

## Import external animal art in the new studio

In **Mods**, pick or create a mod, then **Make something → New animal**. Choose its kind, name and starting behavior. Under **Animations**, choose **Draw** beside an animation, then **Blank**. In the editor, **This direction → Import external art** opens the file picker.

| Source | Conversion controls | Result |
|---|---|---|
| One PNG picture | One picture; visible height; selected direction or repeat | A fitted frame, with its visible bottom at the ground point |
| Horizontal PNG strip | Horizontal frame strip; column count; height; timing | A sequence for the chosen direction, or the same sequence repeated |
| Directional PNG sheet | Grid; columns; rows; row directions such as `N,NE,E,SE,S`; height; timing | Frames across each row, assigned to its named direction |
| OBJ mesh with MTL | Select the OBJ and MTL together, plus any named texture images; Y-up front axis; height | Five rendered views of the supplied mesh |
| Several OBJ poses | Select the numbered OBJ files together, with MTL/textures; timing | Each file becomes one pose, sorted by filename, rendered in five directions |

Use a regular grid with margins around each sprite. PNG transparency is retained; scaling uses nearest pixels. Check all directions, frames and the ground point before **Save**, which builds the game's animation and palette inside the mod. Import replaces frames in the selected layer for the supplied directions; other matching layers are retained. Undo restores the previous drawing.

A picture does not supply unseen angles: repeating it gives the same view in each direction. OBJ import converts polygon meshes and supplied poses; it does not rig a model or invent a walking cycle. Export Y-up OBJ poses from your modeling application. MTL diffuse colours and selected texture images are supported; advanced material/shader effects are not.

For a quick prototype, **Link other poses…** reuses one drawn animation for the listed behavior slots, keeping crate and egg poses. Wait for linking to finish before editing needs. A finished animal needs appropriate separate walking, eating, resting and other poses.

Under **Pictures → Use an animation frame…**, choose one drawn frame to make its icon, list picture and plaque at the game's sizes. Each picture remains editable. Under **Needs**, set its food, habitat, terrain and compatibility; the Inspector sets its price, group size, space and other values.

**Test in the zoo** enables the mod and restarts the Test Zoo. Build an appropriate enclosure, add companions, shelter and keepers, and compare its recommendations before and after meeting its needs. **Play with it** tests the ordinary game: **Adopt Animals → Mods → choose the animal → male or female button → place it**. Selecting the icon alone does not start adoption.

For a zoo animal that swims, start from an amphibious species and keep its land
and water animation slots separate. Test natural entry into the pond and return
to land, as well as feeding. A marine animal needs a completed tank with enough
water depth, the right salinity, a filter beside the tank and the appropriate
carer. Check its surface and submerged art with their underlays and shadows;
matching frame counts matter. A successful package check does not certify every
marine behavior or show trick. Use the Test Zoo's species check for those deeper
diagnostics, and compare inherited warnings with the base species.

**Save as a map** preserves the habitat design for ordinary Play. Live animals
and staff must be adopted again when starting that freeform map.

**Publish → Check it** checks the package locally. **Upload it** sends it to the repository for its own checks; a local pass does not mean it is published. Players download through the game's **Mod Menu → Mod repository**, then switch the downloaded mod on under **Your mods** and restart. In the Lab's Play view, stop and start Play to reload mods.

## Habitat items and theme packs

**Make something → Build an item** supports plants, rocks, shelters, enrichment,
zoo entrances, tank walls, paths, ground textures and animal food as well as the
building, fence, person and filter workflows. Choose a separate item or customize
an existing one. Imported object meshes provide eight directions. A path drawing
contains twenty numbered connection shapes, including its ramps; use whole-canvas
placement for path sheets and ground textures. Ground textures replace an existing
terrain's appearance and retain that terrain's gameplay identity.

An animal's **Needs → Custom food** can select food from an enabled mod. Its normal
carer then supplies that food. Dinosaur egg idle, hatch and broken-shell slots are
editable separately. **Own copy…** makes an independently editable drawing when
several slots currently link to the same animation.

In **Publish → Build a theme pack…**, include two or more compatible local mods,
give the pack its own name, id, version and description, and build it. On **About**,
choose its own image or take the Test Zoo view. Then check and upload the pack
through the ordinary Publish controls as one mod. Players install and switch it
on once. Switch off the separate component mods while testing the pack.

A pack is a snapshot. The original mods remain available to edit; build a new
snapshot after changing them. Different files at the same game path stop the build
and name the conflicting mods. Identical shared files are included once. Mods
with identity-specific settings currently need to stay separate. Authoring project
folders are excluded from the playable pack; retain them separately for editing.

Finish and save asset edits before laying out a map. Check the saved map by
reopening it; **Save as a map** stores its design, while live animals, guests and
staff are created again during Play.

## The rules

These are the same in the Lab's **Check it** and in the mod repository's own checks:

- **Game data only.** That means `.ztd .ai .ani .pal .tga .bmp .png .webp .jpg .wav .ogg .mp3 .json .ini .txt .md .frag .lyt .uca .ucs .ucb .scn .zoo .lng`, and the extension-less frames inside animation folders. No programs or scripts (`.exe .dll .bat .js .ps1` ...), not even inside a `.ztd`.
- No paths that point outside the mod, and no links.
- At most 200,000 files.
- A `mod.json` with an id and a name.

## Sharing it

In the Lab: **Mods**, pick your mod, then **Publish → Upload it**. Set its cover
on **About** first, using your own image or the Test Zoo view.

- You need a CodeRook account. Sign in with `cbx sign-in`, or paste a personal access token into the Lab's **Your uploads** panel.
- The upload is checked automatically. This includes the rules above and a malware scan. Once the checks pass, it's offered in every official game's Mod Menu.
- **To update it**, raise its version and upload it again.
- **To take it down**, use **Your uploads › Take it down**. You can only change or remove mods you uploaded yourself.

## Old mods from the 2001 game

Fan-made downloads from 2001 to 2010 (new animals, objects, expansion-style packs) are `.ztd` files, the same kind the game itself uses. To use one you already have:

- **In the game:** Mod Menu › **Add a mod...**, then pick the `.ztd`, or a `.zip` of several.
- **In the Lab:** Your mods › **Bring in a mod**.

It becomes a mod of its own, switched off, with a `mod.json` that says what it adds. It loads exactly as it did in the original game. We don't re-host other people's old downloads; bring in the ones you have.
