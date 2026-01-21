# Source Code Architecture Briefing

## Overview

This document explains the purpose and functionality of each major source file in the Zoo Tycoon 1 Engine project. The codebase is organized into modular systems that work together to recreate the classic zoo management simulation.

## Core Engine Files

### `main.cpp` - Application Entry Point
**Purpose**: Main application initialization and game loop management
**Responsibilities**:
- SDL2 initialization and window setup
- Resource manager and configuration loading
- Scenario manager setup for map selection
- UI initialization and menu systems
- Game loop coordination and event handling
- Clean shutdown and resource cleanup

### `World.cpp/.hpp` - Game World Management
**Purpose**: Core game world rendering and terrain system
**Responsibilities**:
- Map loading and terrain rendering (isometric view)
- Camera controls (panning, zooming)
- Terrain ID validation and fallback mapping
- Entity drawing coordination
- World-to-screen coordinate conversion
**Recent Achievement**: Fixed invalid terrain ID handling (254, 98, 109, 464 → proper fallbacks)

### `ZooReader.cpp/.hpp` - Binary Map Parser
**Purpose**: Reverse-engineered parser for Zoo Tycoon's proprietary .zoo map format
**Responsibilities**:
- Binary .zoo file structure parsing
- Terrain tile data extraction
- Map dimensions and metadata reading
- Entity object placement data extraction
- Header validation (base terrain ID, map type)
**Key Feature**: Handles invalid terrain IDs found in original game files

### `SpriteDatabase.cpp/.hpp` - Animation & Sprite Management
**Purpose**: Centralized sprite and animation database
**Responsibilities**:
- Loading and caching of terrain sprites (20 types)
- Animal sprite management (Lion, Giraffe, etc.)
- Guest and staff animation handling
- Object and scenery sprite coordination
- Animation frame management
**Current Status**: 15/20 terrain sprites loading successfully

## Asset Management System

### `ResourceManager.cpp/.hpp` - Asset Loading Pipeline
**Purpose**: Unified asset loading from original Zoo Tycoon files
**Responsibilities**:
- .ztd archive file reading (compressed game assets)
- Animation (.ani) file loading
- Resource path resolution and caching
- Asset fallback and error handling
- Integration with original game content

### `AniFile.cpp/.hpp` - Animation Parser
**Purpose**: Parser for Zoo Tycoon's proprietary .ani animation format
**Responsibilities**:
- Binary .ani file structure parsing
- Animation frame sequence extraction
- Sprite sheet coordinate management
- Animation timing and direction handling
- Error recovery for corrupted animations

### `ZtdFile.cpp/.hpp` - Archive Manager
**Purpose**: Handler for Zoo Tycoon's compressed .ztd archive format
**Responsibilities**:
- .ztd archive decompression
- File indexing and lookup
- Asset extraction from archives
- Multi-archive support (terrain.ztd, animals.ztd, ui.ztd)

## Entity System

### `EntityManager.cpp/.hpp` - Entity Lifecycle Management
**Purpose**: Management of all game entities (animals, guests, objects)
**Responsibilities**:
- Entity spawning and removal
- Position tracking and updates
- Entity type management (Animal, Guest, Staff, Object)
- Integration with map data
- Basic AI and behavior coordination

### `Entity.cpp/.hpp` - Base Entity Class
**Purpose**: Base class for all game entities
**Responsibilities**:
- Common entity properties (position, type, animation)
- Generic update and draw interfaces
- Entity state management
- Inheritance foundation for specific entity types

## User Interface System

### `ui/UiLayout.cpp/.hpp` - Layout Management
**Purpose**: UI layout definition and component organization
**Responsibilities**:
- .lyt file parsing (original game UI layouts)
- Component positioning and sizing
- UI hierarchy management
- Layout loading from original game files

### `ui/UiButton.cpp/.hpp` - Button Components
**Purpose**: Interactive button UI elements
**Responsibilities**:
- Button state management (normal, hover, pressed)
- Click event handling
- Visual state transitions
- Text and image rendering

### `ui/UiListBox.cpp/.hpp` - List Components
**Purpose**: Scrollable list UI elements (for scenarios, maps)
**Responsibilities**:
- List item management and display
- Scrolling behavior
- Selection handling
- Item highlighting and navigation

### `ui/UiImage.cpp/.hpp` - Image Components
**Purpose**: Static image display elements
**Responsibilities**:
- Image loading and rendering
- Scaling and positioning
- Animation support for UI sprites
- Transparency and blending

### `ui/UiText.cpp/.hpp` - Text Components
**Purpose**: Text rendering and display
**Responsibilities**:
- Font management and loading
- Text rendering with proper formatting
- Multi-line text support
- Color and style management

### `ui/UiScrollBar.cpp/.hpp` - Scroll Components
**Purpose**: Scrollbar functionality for UI lists
**Responsibilities**:
- Scroll position tracking
- Drag interaction handling
- Visual feedback and states
- Integration with list components

## Support Systems

### `ResourceManager.cpp/.hpp` - Asset Loading Pipeline
**Purpose**: Unified asset loading from original Zoo Tycoon files
**Responsibilities**:
- .ztd archive file reading
- Animation (.ani) file loading
- Resource path resolution
- Asset caching and management

### `Animation.cpp/.hpp` - Animation Data Structure
**Purpose**: Core animation data management
**Responsibilities**:
- Frame sequence storage
- Animation timing control
- Direction-specific animations
- Sprite sheet coordinate management

### `FontManager.cpp/.hpp` - Font System
**Purpose**: Font loading and text rendering support
**Responsibilities**:
- TrueType font loading
- Text rendering coordination
- Font caching and management
- Multi-language support foundation

### `TextManager.cpp/.hpp` - Text Localization
**Purpose**: Game text and string management
**Responsibilities**:
- String localization (.dll language files)
- Text database management
- Multi-language support
- Dynamic text loading

### `Config.cpp/.hpp` - Configuration System
**Purpose**: Game configuration and settings management
**Responsibilities**:
- Configuration file parsing
- Settings validation
- Default value management
- Runtime configuration updates

## Memory Management

### `MemoryManager.cpp/.hpp` - Memory Allocation
**Purpose**: Custom memory allocation system
**Responsibilities**:
- Memory pool management
- Allocation tracking
- Performance optimization
- Leak detection foundation

### `MemoryTracker.cpp/.hpp` - Memory Monitoring
**Purpose**: Real-time memory usage tracking
**Responsibilities**:
- Allocation/deallocation monitoring
- Memory usage statistics
- Leak detection and reporting
- Performance metrics

### `MemoryDumpLog.cpp/.hpp` - Memory Debugging
**Purpose**: Memory state logging and debugging
**Responsibilities**:
- Memory state snapshots
- Allocation history logging
- Debug information output
- Crash analysis support

## Game Content Systems

### `ScenarioManager.cpp/.hpp` - Scenario Management
**Purpose**: Game scenario and map selection
**Responsibilities**:
- Scenario database management
- Freeform map loading
- Campaign scenario handling
- Game mode selection

### `ScenarioDatabase.cpp/.hpp` - Scenario Data
**Purpose**: Scenario metadata and information
**Responsibilities**:
- Scenario information storage
- Difficulty levels and objectives
- Map previews and descriptions
- Compatibility checks

### `UserProfile.cpp/.hpp` - Player Profile
**Purpose**: User profile and save game management
**Responsibilities**:
- Player statistics tracking
- Save game management
- Progress tracking
- User preferences storage

## Utility Libraries

### `IniReader.cpp/.hpp` - Configuration Parser
**Purpose**: INI file parsing utility
**Responsibilities**:
- Game configuration file reading
- Scenario metadata parsing
- Settings file handling
- Key-value pair extraction

### `PeFile.cpp/.hpp` - Executable Parser
**Purpose**: Windows PE file format handling
**Responsibilities**:
- Game executable analysis
- Resource extraction from executables
- Version information reading
- Compatibility checking

### `Utils.cpp/.hpp` - General Utilities
**Purpose**: Common utility functions
**Responsibilities**:
- String manipulation functions
- Mathematical utilities
- Common algorithms
- Helper functions

### `PalletManager.cpp/.hpp` - Color Palette
**Purpose**: Game color palette management
**Responsibilities**:
- Color palette loading
- Palette conversion utilities
- Color mapping support
- Graphics color management

## Graphics Support

### `Window.cpp/.hpp` - Window Management
**Purpose**: SDL2 window creation and management
**Responsibilities**:
- Game window creation
- Display mode handling
- OpenGL context management
- Window events handling

### `InputManager.cpp/.hpp` - Input System
**Purpose**: Input device management
**Responsibilities**:
- Keyboard input handling
- Mouse input processing
- Input mapping and binding
- Event distribution

### `CompassDirection.hpp` - Direction Enumeration
**Purpose**: Standardized direction system
**Usage**: Entity facing directions, animation orientation
**Values**: N, NE, E, SE, S, SW, W, NW (8 compass directions)

## Development Tools

### `test_zoo_reader.cpp` - Testing Utility
**Purpose**: ZooReader functionality testing
**Responsibilities**:
- Map parsing validation
- Terrain data verification
- Performance testing
- Debug output generation

### `LoadScreen.cpp/.hpp` - Loading Interface
**Purpose**: Loading screen management
**Responsibilities**:
- Loading progress display
- Asset loading coordination
- User feedback during loads
- Transition management

## Current Development Focus

**Active Development**: Terrain rendering system
- ✅ Invalid terrain ID mapping completed
- ✅ Performance optimization implemented
- 🔄 Sprite loading improvements in progress
- 🌟 Map validation and testing ongoing

**Next Priority Areas**:
1. Animal behavior and AI systems
2. Guest pathfinding and needs
3. Building construction interface
4. Economic management systems

---

Each component is designed to work together to recreate the authentic Zoo Tycoon experience while modernizing the underlying technology for better performance and maintainability.