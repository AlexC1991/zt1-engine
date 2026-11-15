#include "ViewingAreas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "Fences.hpp"
#include "Guests.hpp"
#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "OriginalConstants.hpp"
#include "PlacedObjects.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"

namespace {
// The 8 directions, N first, clockwise (zoo.exe table 0x630d44, also the
// neighbour lookup 0x40fa92): x, y steps
constexpr int kDir[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
// How far round an exhibit tile with a habitat fence its viewing tiles are
// looked for (0x44ba57 passes 2 to 0x44e6b1)
constexpr int kViewingReach = 2;
// How far a viewing tile looks for its exhibit in each direction (0x44ebe6)
constexpr int kSlotReach = 3;
// An area's span along the side it grows from, at most (0x44ea19: span +
// 1 < 4)
constexpr int kAreaSpan = 3;
// What an area sees: exhibit tiles within this squared distance of one of
// its viewing tiles (0x4f39a2: 100)
constexpr int kSeeDistance2 = 100;

bool inMap(const WorldMap &map, int x, int y) {
  return x >= 0 && y >= 0 && x < map.getWidth() && y < map.getHeight();
}
} // namespace

void ViewingAreas::clear() {
  this->list.clear();
  this->slots.clear();
  this->exhibitKey.clear();
  this->paths.clear();
  this->fenceKey = 0;
  this->sightKey = 0;
  this->blockers.clear();
  this->built = false;
}

std::vector<int> ViewingAreas::of(int exhibit) const {
  std::vector<int> out;
  for (size_t i = 0; i < this->list.size(); i++)
    if (this->list[i].exhibit == exhibit)
      out.push_back(static_cast<int>(i));
  return out;
}

// A habitat fence: its type's .ai has a [HabitatFence] section (zoo.exe:
// ZTFenceType +0x192 - read live 2026-11-15: 1 on the zoo wall, chain-link
// and its gate, 0 on the white picket; the section is what those files
// differ by - the tank walls have it too; the picket, hedges, ropes and the
// low theme walls don't)
bool ViewingAreas::habitatFence(const Fences &fences, int type) {
  auto it = this->habitatFenceType.find(type);
  if (it != this->habitatFenceType.end())
    return it->second;
  bool has = false;
  if (this->rm && type >= 0 && type < static_cast<int>(fences.types().size())) {
    int size = 0;
    char *text = static_cast<char *>(this->rm->getFileContent(fences.types()[type].file, &size));
    if (text && size > 0) {
      std::string s = Utils::string_to_lower(std::string(text, static_cast<size_t>(size)));
      has = s.find("[habitatfence]") != std::string::npos;
    }
    std::free(text);
  }
  this->habitatFenceType[type] = has;
  return has;
}

// A habitat fence on one side of a tile, 0 N, 2 E, 4 S, 6 W (zoo.exe
// 0x44e6b1: the tile's own edge that way, else the neighbour's opposite
// one - the same edge here)
bool ViewingAreas::fenceOnSide(const Fences &fences, int x, int y, int dir) {
  Fences::Edge e;
  if (dir == 0)
    e = {true, x, y};
  else if (dir == 4)
    e = {true, x, y + 1};
  else if (dir == 6)
    e = {false, x, y};
  else
    e = {false, x + 1, y};
  auto it = fences.pieces().find(e);
  return it != fences.pieces().end() && habitatFence(fences, it->second.type);
}

int ViewingAreas::slotArea(int x, int y, int way) const {
  auto it = this->slots.find({x, y});
  if (it == this->slots.end() || way < 0 || way > 7)
    return -1;
  return it->second[static_cast<size_t>(way)];
}

bool ViewingAreas::hasArea(int x, int y, int exhibit) const {
  auto it = this->slots.find({x, y});
  if (it == this->slots.end())
    return false;
  for (int a : it->second)
    if (a >= 0 && this->list[a].exhibit == exhibit)
      return true;
  return false;
}

// zoo.exe 0x44ebe6: the tile onto the area's viewing tiles; then each of
// the 8 ways, up to 3 tiles out, the first tile of the area's exhibit puts
// the area in this tile's slot for that way
void ViewingAreas::addViewing(const WorldMap &map, const Fences &fences, int area, int x, int y) {
  // (0x40fb8d: tile +0x83 & 0x40 - ours: inside the zoo wall, its meaning
  // not read yet)
  if (!fences.insideZoo(x, y))
    return;
  Area &a = this->list[area];
  a.viewing.push_back({x, y});
  auto made = this->slots.try_emplace({x, y});
  if (made.second)
    made.first->second.fill(-1);
  auto &slot = made.first->second;
  for (int d = 0; d < 8; d++) {
    int tx = x, ty = y;
    for (int k = 0; k < kSlotReach; k++) {
      tx += kDir[d][0];
      ty += kDir[d][1];
      if (!inMap(map, tx, ty))
        break;
      if (fences.exhibitAt(tx, ty) == a.exhibit) {
        slot[d] = area;
        break;
      }
    }
  }
}

// zoo.exe 0x44ee8e: a new area, its first viewing tile; what it sees comes
// once the exhibit's areas are all made (0x4f380b)
void ViewingAreas::make(const WorldMap &map, const Fences &fences, int exhibit, int x, int y) {
  Area a;
  a.exhibit = exhibit;
  this->list.push_back(a);
  addViewing(map, fences, static_cast<int>(this->list.size()) - 1, x, y);
}

// zoo.exe 0x44ea19: a viewing tile into its exhibit's areas. Its path
// neighbours N, E, S, W that are already in an area of this exhibit, in
// that order: the first such area (the neighbour's slots in order) it can
// join without the area's span - along y for a neighbour N or S of it,
// along x for one E or W, taken over the area's tiles and the neighbour's
// own place - going over 3 tiles, it joins; none of them: a new area
void ViewingAreas::add(const WorldMap &map, const Fences &fences, int exhibit, int x, int y) {
  if (!fences.insideZoo(x, y))
    return;
  std::vector<std::pair<int, std::pair<int, int>>> near;
  for (int d = 0; d < 8; d += 2) {
    const int nx = x + kDir[d][0], ny = y + kDir[d][1];
    if (inMap(map, nx, ny) && map.isPath(nx, ny) && hasArea(nx, ny, exhibit))
      near.push_back({d, {nx, ny}});
  }
  if (near.empty()) {
    make(map, fences, exhibit, x, y);
    return;
  }
  bool tooWide = false;
  for (const auto &[d, nb] : near) {
    const auto &slot = this->slots.at(nb);
    for (int s = 0; s < 8; s++) {
      const int a = slot[s];
      if (a < 0 || this->list[a].exhibit != exhibit)
        continue;
      const bool alongY = d == 0 || d == 4;
      int lo = alongY ? nb.second : nb.first, hi = lo;
      for (auto [vx, vy] : this->list[a].viewing) {
        lo = std::min(lo, alongY ? vy : vx);
        hi = std::max(hi, alongY ? vy : vx);
      }
      if (hi - lo + 1 <= kAreaSpan) {
        addViewing(map, fences, a, x, y);
        return;
      }
      tooWide = true;
      break;
    }
  }
  if (tooWide)
    make(map, fences, exhibit, x, y);
}

// zoo.exe 0x4f2fa0: a fence on one side of a tile (0 N, 2 E, 4 S, 6 W - the
// tile's own edge slot that way, ZTMapTile +0x14 + side / 2 x 4: a piece is
// in its owner tile's slot only) hides the view: a glass tank wall
// (cSeeThrough, a 'tankwall') while its tank's water is clean enough is
// seen through - glass of no tank, or a see-through fence that isn't a
// tank wall, too; else its height (a tank wall's own, ZTFence +0x454; the
// type's cHeight +0x64) and how much higher its tile is than the viewing
// tile ('base') over 1
bool ViewingAreas::edgeBlocks(const WorldMap &map, const Fences &fences, int tx, int ty, int side, int base) const {
  if (!inMap(map, tx, ty) || side < 0)
    return false;
  Fences::Edge e;
  if (side == 0)
    e = {true, tx, ty};
  else if (side == 4)
    e = {true, tx, ty + 1};
  else if (side == 6)
    e = {false, tx, ty};
  else
    e = {false, tx + 1, ty};
  auto it = fences.pieces().find(e);
  if (it == fences.pieces().end())
    return false;
  const Fences::Piece &p = it->second;
  if (p.ownerX != tx || p.ownerY != ty || p.facing != side)
    return false;
  if (p.type < 0 || p.type >= static_cast<int>(fences.types().size()))
    return false;
  const FenceType &t = fences.types()[static_cast<size_t>(p.type)];
  // (ours: its tank - the one it walls - for the habitat 0x40f8ac finds)
  const Fences::Exhibit *tank = fences.exhibit(p.tank >= 0 ? p.tank : fences.exhibitAt(tx, ty));
  // (a gate is its own type, the fence's g: its own cSeeThrough, cHeight)
  const bool seeThrough = p.gate ? t.gateSeeThrough : t.seeThrough;
  if (seeThrough) {
    if (!t.tank || !tank)
      return false;
    if (tank->purity > Original::seeThroughPurity)
      return false;
  }
  const int height = t.tank && tank && tank->wallSub != 0 ? tank->wallSub : p.gate ? t.gateHeight : t.height;
  return Original::sightFenceClear < height + map.getTile(tx, ty)->height - base;
}

// zoo.exe 0x4f2fcf: the sight line stepping from tile (px, py) into the
// next, (tx, ty): straight across, a fence on the edge between them (either
// tile's slot) hides it; corner to corner, it's hidden only when the ways
// round by both tiles beside are each fenced (the edges on the way, each
// tile's slot as 0x4f2fcf asks them)
bool ViewingAreas::stepBlocked(const WorldMap &map, const Fences &fences, int tx, int ty, int px, int py, int base) const {
  auto fa = [&](int x, int y, int side) { return edgeBlocks(map, fences, x, y, side, base); };
  if (tx == px || ty == py) {
    int side;
    if (tx == px)
      side = ty <= py ? 0 : 4;
    else
      side = px < tx ? 2 : 6;
    return fa(tx, ty, (side - 4) & 7) || fa(px, py, side);
  }
  // the way from (px, py) to (tx, ty): 1 NE, 3 SE, 5 SW, 7 NW
  const int dir = px < tx ? (ty <= py ? 1 : 3) : (ty <= py ? 7 : 5);
  bool first, second;
  if (dir == 1) {
    const int nx = tx - 1, ny = ty, ex = tx, ey = ty + 1;
    first = fa(tx, ty, 6) || fa(nx, ny, 2) || fa(nx, ny, 4) || fa(px, py, 0);
    second = fa(px, py, 2) || fa(ex, ey, 6) || fa(ex, ey, 0) || fa(tx, ty, 4);
  } else if (dir == 7) {
    const int nx = tx + 1, ny = ty, wx = tx, wy = ty + 1;
    first = fa(tx, ty, 2) || fa(nx, ny, 6) || fa(nx, ny, 4) || fa(px, py, 0);
    second = fa(px, py, 6) || fa(wx, wy, 2) || fa(wx, wy, 0) || fa(tx, ty, 4);
  } else if (dir == 3) {
    const int sx = tx - 1, sy = ty, ex = tx, ey = ty - 1;
    first = fa(tx, ty, 6) || fa(sx, sy, 2) || fa(sx, sy, 0) || fa(px, py, 4);
    second = fa(px, py, 2) || fa(ex, ey, 6) || fa(ex, ey, 4) || fa(tx, ty, 0);
  } else {
    const int sx = tx + 1, sy = ty, wx = tx, wy = ty - 1;
    first = fa(tx, ty, 2) || fa(sx, sy, 6) || fa(sx, sy, 0) || fa(px, py, 4);
    second = fa(px, py, 6) || fa(wx, wy, 2) || fa(wx, wy, 4) || fa(tx, ty, 0);
  }
  return first && second;
}

// zoo.exe 0x4f2d44: the sight line from (x0, y0, z0) to (x1, y1, z1), in
// half tiles and half height units - a 3D Bresenham line along its longest
// axis. At each step, on the tile under it: hidden by ground higher than
// the line (the tile's own height, its (x, y) corner, doubled); by scenery
// in the quarter of the tile it's in that blocks the view (cBlocksLOS)
// when the line is under that quarter's corner (the original's own pick:
// NW, NE for the south-west quarter, SW for the north-east one, SE) plus
// the scenery's cHeight - the corner and cHeight not doubled, as the
// original has it; by a fence between it and the tile before (0x4f2fcf,
// against the first tile's height). Along z the tile is looked up a half
// tile on towards the end in x and y. Seen once it reaches the end.
bool ViewingAreas::line(const WorldMap &map, const Fences &fences, int x0, int y0, int z0, int x1, int y1, int z1) const {
  const int dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
  const int ax = std::abs(dx) * 2, ay = std::abs(dy) * 2, az = std::abs(dz) * 2;
  const int sx = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
  const int sy = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
  const int sz = dz < 0 ? -1 : (dz > 0 ? 1 : 0);
  const MapTile *start = inMap(map, x0 >> 1, y0 >> 1) ? map.getTile(x0 >> 1, y0 >> 1) : nullptr;
  const int base = start ? start->height : 0;
  const int stepX = x0 < x1 ? 1 : 0, stepY = y0 < y1 ? 1 : 0;
  int x = x0, y = y0, z = z0;
  int px = -1, py = -1;
  // One step's tests on tile (tx, ty), quarter (x & 1, y & 1): true hidden
  auto hidden = [&](int tx, int ty) {
    if (!inMap(map, tx, ty))
      return false; // (ours: off the map - the original reads tile 0)
    const MapTile *t = map.getTile(tx, ty);
    if (t->height * 2 > z)
      return true;
    auto b = this->blockers.find({tx * 2 + (x & 1), ty * 2 + (y & 1)});
    if (b != this->blockers.end()) {
      static constexpr int kCorner[2][2] = {{CORNER_X0Y0, CORNER_X1Y0}, {CORNER_X0Y1, CORNER_X1Y1}};
      if (z < t->cornerHeight[kCorner[x & 1][y & 1]] + b->second)
        return true;
    }
    if (px >= 0 && (px != tx || py != ty) && stepBlocked(map, fences, tx, ty, px, py, base))
      return true;
    px = tx;
    py = ty;
    return false;
  };
  if (ax >= std::max(ay, az)) {
    int ey = ay - (ax >> 1), ez = az - (ax >> 1);
    while (true) {
      if (hidden(x >> 1, y >> 1))
        return false;
      if (x == x1)
        return true;
      if (ey >= 0) {
        y += sy;
        ey -= ax;
      }
      if (ez >= 0) {
        z += sz;
        ez -= ax;
      }
      x += sx;
      ey += ay;
      ez += az;
    }
  }
  if (ay >= std::max(ax, az)) {
    int ex = ax - (ay >> 1), ez = az - (ay >> 1);
    while (true) {
      if (hidden(x >> 1, y >> 1))
        return false;
      if (y == y1)
        return true;
      if (ex >= 0) {
        x += sx;
        ex -= ay;
      }
      if (ez >= 0) {
        z += sz;
        ez -= ay;
      }
      y += sy;
      ex += ax;
      ez += az;
    }
  }
  int ex = ax - (az >> 1), ey = ay - (az >> 1);
  while (true) {
    if (hidden((x + stepX) >> 1, (y + stepY) >> 1))
      return false;
    if (z == z1)
      return true;
    if (ex >= 0) {
      x += sx;
      ex -= az;
    }
    if (ey >= 0) {
      y += sy;
      ey -= az;
    }
    z += sz;
    ex += ax;
    ey += ay;
  }
}

// zoo.exe viewingSees 0x4f39a2: a viewing tile sees an exhibit tile within
// squared distance 100 when the sight line between them is clear - from
// each tile's (x, y) corner, doubled, at its height doubled and 2 over it
bool ViewingAreas::sees(const WorldMap &map, const Fences &fences, std::pair<int, int> from, std::pair<int, int> to) const {
  const int dx = to.first - from.first, dy = to.second - from.second;
  if (dx * dx + dy * dy > kSeeDistance2)
    return false;
  if (!inMap(map, from.first, from.second) || !inMap(map, to.first, to.second))
    return false;
  const int h0 = map.getTile(from.first, from.second)->height, h1 = map.getTile(to.first, to.second)->height;
  return line(map, fences, from.first * 2, from.second * 2, h0 * 2 + Original::sightEyeHeight, to.first * 2, to.second * 2,
              h1 * 2 + Original::sightEyeHeight);
}

// zoo.exe 0x4f380b: what an area sees - each of its viewing tiles in turn
// takes the exhibit's tiles it sees (0x4f39a2) that none before it took
// (ours: the exhibit's tiles in our order - the original's list from
// 0x4f3a31)
void ViewingAreas::see(const WorldMap &map, const Fences &fences, int area) {
  Area &a = this->list[area];
  a.seen.clear();
  const Fences::Exhibit *ex = fences.exhibit(a.exhibit);
  if (!ex)
    return;
  std::vector<std::pair<int, int>> left(ex->tiles.begin(), ex->tiles.end());
  for (auto v : a.viewing) {
    std::vector<std::pair<int, int>> still;
    for (auto h : left)
      if (sees(map, fences, v, h))
        a.seen.push_back(h);
      else
        still.push_back(h);
    left.swap(still);
  }
}

// The scenery that can hide an area's view (zoo.exe: the tiles' quarter
// slots, ZTMapTile +4..+0x10, an object in each quarter its footprint
// covers - as WorldIndex::rebuild takes them): those whose type blocks the
// view (cBlocksLOS, scenery type +0x13b) with its cHeight (+0x64). Returns
// (ours) a key of what the sight lines see by: these, the ground, the
// tanks' glass
size_t ViewingAreas::findBlockers(const WorldMap &map, const Fences &fences) {
  this->blockers.clear();
  size_t key = 0;
  auto mix = [&key](size_t v) { key = key * 1000003u + v + 0x9e3779b9u; };
  if (this->objects && this->rm)
    for (const PlacedObjects::Object &o : this->objects->objects()) {
      if (o.fence || o.showHidden)
        continue;
      const std::string file = PlacedObjects::fileOf(o);
      auto it = this->blockerType.find(file);
      if (it == this->blockerType.end()) {
        std::pair<bool, int> v{false, 0};
        int fx = 0, fy = 0;
        // (its file by its kind, else the catalogue's of its type, as
        // WorldIndex::footprintOf finds it; no footprint (sceneryTypeLoad's
        // default, 0): it takes no quarter, as WorldIndex has it)
        auto readFrom = [&](const std::string &from) {
          if (!this->rm->hasResource(from))
            return;
          if (IniReader *ai = this->rm->getIniReader(from)) {
            v.first = ai->getInt("characteristics/integers", "cblockslos", Original::sceneryBlocksLOS) != 0;
            v.second = ai->getInt("characteristics/integers", "cheight", 0);
            fx = ai->getInt("characteristics/integers", "cfootprintx", 0);
            fy = ai->getInt("characteristics/integers", "cfootprinty", 0);
            delete ai;
          }
        };
        readFrom(file);
        if (fx <= 0 || fy <= 0)
          for (const CatalogItem &item : ItemCatalog::get().all())
            if (Utils::string_to_lower(item.type) == Utils::string_to_lower(o.typeName)) {
              if (item.file != file)
                readFrom(item.file);
              break;
            }
        // (the zoo's entrance pieces aren't in the catalogue: their files
        // are scenery/other's, as Fences::isZooGate reads them)
        if ((fx <= 0 || fy <= 0) && o.className == "building")
          readFrom("scenery/other/" + Utils::string_to_lower(o.typeName) + ".ai");
        if (fx <= 0 || fy <= 0)
          v.first = false;
        it = this->blockerType.emplace(file, v).first;
        this->footprintHalf[file] = {fx, fy};
      }
      if (!it->second.first)
        continue;
      auto [fx, fy] = this->footprintHalf[file];
      if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
        std::swap(fx, fy);
      const float hx = fx / 4.0f, hy = fy / 4.0f;
      for (int qy = static_cast<int>(std::floor((o.y - hy) * 2)); qy <= static_cast<int>(std::floor((o.y + hy) * 2)); qy++)
        for (int qx = static_cast<int>(std::floor((o.x - hx) * 2)); qx <= static_cast<int>(std::floor((o.x + hx) * 2)); qx++) {
          const float mx = qx * 0.5f + 0.25f, my = qy * 0.5f + 0.25f;
          if (mx > o.x - hx && mx < o.x + hx && my > o.y - hy && my < o.y + hy) {
            int &h = this->blockers.try_emplace({qx, qy}, it->second.second).first->second;
            h = std::max(h, it->second.second); // (ours: two in a quarter, the taller)
          }
        }
    }
  for (const auto &[q, h] : this->blockers) {
    mix(static_cast<size_t>(q.first * 4099 + q.second));
    mix(static_cast<size_t>(h));
  }
  for (const Fences::Exhibit &e : fences.exhibits())
    if (e.tank) {
      mix(static_cast<size_t>(e.id));
      mix(e.purity > Original::seeThroughPurity ? 1u : 2u);
      mix(static_cast<size_t>(e.wallSub));
    }
  for (int y = 0; y < map.getHeight(); y++)
    for (int x = 0; x < map.getWidth(); x++) {
      const MapTile *t = map.getTile(x, y);
      mix(static_cast<size_t>(t->height * 31 + t->cornerHeight[1] * 7 + t->cornerHeight[2] * 3 + t->cornerHeight[3]));
    }
  return key;
}

// zoo.exe 0x44ba57 -> 0x44e6b1: an exhibit's areas made. Its tiles row by
// row (the habitat's tile list, y then x - matched against the original's
// areas round a paddock 2026-11-15); each with a habitat fence on one of
// its four sides adds the tiles within 2 of it that aren't the exhibit's,
// x outer, y inner, each once, in the order found; then each of those with
// a path becomes a viewing tile in that order (0x44ea19)
void ViewingAreas::build(const WorldMap &map, const Fences &fences, int exhibit) {
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex)
    return;
  std::vector<std::pair<int, int>> tiles(ex->tiles.begin(), ex->tiles.end());
  std::stable_sort(tiles.begin(), tiles.end(), [](const auto &a, const auto &b) {
    return a.second != b.second ? a.second < b.second : a.first < b.first;
  });
  std::vector<std::pair<int, int>> found;
  std::set<std::pair<int, int>> have;
  for (auto [x, y] : tiles) {
    bool fenced = false;
    for (int d = 0; d < 8 && !fenced; d += 2)
      fenced = fenceOnSide(fences, x, y, d);
    if (!fenced)
      continue;
    for (int cx = x - kViewingReach; cx <= x + kViewingReach; cx++)
      for (int cy = y - kViewingReach; cy <= y + kViewingReach; cy++)
        if (inMap(map, cx, cy) && fences.exhibitAt(cx, cy) != exhibit && have.insert({cx, cy}).second)
          found.push_back({cx, cy});
  }
  const size_t first = this->list.size();
  for (auto [x, y] : found)
    if (fences.exhibitAt(x, y) != exhibit && map.isPath(x, y))
      add(map, fences, exhibit, x, y);
  for (size_t i = first; i < this->list.size(); i++)
    see(map, fences, static_cast<int>(i));
}

void ViewingAreas::refresh(const WorldMap &map, const Fences &fences) {
  std::vector<std::pair<int, size_t>> key;
  for (const Fences::Exhibit &e : fences.exhibits())
    if (!e.pocket)
      key.push_back({e.id, e.tiles.size()});
  size_t fenceKey = 0;
  for (const auto &kv : fences.pieces())
    fenceKey = fenceKey * 1000003u + static_cast<size_t>(kv.first.x * 131 + kv.first.y * 7 + kv.first.alongX) +
               static_cast<size_t>(kv.second.type) * 17u;
  std::set<std::pair<int, int>> paths;
  for (int y = 0; y < map.getHeight(); y++)
    for (int x = 0; x < map.getWidth(); x++)
      if (map.isPath(x, y))
        paths.insert({x, y});
  bool removed = false;
  for (const auto &p : this->paths)
    if (!paths.count(p)) {
      removed = true;
      break;
    }
  const size_t sight = findBlockers(map, fences);
  if (!this->built || key != this->exhibitKey || fenceKey != this->fenceKey || removed) {
    // (ours: anything but a path laid makes them all again, the exhibits in
    // the order they were made)
    this->list.clear();
    this->slots.clear();
    for (const Fences::Exhibit &e : fences.exhibits())
      if (!e.pocket)
        build(map, fences, e.id);
  } else {
    // zoo.exe 0x451c0b -> 0x451d6d: a path laid - each exhibit with a tile
    // within 2 of it (x outer, y inner, each once) takes it as a viewing
    // tile (0x44ea19); ours: new tiles row by row
    std::vector<std::pair<int, int>> laid;
    for (const auto &p : paths)
      if (!this->paths.count(p))
        laid.push_back(p);
    std::stable_sort(laid.begin(), laid.end(), [](const auto &a, const auto &b) {
      return a.second != b.second ? a.second < b.second : a.first < b.first;
    });
    for (auto [x, y] : laid) {
      std::vector<int> near;
      for (int cx = x - 2; cx <= x + 2; cx++)
        for (int cy = y - 2; cy <= y + 2; cy++) {
          const int ex = inMap(map, cx, cy) ? fences.exhibitAt(cx, cy) : -1;
          if (ex >= 0 && std::find(near.begin(), near.end(), ex) == near.end())
            near.push_back(ex);
        }
      for (int ex : near) {
        const size_t before = this->list.size();
        const std::vector<int> mine = of(ex);
        add(map, fences, ex, x, y);
        for (int a : mine)
          see(map, fences, a);
        for (size_t i = before; i < this->list.size(); i++)
          see(map, fences, static_cast<int>(i));
      }
    }
  }
  if (this->built && sight != this->sightKey) {
    // (ours: the ground, scenery or a tank's glass changed - every area's
    // sight looked at again; the original marks an area, +0x21, for
    // 0x447a00 to look at again)
    for (size_t i = 0; i < this->list.size(); i++)
      see(map, fences, static_cast<int>(i));
  }
  this->sightKey = sight;
  this->exhibitKey = key;
  this->fenceKey = fenceKey;
  this->paths = std::move(paths);
  this->built = true;
}

// zoo.exe 0x447957: an area's guests - those standing on its viewing tiles
void ViewingAreas::countGuests(const Guests &guests) {
  std::map<std::pair<int, int>, int> on;
  for (const Guests::Guest &g : guests.guests())
    if (!g.hidden && g.building < 0)
      on[{static_cast<int>(std::floor(g.x)), static_cast<int>(std::floor(g.y))}]++;
  for (Area &a : this->list) {
    a.guests = 0;
    for (const auto &t : a.viewing) {
      auto it = on.find(t);
      if (it != on.end())
        a.guests += it->second;
    }
  }
}
