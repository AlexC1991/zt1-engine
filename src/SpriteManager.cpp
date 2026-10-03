#include "SpriteManager.hpp"
#include "AniFile.hpp"
#include <SDL2/SDL.h>

// ============================================================================
// SPRITE MANAGER IMPLEMENTATION
// ============================================================================
// Centralized sprite loading and caching
// Separates sprite concerns from general ResourceManager file operations
// ============================================================================

void SpriteManager::init(ResourceManager* rm) {
    if (!rm) {
        SDL_Log("[SpriteManager] ERROR: Null ResourceManager!");
        return;
    }

    this->resourceManager = rm;
    this->isInitialized = true;
    SDL_Log("[SpriteManager] Initialized");
}

Animation* SpriteManager::loadAnimation(const std::string& path) {
    if (!isInitialized) {
        SDL_Log("[SpriteManager] ERROR: Not initialized!");
        return nullptr;
    }

    // Check cache first
    auto it = animationCache.find(path);
    if (it != animationCache.end()) {
        if (it->second.isValid()) {
            return &it->second;
        }
    }

    // Determine archive and directory from path
    // Path format: "animals/elephant/idle.ani" or "ui/cursor.ani"
    std::string ztdFile;
    std::string directory;

    if (path.find("animals/") == 0) {
        ztdFile = "animals.ztd";
        directory = path.substr(0, path.find_last_of('/'));
    } else if (path.find("ui/") == 0) {
        ztdFile = "ui.ztd";
        directory = path.substr(0, path.find_last_of('/'));
    } else {
        SDL_Log("[SpriteManager] Unknown archive for path: %s", path.c_str());
        return nullptr;
    }

    // Load animation using AniFile static method
    Animation* anim = AniFile::getAnimation(
        resourceManager->getPalletManager(),
        ztdFile,
        directory
    );

    if (anim && anim->isValid()) {
        // Animals are map art: upscaled frames for the zoomed-in map
        if (path.find("ui/") == 0)
            anim->setArtOptions(true, directory);
        else
            anim->setWorldArt(directory);

        // Store in cache (move semantics)
        animationCache.emplace(path, std::move(*anim));
        delete anim;

        SDL_Log("[SpriteManager] Loaded: %s", path.c_str());
        return &animationCache[path];
    }

    if (anim) delete anim;
    SDL_Log("[SpriteManager] Failed to load: %s", path.c_str());
    return nullptr;
}

void SpriteManager::preloadCommonSprites() {
    SDL_Log("[SpriteManager] Preloading common sprites...");

    // Common UI sprites
    // const char* commonSprites[] = {
    //     "ui/cursor.ani",
    //     "ui/button.ani",
    //     // Add more as needed
    // };

    // for (const char* sprite : commonSprites) {
    //     loadAnimation(sprite);
    // }

    SDL_Log("[SpriteManager] Preload complete (%d cached)", getCachedCount());
}

void SpriteManager::clear() {
    animationCache.clear();
    SDL_Log("[SpriteManager] Cache cleared");
}
