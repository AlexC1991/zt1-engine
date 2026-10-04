#include "WorldIndex.hpp"

#include <algorithm>
#include <cmath>

#include "Fences.hpp"
#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "PlacedObjects.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"

// An object type's footprint (half tiles): scenery/<subclass>/<type>.ai
// (rocks under other, trees under foliage), else its catalogue entry
std::pair<int, int> WorldIndex::footprintOf(const std::string &subClass, const std::string &type,
                                            ResourceManager *rm) {
  std::string key = Utils::string_to_lower(subClass + "/" + type);
  for (size_t i = 0; i < this->footprintKeys.size(); i++)
    if (this->footprintKeys[i] == key)
      return this->footprintCache[i];
  std::pair<int, int> fp{0, 0};
  bool read = false;
  if (rm)
    if (IniReader *ai = rm->getIniReader("scenery/" + Utils::string_to_lower(subClass) + "/" +
                                         Utils::string_to_lower(type) + ".ai")) {
      fp.first = ai->getInt("characteristics/integers", "cfootprintx", 0);
      fp.second = ai->getInt("characteristics/integers", "cfootprinty", 0);
      delete ai;
      read = true;
    }
  if (rm && !read)
    for (const CatalogItem &item : ItemCatalog::get().all()) {
      if (Utils::string_to_lower(item.type) != Utils::string_to_lower(type))
        continue;
      if (IniReader *ai = rm->getIniReader(item.file)) {
        fp.first = ai->getInt("characteristics/integers", "cfootprintx", 0);
        fp.second = ai->getInt("characteristics/integers", "cfootprinty", 0);
        delete ai;
      }
      break;
    }
  this->footprintKeys.push_back(key);
  this->footprintCache.push_back(fp);
  return fp;
}

void WorldIndex::rebuild(const WorldMap &map, const PlacedObjects &objects, const Fences &fences,
                         ResourceManager *rm) {
  this->w = map.getWidth();
  this->h = map.getHeight();
  this->grid.assign(static_cast<size_t>(this->w) * this->h, What::Nothing);
  // The map's objects (rocks, trees, the entrance building)
  for (const PlacedObjects::Object &o : objects.objects()) {
    if (o.fence)
      continue;
    auto [fx, fy] = footprintOf(o.subClass, o.typeName, rm);
    if (fx <= 0 || fy <= 0)
      continue;
    // Facing east or west turns it
    if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
      std::swap(fx, fy);
    float hx = fx / 4.0f, hy = fy / 4.0f; // half the footprint, in tiles
    What what = o.className == "building" ? What::Building : What::Object;
    for (int y = static_cast<int>(std::floor(o.y - hy)); y <= static_cast<int>(o.y + hy); y++)
      for (int x = static_cast<int>(std::floor(o.x - hx)); x <= static_cast<int>(o.x + hx); x++)
        if (x + 0.5f > o.x - hx && x + 0.5f < o.x + hx && y + 0.5f > o.y - hy &&
            y + 0.5f < o.y + hy)
          this->addBlock(x, y, 1, 1, what);
  }
  // Tank filters
  const Fences::FilterType &k = fences.filterType();
  for (const Fences::Filter &f : fences.filters())
    this->addBlock(f.x, f.y, k.footprintX, k.footprintY, What::Filter);
}

void WorldIndex::addBlock(int x0, int y0, int bw, int bh, What what) {
  for (int y = y0; y < y0 + bh; y++)
    for (int x = x0; x < x0 + bw; x++)
      if (x >= 0 && y >= 0 && x < this->w && y < this->h)
        this->grid[static_cast<size_t>(y) * this->w + x] = what;
}

WorldIndex::What WorldIndex::at(int x, int y) const {
  if (x < 0 || y < 0 || x >= this->w || y >= this->h || this->grid.empty())
    return What::Nothing;
  return this->grid[static_cast<size_t>(y) * this->w + x];
}

void WorldIndex::clearMovers() {
  this->movers.clear();
  this->nexts.clear();
  this->heads.assign(static_cast<size_t>(std::max(0, this->w * this->h)), -1);
}

void WorldIndex::addMover(Kind kind, int id, float x, float y) {
  int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  if (tx < 0 || ty < 0 || tx >= this->w || ty >= this->h || this->heads.empty())
    return;
  int i = static_cast<int>(this->movers.size());
  this->movers.push_back({kind, id, x, y});
  size_t cell = static_cast<size_t>(ty) * this->w + tx;
  this->nexts.push_back(this->heads[cell]);
  this->heads[cell] = i;
}

std::vector<WorldIndex::Mover> WorldIndex::near(float x, float y, float radius) const {
  std::vector<Mover> out;
  if (this->heads.empty())
    return out;
  int x0 = static_cast<int>(std::floor(x - radius)), x1 = static_cast<int>(std::floor(x + radius));
  int y0 = static_cast<int>(std::floor(y - radius)), y1 = static_cast<int>(std::floor(y + radius));
  for (int ty = std::max(0, y0); ty <= std::min(this->h - 1, y1); ty++)
    for (int tx = std::max(0, x0); tx <= std::min(this->w - 1, x1); tx++)
      for (int i = this->heads[static_cast<size_t>(ty) * this->w + tx]; i >= 0; i = this->nexts[i]) {
        const Mover &m = this->movers[i];
        if (std::hypot(m.x - x, m.y - y) <= radius)
          out.push_back(m);
      }
  std::sort(out.begin(), out.end(), [&](const Mover &a, const Mover &b) {
    return std::hypot(a.x - x, a.y - y) < std::hypot(b.x - x, b.y - y);
  });
  return out;
}

int WorldIndex::count(int x, int y) const {
  if (x < 0 || y < 0 || x >= this->w || y >= this->h || this->heads.empty())
    return 0;
  int n = 0;
  for (int i = this->heads[static_cast<size_t>(y) * this->w + x]; i >= 0; i = this->nexts[i])
    n++;
  return n;
}
