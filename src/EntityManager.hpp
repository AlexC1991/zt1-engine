#ifndef ENTITY_MANAGER_HPP
#define ENTITY_MANAGER_HPP

#include <vector>
#include <unordered_map>
#include <string>
#include <SDL2/SDL.h>
#include "ZooReader.hpp"
#include "SpriteManager.hpp"

// ============================================================================
// ENTITY MANAGER - Original ZT1 Engine Architecture
// ============================================================================
// PURPOSE: Track all dynamic entities in the world (animals, guests, staff)
// SEPARATION: Entity data storage and simulation, NOT rendering
// SPATIAL TRACKING: Knows which entities are on which tiles
// ============================================================================

enum class EntityType {
    Animal,
    Guest,
    Staff,
    Scenery
};

enum class EntityState {
    Idle,
    Walking,
    Eating,
    Drinking,
    Sleeping
};

struct Entity {
    int id;                          // Unique entity ID
    EntityType type;                 // What kind of entity
    EntityState state;               // Current animation state

    // Position
    float x, y;                      // World coordinates (can be fractional)
    int elevation;                   // Height level

    // Visual
    std::string animationPath;       // Path to animation (e.g., "animals/elephant/idle.ani")
    Animation* currentAnimation;     // Cached animation pointer
    int facingDirection;             // 0-7 (N, NE, E, SE, S, SW, W, NW)

    // Simulation
    float hunger;                    // 0.0 - 1.0
    float thirst;                    // 0.0 - 1.0
    float happiness;                 // 0.0 - 1.0

    // Movement
    float velocityX, velocityY;

    Entity()
        : id(0), type(EntityType::Animal), state(EntityState::Idle),
          x(0), y(0), elevation(0),
          currentAnimation(nullptr), facingDirection(0),
          hunger(0.5f), thirst(0.5f), happiness(0.8f),
          velocityX(0), velocityY(0) {}
};

class EntityManager {
public:
    EntityManager();
    ~EntityManager();

    // Load entities from .zoo file
    void loadFromZooReader(const ZooReader& reader);

    // Update all entities (simulation)
    void update(float deltaTime);

    // Render all entities (reads entity data, draws sprites)
    void draw(SDL_Renderer* renderer, int camX, int camY, int startX, int startY);

    // Add/remove entities
    int addEntity(const Entity& entity);
    void removeEntity(int id);

    // Get entity by ID
    Entity* getEntity(int id);

    // Get all entities at tile position
    std::vector<Entity*> getEntitiesAtTile(int tileX, int tileY);

    // Get all entities
    const std::vector<Entity>& getAllEntities() const { return entities; }

    // Clear all entities
    void clear();

private:
    std::vector<Entity> entities;
    int nextId;

    // Spatial hash: tile position -> entity IDs
    // Key: (y << 16) | x  (pack tile coords into single int)
    std::unordered_map<int, std::vector<int>> tileEntityMap;

    // Update spatial hash for an entity
    void updateSpatialHash(Entity& entity);

    // Helper: Pack tile coords
    int packTileCoords(int x, int y) const {
        return (y << 16) | (x & 0xFFFF);
    }
};

#endif // ENTITY_MANAGER_HPP
