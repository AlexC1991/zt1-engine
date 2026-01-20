#ifndef WORLD_HPP
#define WORLD_HPP

#include "ResourceManager.hpp"
#include "Animation.hpp"
#include "ZooReader.hpp"
#include <SDL2/SDL.h>
#include <string>

class World {
public:
  World(ResourceManager *resourceManager);
  ~World();

  void loadScenario(const std::string &path);
  void loadFreeform(const std::string &path);

  void update(const Uint8 *state); // taking key state for panning
  void draw(SDL_Renderer *renderer);

private:
  ResourceManager *resourceManager;
  SDL_Texture *grassTexture;
  Animation* terrainSprites[20]; // Map terrain ID to sprite
  
  // Camera
  int camX, camY;
  float zoom = 1.0f;

  // Zoo Reader provides dimensions and data
  ZooReader zooReader;
};

#endif // WORLD_HPP
