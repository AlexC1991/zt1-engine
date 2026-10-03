#include "EntityManager.hpp"
#include "CompassDirection.hpp"
#include <SDL2/SDL.h>
#include <cmath>

// ============================================================================
// ENTITY MANAGER IMPLEMENTATION
// ============================================================================
// Manages all dynamic entities following original ZT1 architecture
// Tracks entity positions on tiles for spatial queries
// Separates simulation (update) from rendering (draw)
// ============================================================================

EntityManager::EntityManager() : nextId(1) {
}

EntityManager::~EntityManager() {
    clear();
}

void EntityManager::loadFromZooReader(const ZooReader& reader) {
    // TODO: Parse entity data from .zoo file
    // For now, this is a placeholder
    SDL_Log("[EntityManager] Loading entities from map...");

    // Future: Parse entity placement from zoo file header/data
    // Entities stored in zoo file contain:
    //   - Type ID
    //   - Position (x, y, elevation)
    //   - Initial state

    SDL_Log("[EntityManager] Entities loaded");
}

int EntityManager::addEntity(const Entity& entity) {
    Entity newEntity = entity;
    newEntity.id = nextId++;

    entities.push_back(newEntity);
    updateSpatialHash(entities.back());

    SDL_Log("[EntityManager] Added entity ID %d at (%.1f, %.1f)", newEntity.id, newEntity.x, newEntity.y);
    return newEntity.id;
}

void EntityManager::removeEntity(int id) {
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        if (it->id == id) {
            // Remove from spatial hash
            int tileX = static_cast<int>(it->x);
            int tileY = static_cast<int>(it->y);
            int key = packTileCoords(tileX, tileY);

            auto& entitiesAtTile = tileEntityMap[key];
            entitiesAtTile.erase(
                std::remove(entitiesAtTile.begin(), entitiesAtTile.end(), id),
                entitiesAtTile.end()
            );

            // Remove entity
            entities.erase(it);
            SDL_Log("[EntityManager] Removed entity ID %d", id);
            return;
        }
    }
}

Entity* EntityManager::getEntity(int id) {
    for (auto& entity : entities) {
        if (entity.id == id) {
            return &entity;
        }
    }
    return nullptr;
}

std::vector<Entity*> EntityManager::getEntitiesAtTile(int tileX, int tileY) {
    std::vector<Entity*> result;

    int key = packTileCoords(tileX, tileY);
    auto it = tileEntityMap.find(key);

    if (it != tileEntityMap.end()) {
        for (int entityId : it->second) {
            Entity* entity = getEntity(entityId);
            if (entity) {
                result.push_back(entity);
            }
        }
    }

    return result;
}

void EntityManager::updateSpatialHash(Entity& entity) {
    int tileX = static_cast<int>(entity.x);
    int tileY = static_cast<int>(entity.y);
    int key = packTileCoords(tileX, tileY);

    // Check if already in this tile
    auto& entitiesAtTile = tileEntityMap[key];
    bool found = false;
    for (int id : entitiesAtTile) {
        if (id == entity.id) {
            found = true;
            break;
        }
    }

    if (!found) {
        entitiesAtTile.push_back(entity.id);
    }
}

void EntityManager::update(float deltaTime) {
    // Simulate all entities
    for (auto& entity : entities) {
        // Simple movement update
        entity.x += entity.velocityX * deltaTime;
        entity.y += entity.velocityY * deltaTime;

        // Update spatial hash if entity moved to new tile
        updateSpatialHash(entity);

        // Simple AI simulation
        entity.hunger += 0.01f * deltaTime;
        entity.thirst += 0.015f * deltaTime;

        // Clamp values
        if (entity.hunger > 1.0f) entity.hunger = 1.0f;
        if (entity.thirst > 1.0f) entity.thirst = 1.0f;

        // State transitions (basic AI)
        if (entity.hunger > 0.7f && entity.state != EntityState::Eating) {
            entity.state = EntityState::Eating;
        } else if (entity.thirst > 0.7f && entity.state != EntityState::Drinking) {
            entity.state = EntityState::Drinking;
        } else if (std::abs(entity.velocityX) > 0.01f || std::abs(entity.velocityY) > 0.01f) {
            entity.state = EntityState::Walking;
        } else {
            entity.state = EntityState::Idle;
        }
    }
}

void EntityManager::draw(SDL_Renderer* renderer, int camX, int camY, int startX, int startY) {
    // Render all entities
    // Note: This should eventually be moved to a separate EntityRenderer class
    // for true separation of concerns

    for (auto& entity : entities) {
        if (!entity.currentAnimation) continue;

        // Convert entity world position to screen position
        // Using same isometric conversion as terrain
        const int TILE_WIDTH = 64;
        const int TILE_HEIGHT = 32;

        int isoX = (static_cast<int>(entity.x) - static_cast<int>(entity.y)) * (TILE_WIDTH / 2);
        int isoY = (static_cast<int>(entity.x) + static_cast<int>(entity.y)) * (TILE_HEIGHT / 2);

        int screenX = isoX + camX + startX;
        int screenY = isoY + camY + startY - (entity.elevation * 16);

        // Draw entity sprite
        CompassDirection dir = CompassDirection::S; // Default south

        // Convert facing direction to CompassDirection
        switch (entity.facingDirection) {
            case 0: dir = CompassDirection::N; break;
            case 1: dir = CompassDirection::NE; break;
            case 2: dir = CompassDirection::E; break;
            case 3: dir = CompassDirection::SE; break;
            case 4: dir = CompassDirection::S; break;
            case 5: dir = CompassDirection::SW; break;
            case 6: dir = CompassDirection::W; break;
            case 7: dir = CompassDirection::NW; break;
        }

        entity.currentAnimation->draw(renderer, screenX, screenY, dir);
    }
}

void EntityManager::clear() {
    entities.clear();
    tileEntityMap.clear();
    nextId = 1;
    SDL_Log("[EntityManager] Cleared all entities");
}
