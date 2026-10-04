#include "World.hpp"
#include "ItemCatalog.hpp"
#include "Features.hpp"
#include "RenderSettings.hpp"
#include "ArtScaler.hpp"
#include "GpuFsr.hpp"
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
  placedObjects.load(zooReader, resourceManager);
  ambient.load(resourceManager);
  ambient.clear();
  staff.loadTypes(resourceManager);
  staff.clear();
  items.load(resourceManager);
  items.clear();
  staff.setItems(&items);
  staff.setIndex(&index);
  staff.setWalkways(&walkways);
  walkways.clear();
  reindex();
  fences.loadTypes(resourceManager);
  fences.load(zooReader, worldMap);
  fenceTool = -1;
  bulldozer = false;
  dragging = false;

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
  if (this->tracked >= 0) {
    if (this->staff.member(this->tracked))
      this->centreOnStaff(this->tracked);
    else
      this->tracked = -1;
  }

  // Update entities (simulation)
  if (!paused) {
    entityManager.update(deltaTime);
    fences.update(deltaTime);
    ambient.update(deltaTime, worldMap);
    // Where everything is: what stands (now and then), who's about (now)
    reindexIn -= deltaTime;
    if (reindexIn <= 0)
      reindex();
    index.clearMovers();
    staff.addToIndex(index);
    staff.update(deltaTime, worldMap, fences);
    items.update(deltaTime, fences);
  }
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
  // Clear screen (the bars beside a centred view stay black)
  SDL_RenderSetViewport(renderer, nullptr);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  // The map's area: the view set by the game, else the whole window
  Camera &cam = worldRenderer.getCamera();
  int outW = 0, outH = 0;
  SDL_GetRendererOutputSize(renderer, &outW, &outH);
  SDL_Rect view = this->viewRect.w > 0 && this->viewRect.h > 0
                      ? this->viewRect
                      : SDL_Rect{0, 0, outW, outH};
  outputW = view.w;
  outputH = view.h;
  cam.screenCenterX = view.w / 2;
  cam.screenCenterY = view.h / 2;

  // The camera's zoom times the view's scale, while drawing
  const float userZoom = cam.zoom > 0.0f ? cam.zoom : 1.0f;
  const float zoom = userZoom * this->viewScale;
  cam.zoom = zoom;

  // Zoomed in past 1x, GPU FSR draws the map at 1:1 into a layer and
  // upscales it into the view; without it the map's art is drawn from its
  // upscaled textures.
  const bool gpuFsr = GpuFsr::upscales(zoom);
  if (gpuFsr) {
    GpuFsr::beginLayer(renderer, GpuFsr::Layer::World,
                       static_cast<int>(std::ceil(view.w / zoom)),
                       static_cast<int>(std::ceil(view.h / zoom)),
                       SDL_Color{0, 0, 0, 255});
  } else {
    SDL_RenderSetViewport(renderer, &view); // in pixels (scale is 1 here)
    SDL_RenderSetScale(renderer, zoom, zoom);
  }
  RenderSettings::worldZoomedIn =
      !gpuFsr && zoom > 1.001f && ArtScaler::worldFactor() > 1;

  // Render terrain layer
  worldRenderer.renderTerrain(renderer, worldMap, SpriteDatabase::get());

  // Tank water, then the map's objects: entrance, fences, rocks, trees
  fences.drawWater(renderer, worldRenderer, worldMap);
  this->drawToolOverlay(renderer);
  ambient.drawShadows(renderer, worldRenderer, worldMap);
  std::vector<Fences::Drawable> fenceArt;
  fences.collect(worldRenderer, worldMap, fenceArt);
  // The bulldozer's piece glows red
  if (bulldozer && highlight.x >= 0) {
    std::vector<Fences::Drawable> only;
    fences.preview = {{highlight, false}};
    fences.deleting = true;
    int saved = fences.previewType;
    if (const Fences::Piece *p = fences.at(highlight))
      fences.previewType = p->type;
    fences.collect(worldRenderer, worldMap, only);
    fences.preview.clear();
    fences.deleting = false;
    fences.previewType = saved;
    for (Fences::Drawable &d : only)
      if (d.tinted)
        fenceArt.push_back(d);
  }
  items.collect(worldRenderer, worldMap, fenceArt);
  // Walkways in a pass of their own after the ground's objects (sorted
  // among them, something on the ground drew over the decks)
  std::vector<Fences::Drawable> deckArt;
  this->collectWalkways(deckArt);
  staff.collect(worldRenderer, worldMap, fenceArt, &deckArt);
  placedObjects.draw(renderer, worldRenderer, worldMap, fenceArt, [this](float x, float y) {
    return Features::elevatedPaths &&
           this->walkways.at(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))) != nullptr;
  });
  // Staff up on the decks, sorted in with them
  std::stable_sort(deckArt.begin(), deckArt.end(),
                   [](const Fences::Drawable &a, const Fences::Drawable &b) { return a.depth < b.depth; });
  for (Fences::Drawable &d : deckArt) {
    if (d.custom)
      d.custom(renderer);
    else if (d.art)
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tinted ? &d.tint : nullptr, d.frame);
  }
  // (debug) over the objects, so blocked tiles under rocks show
  if (this->debugPaths)
    this->drawPathDebug(renderer);

  // Render entities layer (same zoom-centred origin as the terrain)
  // Birds over the map's objects
  ambient.drawFlyers(renderer, worldRenderer, worldMap);

  entityManager.draw(renderer, cam.x, cam.y,
                     static_cast<int>(cam.screenCenterX / zoom),
                     static_cast<int>(cam.screenCenterY / zoom));

  if (gpuFsr)
    GpuFsr::endLayer(renderer, GpuFsr::Layer::World, view.w / zoom,
                     view.h / zoom, false, &view);

  // Reset
  SDL_RenderSetScale(renderer, 1.0f, 1.0f);
  SDL_RenderSetViewport(renderer, nullptr);
  RenderSettings::worldZoomedIn = false;
  cam.zoom = userZoom;
}

// Laying fence: the grid line under each piece of it, green where it can
// go and red where it can't, and (as the original) the tile under the
// cursor outlined
void World::drawToolOverlay(SDL_Renderer *renderer) {
  const bool filter = this->filterTool && this->fences.previewFilterX >= 0;
  if (!this->pathTool.empty()) {
    // The tiles a path would go on: green, red where it can't
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (auto &p : this->pathPreview) {
      auto [x, y] = p.first;
      if (this->pathDragging && this->raisedDrag) {
        bool ground = false;
        for (const DeckPreview &d : this->deckPreview)
          if (d.x == x && d.y == y)
            ground = d.ground;
        if (!ground)
          continue;
      }
      const MapTile *t = this->worldMap.getTile(x, y);
      if (!t)
        continue;
      const float cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
      const int ci[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X1Y1, CORNER_X0Y1};
      SDL_Vertex v[4];
      // (measured: the tile filled green or orange, nearly solid)
      SDL_Color c = p.second == Fences::Fit::Ok         ? SDL_Color{49, 150, 24, 225}
                    : p.second == Fences::Fit::InTheWay ? SDL_Color{165, 97, 24, 225}
                                                        : SDL_Color{170, 30, 24, 225};
      for (int i = 0; i < 4; i++) {
        float sx, sy, d;
        this->worldRenderer.worldToScreenF(x + cx[i], y + cy[i],
                                           static_cast<float>(t->cornerHeight[ci[i]]), sx, sy, d);
        v[i] = {{sx, sy}, c, {0, 0}};
      }
      int idx[6] = {0, 1, 2, 0, 2, 3};
      SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }
    return;
  }
  if (!filter && (this->fenceTool < 0 || this->hoverTileX < 0))
    return;
  auto corner = [&](int vx, int vy) {
    // The ground's height at a grid point: the tiles touching it agree on
    // flat ground; on a cliff, the highest
    int h = INT32_MIN;
    for (int dy = -1; dy <= 0; dy++)
      for (int dx = -1; dx <= 0; dx++)
        if (const MapTile *t = this->worldMap.getTile(vx + dx, vy + dy)) {
          static const int index[4] = {CORNER_X1Y1, CORNER_X0Y1, CORNER_X1Y0, CORNER_X0Y0};
          h = std::max(h, t->cornerHeight[index[(dx + 1) + (dy + 1) * 2]]);
        }
    float sx, sy, depth;
    this->worldRenderer.worldToScreenF(static_cast<float>(vx), static_cast<float>(vy),
                                       static_cast<float>(h == INT32_MIN ? 0 : h), sx, sy,
                                       depth);
    return SDL_FPoint{sx, sy};
  };
  auto line = [&](SDL_FPoint a, SDL_FPoint b) {
    SDL_RenderDrawLineF(renderer, a.x, a.y, b.x, b.y);
    SDL_RenderDrawLineF(renderer, a.x, a.y + 1, b.x, b.y + 1);
  };
  // Green: it can go; red: not there at all; orange: something's in the way
  auto colour = [&](Fences::Fit f) {
    if (f == Fences::Fit::Ok)
      SDL_SetRenderDrawColor(renderer, 71, 198, 110, 255);
    else if (f == Fences::Fit::Outside)
      SDL_SetRenderDrawColor(renderer, 230, 50, 40, 255);
    else
      SDL_SetRenderDrawColor(renderer, 235, 150, 35, 255);
  };
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const auto &p : filter ? decltype(this->fences.preview){} : this->fences.preview) {
    const Fences::Edge &e = p.first;
    colour(p.second ? Fences::Fit::Ok : this->fences.fit(e, this->worldMap));
    line(corner(e.x, e.y), corner(e.alongX ? e.x + 1 : e.x, e.alongX ? e.y : e.y + 1));
  }
  if (filter) {
    // The filter's footprint, in its colour
    const Fences::FilterType &k = this->fences.filterType();
    int x = this->fences.previewFilterX, y = this->fences.previewFilterY;
    colour(this->fences.previewFilterFit);
    SDL_FPoint a = corner(x, y), b = corner(x + k.footprintX, y),
               c = corner(x + k.footprintX, y + k.footprintY), d = corner(x, y + k.footprintY);
    line(a, b);
    line(b, c);
    line(c, d);
    line(d, a);
    return;
  }
  if (!this->dragging || !this->dragMoved) {
    int x = this->hoverTileX, y = this->hoverTileY;
    colour(this->hoverTileFit);
    SDL_FPoint a = corner(x, y), b = corner(x + 1, y), c = corner(x + 1, y + 1),
               d = corner(x, y + 1);
    line(a, b);
    line(b, c);
    line(c, d);
    line(d, a);
  }
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
  // The box is never bigger than at the original's furthest zoom out
  // (half size): zoomed out further (the mouse wheel goes to a tenth) it
  // keeps that size, centred on the view, like the original's box
  const float kOriginalMinZoom = 0.5f;
  float zoom = std::max(cam.zoom > 0 ? cam.zoom : 1.0f, kOriginalMinZoom) *
               this->viewScale;
  float halfDiff = outputW / zoom * 0.5f / (worldRenderer.getTileWidth() * 0.5f);
  float halfSum = outputH / zoom * 0.5f / (worldRenderer.getTileHeight() * 0.5f);
  float d = cu - cv, s = cu + cv;
  auto toX = [&](float diff) { return box.x + (diff + V) / span * box.w; };
  auto toY = [&](float sum) { return box.y + sum / span * box.h; };
  SDL_Rect view = {static_cast<int>(toX(d - halfDiff)), static_cast<int>(toY(s - halfSum)),
                   static_cast<int>(toX(d + halfDiff) - toX(d - halfDiff)),
                   static_cast<int>(toY(s + halfSum) - toY(s - halfSum))};
  // Near the map's edge the box stops at the minimap's edges (all four
  // sides showing) rather than running off it
  SDL_Rect shown;
  if (!SDL_IntersectRect(&view, &box, &shown))
    return;
  SDL_SetRenderDrawColor(renderer, 255, 223, 115, 255);
  SDL_RenderDrawRect(renderer, &shown);
}

void World::miniMapClick(float fx, float fy) {
  int U, V;
  worldRenderer.getViewSize(U, V);
  float span = static_cast<float>(U + V);
  float diff = fx * span - V, sum = fy * span;
  worldRenderer.centreViewOn((sum + diff) * 0.5f, (sum - diff) * 0.5f);
}

// ============================================================================
// FENCE TOOL AND BULLDOZER
// ============================================================================
bool World::pick(int x, int y, float &wx, float &wy) {
  // Window pixels to logical (as drawn: the view rect, then the zoom)
  const Camera &cam = worldRenderer.getCamera();
  float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
  float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
  Camera &c = worldRenderer.getCamera();
  float savedZoom = c.zoom;
  c.zoom = zoom; // the centre is in logical pixels at the drawing zoom
  bool on = worldRenderer.screenToWorld(lx, ly, worldMap, wx, wy);
  c.zoom = savedZoom;
  return on;
}

bool World::vertexToWindow(int vx, int vy, int &x, int &y) {
  const Camera &cam = worldRenderer.getCamera();
  float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
  // The vertex's height: any tile touching it
  int h = 0;
  for (int dy = -1; dy <= 0; dy++)
    for (int dx = -1; dx <= 0; dx++)
      if (const MapTile *t = worldMap.getTile(vx + dx, vy + dy)) {
        int c = (dx == 0 ? 0 : 1) + (dy == 0 ? 0 : 2);
        static const int index[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X0Y1, CORNER_X1Y1};
        h = t->cornerHeight[index[c]];
      }
  Camera &c = worldRenderer.getCamera();
  float savedZoom = c.zoom;
  c.zoom = zoom;
  float sx, sy, depth;
  worldRenderer.worldToScreenF(static_cast<float>(vx), static_cast<float>(vy),
                               static_cast<float>(h), sx, sy, depth);
  c.zoom = savedZoom;
  x = static_cast<int>(std::lround(this->viewRect.x + sx * zoom));
  y = static_cast<int>(std::lround(this->viewRect.y + sy * zoom));
  return true;
}

void World::pointToWindow(float wx, float wy, float h, int &x, int &y) {
  const Camera &cam = worldRenderer.getCamera();
  float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
  Camera &c = worldRenderer.getCamera();
  float savedZoom = c.zoom;
  c.zoom = zoom;
  float sx, sy, depth;
  worldRenderer.worldToScreenF(wx, wy, h, sx, sy, depth);
  c.zoom = savedZoom;
  x = static_cast<int>(std::lround(this->viewRect.x + sx * zoom));
  y = static_cast<int>(std::lround(this->viewRect.y + sy * zoom));
}

bool World::pickFence(int x, int y, Fences::Edge &e) {
  const Camera &cam = worldRenderer.getCamera();
  float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
  float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
  Camera &c = worldRenderer.getCamera();
  float savedZoom = c.zoom;
  c.zoom = zoom;
  bool found = this->fences.pieceAt(lx, ly, worldRenderer, worldMap, e);
  c.zoom = savedZoom;
  return found;
}

// The tile edge nearest a point on the ground
Fences::Edge World::nearestEdge(float wx, float wy) const {
  float fx = wx - std::floor(wx), fy = wy - std::floor(wy);
  int tx = static_cast<int>(std::floor(wx)), ty = static_cast<int>(std::floor(wy));
  float d[4] = {fy, 1 - fy, fx, 1 - fx}; // top, bottom, left, right
  int best = 0;
  for (int i = 1; i < 4; i++)
    if (d[i] < d[best])
      best = i;
  Fences::Edge e;
  switch (best) {
  case 0: e = {true, tx, ty}; break;
  case 1: e = {true, tx, ty + 1}; break;
  case 2: e = {false, tx, ty}; break;
  default: e = {false, tx + 1, ty}; break;
  }
  return e;
}

void World::setFenceTool(int type) {
  this->fenceTool = type;
  this->worldRenderer.setToolGrid(type >= 0); // the grid, as in the original
  this->fences.previewType = type;
  this->fences.preview.clear();
  this->hoverPrice = -1;
  this->hoverTileX = -1;
  this->dragging = false;
  if (type >= 0)
    this->bulldozer = false;
}

// The path debug overlay: blocked tiles red; each staff member's A* tiles
// lit in its own colour, the straightened route it walks as a line, its
// goal tile outlined
void World::drawPathDebug(SDL_Renderer *renderer) {
  auto corner = [&](float vx, float vy) {
    const MapTile *t = this->worldMap.getTile(static_cast<int>(std::floor(vx)),
                                              static_cast<int>(std::floor(vy)));
    float h = t ? static_cast<float>(t->height) : 0;
    float sx, sy, d;
    this->worldRenderer.worldToScreenF(vx, vy, h, sx, sy, d);
    return SDL_FPoint{sx, sy};
  };
  auto tileQuad = [&](int x, int y, SDL_Color c) {
    SDL_FPoint p[4] = {corner(x + 0.02f, y + 0.02f), corner(x + 0.98f, y + 0.02f),
                       corner(x + 0.98f, y + 0.98f), corner(x + 0.02f, y + 0.98f)};
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++)
      v[i] = {p[i], c, {0, 0}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
  };
  auto tileOutline = [&](int x, int y, SDL_Color c) {
    SDL_FPoint p[5] = {corner(x, y), corner(x + 1, y), corner(x + 1, y + 1), corner(x, y + 1),
                       corner(x, y)};
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLinesF(renderer, p, 5);
  };
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  // What stands in the way
  for (int y = 0; y < this->index.height(); y++)
    for (int x = 0; x < this->index.width(); x++)
      if (this->index.blocked(x, y))
        tileQuad(x, y, SDL_Color{230, 40, 40, 90});
  static const SDL_Color colours[6] = {{60, 220, 255, 110}, {255, 220, 40, 110}, {120, 255, 90, 110},
                                       {255, 120, 220, 110}, {255, 150, 50, 110}, {170, 130, 255, 110}};
  for (const Staff::Member &m : this->staff.members()) {
    if (m.path.empty())
      continue;
    SDL_Color c = colours[m.id % 6];
    for (auto [x, y] : m.routeTiles)
      tileQuad(x, y, c);
    tileOutline(m.goalX, m.goalY, SDL_Color{255, 255, 255, 255});
    // The line it walks: from where it is through what's left
    std::vector<SDL_FPoint> line{corner(m.x, m.y)};
    for (size_t i = m.pathAt; i < m.path.size(); i++)
      line.push_back(corner(m.path[i].first, m.path[i].second));
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
    SDL_RenderDrawLinesF(renderer, line.data(), static_cast<int>(line.size()));
    for (SDL_FPoint &p : line)
      p.y += 1;
    SDL_RenderDrawLinesF(renderer, line.data(), static_cast<int>(line.size()));
  }
}

void World::reindex() {
  this->index.rebuild(this->worldMap, this->placedObjects, this->fences, this->resourceManager);
  this->index.clearMovers();
  this->staff.addToIndex(this->index);
  this->reindexIn = 1.0f;
}

void World::setPathTool(const std::string &type, int cost) {
  if (type == this->pathTool)
    return;
  this->pathTool = type;
  this->pathCost = cost;
  if (!raisable(type))
    this->buildHeight = 0;
  this->pathPreview.clear();
  this->pathDragging = false;
  this->hoverPrice = -1;
  this->worldRenderer.setToolGrid(!type.empty() || this->fenceTool >= 0);
  if (!type.empty())
    this->bulldozer = false;
}

Fences::Fit World::pathFit(int x, int y) const {
  Fences::Fit f = this->fences.tileFit(x, y, this->worldMap);
  if (f != Fences::Fit::Ok)
    return f;
  if (this->fences.exhibitAt(x, y) >= 0 || !this->worldMap.pathShapeOk(x, y))
    return Fences::Fit::Outside;
  if (this->index.blocked(x, y))
    return Fences::Fit::InTheWay;
  int have = this->worldMap.getPathType(x, y);
  if (have >= 0 && this->worldMap.getPathTypes()[have] == this->pathTool)
    return Fences::Fit::InTheWay;
  return Fences::Fit::Ok;
}

// A drag lays the tiles the cursor passes over, gaps filled straight
void World::addPathTile(int x, int y) {
  auto add = [&](int tx, int ty) {
    for (auto &p : this->pathPreview)
      if (p.first == std::make_pair(tx, ty))
        return;
    this->pathPreview.push_back({{tx, ty}, pathFit(tx, ty)});
  };
  auto [lx, ly] = this->pathLast;
  if (lx >= 0) {
    while (lx != x || ly != y) {
      if (std::abs(x - lx) >= std::abs(y - ly))
        lx += x > lx ? 1 : -1;
      else
        ly += y > ly ? 1 : -1;
      add(lx, ly);
    }
  } else {
    add(x, y);
  }
  this->pathLast = {x, y};
}

bool World::raisable(const std::string &type) {
  static const char *ok[] = {"path", "stnepath", "brkpath", "asphpath", "bluepath",
                             "yellpath", "asipath", "mexpath", "atlpath", "dkpath"};
  for (const char *k : ok)
    if (type == k)
      return true;
  return false;
}

void World::adjustBuildHeight(int by) {
  if (!Features::elevatedPaths || !raisable(this->pathTool))
    by = -this->buildHeight;
  this->buildHeight = std::clamp(this->buildHeight + by, 0, 12);
  if (this->pathDragging && this->raisedDrag)
    this->layDeckPreview();
}

// A raised drag: from the ground (or the deck) at its first tile, a unit a
// tile up or down towards the build height, then level. Tiles that come
// out at ground level are ground path.
void World::layDeckPreview() {
  this->deckPreview.clear();
  if (this->pathPreview.empty())
    return;
  auto ground = [&](int x, int y) {
    const MapTile *t = this->worldMap.getTile(x, y);
    return t ? *std::min_element(t->cornerHeight, t->cornerHeight + 4) : 0;
  };
  auto [fx, fy] = this->pathPreview.front().first;
  int level;
  if (const Walkways::Deck *d = this->walkways.at(fx, fy))
    level = *std::max_element(d->h, d->h + 4);
  else
    level = ground(fx, fy);
  // The height to reach: the build height above the ground where it starts
  int target = ground(fx, fy) + this->buildHeight;
  // The first tile: as it is (a deck stays; the ground gets a path)
  {
    DeckPreview p{fx, fy, {level, level, level, level}, !this->walkways.at(fx, fy), true};
    if (p.ground)
      p.ok = this->pathFit(fx, fy) != Fences::Fit::Outside;
    else
      std::copy(this->walkways.at(fx, fy)->h, this->walkways.at(fx, fy)->h + 4, p.h);
    this->deckPreview.push_back(p);
  }
  for (size_t i = 1; i < this->pathPreview.size(); i++) {
    auto [px, py] = this->pathPreview[i - 1].first;
    auto [x, y] = this->pathPreview[i].first;
    int dir = x > px ? 1 : x < px ? 3 : y > py ? 2 : 0;
    int to = level + (target > level ? 1 : target < level ? -1 : 0);
    DeckPreview p{x, y, {0, 0, 0, 0}, false, false};
    Walkways::shape(dir, level, to, p.h);
    const MapTile *t = this->worldMap.getTile(x, y);
    bool onGround = t != nullptr;
    for (int c = 0; c < 4 && onGround; c++)
      onGround = p.h[c] == t->cornerHeight[c];
    if (onGround) {
      p.ground = true;
      p.ok = this->pathFit(x, y) == Fences::Fit::Ok;
    } else {
      // (not over a rock or a tree: they'd stand up through it)
      bool clear = true;
      for (const PlacedObjects::Object &o : this->placedObjects.objects())
        if (!o.fence && static_cast<int>(std::floor(o.x)) == x && static_cast<int>(std::floor(o.y)) == y)
          clear = false;
      p.ok = clear && this->walkways.canBuild(x, y, p.h, this->worldMap, this->fences) &&
             !this->walkways.at(x, y);
    }
    this->deckPreview.push_back(p);
    level = to;
  }
}

// Walkways as drawables: posts down to the ground, the deck (the path art
// at its height), railings along the open edges
void World::collectWalkways(std::vector<Fences::Drawable> &out) {
  if (!Features::elevatedPaths)
    return;
  auto add = [&](int x, int y, const int h[4], const std::string &type, const SDL_Color *tint) {
    float centre = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
    float sx, sy, depth, gx, gy;
    this->worldRenderer.worldToScreenF(x + 0.5f, y + 0.5f, centre, sx, sy, depth);
    // Sorted by the front of its tile on the ground: after whatever lies
    // under it, before what stands in front
    this->worldRenderer.worldToScreenF(x + 0.5f, y + 0.5f, 0.0f, gx, gy, depth);
    depth += 0.6f;
    int hh[4] = {h[0], h[1], h[2], h[3]};
    SDL_Color tc = tint ? *tint : SDL_Color{255, 255, 255, 255};
    bool tinted = tint != nullptr;
    Fences::Drawable d{depth - 0.001f, sx, sy, nullptr, CompassDirection::N, tc, tinted};
    d.custom = [this, x, y, hh, type, tc, tinted](SDL_Renderer *r) {
      const MapTile *t = this->worldMap.getTile(x, y);
      const float cxs[4] = {0, 1, 1, 0}, cys[4] = {0, 0, 1, 1};
      // RCT-style supports from ZT1's own concrete: a footing and a square
      // pillar under the tile's middle up to the deck's underside, cross
      // beams on tall spans, the deck slab's edge
      SpriteDatabase &db = SpriteDatabase::get();
      Uint8 alpha = tinted ? tc.a : 255;
      float deckMid = (hh[0] + hh[1] + hh[2] + hh[3]) * 0.25f;
      float groundMid = t ? (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) * 0.25f : 0.0f;
      const float pillar = 0.13f, footing = 0.22f, slab = 0.35f;
      float under = deckMid - slab;
      if (under > groundMid + 0.1f) {
        this->worldRenderer.drawBox(r, db, x + 0.5f - footing, y + 0.5f - footing, x + 0.5f + footing,
                                    y + 0.5f + footing, groundMid, groundMid + 0.3f, 14, true, alpha);
        this->worldRenderer.drawBox(r, db, x + 0.5f - pillar, y + 0.5f - pillar, x + 0.5f + pillar,
                                    y + 0.5f + pillar, groundMid + 0.3f, under, 14, false, alpha);
        // Tall: a beam along the deck to the next tile's pillar (east and
        // south, so each is drawn once)
        if (under - groundMid >= 4.0f)
          for (int dir : {1, 2}) {
            if (!this->walkways.joins(x, y, dir))
              continue;
            float bz = groundMid + (under - groundMid) * 0.5f;
            if (dir == 1)
              this->worldRenderer.drawBox(r, db, x + 0.5f, y + 0.5f - 0.06f, x + 1.5f, y + 0.5f + 0.06f, bz - 0.25f, bz,
                                          14, true, alpha);
            else
              this->worldRenderer.drawBox(r, db, x + 0.5f - 0.06f, y + 0.5f, x + 0.5f + 0.06f, y + 1.5f, bz - 0.25f, bz,
                                          14, true, alpha);
          }
      }
      // The slab's edge under a level deck
      int lowest = *std::min_element(hh, hh + 4), highest = *std::max_element(hh, hh + 4);
      if (lowest == highest && t && lowest > *std::max_element(t->cornerHeight, t->cornerHeight + 4))
        this->worldRenderer.drawBox(r, db, static_cast<float>(x), static_cast<float>(y), x + 1.0f, y + 1.0f,
                                    lowest - slab, static_cast<float>(lowest), 14, false, alpha);
      // The deck
      auto connected = [&](int nx, int ny) {
        int dir = nx > x ? 1 : nx < x ? 3 : ny > y ? 2 : 0;
        if (this->walkways.joins(x, y, dir))
          return true;
        // Stairs' foot: a ground path whose edge meets it
        const MapTile *n = this->worldMap.getTile(nx, ny);
        if (!n || !this->worldMap.isPath(nx, ny))
          return false;
        int c0, c1, n0, n1;
        Walkways::edgeCorners(dir, c0, c1);
        Walkways::edgeCorners((dir + 2) & 3, n0, n1);
        return hh[c0] == n->cornerHeight[n0] && hh[c1] == n->cornerHeight[n1];
      };
      this->worldRenderer.drawPathPiece(r, SpriteDatabase::get(), type, x, y, hh, connected,
                                          tinted ? &tc : nullptr);
      // Railings along the open edges
      for (int dir = 0; dir < 4; dir++) {
        int dx, dy;
        Walkways::step(dir, dx, dy);
        if (connected(x + dx, y + dy))
          continue;
        int c0, c1;
        Walkways::edgeCorners(dir, c0, c1);
        float ax, ay, bx, by, dd;
        this->worldRenderer.worldToScreenF(x + cxs[c0], y + cys[c0], hh[c0] + 0.7f, ax, ay, dd);
        this->worldRenderer.worldToScreenF(x + cxs[c1], y + cys[c1], hh[c1] + 0.7f, bx, by, dd);
        float gx0, gy0, gx1, gy1;
        this->worldRenderer.worldToScreenF(x + cxs[c0], y + cys[c0], static_cast<float>(hh[c0]), gx0, gy0, dd);
        this->worldRenderer.worldToScreenF(x + cxs[c1], y + cys[c1], static_cast<float>(hh[c1]), gx1, gy1, dd);
        SDL_SetRenderDrawColor(r, 200, 180, 140, tinted ? tc.a : 255);
        SDL_RenderDrawLineF(r, ax, ay, bx, by);
        SDL_RenderDrawLineF(r, ax, ay + 1, bx, by + 1);
        for (int k = 0; k <= 4; k++) {
          float f = k / 4.0f;
          SDL_RenderDrawLineF(r, ax + (bx - ax) * f, ay + (by - ay) * f, gx0 + (gx1 - gx0) * f,
                              gy0 + (gy1 - gy0) * f);
        }
      }
    };
    out.push_back(d);
  };
  for (const auto &kv : this->walkways.all())
    add(kv.first.first, kv.first.second, kv.second.h, kv.second.type, nullptr);
  // The drag's decks, tinted
  if (this->pathDragging && this->raisedDrag) {
    static const SDL_Color green{120, 255, 120, 230}, red{255, 90, 90, 230};
    for (const DeckPreview &p : this->deckPreview)
      if (!p.ground)
        add(p.x, p.y, p.h, this->pathTool, p.ok ? &green : &red);
  }
}

int World::deckAt(int x, int y, int &tx, int &ty) {
  if (!Features::elevatedPaths)
    return 0;
  const Camera &cam = worldRenderer.getCamera();
  float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
  float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
  Camera &c = worldRenderer.getCamera();
  float saved = c.zoom;
  c.zoom = zoom;
  float best = 22.0f * 22.0f;
  int found = 0;
  for (const auto &kv : this->walkways.all()) {
    const int *h = kv.second.h;
    float sx, sy, d;
    worldRenderer.worldToScreenF(kv.first.first + 0.5f, kv.first.second + 0.5f,
                                 (h[0] + h[1] + h[2] + h[3]) * 0.25f, sx, sy, d);
    float dist = (sx - lx) * (sx - lx) + (sy - ly) * (sy - ly);
    if (dist < best) {
      best = dist;
      tx = kv.first.first;
      ty = kv.first.second;
      found = 1;
    }
  }
  c.zoom = saved;
  return found;
}

void World::setStaffTool(int type) {
  if (type == this->staffTool)
    return;
  this->staffTool = type;
  this->staff.previewType = -1;
  this->hoverPrice = -1;
  if (type >= 0)
    this->bulldozer = false;
}

void World::pickUpStaff(int id) {
  this->staff.carried = id;
}

void World::centreOnStaff(int id) {
  if (const Staff::Member *m = this->staff.member(id))
    this->centreOn(m->x, m->y);
}

void World::centreOn(float x, float y) {
  const MapTile *t = this->worldMap.getTile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  this->worldRenderer.centreOnWorld(x, y, t ? static_cast<float>(t->height) : 0.0f);
}

void World::setFilterTool(bool on) {
  if (on == this->filterTool)
    return;
  this->filterTool = on;
  this->fences.previewFilterX = -1;
  if (on)
    this->fences.preview.clear();
  this->hoverPrice = -1;
  this->hoverTileX = -1;
  if (on)
    this->bulldozer = false;
}

void World::hoverOff() {
  this->fences.previewFilterX = -1;
  if (this->dragging)
    return;
  if (this->fenceTool >= 0)
    this->fences.preview.clear();
  this->hoverPrice = -1;
  this->hoverTileX = -1;
}

void World::cancelTool() {
  this->assigning = -1;
  this->dragging = false;
  this->fences.preview.clear();
}

void World::cycleFenceMode(int step) {
  int n = (static_cast<int>(this->fenceMode) + step + 4) % 4;
  this->fenceMode = static_cast<FenceMode>(n);
  if (this->dragging && this->dragMoved) {
    // Re-lay the drag in the new mode, to where the cursor is
    this->layDrag(this->dragVertexX, this->dragVertexY);
    int count = 0;
    for (auto &p : this->fences.preview)
      count += p.second ? 1 : 0;
    this->hoverPrice = count * this->fences.types()[this->fenceTool].cost;
  }
}

const char *World::fenceModeName(FenceMode m) {
  switch (m) {
  case FenceMode::Bend: return "Bend";
  case FenceMode::BendOther: return "Bend (other way)";
  case FenceMode::Straight: return "Straight";
  default: return "Rectangle";
  }
}

// The drag's fence from the press's grid point to the one at the cursor,
// in the current mode
void World::layDrag(int vx, int vy) {
  this->fences.preview.clear();
  this->dragVertexX = vx;
  this->dragVertexY = vy;
  auto add = [&](const Fences::Edge &e) {
    for (auto &p : this->fences.preview)
      if (p.first == e)
        return;
    this->fences.preview.push_back({e, this->fences.canPlace(e, this->worldMap)});
  };
  // Along one grid line from (x0, y0) to (x1, y1)
  auto run = [&](int x0, int y0, int x1, int y1) {
    while (x0 != x1) {
      int nx = x0 + (x1 > x0 ? 1 : -1);
      add({true, std::min(x0, nx), y0});
      x0 = nx;
    }
    while (y0 != y1) {
      int ny = y0 + (y1 > y0 ? 1 : -1);
      add({false, x0, std::min(y0, ny)});
      y0 = ny;
    }
  };
  const int px = this->pressVertexX, py = this->pressVertexY;
  bool xFirst = this->firstAxis != 1;
  switch (this->fenceMode) {
  case FenceMode::BendOther:
    xFirst = !xFirst;
    [[fallthrough]];
  case FenceMode::Bend:
    if (xFirst) {
      run(px, py, vx, py);
      run(vx, py, vx, vy);
    } else {
      run(px, py, px, vy);
      run(px, vy, vx, vy);
    }
    break;
  case FenceMode::Straight:
    if (std::abs(vx - px) >= std::abs(vy - py))
      run(px, py, vx, py);
    else
      run(px, py, px, vy);
    break;
  case FenceMode::Box:
    run(px, py, vx, py);
    run(vx, py, vx, vy);
    run(vx, vy, px, vy);
    run(px, vy, px, py);
    break;
  }
}

void World::mouseMove(int x, int y) {
  if (this->bulldozer) {
    // What would come back for the piece under it ("$56"), and its name
    Fences::Edge e;
    this->highlight = this->pickFence(x, y, e) ? e : Fences::Edge{false, -1, -1};
    this->hoverPrice = -1;
    this->bulldozeTip.clear();
    if (const Fences::Piece *p = this->fences.at(this->highlight)) {
      this->hoverPrice = this->fences.refundOf(this->highlight);
      this->bulldozeTip = this->fences.types()[p->type].name;
    }
    return;
  }
  if (!this->pathTool.empty()) {
    float wx, wy;
    if (!this->pick(x, y, wx, wy)) {
      if (!this->pathDragging)
        this->pathPreview.clear();
      return;
    }
    int tx = static_cast<int>(std::floor(wx)), ty = static_cast<int>(std::floor(wy));
    if (this->pathDragging) {
      this->addPathTile(tx, ty);
      if (this->raisedDrag)
        this->layDeckPreview();
    } else {
      this->pathPreview = {{{tx, ty}, pathFit(tx, ty)}};
    }
    int n = 0;
    if (this->pathDragging && this->raisedDrag) {
      // Decks cost twice a path tile (their posts)
      for (auto &p : this->deckPreview)
        n += p.ok ? (p.ground ? (this->worldMap.isPath(p.x, p.y) ? 0 : 1) : 2) : 0;
    } else {
      for (auto &p : this->pathPreview)
        n += p.second == Fences::Fit::Ok ? 1 : 0;
    }
    this->hoverPrice = n * this->pathCost;
    return;
  }
  // A staff member picked up follows the cursor
  if (this->staff.carried >= 0) {
    float wx, wy;
    if (this->pick(x, y, wx, wy))
      this->staff.moveTo(this->staff.carried, wx, wy);
    return;
  }
  if (this->staffTool >= 0) {
    float wx, wy;
    if (!this->pick(x, y, wx, wy)) {
      this->staff.previewType = -1;
      this->hoverPrice = -1;
      return;
    }
    this->staff.previewType = this->staffTool;
    this->staff.previewX = wx;
    this->staff.previewY = wy;
    this->staff.previewFit = this->staff.canPlaceType(this->staffTool, wx, wy, this->worldMap, this->fences);
    // "-$1000" where one can go (the first month's pay), "$0" where not
    this->hoverPrice = this->staff.previewFit == Fences::Fit::Ok
                           ? this->staff.types()[this->staffTool].salary
                           : 0;
    return;
  }
  if (this->fenceTool < 0 && !this->filterTool && !this->bulldozer) {
    const Camera &cam = worldRenderer.getCamera();
    float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
    float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
    Camera &c = worldRenderer.getCamera();
    float saved = c.zoom;
    c.zoom = zoom;
    this->staff.hovered = this->staff.pick(lx, ly, worldRenderer, worldMap);
    c.zoom = saved;
  } else {
    this->staff.hovered = -1;
  }
  if (this->filterTool) {
    float wx, wy;
    if (!this->pick(x, y, wx, wy)) {
      this->fences.previewFilterX = -1;
      this->hoverPrice = -1;
      return;
    }
    // Centred on the grid point nearest the cursor
    const Fences::FilterType &k = this->fences.filterType();
    int fx = static_cast<int>(std::lround(wx)) - k.footprintX / 2;
    int fy = static_cast<int>(std::lround(wy)) - k.footprintY / 2;
    this->fences.previewFilterX = fx;
    this->fences.previewFilterY = fy;
    this->fences.previewFilterFit = this->fences.filterFit(fx, fy, this->worldMap);
    this->hoverPrice = this->fences.previewFilterFit == Fences::Fit::Ok ? k.cost : 0;
    return;
  }
  if (this->fenceTool < 0)
    return;
  float wx, wy;
  if (!this->pick(x, y, wx, wy)) {
    if (!this->dragging) {
      this->fences.preview.clear();
      this->hoverPrice = -1; // off the map: no price
      this->hoverTileX = -1;
    }
    return;
  }
  const int cost = this->fences.types()[this->fenceTool].cost;
  // The tile under the cursor: can fence go round it?
  this->hoverTileX = static_cast<int>(std::floor(wx));
  this->hoverTileY = static_cast<int>(std::floor(wy));
  this->hoverTileFit = this->fences.tileFit(this->hoverTileX, this->hoverTileY, this->worldMap);
  if (this->dragging) {
    // From the press's grid point to the one at the cursor, in the current
    // mode. Until the cursor reaches another grid point, it's the one piece
    // pressed on. The way the drag starts (along x or y) is the bend's
    // first leg; it's decided afresh while the cursor is near the start.
    int vx = static_cast<int>(std::lround(wx)), vy = static_cast<int>(std::lround(wy));
    if (!Features::fenceModes) {
      // The original: the fence follows the grid points the mouse goes
      // through (back over them takes pieces off)
      auto &path = this->followPath;
      if (path.empty())
        path.push_back({this->pressVertexX, this->pressVertexY});
      for (size_t i = 0; i < path.size(); i++)
        if (path[i] == std::make_pair(vx, vy)) {
          path.resize(i + 1);
          break;
        }
      if (path.back() != std::make_pair(vx, vy)) {
        auto [px, py] = path.back();
        bool xFirst = std::abs(vx - px) >= std::abs(vy - py);
        for (int pass = 0; pass < 2; pass++) {
          if ((pass == 0) == xFirst)
            while (px != vx) { px += vx > px ? 1 : -1; path.push_back({px, py}); }
          else
            while (py != vy) { py += vy > py ? 1 : -1; path.push_back({px, py}); }
        }
      }
      if (path.size() > 1)
        this->dragMoved = true;
      this->fences.preview.clear();
      if (!this->dragMoved) {
        this->fences.preview = {{this->pressEdge, this->fences.canPlace(this->pressEdge, this->worldMap)}};
      } else {
        for (size_t i = 1; i < path.size(); i++) {
          auto [ax, ay] = path[i - 1];
          auto [bx, by] = path[i];
          Fences::Edge e = ax != bx ? Fences::Edge{true, std::min(ax, bx), ay}
                                    : Fences::Edge{false, ax, std::min(ay, by)};
          bool dup = false;
          for (auto &q : this->fences.preview)
            dup = dup || q.first == e;
          if (!dup)
            this->fences.preview.push_back({e, this->fences.canPlace(e, this->worldMap)});
        }
      }
      int n = 0;
      for (auto &q : this->fences.preview)
        n += q.second ? 1 : 0;
      this->hoverPrice = n * cost;
      return;
    }
    int dx = vx - this->pressVertexX, dy = vy - this->pressVertexY;
    if (dx == 0 && dy == 0)
      this->firstAxis = -1;
    else if (this->firstAxis < 0 || (std::abs(dx) <= 1 && std::abs(dy) <= 1 &&
                                     std::abs(dx) != std::abs(dy)))
      this->firstAxis = std::abs(dx) >= std::abs(dy) ? 0 : 1;
    if (dx != 0 || dy != 0)
      this->dragMoved = true;
    if (dx == 0 && dy == 0) {
      this->fences.preview = {
          {this->pressEdge, this->fences.canPlace(this->pressEdge, this->worldMap)}};
      this->dragVertexX = vx;
      this->dragVertexY = vy;
    } else {
      this->layDrag(vx, vy);
    }
  } else {
    // The piece the cursor is over
    Fences::Edge e = this->nearestEdge(wx, wy);
    this->fences.preview = {{e, this->fences.canPlace(e, this->worldMap)}};
  }
  int n = 0;
  for (auto &p : this->fences.preview)
    n += p.second ? 1 : 0;
  this->hoverPrice = n * cost;
}

World::ToolResult World::mouseDown(int x, int y) {
  ToolResult r;
  float wx = 0, wy = 0;
  bool onMap = this->pick(x, y, wx, wy);
  if (this->bulldozer) {
    {
      const Camera &cam = worldRenderer.getCamera();
      float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
      float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
      Camera &c = worldRenderer.getCamera();
      float saved = c.zoom;
      c.zoom = zoom;
      int f = this->fences.filterAt(lx, ly, worldRenderer, worldMap);
      c.zoom = saved;
      if (f >= 0) {
        r.refund = static_cast<int>(std::lround(this->fences.filterType().cost * 0.8));
        this->fences.removeFilter(f);
        return r;
      }
    }
    // A walkway deck under it: taken down, 80% of its price back
    {
      int dtx, dty;
      if (this->deckAt(x, y, dtx, dty)) {
        std::string file = "paths/" + this->walkways.at(dtx, dty)->type + ".ai";
        int price = 0;
        for (const CatalogItem &item : ItemCatalog::get().all())
          if (item.file == file)
            price = item.cost;
        this->walkways.remove(dtx, dty);
        r.refund = static_cast<int>(std::lround(price * 2 * 0.8));
        return r;
      }
    }
    Fences::Edge e;
    if (!this->pickFence(x, y, e)) {
      // A path tile under it: lifted, 80% back
      if (onMap) {
        int tx = static_cast<int>(std::floor(wx)), ty = static_cast<int>(std::floor(wy));
        if (this->worldMap.isPath(tx, ty)) {
          // 80% of that path type's price (paths/<type>.ai)
          std::string file = "paths/" + this->worldMap.getPathTypes()[this->worldMap.getPathType(tx, ty)] + ".ai";
          int price = 0;
          for (const CatalogItem &item : ItemCatalog::get().all())
            if (item.file == file)
              price = item.cost;
          this->worldMap.setPath(tx, ty, "");
          r.refund = static_cast<int>(std::lround(price * 0.8));
        }
      }
      return r;
    }
    if (this->fences.tankOf(e) >= 0) {
      this->pendingDrain = e;
      r.askDrainTank = true;
      return r;
    }
    r.refund = this->fences.refundOf(e);
    this->fences.remove(e, this->worldMap);
    this->highlight = {false, -1, -1};
    this->hoverPrice = -1;
    this->fences.updateExhibits(this->worldMap, date.day, date.month, date.year);
    return r;
  }
  // Putting a picked-up staff member down
  if (this->staff.carried >= 0) {
    int id = this->staff.carried;
    this->staff.carried = -1;
    if (const Staff::Member *m = this->staff.member(id)) {
      float mx = m->x, my = m->y;
      this->staff.moveTo(id, -1000, -1000); // (not in its own way)
      if (this->staff.canPlaceMember(mx, my, this->worldMap, this->fences) == Fences::Fit::Ok) {
        this->staff.moveTo(id, mx, my);
      } else {
        this->staff.carried = id; // can't go there: still held
        this->staff.moveTo(id, mx, my);
      }
    }
    return r;
  }
  // Assigning a keeper: the exhibit clicked (none has animals yet: "Zoo
  // staff cannot be assigned to empty exhibits.")
  if (this->assigning >= 0) {
    int who = this->assigning;
    this->assigning = -1;
    if (onMap) {
      int ex = this->fences.exhibitAt(static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wy)));
      if (ex >= 0)
        r.messageId = this->staff.assign(who, ex, this->fences);
    }
    return r;
  }
  if (!this->pathTool.empty()) {
    if (!onMap)
      return r;
    this->pathDragging = true;
    this->pathPreview.clear();
    this->pathLast = {-1, -1};
    int ptx = static_cast<int>(std::floor(wx)), pty = static_cast<int>(std::floor(wy));
    // Pressing on a deck starts from it (and at height 0 comes down)
    int dtx, dty;
    bool fromDeck = Features::elevatedPaths && raisable(this->pathTool) && this->deckAt(x, y, dtx, dty);
    if (fromDeck) {
      ptx = dtx;
      pty = dty;
    }
    this->raisedDrag = Features::elevatedPaths && raisable(this->pathTool) && (this->buildHeight > 0 || fromDeck);
    this->addPathTile(ptx, pty);
    if (this->raisedDrag)
      this->layDeckPreview();
    this->hoverPrice = this->pathPreview.front().second == Fences::Fit::Ok ? this->pathCost : 0;
    return r;
  }
  if (this->staffTool >= 0) {
    if (!onMap)
      return r;
    int id = this->staff.hire(this->staffTool, wx, wy, this->worldMap, this->fences);
    if (id >= 0) {
      r.wage = this->staff.types()[this->staffTool].salary;
      r.staff = id;
    }
    this->mouseMove(x, y);
    return r;
  }
  if (this->filterTool) {
    this->mouseMove(x, y);
    int fx = this->fences.previewFilterX, fy = this->fences.previewFilterY;
    if (fx < 0)
      return r;
    if (this->fences.placeFilter(fx, fy, this->worldMap)) {
      r.cost = this->fences.filterType().cost;
    } else if (this->fences.previewFilterFit == Fences::Fit::Outside) {
      // "Filters must be placed adjacent to a completed tank. Only one
      // filter can be placed next to a tank."
      r.messageId = 10517;
    }
    this->mouseMove(x, y);
    return r;
  }
  if (this->fenceTool < 0) {
    // No tool: a staff member opens its panel
    {
      const Camera &cam = worldRenderer.getCamera();
      float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
      float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
      Camera &c = worldRenderer.getCamera();
      float saved = c.zoom;
      c.zoom = zoom;
      r.staff = this->staff.pick(lx, ly, worldRenderer, worldMap);
      c.zoom = saved;
      if (r.staff >= 0)
        return r;
    }
    // A tank filter opens its panel
    {
      const Camera &cam = worldRenderer.getCamera();
      float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
      float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
      Camera &c = worldRenderer.getCamera();
      float saved = c.zoom;
      c.zoom = zoom;
      r.filter = this->fences.filterAt(lx, ly, worldRenderer, worldMap);
      c.zoom = saved;
      if (r.filter >= 0)
        return r;
    }
    // An exhibit's gate opens its information (as the original: not its
    // other fences, not its ground)
    Fences::Edge e;
    if (this->pickFence(x, y, e))
      if (const Fences::Piece *p = this->fences.at(e))
        if (p->gate)
          r.exhibit = this->fences.exhibitOf(e);
    return r;
  }
  if (!onMap)
    return r;
  // The press puts down the piece under the cursor; dragging from there
  // lays more along the grid lines
  this->dragging = true;
  this->dragCount++;
  this->pressEdge = this->nearestEdge(wx, wy);
  // The press's grid point: the end of that piece nearest the cursor
  int ax = this->pressEdge.x, ay = this->pressEdge.y;
  int bx = this->pressEdge.alongX ? ax + 1 : ax, by = this->pressEdge.alongX ? ay : ay + 1;
  float da = (wx - ax) * (wx - ax) + (wy - ay) * (wy - ay);
  float db = (wx - bx) * (wx - bx) + (wy - by) * (wy - by);
  this->pressVertexX = da <= db ? ax : bx;
  this->pressVertexY = da <= db ? ay : by;
  this->dragMoved = false;
  this->firstAxis = -1;
  this->followPath.clear();
  this->fences.preview = {
      {this->pressEdge, this->fences.canPlace(this->pressEdge, this->worldMap)}};
  this->hoverPrice = this->fences.preview.front().second
                         ? this->fences.types()[this->fenceTool].cost
                         : 0;
  return r;
}

World::ToolResult World::mouseUp(int x, int y) {
  ToolResult r;
  if (this->pathDragging) {
    this->mouseMove(x, y);
    this->pathDragging = false;
    if (this->raisedDrag) {
      this->raisedDrag = false;
      if (this->staff.logRoutes)
        for (const DeckPreview &p : this->deckPreview)
          SDL_Log("[Walkway] %d,%d heights %d %d %d %d %s %s", p.x, p.y, p.h[0], p.h[1], p.h[2], p.h[3],
                  p.ground ? "ground" : "deck", p.ok ? "ok" : "NO");
      for (const DeckPreview &p : this->deckPreview) {
        if (!p.ok)
          continue;
        if (p.ground) {
          if (canLayPath(p.x, p.y)) {
            this->worldMap.setPath(p.x, p.y, this->pathTool);
            r.cost += this->pathCost;
          }
        } else if (!this->walkways.at(p.x, p.y)) {
          Walkways::Deck d;
          d.type = this->pathTool;
          std::copy(p.h, p.h + 4, d.h);
          this->walkways.set(p.x, p.y, d);
          r.cost += this->pathCost * 2;
        }
      }
      this->deckPreview.clear();
      this->pathPreview.clear();
      this->pathLast = {-1, -1};
      return r;
    }
    for (auto &p : this->pathPreview)
      if (p.second == Fences::Fit::Ok && canLayPath(p.first.first, p.first.second)) {
        this->worldMap.setPath(p.first.first, p.first.second, this->pathTool);
        r.cost += this->pathCost;
      }
    this->pathPreview.clear();
    this->pathLast = {-1, -1};
    return r;
  }
  if (!this->dragging || this->fenceTool < 0)
    return r;
  this->mouseMove(x, y);
  this->dragging = false;
  const int cost = this->fences.types()[this->fenceTool].cost;
  for (auto &p : this->fences.preview) {
    if (p.second && this->fences.canPlace(p.first, this->worldMap)) {
      this->fences.place(p.first, this->fenceTool, this->dragCount, this->worldMap);
      r.cost += cost;
    } else if (this->fences.fit(p.first, this->worldMap) == Fences::Fit::Outside &&
               this->fences.outsideZooWall(p.first, this->worldMap)) {
      r.outsideZoo = true;
    }
  }
  this->fences.preview.clear();
  if (r.cost > 0)
    r.newExhibits =
        this->fences.updateExhibits(this->worldMap, date.day, date.month, date.year);
  return r;
}

int World::confirmDrain() {
  if (this->pendingDrain.x < 0)
    return 0;
  int refund = this->fences.refundOf(this->pendingDrain);
  this->fences.remove(this->pendingDrain, this->worldMap);
  this->pendingDrain = {false, -1, -1};
  this->highlight = {false, -1, -1};
  this->fences.updateExhibits(this->worldMap, date.day, date.month, date.year);
  return refund;
}
