#include "SpriteDatabase.hpp"
#include "MemoryTracker.hpp"
#include <SDL.h>

SpriteDatabase::~SpriteDatabase() {
    cleanup();
}

void SpriteDatabase::init(ResourceManager* rm) {
    ZT_MEMORY_CONTEXT(MemoryOwner::SpriteDatabase);
    this->resourceManager = rm;
    SDL_Log("SpriteDatabase: Initialized");
}

void SpriteDatabase::loadDefinitions() {
    ZT_MEMORY_CONTEXT(MemoryOwner::SpriteDatabase);

    if (!resourceManager) {
        SDL_Log("SpriteDatabase: Cannot load definitions - no ResourceManager");
        return;
    }

    // Load terrain sprites first (always needed)
    loadTerrainSprites();

    // Load animal sprites that exist in the resources
    // We'll try to load common animals and log what we find
    const AnimalSpecies animalsToTry[] = {
        AnimalSpecies::Lion,
        AnimalSpecies::Tiger,
        AnimalSpecies::Bear_Grizzly,
        AnimalSpecies::Bear_Polar,
        AnimalSpecies::Elephant_African,
        AnimalSpecies::Giraffe,
        AnimalSpecies::Zebra,
        AnimalSpecies::Hippo,
        AnimalSpecies::Gorilla,
        AnimalSpecies::Chimpanzee,
        AnimalSpecies::Leopard,
        AnimalSpecies::Cheetah,
        AnimalSpecies::Moose,
        AnimalSpecies::Ostrich,
        AnimalSpecies::Flamingo,
        AnimalSpecies::Crocodile,
        AnimalSpecies::Penguin_Emperor
    };

    int loadedCount = 0;
    for (auto species : animalsToTry) {
        const char* path = getAnimalPath(species);
        if (path) {
            SpriteDefinition def;
            def.basePath = path;
            def.type = EntityType::Animal;
            def.entityId = static_cast<uint16_t>(species);

            if (loadSpriteDefinition(def)) {
                animalSprites[def.entityId] = std::move(def);
                loadedCount++;
            }
        }
    }

    SDL_Log("SpriteDatabase: Loaded %d animal sprites", loadedCount);

    // Load guest sprite
    guestSprite.basePath = "scenery/guest";
    guestSprite.type = EntityType::Guest;
    guestSprite.entityId = 0;
    loadSpriteDefinition(guestSprite);

    // Load staff sprite
    staffSprite.basePath = "scenery/staff";
    staffSprite.type = EntityType::Staff;
    staffSprite.entityId = 0;
    loadSpriteDefinition(staffSprite);

    logLoadedSprites();
}

void SpriteDatabase::loadTerrainSprites() {
    ZT_MEMORY_CONTEXT(MemoryOwner::SpriteDatabase);

    SDL_Log("SpriteDatabase: Loading terrain sprites...");

    // Initialize all to nullptr
    for (int i = 0; i < 20; i++) {
        terrainSprites[i] = nullptr;
    }

    // Load terrain sprites
    // 0: Grass, 1: Sand, 2: Dirt, 3: Grey Rock, 4: Brown Rock, etc.
    terrainSprites[0] = resourceManager->getAnimation("terrain/icgrass");
    SDL_Log("SpriteDatabase: Terrain 0 (grass) = %p", (void*)terrainSprites[0]);

    terrainSprites[1] = resourceManager->getAnimation("terrain/icsand");
    SDL_Log("SpriteDatabase: Terrain 1 (sand) = %p", (void*)terrainSprites[1]);

    terrainSprites[2] = resourceManager->getAnimation("terrain/icdirt");
    terrainSprites[3] = resourceManager->getAnimation("terrain/icgrock");
    terrainSprites[4] = resourceManager->getAnimation("terrain/icbnrock");
    terrainSprites[5] = resourceManager->getAnimation("terrain/icaphalt");
    terrainSprites[6] = resourceManager->getAnimation("terrain/icccrete");
    terrainSprites[7] = resourceManager->getAnimation("terrain/icgravel");
    terrainSprites[8] = resourceManager->getAnimation("terrain/icgrs_sv");
    terrainSprites[9] = resourceManager->getAnimation("terrain/icffloor");
    terrainSprites[10] = resourceManager->getAnimation("terrain/icfflorc");
    terrainSprites[11] = resourceManager->getAnimation("terrain/icfflord");
    // 12, 13, 14 - not used or unknown
    terrainSprites[15] = resourceManager->getAnimation("terrain/icsnow");
    // 16, 17 - not used
    terrainSprites[18] = resourceManager->getAnimation("terrain/icwater");
    terrainSprites[19] = resourceManager->getAnimation("terrain/icdpwatr");

    int count = 0;
    for (int i = 0; i < 20; i++) {
        if (terrainSprites[i]) count++;
    }
    SDL_Log("SpriteDatabase: Loaded %d terrain sprites", count);
}

Animation* SpriteDatabase::getTerrainSprite(int terrainId) {
    if (terrainId < 0 || terrainId >= 20) return nullptr;
    return terrainSprites[terrainId];
}

bool SpriteDatabase::loadSpriteDefinition(SpriteDefinition& def) {
    if (!resourceManager) return false;

    // Try to load idle animation first
    std::string idlePath = def.basePath + "/idle";
    Animation* idle = resourceManager->getAnimation(idlePath);

    if (!idle) {
        // Try without /idle suffix
        idle = resourceManager->getAnimation(def.basePath);
    }

    if (!idle) {
        // Try alternate paths
        std::string altPath = def.basePath + "/N"; // Direction-based
        idle = resourceManager->getAnimation(altPath);
    }

    if (idle && idle->isValid()) {
        def.animations[AnimState::Idle] = idle;
        def.defaultAnim = idle;
        def.isLoaded = true;

        // Try to load other animation states
        const std::pair<AnimState, const char*> states[] = {
            {AnimState::Walking, "/walk"},
            {AnimState::Running, "/run"},
            {AnimState::Eating, "/eat"},
            {AnimState::Drinking, "/drink"},
            {AnimState::Sleeping, "/sleep"},
            {AnimState::Playing, "/play"}
        };

        for (const auto& state : states) {
            std::string statePath = def.basePath + state.second;
            Animation* anim = resourceManager->getAnimation(statePath);
            if (anim && anim->isValid()) {
                def.animations[state.first] = anim;
            }
        }

        return true;
    }

    return false;
}

SpriteDefinition* SpriteDatabase::getAnimalSprite(AnimalSpecies species) {
    auto it = animalSprites.find(static_cast<uint16_t>(species));
    if (it != animalSprites.end()) {
        return &it->second;
    }
    return nullptr;
}

SpriteDefinition* SpriteDatabase::getObjectSprite(ObjectType objType) {
    auto it = objectSprites.find(static_cast<uint16_t>(objType));
    if (it != objectSprites.end()) {
        return &it->second;
    }
    return nullptr;
}

SpriteDefinition* SpriteDatabase::getGuestSprite() {
    if (guestSprite.isLoaded) return &guestSprite;
    return nullptr;
}

SpriteDefinition* SpriteDatabase::getStaffSprite() {
    if (staffSprite.isLoaded) return &staffSprite;
    return nullptr;
}

Animation* SpriteDatabase::getAnimation(EntityType type, uint16_t id, AnimState state) {
    SpriteDefinition* def = nullptr;

    switch (type) {
        case EntityType::Animal:
            def = getAnimalSprite(static_cast<AnimalSpecies>(id));
            break;
        case EntityType::Guest:
            def = getGuestSprite();
            break;
        case EntityType::Staff:
            def = getStaffSprite();
            break;
        case EntityType::Object:
        case EntityType::Building:
        case EntityType::Foliage:
            def = getObjectSprite(static_cast<ObjectType>(id));
            break;
        default:
            break;
    }

    if (!def) return nullptr;

    // Try to get specific state animation
    auto it = def->animations.find(state);
    if (it != def->animations.end()) {
        return it->second;
    }

    // Fall back to default animation
    return def->defaultAnim;
}

void SpriteDatabase::logLoadedSprites() const {
    SDL_Log("=== SpriteDatabase Status ===");
    SDL_Log("Animals loaded: %zu", animalSprites.size());
    SDL_Log("Objects loaded: %zu", objectSprites.size());
    SDL_Log("Guest sprite: %s", guestSprite.isLoaded ? "loaded" : "not loaded");
    SDL_Log("Staff sprite: %s", staffSprite.isLoaded ? "loaded" : "not loaded");

    int terrainCount = 0;
    for (int i = 0; i < 20; i++) {
        if (terrainSprites[i]) terrainCount++;
    }
    SDL_Log("Terrain sprites: %d/20", terrainCount);
}

void SpriteDatabase::cleanup() {
    ZT_MEMORY_CONTEXT(MemoryOwner::SpriteDatabase);

    // Note: Animations are owned by ResourceManager, so we don't delete them here
    // Just clear our references
    animalSprites.clear();
    objectSprites.clear();
    guestSprite = SpriteDefinition();
    staffSprite = SpriteDefinition();

    for (int i = 0; i < 20; i++) {
        terrainSprites[i] = nullptr;
    }

    SDL_Log("SpriteDatabase: Cleaned up");
}

const char* SpriteDatabase::getAnimalPath(AnimalSpecies species) {
    switch (species) {
        case AnimalSpecies::Lion: return "animals/Lion";
        case AnimalSpecies::Tiger: return "animals/Tiger";
        case AnimalSpecies::Bear_Grizzly: return "animals/Grizzly";
        case AnimalSpecies::Bear_Polar: return "animals/PolarBear";
        case AnimalSpecies::Elephant_African: return "animals/AfricanElephant";
        case AnimalSpecies::Elephant_Asian: return "animals/AsianElephant";
        case AnimalSpecies::Giraffe: return "animals/Giraffe";
        case AnimalSpecies::Zebra: return "animals/Zebra";
        case AnimalSpecies::Hippo: return "animals/Hippo";
        case AnimalSpecies::Rhino_Black: return "animals/BlackRhino";
        case AnimalSpecies::Rhino_White: return "animals/WhiteRhino";
        case AnimalSpecies::Gorilla: return "animals/Gorilla";
        case AnimalSpecies::Chimpanzee: return "animals/Chimp";
        case AnimalSpecies::Orangutan: return "animals/Orangutan";
        case AnimalSpecies::Leopard: return "animals/Leopard";
        case AnimalSpecies::Cheetah: return "animals/Cheetah";
        case AnimalSpecies::Jaguar: return "animals/Jaguar";
        case AnimalSpecies::Moose: return "animals/Moose";
        case AnimalSpecies::Bison: return "animals/Bison";
        case AnimalSpecies::Okapi: return "animals/Okapi";
        case AnimalSpecies::Ostrich: return "animals/Ostrich";
        case AnimalSpecies::Flamingo: return "animals/Flamingo";
        case AnimalSpecies::Crocodile: return "animals/Crocodile";
        case AnimalSpecies::Gazelle: return "animals/Gazelle";
        case AnimalSpecies::Ibex: return "animals/Ibex";
        case AnimalSpecies::Gemsbok: return "animals/Gemsbok";
        case AnimalSpecies::Wildebeest: return "animals/Wildebeest";
        case AnimalSpecies::Camel: return "animals/Camel";
        case AnimalSpecies::Kangaroo: return "animals/Kangaroo";
        case AnimalSpecies::Penguin_Emperor: return "animals/EmperorPenguin";
        default: return nullptr;
    }
}

const char* SpriteDatabase::getObjectPath(ObjectType objType) {
    switch (objType) {
        case ObjectType::Tree_Oak: return "scenery/tree_oak";
        case ObjectType::Tree_Pine: return "scenery/tree_pine";
        case ObjectType::Rock_Small: return "scenery/rock_sm";
        case ObjectType::Rock_Large: return "scenery/rock_lg";
        case ObjectType::Bench: return "scenery/bench";
        case ObjectType::Trash_Can: return "scenery/trashcan";
        case ObjectType::Lamp_Post: return "scenery/lamp";
        case ObjectType::Fountain: return "scenery/fountain";
        case ObjectType::Statue: return "scenery/statue";
        case ObjectType::Zoo_Entrance: return "buildings/entrance";
        case ObjectType::Restaurant: return "buildings/restaurant";
        case ObjectType::Restroom: return "buildings/restroom";
        case ObjectType::Gift_Shop: return "buildings/giftshop";
        case ObjectType::Fence_Chain: return "fences/chain";
        case ObjectType::Fence_Iron: return "fences/iron";
        case ObjectType::Fence_Wood: return "fences/wood";
        default: return nullptr;
    }
}
