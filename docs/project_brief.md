# Zoo Tycoon 1 Engine - Project Brief

## Overview

A modern C++ reimplementation of the classic Zoo Tycoon 1 game engine, built from reverse-engineering the original binary. This project aims to recreate the beloved zoo management simulation with improved performance, modern tooling, and cross-platform compatibility while maintaining 100% compatibility with original game assets.

## Technology Stack

- **Language**: C++20
- **Graphics**: SDL2 (Simple DirectMedia Layer)
- **Build System**: CMake
- **Platform**: Windows (primary), with cross-platform potential
- **Asset Compatibility**: Original Zoo Tycoon 1 files (.zoo, .ztd, .ani, etc.)

## Project Architecture

```
zt1-engine/
├── src/                    # Core engine source code
│   ├── World.cpp           # Terrain rendering and map management
│   ├── ZooReader.cpp       # Binary map file parser
│   ├── EntityManager.cpp   # Animals, guests, objects
│   ├── SpriteDatabase.cpp  # Animation and sprite management
│   └── ResourceManager.cpp # Asset loading system
├── build/                 # Build output directory
├── vendor/                # Third-party dependencies
└── engine-build-resources/ # Build automation scripts
```

## Key Features Implemented

### ✅ Core Engine Systems
- **Map Loading**: Binary .zoo file parsing and terrain rendering
- **Sprite System**: Animation and sprite management from .ztd archives
- **UI Framework**: Menu systems and user interface
- **Entity Management**: Animals, guests, and scenery objects
- **Resource Management**: Asset loading from original game files

### ✅ Advanced Rendering
- **Isometric View**: Classic zoo tycoon perspective
- **Terrain System**: 20 terrain types with proper elevation
- **Camera Controls**: Panning and zoom functionality
- **Performance Optimized**: Efficient culling and rendering

### ✅ Asset Compatibility
- **Original Game Files**: 100% compatible with Zoo Tycoon 1 assets
- **DLC Support**: Dinosaur Digs and Marine Mania expansions
- **Archive Support**: .ztd compressed asset containers
- **Animation System**: .ani format animation playback

## Recent Achievements

### Terrain Rendering Fix (Latest)
- **Problem**: Invalid terrain IDs (254, 98, 109, 464) causing black tiles
- **Solution**: Comprehensive terrain ID mapping system with fallbacks
- **Result**: Smooth terrain rendering across all freeform maps
- **Performance**: Optimized debug logging for better frame rates

## Development Workflow

### Build Commands
- `BUILD_ENGINE.bat V` - Verify build (compile only)
- `BUILD_ENGINE.bat Q` - Quick build and run
- `BUILD_ENGINE.bat F` - Full clean rebuild

### Testing Pipeline
1. **Build Verification**: Ensure code compiles without errors
2. **Map Testing**: Load various freeform maps (.zoo files)
3. **Asset Validation**: Verify sprite and animation loading
4. **Performance Checks**: Monitor frame rate and memory usage

## Current Status

**Version**: Pre-Alpha Development  
**Completion**: ~60% of core systems implemented  
**Focus**: Terrain rendering and basic gameplay systems  

### Working Features
- ✅ Map loading and terrain rendering
- ✅ UI menu systems  
- ✅ Resource management
- ✅ Basic entity framework
- ✅ Camera controls

### In Development
- 🔄 Animal behavior and AI
- 🔄 Guest pathfinding and needs
- 🔄 Zoo economics and management
- 🔄 Building construction system

## Technical Challenges

### Reverse Engineering Complexity
- **Binary Formats**: Decoding proprietary .zoo/.ztd file structures
- **Asset Compatibility**: Maintaining exact visual parity with original
- **Performance**: Optimizing isometric rendering for modern hardware
- **Cross-Platform**: Adapting Windows-centric codebase

### Current Focus Areas
1. **Entity AI**: Implementing animal and guest behavior systems
2. **Economic Systems**: Zoo management and financial mechanics
3. **User Interface**: Building construction and interaction
4. **Performance**: Optimizing for larger maps and more entities

## Future Roadmap

### Short Term (Next 3 Months)
- Complete animal AI and behavior systems
- Implement guest needs and pathfinding
- Add building construction and placement
- Create zoo management UI

### Medium Term (6 Months)
- Multiplayer support prototype
- Custom scenario editor
- Modding framework
- Performance optimization suite

### Long Term (1 Year+)
- Full cross-platform support
- Enhanced graphics options
- Community content platform

## Contributing

This is a reverse-engineering project requiring:
- **C++ expertise**: Modern C++ and graphics programming
- **Game Development**: Understanding of simulation game mechanics
- **Reverse Engineering**: Binary analysis and file format expertise
- **Testing**: Rigorous compatibility testing with original assets

## License & Legal

Project maintained for educational and preservation purposes. Requires ownership of original Zoo Tycoon 1 game for asset compatibility.

---

*"Building the future of classic zoo management, one line of code at a time."*