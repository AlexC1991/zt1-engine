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

    // One frame of a path type's art: paths/<type>.ai names the animation
    // ([Animations] idle = idle), whose frames are paths/<type>/idle/1..20.
    // Frames 1-4 are ramps, 5-20 flat pieces with kerbs. Loaded on first use.
    struct Sprite {
        SDL_Texture* texture = nullptr;
        int anchorX = 0, anchorY = 0; // image pixel on the tile centre
        int width = 0, height = 0;
    };
    const Sprite& getPathSprite(SDL_Renderer* renderer, const std::string& type,
                                int frame);

    // Terrain name from its tiletex*.cfg section, without "tt" (e.g. "Grass",
    // "SavannahGrass"); used to look up colours such as ui/miniclr.cfg
    std::string getTerrainName(int terrainId) const;

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
    std::unordered_map<int, std::string> terrainNames;
    std::unordered_map<int, SDL_Texture*> terrainTextures;
    SDL_Renderer* textureRenderer = nullptr;
    std::unordered_map<std::string, Sprite> pathSprites;
    std::unordered_map<std::string, std::string> pathArtFolders;

    bool isInitialized = false;
};

#endif // SPRITE_DATABASE_HPP
