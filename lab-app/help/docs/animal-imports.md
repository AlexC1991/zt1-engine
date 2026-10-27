# Three animals with the new Mods studio

Recorded 26 October 2026. This tutorial uses the development version of the new ZT1 Lab Mods workspace. It creates three fictional zoo animals in one mod, Wildlife Import Tutorial.

| Animal | Input | Game frames | Custom needs | Price |
|---|---|---|---|---|
| Copperleaf Pika | One transparent PNG | One pose repeated across five directions | Fruit; 2–4 animals; grass and deciduous forest floor; foliage, rocks and shelter | $420 |
| Indigo Springhare | 4-column, 5-row PNG sheet | Four walking frames in each of five directions | Grass; 2–6 animals; grassland and shelter | $610 |
| Jade Shellback | Two numbered OBJ poses plus MTL | Two supplied poses rendered in five directions | Herbivore chow; 1–3 animals; grassland, rocks and shelter | $900 |

## Before starting

The Lab must show Engine running. Copy the supplied source-assets folder somewhere you can find with the file picker. There is no need to create internal project JSON or copy files into the mod's .art folder. The Lab's Import and Save controls perform the conversion.

Create an empty mod from the picker. Use Make something → New animal → Zoo animal. Copperleaf Pika and Indigo Springhare start from Thomson's Gazelle; Jade Shellback starts from Plains Zebra. These provide the initial care and behavior system.

## Import settings

For Copperleaf Pika: Draw beside walk → Blank → This direction → Import external art. Select copperleaf-pika.png. Choose One picture, height 38, Repeat for all five directions. This is a prototype with one viewpoint; repetition does not create unseen angles.

For Indigo Springhare: choose indigo-springhare-sheet.png. Select Grid, columns 4, rows 5, directions N,NE,E,SE,S, height 42, frame time 120 ms. Preview every view and frame. The three other compass directions are mirrored by the game. A horizontal strip can instead be assigned to the selected direction with its column count.

For Jade Shellback: select jade-shellback-01.obj, jade-shellback-02.obj and jade-shellback.mtl together. Choose Y-up with front +Z, height 32, 200 ms per pose. Files are sorted by name. The supplied model has two different leg poses; a single static OBJ stays still. Import is not automatic rigging or animation generation.

Check the visible feet against the red ground point. Save builds the game's palette and animation in the mod. Use Link other poses for the prototype's adult slots and wait until linking finishes. The examples share their imported pose sets across eating, resting and other behavior states; they do not have a complete bespoke animation library. Crate poses remain shared with the game.

## Needs and pictures

Set food, biome, preferred ground, objects and animal compatibility in Needs. Set price, group size, space and hunger growth in the Inspector. Wait for Saved after changing values. Check inherited dislikes as well as the new biome. These examples use the fictional creature family and like its members.

Pika: grass 50, deciduous floor 50; group 2–4, space 80, density 40, hunger growth 4. Springhare: grass 100; group 2–6, space 80, density 40, hunger growth 3. Shellback: grass 100; group 1–3, space 100, density 50, hunger growth 2. All require shelter.

In Pictures → Use an animation frame, choose the animal's own walk animation, SE, frame 1, and make the icon, list picture and plaque. Each picture can then be edited independently. Fill in About this mod and use the Test Zoo view or an image for its Mod Menu preview.

## Test and use it

Test in the zoo loads the mod temporarily and restarts the Test Zoo. Your normal mod switches stay unchanged. The temporary-test label has an **End test** control to return to them. Testing does not yet build a home automatically. Build an enclosed habitat with the palette. Start with bare grass to check missing-needs warnings, then add the requested ground, companions, shelter, trees, rocks and keeper access. The finished tests used separate enclosures and paired animals.

In Play, start a freeform zoo. Adopt Animals → filter Mods → select an icon → click male or female → place the animal in a closed land exhibit. Selecting the icon alone does not start adoption. The small ordinary-game pen in the final screenshot is a purchase/rendering check; the separate Lab habitats are the welfare test.

## Publish and retrieve

The studio's top Publish button opens Check it and Upload it. Check it validates the local package; a pass does not publish it. Upload it sends the package using the signed-in CodeRook account. Wait for the repository's rules and malware checks to pass and confirm Your uploads says In the mod repository. For an update, raise the version and upload the same mod ID.

Players use Game Options → Mods & Updates → Mod Menu → Mod repository. Download a part, return to Your mods, switch it on, and restart. In Lab Play, stop and start Play instead of the game's Restart. The repository button requires the official repository client; the test installation had it and successfully fetched the live Plasma Fence listing. We did not upload the tutorial prototype or replace that existing download. Local Add a mod can import the supplied mod ZIP.

## Recorded results

All three were loaded, displayed and adopted in the ordinary game with their own names, icons and prices. Species diagnostics reported zero problems. After suitable Lab habitats were built, suitability was 87 for Pika, 81 for Springhare and 82 for Shellback, with happiness 95–100. Hunger decreased during the feeding stress check, and no tested animal was sick or escaped.

The raw creator-process.mp4 is a silent one-frame-per-second capture of the Lab workspace, including pauses, bugs and retakes. Native file dialogs and the embedded game's child window are not fully included in that capture; their process is described here and actual game screenshots are included below. The illustrated guide presents the verified path. Breeding, long-term survival, textured OBJ material variants and live publishing/download of this prototype are not certified by these tests.

## Illustrated steps

### 1. Choose New mod, enter Wildlife Import Tutorial, and use Empty: build it in the studio.

![Step 1](../../renderer/help/media/03f6a7750bb5e5af61.webp)

### 2. Make something → New animal. Choose Zoo animal; this selects its care system.

![Step 2](../../renderer/help/media/e4e859ffb925a154ba.webp)

### 3. Name the species Copperleaf Pika and choose Thomson’s Gazelle as its starting behavior.

![Step 3](../../renderer/help/media/433fd0340ae6641943.webp)

### 4. Choose Draw on walk, then Blank. The direction ring shows the five views that need frames.

![Step 4](../../renderer/help/media/1e6ba5ee1edc650a7c.webp)

### 5. Choose One picture, fit visible art to 38 pixels, and Repeat for all five directions for this simple prototype. Repetition does not invent new viewpoints.

![Step 5](../../renderer/help/media/5a65194403f7b60522.webp)

### 6. Check the visible feet against the red ground point. The importer ignores nearly transparent edge pixels when fitting the art.

![Step 6](../../renderer/help/media/0873493115ba25690e.webp)

### 7. Use Link other poses to reuse the imported art for the prototype’s adult behavior slots. This shares frames; it is not bespoke eating or sleeping animation.

![Step 7](../../renderer/help/media/00e0922751d32415e8.webp)

### 8. Create Indigo Springhare in the same mod. Each species has its own art, needs and price.

![Step 8](../../renderer/help/media/0ad9cad50af011b45b.webp)

### 9. For Indigo Springhare, select Grid: frames across, directions down. Enter 4 columns, 5 rows, N,NE,E,SE,S; fit to 42 pixels with 120 ms frames.

![Step 9](../../renderer/help/media/cba26b12c9c05967c8.webp)

### 10. Check the four imported frames and all five directions at game size. Save the finished conversion.

![Step 10](../../renderer/help/media/91d6dfacbbad7f9d16.webp)

### 11. Select both numbered OBJ pose files and their MTL together. Choose Y-up, front +Z, 32 pixels and 200 ms per supplied pose. The importer renders five directional views.

![Step 11](../../renderer/help/media/522a89181ffe37ddbd.webp)

### 12. Jade Shellback rendered from the OBJ meshes: two supplied poses, five directions, MTL colours and transparent background. Save compiles the same game art used for PNG imports.

![Step 12](../../renderer/help/media/30e3660ee72d782132.webp)

### 13. Pictures → Use an animation frame. Pick the drawn walk animation, SE, frame 1, and Icon, list picture and plaque. The Lab sizes and compiles them.

![Step 13](../../renderer/help/media/22cab71528f3be70f2.webp)

### 14. The three picture cards now show Jade Shellback’s own art rather than the base animal’s pictures. Each card can still be edited.

![Step 14](../../renderer/help/media/a024a9c08886115302.webp)

### 15. Use Needs for food and preferred ground; the Inspector sets adoption price, group size, space and hunger rate. Values save into the mod as they change.

![Step 15](../../renderer/help/media/ddf5721518fccad543.webp)

### 16. Needs includes inherited likes and dislikes. Update forest objects and compatible creature families as well as the biome and terrain.

![Step 16](../../renderer/help/media/17509819a637082f66.webp)

### 17. A bare grass enclosure exposes missing companion, shelter and habitat needs. These warnings are part of testing the new values.

![Step 17](../../renderer/help/media/1d8e2d5486464ed65b.webp)

### 18. Verified final Copperleaf Pika art in the engine, with its completed habitat and welfare values.

![Step 18](../../renderer/help/media/fba686d1be6a48991a.webp)

### 19. Verified final Indigo Springhare art in the engine, with its completed habitat and welfare values.

![Step 19](../../renderer/help/media/fc99d2fef2f7ad24cf.webp)

### 20. Verified final Jade Shellback art in the engine, with its completed habitat and welfare values.

![Step 20](../../renderer/help/media/2e32372f39603f16a0.webp)

### 21. About this mod supplies the name, author, version and description shown in the game’s Mod Menu.

![Step 21](../../renderer/help/media/27b84fadeb9dbc9e12.webp)

### 22. The game’s Mod Menu shows Wildlife Import Tutorial switched on, with its author, description and preview.

![Step 22](../../renderer/help/media/9494a7b3b3e348d2b5.webp)

### 23. Live repository browsing works in this installation. Have means a part is installed; the action offers Download again.

![Step 23](../../renderer/help/media/e372f27eac9e20fdb0.webp)

### 24. Adopt Animals → Mods lists all three new icons and their custom names and prices.

![Step 24](../../renderer/help/media/1765575e1d9b32ce66.webp)

### 25. Select the animal, then its male or female button, then place it. All three imported animals were adopted through the ordinary game UI. This small pen checks purchase and rendering; the separate Lab habitats test welfare.

![Step 25](../../renderer/help/media/bbf69c24266a9a8f1b.webp)

### 26. The completed mod has a preview and description; package checks pass with no listed problems. Upload it remains a separate publishing step.

![Step 26](../../renderer/help/media/22b55e9b972edc82f7.webp)


## Additional swimming and marine tests

The updated pack now also includes Copperbank Hippo and Azure Reef Dolphin variants and the Aquatic Mod Test habitat map. See [the aquatic tutorial](help:aquatic-tutorial), illustrated guide and results. These use Remake/Recolour and preserve distinct land, water and tank poses. Basic placement, natural swimming, feeding, purity, salinity and minimum-depth checks passed; inherited marine trick/layer warnings remain.
