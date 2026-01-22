# 2025 Remake: Class Hierarchy & Architecture

This document defines the C++ class structure based on the reverse-engineered documentation.

## 1. Data & Resource Classes
- **`ZTStringTable`**: Handles `lang.dll` loading and string lookups.
- **`ZTArchiveManager`**: Manages `.ztd` and `.zup` file priorities.
- **`ZTTerrainRegistry`**: Loads `tiletex.cfg` to map IDs to textures.

## 2. World & Rendering Classes
- **`ZTMapLoader`**: Parses binary headers and 10-byte tile strides.
- **`ZTRenderer`**: Implements isometric projection and elevation logic.
- **`ZTSpriteDatabase`**: Cache for terrain and object animations.

## 3. Simulation & Entity Classes
- **`ZTEntityManager`**: Manages the lifecycle of all objects in the world.
- **`ZTEntityAI`**: Parses `.ai` scripts and manages simulation attributes.
- **`ZTUIManager`**: Recreates the windows and buttons based on documented UI IDs.
