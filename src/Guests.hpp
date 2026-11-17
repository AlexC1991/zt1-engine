#ifndef GUESTS_HPP
#define GUESTS_HPP

#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "Animation.hpp"
#include "Fences.hpp"
#include "PlacedObjects.hpp"

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
  friend std::string runShowManagementChecks(class World &, ResourceManager *);
  friend class SaveGame; // (saving and loading a game)
  friend std::string runParityChecks(class World &, ResourceManager *);
  friend std::string runGuestPickupChecks(class World &, ResourceManager *, int &, int &);
  friend class Shows; // Shared marine-show tasks use the normal routes and animation queues.
  friend std::string runShowPolishChecks(class World &, ResourceManager *, bool);
  friend int runShowChecks(class World &, class UiGameScreen &, class ZooSim &, const std::string &,
                           const std::string &, const std::function<void(const char *)> &);
public:
  enum class Kind { Man, Woman, Boy, Girl };
  struct Type {
    Kind kind = Kind::Man;
    std::string key; // man, woman, boy, girl
    int nameId = 0;
    // Tiles a second walking (its cSlowRate) and running (Guests::load)
    int slowRate = 33;
    float speed = 0.466f, runSpeed = 0.7f;
    bool female = false, child = false;
    std::map<std::string, std::string> anims; // name -> art path (no .ani)
    std::string fullPal;
    std::vector<std::string> slots[4]; // shirt, pants or skirt, hair, skin palettes
    // Watching an exhibit: [<kind>\BehaviorSetGawk] f = fPlaySetProb(bGawk1,
    // <w>, bGawk2, <w>, bGawk3, <w>): gawk, camera, stand
    int gawkWeights[3] = {45, 10, 45}; // gawk, camera, stand
    // Looking at scenery (guests.ai [<kind>\BehaviorSet\bGawkObj1..6]: its
    // fPlaySetProb weights of bGawk1, bGawk2, bGawk3), by the object's
    // EstheticBonus band: <0, 0-5, 6-10, 11-15, 16-25, 26-100
    int gawkObjWeights[6][3] = {{45, 10, 45}, {45, 10, 45}, {45, 10, 45}, {45, 10, 45}, {40, 40, 20}, {30, 60, 10}};
    // Touching a live electric fence instead of watching (zoo.exe
    // 0x425af4): cTestFenceChance in 100, cZapHappinessHit when zapped
    int testFenceChance = 0, zapHit = 0;
    // Tapping a tank's glass while watching (zoo.exe 0x425a85: cTapWallChance,
    // ZTGuestType +0x2c8, 0 by default - 0x4c49dc)
    int tapWallChance = 0;
    // A price judged (zoo.exe 0x619245 / 0x533480: guest type +0x244
    // cPriceHappy1Change, +0x248 cPriceAngry1Change - guestTypeLoad 0x4c5102)
    int priceHappy = 0, priceAngry = 0;
    // Eating standing up, no seat found (zoo.exe Task6d_EatStanding::begin
    // 0x52dd30: guest type +0x2ac, cStandAndEatChange)
    int standAndEat = 0;
    // Its bTapTank set: [<kind>\BehaviorSet\bTapTank] f = fPlay(<anim>), in
    // order (guests.ai: tap, peek, map; the children no map)
    std::vector<std::string> tapSet;
    // Its [<kind>/Characteristics/Integers] (zoo.exe ZTGuestType::guestTypeLoad
    // 0x4c4cb9; a kind's missing keys are the DefaultSubtype man's): each
    // check's period in status ticks, the needs' increments, thresholds and
    // happiness at their limit, leaving, souvenirs and surroundings
    int hungerCheck = 30, thirstCheck = 30, bathroomCheck = 30, souvenirCheck = 40, energyCheck = 30,
        trashCheck = 30, chaseCheck = 5, leaveCheck = 20, viewingCheck = 20, environmentCheck = 20;
    int hungerIncrement = 4, thirstIncrement = 3, bathroomIncrement = 3, energyIncrement = 3;
    int hungerThreshold = 50, thirstThreshold = 50, bathroomThreshold = 50, energyThreshold = 50;
    int angryFood = -30, angryThirst = -30, angryBathroom = -30, angryEnergy = -15, maxEnergy = 100;
    int leaveLow = 12, leaveMed = 24, leaveHigh = 30, leaveDone = 60;
    int souvenirMed = 5, souvenirHigh = 10, angryTrash = -5, escapedChange = -100, initialHappiness = 50;
    int estheticThreshold = 200, happyEsthetic = 5, crowdedThreshold = 75, crowdedChange = -3;
    int stinkThreshold = 2, sickChange = -40;
    // Watching an exhibit (zoo.exe guestViewingTick 0x43afde, Task70's
    // gawk time 0x42e793): cSickAnimalChange, cHappyAnimalChange1/2,
    // cAngryAnimalChange1/2/3, cDifferentSpeciesThreshold/Change; its
    // cGawkTime list by the exhibit's suitability, cGawkAttrTime by its
    // attractiveness
    int sickAnimalChange = -20, happyAnimal1 = 10, happyAnimal2 = 5, angryAnimal1 = -10, angryAnimal2 = -25,
        angryAnimal3 = -35, speciesThreshold = 4, speciesChange = 5;
    std::vector<int> gawkTime{10, 15, 25, 35, 45}, gawkAttrTime{0, 1, 4, 7, 10, 20};
    int sawReset = 20; // cSawAnimalReset
    // Sick from a smell (zoo.exe chooseTask 0x5b0509, Task79_GuestSick):
    // cSickChance in 100 each time it picks what to do next; drawn with
    // cSickPalName's skin while it stands in the stink (class slot 0x50)
    int sickChance = 20;
    std::string sickPal;
    // Mimicking an animal it has seen while watching (zoo.exe 0x425a15:
    // cMimicChance, ZTGuestType +0x2bc)
    int mimicChance = 15;
  };
  // (Water: out on the water - a ride from a return building, swimming out,
  // drowning; Guest::waterTask says which)
  //
  // What a guest is doing. The original gives each unit a task object made
  // by its number (unitMakeTask), each its own class with the same
  // plumbing: a constructor (the unit, +4, and its task state, unit+0x15c,
  // +8; the unit kept at +0x14 only if its type isKindOf 0x6386b0), a
  // destructor (its class table set back, then the base task's 0x6323c4;
  // freed when asked), its number (returned by its taskId, the status line
  // and the "in a building" test 0x4108f7 read it), and the slots run by
  // every task's tick (begin, whileGoing, checkArrived, atTarget). Ours keeps
  // a State and the guest's own fields instead; the classes' plumbing read
  // 2026-11-17 (each ctor, dtor and taskId as said; each destroy sets its
  // own class table, then the base's, and each freeIfAsked deletes it when
  // its argument's bit 1 is set - read 2026-11-18), and where each is:
  //   0x64 Task64_PickAttraction (ctor 0x440fb9, dtor 0x422886, destroy
  //     0x422890, freeIfAsked 0x42289d, taskId 0x4281f7): Guests::decide's
  //     pick (guestPickAttraction 0x4282e1).
  //   0x65 Task65_Guest (ctor 0x413ed7, dtor 0x4260f5, destroy 0x4260ff,
  //     freeIfAsked 0x42610c, taskId 0x436800 - shared with the keepers'):
  //     going to an exhibit's viewing tile (whileGoing 0x43aab4, atTarget
  //     0x42612f: task 0x70) - Guests::update's walk to Guest::target.
  //   0x66 Task66_BuildingCrowded (ctor 0x4a8162, dtor 0x42a49b, destroy
  //     0x42a4a5, freeIfAsked 0x42a4b2, taskId 0x4124c6): seekBuilding's
  //     crowded building (10243).
  //   0x67 Task67_GoToBuilding (ctor 0x4a826d, dtor 0x42c836, destroy
  //     0x42c840, freeIfAsked 0x42c84d), 0x68 Task68_EnterBuilding (ctor
  //     0x42c7ca, dtor 0x42a4d5, destroy 0x42a4df, freeIfAsked 0x42a4ec),
  //     0x69 Task69_ExitBuilding (ctor 0x42a8f0, dtor 0x42a549, destroy
  //     0x42a553, freeIfAsked 0x42a560), 0x6a Task6a_InsideBuilding (ctor
  //     0x4a8201, dtor 0x42a50f, destroy 0x42a519, freeIfAsked 0x42a526):
  //     Guest::building and State::Using.
  //   0x6b Task6b_GoEat (ctor 0x59d3e6, dtor 0x59d3ac, destroy 0x59d3b6,
  //     freeIfAsked 0x59d3c3, taskId 0x59d57a), 0x6c Task6c_Eat (ctor
  //     0x52dc85, dtor 0x52d967, destroy 0x52d971, freeIfAsked 0x52d97e,
  //     taskId 0x52d94f), 0x6d Task6d_EatStanding (ctor 0x52de0e, dtor
  //     0x52da06, destroy 0x52da10, freeIfAsked 0x52da1d, taskId 0x52da00):
  //     Guest::eatStep.
  //   0x70 Task70_ViewAnimals (ctor 0x4a837b, dtor 0x4261cf, destroy
  //     0x4261d9, freeIfAsked 0x4261e6, taskId 0x423214): State::Gawking at
  //     an exhibit.
  //   0x71 Task71_LeaveAnimalsFree (ctor 0x4a6758, dtor 0x4fa2e4, destroy
  //     0x4fa2ee, freeIfAsked 0x4fa2fb, taskId 0x4233e4): Guest::leaving.
  //   0x76 Task76_GuestFollowGuide (ctor 0x58d276, dtor 0x58d2e2, destroy
  //     0x58d2ec, freeIfAsked 0x58d2f9, taskId 0x58ce66 - shared with the
  //     guide's): Guest::guide, joinTour.
  //   0x79 Task79_GuestSick (ctor 0x5b06c2, dtor 0x5b05a4, destroy 0x5b05ae,
  //     freeIfAsked 0x5b05bb, taskId 0x5b05de): Guest::puking.
  //   0x7f Task7f_GuestInWater (ctor 0x49a5ad, dtor 0x49a432, taskId
  //     0x4994bd), 0x84 Task84_Drown (ctor 0x61a68a, dtor 0x61a6dc, taskId
  //     0x61a6d6; destroy 0x61a6fa, its own class table 0x633fec), 0x83
  //     Task83_InWater (dtor 0x5a5d2b, destroy 0x5a5d35, freeIfAsked
  //     0x5a5d42, taskId 0x5a5d03), 0x82 the ride (class table 0x6335d4: dtor
  //     0x5a058e, destroy 0x5a0598, freeIfAsked 0x5a05a5, taskId 0x59fe50):
  //     State::Water, Guest::waterTask (kOutOfWaterTask, kDrownTask,
  //     kInWaterTask, kRideTask).
  //   0x81 Task81_GawkObject (ctor 0x4a6bbd, dtor 0x441256, destroy 0x441260,
  //     freeIfAsked 0x44126d, taskId 0x441216): Guest::gawkObject.
  // (zoo.exe ZTGuest::debugText 0x619050, class slot 0x64: a developer's
  // overlay, drawn only while zooTime +0x100 is set - "Hap:%d Hun:%d Ths:%d
  // Bth:%d Eng:%d NonPath:%d" from +0x2a8, +0x2b0, +0x2b8, +0x2c0, +0x2c8
  // and slot 0x194, white, then the unit's own text; as the staff's
  // 0x610f8f, ours has no counterpart: the needs are Guest's own fields)
  enum class State { Arriving, Walking, Gawking, Leaving, Fleeing, Using, Caught, Water };
  // Its tasks on the water, by the original's task numbers (unitMakeTask):
  // 0x82 a ride (Task82, class table 0x6335d4), 0x66 back to a return
  // building's water door (guestGoToBuilding 0x43b2f6 on a ride), 0x83 in
  // the water, making for land (Task83_InWater), 0x84 drowning
  // (Task84_Drown), 0x7f in the water and not on a ride: out (Task7f)
  static constexpr int kEatWalkAbout = -2;
  static constexpr int kRideTask = 0x82, kRideBackTask = 0x66, kInWaterTask = 0x83, kDrownTask = 0x84,
                       kOutOfWaterTask = 0x7f;
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
    // Where it has been (zoo.exe +0x260, guestRecordVisit 0x4a840e): the
    // last 10 places, oldest first, each a tile - one of an exhibit's, a
    // building's, a scenery object's - and its visits since the list was
    // last emptied (+0x334, a word: Task64's "seen everything")
    std::vector<std::pair<int, int>> visits;
    int visitCount = 0;
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
    int showTank = -1, showStand = -1, showSeat = -1, showRating = 0;
    float showDoorX = 0, showDoorY = 0, showZ = 0, showExit = 0, showExitLength = 0;
    std::string need;
    bool hidden = false;      // inside it
    bool trash = false;       // holding a wrapper or cup
    uint32_t inventory[5] = {}; // original item name ids; 0 is an empty slot
    bool sick = false;
    int trashCheck = 0, souvenirCheck = 0;
    // Ticks to its next look at its surroundings (zoo.exe +0x310); just done
    // with something - out of a building, done watching, no seat: its needs
    // looked at on the next tick without waiting for their checks (+0x33e)
    int environmentCheck = 0;
    bool recheck = false;
    bool smelled = false; // made sick by its block's stink, until out of it (+0x336)
    // A predator seen (zoo.exe +0x24c) and the one it's fleeing (an animal
    // id, +0x22c); ticks to its next calming down (+0x314, cSawAnimalReset)
    bool sawPredator = false;
    bool puking = false; // being sick (Task79: bSick, its puke animation)
    // Eating standing up (Task6d_EatStanding: its bEatStanding set - 0 the
    // eatstanding play, 1 the idle after; -1 not); kEatWalkAbout: no seat
    // for its food, on its walk about first (Task6c_Eat)
    int eatStep = -1;
    // ours: loaded holding food (our saves keep no task: a guest holding
    // food in the original is inside the stand or off to eat it - Task6b),
    // so it goes to eat it at its first pick. Not saved
    bool eatAfterLoad = false;
    // The slot it took at a building (zoo.exe +0x24d, guestGoToBuilding's
    // 0x42c02b), -1 none; in a building's b<name><goal><slot> set (a bench,
    // a picnic table, the swing, the ring toss ...), the line it's at (the
    // play under way; -1 not); taken in as its user yet (the set's
    // fEnterBuilding, 0x5ad3a6); playing it backwards (fPlayReverse)
    int slot = -1;
    int sitStep = -1;
    bool takenIn = false, reverse = false;
    int predator = -1;
    int sawResetCheck = 20;
    bool souvenir = false;    // has one
    // wants one: set when a souvenir check decides so (10090), cleared at the
    // next check or when a gift's bought (zoo.exe +0x33c)
    bool wantsSouvenir = false;
    int gawkObject = -1;      // the scenery it's going to look at (object id)
    float gawkSeconds = 0;    // how long it's been watching
    // Watching an exhibit (Task70_ViewAnimals): the way of its tile's slot
    // that holds its viewing area (zoo.exe +0x328, -1 none: 0x43b8a2); when
    // it began (+0x298, 0x42e8ce) and when its gawk time is up (game
    // seconds); the kinds of animal its areas have shown it lately, their
    // cNameIDs, oldest first, 5 at most (+0x278, 0x42e527)
    int viewWay = -1;
    double viewStart = 0, viewEnd = 0;
    std::vector<int> recentSpecies;
    // Playing its bTapTank (zoo.exe 0x425a85): which of the set's fPlays
    // (tap, peek, map), -1 not tapping; the game time towards the next
    // update (the original's whileGoing looks again each update while it
    // taps: 0x42325d)
    int tapStep = -1;
    float tapUpdateMs = 0;
    int lastThought = 0;      // lang string
    std::string thoughtArg;
    std::vector<std::string> thoughts; // newest first (Guest Information)
    std::string name;         // "Guest 48"
    double arrived = 0;       // seconds of play
    // On a tour guide's tour ("Following Tour Guide"), and whether its talk
    // has cheered it yet
    int guide = -1;
    bool tourHeard = false;
    // Attacked by a loose predator (never twice); while caught, its
    // bCaughtBy set's lines (the next one to run) and its attacker (zoo.exe
    // +0x22c, the predator's id; -1 once fShowPredator has let it go) - not
    // saved
    bool attacked = false;
    std::vector<std::string> caughtLines;
    size_t caughtLine = 0;
    int attacker = -1;
    // The rename cheats' flags (zoo.exe +0x340, not saved): Original::Cheat's
    // stinky, rpro, boogyman, yellowman, redman - Guests::cheatTick
    int cheatFlags = 0;
    bool stinkSick = false; // (being sick from a "Stinky" guest: its bSick, no task)
    // Marine Mania's water rides (zoo.exe guestBuy 0x42d858 on a cReturnBuilding):
    // the ride's building type (its cNameID, ZTGuest +0x284's low bits; 0
    // none), its behaviour set (+0x288: "breturnboatshak1"), in the water
    // (+0x284's 0x80000, Task83), the building it came out of (an object id)
    int ride = 0;
    std::string rideSet;
    bool inWater = false;
    int rideFrom = -1;
    // Its task on the water (State::Water: kRideTask...) and the task's own:
    // a ride's legs left (Task82 +0x18), idling between them (+0x20 cIdler,
    // +0x21 idling now, +0x1c until when, game seconds), looking at
    // exhibits (+0x22 cExhibitViewer); Task83's tries (+0x18); a drowning's
    // plays done
    int waterTask = 0, legs = 0, tries = 0, drownStep = 0;
    bool idler = false, idling = false, viewer = false;
    double idleUntil = 0;
    // ours: its art for the animation last asked for (Guests::art), for its
    // type and clothes, while the art cache has let none go (artGen)
    mutable Animation *artMemo = nullptr;
    mutable std::string artMemoName;
    mutable int artMemoType = -1, artMemoColours[4] = {};
    mutable uint32_t artMemoGen = 0;
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
  // "Ouch! I've just been attacked by %s." - it lives (a man-eater's: eaten
  // at the set's fShowPredator)
  int nearestPrey(float x, float y, float radius) const;
  bool preyPosition(int guest, float &x, float &y) const;
  void caught(int guest, int animal, const std::string &animalKey, bool female, const std::string &animalName);
  // Its set's fHidePredator (zoo.exe 0x50e879) and fShowPredator (0x50e764)
  // lines: the attacker hidden; the attacker shown and let go, the guest
  // eaten by a man-eater (World wires them to Animals)
  std::function<void(int animal)> hidePredator;
  std::function<void(int animal, int guest)> showPredator;
  // Whether a guest is still in the set it was caught into by that animal
  bool heldBy(int guest, int animal) const;
  // Taken away (zoo.exe unitMarkRemoved 0x4fa78e(1): +0x13c, removed at the
  // sweep 0x434df7) - fKillPrey's guest. ours: out of the zoo, as drowned
  void takeAway(int guest);
  void releaseTour(int guide);
  void tourTalk(int guide, int bonus);
  std::function<bool(int guide)> guideSpeaking;
  // A guest come in at the gate (zoo.exe 0x4f6e3c ends with 0x4f7ebe, the
  // special days' flyers)
  std::function<void()> onArrive;
  // A guest taken in as a building's user (buildingAddUser 0x42ce0a): the
  // building's use sound (0x42cc1a: World::startUseSound)
  std::function<void(int building)> useSoundStart;
  // A building wrecked (zoo.exe buildingPutGuestsOut 0x5ae745, from
  // ZTBuilding::toRubble 0x611bd6): every guest inside put out now, as if
  // its time inside were over (guestBuy 0x42d858 with 1: paid, judged, its
  // effects; one about to go out on its water ride doesn't go). ours: by the
  // placed objects' index - the ones heading for it look again, those after
  // it move down one (it's taken out of the list next); how many were put out
  int buildingGone(int index);
  int putOut(int index); // (0x5ae745: those inside only)
  std::function<int(int guide)> guideBonusOf;    // its cTourGuideBonus
  std::function<int(int exhibit)> tourBonusAt;   // a guide on the exhibit's viewing tiles
  // An exhibit's viewing areas (zoo.exe: the habitat's +0x34 list - World's
  // ViewingAreas), in the order they were made: each one's viewing tiles,
  // the exhibit tiles it sees and the guests standing on its viewing tiles
  struct ViewArea {
    std::vector<std::pair<int, int>> viewing, seen;
    int guests = 0;
    int exhibit = -1, guideBonus = 0; // (areaInSlot: its exhibit, a guide's cTourGuideBonus there: +0x58)
  };
  std::function<std::vector<ViewArea>(int exhibit)> viewAreas;
  // The viewing area in a tile's slot for one of the 8 ways (zoo.exe
  // 0x43b8a2: the habitat manager's grid [0x63805c]+0x28), false none
  std::function<bool(int x, int y, int way, ViewArea &out)> areaInSlot;
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
  // (where it is: false, none)
  bool entrance(int &x, int &y) const {
    x = this->entranceX;
    y = this->entranceY;
    return this->entranceX >= 0;
  }
  // A building's door tiles as it stands (its entrpos turned with it; none
  // when its .ai gives no entrpos: any path tile beside it serves)
  std::vector<std::pair<int, int>> doorTiles(const PlacedObjects::Object &o) const;
  bool canWalk(int x, int y) const { return walkable(x, y); }

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
  std::vector<Guest> &mutableGuests() { return this->list; } // (the rename cheats)
  const std::vector<Type> &types() const { return this->typeList; }
  const Guest *guest(int id) const;
  Guest *guest(int id) { return const_cast<Guest *>(static_cast<const Guests *>(this)->guest(id)); }
  // Average happiness, 0..100 as the HUD's bar shows it
  int averageHappiness() const;
  // The guests counted for the advisor (zoo.exe zooRatingUpdate 0x41f881):
  // those in the zoo; those with hunger, thirst, a restroom need or
  // tiredness over cUIThreshold (guests.ai, 75); wanting a gift; holding
  // litter; and their happiness on average (-100..100)
  struct Census {
    int guests = 0, hungry = 0, thirsty = 0, bathroom = 0, tired = 0, gift = 0, trash = 0;
    int happiness = 0;
  };
  Census census() const;
  int uiThreshold = 75;
  // zoo.ini [AI] useAttractionAttractiveness (zoo.exe 0x4bc8f8: read once
  // as the game starts, into [0x63b088]; 0 when it's not there, and the
  // game's zoo.ini doesn't have it): a 'fun' building picked by its score
  // (Original::attractionScore, the furthest), not the one drawn
  bool useAttractionAttractiveness = false;
  // The day's messages ("The entrance fee is a really good value"), taken
  std::vector<int> takeMessages() {
    std::vector<int> m;
    m.swap(this->messages);
    return m;
  }
  // News about one guest for the message bar ("%s is drowning!": zoo.exe
  // 0x4807c2, kind 2), taken
  struct Notice {
    std::string text;
    int kind = 0, guest = -1;
  };
  std::vector<Notice> takeNotices() {
    std::vector<Notice> n;
    n.swap(this->notices);
    return n;
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
  // Picked up with the guest panel's PickupGuest (ZUPDATE1's infogst.lyt):
  // the one held, -1 none; whether it can be put down where it is now, and
  // the message a click there gives (0 none)
  int carried = -1;
  bool carriedFit = false;
  int carriedMessage = 0;
  float carriedFromX = 0, carriedFromY = 0; // where it was picked up
  void pickUp(int id);
  bool canPutDown(float x, float y, int &message) const;
  void putDown(int id, float x, float y);
  // The holding given up (a right click, another tool): put back down where
  // it was picked up (zoo.exe endPlacing 0x443df7)
  void putBack();
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
  // (the ZT1 Lab) A guest's route between two tiles, by its rules (the
  // paths, not into exhibits, no fences or steps): nothing moves
  bool testRoute(int sx, int sy, int gx, int gy, std::vector<std::pair<int, int>> &out) const;
  // (the ZT1 Lab) One of a guest's animations, now; what it has; a need
  // set (happiness, hunger, thirst, bathroom, tired) or it sent home
  void labPlay(int id, const std::string &anim, float seconds);
  std::vector<std::string> labAnims(int id) const;
  void labSet(int id, const std::string &field, float value);
  void labLeave(int id);
  // Its visited list (zoo.exe +0x260): a place pushed on, the last 10 kept,
  // its visit count up by 1 (guestRecordVisit 0x4a840e); an exhibit on it
  // (guestVisitedHabitat 0x42281c: a tile of it); a building or scenery on
  // it - that one or one of its kind (0x4285e2); and the places there are
  // to visit, the exhibits with animals plus the 'fun' buildings (0x4281fd)
  void recordVisit(Guest &g, int x, int y);
  void recordVisit(Guest &g, const PlacedObjects::Object &o);
  bool visitedExhibit(const Guest &g, int exhibit) const;
  bool visitedObject(const Guest &g, const PlacedObjects::Object &o) const;
  int placesToVisit() const;
  // Whether a thing satisfies any need (its .ai's [Satisfies]: zoo.exe
  // keeps such things on its lists 0x639060 - the "The Rock" cheat's)
  bool satisfiesAny(const std::string &file) const { return !infoOf(file).satisfies.empty(); }
  // The rename cheats' guest flags at work, each tick (zoo.exe ZTGuest's
  // update 0x43a62c)
  void cheatTick();

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
  // guests.ai's [BehaviorSet\bCaughtBy<animal><m|f>] sets and the sets
  // their fPlaySetTerrain names: "bcaughtbylionm" -> its lines
  std::map<std::string, std::vector<std::string>> caughtSets;
  void caughtNext(Guest &g);
  // guests.ai's water behaviour sets ("man\behaviorset\breturnchroom0",
  // "behaviorset\binwater", bSuitIdle, bSuitDrown, bSurfDrown): their lines
  std::map<std::string, std::vector<std::string>> behaviourSets;
  // A building as guests use it (its .ai): what it satisfies, sells, its
  // price, how long they're in, and what it does for them
  struct BuildingInfo {
    std::set<std::string> satisfies;
    std::string item;          // the first thing it sells
    // all it sells ([Sells], ZTBuildingType +0x200: one drawn at random a
    // sale, zoo.exe 0x42d234)
    std::vector<std::string> sells;
    float price = 0, priceFactor = 1, low = 0, high = 30;
    // cCommerce (ZTBuildingType +0x23d, loadConfig 0x4c1058): its users pay
    // (guestBuy 0x42d858) and its price is judged at the door (0x42c8af)
    bool commerce = false;
    int capacity = 1;
    float timeInside = 3;
    bool hideUser = false;
    // cHideBuilding (ZTBuildingType +0x1a5, loadConfig 0x4c1431): not drawn
    // while it has a user (ZTBuilding::draw 0x433aaf at 0x4413a5)
    bool hideBuilding = false;
    // cUserStaysOutside (ZTBuildingType +0x1a6; +0x194 is cHideUser,
    // loadConfig 0x4c1402/0x4c144c) and its type's name (+0xa4, its .ai's:
    // "bench", "picnic" - Task68's behaviour set is named from it)
    bool staysOutside = false;
    std::string typeName;
    float adultChange = 0, childChange = 0, hunger = 0, thirst = 0, bathroom = 0, energy = 0;
    // its item's
    float itemAdult = 0, itemChild = 0, itemHunger = 0, itemThirst = 0, itemBathroom = 0, itemEnergy = 0;
    bool itemTrash = false;
    // (its nameID, what a guest's hands hold - ZTGuest +0x26c; consumenow,
    // item +0x1c; removewhenconsumed, +0x1e: ZTShopItem's loader 0x464f00)
    uint32_t itemId = 0;
    bool itemNow = true, itemRemove = true;
    int usedThought = 0, consumedThought = 0;
    // scenery guests come to look at: cEstheticWeight (over 0: one of them),
    // cGawkOnlyFromFront, and [EstheticBonus] by the guest kind's cNameID
    int estheticWeight = 0;
    bool gawkFront = false;
    std::map<int, int> estheticBonus;
    std::string useSound;
    int useAtten = 0;
    int fx = 2, fy = 2;
    // Its doors: each [slot]'s entrpos, from its middle facing the way it
    // was built (64 a tile; ZTBuildingType::loadConfig 0x4c0b4d)
    std::vector<std::pair<int, int>> doors;
    struct DrawSlot { float x, y, z; };
    std::vector<DrawSlot> drawSlots;
    // A water ride (Marine Mania; ZTBuildingType::loadConfig 0x4c0b4d):
    // cNameID; cReturnBuilding (+0x240), cIdler (+0x19c), cExhibitViewer
    // (+0x19d), cUserTracker (+0x19b), cStrictBuildingCapacity (+0x250);
    // each [slot]: its entrpos and exitpos (64 a tile, turned with it),
    // exitonwater, entranceonwater, isreturnslot, and its id list (the
    // guest kinds' cNameIDs, read at 0x4c08dc)
    int nameId = 0, strictCap = 0;
    bool returnBuilding = false, idler = false, viewer = false, tracker = false;
    struct Slot {
      int ex = 0, ey = 0, xx = 0, xy = 0;
      // slotpos (the spot inside where the user plays its set, 64 a tile)
      // and facing (0-7, turned with the building: 0x5ad288)
      int sx = 0, sy = 0, facing = 0;
      bool exitOnWater = false, entranceOnWater = false, isReturn = false;
      std::vector<int> ids;
      // all its entrpos pairs (the swing's and the bounce house's slot has
      // two, either side; ex, ey is the first)
      std::vector<std::pair<int, int>> entrances;
    };
    std::vector<Slot> slots;
  };
  // A move on the water from a behaviour set's line (guests.ai
  // fMoveWithSound(0,0,<anim>,<speed>,<sound>), fPlayTime(<anim>, <s>),
  // fPlay / fPlayWithSound): its animation, speed (a rate, as cSlowRate),
  // sound, seconds
  struct Move {
    std::string anim, sound;
    int speed = 0;
    float seconds = 0;
    std::string fn; // the line's function (fplay, fenterbuilding ...)
  };
  std::vector<Move> behaviour(const Guest &g, const std::string &set) const;
  std::vector<Notice> notices;
  // The water: a lake tile out of the exhibits (terrain 9 or 10); a route
  // over it to a tile (the last may be land); the lake's size from a tile
  // (as many as 100); a building slot's point in the world
  bool swimmable(int x, int y) const;
  bool waterTo(Guest &g, int tx, int ty, const Move &m);
  int waterSize(int x, int y) const;
  void slotPoint(const PlacedObjects::Object &o, int px, int py, float &x, float &y) const;
  // The rides and the water (Task82, guestGoToBuilding on a ride, Task83,
  // Task84, Task7f): out onto it, the next leg, back, making for land,
  // drowning, getting out
  void rideOut(Guest &g, const PlacedObjects::Object &o, const BuildingInfo &b);
  void rideLeg(Guest &g);
  void rideBack(Guest &g);
  void inWater(Guest &g);
  void drown(Guest &g);
  void outOfWater(Guest &g);
  void enterWater(Guest &g);
  void landed(Guest &g);
  bool nearNeedBuilding(Guest &g);
  void waterNext(Guest &g);
  void endRide(Guest &g);
  mutable std::map<std::string, BuildingInfo> buildingInfo;
  // An item (items/<name>.cfg; its fields the item* ones), and its nameID to
  // its name (what a held item is)
  mutable std::map<std::string, BuildingInfo> itemInfo;
  mutable std::map<uint32_t, std::string> itemNames;
  const BuildingInfo &itemOf(const std::string &name) const;
  const BuildingInfo &infoOf(const std::string &file) const;
  // Scenery to look at (zoo.exe guestPickAttraction 0x4282e1's third
  // group): how much it weighs in the guest's choice, the one it picks and
  // where it stands, and looking at it when there
  int sceneryWeight(const Guest &g) const;
  int pickScenery(Guest &g, int &tx, int &ty);
  void gawkScenery(Guest &g);
  // (only: that building alone - the one drawn - with no room or radius look)
  bool seekBuilding(Guest &g, const std::string &need, int radius, bool crowdedThought = true, int only = -1);
  void useBuilding(Guest &g);
  // Taken in as a building's user (zoo.exe buildingAddUser 0x42ce0a): its
  // visitors counted, the building's cUsedThought, and what it buys, wins
  // or eats there
  void addUser(Guest &g, PlacedObjects::Object &o, const BuildingInfo &b);
  // What a guest buys or holds (zoo.exe holdItem 0x42d54e, guestConsumeItem
  // 0x42d380, 0x42d6aa): the hand an item kind goes in; into its hands;
  // eaten, drunk or used; going off to eat what it holds (Task6b_GoEat)
  static int handFor(const std::string &kind);
  bool holdItem(Guest &g, const BuildingInfo &b, const std::string &kind = std::string());
  void consumeItem(Guest &g, const BuildingInfo &item);
  const BuildingInfo *heldItem(uint32_t id) const;
  void goEat(Guest &g);
  // No seat for it: eats standing up where its walk about ended
  // (Task6d_EatStanding::begin 0x52dd30)
  void eatStanding(Guest &g);
  // A show to go to, at chooseTask's place (zoo.exe 0x440eb7 -> 0x440f0c):
  // set by Shows::init; true when the guest is off to a grandstand
  std::function<bool(Guest &)> showPick;
  // A guest's walk about (zoo.exe unitWalkAbout 0x43d948, its passable
  // test ZTGuest's class slot 0x168): false when no route was made
  bool walkAbout(Guest &g);
  // Out of the building it was in; true: gone on at once (its ride, to eat)
  bool outNow(Guest &g);
  bool forcedOut = false; // (buildingGone: put out, guestBuy 0x42d858 with 1)
  // A building's name as a thought gives it (its own, else its kind's
  // cNameID text)
  std::string buildingName(const PlacedObjects::Object &o) const;
  // A building's set for a goal and slot (zoo.exe 0x5ad535): its name,
  // b<name><goal><slot> ("bbencheat0", "bswingfun0"); empty where the
  // building hides its user or keeps it outside, or the kind hasn't it
  std::string buildingSet(const Guest &g, const BuildingInfo &b, const std::string &goal, int slot) const;
  // Its building set's lines from g.sitStep on: true while one plays; false
  // when the set is over (fExitBuilding: out at the slot's exitpos)
  bool buildingSetLine(Guest &g);
  // The buildings hidden while used (cHideBuilding) last frame
  std::vector<int> usedHidden;
  // A point of a building's (64 a tile, across and along it), in the world
  static void buildingPoint(const PlacedObjects::Object &o, int px, int py, float &x, float &y);
  // The price it charges a guest (its +0x1ac: its own, else the type's
  // default) and the default it's judged against (cDefaultCost, +0x178)
  float priceOf(const PlacedObjects::Object &o, const BuildingInfo &b) const;
  bool positionShowSeat(Guest &g, const PlacedObjects::Object &stand, int seat) const;
  std::vector<Type> typeList;
  std::vector<Guest> list;
  int nextId = 1;
  int entranceX = -1, entranceY = -1;
  float spawnClock = 0;
  double income = 0;
  int arrivals = 0;
  std::vector<int> messages;
  mutable std::map<std::string, Animation *> artCache;
  float artSweepIn = 15.0f; // (game seconds to the next freeing of art no guest wears)
  uint32_t artGen = 1; // (moved on as art's freed: each guest's artMemo)
  Animation *artUncached(const Guest &g, const std::string &name) const;
  // The colours it's drawn in now: its own, the skin cSickPalName's while
  // smell-sick (kSickSkin in the skin slot)
  void wornColours(const Guest &g, int out[4]) const;
  // ours: each guest animation loaded once in a palette whose every entry is
  // a colour of its own (by its type's full palette and path); an outfit is
  // a copy of it with the outfit's colours (Animation::recolouredCopy) - a
  // new guest's art was read and decoded again for every outfit, 2-4 ms a
  // guest and most of a zoo's opening. Never drawn itself
  mutable std::map<std::string, std::shared_ptr<Animation>> indexArt;
  Animation *outfit(const Type &t, const int colours[4], const std::string &path, bool copied) const;
public:
  // (checks) an outfit made as a copy and loaded as before: the same art
  std::string outfitCheck() const;
private:
  Animation *selectArrow = nullptr;
  std::mt19937 rng{1977};
  // guests.ai [Characteristics/Integers] (zoo.exe ZTGuestType::loadClassConfig
  // 0x52196e): cSearchRadius, and cRandomStatNth with the ranges a new
  // guest's one randomised need starts in (hunger, thirst, energy, bathroom)
  int searchRadius = 40, randomStatNth = 2;
  int randomMin[4] = {41, 41, 0, 0}, randomMax[4] = {50, 50, 0, 0};
  // Animal types never a guest's favourite (cIsSpecialAnimal: zoo.exe
  // 0x4f60f5 skips them), by index; worked out for this many types
  std::set<int> specialAnimals;
  size_t specialFor = 0;
  // The 5x5-tile blocks guests judge where they are by (zoo.exe
  // blockGridRefresh 0x41ffdc, every 7.5 s): the guests in each, the
  // EstheticBonus of what stands there by guest kind (cNameID), its stink
  struct Block {
    int guests = 0;
    std::map<int, float> esthetic;
    float stink = 0;
  };
  std::vector<Block> blocks;
  int blocksW = 0, blocksH = 0;
  float blockMs = 0;
  int dungStink = 1; // scenery/other/poo.ai cStink
  bool seekFull = false; // (the last seekBuilding found only full ones)
  // ztunit.ai [Characteristics/Integers]: cPredatorRadius (tiles), and what
  // a step off the paths costs a route, ordinarily and running for its life
  int predatorRadius = 7, nonPathCost = 5, nonPathCostEmergency = 1;
  int findPredator(const Guest &g) const; // (an animal id, -1 none)
  // How crowded the viewing area it stands in is (zoo.exe 0x425f6a)
  int areaCrowd(const Guest &g) const;
  // What the viewing area it stands in sees (zoo.exe 0x4232f4 -> the
  // area's counts, 0x447a00): the animals on its seen tiles, their kinds,
  // the sick among them; false: it stands in none of its exhibit's areas
  struct Sight {
    int animals = 0, species = 0, sick = 0;
  };
  bool areaSight(const Guest &g, Sight &out) const;
  // Its viewing area (zoo.exe 0x4232f4: its tile's slot for the way kept in
  // +0x328); finding one from a random way (0x43b92e); false none
  bool viewArea(const Guest &g, ViewArea &out) const;
  bool findViewArea(Guest &g);
  // The kinds of animal an area sees, each once, in the order its seen
  // tiles show them (0x447a00: +0x24), their cNameIDs
  std::vector<int> areaKinds(const ViewArea &a) const;
  // Task70_ViewAnimals: begun at the viewing tile (0x42e527: false, it
  // can't - no area there); its next play while its gawk time lasts
  // (whileGoing 0x42325d: mimic, test the fence, else bGawk again); done
  // (atTarget 0x42e9f9 -> 0x42ea14)
  bool viewBegin(Guest &g);
  void viewNext(Guest &g);
  void viewDone(Guest &g);
  void playGawk(Guest &g);
  bool mimic(Guest &g);
  // zoo.exe 0x425a85: maybe tap a tank's glass on a side of its tile (true
  // when it taps, or goes on tapping)
  bool tapTank(Guest &g);
  // The bTapTank set's next fPlay (tap, peek, map - those its art has);
  // false when the set is done
  bool tapPlay(Guest &g, int step);
  // One of an exhibit's animals whose happiness is lo..hi, at random
  // (zoo.exe 0x426049; -1 none)
  int pickAnimal(int exhibit, int lo, int hi);
  // How long it watches (zoo.exe 0x42e793), in seconds
  int gawkSeconds(const Guest &g);
  bool fleeGround(int x, int y) const;
  void refreshBlocks();
  const Block *blockAt(float x, float y) const;
  void surroundings(Guest &g);
  // crowd sounds
  int bedLevel = -1, bedChannel = -1;
  float chatterClock = 0;
  std::map<std::string, int> soundAtten;
  std::map<std::string, std::string> soundFile;

  bool walkable(int x, int y) const;
  bool canStep(int x, int y, int nx, int ny) const;
  bool walkTo(Guest &g, int tx, int ty, float speed, bool emergency = false);
  float walkSpeed(const Guest &g) const;
  void decide(Guest &g);
  void tick(Guest &g);
  void think(Guest &g, int stringId, const std::string &arg = "", int animal = -1, int exhibit = -1);
  int pickExhibit(Guest &g, int &vx, int &vy);
  void viewing(Guest &g);
  void wander(Guest &g);
  void play(Guest &g, const std::string &anim, float seconds);
  // Come to an exhibit: maybe it touches the live electric fence there
  // instead of watching (zoo.exe 0x425af4); true when it did
  bool testFence(Guest &g);
  float groundAt(float x, float y) const;
  CompassDirection facing(const WorldRenderer &view, const Guest &g) const;
};

#endif // GUESTS_HPP
