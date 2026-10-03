#ifndef SPRITE_DATABASE_HPP
#define SPRITE_DATABASE_HPP

#include <unordered_map>
#include <string>
#include "Animation.hpp"
#include "ResourceManager.hpp"

// ============================================================================
// SPRITE DATABASE - Original ZT1 Engine Architecture
// ============================================================================
// PURPOSE: Cache and manage terrain sprite textures
// SEPARATION: This class ONLY handles sprite loading and caching
//             Rendering logic belongs in WorldRenderer
// ============================================================================

class SpriteDatabase {
public:
    static SpriteDatabase& get() {
        static SpriteDatabase instance;
        return instance;
    }

    // Initialize with resource manager
    void init(ResourceManager* rm);

    // Load terrain sprite definitions from terrain.ztd
    void loadDefinitions();

    // Get cached terrain sprite by ID (0-15)
    // Returns nullptr if not loaded
    Animation* getTerrainSprite(int terrainId);

    // Get the tileable ground texture for a terrain type (the "texture" key
    // in terrain/tiletex*.cfg). Loaded on first use; nullptr if unavailable.
    SDL_Texture* getTerrainTexture(SDL_Renderer* renderer, int terrainId);

    // Whether a terrain type blends into its neighbours ("blend" key in
    // terrain/tiletex*.cfg, default 1; concrete and asphalt are 0)
    bool terrainBlends(int terrainId) const;

    // Clear all cached sprites
    void clear();

private:
    SpriteDatabase() = default;
    ~SpriteDatabase() = default;

    // Prevent copying
    SpriteDatabase(const SpriteDatabase&) = delete;
    SpriteDatabase& operator=(const SpriteDatabase&) = delete;

    // Load a single terrain sprite from archive
    void loadTerrainSprite(int terrainId, const std::string& path);

    // Read one tiletex*.cfg file
    int loadDefinitionFile(const std::string& cfgPath);

    ResourceManager* resourceManager = nullptr;

    // Terrain sprite cache: terrainId (0-15) -> Animation
    std::unordered_map<int, Animation> terrainSprites;

    // Ground textures: terrainId -> path, and lazily created textures
    std::unordered_map<int, std::string> terrainTexturePaths;
    std::unordered_map<int, bool> terrainBlendFlags;
    std::unordered_map<int, SDL_Texture*> terrainTextures;
    SDL_Renderer* textureRenderer = nullptr;

    bool isInitialized = false;
};

#endif // SPRITE_DATABASE_HPP
