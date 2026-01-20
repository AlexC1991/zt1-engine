#include "World.hpp"
#include "CompassDirection.hpp"
#include "MemoryManager.hpp"

World::World(ResourceManager *resourceManager) {
  this->resourceManager = resourceManager;
  this->grassTexture = nullptr;
  this->camX = 0;
  this->camY = 0;
  
  // Initialize terrain sprites
  for (int i = 0; i < 20; i++) {
      this->terrainSprites[i] = nullptr;
  }
  
  SDL_Log("World initialized");
  
  // Load terrain assets immediately
  // 0: Grass, 1: Sand, 2: Dirt, 3: Grey Rock, 4: Brown Rock, 18: Water
  this->terrainSprites[0] = resourceManager->getAnimation("terrain/icgrass");
  this->terrainSprites[1] = resourceManager->getAnimation("terrain/icsand");
  this->terrainSprites[2] = resourceManager->getAnimation("terrain/icdirt");
  this->terrainSprites[3] = resourceManager->getAnimation("terrain/icgrock");
  this->terrainSprites[4] = resourceManager->getAnimation("terrain/icbnrock");
  this->terrainSprites[5] = resourceManager->getAnimation("terrain/icaphalt");
  this->terrainSprites[6] = resourceManager->getAnimation("terrain/icccrete");
  this->terrainSprites[7] = resourceManager->getAnimation("terrain/icgravel");
  this->terrainSprites[8] = resourceManager->getAnimation("terrain/icgrs_sv");
  this->terrainSprites[9] = resourceManager->getAnimation("terrain/icffloor");
  this->terrainSprites[10] = resourceManager->getAnimation("terrain/icfflorc");
  this->terrainSprites[11] = resourceManager->getAnimation("terrain/icfflord");
  this->terrainSprites[15] = resourceManager->getAnimation("terrain/icsnow");
  this->terrainSprites[18] = resourceManager->getAnimation("terrain/icwater");
  this->terrainSprites[19] = resourceManager->getAnimation("terrain/icdpwatr");
}

World::~World() {
    for (int i = 0; i < 20; i++) {
        if (this->terrainSprites[i]) delete this->terrainSprites[i];
    }
}

void World::loadScenario(const std::string &path) {
  SDL_Log("Loading scenario: %s", path.c_str());

  // Load terrain assets
  // Terrain assets loaded in constructor
  
  // Test loading ZooReader with a specific file
  std::string zooPath = "scenario/scn01/scn01.zoo"; // Test path
  
  int size = 0;
  void* raw_data = this->resourceManager->getFileContent(zooPath, &size);
  
  if (raw_data) {
      AssetBuffer buffer = MemoryManager::get().wrap(raw_data, static_cast<size_t>(size));

      if (this->zooReader.load(buffer)) {
          SDL_Log("World::loadScenario: Loaded map dimensions %dx%d", 
              this->zooReader.getMapWidth(), this->zooReader.getMapHeight());
      }
  } else {
      SDL_Log("World::loadScenario: Failed to read %s", zooPath.c_str());
  }

  // Reset camera
  this->camX = 0;
  this->camY = 0;
}

void World::loadFreeform(const std::string &path) {
  SDL_Log("Loading freeform map: %s", path.c_str());

  // Use memory manager to wrap file content
  int size = 0;
  void* raw_data = this->resourceManager->getFileContent(path, &size);
  
  if (raw_data) {
      // Check if it's an INI file (starts with '[')
      const char* data_char = (const char*)raw_data;
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
          } else {
             SDL_Log("World::loadFreeform: Could not find savegame key in %s", path.c_str());
          }
      } else {
          // Assume it's a binary map file
          AssetBuffer buffer = MemoryManager::get().wrap(raw_data, static_cast<size_t>(size));

          if (this->zooReader.load(buffer)) {
              SDL_Log("World::loadFreeform: Loaded map dimensions %dx%d", 
                  this->zooReader.getMapWidth(), this->zooReader.getMapHeight());
          }
      }
  } else {
      SDL_Log("World::loadFreeform: Failed to read %s", path.c_str());
  }

  // Reset camera
  this->camX = 0;
  this->camY = 0;
}

void World::update(const Uint8 *state) {
  int scrollSpeed = 10;
  if (state[SDL_SCANCODE_LEFT]) this->camX += scrollSpeed;
  if (state[SDL_SCANCODE_RIGHT]) this->camX -= scrollSpeed;
  if (state[SDL_SCANCODE_UP]) this->camY += scrollSpeed;
  if (state[SDL_SCANCODE_DOWN]) this->camY -= scrollSpeed;
}

void World::draw(SDL_Renderer *renderer) {
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Black background
  SDL_RenderClear(renderer);

  int mapWidth = this->zooReader.getMapWidth();
  int mapHeight = this->zooReader.getMapHeight();

  // If map not loaded, bail or draw something else
  if (mapWidth == 0) return;

  // Isometric Grid Constants
  const int TILE_WIDTH = 64;
  const int TILE_HEIGHT = 32;

  // Center offset
  int startX = 400;
  int startY = 100;

  for (int y = 0; y < mapHeight; y++) {
    for (int x = 0; x < mapWidth; x++) {
       // Iso Projection
       int screenX = (x - y) * (TILE_WIDTH / 2) + this->camX + startX;
       int screenY = (x + y) * (TILE_HEIGHT / 2) + this->camY + startY;

        // Culling (very rough)
        if (screenX < -TILE_WIDTH || screenX > 1000 || screenY < -TILE_HEIGHT || screenY > 800)
            continue;

        const ZooReader::ZooTile* tile = this->zooReader.getTile(x, y);
        if (!tile) continue;
        
        int tId = tile->terrainId;
        
        // Handle "Default Terrain" (ID 0) based on map header
        if (tId == 0) {
            uint32_t baseId = this->zooReader.getBaseTerrainId();
            if (baseId == 6) {
                 tId = 15; // Snow
            } else if (baseId == 1) {
                 // Context sensitive "Other" terrain
                 uint32_t mapType = this->zooReader.getMapType();
                 if (mapType == 12) {
                     tId = 8; // Savannah (Ancient)
                 } else if (mapType == 11) {
                     tId = 18; // Water (Ocean)
                 } else if (mapType == 16) {
                     tId = 9; // Forest Floor (Volcano/Island)
                 } else {
                     // Default for BaseId 1 (e.g. Mythgard - Colorful Forest)
                     tId = 11; // Deciduous Floor
                 }
            }
        }

        Animation* anim = nullptr;
        
        // Safeguard array bounds
        if (tId >= 0 && tId < 20) {
            anim = this->terrainSprites[tId];
        }
        
        // Fallback to Grass (0) if specific terrain not loaded
        if (!anim && tId != 18) anim = this->terrainSprites[0]; // Don't use grass for water if missing
        
        if (anim) {
            anim->draw(renderer, screenX, screenY, CompassDirection::N);
        } else if (tId == 18 && this->terrainSprites[18]) {
            // Water specific
             this->terrainSprites[18]->draw(renderer, screenX, screenY, CompassDirection::N);
        } else {
            // Wireframe or Dot for missing
            SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
            SDL_RenderDrawPoint(renderer, screenX + 32, screenY + 16);
        }
    }
  }
}
