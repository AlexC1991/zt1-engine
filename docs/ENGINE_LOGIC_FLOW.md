# Engine Logic Flow & Initialization Sequence

This document archives the execution order required for a 1:1 engine remake.

## 1. Startup & Resource Binding
The engine must initialize systems in this documented priority:
1. **FileSystem Setup:** Mount `.ztd` archives using the priority rules in `RESOURCE_ARCHITECTURE.md`.
2. **String Table Loading:** Load `lang.dll` to populate the global string cache.
3. **Config Parsing:** Read `terrain/tiletex.cfg` to build the `TerrainRegistry`.

## 2. World Creation Sequence
When a map is loaded, the 'Creator' logic follows these steps:
1. **Header Analysis:** Extract Map Type and Base Terrain from the `.zoo` header.
2. **Grid Allocation:** Allocate a `dim x dim` array of 10-byte tile structures.
3. **Remapping Pass:** Apply the remapping rules (e.g., Excavation 247 -> Asphalt) before first render.

## 3. The Main Loop (Per Frame)
1. **Input Processing:** Camera movement and UI interaction.
2. **Simulation Update:** Update entity attributes (Hunger, Happiness) using `ENTITY_ATTRIBUTES.md` constants.
3. **Visibility Culling:** Determine which isometric tiles are within the screen frustum.
4. **Z-Order Rendering:** Draw layers from back-to-front as defined in `GLOBAL_ENGINE_CONFIG.md`.
