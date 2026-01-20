#include "Entity.hpp"
#include "MemoryTracker.hpp"
#include <SDL.h>
#include <cstdlib>

// ============================================================================
// Base Entity
// ============================================================================

Entity::Entity() {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);
}

Entity::Entity(EntityType type, uint16_t id)
    : entityType(type), entityId(id) {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);
}

Entity::~Entity() {
    // Animation is owned by SpriteDatabase/ResourceManager, don't delete
}

void Entity::update(float deltaTime) {
    animTimer += deltaTime;
}

void Entity::draw(SDL_Renderer* renderer, int screenX, int screenY) {
    if (!isVisible || !currentAnimation) return;

    // Apply elevation offset (higher tiles appear higher on screen)
    int elevationOffset = elevation * 8; // 8 pixels per elevation level

    currentAnimation->draw(renderer, screenX, screenY - elevationOffset, direction);
}

// ============================================================================
// Animal
// ============================================================================

Animal::Animal() : Entity(EntityType::Animal, 0) {
    species = AnimalSpecies::None;
}

Animal::Animal(AnimalSpecies sp) : Entity(EntityType::Animal, static_cast<uint16_t>(sp)) {
    species = sp;

    // Set animation from sprite database
    Animation* anim = SpriteDatabase::get().getAnimation(EntityType::Animal, entityId, AnimState::Idle);
    if (anim) {
        setAnimation(anim);
    }
}

Animal::~Animal() {
}

void Animal::update(float deltaTime) {
    Entity::update(deltaTime);
    updateBehavior(deltaTime);

    // Update needs over time (very slowly for now)
    if (animTimer > 10.0f) {
        animTimer = 0;
        if (hunger < 100) hunger += 1;
        if (thirst < 100) thirst += 1;
        if (energy > 0 && behaviorState != BehaviorState::Sleeping) energy -= 1;
    }

    // Update animation state based on behavior
    AnimState newState = AnimState::Idle;
    switch (behaviorState) {
        case BehaviorState::Wandering:
            newState = AnimState::Walking;
            break;
        case BehaviorState::Eating:
            newState = AnimState::Eating;
            break;
        case BehaviorState::Drinking:
            newState = AnimState::Drinking;
            break;
        case BehaviorState::Sleeping:
            newState = AnimState::Sleeping;
            break;
        case BehaviorState::Playing:
            newState = AnimState::Playing;
            break;
        default:
            newState = AnimState::Idle;
            break;
    }

    if (newState != animState) {
        animState = newState;
        Animation* anim = SpriteDatabase::get().getAnimation(EntityType::Animal, entityId, animState);
        if (anim) {
            setAnimation(anim);
        }
    }
}

void Animal::updateBehavior(float deltaTime) {
    behaviorTimer += deltaTime;
    wanderTimer += deltaTime;

    // Simple behavior state machine
    switch (behaviorState) {
        case BehaviorState::Idle:
            // Randomly decide to wander after some time
            if (wanderTimer > 3.0f + (rand() % 5)) {
                wanderTimer = 0;
                behaviorState = BehaviorState::Wandering;

                // Pick a random nearby target
                int range = 3;
                targetTileX = tileX + (rand() % (range * 2 + 1)) - range;
                targetTileY = tileY + (rand() % (range * 2 + 1)) - range;
            }
            break;

        case BehaviorState::Wandering:
            // Move toward target
            if (targetTileX >= 0 && targetTileY >= 0) {
                float dx = static_cast<float>(targetTileX - tileX);
                float dy = static_cast<float>(targetTileY - tileY);

                if (dx != 0 || dy != 0) {
                    // Update direction based on movement
                    if (std::abs(dx) > std::abs(dy)) {
                        direction = dx > 0 ? CompassDirection::E : CompassDirection::W;
                    } else {
                        direction = dy > 0 ? CompassDirection::S : CompassDirection::N;
                    }

                    // Move sub-position
                    float speed = moveSpeed * deltaTime;
                    if (dx > 0) subX += speed;
                    else if (dx < 0) subX -= speed;
                    if (dy > 0) subY += speed;
                    else if (dy < 0) subY -= speed;

                    // Move to next tile if sub-position overflows
                    if (subX >= 1.0f) { subX -= 1.0f; tileX++; }
                    if (subX < 0.0f) { subX += 1.0f; tileX--; }
                    if (subY >= 1.0f) { subY -= 1.0f; tileY++; }
                    if (subY < 0.0f) { subY += 1.0f; tileY--; }
                }

                // Check if reached target
                if (tileX == targetTileX && tileY == targetTileY) {
                    behaviorState = BehaviorState::Idle;
                    targetTileX = -1;
                    targetTileY = -1;
                }
            } else {
                behaviorState = BehaviorState::Idle;
            }
            break;

        case BehaviorState::Eating:
            if (behaviorTimer > 5.0f) {
                behaviorTimer = 0;
                hunger = std::max(0, hunger - 30);
                behaviorState = BehaviorState::Idle;
            }
            break;

        case BehaviorState::Drinking:
            if (behaviorTimer > 3.0f) {
                behaviorTimer = 0;
                thirst = std::max(0, thirst - 30);
                behaviorState = BehaviorState::Idle;
            }
            break;

        case BehaviorState::Sleeping:
            if (behaviorTimer > 10.0f) {
                behaviorTimer = 0;
                energy = std::min(100, energy + 50);
                behaviorState = BehaviorState::Idle;
            }
            break;

        default:
            break;
    }
}

void Animal::setTarget(int tx, int ty) {
    targetTileX = tx;
    targetTileY = ty;
    if (tx >= 0 && ty >= 0) {
        behaviorState = BehaviorState::Wandering;
    }
}

void Animal::draw(SDL_Renderer* renderer, int screenX, int screenY) {
    Entity::draw(renderer, screenX, screenY);

    // Debug: draw a colored dot if no animation
    if (!currentAnimation) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // Yellow for animals
        SDL_Rect dot = {screenX + 28, screenY + 12, 8, 8};
        SDL_RenderFillRect(renderer, &dot);
    }
}

// ============================================================================
// Guest
// ============================================================================

Guest::Guest() : Entity(EntityType::Guest, 0) {
    Animation* anim = SpriteDatabase::get().getAnimation(EntityType::Guest, 0, AnimState::Idle);
    if (anim) {
        setAnimation(anim);
    }
}

Guest::~Guest() {
}

void Guest::update(float deltaTime) {
    Entity::update(deltaTime);

    // Follow path if we have one
    if (!path.empty() && pathIndex < static_cast<int>(path.size())) {
        followPath(deltaTime);
    }

    // Update needs
    if (animTimer > 10.0f) {
        animTimer = 0;
        if (hunger < 100) hunger += 1;
        if (thirst < 100) thirst += 1;
    }
}

void Guest::setPath(const std::vector<std::pair<int, int>>& newPath) {
    path = newPath;
    pathIndex = 0;
}

void Guest::followPath(float deltaTime) {
    if (pathIndex >= static_cast<int>(path.size())) return;

    int targetX = path[pathIndex].first;
    int targetY = path[pathIndex].second;

    float dx = static_cast<float>(targetX - tileX);
    float dy = static_cast<float>(targetY - tileY);

    if (dx != 0 || dy != 0) {
        // Update direction
        if (std::abs(dx) > std::abs(dy)) {
            direction = dx > 0 ? CompassDirection::E : CompassDirection::W;
        } else {
            direction = dy > 0 ? CompassDirection::S : CompassDirection::N;
        }

        // Move
        float speed = 2.0f * deltaTime;
        if (dx > 0) subX += speed;
        else if (dx < 0) subX -= speed;
        if (dy > 0) subY += speed;
        else if (dy < 0) subY -= speed;

        // Tile transition
        if (subX >= 1.0f) { subX -= 1.0f; tileX++; }
        if (subX < 0.0f) { subX += 1.0f; tileX--; }
        if (subY >= 1.0f) { subY -= 1.0f; tileY++; }
        if (subY < 0.0f) { subY += 1.0f; tileY--; }
    }

    // Check if reached waypoint
    if (tileX == targetX && tileY == targetY) {
        pathIndex++;
    }
}

void Guest::draw(SDL_Renderer* renderer, int screenX, int screenY) {
    Entity::draw(renderer, screenX, screenY);

    // Debug: draw a colored dot if no animation
    if (!currentAnimation) {
        SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255); // Cyan for guests
        SDL_Rect dot = {screenX + 28, screenY + 12, 6, 6};
        SDL_RenderFillRect(renderer, &dot);
    }
}

// ============================================================================
// Staff
// ============================================================================

Staff::Staff() : Entity(EntityType::Staff, 0) {
    Animation* anim = SpriteDatabase::get().getAnimation(EntityType::Staff, 0, AnimState::Idle);
    if (anim) {
        setAnimation(anim);
    }
}

Staff::~Staff() {
}

void Staff::update(float deltaTime) {
    Entity::update(deltaTime);

    // Simple patrol behavior could go here
}

void Staff::draw(SDL_Renderer* renderer, int screenX, int screenY) {
    Entity::draw(renderer, screenX, screenY);

    // Debug: draw a colored dot if no animation
    if (!currentAnimation) {
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255); // Green for staff
        SDL_Rect dot = {screenX + 28, screenY + 12, 6, 6};
        SDL_RenderFillRect(renderer, &dot);
    }
}

// ============================================================================
// SceneryObject
// ============================================================================

SceneryObject::SceneryObject() : Entity(EntityType::Object, 0) {
    objectType = ObjectType::None;
}

SceneryObject::SceneryObject(ObjectType type) : Entity(EntityType::Object, static_cast<uint16_t>(type)) {
    objectType = type;

    Animation* anim = SpriteDatabase::get().getAnimation(EntityType::Object, entityId, AnimState::Idle);
    if (anim) {
        setAnimation(anim);
    }
}

SceneryObject::~SceneryObject() {
}

void SceneryObject::update(float deltaTime) {
    // Static objects typically don't update
    Entity::update(deltaTime);
}

void SceneryObject::draw(SDL_Renderer* renderer, int screenX, int screenY) {
    Entity::draw(renderer, screenX, screenY);

    // Debug: draw a colored dot if no animation
    if (!currentAnimation) {
        SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255); // Grey for objects
        SDL_Rect dot = {screenX + 28, screenY + 12, 6, 6};
        SDL_RenderFillRect(renderer, &dot);
    }
}
