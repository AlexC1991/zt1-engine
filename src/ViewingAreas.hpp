#ifndef VIEWINGAREAS_HPP
#define VIEWINGAREAS_HPP

#include <array>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Fences;
class Guests;
class PlacedObjects;
class ResourceManager;
class WorldMap;

// An exhibit's viewing areas, as the original makes them (zoo.exe: the
// habitat's list at +0x34, each area an 0x5c-byte object - 0x44ee8e).
// The path tiles round an exhibit's fences are grouped into areas of a few
// tiles each; guests stand in them to look, and a tour guide only takes a
// group to one that guests are at (0x58ae2c, 0x58b0a3).
//
// An area holds (zoo.exe offsets):
// - its viewing tiles, in the order they joined it (+0x40),
// - the exhibit tiles it sees (+4: 0x4f380b),
// - the guests standing on its viewing tiles (+0x54: 0x447957).
class ViewingAreas {
public:
  struct Area {
    int exhibit = -1;
    std::vector<std::pair<int, int>> viewing;
    std::vector<std::pair<int, int>> seen;
    int guests = 0;
  };

  void setResources(ResourceManager *rm) { this->rm = rm; }
  // The scenery that can stand in an area's sight line (0x4f2d44: the
  // tiles' quarter slots)
  void setObjects(const PlacedObjects *objects) { this->objects = objects; }
  // Brought up to date with the map: an exhibit or fence changed, a path
  // taken up - all made again (0x44ba57 for each exhibit); paths only laid -
  // each new one added to the exhibits near it (0x451d6d)
  void refresh(const WorldMap &map, const Fences &fences);
  // The guests standing on each area's viewing tiles counted (0x447957)
  void countGuests(const Guests &guests);
  const std::vector<Area> &areas() const { return this->list; }
  // An exhibit's areas, in the order they were made (the habitat's +0x34)
  std::vector<int> of(int exhibit) const;
  // The area in a tile's slot for a way (0..7: N, NE, E, SE, S, SW, W, NW -
  // zoo.exe 0x43b8a2 reads the grid [0x63805c]+0x28 so), -1 none
  int slotArea(int x, int y, int way) const;
  // Made again from nothing next refresh
  void clear();

private:
  ResourceManager *rm = nullptr;
  const PlacedObjects *objects = nullptr;
  std::vector<Area> list;
  // Each viewing tile's 8 slots (N, NE, E, SE, S, SW, W, NW): the area that
  // sees its exhibit that way (the habitat manager's grid [0x63805c]+0x28,
  // 0x44ebe6), -1 none
  std::map<std::pair<int, int>, std::array<int, 8>> slots;
  // What the areas were made from: each exhibit's id and tiles, the habitat
  // fences, the path tiles
  std::vector<std::pair<int, size_t>> exhibitKey;
  size_t fenceKey = 0;
  std::set<std::pair<int, int>> paths;
  bool built = false;
  std::map<int, bool> habitatFenceType; // fence type -> has [HabitatFence]
  // What stands in the sight lines: each quarter tile (in half tiles) with
  // scenery that blocks the view (cBlocksLOS), its type's cHeight
  std::map<std::pair<int, int>, int> blockers;
  std::map<std::string, std::pair<bool, int>> blockerType; // file -> cBlocksLOS, cHeight
  std::map<std::string, std::pair<int, int>> footprintHalf; // file -> cFootprintX, Y
  // (ours: what the sight lines last saw - the ground, the scenery, the
  // tanks' glass - so a change is looked at again)
  size_t sightKey = 0;

  bool habitatFence(const Fences &fences, int type);
  bool fenceOnSide(const Fences &fences, int x, int y, int dir);
  void build(const WorldMap &map, const Fences &fences, int exhibit);
  void add(const WorldMap &map, const Fences &fences, int exhibit, int x, int y);
  void make(const WorldMap &map, const Fences &fences, int exhibit, int x, int y);
  void addViewing(const WorldMap &map, const Fences &fences, int area, int x, int y);
  void see(const WorldMap &map, const Fences &fences, int area);
  size_t findBlockers(const WorldMap &map, const Fences &fences);
  bool sees(const WorldMap &map, const Fences &fences, std::pair<int, int> from, std::pair<int, int> to) const;
  bool line(const WorldMap &map, const Fences &fences, int x0, int y0, int z0, int x1, int y1, int z1) const;
  bool stepBlocked(const WorldMap &map, const Fences &fences, int tx, int ty, int px, int py, int base) const;
  bool edgeBlocks(const WorldMap &map, const Fences &fences, int tx, int ty, int side, int base) const;
  bool hasArea(int x, int y, int exhibit) const;
};

#endif // VIEWINGAREAS_HPP
