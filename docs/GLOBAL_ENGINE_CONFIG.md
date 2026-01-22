# Global Engine Configuration & World Logic

This document defines the rules for how the world is constructed from raw data.

## 1. Isometric Projection Rules
- **Tile Dimensions:** 64x32 pixels (Standard ZT1 2:1 Isometric ratio).
- **Elevation Step:** 16 pixels per elevation unit (Byte 1 of Tile Data).
- **Coordinate Mapping:** `screenX = (x - y) * 32`, `screenY = (x + y) * 16 - (elevation * 16)`.

## 2. Terrain Byte Encoding (CRITICAL DISCOVERY)

The terrain byte uses **nibble-based encoding**: `[FLAGS][TERRAIN]`

### Byte Format
```
Bit Layout: [F3 F2 F1 F0] [T3 T2 T1 T0]
            High Nibble    Low Nibble
            (Flags)        (Terrain Type)
```

### Decoding Algorithm
```cpp
int terrainType = rawByte & 0x0F;  // Extract low nibble (0-15)
int flags = (rawByte >> 4) & 0x0F; // Extract high nibble (flags)
```

### Terrain Types (Low Nibble 0-15)
| Value | Terrain | Value | Terrain |
|-------|---------|-------|---------|
| 0 | Grass | 8 | Snow |
| 1 | Savannah | 9 | Fresh Water |
| 2 | Sand | 10 | Salt Water |
| 3 | Dirt | 11 | Deciduous Floor |
| 4 | Rainforest | 12 | Waterfall |
| 5 | Brown Stone | 13 | Conifer Floor |
| 6 | Gray Stone | 14 | Concrete |
| 7 | Gravel | 15 | Asphalt |

### Flag Values (High Nibble)
| Flag | Hex | Meaning |
|------|-----|---------|
| 0 | 0x00 | Natural/unmodified terrain |
| 1 | 0x10 | Modified variant (edge blending) |
| 4 | 0x40 | Biome transition variant |
| 5 | 0x50 | Modified variant 3 |
| 6 | 0x60 | Special marker (spawns?) |
| 15 | 0xF0 | Player-placed terrain |

### Example Decodings
| Raw | Hex | Low Nibble | Terrain | High Nibble | Flag |
|-----|-----|------------|---------|-------------|------|
| 0 | 0x00 | 0 | Grass | 0 | Natural |
| 17 | 0x11 | 1 | Savannah | 1 | Modified |
| 68 | 0x44 | 4 | Rainforest | 4 | Transition |
| 85 | 0x55 | 5 | Brown Stone | 5 | Modified |
| 244 | 0xF4 | 4 | Rainforest | 15 | Player-placed |
| 247 | 0xF7 | 7 | Gravel | 15 | Player-placed |
| 255 | 0xFF | 15 | Asphalt | 15 | Player-placed |

### Legacy Documentation (Superseded)
The previous interpretation was incorrect:
| Raw Byte 0 | Old Interpretation | Correct Interpretation |
| :--- | :--- | :--- |
| 0xF7 (247) | "Asphalt" | Gravel (low nibble 7) with player-placed flag |
| 0xFF (255) | Unknown | Asphalt (low nibble 15) with player-placed flag |

## 3. Rendering Priority (Z-Sorting)
To remake the world properly, tiles must be drawn in this specific order:
1. **Floor Layer:** The flat terrain texture.
2. **Substrate Layer:** The 'wall' textures that fill gaps between different elevations.
3. **Path Layer:** Overlays on top of terrain.
4. **Object Layer:** Static scenery, buildings, and fences.
5. **Entity Layer:** Dynamic animals, guests, and staff.
