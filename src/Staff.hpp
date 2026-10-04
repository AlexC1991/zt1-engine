#ifndef STAFF_HPP
#define STAFF_HPP

#include <map>
#include <random>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "Animation.hpp"
#include "Fences.hpp"
#include "ZooItems.hpp"
#include "WorldIndex.hpp"
#include "Walkways.hpp"

class ResourceManager;
class WorldMap;
class WorldRenderer;

// The zoo's staff (staff.cfg and the packs' staff1 / staff2: keepers,
// maintenance workers, tour guides, scientists, marine specialists, the DRT
// helicopter). Measured in the original:
// - hiring charges the first month's salary (cPurchaseCost, the
//   helicopter's cMonthlyCost) at once, and every month after that;
// - a new one is "<Zookeeper> <n>", counted per type, a man or a woman,
//   with one of the hair and skin palettes swapped in (colorrep);
// - they walk about the zoo grounds at about half a tile a second, a few
//   walks then standing a moment (bWalkRandomly), "Monitoring zoo";
// - maintenance workers go and fix worn fences and service tank filters
//   (their Job Assignment ticks: empty trash, sweep, fences, filters);
//   keepers, scientists and marine specialists need animals.
class Staff {
public:
  enum class Kind { Keeper, Maint, Guide, Scientist, Trainer, Helicopter };
  struct Type {
    std::string file, key;
    Kind kind = Kind::Keeper;
    int nameId = 0, salary = 0, dutiesText = 0;
    float speed = 0.55f; // tiles a second
    bool sexes[2] = {true, false};
    // Animation name -> art path (no .ani), per sex (0 m, 1 f)
    std::map<std::string, std::string> anims[2];
    std::string fullPal;
    std::vector<std::string> hair, skin;
    std::string listImage[2], infoImage[2];
    // The DRT: hired as its base (scenery/building/helibase.ai, a 10 x 8
    // half-tile building), the helicopter in its slot on top
    std::string baseFile;
    Animation *base = nullptr;
    int footX = 1, footY = 1; // tiles
  };
  enum class Job { None, Fence, Filter, Litter, Visit };
  struct Member {
    int id = 0, type = 0;
    bool female = false;
    int hair = 0, skin = 0;
    std::string name;
    float x = 0, y = 0;       // tiles
    float fx = 0, fy = 1;     // facing (world)
    std::string anim = "idle";
    float animTime = 0;
    int playsLeft = 0;        // a work animation played this many times
    std::vector<std::pair<float, float>> path;
    std::vector<int> pathLayers; // each point's layer: 0 ground, 1 a walkway deck
    int layer = 0;               // where it is now
    size_t pathAt = 0;
    int walksLeft = 0;
    float idleLeft = 0;
    float workCheck = 0;
    int dutyId = 0;           // what it's doing (lang string)
    std::string dutyArg;      // its %s (the exhibit's name)
    bool duties[4] = {true, true, true, true}; // maint: trash, sweep, fences, filters
    std::vector<int> exhibits; // keepers: assigned exhibits
    Job job = Job::None;
    Fences::Edge jobEdge = {false, -1, -1};
    int jobFilter = -1;
    int jobItem = -1;         // litter or dung it's going to
    // Keepers visiting an exhibit: through its gate (access lets it walk
    // in that exhibit), then feeding, raking, out again
    int access = -1, visit = -1, phase = 0;
    float outX = 0, outY = 0, inX = 0, inY = 0;
    // (debug) the A* tiles of its current route and where it's going
    std::vector<std::pair<int, int>> routeTiles;
    int goalX = -1, goalY = -1;
  };

  void loadTypes(ResourceManager *rm);
  void clear();
  const std::vector<Type> &types() const { return this->typeList; }
  int typeOfFile(const std::string &file) const;
  const std::vector<Member> &members() const { return this->list; }
  Member *member(int id);
  const Member *member(int id) const;

  // Where one can be put: zoo ground, not water, an exhibit or a building
  Fences::Fit canPlace(float x, float y, const WorldMap &map, const Fences &fences) const;
  // For a type: a DRT base needs its whole footprint clear; staff a free
  // spot on zoo ground
  Fences::Fit canPlaceType(int type, float x, float y, const WorldMap &map,
                           const Fences &fences) const;
  Fences::Fit canPlaceMember(float x, float y, const WorldMap &map, const Fences &fences) const;
  int hire(int type, float x, float y, const WorldMap &map, const Fences &fences);
  void fire(int id);
  // Picked up (Move Staff Member) and put down again
  void moveTo(int id, float x, float y);

  void update(float seconds, const WorldMap &map, Fences &fences);
  // What lies about (food, dung, litter) for keepers and maintenance
  void setItems(ZooItems *items) { this->items = items; }
  // Where everything is (what blocks a tile, who's about)
  void setIndex(WorldIndex *index) { this->index = index; }
  void setWalkways(const Walkways *walkways) { this->walkways = walkways; }
  // The height it stands at (on a deck: the deck's)
  float heightOf(const Member &m, const WorldMap &map) const;
  // Puts every member in the index (each frame, before moving them)
  void addToIndex(WorldIndex &index) const;
  // Assigning a keeper, scientist or marine specialist to an exhibit: 0
  // done, else the original's message (lang string) why not
  int assign(int id, int exhibit, const Fences &fences);
  // The text for what it's doing ("Going to Exhibit 1")
  std::string dutyText(const Member &m) const;
  // (dev console) sends one to a tile now; false: no way there
  bool walkTo(int id, int x, int y, const WorldMap &map, const Fences &fences);
  // Drawables for the map's objects pass; the selected one shows the arrow
  // over its head, the hovered one lit up
  // (those up on a walkway deck go to 'raised', drawn with the decks)
  void collect(const WorldRenderer &view, const WorldMap &map,
               std::vector<Fences::Drawable> &out,
               std::vector<Fences::Drawable> *raised = nullptr) const;
  // The one drawn at a logical screen point (-1: none)
  int pick(float sx, float sy, const WorldRenderer &view, const WorldMap &map) const;
  int monthlyWages() const;
  // The art an animation of a member draws with (its colours)
  Animation *art(const Member &m, const std::string &name) const;
  Animation *preview(int type) const;

  int selected = -1, hovered = -1, carried = -1;
  // (debug) every route planned goes to the log
  bool logRoutes = false;
  // The one being hired, at the cursor (type < 0: none)
  int previewType = -1;
  float previewX = 0, previewY = 0;
  Fences::Fit previewFit = Fences::Fit::Ok;

private:
  ResourceManager *rm = nullptr;
  ZooItems *items = nullptr;
  WorldIndex *index = nullptr;
  const Walkways *walkways = nullptr;
  bool findLayeredPath(Member &m, int gx, int gy, const WorldMap &map, const Fences &fences,
                       float endX, float endY);
  std::vector<Type> typeList;
  std::vector<Member> list;
  std::map<int, int> counts; // per type: how many were ever hired
  int nextId = 1;
  mutable std::map<std::string, Animation *> artCache;
  Animation *selectArrow = nullptr;
  std::mt19937 rng{2001};

  float groundAt(const WorldMap &map, float x, float y) const;
  bool walkable(int x, int y, const WorldMap &map, const Fences &fences, int access = -1) const;
  bool canStep(int x, int y, int nx, int ny, const WorldMap &map, const Fences &fences,
               int access = -1) const;
  bool keeperWork(Member &m, const WorldMap &map, Fences &fences);
  void visitStep(Member &m, const WorldMap &map, Fences &fences);
  bool needsVisit(const Member &m, int exhibit, const Fences &fences) const;
  void face(Member &m, float x, float y);
  // A path over the tiles from a member to a goal (tile); empty: none
  bool findPath(Member &m, int gx, int gy, const WorldMap &map, const Fences &fences,
                float endX, float endY);
  void wander(Member &m, const WorldMap &map, const Fences &fences);
  bool findWork(Member &m, const WorldMap &map, Fences &fences);
  void play(Member &m, const std::string &anim, int times);
  CompassDirection facing(const WorldRenderer &view, const Member &m) const;
  float animSeconds(const Member &m, const std::string &anim) const;
};

#endif // STAFF_HPP
