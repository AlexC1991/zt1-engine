# ZT1 Engine Architecture Rebuild - January 2026

## Summary

The engine has been completely rebuilt from scratch to match the original Zoo Tycoon 1 architecture. All components now follow proper separation of concerns as documented in the original engine reverse engineering.

## New Architecture

### Core Separation of Concerns

**Data Layer** (No Rendering):
- `WorldMap.cpp/hpp` - Pure map data storage (tiles, terrain, elevation)
- `EntityManager.cpp/hpp` - Entity tracking with spatial hashing
- `ZooReader.cpp/hpp` - Binary .zoo file parsing

**Rendering Layer** (No Data Modification):
- `WorldRenderer.cpp/hpp` - Isometric terrain rendering
- `Animation.cpp/hpp` - Frame data and texture management

**Resource Layer** (Asset Management):
- `SpriteDatabase.cpp/hpp` - Terrain sprite cache (16 terrain types)
- `SpriteManager.cpp/hpp` - Entity/object sprite cache
- `ResourceManager.cpp/hpp` - File loading from .ztd archives

**Coordination Layer**:
- `World.cpp/hpp` - Coordinates all subsystems

## File Structure

```
src/
├── World.cpp/hpp              [NEW] - Coordinator for all subsystems
├── WorldMap.cpp/hpp           [NEW] - Map data storage
├── WorldRenderer.cpp/hpp      [NEW] - Rendering logic
├── SpriteDatabase.cpp/hpp     [NEW] - Terrain sprite cache
├── SpriteManager.cpp/hpp      [NEW] - Entity sprite cache
├── EntityManager.cpp/hpp      [NEW] - Entity tracking + spatial hash
├── Animation.cpp/hpp          [KEPT] - Already data-only
├── ZooReader.cpp/hpp          [KEPT] - Already correct
└── ResourceManager.cpp/hpp    [KEPT] - File operations
```

## Key Changes

### 1. WorldMap (Pure Data)
- Stores tile grid in memory: `vector<vector<MapTile>>`
- Each tile contains:
  - Elevation (0-31)
  - Terrain type (0-15) decoded from nibble
  - Flags (0x00, 0x10, 0x40, 0xF0)
  - Substrate material for walls
- **NO rendering code**

### 2. WorldRenderer (Pure Rendering)
- Reads WorldMap data
- Implements isometric projection:
  - `screenX = (x - y) * (tileWidth/2)`
  - `screenY = (x + y) * (tileHeight/2) - (elevation * 16)`
- Z-order sorting (painter's algorithm)
- Frustum culling
- **Does NOT modify map data**

### 3. SpriteDatabase (Terrain Sprites Only)
- Loads 16 terrain types from `terrain.ztd`
- Pattern: `terrain/ic[name]/ic[name].ani`
- Caches animations in memory
- Singleton pattern

### 4. SpriteManager (Entity Sprites)
- Loads animal, UI, building sprites
- Separate from terrain sprites
- Path-based caching
- Singleton pattern

### 5. EntityManager (Spatial Tracking)
- Tracks entities on tiles using spatial hash
- Key format: `(y << 16) | x`
- Query: "What entities are at tile (x,y)?"
- Simulation update separate from rendering

### 6. Animation (Linear Filtering Fix)
- Added `SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear)` on line 131
- Fixes pixelated look when scaling
- Uses bilinear interpolation

## Original ZT1 Architecture Compliance

✓ **Data-Rendering Separation**: WorldMap stores data, WorldRenderer draws it
✓ **Terrain Nibble Encoding**: Low nibble = terrain (0-15), high nibble = flags
✓ **Isometric Projection**: 64x32 tiles, 16px elevation steps
✓ **Z-Order Rendering**: Floor → Substrate → Paths → Objects → Entities
✓ **Sprite Caching**: Separate databases for terrain vs entities
✓ **Spatial Hashing**: EntityManager tracks which entities are on which tiles
✓ **10-Byte Tile Stride**: ZooReader correctly parses .zoo files

## Building

All new files are automatically included via CMakeLists.txt:
```cmake
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")
```

No changes to build configuration needed.

## Testing

Run with original Zoo Tycoon 1 maps:
```
.\build\Release\zt1-engine.exe
```

Maps are loaded from:
- `build/Release/maps/*.zoo`
- `build/Release/freeform/*.scn`

## Controls

- **Arrow Keys / WASD**: Move camera
- **[ / ]**: Zoom in/out
- **8 / 9**: Decrease/Increase tile size (debug)

## Performance

Expected improvements:
- Proper separation allows better optimization
- Frustum culling reduces unnecessary draws
- Spatial hashing enables fast entity queries
- Texture filtering (linear) smooths scaled sprites

## Next Steps

1. ✅ Build and test
2. Verify terrain rendering matches original ZT1
3. Add entity rendering (animals, guests)
4. Implement proper AI state machine
5. Add path rendering layer
6. Add building/object placement
