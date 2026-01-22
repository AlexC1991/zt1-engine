#ifndef SPRITEDATABASE_HPP
#define SPRITEDATABASE_HPP
#include "Animation.hpp"
#include "ResourceManager.hpp"
#include "Enums.hpp"
#include <string>

class SpriteDatabase {
public:
    static SpriteDatabase& get();
    void init(ResourceManager* rm);
    void loadDefinitions();
    Animation* getTerrainSprite(int id);
    Animation* getAnimation(const std::string& name);
    // Restoration of function needed by Entity.cpp
    Animation* getAnimation(EntityType type, int id, AnimState state);

private:
    void loadTerrainSprites();
    ResourceManager* resourceManager;
    Animation* terrainSprites[256];
};
#endif
