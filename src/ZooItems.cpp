#include "ZooItems.hpp"

#include <algorithm>
#include <cmath>

#include "ResourceManager.hpp"
#include "ItemCatalog.hpp"
#include "IniReader.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"

namespace {
constexpr float kSecondsPerHour = 12.0f / 24.0f;
constexpr float kEatenPerHour = 2.5f; // units a fake animal eats an hour
} // namespace

void ZooItems::load(ResourceManager *rm) { this->rm = rm; }

Animation *ZooItems::art(const std::string &path) const {
  auto c = this->artCache.find(path);
  if (c != this->artCache.end())
    return c->second;
  Animation *a = this->rm && this->rm->hasResource(path + ".ani") ? this->rm->getAnimation(path) : nullptr;
  this->artCache[path] = a;
  return a;
}

const ZooItems::Item *ZooItems::item(int id) const {
  for (const Item &i : this->list)
    if (i.id == id)
      return &i;
  return nullptr;
}

int ZooItems::add(Kind kind, int exhibit, float x, float y) {
  Item i;
  i.id = this->nextId++;
  i.kind = kind;
  i.exhibit = exhibit;
  i.x = x;
  i.y = y;
  this->list.push_back(i);
  return i.id;
}

static bool marineFood(const std::string &food) {
  return food == "krill" || food == "fish" || food == "chum" || food == "seagrass" || food == "urchin";
}

std::string ZooItems::foodFile(const std::string &food) {
  for (const CatalogItem &candidate : ItemCatalog::get().all())
    if (candidate.registrySection == "food" && candidate.type == food)
      return candidate.file;
  return (marineFood(food) ? "scenery/food/" : "scenery/other/") + food + ".ai";
}

int ZooItems::addFood(int exhibit, float x, float y, int animals, const std::string &food) {
  int id = add(Kind::Food, exhibit, x, y);
  Item &i = this->list.back();
  // A pile of chow (or Marine Mania's fish, chum...) is cFoodUnits (1000);
  // the fake animals' food as before
  i.units = i.full = (food.size() > 4 && food.compare(food.size() - 4, 4, "chow") == 0) || marineFood(food)
                         ? kChowUnits
                         : std::max(1, animals) * kFoodPerAnimal;
  i.food = food;
  if (this->rm && this->rm->hasResource(foodFile(food))) {
    if (IniReader *ai = this->rm->getIniReader(foodFile(food))) {
      i.units = i.full = std::max(1, ai->getInt("characteristics/integers", "cfoodunits", static_cast<int>(i.full)));
      delete ai;
    }
  }
  return id;
}

void ZooItems::remove(int id) {
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [&](const Item &i) { return i.id == id; }),
                   this->list.end());
}

void ZooItems::removeIn(int exhibit, Kind kind) {
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [&](const Item &i) { return i.exhibit == exhibit && i.kind == kind; }),
                   this->list.end());
}

void ZooItems::removeOn(const std::set<std::pair<int, int>> &tiles) {
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [&](const Item &i) {
                                    return i.kind != Kind::Litter &&
                                           tiles.count({static_cast<int>(std::floor(i.x)), static_cast<int>(std::floor(i.y))});
                                  }),
                   this->list.end());
}

float ZooItems::foodIn(int exhibit) const {
  float n = 0;
  for (const Item &i : this->list)
    if (i.kind == Kind::Food && i.exhibit == exhibit)
      n += i.units;
  return n;
}

float ZooItems::eat(int id, float units) {
  for (size_t k = 0; k < this->list.size(); k++) {
    Item &i = this->list[k];
    if (i.id != id || i.kind != Kind::Food)
      continue;
    float got = std::min(units, std::max(0.0f, i.units));
    i.units -= got;
    if (i.units <= 0)
      this->list.erase(this->list.begin() + k);
    return got;
  }
  return 0;
}

std::vector<int> ZooItems::ofKind(Kind kind, int exhibit) const {
  std::vector<int> out;
  for (const Item &i : this->list)
    if (i.kind == kind && (exhibit == -2 || i.exhibit == exhibit))
      out.push_back(i.id);
  return out;
}

void ZooItems::update(float seconds, const Fences &fences) {
  this->clock += seconds;
  for (Item &i : this->list) {
    if (i.kind != Kind::Food)
      continue;
    const Fences::Exhibit *ex = fences.exhibit(i.exhibit);
    int animals = ex ? ex->testAnimals : 0;
    i.units -= animals * kEatenPerHour * seconds / kSecondsPerHour;
  }
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [](const Item &i) { return i.kind == Kind::Food && i.units <= 0; }),
                   this->list.end());
}

void ZooItems::drawInTank(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map, int tank) const {
  std::vector<Fences::Drawable> out;
  this->onlyTank = tank;
  this->collect(view, map, out);
  this->onlyTank = -2;
  std::stable_sort(out.begin(), out.end(),
                   [](const Fences::Drawable &a, const Fences::Drawable &b) { return a.depth < b.depth; });
  for (const Fences::Drawable &d : out)
    if (d.art)
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tinted ? &d.tint : nullptr, d.frame);
}

void ZooItems::collect(const WorldRenderer &view, const WorldMap &map,
                       std::vector<Fences::Drawable> &out) const {
  SDL_Color white{255, 255, 255, 255};
  for (const Item &i : this->list) {
    // (in a tank: drawn with it, under the water - unless that's now)
    bool inTank = this->tankAt && this->tankAt(i.x, i.y) >= 0;
    if (this->onlyTank != -2 ? (!inTank || this->tankAt(i.x, i.y) != this->onlyTank) : inTank)
      continue;
    Animation *a = nullptr;
    switch (i.kind) {
    case Kind::Food: {
      float share = i.full > 0 ? i.units / i.full : 0;
      std::string size = share > 0.66f ? "full" : share > 0.33f ? "mid" : "small";
      std::string cacheKey = i.food + "/" + size;
      auto slot = this->foodSlots.find(cacheKey);
      if (slot == this->foodSlots.end()) {
        std::string path;
        if (this->rm && this->rm->hasResource(foodFile(i.food))) {
          if (IniReader *ai = this->rm->getIniReader(foodFile(i.food))) {
            path = ai->get("animations", size == "full" ? "full" : size == "mid" ? "half" : "empty");
            delete ai;
          }
        }
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".ani") == 0)
          path.resize(path.size() - 4);
        if (path.find('/') == std::string::npos)
          path = "food/" + i.food + "/" + (path.empty() ? size : path) + "/" + (path.empty() ? size : path);
        slot = this->foodSlots.emplace(cacheKey, path).first;
      }
      a = this->art(slot->second);
      if (!a && size == "mid")
        a = this->art("food/" + i.food + "/med/med");
      break;
    }
    case Kind::Dung:
      a = this->art("objects/poo/idle/idle");
      break;
    case Kind::Litter:
      a = this->art("items/trash/trash");
      break;
    }
    if (!a)
      continue;
    const MapTile *t = map.getTile(static_cast<int>(std::floor(i.x)), static_cast<int>(std::floor(i.y)));
    float h = t ? static_cast<float>(t->height) : 0;
    float sx, sy, depth;
    view.worldToScreenF(i.x, i.y, h, sx, sy, depth);
    Fences::Drawable d{depth - 0.01f, sx, sy, a, CompassDirection::SE, white, false};
    // (its frames play, as the original's do: each item from its own start)
    uint32_t ms = a->frameTimeMs() ? a->frameTimeMs() : 100;
    d.frame = static_cast<int>((this->clock + i.id * 0.37f) * 1000.0f / ms);
    out.push_back(d);
  }
}
