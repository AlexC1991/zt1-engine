#include "EntityManager.hpp"
#include "MemoryTracker.hpp"
#include <algorithm>
#include <SDL.h>

EntityManager::EntityManager() {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);
}

EntityManager::~EntityManager() {
    clear();
}

void EntityManager::loadFromZooReader(const ZooReader& reader) {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);

    clear();

    int width = reader.getMapWidth();
    int height = reader.getMapHeight();

    SDL_Log("EntityManager: Scanning %dx%d tiles for entities...", width, height);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const ZooReader::ZooTile* tile = reader.getTile(x, y);
            if (tile) {
                parseEntityFromTile(*tile, x, y);
            }
        }
    }

    SDL_Log("EntityManager: Loaded %d animals, %d guests, %d objects",
            animalCount, guestCount, objectCount);
}

void EntityManager::parseEntityFromTile(const ZooReader::ZooTile& tile, int x, int y) {
    // Zoo Tycoon tile data format (8 bytes after terrain/elevation):
    // The exact format is reverse-engineered and may vary by version.
    //
    // Typical structure based on Zoo Tycoon modding community:
    // Byte 0-1: Entity type/ID (uint16_t) - 0 = no entity
    // Byte 2: Entity flags (walkable, water, etc.)
    // Byte 3: Entity sub-type or state
    // Byte 4-5: Additional data (health, age, etc.)
    // Byte 6-7: Reserved/padding
    //
    // Note: This is speculative and may need adjustment based on actual file analysis

    uint16_t entityWord = tile.data[0] | (tile.data[1] << 8);
    uint8_t flags = tile.data[2];
    uint8_t subType = tile.data[3];

    // Skip if no entity data
    if (entityWord == 0) return;

    // Entity type encoding (speculative):
    // 0x0001-0x00FF: Animals (species ID)
    // 0x0100-0x01FF: Objects/Scenery
    // 0x0200-0x02FF: Buildings
    // 0x0300-0x03FF: Fences
    // 0x0400-0x04FF: Paths
    // 0x1000+: Special entities (guests spawn from entrance, etc.)

    if (entityWord >= 1 && entityWord <= 255) {
        // Potential animal
        // Map entity word to species (this mapping is speculative)
        AnimalSpecies species = static_cast<AnimalSpecies>(entityWord);

        // Only create if it's a known species
        if (entityWord <= static_cast<uint16_t>(MAX_SPECIES)) {
            auto animal = std::make_unique<Animal>(species);
            animal->tileX = x;
            animal->tileY = y;
            animal->elevation = tile.elevation;

            // Parse additional data
            animal->isMale = (flags & 0x01) != 0;
            animal->health = std::min(100, static_cast<int>(tile.data[4]));

            entities.push_back(std::move(animal));
            animalCount++;
        }
    }
    else if (entityWord >= 0x0100 && entityWord <= 0x01FF) {
        // Object/Scenery
        ObjectType objType = static_cast<ObjectType>(entityWord - 0x0100);

        auto obj = std::make_unique<SceneryObject>(objType);
        obj->tileX = x;
        obj->tileY = y;
        obj->elevation = tile.elevation;

        entities.push_back(std::move(obj));
        objectCount++;
    }
    else if (entityWord >= 0x0200 && entityWord <= 0x02FF) {
        // Building
        ObjectType objType = static_cast<ObjectType>(entityWord - 0x0200 + 100); // Buildings start at 100

        auto obj = std::make_unique<SceneryObject>(objType);
        obj->tileX = x;
        obj->tileY = y;
        obj->elevation = tile.elevation;

        entities.push_back(std::move(obj));
        objectCount++;
    }

    // Note: Guests and staff are typically spawned dynamically, not stored in map data
}

void EntityManager::addAnimal(AnimalSpecies species, int tileX, int tileY) {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);

    auto animal = std::make_unique<Animal>(species);
    animal->tileX = tileX;
    animal->tileY = tileY;

    entities.push_back(std::move(animal));
    animalCount++;
}

void EntityManager::addGuest(int tileX, int tileY) {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);

    auto guest = std::make_unique<Guest>();
    guest->tileX = tileX;
    guest->tileY = tileY;

    entities.push_back(std::move(guest));
    guestCount++;
}

void EntityManager::addStaff(Staff::StaffType type, int tileX, int tileY) {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);

    auto staff = std::make_unique<Staff>();
    staff->staffType = type;
    staff->tileX = tileX;
    staff->tileY = tileY;

    entities.push_back(std::move(staff));
}

void EntityManager::addObject(ObjectType objType, int tileX, int tileY) {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);

    auto obj = std::make_unique<SceneryObject>(objType);
    obj->tileX = tileX;
    obj->tileY = tileY;

    entities.push_back(std::move(obj));
    objectCount++;
}

void EntityManager::update(float deltaTime) {
    for (auto& entity : entities) {
        if (entity && entity->isActive) {
            entity->update(deltaTime);
        }
    }
}

void EntityManager::sortByDepth() {
    std::sort(entities.begin(), entities.end(),
        [](const std::unique_ptr<Entity>& a, const std::unique_ptr<Entity>& b) {
            return a->getDepth() < b->getDepth();
        });
}

void EntityManager::draw(SDL_Renderer* renderer, int camX, int camY, int startX, int startY) {
    // Sort entities by depth before drawing
    sortByDepth();

    const int TILE_WIDTH = 64;
    const int TILE_HEIGHT = 32;

    for (auto& entity : entities) {
        if (!entity || !entity->isVisible) continue;

        // Calculate screen position using same isometric projection as terrain
        float worldX = entity->tileX + entity->subX;
        float worldY = entity->tileY + entity->subY;

        int screenX = static_cast<int>((worldX - worldY) * (TILE_WIDTH / 2)) + camX + startX;
        int screenY = static_cast<int>((worldX + worldY) * (TILE_HEIGHT / 2)) + camY + startY;

        // Culling - match actual screen resolution (1280x720)
        if (screenX < -TILE_WIDTH || screenX > 1280 || screenY < -TILE_HEIGHT || screenY > 720) {
            continue;
        }

        entity->draw(renderer, screenX, screenY);
    }
}

std::vector<Entity*> EntityManager::getEntitiesAt(int tileX, int tileY) {
    std::vector<Entity*> result;

    for (auto& entity : entities) {
        if (entity && entity->tileX == tileX && entity->tileY == tileY) {
            result.push_back(entity.get());
        }
    }

    return result;
}

void EntityManager::clear() {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);

    entities.clear();
    animalCount = 0;
    guestCount = 0;
    objectCount = 0;
}
