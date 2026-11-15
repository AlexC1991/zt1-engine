#include "ViewingAreas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "Fences.hpp"
#include "Guests.hpp"
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

// zoo.exe 0x4f380b: what an area sees - its exhibit's tiles within 10 of
// one of its viewing tiles (0x4f39a2)
// (ours: 0x4f39a2's line of sight, 0x4f2d44 - the ground's height and
// walls that block the view between - isn't ported yet: on rising ground
// the original sees fewer)
void ViewingAreas::see(const Fences &fences, int area) {
  Area &a = this->list[area];
  a.seen.clear();
  const Fences::Exhibit *ex = fences.exhibit(a.exhibit);
  if (!ex)
    return;
  for (auto [hx, hy] : ex->tiles)
    for (auto [vx, vy] : a.viewing) {
      const int dx = hx - vx, dy = hy - vy;
      if (dx * dx + dy * dy <= kSeeDistance2) {
        a.seen.push_back({hx, hy});
        break;
      }
    }
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
    see(fences, static_cast<int>(i));
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
          see(fences, a);
        for (size_t i = before; i < this->list.size(); i++)
          see(fences, static_cast<int>(i));
      }
    }
  }
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
