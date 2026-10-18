#ifndef ZOO_ITEMS_HPP
#define ZOO_ITEMS_HPP

#include <map>
#include <random>
#include <set>
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
  friend class SaveGame; // (saving and loading a game)
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
  // A keeper food's .ai (scenery/other/herbchow.ai; Marine Mania's are under
  // scenery/food/)
  static std::string foodFile(const std::string &food);
  // The tank a spot is in (-1: none): food in a tank is drawn with it
  std::function<int(float x, float y)> tankAt;
  mutable int onlyTank = -2;
  void drawInTank(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map, int tank) const;
  // A keeper's pile of chow (scenery/other/<food>.ai: cFoodUnits 1000)
  static constexpr float kChowUnits = 1000.0f;
  void remove(int id);
  void removeIn(int exhibit, Kind kind);
  // What lay on these tiles (a tank taken away: its food and dung go with
  // its water, as the original removes the objects in it - 0x458c2b)
  void removeOn(const std::set<std::pair<int, int>> &tiles);

  float foodIn(int exhibit) const;
  // An animal eats from a pile of food: what it got (the pile goes when
  // it's eaten up)
  float eat(int id, float units);
  std::vector<int> ofKind(Kind kind, int exhibit = -2) const; // -2: anywhere

  // Fake animals eat their exhibit's food (a few units each an hour)
  void update(float seconds, const Fences &fences);
  // Game time played (its art plays by it: the fish food's swirl, a pile
  // of chow's flies - still while paused)
  float clock = 0;
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
