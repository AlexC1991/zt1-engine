#include "Enums.hpp"
#ifndef ENTITY_MANAGER_HPP
#define ENTITY_MANAGER_HPP

#include <vector>
#include <memory>
#include <SDL2/SDL.h>
#include "Entity.hpp"
#include "ZooReader.hpp"

class EntityManager {
public:
    EntityManager();
    ~EntityManager();

    // Initialize from ZooReader tile data
    void loadFromZooReader(const ZooReader& reader);

    // Add entities manually
    void addAnimal(AnimalSpecies species, int tileX, int tileY);
    void addGuest(int tileX, int tileY);
    void addStaff(Staff::StaffType type, int tileX, int tileY);
    void addObject(ObjectType objType, int tileX, int tileY);

    // Update all entities
    void update(float deltaTime);

    // Draw all entities (sorted by depth)
    void draw(SDL_Renderer* renderer, int camX, int camY, int startX, int startY);

    // Get entities at a specific tile
    std::vector<Entity*> getEntitiesAt(int tileX, int tileY);

    // Get all entities
    const std::vector<std::unique_ptr<Entity>>& getEntities() const { return entities; }

    // Clear all entities
    void clear();

    // Stats
    int getAnimalCount() const { return animalCount; }
    int getGuestCount() const { return guestCount; }
    int getObjectCount() const { return objectCount; }

private:
    std::vector<std::unique_ptr<Entity>> entities;

    int animalCount = 0;
    int guestCount = 0;
    int objectCount = 0;

    // Sort entities by depth for proper rendering order
    void sortByDepth();

    // Parse entity data from ZooTile
    void parseEntityFromTile(const ZooReader::ZooTile& tile, int x, int y);
};

#endif // ENTITY_MANAGER_HPP
