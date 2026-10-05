#ifndef GUESTS_HPP
#define GUESTS_HPP

#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "Animation.hpp"
#include "Fences.hpp"

class Animals;
class PlacedObjects;
class ZooItems;
class ResourceManager;
class WorldIndex;
class WorldMap;
class WorldRenderer;

// The zoo's guests (guests/guests.ai: men, women, boys and girls), as
// zoo.exe runs them (read in its disassembly):
// - every 2 s (economy.cfg newguest) one may come, by how good the zoo is
//   (its rating) and how cheap the admission (cAdultAdmission bands); none
//   while an animal is loose. Each pays the admission x 3.5 (children half:
//   checked against the original's books);
// - they walk the paths, picking an exhibit they haven't seen (its animals'
//   cAttractiveness, their favourite animal, the further the better) and
//   gawk at it for a while (longer the happier and more attractive its
//   animals), then the next;
// - a status tick a second: hungrier, thirstier and so on; at the fence,
//   happier or not by how happy the animals are, how many kinds, how
//   crowded; unhappy, they may leave; seen it all, most leave;
// - an escaped animal near: they run ("RUN, %s has escaped!").
class Guests {
  friend class SaveGame; // (saving and loading a game)
public:
  enum class Kind { Man, Woman, Boy, Girl };
  struct Type {
    Kind kind = Kind::Man;
    std::string key; // man, woman, boy, girl
    int nameId = 0;
    float speed = 0.55f, runSpeed = 0.9f;
    bool female = false, child = false;
    std::map<std::string, std::string> anims; // name -> art path (no .ani)
    std::string fullPal;
    std::vector<std::string> slots[4]; // shirt, pants or skirt, hair, skin palettes
    int gawkWeights[3] = {45, 10, 45}; // gawk, camera, stand
  };
  enum class State { Arriving, Walking, Gawking, Leaving, Fleeing, Using, Caught };
  struct Guest {
    int id = 0, type = 0;
    int colours[4] = {};
    float x = 0, y = 0, fx = 0, fy = 1;
    std::string anim = "stand";
    float animTime = 0, playFor = 0;
    State state = State::Walking;
    std::vector<std::pair<float, float>> path;
    size_t pathAt = 0;
    float speed = 0.55f;
    int target = -1;          // the exhibit it's going to or watching
    std::set<int> seen;       // exhibits seen this round
    int favourite = -1;       // an animal type
    // -100..100 happiness; 0..100 hunger, thirst, bathroom, tiredness;
    // changes wait for the next tick
    float happiness = 50, hunger = 0, thirst = 0, bathroom = 0, tired = 0;
    float dHappy = 0, dHunger = 0, dThirst = 0, dBathroom = 0, dTired = 0;
    float tickMs = 0;
    int viewCheck = 0, leaveCheck = 0, hungerCheck = 0, thirstCheck = 0, bathroomCheck = 0,
        energyCheck = 0, chaseCheck = 0;
    bool leaving = false, sawEscape = false;
    // A building it's going to or in (an index into the placed objects),
    // for which need ("food", "drink", "bathroom", "energy", "trash")
    int building = -1;
    std::string need;
    bool hidden = false;      // inside it
    bool trash = false;       // holding a wrapper or cup
    int trashCheck = 0, souvenirCheck = 0;
    bool souvenir = false;    // has one
    std::set<int> visited;    // attractions this round (object ids)
    float gawkSeconds = 0;    // how long it's been watching
    int lastThought = 0;      // lang string
    std::string thoughtArg;
    std::vector<std::string> thoughts; // newest first (Guest Information)
    std::string name;         // "Guest 48"
    double arrived = 0;       // seconds of play
    // On a tour guide's tour ("Following Tour Guide"), and whether its talk
    // has cheered it yet
    int guide = -1;
    bool tourHeard = false;
    // Attacked by a loose predator (never twice), and getting up after
    bool attacked = false;
    int caughtStage = 0;
  };

  void load(ResourceManager *rm);
  void clear();
  // The buildings and benches they use, and where litter goes
  void setObjects(PlacedObjects *objects, ZooItems *items) {
    this->objects = objects;
    this->items = items;
  }
  // Exhibit donations since last asked (Private Donations): its exhibits
  // to the amounts
  double takeDonations() {
    double d = this->donations;
    this->donations = 0;
    return d;
  }
  // Members (guests who joined on leaving happy): how many
  int members = 0;
  // Tour guides: an exhibit's viewing tiles (paths beside it); a guest
  // joining a guide's tour (not if busy, on another tour or in need: false),
  // heading for its exhibit; a guide's group let go; its talk heard (its
  // followers already there cheered by bonus, latecomers while it talks)
  std::vector<std::pair<int, int>> viewingTiles(int exhibit) const;
  // The tiles a guest can walk to from a tile (tests, placing things)
  std::set<std::pair<int, int>> reachableFrom(int x, int y) const;
  bool joinTour(int guest, int guide, int exhibit, int tx, int ty);
  // Loose predators (zoo.exe 0x43971c, 0x4a6043): only men (cPrey 9503 is
  // the man guest) not already attacked; caught, it's mauled (its
  // bCaughtBy<animal><m|f>: the animation and roar), gets up and thinks
  // "Ouch! I've just been attacked by %s." - it lives
  int nearestPrey(float x, float y, float radius) const;
  bool preyPosition(int guest, float &x, float &y) const;
  void caught(int guest, const std::string &animalKey, bool female, const std::string &animalName);
  void releaseTour(int guide);
  void tourTalk(int guide, int bonus);
  std::function<bool(int guide)> guideSpeaking;
  std::function<int(int guide)> guideBonusOf;    // its cTourGuideBonus
  std::function<int(int exhibit)> tourBonusAt;   // a guide on the exhibit's viewing tiles
  // The HUD's Hide Guests toggle: not drawn, not picked
  bool hideAll = false;
  // What guests did at each exhibit since last asked: time spent viewing
  // (game seconds) and donations
  struct ExhibitBooks {
    double viewed = 0, donated = 0;
  };
  std::map<int, ExhibitBooks> takeExhibitBooks() {
    std::map<int, ExhibitBooks> b;
    b.swap(this->exhibitBooks);
    return b;
  }
  // What the stands took since last asked (Concessions)
  double takeConcessions() {
    double c = this->concessions;
    this->concessions = 0;
    return c;
  }
  void setWorld(const WorldMap *map, const Fences *fences, const Animals *animals, WorldIndex *index) {
    this->map = map;
    this->fences = fences;
    this->animals = animals;
    this->index = index;
  }
  // Where they come in and go out (a path tile by the zoo's entrance)
  void setEntrance(int x, int y) {
    this->entranceX = x;
    this->entranceY = y;
  }
  bool hasEntrance() const { return this->entranceX >= 0; }

  // The zoo's rating and admission (adult), for who comes
  void update(float seconds, int zooRating, double admission);
  // A guest put in at once (tests, the dev console): its fee
  double admit(double admission);
  // What came in since last asked: the admissions taken
  double takeIncome(int &count) {
    double m = this->income;
    count = this->arrivals;
    this->income = 0;
    this->arrivals = 0;
    return m;
  }
  const std::vector<Guest> &guests() const { return this->list; }
  const std::vector<Type> &types() const { return this->typeList; }
  const Guest *guest(int id) const;
  // Average happiness, 0..100 as the HUD's bar shows it
  int averageHappiness() const;
  // The day's messages ("The entrance fee is a really good value"), taken
  std::vector<int> takeMessages() {
    std::vector<int> m;
    m.swap(this->messages);
    return m;
  }

  void addToIndex(WorldIndex &index) const;
  void collect(const WorldRenderer &view, const WorldMap &map, std::vector<Fences::Drawable> &out) const;
  int pick(float sx, float sy, const WorldRenderer &view, const WorldMap &map) const;
  Animation *art(const Guest &g, const std::string &name) const;
  // Its mini icon for the Guest List (guests/ls<m|f|b|g>guest) in its own
  // colours
  Animation *listIcon(const Guest &g) const;
  // Crowd sounds: the bed by how many there are, chatter now and then
  void updateSounds(float seconds);
  bool sounds = true;

  int hovered = -1, selected = -1;
  // Every guest thought with what it's about (ZTThoughtMgr): newest first
  struct Thought {
    std::string text;
    int guest = -1, animal = -1, exhibit = -1;
  };
  std::vector<Thought> thoughtLog;
  // The latest about an animal (5: Animal Information's Thoughts) or an
  // exhibit (20: the Exhibit List's)
  std::vector<std::string> thoughtsAbout(int animal, int exhibit, size_t most) const;
  double clock = 0;
  // Guest Information's lines: "Just arrived", "A short time", "A while"
  int timeInParkText(const Guest &g) const;

private:
  ResourceManager *rm = nullptr;
  const WorldMap *map = nullptr;
  const Fences *fences = nullptr;
  const Animals *animals = nullptr;
  WorldIndex *index = nullptr;
  PlacedObjects *objects = nullptr;
  ZooItems *items = nullptr;
  double concessions = 0, donations = 0;
  std::map<int, ExhibitBooks> exhibitBooks;
  std::map<std::string, std::string> caughtSound; // "blackbrm" -> its roar's [Sounds] key
  // A building as guests use it (its .ai): what it satisfies, sells, its
  // price, how long they're in, and what it does for them
  struct BuildingInfo {
    std::set<std::string> satisfies;
    std::string item;          // the first thing it sells
    float price = 0, priceFactor = 1, low = 0, high = 30;
    int capacity = 1;
    float timeInside = 3;
    bool hideUser = false;
    float adultChange = 0, childChange = 0, hunger = 0, thirst = 0, bathroom = 0, energy = 0;
    // its item's
    float itemAdult = 0, itemChild = 0, itemHunger = 0, itemThirst = 0, itemBathroom = 0;
    bool itemTrash = false;
    int usedThought = 0, consumedThought = 0;
    std::string useSound;
    int useAtten = 0;
    int fx = 2, fy = 2;
  };
  mutable std::map<std::string, BuildingInfo> buildingInfo;
  const BuildingInfo &infoOf(const std::string &file) const;
  bool seekBuilding(Guest &g, const std::string &need, int radius);
  void useBuilding(Guest &g);
  std::vector<Type> typeList;
  std::vector<Guest> list;
  int nextId = 1;
  int entranceX = -1, entranceY = -1;
  float spawnClock = 0;
  double income = 0;
  int arrivals = 0;
  std::vector<int> messages;
  mutable std::map<std::string, Animation *> artCache;
  Animation *selectArrow = nullptr;
  std::mt19937 rng{1977};
  // crowd sounds
  int bedLevel = -1, bedChannel = -1;
  float chatterClock = 0;
  std::map<std::string, int> soundAtten;
  std::map<std::string, std::string> soundFile;

  bool walkable(int x, int y) const;
  bool canStep(int x, int y, int nx, int ny) const;
  bool walkTo(Guest &g, int tx, int ty, float speed);
  void decide(Guest &g);
  void tick(Guest &g);
  void gawk(Guest &g);
  void think(Guest &g, int stringId, const std::string &arg = "", int animal = -1, int exhibit = -1);
  int pickExhibit(Guest &g, int &vx, int &vy);
  void viewing(Guest &g);
  void wander(Guest &g);
  void play(Guest &g, const std::string &anim, float seconds);
  float groundAt(float x, float y) const;
  CompassDirection facing(const WorldRenderer &view, const Guest &g) const;
};

#endif // GUESTS_HPP
