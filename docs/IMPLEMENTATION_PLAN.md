# ZT1 Engine Remake: Implementation Roadmap

This checklist breaks down the remake into logical milestones based on the reverse-engineered documents.

## Phase 1: The Core Foundation (Infrastructure)
- [ ] **FileSystem Abstraction:** Implement a system to read from ZTD/ZIP archives.
  * *See: RESOURCE_ARCHITECTURE.md*
- [ ] **String Provider:** Create a class to load and cache strings from `lang.dll`.
  * *See: lang.dll.txt*
- [ ] **Terrain Registry:** Build a loader for `tiletex.cfg` to map IDs to assets.
  * *See: TERRAIN_ARCHITECTURE.md*

## Phase 2: World Loading & Rendering (The Visuals)
- [ ] **Map Parser:** Implement the 10-byte stride reader for `.zoo` files.
  * *See: MAP_FILE_FORMAT.md*
- [ ] **Isometric Camera:** Setup the 2:1 projection math and scrolling logic.
  * *See: GLOBAL_ENGINE_CONFIG.md*
- [ ] **Elevation Engine:** Render tiles at variable heights with substrate (wall) textures.
  * *See: GLOBAL_ENGINE_CONFIG.md*

## Phase 3: The Living World (Simulation)
- [ ] **Sprite Database:** Create an animation player for `.ani` files.
- [ ] **AI Loop:** Implement the hunger/thirst/happiness tick logic.
  * *See: ENTITY_ATTRIBUTES.md*
- [ ] **State Machine:** Implement the idle/walk/eat behaviors.
  * *See: OBJECT_BEHAVIOR_LOGIC.md*

## Phase 4: Interface & Final Polish
- [ ] **UI Renderer:** Draw the bottom bar and menus using documented IDs.
  * *See: UI_SYSTEM_ARCHITECTURE.md*
- [ ] **Scenario Logic:** Implement goal-checking based on map headers.

