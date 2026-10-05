#include "World.hpp"
#include "ItemCatalog.hpp"
#include "IniReader.hpp"
#include "Utils.hpp"
#include "Sound.hpp"
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
    this->startAmbience(path);
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
  animals.loadTypes(resourceManager);
  animals.clear();
  animals.setItems(&items);
  animals.setIndex(&index);
  animals.setObjects(&placedObjects);
  staff.setAnimals(&animals);
  guests.load(resourceManager);
  guests.clear();
  guests.setWorld(&worldMap, &fences, &animals, &index);
  guests.setObjects(&placedObjects, &items);
  staff.setGuests(&guests);
  animals.findPrey = [this](float x, float y, float r) { return this->guests.nearestPrey(x, y, r); };
  animals.preyAt = [this](int g, float &x, float &y) { return this->guests.preyPosition(g, x, y); };
  animals.caughtPrey = [this](int g, const Animals::Member &by) {
    const Animals::Type &t = this->animals.types()[by.type];
    std::string key = t.file.substr(t.file.find_last_of('/') + 1);
    key = key.substr(0, key.find('.'));
    this->guests.caught(g, key, by.female, by.name);
  };
  staff.setObjects(&placedObjects);
  guests.guideSpeaking = [this](int id) { return this->staff.guideSpeaking(id); };
  guests.guideBonusOf = [this](int id) {
    for (const Staff::Member &m : this->staff.members())
      if (m.id == id)
        return this->staff.types()[m.type].tourBonus;
    return 30;
  };
  guests.tourBonusAt = [this](int exhibit) { return this->staff.guideBonusAt(this->guests.viewingTiles(exhibit)); };
  terrain.load(resourceManager);
  terrain.setMap(&worldMap, &fences);
  fences.edgeBlocked = [this](const Fences::Edge &e) {
    // (an edge runs a tile along one side: blocked when a footprint covers
    // part of its length on both sides of it)
    for (const PlacedObjects::Object &o : this->placedObjects.objects()) {
      if (o.fence)
        continue;
      auto [fx, fy] = this->footprintOf(PlacedObjects::fileOf(o));
      if (fx <= 0 || fy <= 0)
        continue;
      if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
        std::swap(fx, fy);
      float hx = fx / 4.0f, hy = fy / 4.0f;
      if (e.alongX) {
        if (o.y - hy < e.y - 0.01f && o.y + hy > e.y + 0.01f &&
            std::min(o.x + hx, e.x + 1.0f) - std::max(o.x - hx, static_cast<float>(e.x)) > 0.01f)
          return true;
      } else {
        if (o.x - hx < e.x - 0.01f && o.x + hx > e.x + 0.01f &&
            std::min(o.y + hy, e.y + 1.0f) - std::max(o.y - hy, static_cast<float>(e.y)) > 0.01f)
          return true;
      }
    }
    return false;
  };
  terrain.hasBigObject = [this](int x, int y) {
    WorldIndex::What w = this->index.at(x, y);
    return w == WorldIndex::What::Object || w == WorldIndex::What::Building;
  };
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
  SDL_Log("[World]   Q / E (or the rotate buttons): Rotate view");
  SDL_Log("[World]   Debug views are console commands: flat, tiles, terrainids");
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

  // Sounds hold while paused; animals don't react
  Sound::get().setPaused(paused);
  animals.paused = paused;
  // Where a point is from the middle of the view, in screen pixels (for
  // sounds on the map)
  if (!staff.viewOffset) {
    staff.viewOffset = [this](float x, float y, float &dx, float &dy) { return this->animals.viewOffset(x, y, dx, dy); };
    ambient.viewOffset = staff.viewOffset;
  }
  if (!animals.viewOffset)
    animals.viewOffset = [this](float x, float y, float &dx, float &dy) {
      const Camera &cam = this->worldRenderer.getCamera();
      float zoom = cam.zoom > 0 ? cam.zoom : 1.0f;
      const MapTile *t = this->worldMap.getTile(static_cast<int>(x), static_cast<int>(y));
      float sx, sy, d;
      this->worldRenderer.worldToScreenF(x, y, t ? static_cast<float>(t->cornerHeight[0]) : 0.0f, sx, sy, d);
      float cx = this->viewRect.w / 2.0f / (zoom * this->viewScale);
      float cy = this->viewRect.h / 2.0f / (zoom * this->viewScale);
      dx = (sx - cx) * zoom;
      dy = (sy - cy) * zoom;
      return true;
    };
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
    animals.addToIndex(index);
    guests.addToIndex(index);
    clock += deltaTime;
    // A bird call now and then, somewhere about the view (worldsnd.cfg:
    // a 1 in <chance> try a second)
    birdClock += deltaTime;
    while (birdClock >= 1.0f) {
      birdClock -= 1.0f;
      int total = 0;
      for (const BirdCall &b : birdCalls)
        total += b.prob;
      if (birdChance > 0 && total > 0 && std::rand() % birdChance == 0) {
        int roll = std::rand() % total;
        for (const BirdCall &b : birdCalls)
          if ((roll -= b.prob) < 0) {
            float dx = static_cast<float>(std::rand() % 400 - std::rand() % 400);
            float dy = static_cast<float>(std::rand() % 300 - std::rand() % 300);
            Sound::get().playAt(b.file, dx, dy);
            break;
          }
      }
    }
    // Which exhibit each animal is in (one opened up: loose; a fence closed
    // round them: in that)
    animals.rehome(worldMap, fences);
    // Each exhibit's animals, for its keepers
    for (const Fences::Exhibit &ex : fences.exhibits())
      if (Fences::Exhibit *e = fences.exhibit(ex.id))
        e->animals = animals.countIn(ex.id);
    staff.update(deltaTime, worldMap, fences);
    // Gates open for staff coming through
    fences.updateGates(deltaTime, [this](const Fences::Edge &e) {
      float mx = e.alongX ? e.x + 0.5f : static_cast<float>(e.x);
      float my = e.alongX ? static_cast<float>(e.y) : e.y + 0.5f;
      for (const Staff::Member &m : this->staff.members())
        if (std::hypot(m.x - mx, m.y - my) < 1.1f)
          return true;
      return false;
    });
    animals.update(deltaTime, clock, worldMap, fences);
    animals.lifeEvents(worldMap, fences);
    // Fences animals broke getting out
    for (const Fences::Edge &e : animals.takeBreaks())
      fences.breakPiece(e);
    // (the entrance found once the zoo wall is known)
    if (!guests.hasEntrance())
      this->findEntrance();
    guests.update(deltaTime, zooRating, zooAdmission);
    // Each exhibit's viewing time and donations on its books; an exhibit's
    // clock starts when it's first seen
    for (const auto &[id, b] : guests.takeExhibitBooks())
      if (Fences::Exhibit *ex = fences.exhibit(id)) {
        ex->viewed += b.viewed;
        ex->donationsNow += b.donated;
        ex->donationsTotal += b.donated;
      }
    for (const Fences::Exhibit &ex : fences.exhibits())
      if (ex.builtAt < 0)
        if (Fences::Exhibit *e = fences.exhibit(ex.id))
          e->builtAt = guests.clock;
    guests.updateSounds(deltaTime);
    this->updateBuildingSounds(deltaTime);
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
  // The mouse at the window's edge scrolls the map, as in the original
  // (zoo.exe 0x419eb2): within 3 px of the left or top, 2 of the right or
  // bottom (8 and 7 in a window), corners both ways, 32 screen pixels 30
  // times a second (mouseScrollX/Y, at every zoom), not while a terraform
  // drag is under way; while the window has the mouse and the focus
  if (this->edgeScroll && !this->terrain.isHeld())
    if (SDL_Window *w = SDL_GetMouseFocus())
      if (SDL_GetKeyboardFocus() == w) {
        int mx = 0, my = 0, ww = 0, wh = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_GetWindowSize(w, &ww, &wh);
        bool full = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
        const int near = full ? 3 : 8, far = full ? 2 : 7;
        float edgeSpeed = kEdgeScrollPixels * 30.0f * deltaTime / cam.zoom;
        int step = static_cast<int>(std::lround(edgeSpeed));
        if (mx <= near)
          cam.x += step;
        else if (mx >= ww - far)
          cam.x -= step;
        if (my <= near)
          cam.y += step;
        else if (my >= wh - far)
          cam.y -= step;
      }

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

  // (the old debug keys 8 / 9 tile size, E flat view, R rotate and 0
  // terrain ids are console commands now: E and R clashed with the rotate
  // hotkeys and flattened the map when a player turned the view)
  if (false) {
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

  if (false) {
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
  if (false) {
    worldRenderer.toggleElevation();
    SDL_Log("[World] Elevation: %s",
            worldRenderer.isElevationEnabled() ? "ON" : "OFF");
    keyTimer = 0;
  }

  // Rotate view 90 degrees
  if (false) {
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
  if (false) {
    worldRenderer.toggleTerrainDebug();
    SDL_Log("[World] Terrain Debug Mode: %s",
            worldRenderer.isTerrainDebugEnabled() ? "ON" : "OFF");
    keyTimer = 0;
  }
}

bool World::shrinkTarget(SDL_Renderer *renderer, int level, int w, int h) {
  ShrinkLayer &l = this->shrinkLayers[level];
  if (l.tex && l.w >= w && l.h >= h && l.w <= w + 64 && l.h <= h + 64) {
    if (l.w == w && l.h == h)
      return true;
  }
  if (l.tex)
    SDL_DestroyTexture(l.tex);
  l.tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
  l.w = w;
  l.h = h;
  if (!l.tex)
    return false;
  SDL_SetTextureScaleMode(l.tex, SDL_ScaleModeLinear);
  return true;
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
  // Zoomed out, the map is drawn at 1:1 into a layer and shrunk into the
  // view smoothly (halving at a time, like mipmaps): shrunk as it's drawn,
  // the art lost most of its pixels and looked rough
  SDL_Texture *savedTarget = SDL_GetRenderTarget(renderer);
  const int fullW = static_cast<int>(std::ceil(view.w / zoom)),
            fullH = static_cast<int>(std::ceil(view.h / zoom));
  const bool shrink = !gpuFsr && zoom < 0.999f && fullW <= 8192 && fullH <= 8192 &&
                      shrinkTarget(renderer, 0, fullW, fullH);
  if (shrink) {
    SDL_SetRenderTarget(renderer, this->shrinkLayers[0].tex);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
  } else if (gpuFsr) {
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
  fences.redrawGround = [this](SDL_Renderer *r, const std::set<std::pair<int, int>> &tiles) {
    this->worldRenderer.renderTerrainTiles(r, this->worldMap, SpriteDatabase::get(), tiles);
  };
  fences.collect(worldRenderer, worldMap, fenceArt);
  // The bulldozer's piece glows red; the gate tool's green
  if ((bulldozer || gateTool) && highlight.x >= 0) {
    std::vector<Fences::Drawable> only;
    fences.preview = {{highlight, gateTool}};
    fences.deleting = bulldozer;
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
  animals.collect(worldRenderer, worldMap, fenceArt);
  guests.collect(worldRenderer, worldMap, fenceArt);
  if (this->objectGhost && !this->objectTool.empty())
    if (Animation *a = this->placedObjects.artOfFile(this->objectTool)) {
      const MapTile *t = worldMap.getTile(static_cast<int>(std::floor(ghostX)), static_cast<int>(std::floor(ghostY)));
      float h = 0;
      if (t) {
        float fx = ghostX - std::floor(ghostX), fy = ghostY - std::floor(ghostY);
        float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
        float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
        h = top * (1 - fy) + bottom * fy;
      }
      float sx, sy, depth;
      worldRenderer.worldToScreenF(ghostX, ghostY, h, sx, sy, depth);
      float angle = ghostFacing * 3.14159265f / 4.0f;
      SDL_Color tint = ghostFit == Fences::Fit::Ok ? SDL_Color{0, 255, 0, 255} : SDL_Color{255, 60, 60, 255};
      fenceArt.push_back({depth, sx, sy, a, worldRenderer.screenSide(std::sin(angle), -std::cos(angle)), tint, true});
    }
  placedObjects.draw(renderer, worldRenderer, worldMap, fenceArt, [this](float x, float y) {
    return Features::elevatedPaths &&
           this->walkways.at(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))) != nullptr;
  });
  // (debug) footprints
  if (this->debugFootprints)
    for (const PlacedObjects::Object &o : this->placedObjects.objects()) {
      if (o.fence || !o.bought)
        continue;
      auto [fx, fy] = this->footprintOf(PlacedObjects::fileOf(o));
      if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
        std::swap(fx, fy);
      float hx = fx / 4.0f, hy = fy / 4.0f;
      const float px[5] = {o.x - hx, o.x + hx, o.x + hx, o.x - hx, o.x - hx};
      const float py[5] = {o.y - hy, o.y - hy, o.y + hy, o.y + hy, o.y - hy};
      SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
      float gz = 0;
      if (const MapTile *t = this->worldMap.getTile(static_cast<int>(std::floor(o.x)), static_cast<int>(std::floor(o.y))))
        gz = static_cast<float>(t->cornerHeight[0]);
      for (int k = 0; k < 4; k++) {
        float ax, ay, bx, by, d;
        worldRenderer.worldToScreenF(px[k], py[k], gz, ax, ay, d);
        worldRenderer.worldToScreenF(px[k + 1], py[k + 1], gz, bx, by, d);
        SDL_RenderDrawLineF(renderer, ax, ay, bx, by);
      }
    }
  // Staff up on the decks, sorted in with them
  std::stable_sort(deckArt.begin(), deckArt.end(),
                   [](const Fences::Drawable &a, const Fences::Drawable &b) { return a.depth < b.depth; });
  for (Fences::Drawable &d : deckArt) {
    if (d.custom)
      d.custom(renderer);
    else if (d.art)
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tinted ? &d.tint : nullptr, d.frame);
  }
  // The path tool's walkway under the cursor, lit over its deck
  if (this->deckOverlay.size() == 4) {
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer, nullptr, this->deckOverlay.data(), 4, idx, 6);
  }
  this->deckOverlay.clear();
  this->drawBrush(renderer);
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
  if (shrink) {
    int level = 0, w = fullW, h = fullH;
    while (w / 2 >= view.w && h / 2 >= view.h && level + 1 < kShrinkLevels &&
           shrinkTarget(renderer, level + 1, w / 2, h / 2)) {
      SDL_SetRenderTarget(renderer, this->shrinkLayers[level + 1].tex);
      SDL_RenderSetViewport(renderer, nullptr);
      SDL_RenderCopy(renderer, this->shrinkLayers[level].tex, nullptr, nullptr);
      level++;
      w /= 2;
      h /= 2;
    }
    SDL_SetRenderTarget(renderer, savedTarget);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_Rect src = {0, 0, w, h};
    SDL_RenderCopy(renderer, this->shrinkLayers[level].tex, &src, &view);
  }

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
  // The bulldozer's path tile: red (measured from the original)
  if (this->bulldozer && this->bulldozePath.first >= 0) {
    auto [x, y] = this->bulldozePath;
    if (const MapTile *t = this->worldMap.getTile(x, y)) {
      const float cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
      const int ci[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X1Y1, CORNER_X0Y1};
      SDL_Vertex v[4];
      for (int i = 0; i < 4; i++) {
        float sx, sy, d;
        this->worldRenderer.worldToScreenF(x + cx[i], y + cy[i],
                                           static_cast<float>(t->cornerHeight[ci[i]]), sx, sy, d);
        v[i] = {{sx, sy}, SDL_Color{230, 40, 20, 150}, {0, 0}};
      }
      int idx[6] = {0, 1, 2, 0, 2, 3};
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }
    return;
  }
  const bool filter = this->filterTool && this->fences.previewFilterX >= 0;
  if (!this->pathTool.empty()) {
    // The tiles a path would go on: green, red where it can't
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    // Over a walkway: its deck lit, at its height
    if (!this->pathDragging && !this->line.active && this->hoverDeck.first >= 0)
      if (const Walkways::Deck *dk = this->walkways.at(this->hoverDeck.first, this->hoverDeck.second)) {
        const float cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
        const int ci[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X1Y1, CORNER_X0Y1};
        SDL_Vertex v[4];
        for (int i = 0; i < 4; i++) {
          float sx, sy, d;
          this->worldRenderer.worldToScreenF(this->hoverDeck.first + cx[i], this->hoverDeck.second + cy[i],
                                             dk->h[ci[i]] + 0.05f, sx, sy, d);
          v[i] = {{sx, sy}, SDL_Color{49, 150, 24, 200}, {0, 0}};
        }
        int idx[6] = {0, 1, 2, 0, 2, 3};
        this->deckOverlay.assign(v, v + 4);
        (void)idx;
      }
    for (auto &p : this->pathPreview) {
      auto [x, y] = p.first;
      if (this->line.active || this->smartPlan) {
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
    // (over a fence it replaces: entityoverwritecolor, zoo.exe 0x4df7eb)
    if (p.second && this->fences.at(e))
      SDL_SetRenderDrawColor(renderer, 225, 112, 0, 255);
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
  // The spot on the ground under the middle of the view (as drawn, hills
  // and all) stays there after rotating. Off the map's edge, the nearest
  // spot on the map.
  float wx, wy;
  if (!this->pick(this->viewRect.x + this->viewRect.w / 2, this->viewRect.y + this->viewRect.h / 2, wx, wy)) {
    float u, v;
    worldRenderer.getViewCentre(u, v);
    int fu = static_cast<int>(std::floor(u)), fv = static_cast<int>(std::floor(v));
    int x00, y00, x10, y10, x01, y01;
    worldRenderer.viewToWorldVertex(fu, fv, x00, y00);
    worldRenderer.viewToWorldVertex(fu + 1, fv, x10, y10);
    worldRenderer.viewToWorldVertex(fu, fv + 1, x01, y01);
    float a = u - fu, b = v - fv;
    wx = x00 + (x10 - x00) * a + (x01 - x00) * b;
    wy = y00 + (y10 - y00) * a + (y01 - y00) * b;
  }
  wx = std::clamp(wx, 0.5f, this->worldMap.getWidth() - 0.5f);
  wy = std::clamp(wy, 0.5f, this->worldMap.getHeight() - 0.5f);
  worldRenderer.rotateView(steps);
  this->centreOn(wx, wy);
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
  this->smartPlan = false;
  this->line = {};
  this->deckPreview.clear();
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
  // Under a walkway only with headroom (three units, a walker's height)
  if (Features::elevatedPaths)
    if (const Walkways::Deck *d = this->walkways.at(x, y)) {
      const MapTile *t = this->worldMap.getTile(x, y);
      if (*std::min_element(d->h, d->h + 4) - *std::max_element(t->cornerHeight, t->cornerHeight + 4) < 3)
        return Fences::Fit::InTheWay;
    }
  return Fences::Fit::Ok;
}

// A path drag's tiles planned as one when the ground alone can't carry it
// (a cliff between two of them, a tile too steep, a walkway on the way):
// a height for each edge it crosses, as low as the ground allows, never
// more than a unit's change a tile and level where it turns; tiles that
// come out on the ground are path, the rest walkway (stairs where it
// climbs). Joining a walkway, it meets that walkway's edge.
void World::planSmartPath() {
  this->smartPlan = false;
  this->deckPreview.clear();
  if (!Features::elevatedPaths || !raisable(this->pathTool) || this->pathPreview.size() < 2)
    return;
  const size_t n = this->pathPreview.size();
  std::vector<std::pair<int, int>> tiles;
  for (auto &p : this->pathPreview)
    tiles.push_back(p.first);
  std::vector<int> dir(n - 1);
  for (size_t i = 0; i + 1 < n; i++) {
    int dx = tiles[i + 1].first - tiles[i].first, dy = tiles[i + 1].second - tiles[i].second;
    if (std::abs(dx) + std::abs(dy) != 1)
      return; // (not a chain of neighbours)
    dir[i] = dx > 0 ? 1 : dx < 0 ? 3 : dy > 0 ? 2 : 0;
  }
  auto groundEdge = [&](int x, int y, int d, int &a, int &b) {
    const MapTile *t = this->worldMap.getTile(x, y);
    int c0, c1;
    Walkways::edgeCorners(d, c0, c1);
    a = t ? t->cornerHeight[c0] : 0;
    b = t ? t->cornerHeight[c1] : 0;
  };
  // Needed at all? A cliff between two tiles, a tile the ground path can't
  // take, a walkway on the way
  bool needed = false;
  for (size_t i = 0; i < n && !needed; i++) {
    auto [x, y] = tiles[i];
    if (this->walkways.at(x, y) || !this->worldMap.pathShapeOk(x, y))
      needed = true;
    if (i + 1 < n) {
      int a, b, p, q;
      groundEdge(x, y, dir[i], a, b);
      groundEdge(tiles[i + 1].first, tiles[i + 1].second, (dir[i] + 2) & 3, p, q);
      if (a != p || b != q)
        needed = true;
    }
  }
  if (!needed)
    return;
  // Edges: E[0] the first tile's back, E[i] between tiles i - 1 and i,
  // E[n] the last tile's front. Lower bounds from the ground either side;
  // a walkway on the way fixes its edges.
  const int back = (dir[0] + 2) & 3;
  std::vector<int> lo(n + 1, INT32_MIN), fixedAt(n + 1, INT32_MIN);
  std::vector<bool> bad(n, false);
  auto entryDir = [&](size_t i) { return i == 0 ? back : (dir[i - 1] + 2) & 3; };
  auto exitDir = [&](size_t i) { return i + 1 < n ? dir[i] : dir[n - 2]; };
  for (size_t i = 0; i < n; i++) {
    auto [x, y] = tiles[i];
    int a, b;
    groundEdge(x, y, entryDir(i), a, b);
    lo[i] = std::max(lo[i], std::max(a, b));
    groundEdge(x, y, exitDir(i), a, b);
    lo[i + 1] = std::max(lo[i + 1], std::max(a, b));
    if (this->walkways.at(x, y)) {
      for (int k = 0; k < 2; k++) {
        int e0, e1;
        this->walkways.edgeHeights(x, y, k == 0 ? entryDir(i) : exitDir(i), e0, e1);
        size_t edge = k == 0 ? i : i + 1;
        if (e0 != e1 || (fixedAt[edge] != INT32_MIN && fixedAt[edge] != e0))
          bad[i] = true;
        fixedAt[edge] = e0;
      }
    }
  }
  std::vector<int> E(n + 1);
  for (size_t i = 0; i <= n; i++)
    E[i] = fixedAt[i] != INT32_MIN ? fixedAt[i] : lo[i];
  // Raised only as far as needed: a unit a tile at most, level at turns
  for (size_t pass = 0; pass < 4 * n + 8; pass++) {
    bool changed = false;
    for (size_t i = 0; i <= n; i++) {
      int want = E[i];
      if (i > 0)
        want = std::max(want, E[i - 1] - 1);
      if (i < n)
        want = std::max(want, E[i + 1] - 1);
      if (want != E[i] && fixedAt[i] == INT32_MIN) {
        E[i] = want;
        changed = true;
      }
    }
    for (size_t i = 1; i + 1 < n; i++)
      if (dir[i - 1] != dir[i] && E[i] != E[i + 1]) {
        int m = std::max(E[i], E[i + 1]);
        if (fixedAt[i] == INT32_MIN)
          E[i] = m;
        if (fixedAt[i + 1] == INT32_MIN)
          E[i + 1] = m;
        changed = true;
      }
    if (!changed)
      break;
  }
  for (size_t i = 0; i < n; i++) {
    auto [x, y] = tiles[i];
    DeckPreview p{x, y, {0, 0, 0, 0}, false, false};
    if (const Walkways::Deck *have = this->walkways.at(x, y)) {
      // A walkway already there: kept as it is
      std::copy(have->h, have->h + 4, p.h);
      p.ok = !bad[i] && std::abs(E[i] - E[i + 1]) <= 1;
      this->deckPreview.push_back(p);
      continue;
    }
    int d = exitDir(i);
    Walkways::shape(d, E[i], E[i + 1], p.h);
    if (std::abs(E[i] - E[i + 1]) > 1)
      bad[i] = true;
    const MapTile *t = this->worldMap.getTile(x, y);
    bool onGround = t != nullptr;
    for (int c = 0; c < 4 && onGround; c++)
      onGround = p.h[c] == t->cornerHeight[c];
    if (onGround) {
      p.ground = true;
      Fences::Fit f = this->pathFit(x, y);
      p.ok = !bad[i] && (f == Fences::Fit::Ok || (f == Fences::Fit::InTheWay && this->worldMap.isPath(x, y)));
    } else {
      bool clear = true;
      for (const PlacedObjects::Object &o : this->placedObjects.objects())
        if (!o.fence && static_cast<int>(std::floor(o.x)) == x && static_cast<int>(std::floor(o.y)) == y)
          clear = false;
      p.ok = !bad[i] && clear && this->walkways.canBuild(x, y, p.h, this->worldMap, this->fences);
    }
    this->deckPreview.push_back(p);
  }
  for (size_t i = 0; i < n; i++)
    this->pathPreview[i].second = this->deckPreview[i].ok ? Fences::Fit::Ok : Fences::Fit::Outside;
  this->smartPlan = true;
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
  if (this->line.active) {
    this->layDeckPreview();
    int n = 0;
    for (auto &p : this->deckPreview)
      n += p.ok ? (p.ground ? (this->worldMap.isPath(p.x, p.y) ? 0 : 1)
                            : (this->walkways.at(p.x, p.y) ? 0 : 2))
                : 0;
    this->hoverPrice = n * this->pathCost;
  }
}

void World::startLine(int tx, int ty, bool fromDeck) {
  this->line = {};
  this->line.active = true;
  this->line.fromDeck = fromDeck;
  this->line.sx = this->line.ex = tx;
  this->line.sy = this->line.ey = ty;
  this->pathDragging = true;
  this->layDeckPreview();
  const DeckPreview &p = this->deckPreview.front();
  this->hoverPrice = p.ok && p.ground && !this->worldMap.isPath(tx, ty) ? this->pathCost : 0;
}

// The height a walkway leaves its start tile at, going 'dir': the deck's
// edge there, or the ground's
int World::lineStartLevel(int dir) const {
  int a, b;
  if (this->line.fromDeck && this->walkways.edgeHeights(this->line.sx, this->line.sy, dir, a, b))
    return std::max(a, b);
  const MapTile *t = this->worldMap.getTile(this->line.sx, this->line.sy);
  if (!t)
    return 0;
  int c0, c1;
  Walkways::edgeCorners(dir, c0, c1);
  return std::max(t->cornerHeight[c0], t->cornerHeight[c1]);
}

// The end: the tile whose middle (as drawn, at the height the walkway
// would have there: the start's, climbing a unit a tile to the build
// height, or a path or walkway it would come down onto) is nearest the
// cursor. The way there is the path mode's (a bend, or straight).
void World::aimLine(int x, int y) {
  const Camera &cam = worldRenderer.getCamera();
  float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
  float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
  Camera &c = worldRenderer.getCamera();
  float saved = c.zoom;
  c.zoom = zoom;
  const MapTile *st = this->worldMap.getTile(this->line.sx, this->line.sy);
  int target = (st ? *std::min_element(st->cornerHeight, st->cornerHeight + 4) : 0) + this->buildHeight;
  int start = this->lineStartLevel(1);
  float best = 1e30f;
  int bx = this->line.sx, by = this->line.sy;
  const int R = 48;
  for (int ty = this->line.sy - R; ty <= this->line.sy + R; ty++)
    for (int tx = this->line.sx - R; tx <= this->line.sx + R; tx++) {
      const MapTile *t = this->worldMap.getTile(tx, ty);
      if (!t)
        continue;
      int k = std::abs(tx - this->line.sx) + std::abs(ty - this->line.sy);
      // Straight: only along one axis
      if (this->pathMode == FenceMode::Straight && tx != this->line.sx && ty != this->line.sy)
        continue;
      float h;
      if (k == 0 && this->line.fromDeck)
        h = this->walkways.heightAt(tx, ty, 0.5f, 0.5f);
      else if (k > 0 && this->walkways.at(tx, ty))
        h = this->walkways.heightAt(tx, ty, 0.5f, 0.5f);
      else if (k == 0 || this->worldMap.isPath(tx, ty))
        h = (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) * 0.25f;
      else
        h = static_cast<float>(std::clamp(target, start - k, start + k));
      float sx, sy, d;
      worldRenderer.worldToScreenF(tx + 0.5f, ty + 0.5f, h, sx, sy, d);
      float dist = (sx - lx) * (sx - lx) + (sy - ly) * (sy - ly);
      if (dist < best) {
        best = dist;
        bx = tx;
        by = ty;
      }
    }
  c.zoom = saved;
  this->line.ex = bx;
  this->line.ey = by;
  if (this->line.firstAxis < 0 && (bx != this->line.sx || by != this->line.sy))
    this->line.firstAxis = std::abs(bx - this->line.sx) >= std::abs(by - this->line.sy) ? 0 : 1;
}

std::vector<std::pair<int, int>> World::modeTiles(int sx, int sy, int ex, int ey, int firstAxis) const {
  std::vector<std::pair<int, int>> out = {{sx, sy}};
  int axis = firstAxis >= 0 ? firstAxis : (std::abs(ex - sx) >= std::abs(ey - sy) ? 0 : 1);
  if (this->pathMode == FenceMode::BendOther)
    axis = 1 - axis;
  if (this->pathMode == FenceMode::Straight) {
    // along the longer way only
    axis = std::abs(ex - sx) >= std::abs(ey - sy) ? 0 : 1;
    if (axis == 0)
      ey = sy;
    else
      ex = sx;
  }
  int x = sx, y = sy;
  auto run = [&](int a) {
    while (a == 0 ? x != ex : y != ey) {
      if (a == 0)
        x += ex > x ? 1 : -1;
      else
        y += ey > y ? 1 : -1;
      out.push_back({x, y});
    }
  };
  run(axis);
  run(1 - axis);
  return out;
}

// A ground path drag in the path mode (pathmodes on): from the press's
// tile to the one under the cursor, bent once or straight
void World::layPathDrag(int tx, int ty) {
  auto [px, py] = this->pathPress;
  if (this->pathFirstAxis < 0 && (tx != px || ty != py))
    this->pathFirstAxis = std::abs(tx - px) >= std::abs(ty - py) ? 0 : 1;
  this->pathPreview.clear();
  for (auto [x, y] : this->modeTiles(px, py, tx, ty, this->pathFirstAxis))
    this->pathPreview.push_back({{x, y}, this->pathFit(x, y)});
}

void World::cyclePathMode(int step) {
  int n = (static_cast<int>(this->pathMode) + step + 3) % 3; // (no box for paths)
  this->pathMode = static_cast<FenceMode>(n);
  if (this->line.active) {
    this->layDeckPreview();
  } else if (this->pathDragging && this->pathLast.first >= 0) {
    this->layPathDrag(this->pathLast.first, this->pathLast.second);
    this->planSmartPath();
  }
}

// What a walkway line builds, then its tiles' prices
int World::buildLine() {
  int cost = 0;
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
        cost += this->pathCost;
      }
    } else if (!this->walkways.at(p.x, p.y)) {
      Walkways::Deck d;
      d.type = this->pathTool;
      std::copy(p.h, p.h + 4, d.h);
      this->walkways.set(p.x, p.y, d);
      cost += this->pathCost * 2;
    }
  }
  this->deckPreview.clear();
  this->pathPreview.clear();
  return cost;
}

// A walkway line's tiles, the path mode's way from its start to its end
// (bent once, level at the turn). From its start (the ground there gets a
// path; a deck stays as it is) a unit a tile up or down toward the build
// height, then level. Ending on a ground path or a deck, it comes down (or
// up) to meet it: the stairs at both ends are made for you. Tiles that come
// out at ground level are ground path.
void World::layDeckPreview() {
  this->deckPreview.clear();
  this->pathPreview.clear();
  if (!this->line.active)
    return;
  const int sx = this->line.sx, sy = this->line.sy, ex = this->line.ex, ey = this->line.ey;
  std::vector<std::pair<int, int>> T =
      Features::pathModes ? this->modeTiles(sx, sy, ex, ey, this->line.firstAxis)
                          : std::vector<std::pair<int, int>>{};
  if (!Features::pathModes) {
    // (pathmodes off: straight only, as first made)
    T = {{sx, sy}};
    int dx = ex > sx ? 1 : ex < sx ? -1 : 0, dy = ey > sy ? 1 : ey < sy ? -1 : 0;
    if (dx && dy)
      std::abs(ex - sx) >= std::abs(ey - sy) ? dy = 0 : dx = 0;
    int x = sx, y = sy;
    while ((dx && x != ex) || (dy && y != ey)) {
      x += dx;
      y += dy;
      T.push_back({x, y});
    }
  }
  const int n = static_cast<int>(T.size());
  std::vector<int> D(std::max(1, n - 1), 1); // the way from tile i to i + 1
  for (int i = 0; i + 1 < n; i++) {
    int dx = T[i + 1].first - T[i].first, dy = T[i + 1].second - T[i].second;
    D[i] = dx > 0 ? 1 : dx < 0 ? 3 : dy > 0 ? 2 : 0;
  }
  auto turn = [&](int i) { return i > 0 && i + 1 < n && D[i - 1] != D[i]; };
  const MapTile *st = this->worldMap.getTile(sx, sy);
  if (!st)
    return;
  const int start = this->lineStartLevel(D[0]);
  const int target = *std::min_element(st->cornerHeight, st->cornerHeight + 4) + this->buildHeight;
  // The far end: onto a ground path or a deck there, its edge's height
  bool join = false;
  int endLevel = 0;
  int last = n - 1; // the last tile built as walkway
  if (n >= 2) {
    int a, b;
    const int back = (D[n - 2] + 2) & 3;
    if (this->walkways.edgeHeights(ex, ey, back, a, b)) {
      join = true;
      endLevel = std::max(a, b);
    } else if (this->worldMap.isPath(ex, ey)) {
      const MapTile *t = this->worldMap.getTile(ex, ey);
      int c0, c1;
      Walkways::edgeCorners(back, c0, c1);
      join = true;
      endLevel = std::max(t->cornerHeight[c0], t->cornerHeight[c1]);
    }
    int slopes = 0;
    for (int i = 1; i <= n - 2; i++)
      slopes += turn(i) ? 0 : 1;
    if (join && std::abs(endLevel - start) <= slopes)
      last = n - 2;
    else
      join = false;
  }
  // Edge heights: level[i] is where tile i meets tile i + 1
  std::vector<int> level(n, start);
  for (int i = 1; i <= last; i++) {
    if (turn(i)) {
      level[i] = level[i - 1];
      continue;
    }
    int l = std::clamp(target, level[i - 1] - 1, level[i - 1] + 1);
    if (join) {
      int rem = 0; // slopes still to come before the end
      for (int k = i + 1; k <= last; k++)
        rem += turn(k) ? 0 : 1;
      l = std::clamp(l, endLevel - rem, endLevel + rem);
    }
    level[i] = l;
  }
  // The start tile: as it is (a deck stays; the ground gets a path)
  {
    DeckPreview p{sx, sy, {start, start, start, start}, !this->line.fromDeck, true};
    if (p.ground)
      p.ok = this->pathFit(sx, sy) != Fences::Fit::Outside;
    else
      std::copy(this->walkways.at(sx, sy)->h, this->walkways.at(sx, sy)->h + 4, p.h);
    this->deckPreview.push_back(p);
  }
  for (int i = 1; i <= last; i++) {
    auto [x, y] = T[i];
    DeckPreview p{x, y, {0, 0, 0, 0}, false, false};
    if (turn(i))
      for (int c = 0; c < 4; c++)
        p.h[c] = level[i];
    else
      Walkways::shape(D[i - 1], level[i - 1], level[i], p.h);
    const MapTile *t = this->worldMap.getTile(x, y);
    bool onGround = t != nullptr;
    for (int c = 0; c < 4 && onGround; c++)
      onGround = p.h[c] == t->cornerHeight[c];
    if (onGround) {
      p.ground = true;
      Fences::Fit f = this->pathFit(x, y);
      p.ok = f == Fences::Fit::Ok || (f == Fences::Fit::InTheWay && this->worldMap.isPath(x, y));
    } else if (const Walkways::Deck *have = this->walkways.at(x, y)) {
      // Across a deck already there: only the same one
      p.ok = std::equal(p.h, p.h + 4, have->h);
    } else {
      // (not over a rock or a tree: they'd stand up through it)
      bool clear = true;
      for (const PlacedObjects::Object &o : this->placedObjects.objects())
        if (!o.fence && static_cast<int>(std::floor(o.x)) == x && static_cast<int>(std::floor(o.y)) == y)
          clear = false;
      p.ok = clear && this->walkways.canBuild(x, y, p.h, this->worldMap, this->fences);
    }
    this->deckPreview.push_back(p);
  }
  // The end it joins: as it is
  if (join) {
    DeckPreview p{ex, ey, {0, 0, 0, 0}, this->walkways.at(ex, ey) == nullptr, true};
    if (const Walkways::Deck *d = this->walkways.at(ex, ey))
      std::copy(d->h, d->h + 4, p.h);
    this->deckPreview.push_back(p);
  }
  for (const DeckPreview &p : this->deckPreview)
    this->pathPreview.push_back({{p.x, p.y}, p.ok ? Fences::Fit::Ok : Fences::Fit::Outside});
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
      // Low (stairs off the ground, a deck a unit or two up): built solid
      // under it, as RCT's low paths and stairs
      const int cornerOrder[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X1Y1, CORNER_X0Y1};
      // (the ground in front of its post, in its screen column, hides the
      // post's lower part: the supports are drawn after all the ground, so a
      // raised block in front would otherwise show them through it)
      SDL_Rect oldClip;
      SDL_RenderGetClipRect(r, &oldClip);
      bool hadClip = SDL_RenderIsClipEnabled(r);
      {
        float px0, py0, d0, ax, ay, bx, by, dd;
        this->worldRenderer.worldToScreenF(x + 0.5f, y + 0.5f, groundMid, px0, py0, d0);
        this->worldRenderer.worldToScreenF(x + 1.5f, y + 0.5f, groundMid, ax, ay, dd);
        this->worldRenderer.worldToScreenF(x + 0.5f, y + 1.5f, groundMid, bx, by, dd);
        ax -= px0; ay -= py0; bx -= px0; by -= py0;
        // the world step straight down the screen
        float wdx = bx, wdy = -ax;
        if (wdx * ay + wdy * by < 0) {
          wdx = -wdx;
          wdy = -wdy;
        }
        float len = std::hypot(wdx, wdy);
        float clipBottom = 1e9f;
        if (len > 0.0001f) {
          wdx = wdx / len * 0.25f;
          wdy = wdy / len * 0.25f;
          for (int k = 1; k <= 28; k++) {
            float qx = x + 0.5f + wdx * k, qy = y + 0.5f + wdy * k;
            int tx = static_cast<int>(std::floor(qx)), ty = static_cast<int>(std::floor(qy));
            const MapTile *q = this->worldMap.getTile(tx, ty);
            if (!q)
              continue;
            float fx = qx - tx, fy = qy - ty;
            float top = q->cornerHeight[CORNER_X0Y0] * (1 - fx) + q->cornerHeight[CORNER_X1Y0] * fx;
            float bottom = q->cornerHeight[CORNER_X0Y1] * (1 - fx) + q->cornerHeight[CORNER_X1Y1] * fx;
            float h = top * (1 - fy) + bottom * fy;
            float sx2, sy2, d2;
            this->worldRenderer.worldToScreenF(qx, qy, h, sx2, sy2, d2);
            clipBottom = std::min(clipBottom, sy2);
          }
        }
        if (clipBottom < 1e8f) {
          SDL_Rect c = {-100000, -100000, 200000, static_cast<int>(std::ceil(clipBottom)) + 100000};
          if (hadClip) {
            SDL_Rect both;
            if (!SDL_IntersectRect(&c, &oldClip, &both))
              both = {0, 0, 0, 0};
            c = both;
          }
          SDL_RenderSetClipRect(r, &c);
        }
      }
      bool solid = t && *std::max_element(hh, hh + 4) - *std::min_element(t->cornerHeight, t->cornerHeight + 4) <= 2;
      if (solid) {
        float top[4], bottom[4];
        for (int i = 0; i < 4; i++) {
          top[i] = static_cast<float>(hh[cornerOrder[i]]);
          bottom[i] = static_cast<float>(t->cornerHeight[cornerOrder[i]]);
        }
        this->worldRenderer.drawSolid(r, db, x, y, top, bottom, 14, alpha);
      } else if (under > groundMid + 0.1f) {
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
      SDL_RenderSetClipRect(r, hadClip ? &oldClip : nullptr);
      // The slab's edge under a level deck
      int lowest = *std::min_element(hh, hh + 4), highest = *std::max_element(hh, hh + 4);
      if (!solid && lowest == highest && t && lowest > *std::max_element(t->cornerHeight, t->cornerHeight + 4))
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
  static const SDL_Color bulldozeRed{255, 70, 60, 255};
  for (const auto &kv : this->walkways.all())
    add(kv.first.first, kv.first.second, kv.second.h, kv.second.type,
        this->bulldozer && kv.first == this->bulldozeDeck ? &bulldozeRed : nullptr);
  // The drag's decks, tinted
  if (this->line.active || this->smartPlan) {
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
  // The deck whose top (as drawn, at its heights) is under the point; of
  // two, the one in front
  float best = -1e30f;
  int found = 0;
  for (const auto &kv : this->walkways.all()) {
    const int *h = kv.second.h;
    const float cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
    const int ci[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X1Y1, CORNER_X0Y1};
    float px[4], py[4], d = 0;
    for (int i = 0; i < 4; i++)
      worldRenderer.worldToScreenF(kv.first.first + cx[i], kv.first.second + cy[i],
                                   static_cast<float>(h[ci[i]]), px[i], py[i], d);
    // Its top, or the side just under it (a unit and a half: the stairs'
    // or slab's face, not the open space down its pillars)
    float gx[4], gy[4];
    const MapTile *gt = this->worldMap.getTile(kv.first.first, kv.first.second);
    for (int i = 0; i < 4; i++) {
      float ground = gt ? static_cast<float>(gt->cornerHeight[ci[i]]) : 0.0f;
      worldRenderer.worldToScreenF(kv.first.first + cx[i], kv.first.second + cy[i],
                                   std::max(ground, h[ci[i]] - 1.5f), gx[i], gy[i], d);
    }
    auto inQuad = [&](const float *qx, const float *qy) {
      bool pos = false, neg = false;
      for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        float cross = (qx[j] - qx[i]) * (ly - qy[i]) - (qy[j] - qy[i]) * (lx - qx[i]);
        pos = pos || cross > 0;
        neg = neg || cross < 0;
      }
      return !(pos && neg);
    };
    bool inside = inQuad(px, py);
    for (int i = 0; i < 4 && !inside; i++) {
      int j = (i + 1) % 4;
      const float qx[4] = {px[i], px[j], gx[j], gx[i]}, qy[4] = {py[i], py[j], gy[j], gy[i]};
      inside = inQuad(qx, qy);
    }
    float sx, sy, depth;
    worldRenderer.worldToScreenF(kv.first.first + 0.5f, kv.first.second + 0.5f, 0.0f, sx, sy, depth);
    if (inside && depth > best) {
      best = depth;
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

// Where guests come in: the path tile nearest the zoo's entrance (the
// building whose type ends in "gate", e.g. fgate), inside the zoo
void World::findEntrance() {
  const PlacedObjects::Object *gate = nullptr;
  for (const PlacedObjects::Object &o : this->placedObjects.objects()) {
    const std::string &t = o.typeName;
    if (o.className == "building" && t.size() >= 4 && t.compare(t.size() - 4, 4, "gate") == 0)
      gate = &o;
  }
  if (!gate)
    return;
  int gx = static_cast<int>(std::floor(gate->x)), gy = static_cast<int>(std::floor(gate->y));
  for (int r = 0; r < 12; r++)
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++) {
        if (std::max(std::abs(dx), std::abs(dy)) != r)
          continue;
        int x = gx + dx, y = gy + dy;
        if (this->worldMap.isPath(x, y) && this->fences.insideZoo(x, y) && this->fences.exhibitAt(x, y) < 0) {
          this->guests.setEntrance(x, y);
          SDL_Log("[Guests] entrance at %d,%d", x, y);
          return;
        }
      }
}

void World::startAmbience(const std::string &scenario) {
  Sound::get().stop(this->ambienceChannel);
  this->ambienceChannel = -1;
  std::string config = "worldlp.cfg";
  if (IniReader *scn = this->resourceManager->getIniReader(scenario)) {
    std::string c = scn->get("start", "worldconfig");
    if (!c.empty())
      config = c;
    delete scn;
  }
  if (IniReader *w = this->resourceManager->getIniReader(config)) {
    std::string name = w->get("worldsound", "name");
    if (!name.empty())
      this->ambienceChannel = Sound::get().loop(name, w->getInt("worldsound", "atten", 0));
    delete w;
  }
  this->birdCalls.clear();
  this->birdChance = 0;
  if (IniReader *b = this->resourceManager->getIniReader("worldsnd.cfg")) {
    std::string group = b->get("ambientlevels", "group", "happy");
    this->birdChance = b->getInt(group, "chance", 0);
    std::vector<std::string> sounds = b->getList(group, "sound"), probs = b->getList(group, "prob");
    for (size_t i = 0; i < sounds.size() && i < probs.size(); i++)
      this->birdCalls.push_back({sounds[i], std::atoi(probs[i].c_str())});
    delete b;
  }
}

void World::updateBuildingSounds(float seconds) {
  if (!this->animals.viewOffset)
    return;
  this->buildingSoundClock += seconds;
  bool mayStart = this->buildingSoundClock >= 1.0f;
  const auto &objs = this->placedObjects.objects();
  // (gone objects' loops stopped)
  for (auto it = this->buildingLoops.begin(); it != this->buildingLoops.end();)
    if (it->first >= static_cast<int>(objs.size())) {
      Sound::get().stop(it->second);
      it = this->buildingLoops.erase(it);
    } else {
      ++it;
    }
  for (size_t i = 0; i < objs.size(); i++) {
    const PlacedObjects::Object &o = objs[i];
    if (o.fence)
      continue;
    std::string file = PlacedObjects::fileOf(o);
    auto a = this->ambientOf.find(file);
    if (a == this->ambientOf.end()) {
      std::pair<std::string, int> s{"", 0};
      if (IniReader *ai = this->resourceManager->getIniReader(file)) {
        s.first = Utils::string_to_lower(ai->get("ambientsound", "name"));
        s.second = ai->getInt("ambientsound", "attenuation", 0);
        delete ai;
      }
      a = this->ambientOf.emplace(file, s).first;
    }
    if (a->second.first.empty())
      continue;
    float dx = 0, dy = 0;
    this->animals.viewOffset(o.x, o.y, dx, dy);
    int away = static_cast<int>(std::hypot(dx, dy) * 2.75f);
    bool near = a->second.second + away < 2200;
    auto ch = this->buildingLoops.find(static_cast<int>(i));
    if (ch != this->buildingLoops.end()) {
      if (!near || !Sound::get().place(ch->second, dx, dy, a->second.second)) {
        Sound::get().stop(ch->second);
        this->buildingLoops.erase(ch);
      }
    } else if (near && mayStart) {
      int c = Sound::get().loopAt(a->second.first, dx, dy, a->second.second);
      if (c >= 0)
        this->buildingLoops[static_cast<int>(i)] = c;
      mayStart = false;
      this->buildingSoundClock = 0;
    }
  }
}

void World::exhibitsNear(int x0, int y0, int x1, int y1, std::vector<int> &out) const {
  for (const Fences::Exhibit &e : this->fences.exhibits())
    for (auto [x, y] : e.tiles)
      if (x >= x0 && x < x1 && y >= y0 && y < y1) {
        out.push_back(e.id);
        break;
      }
}

void World::pickUpAnimal(int id) {
  const Animals::Member *m = this->animals.member(id);
  if (!m)
    return;
  int ex = m->exhibit, type = m->type;
  this->animals.snapshot(ex, this->worldMap, this->fences);
  this->animals.carried = id;
  this->animals.markDirty(ex);
  this->animals.react(ex, Animals::Change::Animal, "", type, true, this->worldMap, this->fences);
  std::string file;
  int atten = 0;
  if (this->animals.soundOf(type, "pickupsound", file, atten))
    Sound::get().play(file, atten);
}

void World::sellAnimal(int id) {
  const Animals::Member *m = this->animals.member(id);
  if (!m)
    return;
  int ex = m->exhibit, type = m->type;
  this->animals.snapshot(ex, this->worldMap, this->fences);
  this->animals.remove(id);
  this->animals.react(ex, Animals::Change::Animal, "", type, true, this->worldMap, this->fences);
}

void World::setTerrainTool(bool active, bool painting, int type, int size, int mode) {
  if (active != this->terrain.isActive()) {
    this->terrain.setActive(active);
    this->worldRenderer.setToolGrid(active || this->fenceTool >= 0 || !this->pathTool.empty());
    this->hoverPrice = -1;
    if (active)
      this->bulldozer = false;
  }
  this->terrain.painting = painting;
  this->terrain.terrainType = type;
  this->terrain.size = std::clamp(size, 1, 5);
  this->terrain.mode = static_cast<TerrainTool::Mode>(std::clamp(mode, 0, 3));
}

// The brush on the ground: its outline, green
void World::drawBrush(SDL_Renderer *renderer) {
  if (!this->terrain.isActive() || !this->terrain.hasBrush())
    return;
  int x0, y0, x1, y1;
  this->terrain.brushRect(x0, y0, x1, y1);
  auto vertex = [&](int vx, int vy) {
    // The ground at a vertex: the highest of the tiles meeting there
    int h = -1000;
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++)
        if (const MapTile *t = this->worldMap.getTile(vx - dx, vy - dy)) {
          static const int corner[2][2] = {{CORNER_X0Y0, CORNER_X1Y0}, {CORNER_X0Y1, CORNER_X1Y1}};
          h = std::max(h, t->cornerHeight[corner[dy][dx]]);
        }
    float sx, sy, d;
    this->worldRenderer.worldToScreenF(static_cast<float>(vx), static_cast<float>(vy),
                                       static_cast<float>(h), sx, sy, d);
    return SDL_FPoint{sx, sy};
  };
  std::vector<SDL_FPoint> ring;
  for (int x = x0; x <= x1; x++) ring.push_back(vertex(x, y0));
  for (int y = y0 + 1; y <= y1; y++) ring.push_back(vertex(x1, y));
  for (int x = x1 - 1; x >= x0; x--) ring.push_back(vertex(x, y1));
  for (int y = y1 - 1; y >= y0; y--) ring.push_back(vertex(x0, y));
  SDL_SetRenderDrawColor(renderer, 60, 255, 60, 255);
  SDL_RenderDrawLinesF(renderer, ring.data(), static_cast<int>(ring.size()));
}

void World::setObjectTool(const std::string &file, int cost, int facing) {
  if (file == this->objectTool && facing == this->objectFacing && cost == this->objectCost)
    return;
  this->objectTool = file;
  this->objectCost = cost;
  this->objectFacing = facing;
  this->objectGhost = false;
  this->hoverPrice = -1;
  if (!file.empty())
    this->bulldozer = false;
}

std::pair<int, int> World::footprintOf(const std::string &file) {
  auto it = this->footprints.find(file);
  if (it != this->footprints.end())
    return it->second;
  std::pair<int, int> fp{0, 0};
  if (IniReader *ai = this->resourceManager->getIniReader(file)) {
    fp.first = ai->getInt("characteristics/integers", "cfootprintx", 0);
    fp.second = ai->getInt("characteristics/integers", "cfootprinty", 0);
    delete ai;
  }
  this->footprints[file] = fp;
  return fp;
}

// The world facing (0 N, 2 E, 4 S, 6 W) that shows as the buy panel's icon
// does in this turn of the view (0 SE, 1 SW, 2 NW, 3 NE)
int World::worldFacing(int iconFacing) const {
  static const CompassDirection sides[4] = {CompassDirection::SE, CompassDirection::SW,
                                            CompassDirection::NW, CompassDirection::NE};
  CompassDirection want = sides[((iconFacing % 4) + 4) % 4];
  for (int f = 0; f < 8; f += 2) {
    float angle = f * 3.14159265f / 4.0f;
    if (this->worldRenderer.screenSide(std::sin(angle), -std::cos(angle)) == want)
      return f;
  }
  return 4;
}

// Its footprint (half tiles; none counts as one) lined up on the half-tile
// grid round the point
void World::snapObject(const std::string &file, int facing, float &x, float &y) {
  auto [fx, fy] = this->footprintOf(file);
  fx = std::max(1, fx);
  fy = std::max(1, fy);
  if ((facing & 6) == 2 || (facing & 6) == 6)
    std::swap(fx, fy);
  float left = std::round((x - fx / 4.0f) * 2.0f) / 2.0f;
  float top = std::round((y - fy / 4.0f) * 2.0f) / 2.0f;
  x = left + fx / 4.0f;
  y = top + fy / 4.0f;
}

Fences::Fit World::objectFit(const std::string &file, float x, float y, int facing) {
  auto [fx, fy] = this->footprintOf(file);
  fx = std::max(1, fx);
  fy = std::max(1, fy);
  if ((facing & 6) == 2 || (facing & 6) == 6)
    std::swap(fx, fy);
  float hx = fx / 4.0f, hy = fy / 4.0f;
  int x0 = static_cast<int>(std::floor(x - hx + 0.01f)), x1 = static_cast<int>(std::floor(x + hx - 0.01f));
  int y0 = static_cast<int>(std::floor(y - hy + 0.01f)), y1 = static_cast<int>(std::floor(y + hy - 0.01f));
  for (int ty = y0; ty <= y1; ty++)
    for (int tx = x0; tx <= x1; tx++) {
      const MapTile *t = this->worldMap.getTile(tx, ty);
      if (!t || !this->fences.insideZoo(tx, ty))
        return Fences::Fit::Outside;
      if (t->terrainType == 9 || t->terrainType == 10 || this->worldMap.isPath(tx, ty))
        return Fences::Fit::Outside;
      if (this->fences.exhibitAt(tx, ty) >= 0)
        if (const Fences::Exhibit *e = this->fences.exhibit(this->fences.exhibitAt(tx, ty)))
          if (e->tank)
            return Fences::Fit::Outside;
      // No fence through it
      if (tx < x1 && this->fences.at(Fences::Edge{false, tx + 1, ty}))
        return Fences::Fit::InTheWay;
      if (ty < y1 && this->fences.at(Fences::Edge{true, tx, ty + 1}))
        return Fences::Fit::InTheWay;
    }
  // Nothing else standing where it would
  for (const PlacedObjects::Object &o : this->placedObjects.objects()) {
    if (o.fence)
      continue;
    auto [ox, oy] = this->footprintOf(PlacedObjects::fileOf(o));
    ox = std::max(1, ox);
    oy = std::max(1, oy);
    if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
      std::swap(ox, oy);
    if (std::fabs(o.x - x) < hx + ox / 4.0f - 0.01f && std::fabs(o.y - y) < hy + oy / 4.0f - 0.01f)
      return Fences::Fit::InTheWay;
  }
  return Fences::Fit::Ok;
}

void World::setAnimalTool(int type, bool female) {
  if (type == this->animalTool && female == this->animalFemale)
    return;
  this->animalTool = type;
  this->animalFemale = female;
  this->animals.previewType = -1;
  this->hoverPrice = -1;
  if (type >= 0)
    this->bulldozer = false;
}

void World::centreOnAnimal(int id) {
  if (const Animals::Member *m = this->animals.member(id))
    this->centreOn(m->x, m->y);
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
  this->objectGhost = false;
  this->animals.previewType = -1;
  this->animals.hovered = -1;
  this->fences.previewFilterX = -1;
  this->fences.hoverPlatform = -1;
  this->hoverTipText.clear();
  if (this->dragging)
    return;
  if (this->fenceTool >= 0)
    this->fences.preview.clear();
  this->hoverPrice = -1;
  this->hoverTileX = -1;
}

bool World::cancelLine() {
  if (!this->line.active)
    return false;
  this->line = {};
  this->pathDragging = false;
  this->deckPreview.clear();
  this->pathPreview.clear();
  this->hoverPrice = -1;
  return true;
}

void World::cancelTool() {
  if (this->cancelLine())
    return;
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
    this->hoverPrice = this->fencePreviewPrice();
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

int World::wallDragCost() const {
  if (this->wallDrag.tank < 0)
    return 0;
  return std::abs(this->wallDrag.steps) *
         static_cast<int>(std::lround(this->fences.wallStepCost(this->wallDrag.tank)));
}

void World::mouseMove(int x, int y) {
  // A tank wall dragged: the walls follow the cursor up and down, a step
  // a height unit, the price beside it (as the original)
  if (this->wallDrag.tank >= 0) {
    const Camera &cam = worldRenderer.getCamera();
    float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
    Camera &c = worldRenderer.getCamera();
    float saved = c.zoom;
    c.zoom = zoom;
    float x0, y0, x1, y1, d;
    worldRenderer.worldToScreenF(0, 0, 0, x0, y0, d);
    worldRenderer.worldToScreenF(0, 0, 1, x1, y1, d);
    c.zoom = saved;
    float unit = std::max(1.0f, (y0 - y1) * zoom * 0.5f); // (a step: a subtile)
    int want = static_cast<int>((this->wallDrag.pressY - y) / unit);
    const int id = this->wallDrag.tank;
    while (this->wallDrag.steps < want && this->fences.canAdjustWall(id, 1)) {
      this->fences.adjustWall(id, 1);
      this->wallDrag.steps++;
    }
    while (this->wallDrag.steps > want && this->fences.canAdjustWall(id, -1)) {
      this->fences.adjustWall(id, -1);
      this->wallDrag.steps--;
    }
    this->hoverPrice = this->wallDragCost();
    return;
  }
  if (this->bulldozer) {
    // What it would take away, drawn red, with what would come back for it
    // ("$56") and its name (as the original). In the order a click takes
    // them: a filter, a walkway deck, a fence, a path tile.
    this->highlight = {false, -1, -1};
    this->bulldozeDeck = this->bulldozePath = {-1, -1};
    this->fences.highlightFilter = -1;
    this->placedObjects.highlight = -1;
    this->hoverPrice = -1;
    this->bulldozeTip.clear();
    auto item = [](const std::string &file) -> const CatalogItem * {
      for (const CatalogItem &it : ItemCatalog::get().all())
        if (it.file == file)
          return &it;
      return nullptr;
    };
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
        this->fences.highlightFilter = f;
        this->hoverPrice = static_cast<int>(std::lround(this->fences.filterType().cost * 0.8));
        if (const CatalogItem *it = item(this->fences.filterType().file))
          this->bulldozeTip = it->name;
        return;
      }
    }
    // Scenery (rocks, trees, ...): red, its name, 80% back (as the
    // original: a Small Rock $60, a Large Rock $120)
    {
      const Camera &cam = worldRenderer.getCamera();
      float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
      float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
      Camera &c = worldRenderer.getCamera();
      float saved = c.zoom;
      c.zoom = zoom;
      int o = this->placedObjects.pick(lx, ly, worldRenderer, worldMap);
      c.zoom = saved;
      if (o >= 0) {
        const PlacedObjects::Object &obj = this->placedObjects.objects()[o];
        const CatalogItem *it = item(PlacedObjects::fileOf(obj));
        this->placedObjects.highlight = o;
        this->hoverPrice = it ? static_cast<int>(std::lround(it->cost * 0.8)) : 0;
        this->bulldozeTip = it ? it->name : obj.name;
        return;
      }
    }
    int dtx, dty;
    if (this->deckAt(x, y, dtx, dty)) {
      this->bulldozeDeck = {dtx, dty};
      const CatalogItem *it = item("paths/" + this->walkways.at(dtx, dty)->type + ".ai");
      this->hoverPrice = it ? static_cast<int>(std::lround(it->cost * 2 * 0.8)) : 0;
      if (it)
        this->bulldozeTip = it->name;
      return;
    }
    Fences::Edge e;
    if (this->pickFence(x, y, e)) {
      this->highlight = e;
      if (const Fences::Piece *p = this->fences.at(e)) {
        this->hoverPrice = this->fences.refundOf(e);
        this->bulldozeTip = this->fences.types()[p->type].name;
      }
      return;
    }
    float wx, wy;
    if (this->pick(x, y, wx, wy)) {
      int tx = static_cast<int>(std::floor(wx)), ty = static_cast<int>(std::floor(wy));
      if (this->worldMap.isPath(tx, ty)) {
        this->bulldozePath = {tx, ty};
        const CatalogItem *it =
            item("paths/" + this->worldMap.getPathTypes()[this->worldMap.getPathType(tx, ty)] + ".ai");
        this->hoverPrice = it ? static_cast<int>(std::lround(it->cost * 0.8)) : 0;
        if (it)
          this->bulldozeTip = it->name;
      }
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
    if (this->line.active) {
      // (a drag only once the mouse has really moved: pressed on a deck's
      // side, the tile nearest is the ground in front)
      bool away = std::abs(x - this->line.pressX) + std::abs(y - this->line.pressY) > 8;
      if (this->line.pressed && !away && !this->line.moved && !this->line.clicked) {
        this->line.ex = this->line.sx;
        this->line.ey = this->line.sy;
      } else {
        this->aimLine(x, y);
      }
      if (this->line.pressed && away && (this->line.ex != this->line.sx || this->line.ey != this->line.sy))
        this->line.moved = true;
      this->layDeckPreview();
    } else if (this->pathDragging) {
      if (Features::pathModes) {
        this->layPathDrag(tx, ty);
        this->pathLast = {tx, ty};
      } else {
        this->addPathTile(tx, ty);
      }
      this->planSmartPath();
    } else {
      int dtx, dty;
      this->hoverDeck = {-1, -1};
      if (Features::elevatedPaths && raisable(this->pathTool) && this->deckAt(x, y, dtx, dty)) {
        this->hoverDeck = {dtx, dty};
        this->pathPreview.clear();
      } else {
        this->pathPreview = {{{tx, ty}, pathFit(tx, ty)}};
      }
    }
    int n = 0;
    if (this->line.active || this->smartPlan) {
      // Decks cost twice a path tile (their posts); what's there already
      // nothing
      for (auto &p : this->deckPreview)
        n += p.ok ? (p.ground ? (this->worldMap.isPath(p.x, p.y) ? 0 : 1)
                              : (this->walkways.at(p.x, p.y) ? 0 : 2))
                  : 0;
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
  // The gate tool: the wall under the cursor lit, green where it can be
  // the gate (its price, $0 unless worn)
  if (this->gateTool) {
    Fences::Edge e;
    this->highlight = {false, -1, -1};
    this->hoverPrice = -1;
    if (this->pickFence(x, y, e) && this->fences.canGate(e)) {
      this->highlight = e;
      this->hoverPrice = this->fences.gateCostOf(e);
    }
    return;
  }
  // Terraforming: the brush follows the cursor; held, it paints or
  // raises and lowers the ground
  if (this->terrain.isActive()) {
    float wx, wy;
    if (!this->pick(x, y, wx, wy)) {
      this->hoverPrice = -1;
      return;
    }
    int tx = static_cast<int>(std::floor(wx)), ty = static_cast<int>(std::floor(wy));
    float px = (x - this->viewRect.x) / this->viewScale, py = (y - this->viewRect.y) / this->viewScale;
    if (this->terrain.isHeld()) {
      const Camera &cam = worldRenderer.getCamera();
      std::vector<int> near;
      int bx0, by0, bx1, by1;
      this->terrain.brushRect(bx0, by0, bx1, by1);
      int reach = this->terrain.painting ? 1 : 25;
      this->exhibitsNear(std::min(bx0, tx) - reach, std::min(by0, ty) - reach, std::max(bx1, tx + 1) + reach,
                         std::max(by1, ty + 1) + reach, near);
      for (int e : near)
        this->animals.snapshot(e, this->worldMap, this->fences);
      uint32_t before = this->worldMap.getGeneration();
      this->terrain.drag(tx, ty, px, py, 16.0f * (cam.zoom > 0 ? cam.zoom : 1.0f));
      if (this->worldMap.getGeneration() != before) {
        this->animals.markDirty();
        for (int e : near)
          this->animals.react(e, Animals::Change::Ground, "", -1, false, this->worldMap, this->fences);
      }
    } else {
      this->terrain.hover(tx, ty);
    }
    this->hoverPrice = this->terrain.painting ? static_cast<int>(this->terrain.strokeCost()) : -1;
    return;
  }
  // Placing an object: its likeness at the cursor, green "-$120" where it
  // can go, red "$0" where not
  if (!this->objectTool.empty()) {
    float wx, wy;
    if (!this->pick(x, y, wx, wy)) {
      this->objectGhost = false;
      this->hoverPrice = -1;
      return;
    }
    this->ghostFacing = this->worldFacing(this->objectFacing);
    this->snapObject(this->objectTool, this->ghostFacing, wx, wy);
    this->ghostX = wx;
    this->ghostY = wy;
    this->objectGhost = true;
    this->ghostFit = this->objectFit(this->objectTool, wx, wy, this->ghostFacing);
    this->hoverPrice = this->ghostFit == Fences::Fit::Ok ? this->objectCost : 0;
    return;
  }
  // An animal picked up follows the cursor
  if (this->animals.carried >= 0) {
    float wx, wy;
    if (this->pick(x, y, wx, wy)) {
      this->animals.moveTo(this->animals.carried, wx, wy);
      this->animals.hold(this->animals.carried, this->worldMap, this->fences);
    }
    return;
  }
  // Adopting: its likeness at the cursor, "-$800" over an exhibit, "$0"
  // anywhere else
  if (this->animalTool >= 0) {
    float wx, wy;
    if (!this->pick(x, y, wx, wy)) {
      this->animals.previewType = -1;
      this->hoverPrice = -1;
      return;
    }
    this->animals.previewType = this->animalTool;
    this->animals.previewFemale = this->animalFemale;
    this->animals.previewX = wx;
    this->animals.previewY = wy;
    this->animals.previewFit = this->animals.canPlace(this->animalTool, wx, wy, this->worldMap, this->fences);
    this->hoverPrice = this->animals.previewFit == Fences::Fit::Ok
                           ? this->animals.types()[this->animalTool].cost
                           : 0;
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
    this->animals.hovered = this->staff.hovered >= 0 || !this->pathTool.empty() || this->staffTool >= 0
                                ? -1
                                : this->animals.pick(lx, ly, worldRenderer, worldMap);
    this->guests.hovered = this->staff.hovered >= 0 || this->animals.hovered >= 0 || !this->pathTool.empty() ||
                                   this->staffTool >= 0
                               ? -1
                               : this->guests.pick(lx, ly, worldRenderer, worldMap);
    // A tank's diver platform: lit, its tank named; a tank wall: how to
    // raise and lower it (as the original)
    this->fences.hoverPlatform =
        this->staff.hovered >= 0 || !this->pathTool.empty() || this->staffTool >= 0
            ? -1
            : this->fences.platformAt(lx, ly, worldRenderer);
    this->hoverTipText.clear();
    if (this->fences.hoverPlatform >= 0) {
      if (const Fences::Exhibit *ex = this->fences.exhibit(this->fences.hoverPlatform))
        this->hoverTipText = ex->name;
    } else if (this->animals.hovered >= 0) {
      if (const Animals::Member *m = this->animals.member(this->animals.hovered))
        this->hoverTipText = m->name;
    } else if (this->guests.hovered >= 0) {
      if (const Guests::Guest *g = this->guests.guest(this->guests.hovered))
        this->hoverTipText = g->name;
    }
    this->fences.hoverGate = {false, -1, -1};
    if (this->fences.hoverPlatform < 0 && this->animals.hovered < 0 && this->guests.hovered < 0 &&
        this->staff.hovered < 0 && this->pathTool.empty() && this->staffTool < 0 && this->objectTool.empty() &&
        this->animalTool < 0) {
      Fences::Edge e;
      if (this->fences.pieceAt(lx, ly, worldRenderer, worldMap, e))
        if (const Fences::Piece *p = this->fences.at(e)) {
          if (p->tank >= 0 && !p->gate)
            this->hoverTipText = this->resourceManager->getString(31079);
          // (an exhibit's gate: lit)
          if (p->gate && p->tank < 0)
            this->fences.hoverGate = e;
        }
    }
    c.zoom = saved;
  } else {
    this->staff.hovered = -1;
    this->animals.hovered = -1;
    this->fences.hoverPlatform = -1;
    this->fences.hoverGate = {false, -1, -1};
    this->hoverTipText.clear();
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
      this->hoverPrice = this->fencePreviewPrice();
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
  this->hoverPrice = this->fencePreviewPrice();
}

int World::fencePreviewPrice() const {
  int sum = 0;
  for (const auto &p : this->fences.preview)
    if (p.second)
      sum += this->fences.layCost(p.first, this->fenceTool);
  return sum;
}

World::ToolResult World::mouseDown(int x, int y) {
  ToolResult r;
  float wx = 0, wy = 0;
  bool onMap = this->pick(x, y, wx, wy);
  this->undoFresh = true;
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
    // Scenery under it: taken away, 80% of its price back
    {
      const Camera &cam = worldRenderer.getCamera();
      float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
      float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
      Camera &c = worldRenderer.getCamera();
      float saved = c.zoom;
      c.zoom = zoom;
      int o = this->placedObjects.pick(lx, ly, worldRenderer, worldMap);
      c.zoom = saved;
      if (o >= 0) {
        std::string file = PlacedObjects::fileOf(this->placedObjects.objects()[o]);
        for (const CatalogItem &item : ItemCatalog::get().all())
          if (item.file == file)
            r.refund = static_cast<int>(std::lround(item.cost * 0.8));
        const PlacedObjects::Object &gone = this->placedObjects.objects()[o];
        {
          UndoStep u{UndoStep::Kind::RemovedObject};
          u.object = gone;
          u.money = r.refund;
          this->recordUndo(u);
        }
        int ex = this->fences.exhibitAt(static_cast<int>(std::floor(gone.x)), static_cast<int>(std::floor(gone.y)));
        this->animals.snapshot(ex, this->worldMap, this->fences);
        this->placedObjects.remove(o);
        this->animals.markDirty();
        this->animals.react(ex, Animals::Change::Object, file, -1, true, this->worldMap, this->fences);
        this->reindex();
        this->bulldozeTip.clear();
        this->hoverPrice = -1;
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
          UndoStep u{UndoStep::Kind::RemovedPath};
          u.x = tx;
          u.y = ty;
          u.pathType = this->pathTypeAt(tx, ty);
          this->worldMap.setPath(tx, ty, "");
          r.refund = static_cast<int>(std::lround(price * 0.8));
          u.money = r.refund;
          this->recordUndo(u);
        }
      }
      return r;
    }
    if (this->fences.tankOf(e) >= 0) {
      this->pendingDrain = e;
      r.askDrainTank = true;
      return r;
    }
    // An exhibit's wall between it and the open zoo: asked first (string
    // 157, as zoo.exe 0x50a6d5: whether or not animals are in it)
    {
      int ex = e.alongX ? this->fences.exhibitAt(e.x, e.y - 1) : this->fences.exhibitAt(e.x - 1, e.y);
      int other = this->fences.exhibitAt(e.x, e.y);
      const Fences::Piece *pc = this->fences.at(e);
      bool habitat = pc && !this->fences.types()[pc->type].zooWall;
      if (habitat && ex != other && (ex < 0 || other < 0)) {
        this->pendingFence = e;
        r.askEscape = true;
        return r;
      }
      // Between two exhibits: "Are you sure you want to merge %s with %s?"
      // (string 156); the one this side keeps its name
      if (habitat && ex >= 0 && other >= 0 && ex != other) {
        this->pendingFence = e;
        std::string s = this->resourceManager->getString(156);
        for (const std::string &n : {this->fences.exhibit(ex)->name, this->fences.exhibit(other)->name}) {
          size_t at = s.find("%s");
          if (at != std::string::npos)
            s.replace(at, 2, n);
        }
        r.askMerge = s;
        return r;
      }
    }
    {
      const Fences::Piece *pc = this->fences.at(e);
      UndoStep u{UndoStep::Kind::RemovedFence};
      u.edge = e;
      u.fenceType = pc && !pc->gate && pc->tank < 0 ? pc->type : -1;
      r.refund = this->removeFence(e);
      u.money = r.refund;
      if (u.fenceType >= 0)
        this->recordUndo(u);
    }
    this->hoverPrice = -1;
    return r;
  }
  // Putting a picked-up animal down: in an exhibit (else still held, and
  // the original's message)
  if (this->animals.carried >= 0) {
    int id = this->animals.carried;
    if (const Animals::Member *m = this->animals.member(id)) {
      float mx = m->x, my = m->y;
      if (this->animals.canDrop(id, mx, my, this->worldMap, this->fences)) {
        int ex = this->fences.exhibitAt(static_cast<int>(std::floor(mx)), static_cast<int>(std::floor(my)));
        int type = m->type;
        this->animals.snapshot(ex, this->worldMap, this->fences);
        this->animals.drop(id, mx, my, this->fences);
        this->animals.react(ex, Animals::Change::Animal, "", type, false, this->worldMap, this->fences);
        std::string file;
        int atten = 0;
        if (this->animals.soundOf(type, "placesound", file, atten))
          Sound::get().play(file, atten);
      } else {
        r.messageId = 10108;
        Sound::get().play("sounds/noplace");
      }
    } else {
      this->animals.carried = -1;
    }
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
    // A walkway under way: this press ends it (built on the release)
    if (this->line.active) {
      this->line.pressed = true;
      this->line.pressX = x;
      this->line.pressY = y;
      return r;
    }
    if (!onMap)
      return r;
    int ptx = static_cast<int>(std::floor(wx)), pty = static_cast<int>(std::floor(wy));
    // Raised, or pressing on a deck: a walkway from there (at height 0 off
    // a deck it comes down)
    const bool canRaise = Features::elevatedPaths && raisable(this->pathTool);
    int dtx, dty;
    bool fromDeck = canRaise && this->deckAt(x, y, dtx, dty);
    if (canRaise && this->buildHeight > 0) {
      this->startLine(fromDeck ? dtx : ptx, fromDeck ? dty : pty, fromDeck);
      this->line.pressed = true;
      this->line.pressX = x;
      this->line.pressY = y;
      return r;
    }
    // On the ground (or off a walkway): the original's path drag, stairs
    // added where it meets a cliff or a walkway
    this->pathDragging = true;
    this->pathPreview.clear();
    this->pathLast = {-1, -1};
    this->pathPress = {fromDeck ? dtx : ptx, fromDeck ? dty : pty};
    this->pathFirstAxis = -1;
    this->addPathTile(fromDeck ? dtx : ptx, fromDeck ? dty : pty);
    this->planSmartPath();
    this->hoverPrice = this->pathPreview.front().second == Fences::Fit::Ok ? this->pathCost : 0;
    return r;
  }
  if (this->gateTool) {
    Fences::Edge e;
    if (this->pickFence(x, y, e) && this->fences.canGate(e)) {
      r.cost = this->fences.gateCostOf(e);
      this->fences.moveGate(e);
      Sound::get().play("sounds/place");
    } else {
      Sound::get().play("sounds/noplace");
    }
    this->mouseMove(x, y);
    return r;
  }
  if (this->terrain.isActive()) {
    if (!onMap)
      return r;
    float px = (x - this->viewRect.x) / this->viewScale, py = (y - this->viewRect.y) / this->viewScale;
    int tx = static_cast<int>(std::floor(wx)), ty = static_cast<int>(std::floor(wy));
    std::vector<int> near;
    int reach = this->terrain.painting ? 3 : 27;
    this->exhibitsNear(tx - reach, ty - reach, tx + reach, ty + reach, near);
    for (int e : near)
      this->animals.snapshot(e, this->worldMap, this->fences);
    uint32_t before = this->worldMap.getGeneration();
    this->terrain.press(tx, ty, px, py);
    if (this->worldMap.getGeneration() != before) {
      this->animals.markDirty();
      for (int e : near)
        this->animals.react(e, Animals::Change::Ground, "", -1, false, this->worldMap, this->fences);
    }
    return r;
  }
  if (!this->objectTool.empty()) {
    if (!onMap)
      return r;
    this->mouseMove(x, y);
    int ex = this->fences.exhibitAt(static_cast<int>(std::floor(this->ghostX)),
                                    static_cast<int>(std::floor(this->ghostY)));
    if (this->objectGhost && this->ghostFit == Fences::Fit::Ok)
      this->animals.snapshot(ex, this->worldMap, this->fences);
    if (this->objectGhost && this->ghostFit == Fences::Fit::Ok &&
        this->placedObjects.add(this->objectTool, this->ghostX, this->ghostY, this->ghostFacing)) {
      r.cost = this->objectCost;
      if (PlacedObjects::Object *o = this->placedObjects.last()) {
        o->openedMonth = this->fences.month();
        UndoStep u{UndoStep::Kind::Object};
        u.id = o->id;
        u.money = r.cost;
        this->recordUndo(u);
      }
      this->reindex();
      this->animals.markDirty();
      Sound::get().play("sounds/place");
      this->animals.react(ex, Animals::Change::Object, this->objectTool, -1, false, this->worldMap, this->fences);
    } else if (this->objectGhost) {
      Sound::get().play("sounds/noplace");
    }
    if (this->objectGhost && this->ghostFit == Fences::Fit::Outside &&
               !this->fences.insideZoo(static_cast<int>(std::floor(this->ghostX)),
                                       static_cast<int>(std::floor(this->ghostY)))) {
      r.outsideZoo = true;
    }
    this->mouseMove(x, y);
    return r;
  }
  if (this->animalTool >= 0) {
    if (!onMap)
      return r;
    int ex = this->fences.exhibitAt(static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wy)));
    bool fits = this->animals.canPlace(this->animalTool, wx, wy, this->worldMap, this->fences) == Fences::Fit::Ok;
    if (fits)
      this->animals.snapshot(ex, this->worldMap, this->fences);
    int id = this->animals.adopt(this->animalTool, this->animalFemale, wx, wy, this->worldMap, this->fences);
    if (id >= 0) {
      r.animalCost = this->animals.types()[this->animalTool].cost;
      {
        UndoStep u{UndoStep::Kind::Animal};
        u.id = id;
        u.money = r.animalCost;
        this->recordUndo(u);
      }
      // Its own placesound, else place.wav
      std::string file;
      int atten = 0;
      if (this->animals.soundOf(this->animalTool, "placesound", file, atten))
        Sound::get().play(file, atten);
      else
        Sound::get().play("sounds/place");
      this->animals.react(ex, Animals::Change::Animal, "", this->animalTool, false, this->worldMap, this->fences);
    } else {
      Sound::get().play("sounds/noplace");
      if (this->animals.canPlace(this->animalTool, wx, wy, this->worldMap, this->fences) ==
          Fences::Fit::Outside)
        r.messageId = 10108; // "Animals can only be placed in exhibits, ..."
    }
    this->mouseMove(x, y);
    return r;
  }
  if (this->staffTool >= 0) {
    if (!onMap)
      return r;
    int id = this->staff.hire(this->staffTool, wx, wy, this->worldMap, this->fences);
    Sound::get().play(id >= 0 ? "sounds/place" : "sounds/noplace");
    if (id >= 0) {
      r.wage = this->staff.types()[this->staffTool].salary;
      r.staff = id;
      UndoStep u{UndoStep::Kind::Staff};
      u.id = id;
      u.money = r.wage;
      this->recordUndo(u);
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
      if (r.staff < 0)
        r.animal = this->animals.pick(lx, ly, worldRenderer, worldMap);
      if (r.staff < 0 && r.animal < 0)
        r.guest = this->guests.pick(lx, ly, worldRenderer, worldMap);
      // A building bought: its Building Information
      if (r.staff < 0 && r.animal < 0 && r.guest < 0) {
        int o = this->placedObjects.pick(lx, ly, worldRenderer, worldMap);
        if (o >= 0 && !this->placedObjects.objects()[o].label.empty())
          r.building = this->placedObjects.objects()[o].id;
      }
      c.zoom = saved;
      if (r.staff >= 0 || r.animal >= 0 || r.guest >= 0 || r.building >= 0)
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
    // A tank's diver platform opens the tank (over its wall, so first)
    {
      const Camera &cam = worldRenderer.getCamera();
      float zoom = (cam.zoom > 0 ? cam.zoom : 1.0f) * this->viewScale;
      float lx = (x - this->viewRect.x) / zoom, ly = (y - this->viewRect.y) / zoom;
      Camera &c = worldRenderer.getCamera();
      float saved = c.zoom;
      c.zoom = zoom;
      int tank = this->fences.platformAt(lx, ly, worldRenderer);
      c.zoom = saved;
      if (tank >= 0) {
        r.exhibit = tank;
        return r;
      }
    }
    // An exhibit's gate opens its information (as the original: not its
    // other fences, not its ground); a tank's wall is dragged up or down
    Fences::Edge e;
    if (this->pickFence(x, y, e))
      if (const Fences::Piece *p = this->fences.at(e)) {
        if (p->gate) {
          r.exhibit = this->fences.exhibitOf(e);
        } else if (p->tank >= 0) {
          this->wallDrag.tank = p->tank;
          this->wallDrag.pressY = y;
          this->wallDrag.steps = 0;
          this->hoverPrice = 0;
        }
      }
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
  this->hoverPrice = this->fencePreviewPrice();
  return r;
}

World::ToolResult World::mouseUp(int x, int y) {
  ToolResult r;
  if (this->terrain.isHeld()) {
    this->terrain.release();
    return r;
  }
  if (this->wallDrag.tank >= 0) {
    this->mouseMove(x, y);
    r.cost = this->wallDragCost();
    this->wallDrag = {};
    this->hoverPrice = -1;
    return r;
  }
  if (this->line.active) {
    if (!this->line.pressed)
      return r;
    this->mouseMove(x, y);
    this->line.pressed = false;
    // A click on its start: the end follows the cursor until the next one
    if (!this->line.clicked && !this->line.moved) {
      this->line.clicked = true;
      return r;
    }
    // Ended where it started: nothing to build
    if (this->line.ex == this->line.sx && this->line.ey == this->line.sy) {
      this->cancelLine();
      return r;
    }
    r.cost = this->buildLine();
    // Clicked: carry on from its end; dragged: done
    if (this->line.clicked) {
      int ex = this->line.ex, ey = this->line.ey;
      this->startLine(ex, ey, this->walkways.at(ex, ey) != nullptr);
      this->line.clicked = true;
    } else {
      this->cancelLine();
    }
    return r;
  }
  if (this->pathDragging && this->smartPlan) {
    this->mouseMove(x, y);
    this->pathDragging = false;
    if (this->smartPlan) {
      r.cost = this->buildLine();
      this->smartPlan = false;
      this->pathLast = {-1, -1};
      return r;
    }
  }
  if (this->pathDragging) {
    this->mouseMove(x, y);
    this->pathDragging = false;
    for (auto &p : this->pathPreview)
      if (p.second == Fences::Fit::Ok && canLayPath(p.first.first, p.first.second)) {
        UndoStep u{UndoStep::Kind::Path};
        u.x = p.first.first;
        u.y = p.first.second;
        u.pathType = this->pathTypeAt(u.x, u.y);
        u.money = this->pathCost;
        this->worldMap.setPath(p.first.first, p.first.second, this->pathTool);
        r.cost += this->pathCost;
        this->recordUndo(u);
      }
    this->pathPreview.clear();
    this->pathLast = {-1, -1};
    return r;
  }
  if (!this->dragging || this->fenceTool < 0)
    return r;
  this->mouseMove(x, y);
  this->dragging = false;
  for (auto &p : this->fences.preview) {
    if (p.second && this->fences.canPlace(p.first, this->worldMap)) {
      const int cost = this->fences.layCost(p.first, this->fenceTool);
      // (over another kind of fence: it's replaced, and undo puts it back)
      const Fences::Piece *was = this->fences.at(p.first);
      int oldType = was ? was->type : -1;
      float oldLife = was ? was->life : -1;
      this->fences.place(p.first, this->fenceTool, this->dragCount, this->worldMap);
      r.cost += cost;
      UndoStep u{UndoStep::Kind::Fence};
      u.edge = p.first;
      u.money = cost;
      u.fenceType = oldType;
      u.fenceLife = oldLife;
      this->recordUndo(u);
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

std::string World::pathTypeAt(int x, int y) const {
  if (!this->worldMap.isPath(x, y))
    return "";
  const auto &types = this->worldMap.getPathTypes();
  int t = this->worldMap.getPathType(x, y);
  return t >= 0 && t < static_cast<int>(types.size()) ? types[t] : std::string();
}

World::UndoMoney World::undo() {
  UndoMoney m;
  bool fences = false;
  for (auto it = this->undoSteps.rbegin(); it != this->undoSteps.rend(); ++it) {
    const UndoStep &u = *it;
    switch (u.kind) {
    case UndoStep::Kind::Object: {
      int i = this->placedObjects.indexOf(u.id);
      if (i >= 0) {
        this->placedObjects.remove(i);
        m.construction += u.money;
      }
      break;
    }
    case UndoStep::Kind::Animal:
      if (this->animals.member(u.id)) {
        this->animals.remove(u.id);
        m.animals += u.money;
      }
      break;
    case UndoStep::Kind::Staff:
      if (this->staff.member(u.id)) {
        this->staff.fire(u.id);
        m.wages += u.money;
      }
      break;
    case UndoStep::Kind::Path:
      this->worldMap.setPath(u.x, u.y, u.pathType);
      m.construction += u.money;
      break;
    case UndoStep::Kind::Fence:
      if (this->fences.at(u.edge)) {
        if (u.fenceType >= 0) {
          // (it replaced another: that one back as it was)
          this->fences.setPieceType(u.edge, u.fenceType);
          if (u.fenceLife >= 0)
            this->fences.setLife(u.edge, u.fenceLife);
        } else {
          this->fences.remove(u.edge, this->worldMap);
        }
        m.construction += u.money;
        fences = true;
      }
      break;
    case UndoStep::Kind::RemovedObject:
      this->placedObjects.restore(u.object);
      m.recycling -= u.money;
      break;
    case UndoStep::Kind::RemovedPath:
      this->worldMap.setPath(u.x, u.y, u.pathType);
      m.recycling -= u.money;
      break;
    case UndoStep::Kind::RemovedFence:
      if (!this->fences.at(u.edge) && this->fences.canPlace(u.edge, this->worldMap)) {
        this->fences.place(u.edge, u.fenceType, 0, this->worldMap);
        m.recycling -= u.money;
        fences = true;
      }
      break;
    }
  }
  this->undoSteps.clear();
  if (fences) {
    this->fences.updateExhibits(this->worldMap, date.day, date.month, date.year);
    this->fences.dropOrphanGates();
    this->animals.rehome(this->worldMap, this->fences);
  }
  this->animals.markDirty();
  this->reindex();
  return m;
}

int World::removeFence(const Fences::Edge &e) {
  int refund = this->fences.refundOf(e);
  this->fences.remove(e, this->worldMap);
  this->highlight = {false, -1, -1};
  this->fences.updateExhibits(this->worldMap, date.day, date.month, date.year);
  this->fences.dropOrphanGates();
  this->animals.markDirty();
  this->animals.rehome(this->worldMap, this->fences);
  return refund;
}

int World::confirmFenceRemoval() {
  if (this->pendingFence.x < 0)
    return 0;
  Fences::Edge e = this->pendingFence;
  this->pendingFence = {false, -1, -1};
  return this->removeFence(e);
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
