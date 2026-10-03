# ZT1 Terrain Configuration - tiletex.cfg

**Source:** `terrain.ztd/terrain/tiletex.cfg`

This is the **authoritative terrain type mapping** from the original Zoo Tycoon 1 game files.

## Terrain Type Definitions

| Type ID | Config Section | Texture File | Icon Path | Help ID | Cost | Notes |
|---------|----------------|--------------|-----------|---------|------|-------|
| 0 | ttGrass | terrain/grass.tga | terrain/icgrass/icgrass | 3365 | 40 | Default terrain |
| 1 | ttSavannahGrass | terrain/grass_sv.tga | terrain/icgrs_sv/icgrs_sv | 3366 | 70 | |
| 2 | ttSand | terrain/sand.tga | terrain/icsand/icsand | 3367 | 30 | |
| 3 | ttDirt | terrain/dirt.tga | terrain/icdirt/icdirt | 3368 | 20 | |
| 4 | ttForestFloor | terrain/ffloor.tga | terrain/icffloor/icffloor | 3369 | 50 | Rainforest |
| 5 | ttBrownRock | terrain/bwnrock.tga | terrain/icbnrock/icbnrock | 3370 | 60 | |
| 6 | ttGrayRock | terrain/gryrock.tga | terrain/icgrock/icgrock | 3371 | 60 | |
| 7 | ttGravel | terrain/gravel.tga | terrain/icgravel/icgravel | 3372 | 50 | |
| 8 | ttSnow | terrain/snow.tga | terrain/icsnow/icsnow | 3373 | 100 | |
| 9 | ttFreshWater | terrain/water.tga | terrain/icwater/icwater | 3374 | 30 | water=1 |
| 10 | ttSaltWater | terrain/depwater.tga | terrain/icdpwatr/icdpwatr | 3375 | 60 | water=2 |
| 11 | ttDeciduousForestFloor | terrain/ffloord.tga | terrain/icfflord/icfflord | 3376 | 80 | |
| 12 | ttWaterfall | terrain/bogus.tga | terrain/icbogus/icbogus | 3377 | 100 | |
| 13 | ttConiferousForestFloor | terrain/ffloorc.tga | terrain/icfflorc/icfflorc | 3378 | 80 | |
| 14 | ttConcrete | terrain/ccrete.tga | terrain/icccrete/icccrete | 3379 | 20 | blend=0 |
| 15 | ttAsphalt | terrain/aphalt.tga | terrain/icaphalt/icaphalt | 3380 | 10 | blend=0 |
| 16 | ttTrampled | terrain/worn.tga | terrain/icbogus/icbogus | 3381 | 30 | |

## File Structure in terrain.ztd

```
terrain.ztd/
├── terrain/
│   ├── tiletex.cfg          # This config file (terrain type definitions)
│   ├── tilevar.cfg          # Lighting/material settings
│   │
│   ├── grass.tga            # Full terrain textures (for 3D view)
│   ├── grass_sv.tga
│   ├── sand.tga
│   ├── ... (other .tga files)
│   │
│   ├── icgrass/             # Icon sprites (for isometric rendering)
│   │   ├── icgrass.ani      # Animation config (text INI)
│   │   ├── icgrass.pal      # Palette (256 colors, 1024 bytes)
│   │   └── N                # Actual sprite data (FATZ format)
│   │
│   ├── icsand/
│   │   ├── icsand.ani
│   │   ├── icsand.pal
│   │   └── N
│   │
│   └── ... (other ic* directories)
```

## Animation Config (.ani) Format

The .ani files are **text INI files**, not binary:

```ini
[animation]
dir0 = terrain
dir1 = icgrass
animation = N
x0 = -22
y0 = -16
x1 = 22
y1 = 16
```

- `dir0`, `dir1`: Path components to find the sprite
- `animation`: Filename of sprite data (usually "N")
- `x0, y0, x1, y1`: Bounding box offsets

## Sprite Data ('N' files) Format

Magic: `FATZ` (0x46 0x41 0x54 0x5A)

Header structure (partially decoded):
```
Offset 0x00: "FATZ" magic
Offset 0x04: Unknown (zeros)
Offset 0x08: Unknown value
Offset 0x0C: Unknown value
Offset 0x10: Palette path (null-terminated string)
              e.g., "terrain/icgrass/icgrass.pal"
...
Offset 0x30+: Width (2 bytes) = 0x20 (32)
              Height (2 bytes) = 0x2C (44)
              ... sprite pixel data
```

## tilevar.cfg - Lighting Settings

```ini
[blend]
blend=1

[mtrl.diffuse]
R=1.0, G=1.0, B=1.0, A=1.0

[mtrl.ambient]
R=1.0, G=1.0, B=1.0, A=1.0

[light0.diffuse]
R=0.7, G=0.7, B=0.7, A=1.00

[light0.ambient]
R=0.0, G=0.0, B=0.0, A=1.00

[light0.direction]
X=-1, Y=-10, Z=0

[light1.diffuse]
R=0.4, G=0.4, B=0.4, A=1.00

[light1.ambient]
R=0.0, G=0.0, B=0.0, A=1.00

[light1.direction]
X=1, Y=-1, Z=0
```

## Special Properties

- **water=1**: Fresh water (type 9)
- **water=2**: Salt water (type 10)
- **blend=0**: No edge blending (Concrete=14, Asphalt=15)

## Nibble Encoding

In .zoo map files, terrain bytes use nibble encoding:
- **Low nibble (bits 0-3)**: Terrain type (0-15, wraps for type 16)
- **High nibble (bits 4-7)**: Flags (0x00=natural, 0xF0=player-placed, etc.)

Example: `0xF4` = Type 4 (Forest Floor) + Flag 0xF0 (player-placed)
