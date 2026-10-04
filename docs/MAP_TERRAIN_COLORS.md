# ZT1 Map Terrain Colors Reference

## Key Finding: Base Terrain Substitution

When a tile has **terrain type 0**, it doesn't mean "Grass" - it means **"use the map's base terrain"**.

The base terrain is stored at **offset 0x20** in the .zoo file header.

## under.zoo (Underground/City Map)

```
Base Terrain: 2 (Sand)
Texture: sand.tga
Expected Color: #F4D03F (Yellow/Tan)

Terrain Distribution:
- 72.9% Type 0 -> Actually SAND (base terrain)
- 23.1% Type 7 -> Gravel (gray)
- 1.9%  Type 10 -> Salt Water (dark blue)
- 0.6%  Type 15 -> Asphalt (dark gray)
```

**The underground map should appear predominantly SAND colored (yellow/tan), NOT grass green!**

## Other Map Base Terrains

| Map | Base Terrain | Type ID | Color |
|-----|--------------|---------|-------|
| under.zoo | Sand | 2 | #F4D03F (Yellow/Tan) |
| tundra.zoo | Gray Rock | 6 | #607D8B (Gray) |
| beach.zoo | Savannah Grass | 1 | #8BC34A (Yellow-Green) |
| default.zoo | Savannah Grass | 1 | #8BC34A (Yellow-Green) |

## Terrain Type Color Reference

| ID | Name | Texture | Hex Color | Description |
|----|------|---------|-----------|-------------|
| 0 | Grass | grass.tga | #4CAF50 | Green (but usually substituted) |
| 1 | Savannah Grass | grass_sv.tga | #8BC34A | Yellow-green |
| 2 | Sand | sand.tga | #F4D03F | Yellow/tan |
| 3 | Dirt | dirt.tga | #8B4513 | Brown |
| 4 | Rainforest Floor | ffloor.tga | #2E7D32 | Dark green |
| 5 | Brown Rock | bwnrock.tga | #795548 | Brown rock |
| 6 | Gray Rock | gryrock.tga | #607D8B | Gray |
| 7 | Gravel | gravel.tga | #9E9E9E | Gray gravel |
| 8 | Snow | snow.tga | #FFFFFF | White |
| 9 | Fresh Water | water.tga | #2196F3 | Blue |
| 10 | Salt Water | depwater.tga | #1565C0 | Dark blue |
| 11 | Deciduous Floor | ffloord.tga | #689F38 | Forest green |
| 12 | Waterfall | bogus.tga | #42A5F5 | Light blue |
| 13 | Coniferous Floor | ffloorc.tga | #33691E | Dark forest green |
| 14 | Concrete | ccrete.tga | #BDBDBD | Light gray |
| 15 | Asphalt | aphalt.tga | #424242 | Dark gray |
| 16 | Trampled | worn.tga | #A1887F | Worn brown |

## Implementation

```cpp
int getActualTerrainType(int tileTerrainType, int baseTerrainId) {
    // Terrain type 0 means "use base terrain"
    if (tileTerrainType == 0) {
        return baseTerrainId & 0x0F;  // Extract low nibble
    }
    return tileTerrainType;
}
```

## Tile Sprite Dimensions

From the .ani files:
```
x0 = -22, y0 = -16, x1 = 22, y1 = 16
```

- **Width:** 44 pixels (from -22 to +22)
- **Height:** 32 pixels (from -16 to +16)
- **Ratio:** ~1.375:1 (close to standard 2:1 isometric)

## Map File Structure (.zoo)

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 4 | Magic "TZFB" |
| 0x0C | 4 | Map Width (tiles) |
| 0x10 | 4 | Map Height (tiles) |
| 0x20 | 1 | Base Terrain ID |
| 0x64 | - | Tile Data Start (10 bytes per tile) |

### Tile Data (10 bytes each)

| Byte | Field |
|------|-------|
| 0 | Terrain (low nibble = type, high nibble = flags) |
| 1 | Elevation (bits 0-4 = height 0-31) |
| 2-9 | Other data (water depth, flags, etc.) |
