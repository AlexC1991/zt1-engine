#ifndef PLACED_OBJECTS_HPP
#define PLACED_OBJECTS_HPP

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL2/SDL.h>

#include "Fences.hpp"

class Animation;
class ResourceManager;
class WorldMap;
class WorldRenderer;
class ZooReader;

// ============================================================================
// PLACED OBJECTS: what stands on the map - the entrance, fences, rocks,
// trees, buildings - as the map file places them
// ============================================================================
// Every object in the map file has a class, subclass and type (e.g.
// fences/zoowall/f, objects/other/srock1, building/building/fgate), a
// position in 64ths of a tile, a height in 16ths of a height unit and a
// facing (0 N, 2 E, 4 S, 6 W: the world's -y, +x, +y, -x).
//
// Fences stand on tile edges: the facing says which side of their tile
// (a fence at y = 2879 facing 4 is the south edge of tile row 44; at
// x = 1152 facing 6 the west edge of column 18). Their art is
// fences/<subclass>/<type>/idle, one frame per screen side of the tile
// (NE, SE, SW, NW), and idle30p / idle30n where the edge slopes.
// Everything else is objects/<type>/idle, one frame per facing as seen.
// Paths are drawn with the terrain; ambient animals (birds) aren't drawn
// yet.
// ============================================================================
class PlacedObjects {
public:
  struct Object {
    std::string className, subClass, typeName, name;
    float x = 0, y = 0; // tiles
    float z = 0;        // height units
    int facing = 0;     // 0-7, 0 = north (-y), clockwise
    bool fence = false;
    Animation *art = nullptr;
    Animation *slopeUp = nullptr, *slopeDown = nullptr; // fences
  };

  ~PlacedObjects();
  void load(const ZooReader &reader, ResourceManager *rm);
  void clear();
  // Draws every object over the terrain, back to front
  // 'under': whether a world point is under something raised (a walkway
  // deck): what stands there draws first, beneath it
  void draw(SDL_Renderer *renderer, const WorldRenderer &view,
            const WorldMap &map,
            const std::vector<Fences::Drawable> &fences = {},
            const std::function<bool(float x, float y)> &under = nullptr);

  const std::vector<Object> &objects() const { return this->list; }

private:
  std::vector<Object> list;
  ResourceManager *rm = nullptr;
  // Art shared by objects of a type (owned here)
  std::unordered_map<std::string, Animation *> art;
  Animation *artFor(const std::string &path);
};

#endif // PLACED_OBJECTS_HPP
