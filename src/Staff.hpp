#ifndef STAFF_HPP
#define STAFF_HPP

#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "Animation.hpp"
#include "Fences.hpp"
#include "ZooItems.hpp"
#include "WorldIndex.hpp"
#include "Walkways.hpp"
#include "ViewingAreas.hpp"

class Animals;

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
class Guests;
class PlacedObjects;

class Staff {
  friend std::string runShowManagementChecks(class World &, ResourceManager *);
  friend class SaveGame; // (saving and loading a game)
  friend std::string runParityChecks(class World &, ResourceManager *);
  friend class Shows; // Shared marine-show tasks use the normal routes and animation queues.
public:
  enum class Kind { Keeper, Maint, Guide, Scientist, Trainer, Helicopter };
  struct Type {
    std::string file, key;
    Kind kind = Kind::Keeper;
    int nameId = 0, salary = 0, dutiesText = 0;
    // Paid each month (zoo.exe ZTStaff::wage 0x5ad1e8: the type's +0x1bc,
    // cMonthlyCost - its default the type's cPurchaseCost, +0x12c, at
    // ZTStaffType::setDefaults 0x4c3081)
    int wage = 0;
    float speed = 0.466f; // tiles a second (cSlowRate's: Original::unitTilesPerSecond)
    bool sexes[2] = {true, false};
    // Animation name -> art path (no .ani), per sex (0 m, 1 f)
    std::map<std::string, std::string> anims[2];
    std::string fullPal;
    std::vector<std::string> hair, skin;
    std::string listImage[2], infoImage[2];
    std::map<std::string, std::pair<std::string, int>> sounds; // [Sounds]: file, attenuation
    // The DRT: hired as its base (scenery/building/helibase.ai, a 10 x 8
    // half-tile building), the helicopter in its slot on top
    std::string baseFile;
    Animation *base = nullptr, *baseUsed = nullptr;
    int footX = 1, footY = 1; // tiles
    // its slot0's slotpos (where it sits) and exitpos (where take-off ends
    // and the way home aims): tiles from the base's middle, heights in
    // levels (16 a level)
    float padX = -1, padY = 0, padZ = 5, exitX = -1, exitY = 0, exitZ = 10;
    float weaponRange = 5;
    // cMinHeight / cMaxHeight (16 a level) and the rates of its fFly
    // behaviour sets (bAscend, bDescend, bFly, bChaseAnimal)
    int minHeight = 128, maxHeight = 256;
    int ascendRate = 150, descendRate = 150, flyRate = 150, chaseRate = 250;
    int padFacing = 7;
    std::string rotorSound, fireSound;
    // (ZTHelicopterType::loadConfig 0x4c42ff: cLoopSoundAtten, +0x1f8)
    int rotorAtten = 0;
    // Tour guides (tour.ai): cTourGuideBonus (guests' happiness at a talk,
    // and the viewing area's donation bonus), cFollowChance, cMaxGroupSize,
    // cCrowdCheck (ticks between looks for followers), cCrowdRadius (tiles),
    // cInformGuestTime (seconds of each of its two talks)
    int tourBonus = 5, followChance = 60, maxGroup = 15, crowdCheck = 5, crowdRadius = 1, informTime = 5;
    // cSlowRate (walking; keeper training raises it) and cFastRate (running
    // after an escaped animal): 64ths of a tile, whole ones an update
    // (Original::unitTilesPerSecond), cCleanTrashRadius (maintenance: over
    // 0, sweeping takes the litter all round, not just beside),
    // cFixFenceModifier
    int slowRate = 33, fastRate = 66, cleanTrashRadius = 0, fixFenceModifier = 0;
    // Maintenance: how often (ms) the work it couldn't get to is forgotten -
    // cClearInvalidListInterval (ZTMaintType +0x1fc, 10000 by setDefaults
    // 0x4c3bb3; the shipped maint.ai doesn't set it)
    int clearInvalidMs = 10000;
    // A marine specialist's tank sweep: cCleanTankPct cleaner each, until
    // cCleanTankThreshold (ZTKeeperType +0x204, +0x208; setDefaults 0x4c32bd)
    int cleanTankPct = 10, cleanTankThreshold = 100;
    // The dirt types it cleans up, by their cNameID: its cDirt list
    // (ZTKeeperType::loadConfig 0x4c34ae, +0x20c; no default - keeper.ai
    // 6062 poo and 6093 small zoo doo, scient.ai 6094 dinopoo alone,
    // trainer.ai none)
    std::vector<int> dirt;
    // cChaseCheck: status ticks between its looks for an animal that would
    // hunt it (ZTStaffType +0x1b8, loadConfig 0x4c3783; setDefaults 0x4c3081
    // 2); running from one, its bRunFromPredator: fRun (run, cFastRate) or
    // fMove(0,0,<anim>,<rate>) - maint.ai's run at 55
    int chaseCheck = 2;
    std::string fleeAnim = "run";
    int fleeRate = -1; // -1: cFastRate
    // Its work's behaviour sets from its .ai ([BehaviorSet\bHeal] and the
    // rest, by lower-case name): the animations played in turn - fPlay,
    // fPlayWithSound with its [Sounds] key, fPlayReverse backwards
    struct Step {
      std::string anim, sound;
      bool reverse = false;
    };
    std::map<std::string, std::vector<Step>> sets;
  };
  // What a staff member is doing. The original gives each unit a job object
  // made by its number (unitMakeJob, zoo.exe 0x413a23), each its own class:
  // a constructor (the unit kept if its type fits the job), a destructor, its
  // number (the status line), and the slots run by every job's tick - begin,
  // whileGoing (picks the target, routes; no way there ends the job),
  // checkArrived (Job::arrived 0x43842f, Job::isCurrent 0x438436,
  // Job::pathDone 0x49933b - ours: the path's end), atTarget (the work, then
  // the next job). Ours keeps one Job and a phase instead; the classes read
  // (2026-11-16) and where each is:
  //   Visit (Staff::visitStep): 1 Job1_GoDropFood 0x5082e5 0x508350 0x4993b9
  //     0x508396; 2 Job2_PutDownFood 0x508448 0x499623 0x435d3f 0x5084b3
  //     0x49959d 0x48f624; 3 Job3_GoHealAnimal 0x47045a 0x47018c 0x492d7a
  //     0x508b3c 0x470010; 4 Job4_HealAnimal 0x470121 0x508b86 0x47000a
  //     0x47002e 0x46ff99 0x470261; 5 Job5_GoCleanDirt 0x4a2963 0x426a95
  //     0x412623 0x412629 0x426adb; 6 Job6_CleanDirt 0x42f860 0x426a4f
  //     0x4269d8 0x4269de 0x4a2dc5; in a tank: 0x16 Job22_GoToTank 0x49ad06
  //     0x49addc 0x49aae4 0x49acd8 0x49aaea 0x49aacf 0x49abfe; 0x14
  //     Job20_ClimbUp 0x49ad94 0x49b001 0x49a04d 0x49a053 0x499fe6 0x49b119;
  //     0x15 Job21_ClimbDown 0x49a8ca 0x49a46c 0x49a20a 0x49a210 0x49a1a3
  //     0x49a57a; 0x17 Job23_GoToPlatformExitTank 0x49a640 0x49a37c 0x49957b
  //     0x49a6ab 0x499581 0x49954b 0x49a3f3; 0x18 Job24_GoCleanTank 0x47f828
  //     0x48abc2 0x5965c6 0x5965cc 0x59658a 0x58a82d; 0x19 Job25_CleanTank
  //     0x58a7ab 0x47f6ab 0x5961de 0x5961e4 0x59619e; 0x1c Job28_ClimbUpB
  //     0x49b575 0x49b31a 0x49b2fc 0x49b3f7 0x49b302 0x49b2a9 0x49b3b7; 0x1d
  //     Job29_ClimbDownB 0x49b88c 0x49b64f 0x49b62f 0x49b700 0x49b635
  //     0x49b5e0 0x49b6ec
  //   Litter: 7 Job7_GoToTrashPile 0x52d30c 0x52d05a 0x52cf85 0x52cef4
  //     0x52d5f5; 8 Job8_Sweep 0x52d6f1 0x52cfd2 0x52d054 0x52d609 0x52d018
  //     0x52d8e8
  //   Trash: 9 Job9_GoToTrashCan 0x52d0a0 0x52d1c3 0x427177 0x4271a1
  //     0x42717d 0x52d209; 10 Job10_EmptyTrashCan 0x48b4d1 0x52d230 0x52d2b2
  //     0x48b3f2 0x52d276 0x59be1c
  //   Dung: 0xb Job11_GoToZoodoo 0x4a27eb 0x4a982b 0x4a2746 0x4a274c
  //     0x50e17f; 0xc Job12_BagZoodoo 0x50e193 0x50e4a2 0x50d9b1 0x50d958
  //     0x50e3ef (ours plays bBag for all dung: we have no dinopoo, whose
  //     bBagDino is cleanup/vac - issue in "Base game playable")
  //   Catch (Staff::catchStep): 0xd Job13_Tranquilize 0x50d6f4 0x50d360
  //     0x50d059 0x50d05f 0x50cffd 0x50db25; 0xe Job14_RetrieveAnimal
  //     0x50da68 0x50dad3 0x50da62 0x50da26 0x50dd6e; 0xf Job15_BoxAnimal
  //     0x50dc0d 0x4a13a6 0x50d33f 0x50d345 0x50d2d8
  //   Fence: 0x10 Job16_GoToFence 0x49e331 0x49e39c 0x4a7a14 0x4a7a1a
  //     0x4a79f0 0x59a2b6; 0x11 Job17_FixFence 0x59a3d5 0x59a440 0x4a23ac
  //     0x59a2ca 0x59a596
  //   Filter: 0x1a Job26_GoToFilter 0x5958b4 0x59591f 0x5ae362 0x5ae320
  //     0x5ae33e 0x596000; 0x1b Job27_FixFilter 0x595f06 0x595dbb 0x4a23c0
  //     0x595e11 0x595d17
  // Not here: 0x12 Job18_Defend is an animal's (bDefense, bPreattack: see
  // Animals::standingOff); 0x13 Job19 (a marine specialist at a tank's
  // surface, tankEntryPoint) isn't modelled (an issue in "Base game
  // playable"). Table 0x63244c is the base job (set by 0x413a43, every
  // job's destructor puts it back; its own destructor 0x6041a3): ours, the
  // Job value.
  enum class Job { None, Fence, Filter, Litter, Visit, Catch, Tour, Trash, Dung, Flee };
  // The helicopter's behaviour set playing (helicopt.ai), each in one of
  // the original's tasks: Parked - 0x6a waiting at base (bIdleGround);
  // SpinUp, Ascend - 0x69 take-off (bSpinup, bAscend); Chase, Fire - 0x6f
  // tranquilizing (bChaseAnimal, bFireWeapon); Return - 0x67 back to base
  // (bFly); Descend, SpinDown - 0x68 landing (bDescend, bSpindown)
  enum class HeliStage { Parked, SpinUp, Ascend, Chase, Fire, Return, Descend, SpinDown };
  struct Member {
    int id = 0, type = 0;
    // x/y stay the base's anchor even while its aircraft is away. Old saves
    // already store those coordinates; flight jobs restart safely on load.
    HeliStage heliStage = HeliStage::Parked;
    float heliX = 0, heliY = 0, heliZ = 0;
    // the tile middle its chase flies to (Job13's route, 0x422238); -1 none
    float heliToX = -1, heliToY = -1;
    int rotorChannel = -1;
    double baseAnimTime = 0;
    bool female = false;
    int hair = 0, skin = 0;
    std::string name;
    float x = 0, y = 0;       // tiles
    float fx = 0, fy = 1;     // facing (world)
    std::string anim = "idle";
    float animTime = 0;
    int playsLeft = 0;        // a work animation played this many times
    // (a behaviour set being played: its steps, playsLeft of them to go, the
    // one playing backwards when reverse)
    std::vector<Type::Step> steps;
    bool reverse = false;
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
    // Maintenance: the work tiles it found no way to (zoo.exe unit +0x27c),
    // left out of its looks until cleared (+0x29c counting down)
    // (keepers too: the tiles they found no way to in a habitat, emptied as
    // their duties there begin and end - zoo.exe 0x49f3ca, 0x49ebe7)
    std::vector<std::pair<int, int>> invalid;
    float invalidClear = 0;
    // Keepers: the animals they found no way to (zoo.exe unit +0x270: Job1
    // 0x4993b9, Job3 0x508b3c), left out until their duties end
    std::vector<int> unreachable;
    Job job = Job::None;
    Fences::Edge jobEdge = {false, -1, -1};
    int jobFilter = -1;
    int jobItem = -1;         // litter or dung it's going to
    int jobAnimal = -1;       // the escaped animal it's after
    float repath = 0;
    // Keepers visiting an exhibit: through its gate (access lets it walk
    // in that exhibit), then feeding, raking, out again
    int access = -1, visit = -1, phase = 0;
    float outX = 0, outY = 0, inX = 0, inY = 0;
    float dropX = 0, dropY = 0; // where it's putting the food down
    // The animal its food duty is for, and that animal's keeper food, what it
    // puts down (zoo.exe keeperFoodDuty 0x49efa6: unit +0x2a4; -1 / empty
    // when none was chosen, as after a load: the exhibit's first animal's)
    int feedAnimal = -1;
    std::string dropFood;
    // An animal that would hunt it (zoo.exe ZTStaff::tick 0x440602): its
    // status ticks (as a guest's, [AI] mStatusPeriod), the countdown to its
    // next look (+0x28c), "saw escaped animal" (+0x24c) and the one seen
    // (+0x22c)
    float statusMs = 0;
    int chaseCount = 0;
    bool sawThreat = false;
    int predator = -1;
    // Marine specialists in a tank (zoo.exe unit+0x258): 0 on land, 1
    // climbing, 2 at the water's surface, 3 under it; the tank; its height
    // over the ground under it (the tank's floor)
    int water = 0, tank = -1;
    int showTank = -1; // A reserved specialist follows the show controller.
    float z = 0;
    // A climb (bClimbUp fFly(climb), bClimbDown fFlyReverse(climb)): from
    // one place to another over one play of the climb animation, backwards
    // climbing down; inside the tank (diving under, up from under) it's
    // drawn with the tank's water
    bool climbing = false, climbDown = false, climbInside = false;
    float climbFrom[3] = {0, 0, 0}, climbTo[3] = {0, 0, 0}, climbTime = 0;
    int climbWater = 0;   // its water state when the climb's done
    bool sweeping = false; // moving with the tank sweep (bCleanTankSub)
    // A tour guide's trip: the exhibit, the guests following, its talk
    std::vector<int> followers;
    float crowdClock = 0, speakLeft = 0;
    bool speaking = false;
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
  std::pair<float, float> placementPoint(int type, float x, float y) const;
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
  // The animals (keepers dart and crate escaped ones)
  void setAnimals(Animals *animals) { this->animals = animals; }
  void setGuests(Guests *guests) { this->guests = guests; }
  void setObjects(PlacedObjects *objects) { this->objects = objects; }
  // The exhibits' viewing areas (tour guides go where guests are; guests
  // stand in them to look)
  ViewingAreas &viewingAreas() { return this->viewing; }
  // Dung raked with a compost in the zoo: cZooDooRecyclingAmount ($50) a
  // pile, Recycling (zoo.exe 0x4a2c98); none without one. Taken since last
  // asked.
  double takeRecycling() {
    double r = this->recycling;
    this->recycling = 0;
    return r;
  }
  // A tour guide giving its talk
  bool guideSpeaking(int id) const;
  // The tour guide bonus at an exhibit's viewing area: a guide (any) on one
  // of its viewing tiles gives its cTourGuideBonus (they don't add up)
  int guideBonusAt(const std::vector<std::pair<int, int>> &tiles) const;
  // (research: training changes a kind of staff's characteristics)
  std::vector<Type> &mutableTypes() { return this->typeList; }
  // Research: a chow's cPurchaseCost changed (zoo.exe effect 5, in cents)
  std::function<double(const std::string &food, double price)> foodPrice;
  // Where a point is from the middle of the view (screen pixels), for
  // their work sounds
  std::function<bool(float x, float y, float &dx, float &dy)> viewOffset;
  void workSound(const Member &m, const std::string &anim) const;
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
  // (the ZT1 Lab) A staff member's route between two tiles, by their rules
  // (zoo ground, out of an exhibit through its gate, paths cheaper):
  // nothing moves
  bool testRoute(int sx, int sy, int gx, int gy, const WorldMap &map, const Fences &fences,
                 std::vector<std::pair<int, int>> &out) const;
  // (the ZT1 Lab) One of a staff member's animations, so many times; what
  // it has
  void labPlay(int id, const std::string &anim, int times);
  std::vector<std::string> labAnims(int id) const;
  // Drawables for the map's objects pass; the selected one shows the arrow
  // over its head, the hovered one lit up
  // (those up on a walkway deck go to 'raised', drawn with the decks)
  void collect(const WorldRenderer &view, const WorldMap &map,
               std::vector<Fences::Drawable> &out,
               std::vector<Fences::Drawable> *raised = nullptr) const;
  // Those in a tank's water, drawn with it: under its surface, and (surface
  // true) the part of a swimmer at the surface above it
  void drawInTank(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map, int tank,
                  bool surface);
  // A tank's water depth (height units over its floor)
  std::function<float(int tank)> tankDepth;
  // The one drawn at a logical screen point (-1: none)
  int pick(float sx, float sy, const WorldRenderer &view, const WorldMap &map) const;
  int monthlyWages() const;
  // The food keepers bought since last asked (Zoo Upkeep Cost: a pile of
  // herbivore chow $229, as the original's books)
  // The food an exhibit's current species needs, including its custom diet.
  std::string foodFor(int exhibit, const Fences &fences, bool &carnivore) const;
  double takeUpkeep() {
    double u = this->upkeep;
    this->upkeep = 0;
    return u;
  }
  // Messages for the message bar (zoo.exe 0x49cec0's priority as kind):
  // a keeper that can't get to its exhibit (11004 / 11001). Taken since
  // last asked.
  struct Notice {
    std::string text;
    int kind = 0;
    int member = -1;
  };
  std::vector<Notice> takeNotices() {
    std::vector<Notice> n;
    n.swap(this->notices);
    return n;
  }
  // The art an animation of a member draws with (its colours)
  Animation *art(const Member &m, const std::string &name) const;
  Animation *preview(int type) const;
  Animation *baseArt(const Member &m) const;
  int baseFrame(const Member &m,Animation *art=nullptr) const;
  int helicopterFrame(const Member &m,Animation *) const;

  int selected = -1, hovered = -1, carried = -1;
  // (debug) every route planned goes to the log
  bool logRoutes = false;
  // The one being hired, at the cursor (type < 0: none)
  int previewType = -1;
  float previewX = 0, previewY = 0;
  Fences::Fit previewFit = Fences::Fit::Ok;
  bool previewPoor = false; // (it fits, but the first month's pay is more than the cash: red)

private:
  ResourceManager *rm = nullptr;
  ZooItems *items = nullptr;
  Animals *animals = nullptr;
  Guests *guests = nullptr;
  PlacedObjects *objects = nullptr;
  ViewingAreas viewing;
  double recycling = 0;
  void dungRecycled(float x, float y);
  bool tourWork(Member &m, const WorldMap &map, Fences &fences);
  void gatherFollowers(Member &m);
  bool catchWork(Member &m, const WorldMap &map, const Fences &fences);
  void helicopterStep(Member &m, float seconds, const WorldMap &map, const Fences &fences);
  void releaseCatch(Member &m);
  // What an exhibit's animals eat: the chow its keeper puts down, and
  // whether it's carnchow (feedc, else feedh: zoo.exe 0x5084b3)
  std::map<std::string, int> chowPrice;
  std::map<int, double> lastVisit; // exhibit -> when a keeper last finished there
  double clock = 0;
  static constexpr double kRevisitSeconds = 90.0; // zoo.ini [AI] exhibitServiceInterval (ticks)
  double upkeep = 0;
  std::vector<Notice> notices;
  void pathTrouble(const Member &m, const std::string &exhibit, bool blocked);
public:
  // A new exhibit's staff access check (zoo.exe exhibitAccessCheck
  // 0x44e4eb, after OK on its name): 0, or the message posted - 11000 "%s
  // is inaccessible to zoo staff.", 11001 "The entrance to %s is blocked..."
  int exhibitAccess(int exhibit, const WorldMap &map, const Fences &fences);
private:
  void catchStep(Member &m, float seconds, const WorldMap &map, const Fences &fences);
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
  // The other keepers at an exhibit: at their duties in it (the habitat's
  // +0xf0, +0x132 when any) and headed for it (+0xf4)
  void keepersAt(int exhibit, int self, const Fences &fences, int &inDuties, int &headed) const;
  // Whether this staff member looks after an animal type (zoo.exe 0x423f00)
  bool caresFor(const Member &m, int animalType) const;
  // The animal its food duty feeds next, -1 none (zoo.exe 0x49f01a)
  int animalToFeed(const Member &m, int exhibit);
  // The animal that would hunt it, -1 none (zoo.exe animalFindPredator
  // 0x4369b9, a staff member's slot 0x2fc, with its slot 0x304 0x4391a2)
  int findThreat(const Member &m, const Fences &fences) const;
  // Off away from it (task 0x75, Task75_AvoidPredator): what it was doing
  // dropped, to a tile on the ring cPredatorRadius + 2 away (slot 0x30c,
  // animalFleeTarget 0x4a639e)
  bool flee(Member &m, const WorldMap &map, Fences &fences);
  int predatorRadius = 7; // ztunit.ai cPredatorRadius (0x6394f4)
  // Whether it cleans up this dung pile (ZTKeeper class slot 0x324,
  // 0x42404e: the pile type's id on its type's cDirt list)
  bool cleans(const Member &m, const ZooItems::Item &dung) const;
  // The dung piles in an exhibit it cleans up
  std::vector<int> dungFor(const Member &m, int exhibit) const;
  void face(Member &m, float x, float y);
  // A path over the tiles from a member to a goal (tile); empty: none
  bool findPath(Member &m, int gx, int gy, const WorldMap &map, const Fences &fences,
                float endX, float endY);
  void wander(Member &m, const WorldMap &map, const Fences &fences);
  bool walkAbout(Member &m, const WorldMap &map, const Fences &fences);
  // The original's staff with nothing to do (#243): who keeps to it, and
  // its step when standing (work on arrival, the keeper's stand, the walk)
  bool idleOriginal(const Member &m) const;
  void idleStep(Member &m, const WorldMap &map, Fences &fences);
  bool findWork(Member &m, const WorldMap &map, Fences &fences);
  void play(Member &m, const std::string &anim, int times);
  void playSet(Member &m, const std::string &set, const std::string &anim, int times);
  void startStep(Member &m);
  void soundAt(const Member &m, const std::string &key) const;
  // A climb to (x, y, z: height over the ground there), down or up, inside
  // the tank or over its wall; its water state after
  void climb(Member &m, float x, float y, float z, bool down, bool inside, int water, const WorldMap &map);
  // An animation's name as it's in the water (walk is subswim under it)
  std::string inWater(const Member &m, const std::string &name) const;
  mutable int onlyTank = -2;
  mutable bool onlySurface = false;
  CompassDirection facing(const WorldRenderer &view, const Member &m) const;
  float animSeconds(const Member &m, const std::string &anim) const;
};

#endif // STAFF_HPP
