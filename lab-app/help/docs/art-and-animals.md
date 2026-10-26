# Draw and import an animal

## Choose a suitable base

Use **Mods → Make something → New animal**. Choose Zoo, Marine or Dinosaur, name it and choose **Behaves like** from that kind. The base supplies behavior, sounds and working slots until you replace them. Changing the kind label alone does not create a swimming behavior set or a dinosaur's egg states.

## Start a drawing

Open a real animation row and choose **Draw**. **Remake** copies the existing frames. **Stencil** overlays the source outline with adjustable inside opacity. **Blank** starts empty. The direction ring selects a view; the timeline selects a frame. Use **Own copy…** before changing art that is linked to another slot.

## Import a PNG or sprite sheet

Choose **Import external art** and select the PNG with the normal file picker. Choose one picture, a horizontal frame strip, or a directional grid. Set the cell count, directions, frame timing and visible height. Check the ground anchor and preview every supplied frame and view before **Save**.

A repeated picture shows the same viewpoint in every direction. It does not create unseen angles. Keep transparent margins and the shadow separate from the visible animal where possible.

## Import OBJ art

Select the OBJ together with its MTL and any texture images. Set the fitted height and inspect the rendered directions. For a pose sequence, supply multiple exported OBJ poses in order. Export Y-up meshes. The importer renders supplied models and poses; it does not rig or animate a single static mesh.

## Finish the slots and pictures

Give walking, eating, resting and other actions appropriate art. **Link other poses** can reuse frames for a prototype, but those links are not separate animations. Check young and adult variants, shadows and water layers too. In **Pictures → Use an animation frame…**, make the buy icon, list image and plaque, then adjust them if needed.

## Set specifics and needs

Use **Needs** for food, carer, biome, terrain, liked objects and neighbors. Use the Inspector for price, group size, space and other values. An enabled mod's custom food can be selected in Needs. The ordinary carer must be able to reach and supply it.

For marine animals, check surface and submerged movement, minimum adoption depth, preferred depth, salinity and water quality. For dinosaurs, prepare **egg_idle**, **egg_hatch** and **egg_break**, hatch timing and an enclosure appropriate to its behavior.

## Prove it in the game

Build a suitable home in Test Zoo, run time and observe feeding and movement. Then adopt the animal through Play's ordinary creature panel: choose the icon, choose male or female, and place it. Test rejected placements as well as valid ones. Use the [test checklist](help:testing) before calling a species finished.

[PNG lesson](help:png-lesson) · [Directional-sheet lesson](help:sheet-lesson) · [OBJ-pose lesson](help:obj-lesson) · [Swimming and marine tutorial](help:aquatic-tutorial)
