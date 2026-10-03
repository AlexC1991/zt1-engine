# ZT1 Engine - Source Directory Structure

Complete reference for all source files in the ZT1 Open Engine remake.

---

## 📁 Core Architecture Files

### World Coordination

**World.cpp / World.hpp**
- **Purpose**: Main coordinator class that orchestrates all subsystems
- **Role**: Glue layer connecting WorldMap, WorldRenderer, EntityManager, etc.
- **Responsibilities**:
  - Initialize all subsystems (SpriteDatabase, SpriteManager)
  - Load maps via ZooReader → WorldMap → EntityManager pipeline
  - Handle camera input (WASD, zoom, debug controls)
  - Coordinate update loop (simulation) and draw loop (rendering)
  - Manage global state (zoom level, camera position)
- **Key Methods**:
  - `loadScenario()` / `loadFreeform()` - Load .zoo/.scn maps
  - `update()` - Simulation tick (entities, input)
  - `draw()` - Rendering frame (terrain, entities)
- **Architecture**: Owns instances of WorldMap, WorldRenderer, EntityManager

---

## 📁 Data Layer (Pure Storage, No Rendering)

### WorldMap.cpp / WorldMap.hpp
- **Purpose**: Store map tile data (terrain, elevation, objects)
- **Role**: Pure data container following original ZT1 tile grid structure
- **Data Structure**:
  ```cpp
  struct MapTile {
      uint8_t elevation;      // Height (0-31)
      uint8_t terrainRaw;     // Raw byte from .zoo file
      uint8_t terrainType;    // Decoded terrain ID (0-15)
      uint8_t terrainFlags;   // Decoded flags (0x00, 0x10, 0xF0)
      uint8_t substrate;      // Wall material for cliffs
      uint16_t objectId;      // Future: building placement
  };
  ```
- **Storage**: 2D vector `vector<vector<MapTile>>`
- **Key Methods**:
  - `loadFromZooReader()` - Parse ZooReader data into tile grid
  - `getTile(x, y)` - Access tile at coordinates
  - `decodeTerrainByte()` - Nibble-based terrain decoding
  - `getSubstrateMaterial()` - Calculate wall material from floor
- **Architecture**: Read by WorldRenderer, never modified during rendering

### EntityManager.cpp / EntityManager.hpp
- **Purpose**: Track all dynamic entities (animals, guests, staff)
- **Role**: Entity lifecycle, spatial tracking, AI simulation
- **Data Structure**:
  ```cpp
  struct Entity {
      int id;                    // Unique ID
      EntityType type;           // Animal/Guest/Staff/Scenery
      EntityState state;         // Idle/Walking/Eating/Drinking
      float x, y;                // World position
      int elevation;             // Height level
      std::string animationPath; // Sprite path
      Animation* currentAnimation;
      int facingDirection;       // 0-7 (N, NE, E, etc.)
      float hunger, thirst, happiness; // Simulation
  };
  ```
- **Spatial Hash**: `unordered_map<int, vector<int>>` - tile → entity IDs
  - Key format: `(y << 16) | x` (packed coordinates)
  - Query: "What entities are at tile (x, y)?"
- **Key Methods**:
  - `addEntity()` / `removeEntity()` - Manage entity lifecycle
  - `update()` - Simulate AI (hunger, movement, state transitions)
  - `draw()` - Render entities at isometric positions
  - `getEntitiesAtTile()` - Spatial query using hash
- **Architecture**: Separate from map data, updated independently

### ZooReader.cpp / ZooReader.hpp
- **Purpose**: Parse binary .zoo map files
- **Role**: Extract raw map data (header, tiles, entities)
- **Binary Format**:
  ```
  Header (44 bytes):
    0x00-0x03: Magic "TZFB"
    0x0C-0x0F: Map Width (uint32)
    0x10-0x13: Map Height (uint32)
    0x20-0x23: Base Terrain ID (uint32)
    0x24-0x27: Map Type (uint32)

  Tile Data (10 bytes per tile, row-major):
    Byte 0: Elevation (0-31)
    Byte 1: Terrain [FLAGS][TYPE] nibble-encoded
    Byte 2: Tile flags (walkable, buildable)
    Byte 3: Water depth
    Bytes 4-9: Entity/object data
  ```
- **Key Methods**:
  - `load()` - Parse binary buffer into memory
  - `getTile(x, y)` - Access raw tile data
  - `getMapWidth/Height()` - Dimensions
  - `getBaseTerrainId()` - Default terrain for ID 0x00
- **Architecture**: Used once during map load, data copied to WorldMap

---

## 📁 Rendering Layer (Pure Visualization, No Data Modification)

### WorldRenderer.cpp / WorldRenderer.hpp
- **Purpose**: Render isometric terrain using painter's algorithm
- **Role**: Read WorldMap data and draw to screen using SpriteDatabase textures
- **Isometric Math** (ZT1 Standard):
  ```cpp
  screenX = (tileX - tileY) * (tileWidth/2) + cameraX + screenCenterX
  screenY = (tileX + tileY) * (tileHeight/2) + cameraY + screenCenterY - (elevation * 16)
  ```
- **Rendering Pipeline**:
  1. Collect visible tiles (frustum culling)
  2. Calculate screen positions using isometric projection
  3. Sort by depth: `sortKey = tileX + tileY` (painter's algorithm)
  4. Draw floor layer (terrain textures)
  5. Draw substrate layer (cliff walls for elevation gaps)
- **Camera**:
  ```cpp
  struct Camera {
      int x, y;              // World space offset
      float zoom;            // Zoom level (1.0 = normal)
      int screenCenterX, Y;  // Screen center point
  };
  ```
- **Key Methods**:
  - `renderTerrain()` - Main rendering loop
  - `tileToScreen()` - Coordinate conversion
  - `setCamera()` - Update viewport
  - `setTileSize()` - Debug tile scaling
- **Architecture**: Reads WorldMap and SpriteDatabase, never modifies data

### Animation.cpp / Animation.hpp
- **Purpose**: Store animation frame data and handle texture management
- **Role**: Container for directional sprite frames (N, NE, E, SE, S, SW, W, NW)
- **Data Structure**:
  ```cpp
  unordered_map<string, vector<SDL_Texture*>> textures;
  unordered_map<string, vector<SDL_Surface*>> surfaces;
  ```
- **Frame Data**:
  - Loaded from .ani files via AniFile parser
  - Stores palette-mapped pixel data
  - Converts to SDL_Texture on first render
  - **Linear filtering enabled** (fixes pixelated scaling)
- **Key Methods**:
  - `draw(renderer, rect, direction)` - Render current frame
  - `queryTexture()` - Get frame dimensions
  - `hasFrames()` - Check if direction exists
  - `loadSurfaces()` - Parse .ani data into surfaces
- **Architecture**: Pure data container, rendering logic minimal

---

## 📁 Resource Layer (Asset Management)

### SpriteDatabase.cpp / SpriteDatabase.hpp
- **Purpose**: Cache and manage terrain sprite textures
- **Role**: Singleton database for 16 terrain types
- **Terrain Sprites**:
  ```
  ID  | Name           | Path
  ----|----------------|---------------------------
  0   | Grass          | terrain/icgrass/icgrass.ani
  1   | Savannah       | terrain/icsavannah/icsavannah.ani
  2   | Sand           | terrain/icsand/icsand.ani
  3   | Dirt           | terrain/icdirt/icdirt.ani
  4   | Rainforest     | terrain/icrforest/icrforest.ani
  5   | Brown Stone    | terrain/icbrnston/icbrnston.ani
  6   | Gray Stone     | terrain/icgryston/icgryston.ani
  7   | Gravel         | terrain/icgravel/icgravel.ani
  8   | Snow           | terrain/icsnow/icsnow.ani
  9   | Fresh Water    | terrain/icwater/icwater.ani
  10  | Salt Water     | terrain/icswater/icswater.ani
  11  | Deciduous      | terrain/icdfloor/icdfloor.ani
  12  | Waterfall      | terrain/icwfall/icwfall.ani
  13  | Conifer        | terrain/iccfloor/iccfloor.ani
  14  | Concrete       | terrain/icconcret/icconcret.ani
  15  | Asphalt        | terrain/icasphalt/icasphalt.ani
  ```
- **Cache**: `unordered_map<int, Animation>` - terrainId → Animation
- **Key Methods**:
  - `init()` - Connect to ResourceManager
  - `loadDefinitions()` - Load all 16 terrain sprites
  - `getTerrainSprite(id)` - Query cached sprite
- **Architecture**: Singleton, initialized at startup

### SpriteManager.cpp / SpriteManager.hpp
- **Purpose**: Cache and manage entity/object sprite textures
- **Role**: Singleton database for animals, UI, buildings, scenery
- **Archive Mapping**:
  - `animals/` → `animals.ztd`
  - `ui/` → `ui.ztd`
  - `buildings/` → `objects.ztd`
- **Cache**: `unordered_map<string, Animation>` - path → Animation
- **Key Methods**:
  - `init()` - Connect to ResourceManager
  - `loadAnimation(path)` - Load and cache sprite
  - `preloadCommonSprites()` - Preload frequently-used assets
- **Architecture**: Singleton, lazy-loading on demand

### ResourceManager.cpp / ResourceManager.hpp
- **Purpose**: Centralized file loading from .ztd archives and loose files
- **Role**: Unified interface for all asset access
- **Archive Priority**:
  1. Loose files (highest priority)
  2. Expansion archives (`XPACK1/*.ztd`, `XPACK2/*.ztd`)
  3. Base game archives (`terrain.ztd`, `animals.ztd`, `ui.ztd`)
- **Resource Types**:
  - File content (raw bytes)
  - Textures (SDL_Texture)
  - Music (Mix_Music)
  - Animations (via AniFile)
  - Palettes (color lookup tables)
  - Strings (lang.dll ID → text)
- **Key Methods**:
  - `getFileContent()` - Load raw file data
  - `getAnimation()` - Load .ani sprite
  - `getPallet()` - Load .pal color table
  - `getString()` - Lookup localized string
  - `getPalletManager()` - Access palette system
- **Architecture**: Created at startup, shared across all subsystems

---

## 📁 Supporting Systems

### MemoryManager.cpp / MemoryManager.hpp
- **Purpose**: Custom memory allocation and tracking
- **Role**: Wrap raw pointers for automatic cleanup
- **Features**:
  - `AssetBuffer` - RAII wrapper for file data
  - Memory ownership tracking (World, ResourceManager, Entity)
  - Debug memory leak detection
- **Architecture**: Singleton, used for .zoo file loading

### MemoryTracker.cpp / MemoryTracker.hpp
- **Purpose**: Debug tool for memory profiling
- **Role**: Track allocations by owner category
- **Categories**: World, ResourceManager, Entity, Animation, etc.

### Config.cpp / Config.hpp
- **Purpose**: Load and store game configuration
- **Role**: Parse `config.ini` for game directories
- **Settings**:
  - Game installation path
  - Language selection
  - Resolution
  - Audio settings

### UserProfile.cpp / UserProfile.hpp
- **Purpose**: Manage player save data
- **Role**: Load/save scenarios, progress, zoo layouts
- **Format**: Binary .sav files (ZT1 format compatible)

---

## 📁 File Format Parsers

### AniFile.cpp / AniFile.hpp
- **Purpose**: Parse .ani animation files
- **Role**: Extract directional frames and palette references
- **Format**:
  - Directory-based: `animal/elephant/` contains `n.ani`, `ne.ani`, etc.
  - Each .ani has frame count, timing, palette reference
  - Pixel data stored as palette indices (8-bit)

### IniReader.cpp / IniReader.hpp
- **Purpose**: Parse .ini configuration files
- **Role**: Read key-value pairs for game settings
- **Format**: Standard INI `[Section]` with `key=value`

### PalletManager.cpp / PalletManager.hpp
- **Purpose**: Manage color palettes for paletted graphics
- **Role**: Load .pal files and provide color lookup
- **Format**: 256-color RGB tables
- **Usage**: Convert 8-bit sprite data → 32-bit RGBA

### ZtdFile.cpp / ZtdFile.hpp
- **Purpose**: Parse .ztd compressed archive files
- **Role**: Decompress and extract files from ZT1 archives
- **Format**: Custom compression (similar to ZIP)
- **Archives**: `terrain.ztd`, `animals.ztd`, `ui.ztd`, `sounds.ztd`

### PeFile.cpp / PeFile.hpp
- **Purpose**: Extract resources from original .exe/.dll files
- **Role**: Read embedded strings, cursors, icons from PE format
- **Usage**: Load `lang.dll` for localized text

---

## 📁 UI System

### UiManager.cpp / UiManager.hpp
- **Purpose**: Manage UI state and screen transitions
- **Role**: Coordinate main menu, game UI, dialogs
- **Screens**: MainMenu, LoadScreen, InGame, PauseMenu

### UiLayout.cpp / UiLayout.hpp
- **Purpose**: Container for UI elements
- **Role**: Position and organize buttons, text, images
- **Layout**: Absolute positioning with anchors

### UiButton.cpp / UiButton.hpp
- **Purpose**: Interactive button widget
- **Role**: Handle clicks, hover states, callbacks
- **States**: Normal, Hover, Pressed, Disabled

### UiText.cpp / UiText.hpp
- **Purpose**: Render text labels
- **Role**: Display static/dynamic text with fonts
- **Fonts**: BFG fonts from original game

### UiImage.cpp / UiImage.hpp
- **Purpose**: Display static images
- **Role**: UI backgrounds, icons, decorations

### UiListBox.cpp / UiListBox.hpp
- **Purpose**: Scrollable list widget
- **Role**: Display selectable items (animal list, scenario list)

### UiScrollBar.cpp / UiScrollBar.hpp
- **Purpose**: Scrolling control
- **Role**: Navigate long lists and content

---

## 📁 Input System

### InputManager.cpp / InputManager.hpp
- **Purpose**: Process keyboard/mouse input
- **Role**: Convert SDL events → game actions
- **Actions**:
  - Camera movement (WASD, arrows)
  - Zoom ([ / ])
  - UI clicks
  - Hotkeys
- **Global State**: `extern float g_ZoomLevel` (linked from World.cpp)

---

## 📁 Audio System

### SoundManager.cpp / SoundManager.hpp
- **Purpose**: Play sound effects and music
- **Role**: Interface to SDL_mixer
- **Formats**: WAV, MP3, OGG
- **Channels**: Background music, ambient sounds, UI sounds, animal sounds

---

## 📁 Rendering Utilities

### FontManager.cpp / FontManager.hpp
- **Purpose**: Load and render BFG fonts
- **Role**: Convert ZT1 font format → SDL textures
- **Fonts**: Multiple sizes and styles from original game

### CompassDirection.hpp
- **Purpose**: Enum for 8-way directions
- **Values**: N, NE, E, SE, S, SW, W, NW, G (generic), H (heavy)
- **Usage**: Animation direction lookup

---

## 📁 Game Logic

### ScenarioManager.cpp / ScenarioManager.hpp
- **Purpose**: Manage scenario/freeform game state
- **Role**: Track objectives, zoo rating, finances
- **Modes**: Scenario (goals), Freeform (sandbox)

### LoadScreen.cpp / LoadScreen.hpp
- **Purpose**: Display loading progress
- **Role**: Show progress bar during asset loading
- **Phases**: Archives → Strings → Animations → Palettes

---

## 📁 Entry Point

### main.cpp
- **Purpose**: Application entry point
- **Role**: Initialize SDL, create window, start main loop
- **Lifecycle**:
  1. SDL_Init()
  2. Create window + renderer
  3. Create Config → ResourceManager → World
  4. Load initial map
  5. Main loop: update() → draw() → present()
  6. Cleanup

---

## 📊 Architecture Summary

```
┌─────────────────────────────────────────────┐
│              main.cpp                       │
│         (SDL Init, Main Loop)               │
└──────────────┬──────────────────────────────┘
               │
               ↓
┌──────────────────────────────────────────────┐
│            World.cpp                         │
│      (Coordinator / Orchestrator)            │
├──────────────────────────────────────────────┤
│ ┌──────────────┐  ┌────────────────────┐    │
│ │ WorldMap     │  │ WorldRenderer      │    │
│ │ (Data)       │←─┤ (Rendering)        │    │
│ └──────────────┘  └────────────────────┘    │
│ ┌──────────────┐  ┌────────────────────┐    │
│ │EntityManager │  │ SpriteDatabase     │    │
│ │(Entities)    │  │ (Terrain Textures) │    │
│ └──────────────┘  └────────────────────┘    │
└─────────────┬────────────────────────────────┘
              │
              ↓
┌──────────────────────────────────────────────┐
│         ResourceManager.cpp                  │
│   (File Loading from .ztd Archives)          │
├──────────────────────────────────────────────┤
│ ZtdFile, PeFile, AniFile, PalletManager      │
└──────────────────────────────────────────────┘
```

---

## 🎯 Data Flow Example: Loading a Map

1. **User**: Click "Load Map" → `freeform/africa.scn`
2. **World**: `loadFreeform("freeform/africa.scn")`
3. **ResourceManager**: Resolve path → `maps/africa.zoo`
4. **ResourceManager**: `getFileContent("maps/africa.zoo")` → raw bytes
5. **ZooReader**: `load(buffer)` → parse binary header + tiles
6. **WorldMap**: `loadFromZooReader(reader)` → build tile grid
7. **WorldMap**: Decode terrain nibbles, calculate substrates
8. **EntityManager**: `loadFromZooReader(reader)` → create entities
9. **SpriteDatabase**: Cache terrain sprites (if not already loaded)
10. **World**: Position camera at map center
11. **Main Loop**: Begin update/draw cycle

---

## 🔍 Rendering Pipeline: Drawing a Frame

1. **main.cpp**: `world.draw(renderer)`
2. **World**: Apply zoom scale
3. **WorldRenderer**: `renderTerrain(renderer, worldMap, spriteDB)`
4. **WorldRenderer**: For each tile:
   - Calculate screen position (isometric math)
   - Frustum cull
   - Add to draw list with sort key
5. **WorldRenderer**: Sort draw list (painter's algorithm)
6. **WorldRenderer**: For each sorted tile:
   - Get terrain texture from SpriteDatabase
   - Draw floor sprite
   - Check neighbor elevations
   - Draw cliff walls (substrate layer)
7. **EntityManager**: `draw(renderer, camera)` → render entities
8. **World**: Reset zoom scale
9. **main.cpp**: `SDL_RenderPresent(renderer)`

---

**Total Files**: ~40 source files
**Lines of Code**: ~15,000 (estimated)
**Architecture**: Data-Rendering separation following original ZT1 design
**Language**: C++20
**Graphics**: SDL2 (software rendering, isometric projection)
**Platform**: Windows (cross-platform capable)
