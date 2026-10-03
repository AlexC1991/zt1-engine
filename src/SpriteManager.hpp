#ifndef SPRITE_MANAGER_HPP
#define SPRITE_MANAGER_HPP

#include <unordered_map>
#include <string>
#include "Animation.hpp"
#include "ResourceManager.hpp"

// ============================================================================
// SPRITE MANAGER - Original ZT1 Engine Architecture
// ============================================================================
// PURPOSE: Centralized sprite loading and caching for all game entities
// SEPARATION: Handles sprite operations separate from general ResourceManager
// SCOPE: Animals, UI elements, buildings, scenery objects
// NOTE: Terrain sprites handled by SpriteDatabase
// ============================================================================

class SpriteManager {
public:
    static SpriteManager& get() {
        static SpriteManager instance;
        return instance;
    }

    // Initialize with resource manager
    void init(ResourceManager* rm);

    // Load an animation by path (e.g., "animals/elephant/idle.ani")
    // Returns cached animation if already loaded
    Animation* loadAnimation(const std::string& path);

    // Preload commonly-used sprites
    void preloadCommonSprites();

    // Clear cache
    void clear();

    // Get cache statistics
    int getCachedCount() const { return static_cast<int>(animationCache.size()); }

private:
    SpriteManager() = default;
    ~SpriteManager() = default;

    // Prevent copying
    SpriteManager(const SpriteManager&) = delete;
    SpriteManager& operator=(const SpriteManager&) = delete;

    ResourceManager* resourceManager = nullptr;

    // Animation cache: path -> Animation
    std::unordered_map<std::string, Animation> animationCache;

    bool isInitialized = false;
};

#endif // SPRITE_MANAGER_HPP
