# Moonstone Habitat and theme packs

This records the new Mods menu workflow used on 26 October 2026. The source OBJ,
MTL and PNG files were selected through native pickers, converted in the editor
and saved through the normal compiler. Internal project JSON was not inserted to
create the playable art. The source files and editable projects are supplied
separately from the installable mods.

## Make a separate item

Choose Mods → Make something → Build an item. Choose its role and the separate
item workflow, then give it a name and select the stock role and properties to
start from. This supplies behavior and working slots; it does not invent new AI.
Open a real animation row, choose Blank, then Import external art. Select OBJ and
MTL together, set the height and preview all eight object directions. Wait for
conversion, then Save. Check the actual slot names: a bin uses empty, half and
full, rather than idle. Link used states to suitable custom art when needed.

Original imported examples: Fern Spire, Moonstone Rock, Cedar Refuge, Moonstone
Gate and fence body, Moonstone Entrance, Fern Feast, Cedar Enrichment, Moonstone
Bench and Moonstone Bin. Amberhorn uses two original body poses and separate egg,
hatch and broken-shell meshes. Their simple art is intended to test the pipeline.

## Customize something already in the game

Select Customize an existing item. Open each relevant row with Remake, use
Recolour across its frames and directions, and Save. The source item changes
while the mod is on. Indigo Fern, Copper Granite, Clay Refuge, Brass Gate, Mint
Zoo Wall, Amber Greens and Amber Play Log demonstrate this route. Pearl Glass
Tank is a separate tank identity with remade stock layers; Copper Tank replaces
an existing tank wall. Neither tank is an entirely original geometry set.

## Paths, ground, needs and eggs

Import the twenty-shape path strip as twenty horizontal cells with whole-canvas
placement. Moonstone Path contains its numbered connection and ramp resources.
Mint Brick Path remakes the stock twenty shapes. Import Violet Meadow as a square
opaque texture; Moss Floor remakes another texture. These reskin existing ground
types, retaining their terrain IDs and gameplay properties.

In Amberhorn → Needs, choose Fern Feast as custom food, a scientist as carer,
and the desired ground and object likes. Enable the mod to populate custom food
choices. Its meadow, water, foliage, rock, shelter and enrichment preferences were
edited through this page; price, space, hatch time and other specifics use the
Inspector. Make egg_idle, egg_hatch and egg_break separately. Own copy… separates
a slot from shared linked art before editing it. Prototype body poses are reused
for other actions; this is not a complete animation set.

## Build and check a map

Finish asset edits, then Test in the zoo and New map → My own size → 64 by 64.
Draw the boundary and place an entrance, lay connected paths, enclosures, objects
and tank walls. Complete and fill the marine tank, set its depth and salinity,
then put the filter beside it. Furniture must sit beside an accessible path;
orient the bench so its entry slots face the path. Save as a map into the mod,
then reopen it to verify the saved layout. Moonstone Habitat was confirmed as a
64 by 64 blank-base design with five enclosures and fourteen placed objects.

Adopt animals and hire carers for live testing. The map design deliberately
omits live actors. Amberhorn hatched, reached suitability 81, received Fern Feast
from the scientist and consumed it. Its rest behavior selected a custom enclosure
object. The original bench restored guest energy. The custom tank held a marine
animal and the Reef Pump raised purity from 30 to 47 over 35 simulated seconds.
These measurements do not certify a balanced zoo or every animal behavior.

## Publish a theme pack as one mod

Open Publish → Build a theme pack…. Include Moonstone Habitat Kit, Park Workshop
Tutorial and Wildlife Import Tutorial. Enter a pack name, id, version and About,
then Build pack. On About, set the pack's own image using Choose a picture… or
From the Test Zoo view. Open Publish → Check it. Moonstone Zoo Theme passed with
three package files, six animals, twenty-nine other definition entries and three
maps. A public upload was not performed in this test.

The pack is a snapshot. Keep the source mods and authoring projects to edit later;
build a new snapshot after changes. The builder combines compatible mods,
deduplicates identical resources and reports differing files at the same path.
Mods with identity-specific settings currently stay separate. Use the pack's
ordinary Upload button when you intend to publish that single listing.

## Retrieve and play

Install Moonstone-Zoo-Theme.zip through Bring in a mod or the game's Add a mod…
picker. Switch the installed pack on and restart. Switch off its three components
and any earlier copy of the same pack. In normal Play, select Moonstone Habitat
from Freeform, then adopt or hire again. The actual ZIP import and map selection
were tested. Amberhorn was adopted for $650 and hatched using its original art.

Containment failure: Amberhorn subsequently escaped the glass enclosure near
the wall. Its base species retains jumping and fence-related behavior. Do not
treat this map's glass enclosures as certified dinosaur containment. Use suitable
strong dinosaur fencing and test the animal's abilities and placement. The
escape and consequent welfare/adoption-ban popup remain in the raw evidence.

walkthrough.mp4 is a captioned sequence of actual checkpoints, not continuous
screen video. creator-process.mp4 is the raw Lab capture at about one frame per
second, with repairs and waits retained; native dialogs and the embedded game
are supplemented by separate actual screenshots. Browser rendering of the local
HTML was unavailable; local references and media decoding were checked instead.
