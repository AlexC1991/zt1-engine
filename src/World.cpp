#include "World.hpp"
#include "MemoryManager.hpp"
#include "MemoryTracker.hpp"
#include <SDL2/SDL.h>

// ============================================================================
// WORLD IMPLEMENTATION - Coordinator
// ============================================================================
// Coordinates all subsystems following original ZT1 architecture
// Each subsystem has clear separation of concerns
// ============================================================================

// Global zoom level (used by InputManager)
float g_ZoomLevel = 1.0f;

int World::keyTimer = 0;
bool World::showDebugInfo = false;

World::World(ResourceManager *resourceManager)
    : resourceManager(resourceManager) {
  ZT_MEMORY_CONTEXT(MemoryOwner::World);

  // Initialize sprite systems
  SpriteDatabase::get().init(resourceManager);
  SpriteDatabase::get().loadDefinitions();

  SpriteManager::get().init(resourceManager);

  SDL_Log("[World] World systems initialized");
}

World::~World() {}

bool World::loadScenario(const std::string &path) { return loadFreeform(path); }

bool World::loadFreeform(const std::string &path) {
  SDL_Log("[World] ========================================");
  SDL_Log("[World] Loading map: %s", path.c_str());
  SDL_Log("[World] ========================================");

  // Resolve path: a .scn names its map in [start] savegame=, e.g.
  // freeform/ff01.scn -> maps/default.zoo
  std::string actualPath = path;
  if (actualPath.find(".scn") != std::string::npos) {
    std::string savegame;
    if (IniReader *scn = resourceManager->getIniReader(path)) {
      savegame = scn->get("start", "savegame");
      delete scn;
    }
    if (!savegame.empty()) {
      actualPath = savegame;
    } else {
      // Fallback: same stem under maps/
      actualPath = actualPath.substr(0, actualPath.find(".scn")) + ".zoo";
      if (actualPath.find("freeform/") != std::string::npos) {
        actualPath.replace(actualPath.find("freeform/"), 9, "maps/");
      }
    }
  }
  SDL_Log("[World] Resolved path: %s", actualPath.c_str());

  // Load map file
  int size = 0;
  void *raw_data = resourceManager->getFileContent(actualPath, &size);

  if (!raw_data) {
    SDL_Log("[World] ERROR: Failed to load file!");
    return false;
  }

  SDL_Log("[World] File loaded: %d bytes", size);
  AssetBuffer buffer =
      MemoryManager::get().wrap(raw_data, static_cast<size_t>(size));

  // Parse .zoo file
  if (!zooReader.load(buffer)) {
    SDL_Log("[World] ERROR: ZooReader failed to parse!");
    return false;
  }

  // Load map data into WorldMap
  if (!worldMap.loadFromZooReader(zooReader)) {
    SDL_Log("[World] ERROR: WorldMap failed to load!");
    return false;
  }

  // Load entities
  entityManager.loadFromZooReader(zooReader);

  // Setup camera
  int mapW = worldMap.getWidth();
  int mapH = worldMap.getHeight();

  // Open the map the way the original does: facing the entrance side,
  // centred on the start tile stored in the map header
  Camera &cam = worldRenderer.getCamera();
  cam.zoom = 1.0f;
  worldRenderer.startViewAt(worldMap, zooReader.getStartCameraX(),
                            zooReader.getStartCameraY());

  SDL_Log("[World] === WORLD LOADED ===");
  SDL_Log("[World]   Map: %dx%d tiles", mapW, mapH);
  SDL_Log("[World]   Camera: (%d, %d)", cam.x, cam.y);
  SDL_Log("[World]   Format version: %u, heights %d..%d",
          zooReader.getVersion(), worldMap.getMinHeight(),
          worldMap.getMaxHeight());
  SDL_Log("[World] ========================================");
  SDL_Log("[World] Controls:");
  SDL_Log("[World]   Arrow Keys / WASD: Move camera");
  SDL_Log("[World]   [ / ]: Zoom in/out");
  SDL_Log("[World]   8 / 9: Decrease/Increase tile size");
  SDL_Log("[World]   E: Toggle elevation");
  SDL_Log("[World]   R: Rotate view");
  SDL_Log("[World]   0: Toggle terrain debug colors");
  SDL_Log("[World] ========================================");

  showDebugInfo = true;
  return true;
}

void World::update(const Uint8 *state, float deltaTime) {
  // Sync mouse wheel zoom (modified by InputManager) with camera
  Camera &cam = worldRenderer.getCamera();
  cam.zoom = g_ZoomLevel;

  handleCameraInput(state, deltaTime);
  handleDebugInput(state);

  // Update entities (simulation)
  entityManager.update(deltaTime);
}

void World::handleCameraInput(const Uint8 *state, float deltaTime) {
  Camera &cam = worldRenderer.getCamera();

  // Camera movement (scaled by zoom)
  int scrollSpeed = static_cast<int>(300.0f * deltaTime * (1.0f / cam.zoom));

  if (state[SDL_SCANCODE_LEFT] || state[SDL_SCANCODE_A])
    cam.x += scrollSpeed;
  if (state[SDL_SCANCODE_RIGHT] || state[SDL_SCANCODE_D])
    cam.x -= scrollSpeed;
  if (state[SDL_SCANCODE_UP] || state[SDL_SCANCODE_W])
    cam.y += scrollSpeed;
  if (state[SDL_SCANCODE_DOWN] || state[SDL_SCANCODE_S])
    cam.y -= scrollSpeed;

  // Zoom controls (keyboard brackets) - also sync to g_ZoomLevel
  if (state[SDL_SCANCODE_RIGHTBRACKET]) {
    cam.zoom += 0.01f;
    if (cam.zoom > 3.0f)
      cam.zoom = 3.0f;
    g_ZoomLevel = cam.zoom; // Keep in sync
  }
  if (state[SDL_SCANCODE_LEFTBRACKET]) {
    cam.zoom -= 0.01f;
    if (cam.zoom < 0.2f)
      cam.zoom = 0.2f;
    g_ZoomLevel = cam.zoom; // Keep in sync
  }
}

void World::handleDebugInput(const Uint8 *state) {
  // Throttle key input
  keyTimer++;
  if (keyTimer < 10)
    return;

  // Tile size adjustment
  if (state[SDL_SCANCODE_8]) {
    int w = worldRenderer.getTileWidth();
    int h = worldRenderer.getTileHeight();
    w -= 8;
    h -= 4;
    if (w < 32)
      w = 32;
    if (h < 16)
      h = 16;
    worldRenderer.setTileSize(w, h);
    SDL_Log("[World] Tile size: %dx%d (8=smaller, 9=larger)", w, h);
    keyTimer = 0;
  }

  if (state[SDL_SCANCODE_9]) {
    int w = worldRenderer.getTileWidth();
    int h = worldRenderer.getTileHeight();
    w += 8;
    h += 4;
    if (w > 256)
      w = 256;
    if (h > 128)
      h = 128;
    worldRenderer.setTileSize(w, h);
    SDL_Log("[World] Tile size: %dx%d (8=smaller, 9=larger)", w, h);
    keyTimer = 0;
  }

  // Elevation (heightfield + cliff walls)
  if (state[SDL_SCANCODE_E]) {
    worldRenderer.toggleElevation();
    SDL_Log("[World] Elevation: %s",
            worldRenderer.isElevationEnabled() ? "ON" : "OFF");
    keyTimer = 0;
  }

  // Rotate view 90 degrees
  if (state[SDL_SCANCODE_R]) {
    worldRenderer.rotateView(1);
    keyTimer = 0;
  }

  // Grid: Ctrl+G, as in the original
  if (state[SDL_SCANCODE_G] && (SDL_GetModState() & KMOD_CTRL)) {
    worldRenderer.toggleGrid();
    SDL_Log("[World] Grid: %s", worldRenderer.isGridVisible() ? "ON" : "OFF");
    keyTimer = 0;
  }

  // Terrain Debug Mode
  if (state[SDL_SCANCODE_0]) {
    worldRenderer.toggleTerrainDebug();
    SDL_Log("[World] Terrain Debug Mode: %s",
            worldRenderer.isTerrainDebugEnabled() ? "ON" : "OFF");
    keyTimer = 0;
  }
}

void World::draw(SDL_Renderer *renderer) {
  // Clear screen
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  // The view is centred on the window as it is now (it can be resized)
  Camera &cam = worldRenderer.getCamera();
  int outW = 0, outH = 0;
  if (SDL_GetRendererOutputSize(renderer, &outW, &outH) == 0 && outW > 0) {
    cam.screenCenterX = outW / 2;
    cam.screenCenterY = outH / 2;
  }

  // Apply zoom
  SDL_RenderSetScale(renderer, cam.zoom, cam.zoom);

  // Render terrain layer
  worldRenderer.renderTerrain(renderer, worldMap, SpriteDatabase::get());

  // Render entities layer (same zoom-centred origin as the terrain)
  float zoom = cam.zoom > 0.0f ? cam.zoom : 1.0f;
  entityManager.draw(renderer, cam.x, cam.y,
                     static_cast<int>(cam.screenCenterX / zoom),
                     static_cast<int>(cam.screenCenterY / zoom));

  // Reset scale
  SDL_RenderSetScale(renderer, 1.0f, 1.0f);
}

void World::setCameraPosition(int x, int y) {
  Camera &cam = worldRenderer.getCamera();
  cam.x = x;
  cam.y = y;
}

void World::getCameraPosition(int &x, int &y) const {
  const Camera &cam = const_cast<World *>(this)->worldRenderer.getCamera();
  x = cam.x;
  y = cam.y;
}
