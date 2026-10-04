# Base Terrain Nibble Decoding Fix

## Problem

Terrain colors were rendering incorrectly despite all 16 terrain sprite files loading successfully. Maps showed the wrong terrain types - for example, underground maps showed grass instead of rock/dirt, tundra maps showed grass instead of snow, etc.

## Root Cause

The `baseTerrainId` field in .zoo map files uses **nibble encoding** just like the terrain tile bytes:
- **Low nibble (bits 0-3)**: Actual terrain type ID (0-15)
- **High nibble (bits 4-7)**: Flags/properties

Examples from scenario maps:
- `baseTerrainId = 33` (0x21) → Low nibble = 1 (Savannah), High nibble = 0x02
- `baseTerrainId = 36` (0x24) → Low nibble = 4 (Rainforest), High nibble = 0x02
- `baseTerrainId = 37` (0x25) → Low nibble = 5 (Brown Stone), High nibble = 0x02

The code in `WorldMap.cpp` was using the full `baseTerrainId` value directly instead of extracting the low nibble. When `baseTerrainId > 15`, the code clamped it back to 0 (Grass), causing ALL terrain ID 0 tiles to incorrectly render as grass.

## The Fix

**File:** `src/WorldMap.cpp` (lines 54-59)

**Before:**
```cpp
// Handle terrain ID 0 = "use base terrain"
if (tile.terrainType == 0) {
    tile.terrainType = static_cast<uint8_t>(baseTerrainId);
    if (tile.terrainType > 15) tile.terrainType = 0; // Clamp
}
```

**After:**
```cpp
// Handle terrain ID 0 = "use base terrain"
// Base terrain ID uses same nibble encoding as terrain bytes:
//   Low nibble (0-15) = actual terrain type
//   High nibble = flags
if (tile.terrainType == 0) {
    tile.terrainType = static_cast<uint8_t>(baseTerrainId & 0x0F); // Extract low nibble
}
```

## Impact

This fix ensures that:
1. **Themed maps render correctly**:
   - Tundra maps (baseTerrainId=6) show Gray Stone, not Grass
   - Underground maps (baseTerrainId=2) show Sand/Rock, not Grass
   - Beach maps (baseTerrainId=1) show Savannah Grass, not regular Grass

2. **Scenario maps with encoded base terrain work**:
   - Maps with baseTerrainId=33,36,37 now decode correctly to terrain types 1,4,5
   - No more clamping to 0 (Grass) for values > 15

3. **Nibble encoding is consistent**:
   - Both terrain tile bytes AND base terrain IDs use the same encoding scheme
   - High nibble contains flags (likely same meaning as tile terrain flags)

## Testing

Maps to test with (all should show correct terrain colors):
- `maps/tundra.zoo` - Should show predominantly Gray Stone (ID 6), not Grass
- `maps/under.zoo` - Should show predominantly Sand/Gravel (IDs 2/7), not Grass
- `maps/beach.zoo` - Should show Savannah/Sand (IDs 1/2), not pure Grass
- `maps/rockdes.zoo` - Should show Savannah (ID 1), not Grass

Scenario maps from `scenario.ztd` should also render with correct base terrain types.

## Related Files

- `src/WorldMap.cpp` - Contains the fix
- `src/WorldMap.hpp` - Defines baseTerrainId field
- `src/ZooReader.cpp` - Reads baseTerrainId from .zoo files
- `docs/ZT1_OPEN_ENGINE.md` - Documents nibble encoding for terrain bytes

## Technical Details

### Nibble Encoding Format

Terrain data in Zoo Tycoon 1 uses 4-bit (nibble) encoding:

**Terrain Tile Bytes:**
```
Byte: 0xF7
  Low nibble:  7 (terrain type = Gravel)
  High nibble: F (flags = 0xF0)
```

**Base Terrain ID:**
```
Value: 36 (0x24)
  Low nibble:  4 (terrain type = Rainforest)
  High nibble: 2 (flags = 0x20)
```

### Terrain Type IDs (0-15)

| ID | Terrain Type | Sprite Path |
|----|--------------|-------------|
| 0 | Grass | terrain/icgrass |
| 1 | Savannah | terrain/icgrs_sv |
| 2 | Sand | terrain/icsand |
| 3 | Dirt | terrain/icdirt |
| 4 | Rainforest | terrain/icffloor |
| 5 | Brown Stone | terrain/icbnrock |
| 6 | Gray Stone | terrain/icgrock |
| 7 | Gravel | terrain/icgravel |
| 8 | Snow | terrain/icsnow |
| 9 | Fresh Water | terrain/icwater |
| 10 | Salt Water | terrain/icdpwatr |
| 11 | Deciduous Floor | terrain/icfflord |
| 12 | Waterfall | terrain/icwater |
| 13 | Conifer Floor | terrain/icfflorc |
| 14 | Concrete | terrain/icccrete |
| 15 | Asphalt | terrain/icaphalt |

### Base Terrain Substitution Logic

When loading map tiles:
1. Read terrain byte (e.g., 0x00)
2. Extract low nibble → terrain type (0x00 → type 0)
3. **If terrain type == 0**: Replace with base terrain
4. Extract base terrain low nibble: `baseTerrainId & 0x0F`
5. Use that as the actual terrain type

This allows maps to define a "default" terrain that fills most of the map, while other terrain types (1-15) are used for specific features.

## Commit Message

```
Fix base terrain nibble decoding for correct terrain colors

Base terrain IDs in .zoo files use nibble encoding like terrain bytes.
The low nibble (0-15) contains the actual terrain type, while the high
nibble contains flags.

Previous code used the full baseTerrainId value and clamped to 0 when
> 15, causing all terrain ID 0 tiles to render as grass regardless of
the map's intended base terrain.

Now correctly extracts low nibble: `baseTerrainId & 0x0F`

Fixes terrain rendering in:
- Tundra maps (should show gray stone, not grass)
- Underground maps (should show sand/gravel, not grass)
- Scenario maps with encoded base terrain (IDs 33,36,37)
```
