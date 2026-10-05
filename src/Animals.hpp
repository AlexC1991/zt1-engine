#ifndef ANIMALS_HPP
#define ANIMALS_HPP

#include <functional>
#include <map>
#include <random>
#include <string>
#include <set>
#include <vector>

#include <SDL2/SDL.h>

#include "Animation.hpp"
#include "Fences.hpp"
#include "WorldIndex.hpp"
#include "ZooItems.hpp"

class PlacedObjects;

class ResourceManager;
class WorldMap;
class WorldRenderer;

// The zoo's animals (animals/<type>.ai, listed in animals.cfg and the
// packs' animal files). Zoo animals for now: the dinosaurs and the marine
// animals have their own ways and come later. Measured in the original:
// - adopting one: pick it, then the male or female button; over an
//   exhibit's ground it shows green with "-$<cost>", anywhere else red
//   with "$0" ("Animals can only be placed in exhibits, which are areas
//   surrounded by exhibit fencing." on a click); the tool stays picked
//   until a right click;
// - named "<Plains Zebra> <n>", counted per type;
// - Animal Information (ui/infoanm.lyt): happiness, hunger, health and
//   exhibit suitability bars; gender, last ate, last slept; Move, Exhibit
//   Information, Sell ("Are you sure you want to sell Plains Zebra 1?
//   Refund: $528" for an $800 zebra at about 66 happiness), Zookeeper
//   Recommendations.
// What they do comes from their behaviour sets ([m\BehaviorSet\b...]):
// how happy they are picks one ([AmbientAnims]), and its steps play their
// animations and walk them about the exhibit.
class Animals {
  friend class SaveGame; // (saving and loading a game)
public:
  // One step of a behaviour set: f = fPlay(stand)
  struct Step {
    std::string fn;
    std::vector<std::string> args;
  };
  struct Ambient {
    int lo = 0, hi = 0;
    std::string set;
  };
  struct Type {
    std::string file, key;
    int nameId = 0, cost = 0, expansion = 0;
    // Speeds (cSlowRate, cMediumRate, cFastRate): staff walk at their
    // cSlowRate / 60 tiles a second (a keeper's 33: the 0.55 measured)
    float slow = 0.5f, medium = 0.8f, fast = 1.1f;
    int initialHappiness = 50;
    int hungerThreshold = 50, hungerIncrement = 10, neededFood = 100, foodEaten = 20;
    int foodUnitValue = 1; // cFoodUnitValue: hunger a unit of chow takes away
    int noFoodChange = -50;
    int energyIncrement = 10, maxEnergy = 100;
    bool swims = false;
    // Water (zoo.exe 0x438f6c, every cWaterCheck ticks): a cEnterWaterChance
    // in 100 to swim with more than cWaterNeeded water tiles in its exhibit,
    // cEnterLandChance to come out with more than cLandNeeded land tiles;
    // now and then a drink (cDrinkWaterChance against cChaseAnimalChance,
    // every cBoredCheck ticks) at the nearest fresh water
    int waterNeeded = 0, landNeeded = 0, enterWaterChance = 0, enterLandChance = 0;
    int drinkWaterChance = 0, chaseAnimalChance = 0;
    // How it rates its exhibit (see Animals.cpp, from zoo.exe): the terrain
    // it wants (percent of the exhibit, or a flat penalty when negative),
    // what it thinks of objects and of the other animals, its trees, rocks
    // and hills
    int genus = 0, family = 0;
    int compatTerrain[18] = {};
    std::map<int, int> suitable, compatible;
    int treePref = 0, rockPref = 0, elevationPref = 0;
    int habitatPreference = 50, pctHabitat = 10;
    int happyHabitatChange = 0, angryHabitatChange = 0, veryAngryHabitatChange = 0;
    int captivity = 0;
    int numberMin = 1, numberMax = 100, numberMinChange = 0, numberMaxChange = 0;
    int habitatSize = 100, animalDensity = 0, allCrowdedChange = 0, otherAnimalAngryChange = 0;
    bool needShelter = false, needToys = false;
    // [Sounds]: name -> its file and attenuation (placesound, pickupsound,
    // and those fPlayWithSound plays)
    std::map<std::string, std::pair<std::string, int>> sounds;
    int facesYOffset = 0; // cFacesYOffset
    int attractiveness = 0; // cAttractiveness (guests come to see it)
    // Getting out (cIsJumper, cIsClimber, cBashStrength, cCrushesFences)
    bool jumper = false, climber = false;
    int bash = 0, crush = 0;
    bool eatsPeople = false; // cPrey lists people (9503): guests run from it
    // cPrey: the cNameIDs of the animals it hunts, per sex (m, f, y: a
    // lion's list is the lioness's alone; one without its own has the
    // male's)
    std::set<int> preyIds[3];
    // What its keeper puts down (cKeeperFoodType: food.cfg's order,
    // herbchow, carnchow, fruichow, bambchow, graschow, fishchow)
    int keeperFood = 0;
    // Care (zoo.exe defaults where a file leaves them out)
    int maxHits = 500, pctHits = 20, sickChange = -20, otherSickChange = -10, sickChance = 10;
    int hungryHealthChange = 5, sickTime = 5, keeperArrivesChange = 25;
    int dirtyIncrement = 15, dirtyThreshold = 70, energyThreshold = 70;
    int timeDeath = 5001, deathChance = 100;
    int reproductionChance = 0, reproductionInterval = 0, happyReproduceThreshold = 100,
        offspring = 1, babyToAdult = 0;
    bool smallPoo = false;
    int buildingUseChance = 0;
    // Per subtype (0 m, 1 f, 2 y): animation name -> art path (no .ani)
    std::map<std::string, std::string> anims[3];
    std::map<std::string, std::vector<Step>> sets[3];
    std::vector<Ambient> ambient;
    std::vector<Ambient> ambientWater; // [AmbientAnimsWater]: in the water
    std::string listImage;
  };
  struct Member {
    int id = 0, type = 0;
    bool female = false, baby = false;
    std::string name;
    int exhibit = -1;
    float x = 0, y = 0;   // tiles
    float fx = 0, fy = 1; // facing (world)
    // What it's playing: once, looping for a time, there and back, or
    // backwards
    enum class Mode { Once, Loop, PingPong, Reverse } mode = Mode::Loop;
    std::string anim = "stand";
    float animTime = 0, playFor = 0;
    // The behaviour sets it's in (a set can play another), and where
    std::vector<std::pair<std::string, size_t>> stack;
    bool stepping = false; // a step is under way (playing or walking)
    // Walking: the points left, the animation and the speed
    std::vector<std::pair<float, float>> path;
    size_t pathAt = 0;
    std::string moveAnim = "walk";
    float speed = 0.5f;
    int foodItem = -1; // the food it's gone to eat
    // Happiness, -100 to 100 (the panel shows it as 0-100); changes wait
    // for the next tick
    float happiness = 50, pending = 0;
    // Hunger (cHungerIncrement more at each hunger check; hungry from
    // cHungerThreshold; the Hunger bar shows 100 less it), health, energy
    // (it sleeps when out)
    float hunger = 0, food = 100, health = 100, energy = 100;
    double lastAte = 0, lastSlept = 0; // sim seconds
    // Its status ticks (a second each) and the checks' countdowns
    bool escaped = false; // out of any exhibit
    // Care: tiredness (energy, 0 rested), the dung meter, sick (health at
    // nothing), a first look for food that found none, old age (ticks
    // left), babies (ready counter, growing up)
    float dirty = 0;
    bool dying = false;
    int dieAfter = 0; // its bDie playing (gone when it's done)
    bool sick = false, noFoodSeen = false, sleeping = false;
    int healthCheck = 0, energyCheck = 0, reproductionCheck = 0, starve = 0;
    int life = 5000, ready = 0, grow = 0, notHappyCheck = 0;
    // A shelter or toy it's off to (object id) or in; hidden inside
    int building = -1, buildingCheck = 0;
    // In the water (swimmers: where it wanders), the water and drink checks
    bool waterMode = false;
    int waterCheck = -1, boredCheck = -1;
    float drinkX = -1, drinkY = -1; // the water it's going to drink at
    // Hunting a guest (loose predators, cPrey 9503): who, since when, the
    // check's countdown; mauling (out of sight: the guest's animation shows
    // both) and resting after
    int prey = -1, preyCheck = 0;
    double chaseStart = 0;
    float maulFor = 0;
    // In its exhibit (zoo.exe 0x43954c, 0x4369b9, 0x4a6043): the animal it's
    // chasing (a prey it lists, or one of its own kind at play - tag), since
    // when; the one it's running from; caught (frozen under the dust ball,
    // by whom); the one it's caught and the dust ball's time left
    int huntAnimal = -1, fleeFrom = -1, caughtBy = -1, victim = -1, huntCheck = 0;
    bool tag = false;
    double huntStart = 0;
    float killIn = 0;
    bool inside = false;
    float insideFor = 0;
    // Darted by a keeper (asleep), crated (waiting to be put in an
    // exhibit), the keeper after it
    bool tranquilised = false, boxed = false;
    float boxTimer = 0;
    int claimedBy = -1;
    // Breaking out: the fence it's going over, and whether it breaks it
    Fences::Edge crossing = {false, -1, -1};
    int crossingFromX = 0, crossingFromY = 0; // the tile it's crossing from
    bool breaks = false;
    // How it goes over (zoo.exe 0x6138c5): "jump_high" or "climb_up" for
    // the path's step over the fence (crossAt), played once across it; ""
    // walks (or bashes) through
    std::string crossAnim;
    int crossAt = -1;
    // Smiles (true) and frowns floating over it, and how long each has been
    // up (ms)
    std::vector<std::pair<bool, float>> faces;
    float tickMs = 0;
    int habitatCheck = 0, captivityCheck = 0, socialCheck = 0, otherCheck = 0, hungerCheck = 0;
  };
  // An exhibit as one kind of animal rates it (worked out every 7 seconds,
  // or as soon as something in it changes)
  struct Rating {
    float terrain = 0, objects = 0, trees = 0, rocks = 0, elevation = 0, shelters = 0, toys = 0;
    float score = 0;   // the suitability, before it's shown as 0-100
    float compat = 0;  // what it thinks of the other kinds of animal there
    int count[18] = {};
    float pct[18] = {};
    float foliage = 0, rockShare = 0, hills = 0; // as compared with its preferences
    int noShelter = 0, noToy = 0, unusedShelters = 0, unusedToys = 0;
    float poo = 0;     // the share of its tiles with poo
    // Objects it doesn't like there (their name ids), and how much
    std::vector<std::pair<int, int>> dislikes;
  };

  void loadTypes(ResourceManager *rm);
  void clear();
  const std::vector<Type> &types() const { return this->typeList; }
  // (research changes a species' characteristics)
  std::vector<Type> &mutableTypes() { return this->typeList; }
  std::vector<Member> &mutableMembers() { return this->list; }
  int typeOfFile(const std::string &file) const;
  const std::vector<Member> &members() const { return this->list; }
  Member *member(int id);
  const Member *member(int id) const;
  // Zoo animals (not the dinosaurs or marine animals, yet)
  bool isZooAnimal(int type) const;

  // Where one can go: an exhibit's ground (a land exhibit for a zoo
  // animal), not water, not on top of another
  Fences::Fit canPlace(int type, float x, float y, const WorldMap &map, const Fences &fences) const;
  int adopt(int type, bool female, float x, float y, const WorldMap &map, const Fences &fences);
  void remove(int id);
  // What selling it gives back: its price by how happy it is (measured:
  // an $800 zebra at about 66 happiness, $528)
  int refund(int id) const;
  // Picked up (Move Animal) and put down again
  void moveTo(int id, float x, float y);
  // While held: whether it can be put down where it is (green or red)
  void hold(int id, const WorldMap &map, const Fences &fences);
  bool carriedFit = false;
  bool canDrop(int id, float x, float y, const WorldMap &map, const Fences &fences) const;
  void drop(int id, float x, float y, const Fences &fences);

  void update(float seconds, double simSeconds, const WorldMap &map, const Fences &fences);
  void setItems(ZooItems *items) { this->items = items; }
  void setIndex(WorldIndex *index) { this->index = index; }
  void addToIndex(WorldIndex &index) const;
  // Which exhibit each is in, from where it stands (an exhibit opened up:
  // its animals are loose, "%s has escaped."; a fence closed round them:
  // they're in that one)
  void rehome(const WorldMap &map, const Fences &fences);
  // Messages for the player (as the original posts them), taken
  std::vector<std::string> takeMessages() {
    std::vector<std::string> m;
    m.swap(this->messages);
    return m;
  }
  int escapedCount() const;
  // The message bar's news ("%s is not happy.", "%s is ill."), taken: its
  // kind (0 general, white; 1 good news, green; 2 urgent, red: the message
  // queue's colours) and the animal it's about
  struct Notice {
    std::string text;
    int kind = 0;
    int animal = -1;
  };
  std::vector<Notice> takeNotices() {
    std::vector<Notice> n;
    n.swap(this->notices);
    return n;
  }
  // Babies born and the old dying (in update), done by the world: false
  // when nothing happened
  void lifeEvents(const WorldMap &map, const Fences &fences);
  // A keeper's visit: every animal in the exhibit cheered (cKeeperArrivesChange)
  void keeperArrives(int exhibit);
  // A keeper heals one: health full, not sick
  void heal(int id);
  // Needs a keeper's care: health at 85% or less
  bool needsHealing(const Member &m) const {
    return m.health * 100 <= this->typeList[m.type].maxHits * 85;
  }
  // Health as the panel shows it (0-100)
  int shownHealth(const Member &m) const {
    return static_cast<int>(m.health * 100 / std::max(1, this->typeList[m.type].maxHits));
  }
  // A keeper's dart (asleep where it is) and crate
  void tranquilise(int id);
  void box(int id);
  // Fences broken getting out, taken (the world breaks them)
  std::vector<Fences::Edge> takeBreaks() {
    std::vector<Fences::Edge> b;
    b.swap(this->breaks);
    return b;
  }
  // How many live in an exhibit (keepers feed and rake for them)
  int countIn(int exhibit) const;
  // The objects standing about (an exhibit's trees, rocks, shelters, toys)
  void setObjects(const PlacedObjects *objects) { this->objects = objects; }
  // Something in an exhibit changed (an object, an animal, the ground):
  // rated again (-1: every exhibit)
  void markDirty(int exhibit = -1);
  // Exhibit Suitability, 0-100, as the panel shows it
  int suitability(int id, const WorldMap &map, const Fences &fences);
  // An exhibit's suitability (zoo.exe hab+0xe8): its animals' average, as
  // last worked out (0 when it hasn't been)
  int exhibitSuitability(int exhibit) const;
  // Happiness as the panel shows it (0-100)
  static int shownHappiness(const Member &m) {
    return static_cast<int>((std::lround(m.happiness) + 100) * 100 / 200);
  }
  // Smiles and frowns (zoo.exe 0x4d8a4d, 0x4d89b2, 0x4dbc79): an
  // exhibit's rating for each kind of animal in it, kept before a change
  // (an object, an animal, the ground), compared after: better, its animals
  // of that kind smile; worse, frown; the same, an object or animal put in
  // or taken out is judged on its own (what they think of it); the ground
  // has no such fallback. Each face floats up for 1.25 s; smile.wav and
  // frown.wav play once.
  enum class Change { Ground, Object, Animal };
  void snapshot(int exhibit, const WorldMap &map, const Fences &fences);
  void react(int exhibit, Change change, const std::string &file, int animalType, bool removed,
             const WorldMap &map, const Fences &fences);
  bool paused = false;
  // Where a point of the map is from the middle of the view (screen
  // pixels), for its sounds
  std::function<bool(float x, float y, float &dx, float &dy)> viewOffset;
  // A [Sounds] entry: its file and attenuation (false: none)
  bool soundOf(int type, const std::string &key, std::string &file, int &atten) const;
  // Zookeeper Recommendations: each line and whether it's red
  std::vector<std::pair<std::string, bool>> advice(int id, const WorldMap &map, const Fences &fences);

  void collect(const WorldRenderer &view, const WorldMap &map,
               std::vector<Fences::Drawable> &out) const;
  int pick(float sx, float sy, const WorldRenderer &view, const WorldMap &map) const;
  Animation *art(const Member &m, const std::string &name) const;
  Animation *preview(int type, bool female) const;
  Animation *crate() const { return this->crateArt; }
  // "Recently", "A short time ago", "A long time ago"
  static int agoText(double since, bool slept);

  double clock() const { return this->now; }

  int selected = -1, hovered = -1, carried = -1;
  // The one being adopted, at the cursor (type < 0: none)
  int previewType = -1;
  bool previewFemale = false;
  float previewX = 0, previewY = 0;
  Fences::Fit previewFit = Fences::Fit::Ok;

  // Guests, for loose predators (zoo.exe 0x43954c): the nearest man guest
  // within a radius not yet attacked (-1: none); where one is; caught
  std::function<int(float x, float y, float radius)> findPrey;
  std::function<bool(int guest, float &x, float &y)> preyAt;
  std::function<void(int guest, const Member &by)> caughtPrey;
  // A water tile (fresh or salt water; a waterfall is land)
  static bool isWater(const WorldMap &map, int x, int y);
private:
  const WorldMap *mapRef = nullptr; // (the map, for the art: swimmers' swim / water idle)
  bool onWater(const Member &m) const;
  void waterTick(Member &m, const WorldMap &map, const Fences &fences);
  ResourceManager *rm = nullptr;
  ZooItems *items = nullptr;
  const PlacedObjects *objects = nullptr;
  bool useShelter(Member &m, const WorldMap &map, const Fences &fences);
  void leaveShelter(Member &m);
  // What an object is to an animal: its name id and habitat (the keys of
  // cSuitableObjects), its footprint, and what kind of thing it is
  struct ObjectInfo {
    int nameId = 0, habitat = 0, fx = 1, fy = 1, capacity = 0, toy = 0;
    bool foliage = false, rock = false, stink = false, shelter = false, building = false;
    bool rest = false, hideUser = false; // animals use it (animalrest), out of sight
    float adultChange = 0, childChange = 0, hungerChange = 0, energyChange = 0;
    int timeInside = 10;
  };
  mutable std::map<std::string, ObjectInfo> objectInfo;
  const ObjectInfo &infoOf(const std::string &file) const;
  struct Evaluation {
    float timerMs = 0;
    bool dirty = true;
    int tiles = 0, dung = -1;
    std::map<int, Rating> kinds; // per animal type
  };
  std::map<int, Evaluation> evaluations;
  const Rating *rating(const Member &m, const WorldMap &map, const Fences &fences);
  void evaluate(int exhibit, const WorldMap &map, const Fences &fences);
  // How many share its exhibit: its own kind (young too), its kind's
  // adults, all adults
  void company(const Member &m, int &kind, int &kindAdults, int &adults) const;
  void tick(Member &m, const WorldMap &map, const Fences &fences);
  WorldIndex *index = nullptr;
  std::vector<Type> typeList;
  std::vector<Member> list;
  std::map<int, int> counts; // per type: how many were ever adopted
  int nextId = 1;
  mutable std::map<std::string, Animation *> artCache;
  Animation *selectArrow = nullptr;
  Animation *smileArt = nullptr, *frownArt = nullptr;
  std::map<int, std::map<int, float>> snaps; // exhibit -> kind -> rating
  std::vector<std::string> messages;
  std::vector<Fences::Edge> breaks;
  std::vector<int> kills; // caught prey to go (next update)
  std::vector<Notice> notices;                 // message bar
  std::vector<std::pair<int, int>> births;     // mother, kind
  Animation *crateArt = nullptr;
  bool breakOut(Member &m, const WorldMap &map, const Fences &fences);
  float total(const Rating &r) const {
    return r.terrain + r.objects + r.trees + r.rocks + r.elevation + r.shelters + r.toys;
  }
  void face(int exhibit, int kind, bool smile);
  void playSound(const Member &m, const std::string &key);
  std::mt19937 rng{1999};
  double now = 0;

  int sub(const Member &m) const { return m.baby ? 2 : m.female ? 1 : 0; }
  float groundAt(const WorldMap &map, float x, float y) const;
  bool standable(int x, int y, const Type &t, int exhibit, const WorldMap &map,
                 const Fences &fences) const;
  bool canStep(int x, int y, int nx, int ny, const WorldMap &map, const Fences &fences) const;
  const std::vector<Step> *set(const Member &m, const std::string &name) const;
  void pushSet(Member &m, const std::string &name);
  void chooseBehaviour(Member &m);
  void nextStep(Member &m, const WorldMap &map, const Fences &fences);
  void play(Member &m, const std::string &anim, Member::Mode mode, float seconds = 0);
  bool walkTo(Member &m, float gx, float gy, const WorldMap &map, const Fences &fences);
  // Hunting in an exhibit: whether one lists another as prey (an adult
  // hunting); a set's way of moving (its fMove / fRun: anim and speed); the
  // nearest adult within reach that would hunt it, or chases it; running off
  bool hunts(const Member &predator, const Member &prey) const;
  void moveOf(const Member &m, const std::string &setName, std::string &anim, float &speed) const;
  const Member *threatTo(const Member &m, float radius) const;
  void runFrom(Member &m, const Member &threat, const WorldMap &map, const Fences &fences);
  void chase(Member &m, const WorldMap &map, const Fences &fences);
  bool busy(const Member &m) const;
  bool walkSomewhere(Member &m, const WorldMap &map, const Fences &fences);
  int nearestFood(const Member &m) const;
  void faceToward(Member &m, float x, float y);
  float animSeconds(const Member &m, const std::string &anim) const;
  CompassDirection facing(const WorldRenderer &view, const Member &m) const;
};

#endif // ANIMALS_HPP
