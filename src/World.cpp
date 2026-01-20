#include "World.hpp"
#include "CompassDirection.hpp"
#include "MemoryManager.hpp"
#include "MemoryTracker.hpp"

World::World(ResourceManager *resourceManager) {
  ZT_MEMORY_CONTEXT(MemoryOwner::World);

  SDL_Log("World::World() - Starting initialization");

  this->resourceManager = resourceManager;
  this->camX = 0;
  this->camY = 0;

  // Initialize SpriteDatabase with our resource manager
  SDL_Log("World::World() - Initializing SpriteDatabase");
  SpriteDatabase::get().init(resourceManager);
  SDL_Log("World::World() - Loading sprite definitions");
  SpriteDatabase::get().loadDefinitions();

  SDL_Log("World::World() - Initialization complete");
}

World::~World() {
  ZT_MEMORY_CONTEXT(MemoryOwner::World);
  SDL_Log("World: Cleaning up");
  // EntityManager will clean up entities automatically
  // SpriteDatabase is a singleton and manages its own lifecycle
}

void World::loadScenario(const std::string &path) {
  SDL_Log("Loading scenario: %s", path.c_str());

  // Extract the actual .zoo file path from the scenario path
  // Scenarios are typically: scenario/scnXX/scnXX.zoo
  std::string zooPath = path;

  // If path ends with .scn, convert to .zoo
  if (zooPath.size() > 4 && zooPath.substr(zooPath.size() - 4) == ".scn") {
    zooPath = zooPath.substr(0, zooPath.size() - 4) + ".zoo";
  }

  int size = 0;
  void *raw_data = this->resourceManager->getFileContent(zooPath, &size);

  if (raw_data) {
    AssetBuffer buffer =
        MemoryManager::get().wrap(raw_data, static_cast<size_t>(size));

    if (this->zooReader.load(buffer)) {
      SDL_Log("World::loadScenario: Loaded map dimensions %dx%d",
              this->zooReader.getMapWidth(), this->zooReader.getMapHeight());

      // Load entities from the map data
      entityManager.loadFromZooReader(zooReader);

      // If no entities were found in the map, add some test animals
      if (entityManager.getAnimalCount() == 0) {
        SDL_Log("World: No animals found in map data, adding test animals");
        int centerX = zooReader.getMapWidth() / 2;
        int centerY = zooReader.getMapHeight() / 2;

        entityManager.addAnimal(AnimalSpecies::Lion, centerX, centerY);
        entityManager.addAnimal(AnimalSpecies::Zebra, centerX + 5, centerY + 3);
        entityManager.addAnimal(AnimalSpecies::Elephant_African, centerX - 3,
                                centerY + 5);
      }
    }
  } else {
    SDL_Log("World::loadScenario: Failed to read %s", zooPath.c_str());
  }

  // Reset camera to center of map
  this->camX = 0;
  this->camY = 0;
}

void World::loadFreeform(const std::string &path) {
  SDL_Log("Loading freeform map: %s", path.c_str());

  int size = 0;
  void *raw_data = this->resourceManager->getFileContent(path, &size);

  if (raw_data) {
    // Check if it's an INI file (starts with '[')
    const char *data_char = (const char *)raw_data;
    if (size > 0 && data_char[0] == '[') {
      // It's an INI file, look for savegame=
      std::string content(data_char, size);
      std::string key = "savegame=";
      size_t pos = content.find(key);
      if (pos != std::string::npos) {
        size_t start = pos + key.length();
        size_t end = content.find_first_of("\r\n", start);
        std::string mapPath = content.substr(start, end - start);

        SDL_Log("World::loadFreeform: Redirecting to map %s", mapPath.c_str());

        // Load the actual map file
        this->loadFreeform(mapPath);
        return;
      } else {
        SDL_Log("World::loadFreeform: Could not find savegame key in %s",
                path.c_str());
      }
    } else {
      // Assume it's a binary map file
      AssetBuffer buffer =
          MemoryManager::get().wrap(raw_data, static_cast<size_t>(size));

      if (this->zooReader.load(buffer)) {
        SDL_Log("World::loadFreeform: Loaded map dimensions %dx%d",
                this->zooReader.getMapWidth(), this->zooReader.getMapHeight());
        SDL_Log("World::loadFreeform: baseTerrainId=%u, mapType=%u",
                this->zooReader.getBaseTerrainId(),
                this->zooReader.getMapType());

        // Load entities from the map data
        entityManager.loadFromZooReader(zooReader);

        // For freeform maps, add some starter animals if none exist AND their
        // sprites are loaded
        if (entityManager.getAnimalCount() == 0) {
          int centerX = zooReader.getMapWidth() / 2;
          int centerY = zooReader.getMapHeight() / 2;

          // Only add if sprite is loaded to prevent crash
          if (SpriteDatabase::get().getAnimalSprite(AnimalSpecies::Lion)) {
            SDL_Log("World: Freeform map - adding starter Lion");
            entityManager.addAnimal(AnimalSpecies::Lion, centerX, centerY);
          }

          if (SpriteDatabase::get().getAnimalSprite(AnimalSpecies::Giraffe)) {
            SDL_Log("World: Freeform map - adding starter Giraffe");
            entityManager.addAnimal(AnimalSpecies::Giraffe, centerX + 8,
                                    centerY + 4);
          }
        }
      }
    }
  } else {
    SDL_Log("World::loadFreeform: Failed to read %s", path.c_str());
  }

  // Reset camera
  this->camX = 0;
  this->camY = 0;
}

void World::update(const Uint8 *state, float deltaTime) {
  // Camera panning
  int scrollSpeed = 10;
  if (state[SDL_SCANCODE_LEFT])
    this->camX += scrollSpeed;
  if (state[SDL_SCANCODE_RIGHT])
    this->camX -= scrollSpeed;
  if (state[SDL_SCANCODE_UP])
    this->camY += scrollSpeed;
  if (state[SDL_SCANCODE_DOWN])
    this->camY -= scrollSpeed;

  // Zoom (optional)
  if (state[SDL_SCANCODE_PAGEUP])
    this->zoom = std::min(2.0f, zoom + 0.01f);
  if (state[SDL_SCANCODE_PAGEDOWN])
    this->zoom = std::max(0.5f, zoom - 0.01f);

  // Update all entities
  entityManager.update(deltaTime);
}

int World::getRemappedTerrainId(int terrainId) const {
  if (terrainId != 0)
    return terrainId;

  // Handle "Default Terrain" (ID 0) based on map header
  uint32_t baseId = this->zooReader.getBaseTerrainId();

  if (baseId == 6) {
    return 15; // Snow
  } else if (baseId == 1) {
    // Context sensitive "Other" terrain
    uint32_t mapType = this->zooReader.getMapType();
    if (mapType == 12) {
      return 8; // Savannah (Ancient)
    } else if (mapType == 11) {
      return 18; // Water (Ocean)
    } else if (mapType == 16) {
      return 9; // Forest Floor (Volcano/Island)
    } else {
      return 11; // Deciduous Floor (default for BaseId 1)
    }
  }

  return 0; // Grass (default)
}

void World::tileToScreen(int tileX, int tileY, int elevation, int &screenX,
                         int &screenY) const {
  screenX = (tileX - tileY) * (TILE_WIDTH / 2) + camX + startX;
  screenY = (tileX + tileY) * (TILE_HEIGHT / 2) + camY + startY;

  // Apply elevation offset (higher tiles appear higher on screen)
  screenY -= elevation * ELEVATION_HEIGHT;
}

void World::drawTerrain(SDL_Renderer *renderer) {
  static bool loggedOnce = false;

  int mapWidth = this->zooReader.getMapWidth();
  int mapHeight = this->zooReader.getMapHeight();

  if (mapWidth == 0 || mapHeight == 0 || mapWidth > 500)
    return;

  // Log terrain ID distribution once
  if (!loggedOnce) {
    int terrainCounts[20] = {0};
    for (int y = 0; y < mapHeight; y++) {
      for (int x = 0; x < mapWidth; x++) {
        const ZooReader::ZooTile *tile = this->zooReader.getTile(x, y);
        if (tile) {
          int tId = getRemappedTerrainId(tile->terrainId);
          if (tId >= 0 && tId < 20) {
            terrainCounts[tId]++;
            // Trace location of Snow/14
            if (tId == 14 || tId == 15) {
              SDL_Log("World: Found Terrain %d at %d,%d", tId, x, y);
            }
          }
        }
      }
    }
    SDL_Log("World: Terrain ID distribution:");
    for (int i = 0; i < 20; i++) {
      if (terrainCounts[i] > 0) {
        SDL_Log("  Terrain %d: %d tiles", i, terrainCounts[i]);
      }
    }
  }

  // Trace disabled for cleanup
  // bool trace = false;

  for (int y = 0; y < mapHeight; y++) {
    for (int x = 0; x < mapWidth; x++) {
      int idx = y * mapWidth + x;

      const ZooReader::ZooTile *tile = this->zooReader.getTile(x, y);
      if (!tile)
        continue;

      int screenX, screenY;
      tileToScreen(x, y, tile->elevation, screenX, screenY);

      // Culling (Updated for 1280x720)
      if (screenX < -TILE_WIDTH || screenX > 1280 || screenY < -TILE_HEIGHT ||
          screenY > 720) {
        continue;
      }

      int terrainId = getRemappedTerrainId(tile->terrainId);

      Animation *anim = SpriteDatabase::get().getTerrainSprite(terrainId);

      // Fallback to grass if specific terrain not loaded
      if (!anim && terrainId != 18) {
        anim = SpriteDatabase::get().getTerrainSprite(0);
      }

      if (anim) {
        anim->draw(renderer, screenX, screenY, CompassDirection::N);
      } else {
        // Debug: magenta dot for missing terrain
        SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
        SDL_RenderDrawPoint(renderer, screenX + 32, screenY + 16);
      }
    }
  }
  loggedOnce = true;
}

void World::drawEntities(SDL_Renderer *renderer) {
  SDL_Log("World::drawEntities: Start");
  entityManager.draw(renderer, camX, camY, startX, startY);
  SDL_Log("World::drawEntities: End");
}

void World::draw(SDL_Renderer *renderer) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black background
  SDL_RenderClear(renderer);

  // Draw terrain layer first
  drawTerrain(renderer);

  // Draw entities on top (with depth sorting)
  drawEntities(renderer);
}
