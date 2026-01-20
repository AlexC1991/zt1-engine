#ifndef WORLD_HPP
#define WORLD_HPP

#include "ResourceManager.hpp"
#include "Animation.hpp"
#include "ZooReader.hpp"
#include "EntityManager.hpp"
#include "SpriteDatabase.hpp"
#include <SDL2/SDL.h>
#include <string>

class World {
public:
  World(ResourceManager *resourceManager);
  ~World();

  void loadScenario(const std::string &path);
  void loadFreeform(const std::string &path);

  void update(const Uint8 *state, float deltaTime);
  void draw(SDL_Renderer *renderer);

  // Entity access
  EntityManager& getEntityManager() { return entityManager; }

  // Map info
  int getMapWidth() const { return zooReader.getMapWidth(); }
  int getMapHeight() const { return zooReader.getMapHeight(); }

  // Camera control
  void setCameraPosition(int x, int y) { camX = x; camY = y; }
  void getCameraPosition(int& x, int& y) const { x = camX; y = camY; }

private:
  ResourceManager *resourceManager;

  // Camera
  int camX, camY;
  float zoom = 1.0f;

  // Isometric constants
  static const int TILE_WIDTH = 64;
  static const int TILE_HEIGHT = 32;
  static const int ELEVATION_HEIGHT = 8;  // Pixels per elevation level

  // Screen offset for centering
  int startX = 400;
  int startY = 100;

  // Zoo Reader provides dimensions and terrain data
  ZooReader zooReader;

  // Entity management
  EntityManager entityManager;

  // Internal methods
  void drawTerrain(SDL_Renderer* renderer);
  void drawEntities(SDL_Renderer* renderer);

  // Helper: Get remapped terrain ID based on map header
  int getRemappedTerrainId(int terrainId) const;

  // Helper: Convert tile coords to screen coords
  void tileToScreen(int tileX, int tileY, int elevation, int& screenX, int& screenY) const;
};

#endif // WORLD_HPP
