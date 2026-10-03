#include "SpriteDatabase.hpp"
#include "AniFile.hpp"
#include <SDL2/SDL.h>

// ============================================================================
// SPRITE DATABASE IMPLEMENTATION
// ============================================================================
// Loads and caches terrain sprites from terrain.ztd
// Following original ZT1 architecture: data caching only, no rendering
// ============================================================================

void SpriteDatabase::init(ResourceManager *rm) {
  if (!rm) {
    SDL_Log("[SpriteDatabase] ERROR: Null ResourceManager!");
    return;
  }
  this->resourceManager = rm;
  this->isInitialized = true;
  SDL_Log("[SpriteDatabase] Initialized");
}

void SpriteDatabase::loadDefinitions() {
  if (!isInitialized) {
    SDL_Log("[SpriteDatabase] ERROR: Not initialized!");
    return;
  }

  // Base game types 0-16, Marine Mania adds 17 (Gunnite) in tiletex1.cfg
  int loadedCount = loadDefinitionFile("terrain/tiletex.cfg");
  loadedCount += loadDefinitionFile("terrain/tiletex1.cfg");
  SDL_Log("[SpriteDatabase] Loaded %d terrain definitions", loadedCount);
}

int SpriteDatabase::loadDefinitionFile(const std::string &cfgPath) {
  if (!resourceManager->hasResource(cfgPath))
    return 0;

  IniReader *ini = resourceManager->getIniReader(cfgPath);
  if (!ini) {
    SDL_Log("[SpriteDatabase] ERROR: Failed to load %s", cfgPath.c_str());
    return 0;
  }

  int loadedCount = 0;
  for (const auto &section : ini->getSections()) {
    // Sections like [ttGrass], [ttSand] with 'type', 'texture' and 'icon'
    int type = ini->getInt(section, "type", -1);
    if (type == -1)
      continue;

    std::string texture = ini->get(section, "texture", "");
    if (!texture.empty())
      terrainTexturePaths[type] = texture;
    terrainBlendFlags[type] = ini->getInt(section, "blend", 1) != 0;
    terrainNames[type] =
        section.rfind("tt", 0) == 0 ? section.substr(2) : section;

    std::string icon = ini->get(section, "icon", "");
    if (!icon.empty())
      loadTerrainSprite(type, icon);
    loadedCount++;
  }

  delete ini;
  return loadedCount;
}

void SpriteDatabase::loadTerrainSprite(int terrainId, const std::string &path) {
  if (!resourceManager)
    return;

  // Use ResourceManager::getAnimation() which handles the full loading pipeline
  Animation *anim = resourceManager->getAnimation(path);

  if (anim && anim->isValid()) {
    // Store in cache (move semantics)
    terrainSprites.emplace(terrainId, std::move(*anim));
    SDL_Log("[SpriteDatabase]   ✓ Loaded terrain ID %d from %s", terrainId,
            path.c_str());
  } else {
    SDL_Log("[SpriteDatabase]   ✗ Failed to load terrain ID %d from %s",
            terrainId, path.c_str());
  }
}

Animation *SpriteDatabase::getTerrainSprite(int terrainId) {
  auto it = terrainSprites.find(terrainId);
  if (it != terrainSprites.end() && it->second.isValid()) {
    return &it->second;
  }

  return nullptr;
}

SDL_Texture *SpriteDatabase::getTerrainTexture(SDL_Renderer *renderer,
                                               int terrainId) {
  if (!renderer || !resourceManager)
    return nullptr;

  // Textures belong to one renderer; drop the cache if it changes
  if (renderer != textureRenderer) {
    for (auto &entry : terrainTextures) {
      if (entry.second)
        SDL_DestroyTexture(entry.second);
    }
    terrainTextures.clear();
    textureRenderer = renderer;
  }

  auto cached = terrainTextures.find(terrainId);
  if (cached != terrainTextures.end())
    return cached->second;

  SDL_Texture *texture = nullptr;
  auto path = terrainTexturePaths.find(terrainId);
  if (path != terrainTexturePaths.end()) {
    texture = resourceManager->getTexture(renderer, path->second,
                                          false); // world art, drawn 1:1
    // Terrain blends are drawn with per-vertex alpha, which RGB textures
    // ignore unless blending is switched on
    if (texture)
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    if (!texture)
      SDL_Log("[SpriteDatabase] Failed to load ground texture %s",
              path->second.c_str());
  }
  terrainTextures[terrainId] = texture; // cache misses too
  return texture;
}

std::string SpriteDatabase::getTerrainName(int terrainId) const {
  auto it = terrainNames.find(terrainId);
  return it == terrainNames.end() ? "" : it->second;
}

bool SpriteDatabase::terrainBlends(int terrainId) const {
  auto it = terrainBlendFlags.find(terrainId);
  return it == terrainBlendFlags.end() || it->second;
}

const SpriteDatabase::Sprite &
SpriteDatabase::getPathSprite(SDL_Renderer *renderer, const std::string &type,
                              int frame) {
  static const Sprite none;
  if (!renderer || !resourceManager || type.empty())
    return none;
  std::string key = type + "#" + std::to_string(frame);
  auto cached = pathSprites.find(key);
  if (cached != pathSprites.end())
    return cached->second;

  // Art folder from the type's .ai, e.g. paths/path/idle
  auto folder = pathArtFolders.find(type);
  if (folder == pathArtFolders.end()) {
    std::string anim = "idle";
    std::string ai = "paths/" + type + ".ai";
    if (resourceManager->hasResource(ai)) {
      if (IniReader *ini = resourceManager->getIniReader(ai)) {
        anim = ini->get("Animations", "idle", "idle");
        delete ini;
      }
    }
    folder = pathArtFolders.emplace(type, "paths/" + type + "/" + anim).first;
  }

  Sprite sprite;
  sprite.texture = resourceManager->getZt1FrameTexture(
      renderer, folder->second + "/" + std::to_string(frame), &sprite.anchorX,
      &sprite.anchorY);
  if (sprite.texture)
    SDL_QueryTexture(sprite.texture, nullptr, nullptr, &sprite.width,
                     &sprite.height);
  else
    SDL_Log("[SpriteDatabase] Missing path art %s/%d", folder->second.c_str(),
            frame);
  return pathSprites[key] = sprite; // cache misses too
}

void SpriteDatabase::clear() {
  terrainSprites.clear();
  for (auto &entry : terrainTextures) {
    if (entry.second)
      SDL_DestroyTexture(entry.second);
  }
  terrainTextures.clear();
  textureRenderer = nullptr;
  SDL_Log("[SpriteDatabase] Cache cleared");
}
