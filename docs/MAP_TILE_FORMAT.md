# ZT1 Map Tile Format (.zoo) - Verified Reference

Verified on 2026-10-03:

1. Decoded all shipped maps (base game, Dinosaur Digs, Marine Mania) and
   checked the result against each map's map-select preview.
2. Ran the **original game** (Complete Collection) and screenshotted 30
   freeform maps in-game (default and zoomed-out views, 3x3 grid of camera
   positions per map). Our renders were aligned to those screenshots
   automatically (masked normalised cross-correlation) and compared side by
   side. Tools: `tools/original_compare/`.

Parser: `src/ZooReader.cpp`; geometry: `src/WorldMap.cpp`; drawing:
`src/WorldRenderer.cpp`.

> The previous version of this document was wrong: byte 5 is NOT a climate
> zone, byte 0 is NOT terrain, terrain 0 does NOT mean "use base terrain",
> header 0x20 is NOT a base terrain id, and the grid does NOT start at 0x64.

## Header

| Offset (v < 82) | Offset (v >= 82) | Field |
|-----------------|------------------|-------|
| 0x00 | 0x00 | Magic `TZFB` |
| 0x04 | 0x04 | Format version: 70/71 base, 82/83 Dinosaur Digs, 104-106 Marine Mania |
| 0x08 | 0x08 | 1033 (locale id) |
| - | 0x0C | Extra uint32 (0 or 2) |
| 0x0C | 0x10 | Width |
| 0x10 | 0x14 | Height |
| 0x14 | 0x18 | **Start camera tile x** |
| 0x18 | 0x1C | **Start camera tile y** |

The start camera fields predict where the original game opens the map
(e.g. beach (4, 42) and tundra (28, 42) sit at their entrances). There is no
view-rotation field. Instead the game turns the view so the map edge nearest
the start tile (the entrance side) faces the lower left of the screen:

| Nearest edge | View rotation (`WorldRenderer`) | Verified on |
|--------------|---------------------------------|-------------|
| x = 0 | 0 | 22 of 30 captured maps (start view within ~60 px) |
| y = 0 | 1 | under.zoo (alignment 0.48 -> 0.70), smcages.zoo |
| x = max | 2 | (no captured map; scn24/25 use it) |
| y = max | 3 | airport.zoo (alignment 0.30 -> 0.42) |

`WorldRenderer::startViewAt` applies this when a map loads.

After this comes a variable-length block (scenario exhibit list with
length-prefixed names such as "Exhibit 1", plus per-version fields), so
**the tile grid has no fixed offset**. Observed starts: 0x28 (base freeform),
0x2C (Dino Digs), 0x34 or 0x20D (Marine Mania), up to 0x11FD (med_kids).

The reader finds it as the first run of `width * height` plausible records
(|height| <= 64, terrain <= 17, bytes 7-9 zero). On perfectly flat maps a
start 1-2 bytes early can also pass, so it takes the last valid start within
the first 10-byte window. Each file contains exactly one such grid.

## Which file a map uses

A freeform/scenario `.scn` names its map in `[start] savegame=`, e.g.
`freeform/ff01.scn` -> `maps/default.zoo` ("Basic Grass Map (Small)").
Don't derive the `.zoo` name from the `.scn` name.

## Tile Record (10 bytes, row-major `[y][x]`)

| Byte | Field |
|------|-------|
| 0-3 | Height of vertex (x, y), signed int32 |
| 4 | Slope code: 2-bit raise per corner |
| 5 | Terrain type (the `type` key in `terrain/tiletex*.cfg`) |
| 6 | Cliff flags: 2 bits per edge |
| 7-9 | Always 0 |

Row-major and unmirrored: the pad numbers painted on `lunar.zoo` read
correctly when plotted with x right, y down.

### Corner heights (hills and pits)

Byte 4 bit pairs: bits 0-1 = vertex (x, y), 2-3 = (x, y+1),
4-5 = (x+1, y+1), 6-7 = (x+1, y). Each corner's height is

```
corner = height + raise[corner] - raise[(x, y)]
```

With this rule shared vertices agree **exactly** across neighbouring tiles
on crater, highland, cratlake, lvalley and dinolrg (0 mismatches).

The game's own slope cursors in `tiles.ztd` confirm it. They are named by
slope code (`0000`, `1100`, `2101`, ...), drawn at 64x32, with the four
digits being the top, right, bottom and left corners in the default view,
which is byte 4's bit pairs read from high to low. All 19 cursor names are
exactly the 19 raise patterns that occur in the maps.

Negative heights are pits, lake beds and craters (crater, cratlake and
sm_cclif go down to -10..-12).

### Cliffs

Where neighbouring tiles disagree about a shared edge, the map has a cliff.
Drops range from 1 (sand/salt-water banks on beach) to 22 units (rock cliffs
on sm_cclif). Every such edge has byte 6 non-zero on **both** sides, so
byte 6 is the game's cliff/wall flag. (Byte 6 is also set on some edges with
no drop; their meaning is not yet known.)

## Terrain Types (byte 5)

From `terrain/tiletex.cfg` and `XPACK2/terrain6.ztd:terrain/tiletex1.cfg`:

| ID | Name | Ground texture | Blends |
|----|------|----------------|--------|
| 0 | Grass | grass.tga | yes |
| 1 | Savannah Grass | grass_sv.tga | yes |
| 2 | Sand | sand.tga | yes |
| 3 | Dirt | dirt.tga | yes |
| 4 | Forest Floor | ffloor.tga | yes |
| 5 | Brown Rock | bwnrock.tga | yes |
| 6 | Gray Rock | gryrock.tga | yes |
| 7 | Gravel | gravel.tga | yes |
| 8 | Snow | snow.tga | yes |
| 9 | Fresh Water | water.tga | yes |
| 10 | Salt Water | depwater.tga | yes |
| 11 | Deciduous Forest Floor | ffloord.tga | yes |
| 12 | Waterfall | bogus.tga | yes |
| 13 | Coniferous Forest Floor | ffloorc.tga | yes |
| 14 | Concrete | ccrete.tga | no (`blend=0`) |
| 15 | Asphalt | aphalt.tga | no (`blend=0`) |
| 16 | Trampled | worn.tga | yes |
| 17 | Gunnite (Marine Mania) | gunnite.tga | yes |

The `icon` entries in the same config are toolbar buttons, not terrain.

## How the Original Draws Terrain (measured)

| Aspect | Original game | Evidence |
|--------|---------------|----------|
| Projection | 2:1 isometric, 64x32 tiles; zoomed out 32x16 | Texture repeat period at both zooms |
| Default view | Rotation 0: world x = 0 edge faces the lower left; each map opens rotated so its entrance edge faces the lower left (see Header) | Tundra/beach entrances; camera fields; slope cursors; under/airport alignment |
| Height | 16 px per unit at 64x32 | `1000.bmp` cursor raises a corner 16 px; under.zoo pit depth |
| Ground texture scale | A 128 px texture repeats every **3 tiles** (256 px water textures assumed 6, same texel density; not measured) | Autocorrelation of sand at default and zoomed-out views |
| Transitions | Per-vertex blend: each vertex mixes the terrains of its four tiles, so a change fades over one tile each side | Shorelines at default zoom; `blend=1` in `terrain/tilevar.cfg` |
| Concrete, asphalt | Hard edges, no blending | `blend=0` |
| Ground shading | Smooth (per vertex); factor = 0.964 - 0.054*nx + 0.118*ny of world normal | Regression of 13.6M screenshot pixels against our normals |
| Cliff faces | Concrete texture, x0.714 (face toward lower right), x0.435 (toward lower left) | Wall colours are a constant multiple of `ccrete.tga` on under and sm_cclif |
| Map border | Nothing drawn past the edge (black) | All edge views |

`terrain/tilevar.cfg` also lists two D3D directional lights (0.7 at
(-1,-10,0) and 0.4 at (1,-1,0)). Taken literally they darken shadowed slopes
much more than the game does, so the renderer uses the fitted factor above.

## Still Unknown

- Byte 6 values on edges without a height drop
- Cliff-face shading in the other three view rotations (only the default
  view was measured; the renderer keeps the same screen-relative factors)
- The object section after the grid (count + length-prefixed type names)
