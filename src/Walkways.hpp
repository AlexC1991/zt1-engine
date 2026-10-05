#ifndef WALKWAYS_HPP
#define WALKWAYS_HPP

#include <map>
#include <string>
#include <utility>
#include <vector>

class WorldMap;
class Fences;

// ELEVATED PATHS (our addition, ZT2-style; ZT1 has none). A walkway tile
// is a path deck above the ground: flat at a height, or a stair climbing a
// height unit across the tile (the path art's ramp pieces), on posts down
// to the ground. Laid with the Paths tool at a build height (PageUp /
// PageDown): a drag from the ground climbs a unit a tile to that height,
// then runs level; dragging off a deck end with a lower height comes back
// down. Walkers use the stairs to get on and off. Behind the
// 'elevatedpaths' switch.
class Walkways {
  friend class SaveGame; // (saving and loading a game)
public:
  struct Deck {
    std::string type;  // the path type whose art it uses
    int h[4] = {0, 0, 0, 0}; // corner heights (X0Y0, X1Y0, X1Y1, X0Y1)
  };
  // The edge from a tile towards a neighbour: 0 north (-y), 1 east (+x),
  // 2 south (+y), 3 west (-x)
  static void edgeCorners(int dir, int &c0, int &c1);
  static void step(int dir, int &dx, int &dy);

  void clear() { this->decks.clear(); }
  const Deck *at(int x, int y) const;
  const std::map<std::pair<int, int>, Deck> &all() const { return this->decks; }
  void set(int x, int y, const Deck &d) { this->decks[{x, y}] = d; }
  void remove(int x, int y) { this->decks.erase({x, y}); }

  // A deck's height at a point on its tile
  float heightAt(int x, int y, float fx, float fy) const;
  // A deck's two corner heights on one edge (false: no deck)
  bool edgeHeights(int x, int y, int dir, int &a, int &b) const;
  // Two neighbouring decks join (their shared edge at the same heights)
  bool joins(int x, int y, int dir) const;

  // A deck that can be built: above the ground at every corner (never
  // under it), inside the zoo
  bool canBuild(int x, int y, const int h[4], const WorldMap &map, const Fences &fences) const;
  // The corner heights of a tile entered over 'dir' (from the previous
  // tile, the edge at 'from') rising or falling a unit towards 'to'
  static void shape(int dir, int from, int to, int out[4]);

private:
  std::map<std::pair<int, int>, Deck> decks;
};

#endif // WALKWAYS_HPP
