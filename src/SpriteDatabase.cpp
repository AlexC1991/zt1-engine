#include "SpriteDatabase.hpp"
#include "ArtScaler.hpp"
#include "AniFile.hpp"
#include "Utils.hpp"
#include <SDL3/SDL.h>

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
    ZT_TRACE("[SpriteDatabase]   ✓ Loaded terrain ID %d from %s", terrainId,
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

// Destroys every texture in a cache and empties it
static void destroyAll(std::unordered_map<int, SDL_Texture *> &cache) {
  for (auto &entry : cache) {
    if (entry.second)
      SDL_DestroyTexture(entry.second);
  }
  cache.clear();
}

SDL_Texture *SpriteDatabase::getTerrainTexture(SDL_Renderer *renderer,
                                               int terrainId, bool hires) {
  if (!renderer || !resourceManager)
    return nullptr;

  // Textures belong to one renderer; drop the cache if it changes
  if (renderer != textureRenderer) {
    destroyAll(terrainTextures);
    destroyAll(terrainTexturesHi);
    textureRenderer = renderer;
    ++epoch;
  }

  auto &cache = hires ? terrainTexturesHi : terrainTextures;
  auto cached = cache.find(terrainId);
  if (cached != cache.end())
    return cached->second;

  SDL_Texture *texture = nullptr;
  auto path = terrainTexturePaths.find(terrainId);
  if (path != terrainTexturePaths.end()) {
    // 1:1 for the normal map; upscaled (wrapping, as ground textures tile)
    // for the zoomed-in map
    texture = hires ? resourceManager->getTexture(
                          renderer, path->second, true,
                          ArtScaler::worldFactor(), true)
                    : resourceManager->getTexture(renderer, path->second,
                                                  false);
    // Terrain blends are drawn with per-vertex alpha, which RGB textures
    // ignore unless blending is switched on
    if (texture)
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    if (!texture)
      SDL_Log("[SpriteDatabase] Failed to load ground texture %s",
              path->second.c_str());
  }
  cache[terrainId] = texture; // cache misses too
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
                              int frame, bool hires) {
  static const Sprite none;
  if (!renderer || !resourceManager || type.empty())
    return none;
  std::string key = type + "#" + std::to_string(frame);
  auto cached = pathSprites.find(key);
  if (cached != pathSprites.end()) {
    Sprite &sprite = cached->second;
    if (hires && !sprite.hiTried && sprite.texture) {
      sprite.hiTried = true;
      sprite.hiTexture = resourceManager->getZt1FrameTexture(
          renderer, pathArtFolders[type] + "/" + std::to_string(frame),
          nullptr, nullptr, true);
    }
    return sprite;
  }

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

    if (anim.size() > 4 && anim.compare(anim.size() - 4, 4, ".ani") == 0)
      anim.resize(anim.size() - 4);
    std::string artFolder = anim.find('/') == std::string::npos ? "paths/" + type + "/" + anim
                                                              : anim.substr(0, anim.find_last_of('/'));
    folder = pathArtFolders.emplace(type, artFolder).first;
  }

  Sprite sprite;
  sprite.texture = resourceManager->getZt1FrameTexture(
      renderer, folder->second + "/" + std::to_string(frame), &sprite.anchorX,
      &sprite.anchorY);
  if (sprite.texture)
    ZT_QueryTexture(sprite.texture, nullptr, nullptr, &sprite.width,
                     &sprite.height);
  else
    SDL_Log("[SpriteDatabase] Missing path art %s/%d", folder->second.c_str(),
            frame);
  pathSprites[key] = sprite; // cache misses too
  return hires ? getPathSprite(renderer, type, frame, true) : pathSprites[key];
}

int SpriteDatabase::fringeSlot(bool towardRight, const char *shape) {
  static const char *const shapes[5] = {"0011", "0001", "0010", "0110", "1011"};
  for (int i = 0; i < 5; i++)
    if (std::string(shape) == shapes[i])
      return (towardRight ? 5 : 0) + i;
  return -1;
}

// measured: zoo.exe draws its cliff faces from fringe.cfg's pictures - each
// of the ten found pixel for pixel (mean difference under 4 of 255) in its
// own picture of a map's cliffs in the Lab (11-22), where the terrain
// textures (ccrete.tga among them) made solid colours changed none of them
SDL_Texture *SpriteDatabase::getFringeAtlas(SDL_Renderer *renderer) {
  if (!renderer || !resourceManager)
    return nullptr;
  if (renderer != fringeRenderer) {
    if (fringeAtlas)
      SDL_DestroyTexture(fringeAtlas);
    fringeAtlas = nullptr;
    fringeTried = false;
    fringeRenderer = renderer;
  }
  if (fringeTried)
    return fringeAtlas;
  fringeTried = true;
  IniReader *cfg = resourceManager->hasResource("fringe.cfg") ? resourceManager->getIniReader("fringe.cfg") : nullptr;
  if (!cfg)
    return nullptr;
  SDL_Surface *atlas = SDL_CreateSurface(32 * kFringeSlots, 64, SDL_PIXELFORMAT_RGBA32);
  SDL_FillSurfaceRect(atlas, nullptr, 0);
  int placed = 0;
  for (int section : {1, 2, 3, 4, 5, 11, 12, 13, 14, 15}) {
    const std::string image = cfg->get(std::to_string(section), "image");
    // (fringe/l0011.bmp: l the face toward the lower left, then its shape)
    const size_t slash = image.find_last_of('/');
    const std::string name = slash == std::string::npos ? image : image.substr(slash + 1);
    const int slot = name.size() >= 5 ? fringeSlot(name[0] == 'r', name.substr(1, 4).c_str()) : -1;
    int size = 0;
    void *bytes = slot >= 0 ? resourceManager->getFileBytes(image, &size) : nullptr;
    SDL_Surface *bmp = bytes ? SDL_LoadBMP_IO(SDL_IOFromConstMem(bytes, size_t(size)), true) : nullptr;
    SDL_Surface *rgba = bmp ? SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (rgba && rgba->w == 32 && rgba->h == 64) {
      // (the key colour, 255,0,128: not drawn)
      for (int y = 0; y < 64; y++) {
        Uint8 *row = static_cast<Uint8 *>(rgba->pixels) + y * rgba->pitch;
        for (int x = 0; x < 32; x++) {
          Uint8 *px = row + x * 4;
          const bool key = px[0] > 247 && px[1] < 8 && px[2] > 120 && px[2] < 136;
          px[3] = key ? 0 : 255;
          if (key)
            px[0] = px[1] = px[2] = 0;
        }
      }
      SDL_Rect to{slot * 32, 0, 32, 64};
      SDL_SetSurfaceBlendMode(rgba, SDL_BLENDMODE_NONE);
      SDL_BlitSurface(rgba, nullptr, atlas, &to);
      placed++;
    }
    if (rgba)
      SDL_DestroySurface(rgba);
    if (bmp)
      SDL_DestroySurface(bmp);
    if (bytes)
      free(bytes);
  }
  delete cfg;
  if (placed == kFringeSlots) {
    fringeAtlas = SDL_CreateTextureFromSurface(renderer, atlas);
    if (fringeAtlas) {
      SDL_SetTextureBlendMode(fringeAtlas, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(fringeAtlas, SDL_SCALEMODE_NEAREST);
    }
  } else {
    SDL_Log("[SpriteDatabase] fringe.cfg: %d of %d cliff pieces", placed, kFringeSlots);
  }
  SDL_DestroySurface(atlas);
  return fringeAtlas;
}

void SpriteDatabase::clear() {
  terrainSprites.clear();
  terrainTexturePaths.clear();
  terrainBlendFlags.clear();
  terrainNames.clear();
  pathSprites.clear();
  pathArtFolders.clear();
  destroyAll(terrainTextures);
  destroyAll(terrainTexturesHi);
  textureRenderer = nullptr;
  if (fringeAtlas)
    SDL_DestroyTexture(fringeAtlas);
  fringeAtlas = nullptr;
  fringeRenderer = nullptr;
  fringeTried = false;
  ++epoch;
  SDL_Log("[SpriteDatabase] Cache cleared");
}
