# Zoo Tycoon 1 .ZTD File Format Dictionary

## Overview

**.ZTD files** are proprietary archive containers used by Zoo Tycoon 1 to store and compress game assets. They are essentially ZIP archives with a custom structure, containing all the graphics, sounds, configurations, and animations needed for the game to run.

## Technical Specifications

### File Format
- **Format**: Modified ZIP archive with standard compression
- **Extension**: `.ztd` (Zoo Tycoon Data)
- **Compression**: Standard ZIP compression (deflate)
- **Structure**: Hierarchical directory structure within archive
- **Compatibility**: Can be opened with standard ZIP libraries

### Size & Scale
- **Individual ZTD files**: Range from few KB to hundreds of MB
- **Total game assets**: ~200+ MB across all ZTD files
- **File count**: Thousands of individual assets per archive

## Major ZTD Archives in Zoo Tycoon 1

### Core Game Assets

#### `terrain.ztd` (~913 KB)
**Purpose**: Terrain and landscape graphics
**Contains**:
- Terrain sprites (grass, sand, dirt, water, snow, etc.)
- Isometric tile graphics for ground rendering
- Elevation and transition tiles
- Terrain animations (water flow, etc.)

**Example Files**:
```
terrain/icgrass/           # Grass terrain icons
terrain/icsand/           # Sand terrain icons  
terrain/icwater/           # Water terrain icons
terrain/icsnow/           # Snow terrain icons
terrain/icdirt/           # Dirt terrain icons
terrain/icffloor/         # Forest floor graphics
terrain/icgrs_sv/         # Savannah grass
```

#### `animals.ztd` (~113 MB) - **LARGEST ARCHIVE**
**Purpose**: All animal graphics and animations
**Contains**:
- Animal sprites for all directions (N, NE, E, SE, S, SW, W, NW)
- Animation frames (idle, walking, eating, sleeping, etc.)
- Animal baby graphics
- Species-specific graphics (lions, elephants, giraffes, etc.)

**Example Structure**:
```
animals/Lion/idle/          # Lion idle animations
animals/Lion/walk/          # Lion walking animations
animals/Lion/eat/           # Lion eating animations
animals/Lion/sleep/         # Lion sleeping animations
animals/Elephant/           # Complete elephant graphics
animals/Giraffe/            # Complete giraffe graphics
animals/[species]/[action]/  # Pattern for all animals
```

#### `ui.ztd` (~8 MB)
**Purpose**: User interface graphics and layouts
**Contains**:
- Menu backgrounds and buttons
- UI icons and symbols
- Interface panels and windows
- Font graphics and text elements
- Loading screens

**Example Files**:
```
ui/startup/                 # Main menu graphics
ui/scenario/                # Scenario selection UI
ui/gameopts/                # Game options interface
ui/sharedui/                # Common UI elements
ui/textbck/                 # Text backgrounds
ui/spinup/spindwn/          # Scroll buttons
```

#### `sounds.ztd` (~8 MB)
**Purpose**: Game audio files
**Contains**:
- Animal sounds (roars, calls, etc.)
- Ambient sounds (crowds, environment)
- UI sound effects
- Music files

#### `objects.ztd` (~1.3 MB)
**Purpose**: Zoo objects and decorations
**Contains**:
- Fountains and statues
- Benches and trash cans
- Food courts and restaurants
- Zoo buildings and facilities

### Expansion Assets

#### `ztatb00.ztd` through `ztatb0d.ztd` (Various sizes)
**Purpose**: Dinosaur Digs expansion assets
**Contains**:
- Dinosaur graphics and animations
- Prehistoric terrain and foliage
- Dinosaur-specific buildings
- Expansion UI elements

#### `animals2.ztd` (~7.6 MB)
**Purpose**: Marine Mania expansion animals
**Contains**:
- Marine animal graphics
- Aquarium decorations
- Water-specific animations
- Sea creature behaviors

### Support Archives

#### `scenery.ztd` (~3.5 MB)
**Purpose**: Foliage and environmental decorations
**Contains**:
- Trees and plants
- Rocks and natural formations
- Flowers and bushes
- Seasonal variations

#### `fences.ztd` (~103 KB)
**Purpose**: Enclosure and fence graphics
**Contains**:
- Different fence types (iron, wood, electric)
- Gate and entrance graphics
- Fence variations and damage states

#### `paths.ztd` (~22 KB)
**Purpose**: Visitor path graphics
**Contains**:
- Path textures (stone, dirt, etc.)
- Junction graphics
- Path animations and effects

#### `scenario.ztd` (~2.3 MB)
**Purpose**: Campaign scenario data
**Contains**:
- Scenario objectives and configurations
- Pre-built zoo layouts
- Campaign progress tracking
- Victory conditions

## Internal File Structure

### Animation Files (.ani)
**Purpose**: Configuration files for sprite animations
**Format**: INI-style text files
**Structure**:
```ini
[animation]
x0=0          # Left coordinate of sprite
y0=0          # Top coordinate of sprite  
x1=64         # Right coordinate
y1=64         # Bottom coordinate
animation=N,S,E,W  # Animation directions
```

**Example** (`animals/Lion/Lion.ani`):
```ini
[animation]
x0=0
y0=0
x1=64
y1=64
animation=N,NE,E,SE,S,SW,W,NW
```

### Directory Naming Conventions

#### Terrain
- `ic[terrain]` - Icon graphics for terrain types
- `terrain/icgrass` - Grass terrain icons
- `terrain/icsand` - Sand terrain icons
- `terrain/icwater` - Water terrain icons

#### Animals
- `animals/[Species]/[Action]/` - Animal animations
- Species names: Lion, Tiger, Elephant, Giraffe, etc.
- Actions: idle, walk, eat, sleep, swim, etc.

#### UI Components
- `ui/[category]/[component]/` - UI elements
- Categories: startup, scenario, gameopts, sharedui
- Components: backgrounds, buttons, text, scrollbars

## Reading ZTD Files

### Technical Implementation

The engine uses **libzip** library to access ZTD contents:

```cpp
// Open ZTD archive
zip_t *archive = zip_open("terrain.ztd", 0, NULL);

// List all files in archive
while (zip_stat_index(archive, index, 0, &file_info) == 0) {
    printf("File: %s (%zu bytes)\n", file_info.name, file_info.size);
    index++;
}

// Extract specific file
zip_file_t *file = zip_fopen(archive, "terrain/icgrass/icgrass.ani", 0);
zip_fread(file, buffer, file_size);
zip_fclose(file);

zip_close(archive);
```

### File Access Patterns

#### Direct File Access
```cpp
// Get raw file content
void *content = ZtdFile::getFileContent("terrain.ztd", "terrain/icgrass", &size);

// Get image as SDL_Surface
SDL_Surface *surface = ZtdFile::getImageSurface("terrain.ztd", "terrain/grass.tga");

// Get animation configuration
IniReader *config = ZtdFile::getIniReader("animals.ztd", "animals/Lion/Lion.ani");
```

#### Fallback System
The engine implements a **loose file override** system:
1. Check for file loose in filesystem first
2. If not found, extract from ZTD archive
3. Allows modding and asset replacement

### Asset Loading Workflow

#### Animation Loading
1. **Locate Animation**: Find .ani file in appropriate ZTD
2. **Parse Configuration**: Read sprite coordinates and frame data
3. **Load Graphics**: Extract sprite images from same ZTD
4. **Create Animation**: Build animation object with frames and directions
5. **Cache Result**: Store in SpriteDatabase for fast access

#### Image Loading
1. **Determine Format**: Check file extension (.bmp, .tga, .zt1)
2. **Extract Data**: Get raw bytes from ZTD archive
3. **Decode Format**: Convert to SDL_Surface using appropriate decoder
4. **Apply Palette**: Use correct color palette from game
5. **Return Surface**: Provide to rendering system

## Asset Categories and Uses

### Terrain Graphics (20 Types)
| ID | Terrain Type | ZTD Path | Description |
|----|--------------|------------|-------------|
| 0  | Grass | terrain/icgrass | Standard grass terrain |
| 1  | Sand | terrain/icsand | Desert and sandy areas |
| 2  | Dirt | terrain/icdirt | Bare earth terrain |
| 3  | Grey Rock | terrain/icgrock | Rocky mountain terrain |
| 4  | Brown Rock | terrain/icbnrock | Brown rocky terrain |
| 5  | Asphalt | terrain/icaphalt | Paved areas |
| 6  | Concrete | terrain/icccrete | Concrete surfaces |
| 7  | Gravel | terrain/icgravel | Gravel paths |
| 8  | Savannah Grass | terrain/icgrs_sv | African savannah grass |
| 9  | Forest Floor | terrain/icffloor | Forest ground cover |
| 10 | Conifer Floor | terrain/icfflorc | Pine forest floor |
| 11 | Deciduous Floor | terrain/icfflord | Deciduous forest floor |
| 12 | Rainforest Floor | terrain/icrfflor | Jungle floor (missing) |
| 13 | Dino Digs Grass | terrain/icgrs_dd | Prehistoric grass |
| 14 | Dino Digs Grass 2 | terrain/icgrsdd2 | Prehistoric grass variant |
| 15 | Snow | terrain/icsnow | Snow and ice terrain |
| 16 | Ice | terrain/icice | Frozen ice surfaces |
| 17 | Mud | terrain/icmud | Wet muddy terrain |
| 18 | Water | terrain/icwater | Water and rivers |
| 19 | Deep Water | terrain/icdpwatr | Deep ocean water |

### Animal Categories
- **Mammals**: Lions, Tigers, Elephants, Giraffes, Bears
- **Reptiles**: Crocodiles, Snakes, Lizards
- **Birds**: Flamingos, Penguins, Ostriches
- **Marine**: Dolphins, Sharks, Sea Lions (Marine Mania)
- **Dinosaurs**: T-Rex, Triceratops, Stegosaurus (Dinosaur Digs)

### UI Elements
- **Menus**: Main menu, options, scenario selection
- **Buttons**: Standard button graphics and hover states
- **Icons**: Small icons for tools and actions
- **Backgrounds**: Menu and panel backgrounds
- **Text**: Font graphics and text elements

## Modding and Customization

### Adding Custom Assets
1. **Create Directory Structure**: Match ZTD internal structure
2. **Use Loose Files**: Place custom assets in game directory
3. **Override System**: Engine automatically uses loose files over ZTD
4. **Maintain Formats**: Use same image and configuration formats

### Custom ZTD Creation
```bash
# Create custom ZTD from asset directory
zip -r custom_animals.ztd animals/
```

### Asset Replacement
- **Loose Files Override**: Files in game directory override ZTD
- **Path Matching**: Must match exact internal ZTD paths
- **Format Compatibility**: Same formats as original assets

## Debugging and Analysis

### Common Issues
- **Missing Animations**: Some animals have incomplete animation sets
- **Format Variations**: Mix of BMP, TGA, and proprietary .zt1 formats
- **Palette Issues**: Some graphics require specific color palettes
- **Size Limitations**: Large archives may cause loading delays

### Tools for Analysis
- **Standard ZIP Tools**: 7-Zip, WinRAR can extract ZTD files
- **Hex Editors**: For analyzing proprietary formats
- **Image Viewers**: For inspecting extracted graphics
- **Text Editors**: For examining .ani configuration files

## Performance Considerations

### Loading Strategies
- **Lazy Loading**: Load assets only when needed
- **Caching**: Keep frequently accessed assets in memory
- **Compression**: ZTD compression reduces disk space usage
- **Streaming**: Large animations may be streamed from disk

### Memory Management
- **Asset Counting**: Track loaded assets to prevent memory overflow
- **Cleanup**: Properly release unused assets
- **Pooling**: Reuse memory for similar asset types

---

**.ZTD files form the backbone of Zoo Tycoon's asset system, providing a compact, organized way to distribute thousands of game assets while maintaining fast access and modding capabilities. Understanding this format is essential for extending or modifying the game.**



# Zoo Tycoon 1 .ZTD File Format Dictionary

## Technical Specifications
- **Format**: Modified ZIP archive.
- **Extension**: `.ztd` (Zoo Tycoon Data).

## Core Resource Mapping

### Terrain Graphics (Documented Mapping)
Based on `tiletex.cfg` and `lang.dll` reverse engineering:

| ID | Name (from lang.dll) | ZTD Path | Help ID |
|----|----------------------|----------|---------|
| 0  | Grass                | terrain/icgrass  | 3365 |
| 1  | Savannah Grass       | terrain/icgrs_sv | 3366 |
| 2  | Sand                 | terrain/icsand   | 3367 |
| 3  | Dirt                 | terrain/icdirt   | 3368 |
| 14 | Concrete             | terrain/icccrete | 3379 |
| 15 | Asphalt              | terrain/icaphalt | 3380 |

### Archive Priority
The engine uses a specific mounting order to support expansion packs and patches:
1. **Update Archives (.zup):** Highest priority overrides.
2. **Expansion Archives:** `ztatb00.ztd` (Dino Digs), `animals2.ztd` (Marine Mania).
3. **Base Archives:** `terrain.ztd`, `animals.ztd`, `ui.ztd`.

## Internal File Structure

### UI Components
- **IDs 1000-1100:** Main tool buttons.
- **IDs 3000-3999:** Terrain names (linked to Help IDs in `tiletex.cfg`).

### Animation Files (.ani)
- **Format**: INI-style configuration files.
- **Directions**: Standard 8-direction support (N, NE, E, SE, S, SW, W, NW).