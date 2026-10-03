// Test program to isolate terrain sprite loading
#include "src/ResourceManager.hpp"
#include "src/SpriteDatabase.hpp"
#include "src/AniFile.hpp"
#include <SDL2/SDL.h>

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO);

    ResourceManager rm;
    rm.loadResourceMap(".");

    SpriteDatabase::get().init(&rm);

    // Test loading a single terrain sprite
    SDL_Log("=== Testing terrain sprite loading ===");

    // Try loading grass terrain (ID 0)
    std::string path = "terrain/icgrass/icgrass";
    SDL_Log("Attempting to load: %s", path.c_str());

    Animation* anim = rm.getAnimation(path);

    if (anim && anim->isValid()) {
        SDL_Log("SUCCESS: Loaded terrain sprite!");
        SDL_Log("  Frame count: %zu", anim->animations->size());
    } else {
        SDL_Log("FAILED: Could not load terrain sprite");
    }

    SDL_Quit();
    return 0;
}
