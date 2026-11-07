# Make items light up at night

Buildings, scenery, shelters, toys, food stands and other placed items can glow after dark when the player has **Day and night** switched on (Game Options → Graphics). You draw the glow as a light map in the drawing editor. Animals, people, paths and fences don't use light maps.

## Paint what glows

1. Open your item in **Mods**, choose an animation row (usually **idle**) and **Draw** it.
2. Pick the colour of the lit part with **⊙** (a window, a bulb, a sign).
3. On the **Night** card, press the bulb: **Glow where the pen colour is**. Set how close a colour counts and press **Make it glow**. Every pixel near that colour is copied onto a **Night light** layer, in every frame and every direction.

You can also make any layer a light map yourself: select it and press the bulb on the **Layers** card. Its pixels glow at night in the colours you paint them, and they aren't part of the daytime picture. Paint, erase and fill it like any other layer.

Turn on the **night view** (the moon on the **View** card) to see the drawing as the zoo shows it after dark: the item darkened and blue, its light map glowing over it.

## Flicker and flashing

Each frame can have its own light map: a flame that flickers, a sign that flashes, a beacon that turns. A frame with nothing on its Night light layer uses the first frame's light map, so a building whose windows stay lit only needs frame 1 painted.

If the item has a separate **used** animation (shops and attractions while guests are inside), its idle light map is used there too unless you paint one for it.

## Cast light around it

A light map makes the item itself glow. To light the ground and things around it, like a street lamp, add a **lamp point**:

1. Choose the **✺** tool (or press **P**) and pick the light's colour as the pen colour.
2. Click where the light comes from, for example the lantern.
3. On the **Night** card, set **Reach** (how many tiles it lights) and **Bright** (the game's street lamp is 1.8).

The game shines the light from that height above the middle of the item, and it turns with the item. The trash button on the Night card takes the lamp point away.

## Save and test

**Save** writes the light maps beside the art, as `<art folder>/light/<direction>_<frame>.png`, and adds a `[lighting]` section to the item's file. Then **Test in the zoo**, place the item and type `lighting 0` in the Test Zoo's console to see it at midnight (`lighting auto` returns to the normal day). In **Play**, switch on **Day and night** and wait for evening.

Players who leave Day and night off see the item exactly as in daylight.

## Writing it by hand

The `[lighting]` section can also be written in the item's `.ai` file:

```ini
[lighting]
lightmap = 1
; x, y (tiles from the middle), height, red, green, blue, brightness, reach
light = 0, 0, 3.88, 255, 226, 159, 1.8, 5
```

`light` can be repeated, one line per light. Each light map is a PNG with the art's ground point at the middle of the picture.
