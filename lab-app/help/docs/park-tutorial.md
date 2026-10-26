# Buildings, fences, people, staff and tank objects

This follows the new ZT1 Lab studio tested on 26 October 2026. The supplied
Park Workshop Tutorial mod has ten creations and the Workshop Garden freeform
map. Use the updated engine from this checkout; older released builds lack the
item builder and independent filter support added during these tests.

## Choose the workflow

In Mods, select a mod or create an empty one. Choose **Make something → Build
an item**. Pick the category and then one of these workflows:

- **Create a separate item** gives a building, fence, staff member or filter a
  new definition and its own name. Pick a working role to inherit its behavior.
- **Customize an existing item** keeps the source definition's path. Your copy
  replaces it while this mod is enabled.

People use **Guest appearance**. The game has man, woman, boy and girl kinds.
An original drawing for man still replaces that appearance; it does not add a
new guest role. Staff also inherit a role, such as keeper or maintenance.

## Original art through the real importer

The `source-assets` folder contains newly constructed OBJ models and their
shared `park-models.mtl`. The models were imported using the studio's Windows
file picker. No compiled game art or editor project was inserted to bypass it.

1. Create **Cedar Snack Kiosk**, Building, separate item, with Burger Stand as
   the working service role.
2. Open **idle → Draw → Blank**, then the editor's Import icon.
3. In the Windows picker, select `cedar-snack-kiosk.obj` and
   `park-models.mtl` together. Include texture images too when your model uses
   them. This particular test uses material colours, not image textures.
4. Set the rendered height to **65 game pixels**, inspect the generated views,
   then convert and **Save**. The importer renders eight item directions.
5. In the Inspector, set construction price **375** and guest price **4.25**.
6. Link **used** to `objects/cedarsnackkiosk/base/idle/idle` for this static
   prototype. A customer now keeps the original kiosk drawing on screen.

Repeat with these inputs:

| Item | Category / inherited role | Files | Height | Sequence |
|---|---|---|---:|---|
| River Rail | Fence / Chain-link | river-rail.obj + MTL | 36 | One pose |
| Lagoon Keeper | Staff / Zoo Keeper | lagoon-keeper-01.obj, -02.obj + MTL | 42 | Two poses, 180 ms |
| Explorer Guest | Guest appearance / man | explorer-guest-01.obj, -02.obj + MTL | 40 | Two poses, 180 ms |
| Reef Pump | Tank filter / Filter | reef-pump-01.obj, -02.obj + MTL | 45 | Two poses, 180 ms |

Set River Rail's price to **95** and strength to **350**. Set Lagoon Keeper's
hiring/monthly pay to **925** and walking rate to **38**. Its male work slots
reuse the imported walk as a prototype. Explorer Guest reuses its walk for a
small set of common poses. Inspect every slot: other poses, female staff art,
fence gates and damage states still link to the game.

A sequence consists of already exported poses. The importer does not rig a
static mesh or invent swimming, walking or work movements. PNG and sprite-sheet
import were covered in the companion animal tutorial; these five item imports
specifically exercised OBJ plus MTL.

## Customize what is already there

Create **Copper Lemonade Stand**, Building, Customize existing, selecting
`scenery/building/drkstnd.ai`. Use the actual catalog choice; a guessed
`lemstand.ai` path is not the source used here.

1. Open **idle → Remake**. The corrected importer includes the complete static
   building and the animated vendor, with its default palette filled.
2. **Recolour** roof shades around `#c39a30` to `#b87333`, closeness **30**.
3. Save, then remake and recolour **used** the same way. Test service states as
   well as idle, so the look does not change while a guest is using it.
4. Set construction price **285** and guest price **3.75**.

Repeat the customization workflow for:

| Item | Source | Edited drawing | Recolour / properties |
|---|---|---|---|
| Copper Chainlink | fences/chainlnk.ai | f / idle | #808080 → #bd7440, closeness 55; price 85, strength 275 |
| Copper Maintenance | staff/maint.ai | m / walk | #808080 → #bd7440, closeness 55; pay 425, rate 40 |
| Violet Guest | Guest appearance / woman | walk | #a08080 → #8851ad, closeness 55 |
| Cobalt Filter | scenery/other/filter1.ai | idle | #808080 → #326aaf, closeness 55; price 240 |

These remakes retain the source's supplied views and frames. The editor now
starts on a direction containing art when a source has diagonal views only.

## Give the filters different behavior

Use each filter's Inspector, not a shared stock-filter setting:

| Property | Reef Pump | Cobalt Filter |
|---|---:|---:|
| Construction cost | 320 | 240 |
| Seconds between cleanings | 30 | 40 |
| Cleaning while healthy | 18 | 12 |
| Cleaning while worn | 8 | 6 |
| Cost per cleaning | 35 | 30 |
| Starting health / worn threshold | 10 / 5 | 10 / 5 |
| Seconds per health lost | 200 | 200 |

The original model is used for Reef Pump's running state. Its off, worn and
broken art remains linked to stock states. Cobalt's non-idle states also remain
linked. These are functional prototypes, not complete original animation sets.

## Test the ecosystem and normal game

**Test in the zoo** enables the mod and reloads the engine. Workshop Garden
contains two 10 × 10 land enclosures, gates, two tanks, the buildings and paths.
Place each filter **outside and adjacent to** a completed tank; the current
game supports one filter per tank. Give keepers gate access and connect each
service building's door to paths. A path that stops at a zoo boundary does not
connect guests to the rest of the zoo.

The recorded tests dirtied both tanks equally. At 31 seconds, Reef was 47 and
Cobalt 29: both lost one purity point naturally, while Reef added 18. At 41
seconds, Cobalt reached 41 after its +12 cycle. At worn health, the corresponding
additions were 8 and 6. Broken filters did not clean; draining tanks turned them
off. Copper Maintenance restored both filters and performed fence fixing.
Lagoon Keeper visited its gated exhibit and supplied food.

Guests bought from both buildings. The Explorer man consumed a hamburger and
hunger became zero on the next status tick. The Violet woman consumed a drink
and thirst became zero. Needs apply on a tick, so do not sample only the instant
after a transaction. Other inherited guest needs still matter.

Use **Save as a map** to preserve the fixture. Map designs keep terrain,
objects, fences and filters, but omit live animals, guests and staff. Adopt and
hire them again when starting the freeform map in normal Play.

In **Play with it**, choose Workshop Garden from the freeform list. The normal
buy/hire panels list the new names and prices. Their **Mods** filter helps find
them. Staff were hired, the original kiosk purchased and placed, and a zebra
adopted into River Rail through these ordinary game panels.

## Check, install and share

**About this mod → From the Test Zoo view** sets its preview. **Publish → Check
it** runs local package rules; it does not upload. This pack passed the local
rules and had Problems 0, with an author metadata hint still to complete before
public upload. Nothing was publicly uploaded during this session.

Install `Park-Workshop-Tutorial.zip` through the game's **Mod Menu → Add a
mod…**, switch it on and restart. In Lab Play, stop and start Play to reload it.
Keep `Workshop-Authoring-Sources.zip` separately for the models and editable
projects; authoring files are deliberately excluded from the installable ZIP.

Disable **Lemonade Stands** while testing this replacement stand: it changes the
same `drkstnd.ai` file and takes precedence when both are enabled. The test
session leaves Park Workshop Tutorial on and Lemonade Stands off for inspection.
Other existing mod switches were retained.

The captioned video presents corrected checkpoints. `creator-process.mp4` is
the raw Lab session, including repairs and waits. It does not reliably include
native picker windows or the embedded game's child window; actual game images
and `gameplay-motion.mp4` cover normal Play. Failed attempts remain in the raw
evidence, labelled in the test report and check log.
