#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include "CompassDirection.hpp"
#include "Animation.hpp"
#include "SpriteDatabase.hpp"

// Forward declaration
class World;

// Base Entity class for all game objects
class Entity {
public:
    Entity();
    Entity(EntityType type, uint16_t id);
    virtual ~Entity();

    // Position in world tile coordinates
    int tileX = 0;
    int tileY = 0;

    // Sub-tile position (0.0 - 1.0 within the tile)
    float subX = 0.5f;
    float subY = 0.5f;

    // Elevation (height level)
    int elevation = 0;

    // Direction the entity is facing
    CompassDirection direction = CompassDirection::S;

    // Animation state
    AnimState animState = AnimState::Idle;

    // Entity identification
    EntityType entityType = EntityType::None;
    uint16_t entityId = 0;

    // Visibility and activity
    bool isActive = true;
    bool isVisible = true;

    // Update entity (AI, animation, etc.)
    virtual void update(float deltaTime);

    // Draw entity at screen position
    virtual void draw(SDL_Renderer* renderer, int screenX, int screenY);

    // Get world position (for depth sorting)
    float getDepth() const { return static_cast<float>(tileX + tileY) + subX + subY; }

    // Set animation from sprite database
    void setAnimation(Animation* anim) { currentAnimation = anim; }
    Animation* getAnimation() const { return currentAnimation; }

protected:
    Animation* currentAnimation = nullptr;
    float animTimer = 0.0f;
};

// Animal entity with behavioral states
class Animal : public Entity {
public:
    Animal();
    Animal(AnimalSpecies species);
    ~Animal() override;

    AnimalSpecies species = AnimalSpecies::None;

    // Animal stats (0-100 scale)
    uint8_t health = 100;
    uint8_t hunger = 50;
    uint8_t thirst = 50;
    uint8_t happiness = 75;
    uint8_t energy = 100;

    // Age in game days
    uint16_t age = 0;

    // Gender
    bool isMale = true;

    // Behavioral state
    enum class BehaviorState {
        Idle,
        Wandering,
        Eating,
        Drinking,
        Sleeping,
        Playing,
        Sick,
        Angry
    };
    BehaviorState behaviorState = BehaviorState::Idle;

    // Movement
    float moveSpeed = 1.0f;
    int targetTileX = -1;
    int targetTileY = -1;

    void update(float deltaTime) override;
    void draw(SDL_Renderer* renderer, int screenX, int screenY) override;

    // Behavioral AI
    void updateBehavior(float deltaTime);
    void setTarget(int tx, int ty);

private:
    float behaviorTimer = 0.0f;
    float wanderTimer = 0.0f;
};

// Guest (visitor) entity
class Guest : public Entity {
public:
    Guest();
    ~Guest() override;

    // Guest stats
    uint8_t happiness = 75;
    uint8_t hunger = 50;
    uint8_t thirst = 50;
    uint8_t energy = 100;
    int money = 100;

    // Pathfinding
    std::vector<std::pair<int, int>> path;
    int pathIndex = 0;

    // State
    enum class GuestState {
        Entering,
        Wandering,
        WatchingAnimal,
        GoingToFood,
        Eating,
        GoingToExit,
        Leaving
    };
    GuestState guestState = GuestState::Entering;

    void update(float deltaTime) override;
    void draw(SDL_Renderer* renderer, int screenX, int screenY) override;

    void setPath(const std::vector<std::pair<int, int>>& newPath);
    void followPath(float deltaTime);
};

// Staff member entity
class Staff : public Entity {
public:
    Staff();
    ~Staff() override;

    enum class StaffType {
        Zookeeper,
        MaintenanceWorker,
        TourGuide
    };
    StaffType staffType = StaffType::Zookeeper;

    // Work state
    enum class WorkState {
        Idle,
        Walking,
        Working,
        OnBreak
    };
    WorkState workState = WorkState::Idle;

    // Pathfinding
    std::vector<std::pair<int, int>> path;
    int pathIndex = 0;

    void update(float deltaTime) override;
    void draw(SDL_Renderer* renderer, int screenX, int screenY) override;
};

// Static object/scenery entity
class SceneryObject : public Entity {
public:
    SceneryObject();
    SceneryObject(ObjectType objType);
    ~SceneryObject() override;

    ObjectType objectType = ObjectType::None;

    // Size in tiles (most are 1x1)
    int sizeX = 1;
    int sizeY = 1;

    // Object state
    bool isOperational = true;

    void update(float deltaTime) override;
    void draw(SDL_Renderer* renderer, int screenX, int screenY) override;
};

#endif // ENTITY_HPP
