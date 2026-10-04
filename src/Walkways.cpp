#include "Walkways.hpp"

#include <algorithm>

#include "Fences.hpp"
#include "WorldMap.hpp"

void Walkways::edgeCorners(int dir, int &c0, int &c1) {
  switch (dir & 3) {
  case 0: c0 = CORNER_X0Y0; c1 = CORNER_X1Y0; break; // north
  case 1: c0 = CORNER_X1Y0; c1 = CORNER_X1Y1; break; // east
  case 2: c0 = CORNER_X0Y1; c1 = CORNER_X1Y1; break; // south
  default: c0 = CORNER_X0Y0; c1 = CORNER_X0Y1; break; // west
  }
}

void Walkways::step(int dir, int &dx, int &dy) {
  static const int sx[4] = {0, 1, 0, -1}, sy[4] = {-1, 0, 1, 0};
  dx = sx[dir & 3];
  dy = sy[dir & 3];
}

const Walkways::Deck *Walkways::at(int x, int y) const {
  auto it = this->decks.find({x, y});
  return it == this->decks.end() ? nullptr : &it->second;
}

float Walkways::heightAt(int x, int y, float fx, float fy) const {
  const Deck *d = this->at(x, y);
  if (!d)
    return 0;
  float top = d->h[CORNER_X0Y0] * (1 - fx) + d->h[CORNER_X1Y0] * fx;
  float bottom = d->h[CORNER_X0Y1] * (1 - fx) + d->h[CORNER_X1Y1] * fx;
  return top * (1 - fy) + bottom * fy;
}

bool Walkways::edgeHeights(int x, int y, int dir, int &a, int &b) const {
  const Deck *d = this->at(x, y);
  if (!d)
    return false;
  int c0, c1;
  edgeCorners(dir, c0, c1);
  a = d->h[c0];
  b = d->h[c1];
  return true;
}

bool Walkways::joins(int x, int y, int dir) const {
  int dx, dy;
  step(dir, dx, dy);
  int a0, a1, b0, b1;
  return this->edgeHeights(x, y, dir, a0, a1) &&
         this->edgeHeights(x + dx, y + dy, (dir + 2) & 3, b0, b1) && a0 == b0 && a1 == b1;
}

bool Walkways::canBuild(int x, int y, const int h[4], const WorldMap &map,
                        const Fences &fences) const {
  const MapTile *t = map.getTile(x, y);
  if (!t)
    return false;
  if (!fences.insideZoo(x, y))
    return false;
  bool above = false;
  for (int c = 0; c < 4; c++) {
    if (h[c] < t->cornerHeight[c])
      return false; // never under the ground
    above = above || h[c] > t->cornerHeight[c];
  }
  // A unit change at most across the tile (the ramp art)
  int lo = *std::min_element(h, h + 4), hi = *std::max_element(h, h + 4);
  if (!above || hi - lo > 1)
    return false;
  // Over a fence or wall on its edges only clear of it (its height and a
  // unit), not resting on it
  const Fences::Edge edges[4] = {{true, x, y}, {false, x + 1, y}, {true, x, y + 1}, {false, x, y}};
  for (int dir = 0; dir < 4; dir++) {
    const Fences::Piece *p = fences.at(edges[dir]);
    if (!p)
      continue;
    int c0, c1;
    edgeCorners(dir, c0, c1);
    int groundTop = std::max(t->cornerHeight[c0], t->cornerHeight[c1]);
    int clearance = std::max(2, fences.types()[p->type].height) + 1;
    if (std::min(h[c0], h[c1]) < groundTop + clearance)
      return false;
  }
  return true;
}

void Walkways::shape(int dir, int from, int to, int out[4]) {
  // The entry edge (back towards the previous tile) at 'from', the far edge
  // at 'to'
  int e0, e1, f0, f1;
  edgeCorners((dir + 2) & 3, e0, e1);
  edgeCorners(dir, f0, f1);
  out[e0] = out[e1] = from;
  out[f0] = out[f1] = to;
}
