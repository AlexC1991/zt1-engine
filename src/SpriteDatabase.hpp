#ifndef SPRITE_DATABASE_HPP
#define SPRITE_DATABASE_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include "Animation.hpp"
#include "ResourceManager.hpp"
#include "CompassDirection.hpp"

// Entity types in Zoo Tycoon
enum class EntityType {
    None = 0,
    Animal,
    Guest,
    Staff,
    Object,      // Scenery, decorations
    Building,    // Structures (zoo entrance, restaurants, etc.)
    Fence,
    Path,
    Foliage      // Trees, plants
};

// Animal species IDs (based on Zoo Tycoon internal IDs)
// These map to animation paths like "animals/Lion/", "animals/Bear/", etc.
enum class AnimalSpecies : uint16_t {
    None = 0,
    // Base game animals
    Lion = 1,
    Tiger = 2,
    Bear_Grizzly = 3,
    Bear_Polar = 4,
    Elephant_African = 5,
    Elephant_Asian = 6,
    Giraffe = 7,
    Zebra = 8,
    Hippo = 9,
    Rhino_Black = 10,
    Rhino_White = 11,
    Gorilla = 12,
    Chimpanzee = 13,
    Orangutan = 14,
    Leopard = 15,
    Cheetah = 16,
    Jaguar = 17,
    Moose = 18,
    Bison = 19,
    Okapi = 20,
    Ostrich = 21,
    Flamingo = 22,
    Crocodile = 23,
    Gazelle = 24,
    Ibex = 25,
    Gemsbok = 26,
    Wildebeest = 27,
    Camel = 28,
    Kangaroo = 29,
    Penguin_Emperor = 30,
    // Add more as needed...
    MAX_SPECIES = 256
};

// Object types for scenery/buildings
enum class ObjectType : uint16_t {
    None = 0,
    // Scenery
    Tree_Oak = 1,
    Tree_Pine = 2,
    Rock_Small = 3,
    Rock_Large = 4,
    Bench = 5,
    Trash_Can = 6,
    Lamp_Post = 7,
    Fountain = 8,
    Statue = 9,
    // Buildings
    Zoo_Entrance = 100,
    Restaurant = 101,
    Restroom = 102,
    Gift_Shop = 103,
    // Fences
    Fence_Chain = 200,
    Fence_Iron = 201,
    Fence_Wood = 202,
    // Add more as needed...
    MAX_OBJECT = 512
};

// Animation state for entities
enum class AnimState {
    Idle,
    Walking,
    Running,
    Eating,
    Drinking,
    Sleeping,
    Playing,
    Sick,
    Dead,
    // Guest-specific
    Watching,
    Buying,
    // Staff-specific
    Working,
    Cleaning
};

// Sprite definition with all directional animations
struct SpriteDefinition {
    std::string basePath;           // e.g., "animals/Lion"
    EntityType type;
    uint16_t entityId;              // Species ID or Object ID

    // Animations keyed by state
    std::unordered_map<AnimState, Animation*> animations;

    // Default animation (usually idle)
    Animation* defaultAnim = nullptr;

    bool isLoaded = false;
};

class SpriteDatabase {
public:
    static SpriteDatabase& get() {
        static SpriteDatabase instance;
        return instance;
    }

    // Initialize with resource manager
    void init(ResourceManager* rm);

    // Load sprite definitions from configuration
    void loadDefinitions();

    // Get sprite for an entity
    SpriteDefinition* getAnimalSprite(AnimalSpecies species);
    SpriteDefinition* getObjectSprite(ObjectType objType);
    SpriteDefinition* getGuestSprite();
    SpriteDefinition* getStaffSprite();

    // Get animation for entity with specific state
    Animation* getAnimation(EntityType type, uint16_t id, AnimState state = AnimState::Idle);

    // Terrain sprites (moved from World for centralization)
    Animation* getTerrainSprite(int terrainId);
    void loadTerrainSprites();

    // Report loaded sprites
    void logLoadedSprites() const;

    // Cleanup
    void cleanup();

private:
    SpriteDatabase() = default;
    ~SpriteDatabase();

    SpriteDatabase(const SpriteDatabase&) = delete;
    SpriteDatabase& operator=(const SpriteDatabase&) = delete;

    ResourceManager* resourceManager = nullptr;

    // Sprite storage
    std::unordered_map<uint16_t, SpriteDefinition> animalSprites;
    std::unordered_map<uint16_t, SpriteDefinition> objectSprites;
    SpriteDefinition guestSprite;
    SpriteDefinition staffSprite;

    // Terrain sprites (20 types)
    Animation* terrainSprites[20] = {nullptr};

    // Helper to load a sprite definition
    bool loadSpriteDefinition(SpriteDefinition& def);

    // Animal path mappings
    static const char* getAnimalPath(AnimalSpecies species);
    static const char* getObjectPath(ObjectType objType);
};

#endif // SPRITE_DATABASE_HPP
