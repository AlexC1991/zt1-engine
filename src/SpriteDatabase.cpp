#include "SpriteDatabase.hpp"
#include "ResourceManager.hpp"
#include "Animation.hpp"
#include "SDL.h"
#include <string>
#include <cctype>

// ============================================================================
// ZT1 TERRAIN ID MAPPING (from TERRAIN_ARCHITECTURE.md)
// ============================================================================
// ID | Name                | Config Section              | Texture
// ---+---------------------+-----------------------------+------------------
//  0 | Grass               | [ttGrass]                   | terrain/grass.tga
//  1 | Savannah Grass      | [ttSavannahGrass]           | terrain/grass_sv.tga
//  2 | Sand                | [ttSand]                    | terrain/sand.tga
//  3 | Dirt                | [ttDirt]                    | terrain/dirt.tga
//  4 | Rainforest Floor    | [ttForestFloor]             | terrain/ffloor.tga
//  5 | Brown Stone         | [ttBrownRock]               | terrain/bwnrock.tga
//  6 | Gray Stone          | [ttGrayRock]                | terrain/gryrock.tga
//  7 | Gravel              | [ttGravel]                  | terrain/gravel.tga
//  8 | Snow                | [ttSnow]                    | terrain/snow.tga
//  9 | Fresh Water         | [ttFreshWater]              | terrain/water.tga
// 10 | Salt Water          | [ttSaltWater]               | terrain/depwater.tga
// 11 | Deciduous Floor     | [ttDeciduousForestFloor]    | terrain/ffloord.tga
// 12 | Waterfall           | [ttWaterfall]               | terrain/bogus.tga
// 13 | Coniferous Floor    | [ttConiferousForestFloor]   | terrain/ffloorc.tga
// 14 | Concrete            | [ttConcrete]                | terrain/ccrete.tga
// 15 | Asphalt             | [ttAsphalt]                 | terrain/aphalt.tga
// 16 | Trampled Terrain    | [ttTrampled]                | terrain/worn.tga
// ============================================================================

SpriteDatabase& SpriteDatabase::get() {
    static SpriteDatabase instance;
    return instance;
}

void SpriteDatabase::init(ResourceManager *rm) {
    this->resourceManager = rm;
    for (int i = 0; i < 256; i++) {
        terrainSprites[i] = nullptr;
    }
    SDL_Log("[SpriteDatabase] Initialized");
}

void SpriteDatabase::loadDefinitions() {
    if (resourceManager) {
        loadTerrainSprites();
    }
}

Animation* SpriteDatabase::getTerrainSprite(int id) {
    if (id < 0 || id >= 256) return nullptr;
    return terrainSprites[id];
}

Animation* SpriteDatabase::getAnimation(const std::string& name) {
    if (!resourceManager) return nullptr;
    return resourceManager->getAnimation(name);
}

Animation* SpriteDatabase::getAnimation(EntityType type, int id, AnimState state) {
    // Placeholder - returns guest idle for any entity request
    return getAnimation("scenery/Guest/idle");
}

// Helper to try multiple file name variations
// ZT1 stores terrain animations in directories like:
//   terrain/icgrass/icgrass.ani
//   terrain/icsand/icsand.ani
//   terrain/icwater/icwater.ani
static Animation* tryLoadTerrain(ResourceManager* rm, const char* baseName) {
    char buf[128];
    Animation* anim = nullptr;

    // Try 1: Directory structure (terrain/icgrass/icgrass)
    snprintf(buf, sizeof(buf), "terrain/ic%s/ic%s", baseName, baseName);
    for (int i = 8; buf[i] && buf[i] != '/'; i++) buf[i] = tolower(buf[i]);
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '/' && buf[i+1] == 'i' && buf[i+2] == 'c') {
            for (int j = i+3; buf[j] && buf[j] != '/'; j++) buf[j] = tolower(buf[j]);
            break;
        }
    }
    anim = rm->getAnimation(buf);
    if (anim) return anim;

    // Try 2: Standard "Ic" prefix (terrain/IcGrass - legacy attempt)
    snprintf(buf, sizeof(buf), "terrain/Ic%s", baseName);
    anim = rm->getAnimation(buf);
    if (anim) return anim;

    // Try 3: Lowercase ic prefix (terrain/icgrass - legacy attempt)
    snprintf(buf, sizeof(buf), "terrain/ic%s", baseName);
    for (int i = 8; buf[i]; i++) buf[i] = tolower(buf[i]);
    anim = rm->getAnimation(buf);
    if (anim) return anim;

    return nullptr;
}

void SpriteDatabase::loadTerrainSprites() {
    SDL_Log("[SpriteDatabase] ========================================");
    SDL_Log("[SpriteDatabase] Loading terrain sprites");
    SDL_Log("[SpriteDatabase] ========================================");

    // Official ZT1 terrain mapping (from TERRAIN_ARCHITECTURE.md)
    struct TerrainDef {
        int id;
        const char* name;
        const char* baseName;
    };

    // Terrain name mapping based on actual ZTD file structure
    // Files are stored as: terrain/ic<name>/ic<name>.ani
    const TerrainDef terrains[] = {
        {  0, "Grass",              "grass"   },   // terrain/icgrass/icgrass.ani
        {  1, "Savannah Grass",     "grs_sv"  },   // terrain/icgrs_sv/icgrs_sv.ani
        {  2, "Sand",               "sand"    },   // terrain/icsand/icsand.ani
        {  3, "Dirt",               "dirt"    },   // terrain/icdirt/icdirt.ani
        {  4, "Rainforest Floor",   "ffloor"  },   // terrain/icffloor/icffloor.ani
        {  5, "Brown Stone",        "bnrock"  },   // terrain/icbnrock/icbnrock.ani
        {  6, "Gray Stone",         "grock"   },   // terrain/icgrock/icgrock.ani
        {  7, "Gravel",             "gravel"  },   // terrain/icgravel/icgravel.ani
        {  8, "Snow",               "snow"    },   // terrain/icsnow/icsnow.ani
        {  9, "Fresh Water",        "water"   },   // terrain/icwater/icwater.ani
        { 10, "Salt Water",         "dpwatr"  },   // terrain/icdpwatr/icdpwatr.ani
        { 11, "Deciduous Floor",    "fflord"  },   // terrain/icfflord/icfflord.ani
        { 12, "Waterfall",          "water"   },   // Use water as fallback
        { 13, "Coniferous Floor",   "fflorc"  },   // terrain/icfflorc/icfflorc.ani
        { 14, "Concrete",           "ccrete"  },   // terrain/icccrete/icccrete.ani
        { 15, "Asphalt",            "aphalt"  },   // terrain/icaphalt/icaphalt.ani
        { 16, "Trampled Terrain",   "dirt"    },   // Use dirt as fallback
    };

    int loaded = 0;
    int failed = 0;

    for (const auto& t : terrains) {
        Animation* anim = tryLoadTerrain(resourceManager, t.baseName);
        if (anim) {
            terrainSprites[t.id] = anim;
            loaded++;
            SDL_Log("[SpriteDatabase]   [OK] ID %2d: %-20s -> terrain/ic%s/ic%s",
                    t.id, t.name, t.baseName, t.baseName);
        } else {
            failed++;
            SDL_Log("[SpriteDatabase]   [!!] ID %2d: %-20s -> MISSING (tried terrain/ic%s/ic%s)",
                    t.id, t.name, t.baseName, t.baseName);
        }
    }

    // Set up fallbacks for missing textures
    SDL_Log("[SpriteDatabase] === SETTING UP FALLBACKS ===");

    // If specific textures are missing, alias to similar ones
    if (!terrainSprites[1] && terrainSprites[0]) {
        terrainSprites[1] = terrainSprites[0];  // Savannah -> Grass
        SDL_Log("[SpriteDatabase]   Aliased ID 1 (Savannah) -> ID 0 (Grass)");
    }
    if (!terrainSprites[4] && terrainSprites[0]) {
        terrainSprites[4] = terrainSprites[0];  // Rainforest Floor -> Grass
        SDL_Log("[SpriteDatabase]   Aliased ID 4 (Rainforest) -> ID 0 (Grass)");
    }
    if (!terrainSprites[11] && terrainSprites[4]) {
        terrainSprites[11] = terrainSprites[4]; // Deciduous -> Rainforest
        SDL_Log("[SpriteDatabase]   Aliased ID 11 (Deciduous) -> ID 4 (Rainforest)");
    }
    if (!terrainSprites[13] && terrainSprites[4]) {
        terrainSprites[13] = terrainSprites[4]; // Coniferous -> Rainforest
        SDL_Log("[SpriteDatabase]   Aliased ID 13 (Coniferous) -> ID 4 (Rainforest)");
    }
    if (!terrainSprites[12] && terrainSprites[9]) {
        terrainSprites[12] = terrainSprites[9]; // Waterfall -> Water
        SDL_Log("[SpriteDatabase]   Aliased ID 12 (Waterfall) -> ID 9 (Water)");
    }
    if (!terrainSprites[16] && terrainSprites[3]) {
        terrainSprites[16] = terrainSprites[3]; // Trampled -> Dirt
        SDL_Log("[SpriteDatabase]   Aliased ID 16 (Trampled) -> ID 3 (Dirt)");
    }

    // Count final loaded sprites
    int finalCount = 0;
    for (int i = 0; i < 17; i++) {
        if (terrainSprites[i]) finalCount++;
    }

    SDL_Log("[SpriteDatabase] ========================================");
    SDL_Log("[SpriteDatabase] Loaded: %d  Failed: %d  Final: %d/17", loaded, failed, finalCount);
    SDL_Log("[SpriteDatabase] ========================================");
}
