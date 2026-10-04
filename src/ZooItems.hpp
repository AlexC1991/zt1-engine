#ifndef ZOO_ITEMS_HPP
#define ZOO_ITEMS_HPP

#include <map>
#include <random>
#include <string>
#include <vector>

#include "Fences.hpp"

class ResourceManager;
class WorldMap;
class WorldRenderer;

// What lies about the zoo for staff to deal with: food a keeper put down
// (food/<kind>/full, mid, small as it goes), dung in exhibits
// (objects/poo), litter on the grounds (items/trash). For now these come
// from the TEST PANEL (fake stats, no animals or guests): an exhibit's fake
// animal count eats its food down a little every hour, nothing more.
class ZooItems {
public:
  enum class Kind { Food, Dung, Litter };
  struct Item {
    int id = 0;
    Kind kind = Kind::Litter;
    int exhibit = -1;
    float x = 0, y = 0;
    float units = 0, full = 0; // food
    std::string food;          // food/<food>/...
  };

  void load(ResourceManager *rm);
  void clear() { this->list.clear(); }
  const std::vector<Item> &items() const { return this->list; }
  const Item *item(int id) const;

  int add(Kind kind, int exhibit, float x, float y);
  // Food for an exhibit: enough for its animals for a day or so
  int addFood(int exhibit, float x, float y, int animals, const std::string &food);
  void remove(int id);
  void removeIn(int exhibit, Kind kind);

  float foodIn(int exhibit) const;
  std::vector<int> ofKind(Kind kind, int exhibit = -2) const; // -2: anywhere

  // Fake animals eat their exhibit's food (a few units each an hour)
  void update(float seconds, const Fences &fences);
  void collect(const WorldRenderer &view, const WorldMap &map,
               std::vector<Fences::Drawable> &out) const;

  static constexpr float kFoodPerAnimal = 60.0f; // units put down an animal

private:
  ResourceManager *rm = nullptr;
  std::vector<Item> list;
  int nextId = 1;
  mutable std::map<std::string, Animation *> artCache;
  Animation *art(const std::string &path) const;
};

#endif // ZOO_ITEMS_HPP
