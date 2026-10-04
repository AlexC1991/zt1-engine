#ifndef WORLD_INDEX_HPP
#define WORLD_INDEX_HPP

#include <string>
#include <vector>

class Fences;
class PlacedObjects;
class ResourceManager;
class WorldMap;

// Where everything is, tile by tile, for anything that needs to know: what
// stands on a tile (a rock, a tree, a building, a tank filter, a DRT base)
// and who is on it (staff now; guests and animals later). Walkers route
// round what stands, step round each other, and ask who's about.
class WorldIndex {
public:
  enum class What : unsigned char { Nothing, Object, Building, Filter, Base };
  enum class Kind : unsigned char { Staff, Guest, Animal };
  struct Mover {
    Kind kind;
    int id;
    float x, y;
  };

  // What stands where: the map's objects by their footprint (cFootprintX /
  // Y, half tiles; a tile is blocked when the footprint covers its middle),
  // the entrance, tank filters, DRT bases (given as rectangles)
  void rebuild(const WorldMap &map, const PlacedObjects &objects, const Fences &fences,
               ResourceManager *rm);
  void addBlock(int x0, int y0, int w, int h, What what);

  What at(int x, int y) const;
  bool blocked(int x, int y) const { return at(x, y) != What::Nothing; }

  // Who's where: set every frame
  void clearMovers();
  void addMover(Kind kind, int id, float x, float y);
  // Everyone within a radius (tiles) of a point, nearest first
  std::vector<Mover> near(float x, float y, float radius) const;
  // How many are on a tile
  int count(int x, int y) const;

  int width() const { return this->w; }
  int height() const { return this->h; }

private:
  int w = 0, h = 0;
  std::vector<What> grid;
  // Movers bucketed by tile (index -> first in 'movers', linked)
  std::vector<Mover> movers;
  std::vector<int> heads, nexts;
  std::vector<std::pair<int, int>> footprintCache; // per object type, half tiles
  std::vector<std::string> footprintKeys;
  std::pair<int, int> footprintOf(const std::string &subClass, const std::string &type,
                                  ResourceManager *rm);
};

#endif // WORLD_INDEX_HPP
