#include "ZooItems.hpp"

#include <algorithm>
#include <cmath>

#include "ResourceManager.hpp"
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

int ZooItems::addFood(int exhibit, float x, float y, int animals, const std::string &food) {
  int id = add(Kind::Food, exhibit, x, y);
  Item &i = this->list.back();
  i.units = i.full = std::max(1, animals) * kFoodPerAnimal;
  i.food = food;
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

float ZooItems::foodIn(int exhibit) const {
  float n = 0;
  for (const Item &i : this->list)
    if (i.kind == Kind::Food && i.exhibit == exhibit)
      n += i.units;
  return n;
}

std::vector<int> ZooItems::ofKind(Kind kind, int exhibit) const {
  std::vector<int> out;
  for (const Item &i : this->list)
    if (i.kind == kind && (exhibit == -2 || i.exhibit == exhibit))
      out.push_back(i.id);
  return out;
}

void ZooItems::update(float seconds, const Fences &fences) {
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

void ZooItems::collect(const WorldRenderer &view, const WorldMap &map,
                       std::vector<Fences::Drawable> &out) const {
  SDL_Color white{255, 255, 255, 255};
  for (const Item &i : this->list) {
    Animation *a = nullptr;
    switch (i.kind) {
    case Kind::Food: {
      float share = i.full > 0 ? i.units / i.full : 0;
      std::string size = share > 0.66f ? "full" : share > 0.33f ? "mid" : "small";
      a = this->art("food/" + i.food + "/" + size + "/" + size);
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
    out.push_back({depth - 0.01f, sx, sy, a, CompassDirection::SE, white, false});
  }
}
