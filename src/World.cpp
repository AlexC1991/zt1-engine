#include "World.hpp"
#include "MemoryManager.hpp"
#include "MemoryTracker.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "SpriteDatabase.hpp"

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
  if (!paused)
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
    rotateView(1);
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
    outputW = outW;
    outputH = outH;
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

// ============================================================================
// IN-GAME HUD HOOKS
// ============================================================================

void World::rotateView(int steps) {
  float u, v;
  worldRenderer.getViewCentre(u, v);
  // Which world spot is centred, so it stays centred after rotating
  int wx, wy;
  bool onMap = worldRenderer.viewTileToWorld(static_cast<int>(std::floor(u)),
                                             static_cast<int>(std::floor(v)),
                                             wx, wy);
  worldRenderer.rotateView(steps);
  if (onMap) {
    // Find the view tile showing (wx, wy) after rotating
    int NU, NV;
    worldRenderer.getViewSize(NU, NV);
    for (int a = 0; a < NU; a++) {
      for (int b = 0; b < NV; b++) {
        int x, y;
        if (worldRenderer.viewTileToWorld(a, b, x, y) && x == wx && y == wy) {
          worldRenderer.centreViewOn(a + 0.5f, b + 0.5f);
          return;
        }
      }
    }
  }
}

void World::zoomStep(int direction) {
  Camera &cam = worldRenderer.getCamera();
  float zoom = direction > 0 ? cam.zoom * 2.0f : cam.zoom * 0.5f;
  cam.zoom = std::clamp(zoom, 0.5f, 1.0f);
  g_ZoomLevel = cam.zoom;
}

void World::rebuildMiniMap(SDL_Renderer *renderer, int w, int h) {
  if (miniMapTexture) {
    SDL_DestroyTexture(miniMapTexture);
    miniMapTexture = nullptr;
  }
  miniMapGeneration = worldMap.getGeneration();
  miniMapRotation = worldRenderer.getViewRotation();
  miniMapW = w;
  miniMapH = h;
  if (w <= 0 || h <= 0 || worldMap.getWidth() == 0)
    return;

  // Colours from ui/miniclr.cfg, by terrain name ([Grass], [Sand], ...)
  auto readColour = [](IniReader *ini, const std::string &section,
                       SDL_Color fallback) {
    if (!ini)
      return fallback;
    std::vector<std::string> c = ini->getList(section, "color");
    if (c.size() < 3)
      return fallback;
    return SDL_Color{(Uint8)std::atoi(c[0].c_str()), (Uint8)std::atoi(c[1].c_str()),
                     (Uint8)std::atoi(c[2].c_str()), 255};
  };
  IniReader *colours = resourceManager->getIniReader("ui/miniclr.cfg");
  SDL_Color terrain[32];
  for (int t = 0; t < 32; t++) {
    std::string name = SpriteDatabase::get().getTerrainName(t);
    if (name == "Waterfall")
      name = "FreshWater";
    terrain[t] = readColour(colours, name, SDL_Color{0, 0, 0, 255});
  }
  SDL_Color pathColour = readColour(colours, "path", SDL_Color{231, 232, 167, 255});
  delete colours;

  // The map as a diamond filling the box, in the current view rotation:
  // across = (u - v + V) / (U + V), down = (u + v) / (U + V)
  int U, V;
  worldRenderer.getViewSize(U, V);
  SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
  if (!surf)
    return;
  SDL_FillRect(surf, nullptr, SDL_MapRGBA(surf->format, 0, 0, 0, 0));
  uint32_t *px = static_cast<uint32_t *>(surf->pixels);
  int pitch = surf->pitch / 4;
  float span = static_cast<float>(U + V);
  for (int py = 0; py < h; py++) {
    for (int pxi = 0; pxi < w; pxi++) {
      float diff = (pxi + 0.5f) / w * span - V; // u - v
      float sum = (py + 0.5f) / h * span;        // u + v
      int u = static_cast<int>(std::floor((sum + diff) * 0.5f));
      int v = static_cast<int>(std::floor((sum - diff) * 0.5f));
      int x, y;
      if (!worldRenderer.viewTileToWorld(u, v, x, y))
        continue;
      SDL_Color c = worldMap.getPathType(x, y) >= 0
                        ? pathColour
                        : terrain[worldMap.getTile(x, y)->terrainType & 31];
      px[py * pitch + pxi] = SDL_MapRGBA(surf->format, c.r, c.g, c.b, 255);
    }
  }
  miniMapTexture = SDL_CreateTextureFromSurface(renderer, surf);
  SDL_FreeSurface(surf);
}

void World::drawMiniMap(SDL_Renderer *renderer, const SDL_Rect &box) {
  if (worldMap.getWidth() == 0)
    return;
  if (!miniMapTexture || miniMapGeneration != worldMap.getGeneration() ||
      miniMapRotation != worldRenderer.getViewRotation() || miniMapW != box.w ||
      miniMapH != box.h)
    rebuildMiniMap(renderer, box.w, box.h);
  if (miniMapTexture)
    SDL_RenderCopy(renderer, miniMapTexture, nullptr, &box);

  // What is on screen, as a rectangle (the original's [ScreenView] colour)
  int U, V;
  worldRenderer.getViewSize(U, V);
  float span = static_cast<float>(U + V);
  float cu, cv;
  worldRenderer.getViewCentre(cu, cv);
  const Camera &cam = worldRenderer.getCamera();
  float zoom = cam.zoom > 0 ? cam.zoom : 1.0f;
  float halfDiff = outputW / zoom * 0.5f / (worldRenderer.getTileWidth() * 0.5f);
  float halfSum = outputH / zoom * 0.5f / (worldRenderer.getTileHeight() * 0.5f);
  float d = cu - cv, s = cu + cv;
  auto toX = [&](float diff) { return box.x + (diff + V) / span * box.w; };
  auto toY = [&](float sum) { return box.y + sum / span * box.h; };
  SDL_Rect view = {static_cast<int>(toX(d - halfDiff)), static_cast<int>(toY(s - halfSum)),
                   static_cast<int>(toX(d + halfDiff) - toX(d - halfDiff)),
                   static_cast<int>(toY(s + halfSum) - toY(s - halfSum))};
  SDL_Rect clip = box;
  SDL_RenderSetClipRect(renderer, &clip);
  SDL_SetRenderDrawColor(renderer, 255, 223, 115, 255);
  SDL_RenderDrawRect(renderer, &view);
  SDL_RenderSetClipRect(renderer, nullptr);
}

void World::miniMapClick(float fx, float fy) {
  int U, V;
  worldRenderer.getViewSize(U, V);
  float span = static_cast<float>(U + V);
  float diff = fx * span - V, sum = fy * span;
  worldRenderer.centreViewOn((sum + diff) * 0.5f, (sum - diff) * 0.5f);
}
