#ifndef ANIMALS_HPP
#define ANIMALS_HPP

#include <functional>
#include <map>
#include <random>
#include <string>
#include <deque>
#include <set>
#include <vector>

#include <SDL3/SDL.h>

#include "Animation.hpp"
#include "Fences.hpp"
#include "OriginalConstants.hpp"
#include "OriginalConstants.hpp"
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
  friend std::string runParityChecks(class World &, ResourceManager *);
  friend std::string runPanelMessageChecks(class World &, ResourceManager *, int &, int &);
  friend class Shows; // Shared marine-show tasks use the normal routes and animation queues.
  friend std::string runShowPolishChecks(class World &, ResourceManager *, bool);
public:
  // Shared with the Lab checker: unsupported steps cannot be mistaken for handled ones.
  enum class Command { Unknown, EndTrick, Play, PlayWithSound, PlayTime, PlayPingPong, PlayReverse, Halt, SetBase, KeepControl, Die, PlaySet, PlaySetTank, PlaySetProb, PlaySetTerrain, Walk, Move, Run, Trot, MoveWithSound, MoveReverse, FaceTowardFood, Hatch, Dustball, EnterBuilding, ExitBuilding, CaughtPrey, FaceTowardTarget, PlayStopAtEnd, TargetBuilding, PlaySpecial };
  static Command behaviorCommand(const std::string &name);
  struct Step;
  // A [BehaviorSet] line read as zoo.exe's parser 0x437d9b reads it
  static bool parseLine(const std::string &text, Step &out);
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
    // Its name: cNameID's text, or an APE download's own cName (CatalogItem::name)
    std::string displayName;
    bool dinosaur = false; // [Member] dinosaur: scientists look after it, not keepers
    // Speeds (cSlowRate, cMediumRate, cFastRate) in tiles a second, by the
    // rule every unit moves by (Original::unitTilesPerSecond: 30 is 2 steps
    // of 1/64 tile an update, 0.466 tiles a second)
    float slow = 0.5f, medium = 0.8f, fast = 1.1f;
    int initialHappiness = 50;
    int hungerThreshold = 50, hungerIncrement = 10, neededFood = 100, foodEaten = 20;
    int foodUnitValue = 1; // cFoodUnitValue: hunger a unit of chow takes away
    int noFoodChange = -50;
    int energyIncrement = 10, maxEnergy = 100;
    bool swims = false;
    // Marine (Marine Mania): cUnderwater (it can live in a tank), cDepth (the
    // water it needs, height units: zoo.exe 0x5af392, against the tank's
    // level; type +0x68 - the lowest it swims over the floor, 0x43dc11);
    // cSurface (type +0x17d, 0x402112): swims at the surface only
    bool underwater = false, surfaceOnly = false;
    bool showAnimal = false;
    int skipTrickHappiness = 50, skipTrickChance = 10;
    int needDepth = 0;
    // cTall (unit type +0x58, the loader 0x4b5681): walks over what's
    // walkableByTall (scenery type +0x57), not what's only walkable (+0x56)
    // - 0x4147b5; its young by [y/] (a young plesiosaur isn't tall)
    bool tall = false, youngTall = false;
    // cOnlyUnderwater (+0x17f): a tank's its whole home; without it a
    // cUnderwater animal needs land too (type slot 0xcc, 0x410e41).
    // cDepthMin, cDepthMax (+0x35c, +0x360): the water depth it likes
    bool onlyUnderwater = false;
    int depthMin = 0, depthMax = 0;
    // Air (zoo.exe 0x4389f7, 0x43750c): cBreathThreshold (type +0x1fc, 100
    // unless set) and cBreathIncrement (+0x200, 0) - a whale, seal or
    // penguin goes up for air when its breath gets to the threshold
    int breathThreshold = 100, breathIncrement = 0;
    // The tank water's effects (zoo.exe 0x438a37; defaults setDefaults
    // 0x4b659e): cDepthChange (+0x364) out of cDepthMin..Max; the wrong
    // water, cSalinityChange / cSalinityHealthChange (+0x368, +0x36c);
    // purity under cMurky / cVeryMurky / cExtremelyMurkyWaterThreshold
    // (+0x424..+0x42c: 60, 20, 1), that band's happiness (+0x430..) and
    // health (+0x43c..) change
    int depthChange = -5, salinityChange = -5, salinityHealthChange = -5;
    int murkyThreshold[3] = {60, 20, 1};
    int murkyChange[3] = {-5, -10, -15}, murkyHealthChange[3] = {-5, -10, -15};
    // cKeeper (type +0x2f0): who cares for it - 9500 a zookeeper (unset),
    // 9551 a scientist (dinosaurs), 9552 a marine specialist (tank animals)
    int keeper = 9500;
    // Water (zoo.exe 0x438f6c, every cWaterCheck ticks): a cEnterWaterChance
    // in 100 to swim with more than cWaterNeeded water tiles in its exhibit,
    // cEnterLandChance to come out with more than cLandNeeded land tiles;
    // now and then a drink (cDrinkWaterChance against cChaseAnimalChance,
    // every cBoredCheck ticks) at the nearest fresh water
    int waterNeeded = 0, landNeeded = 0, enterWaterChance = 0, enterLandChance = 0;
    int underwaterNeeded = 0, enterTankChance = 0;
    int drinkWaterChance = 0, chaseAnimalChance = 0;
    // How it rates its exhibit (see Animals.cpp, from zoo.exe): the terrain
    // it wants (percent of the exhibit, or a flat penalty when negative),
    // what it thinks of objects and of the other animals, its trees, rocks
    // and hills
    int genus = 0, family = 0;
    // cLocation (+0x1f4) and cHabitat (+0x1f0): goal rules 4, 5 and 6 count
    // animals by them (zoo.exe 0x41d665, 0x4f0ff2)
    int location = 0, habitatKind = 0;
    int era = 0; // cEra (Dinosaur Digs: 9621 Jurassic, 9623 Ice Age) - goal rule 6/5
    int compatTerrain[18] = {};
    std::map<int, int> suitable, compatible;
    int treePref = 0, rockPref = 0, elevationPref = 0;
    int habitatPreference = 50, pctHabitat = 10;
    int happyHabitatChange = 0, angryHabitatChange = 0, veryAngryHabitatChange = 0;
    int captivity = 0;
    int numberMin = 1, numberMax = 100, numberMinChange = 0, numberMaxChange = 0;
    int habitatSize = 100, animalDensity = 0, allCrowdedChange = 0, otherAnimalAngryChange = 0;
    // cCrowd, cCrowdHappinessChange: the watchers it bears and what more
    // costs it (zoo.exe animal type +0x268, +0x26c; setDefaults 0x4b659e: 40,
    // -1)
    int crowd = 40, crowdHappinessChange = -1;
    bool needShelter = false, needToys = false;
    // cStaySick (zoo.exe type +0x44d, loadConfig 0x4b6c51): healed, it
    // starts bDoneSick at once (Job4_HealAnimal::atTarget 0x5eccd0) - the
    // sharks and big fish, whose sick sets end in fKeepControl
    bool staySick = false;
    // cKeepMoving (zoo.exe type +0x44c, loadConfig 0x4b6c51, key 0x64017c):
    // "perpetual movement" - the sharks and big fish. animalEatFood 0x4fd951
    // eats one mouthful as it gets to the food and neither sets eating
    // (+0x3a9) nor asks for bEat again
    bool keepMoving = false;
    // [Sounds]: name -> its file and attenuation (placesound, pickupsound,
    // and those fPlayWithSound plays)
    std::map<std::string, std::pair<std::string, int>> sounds;
    int facesYOffset = 0; // cFacesYOffset
    int attractiveness = 0; // cAttractiveness (guests come to see it)
    // Getting out (cIsJumper, cIsClimber, cBashStrength, cCrushesFences)
    bool jumper = false, climber = false;
    int bash = 0, crush = 0;
    // A young one's own numbers: zoo.exe keeps a type for each subtype, the
    // young's [y/Characteristics/Integers] over the grown one's (a young
    // T. rex walks at half the pace, can't jump fences, is more attractive)
    struct Age {
      float slow = 0, medium = 0, fast = 0;
      int attractiveness = 0, chaseAnimalChance = 0, bash = 0, testFenceChance = 0;
      bool jumper = false, climber = false;
    } young;
    Age forAge(bool baby) const {
      return baby ? young
                  : Age{slow, medium, fast, attractiveness, chaseAnimalChance, bash, testFenceChance, jumper, climber};
    }
    // Testing a live electric fence (zoo.exe 0x438ebc): a cTestFenceChance
    // in 100 every cZapCheck ticks to try one cTestFenceOffset tiles off; each zap
    // cZapHappinessChange, and it learns: each zap uses up
    // cTestFenceChanceDrop of its cTestFenceChance, under cMinTestFenceChance
    // it stops for good (0x59c4ba)
    int testFenceChance = 0, testFenceChanceDrop = 0, minTestFenceChance = 0, zapHappinessChange = 0,
        testFenceOffset = 1;
    // Eggs (Dinosaur Digs): cLaysEggs - adopted, and born, as an egg
    // (zoo.exe 0x4e2cf8, 0x60da6b) that hatches after cTimeToHatch ticks
    // with at least cMinHatchHits health (0: 60% of cMaxHits - cMinHits)
    bool laysEggs = false, eatsEggs = false;
    // cExpansionID (ZTEntityType::loadConfig 0x4b4903: +0x54, a byte): 1
    // Dinosaur Digs' animals - the "Comet" cheat's (0x607757)
    int expansionId = 0;
    // cBabiesAttack (type +0x3d4, loadConfig 0x4b6c51): its young hunt as
    // its adults do (zoo.exe 0x4391a2)
    bool babiesAttack = false;
    int timeToHatch = 0, minHatchHits = 0, minHits = 0;
    bool eatsPeople = false; // cPrey lists people (9503): guests run from it
    // cIsManEater (type +0x3d5, loadConfig 0x4b6c51 at 0x4b8e17; the T. rex
    // and the sharks): a guest it mauls is eaten at the end of it (0x50e648)
    bool manEater = false;
    // cResetPreyPosition (type +0x3d8, loadConfig at 0x4b8e29; the
    // dinosaurs, the orca, the giant squid): the guest it catches is put
    // where it stands, its way (0x4a6043)
    bool resetPreyPosition = false;
    // cPreattack (ZTAnimalType+0x3d6; the dinosaurs, Smilodon, Macrauchenia):
    // two that both have it stand off before a chase (zoo.exe 0x42fad3)
    bool preattack = false;
    // Rampaging (zoo.exe 0x438514; ZTAnimalType+0x3f4..0x400): at or under
    // cRampageThreshold happiness, a cRampageChance in 100 a tick to start,
    // for cRampageTimeMin..cRampageTimeMax ticks
    int rampageThreshold = 0, rampageChance = 0, rampageTimeMin = 0, rampageTimeMax = 0;
    // Its footprint (cFootprintX across, cFootprintY along the way it faces,
    // half tiles; zoo.exe's type loader 0x4b5681 keeps them at +0xb4,
    // +0xb8): a big one is drawn in strips by it (Animals::collect)
    int footX = 0, footY = 0;
    // Its other footprints, half tiles (zoo.exe class slot 0x94, ZTUnit::
    // footprint 0x41070b and ZTAnimal's 0x410803): cMapFootprint (ZTUnitType
    // +0x150, both ways: which tiles list it), its crate's cBoxFootprintX/Y
    // (ZTAnimalType +0x1d8) and its egg's cEggFootprintX/Y (+0x410)
    int mapFoot = Original::unitMapFootprint;
    int boxX = Original::animalBoxFootprint, boxY = Original::animalBoxFootprint;
    int eggX = Original::animalEggFootprint, eggY = Original::animalEggFootprint;
    // cPrey: the cNameIDs of the animals it hunts, per sex (m, f, y: a
    // lion's list is the lioness's alone; one without its own has the
    // male's)
    std::set<int> preyIds[3];
    // What its keeper puts down (cKeeperFoodType: food.cfg's order,
    // herbchow, carnchow, fruichow, bambchow, graschow, fishchow)
    int keeperFood = 0;
    std::string customFood; // Enabled mod food key, cKeeperFood in Strings.
    // Care (zoo.exe defaults where a file leaves them out)
    int maxHits = 500, pctHits = 20, sickChange = -20, otherSickChange = -10, sickChance = 10;
    // cSickRandomChance (type +0x264, default 10 at 0x4b665e; no stock file sets it)
    int sickRandomChance = 10;
    int hungryHealthChange = 5, sickTime = 5, keeperArrivesChange = 25, keeperArrivesChangeAmphibious = 35;
    int dirtyIncrement = 15, dirtyThreshold = 70, energyThreshold = 70;
    // cMaxDirty, cMinDirty (the dung meter's limits, type +0x250/+0x254), cPooWaterImpact
    // (+0x448): zoo.exe's type defaults (0x4b659e), no stock file sets the first two
    int maxDirty = 100, minDirty = 0, pooWaterImpact = 1;
    // cSlowAnimSpeed, cMediumAnimSpeed, cFastAnimSpeed and their Water ones
    // (type +0x10c..+0x110, +0x154..+0x158; 100 - 0x4b5de3, 0x4b5c71): the
    // walk's, trot's and run's animation rates in percent
    int animSpeed[3] = {100, 100, 100}, animSpeedWater[3] = {100, 100, 100};
    bool dinoDung = false; // cDinoZoodoo: its dung is dinopoo
    int dungSize = 0, youngDungSize = 0; // cSmallZoodoo 1, cGiantZoodoo 2 (the pile's art: small, giant)
    int timeDeath = 5001, deathChance = 100;
    int reproductionChance = 0, reproductionInterval = 0, happyReproduceThreshold = 100,
        offspring = 1, babyToAdult = 0;
    // cMatingType (1: it waits before taking another mate, and minds losing
    // one) and cNoMateChange (zoo.exe setDefaults 0x4b659e: -19)
    int matingType = 0, noMateChange = -19;
    bool smallPoo = false;
    int buildingUseChance = 0;
    // Per subtype (0 m, 1 f, 2 y): animation name -> art path (no .ani)
    std::map<std::string, std::string> anims[3];
    std::map<std::string, std::vector<Step>> sets[3];
    // Derived from the species' declared tricks, including show-object sets.
    std::set<std::string> showSets, showAnimations;
    std::vector<Ambient> ambient;
    std::vector<Ambient> ambientWater; // [AmbientAnimsWater]: in the water
    // In a tank: [AmbientAnimsUnderwater] / [AmbientAnimsSurface], by where
    // it swims
    std::vector<Ambient> ambientUnder, ambientSurface;
    std::string listImage;
  };
  // The original's tasks (unitMakeTask's numbered classes; Guests.hpp lists
  // the guests'). Every one shares the base task (class table 0x6323c4):
  // its deleting destructor 0x604185 (destroy 0x41226c: the base table set
  // back), its number at +0xc (taskId 0x4781e5; the shared 0x67, 0x68,
  // 0x69 and 0x6a, 0x42a0a5, 0x42a783, 0x42a849, 0x42a1d5), begin 0x404a7e
  // (true) or beginClear 0x4a0bbf (the unit's +0x24c, +0x3a4 and +0x22c
  // cleared, its slot 0x258), and its tick 0x43679e (class slot 5: not
  // arrived (slot 26, 0x4367db: state +0x4c) - checkArrived; still the
  // unit's task (slot 32, 0x4367e2: state +0x14 its number) - whileGoing
  // until arrived, then atTarget). The state it reads: setArrived 0x43e99b
  // (+0x4c), target 0x43680e (+0x18), targetId 0x4a4bb0 (+0x1c),
  // targetObject 0x43031a (the world's lookup of +0x1c), targetTile
  // 0x4367f9 (+0x24), setTargetPos 0x42224d (taskStateSetTarget),
  // setTargetB 0x508410, setTargetAndGo 0x4a8b25 and setTargetC 0x4a722c
  // (+0x18 and where to go), setDuration 0x42e8fc (+0x48) and timeUp
  // 0x42323d (zooTime +0xec less the start +0x44 past +0x48), setNextTask
  // 0x422034 (taskStateSet with that number, the target cleared); the
  // shared checkArrived tests: pathDone 0x436806 / checkArrivedPath
  // 0x438481 (reachedPathEnd), checkArrivedNoTarget 0x49939c (no target,
  // slot 14), checkArrivedShowPortal 0x5a2e9e (the path's end: "Reached
  // show portal"). Task 1 (and 0) is the default class 0x63233c, whose tick
  // is Task1::tick 0x436244 (the walk about: walkSomewhere, Staff::wander,
  // Guests::wander). ours: no task objects - each unit keeps its own state
  // and these are its fields and steps. The animals' (each class's ctor
  // keeps the unit at +0x14 only if it isKindOf 0x638690; its destroy sets
  // its own table then the base's, freeIfAsked frees it on its argument's
  // bit 1 - read 2026-11-18):
  //   0x7a Task7a_DrinkWater (ctor 0x4a0b53, dtor 0x4266a6, destroy
  //     0x4266b0, freeIfAsked 0x4266bd, taskId 0x4232bc; begin beginClear):
  //     whileGoing 0x426582 - with no target tile yet (slot 27), 0x4a0c55
  //     the nearest water; checkArrived 0x42655e - on that tile, arrived;
  //     atTarget 0x4265a0 - Member::drinkX, bDrink.
  //   0x72 Task72_FindKeeperFood (ctor 0x4a8ec4, dtor 0x42773f, destroy
  //     0x427749, freeIfAsked 0x427756, taskId 0x438496; begin 0x49fade,
  //     its move's animation by name - slot 0x1b0): whileGoing 0x43849c,
  //     checkArrivedPath, atTarget 0x4fd66a - its bFindKeeperFood.
  //   0x7c Task7c_SurfaceForAir (ctor 0x4e8a31, dtor 0x498cad, destroy
  //     0x498cb7, freeIfAsked 0x498cc4, taskId 0x498ce7; begin 0x4e89e1:
  //     the unit's slot 0x268 must allow it, then as beginClear):
  //     whileGoing 0x498cf5, atTarget 0x4e8ac4 - Member::surfacing.
  //   0x74 Task74_Chase (ctor 0x4a4e37, dtor 0x4a4d98, destroy 0x4a4da2,
  //     freeIfAsked 0x4a4daf, taskId 0x4a4c06): chasing prey (Animals::chase).
  //   0x75 Task75_AvoidPredator (ctor 0x4a585f, dtor 0x4a4dfd, destroy
  //     0x4a4e07, freeIfAsked 0x4a4e14, taskId 0x4a5502; staff and guests
  //     run it too): running from a predator (Member::fleeFrom).
  //   0x7b Task7b_GoToTargetBuilding (ctor 0x61ac58, dtor 0x61acaa, destroy
  //     0x61acc8, taskId 0x61aca4; begin true): checkArrived 0x61acd3 - on
  //     its target tile, "Reached targeted building"; atTarget 0x61ad83 -
  //     Member::rampageBuilding.
  //   0x73 Task73 (ctor 0x61ad92, dtor 0x61ade4, destroy 0x61ae02, taskId
  //     0x61adde): nothing of its own but beginClear (class table 0x63453c);
  //     no counterpart.
  //   0x7d Task7d_GetTrickFood (ctor 0x5a72d9, dtor 0x5a6c78, destroy
  //     0x5a6c82, freeIfAsked 0x5a6c8f, taskId 0x5a25c7), 0x85
  //     Task85_GoToShowTank (dtor 0x5a3104, destroy 0x5a310e, freeIfAsked
  //     0x5a311b, taskId 0x5a307d; whileGoing 0x5a3083 - with no target
  //     tile yet, 0x5a3afa; checkArrivedShowPortal; atTarget 0x5a3846, its
  //     tank-set test 0x5a3897), 0x86 Task86_ReturnHomeTank (ctor 0x5a8457,
  //     dtor 0x5a36d1, destroy 0x5a36db, freeIfAsked 0x5a36e8, taskId
  //     0x5a2f1f; atTarget 0x5a370b, its test 0x5a375c): the show animals'
  //     trips (Shows.cpp).
  struct Member {
    int id = 0, type = 0;
    bool female = false, baby = false;
    std::string name;
    // Its exhibit (zoo.exe ZTAnimal::habitat 0x410685, class slot 0x24c:
    // +0x388), -1 none
    int exhibit = -1;
    int showTank = -1, showHome = -1;
    bool showTrick = false, showDone = false;
    float x = 0, y = 0;   // tiles
    float fx = 0, fy = 1; // facing (world)
    // What it's playing: once, looping for a time, there and back, or
    // backwards
    enum class Mode { Once, Loop, PingPong, Reverse } mode = Mode::Loop;
    std::string anim = "stand";
    float animTime = 0, playFor = 0;
    // fPlay's other animations, in turn after the first (zoo.exe 0x440b92
    // plays every one its line names); for the step it belongs to (the
    // stack's depth and line), so an interrupted one isn't played later
    std::vector<std::string> playQueue;
    std::pair<size_t, size_t> playQueueAt{0, 0};
    // The behaviour sets it's in (a set can play another), and where
    std::vector<std::pair<std::string, size_t>> stack;
    bool stepping = false; // a step is under way (playing or walking)
    // Walking: the points left, the animation and the speed
    std::vector<std::pair<float, float>> path;
    size_t pathAt = 0;
    std::string moveAnim = "walk";
    float speed = 0.5f;
    // Its move's animation rate in percent (zoo.exe unit +0x1fc, set by
    // 0x413754 as each move starts; 0x435bbe runs the animation at it while
    // it moves) and the move animation it's for (transient)
    int animRate = 100;
    std::string animRateFor;
    // fMove with a time (zoo.exe 0x43646e): the move's own animation's
    // playing left, in seconds of it, before its plain move animation; -1
    // for the whole move (transient), and that plain move's animation (slot
    // 0x134 -> 0x4a9bb9: walk, trot or run by the line's speed)
    float moveAnimLeft = -1;
    std::string moveAnimAfter = "walk";
    bool mealTaken = false; // transient: bEat has already consumed its mouthful
    // Eating (zoo.exe ZTAnimal +0x3a9: set by animalEatFood 0x4fd951 at its
    // food, cleared by animalStopEating 0x4fd86e). Not saved: a loaded
    // animal at its food starts again with its next mouthful
    bool eating = false;
    int foodItem = -1; // the food it's gone to eat
    float awarenessWait = 0, awarenessCheck = 0; // transient local steering timers
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
    // Taken away for poor treatment (zoo.exe unit +0x13c, set by 0x4fa78e(1)
    // from 0x4808ac): gone at the next sweep (0x434df7 - ours lifeEvents,
    // the same frame, so never saved)
    bool takenAway = false;
    // The set it's to start when the one it's in ends (zoo.exe unit +0xdc,
    // set by setBehaviourSet 0x4300f6; "" none). Its flag +0xe8 - whether a
    // later request may replace it - is 0 for the two that ask here (bSleep
    // from 0x4384ba, bSick from 0x4389aa) and 1 for an empty slot, so ours:
    // taken while not empty. Not saved: asleep with no bSleep running, a
    // loaded animal starts its bSleep at once (chooseBehaviour)
    std::string nextSet;
    int healthCheck = 0, energyCheck = 0, reproductionCheck = 0, starve = 0;
    int life = 5000, ready = 0, grow = 0, notHappyCheck = 0;
    // Its mate, mother and father (ids, -1: none - zoo.exe ZTAnimal +0x2e8,
    // +0x2ec, +0x2f0, the debug overlay's Mate, Mother, Father) and the
    // reproduction checks left before a new mate once it's lost one (+0x348)
    int mate = -1, mother = -1, father = -1, matePick = 0;
    // An egg that's been eaten (zoo.exe +0x3a6, set by 0x5a5606): its
    // broken shell, gone when its hatch countdown (15) runs out
    bool eaten = false;
    // A shelter or toy it's off to (object id) or in; hidden inside
    int building = -1, buildingCheck = 0;
    // The building it means to use, not yet set off for (zoo.exe its goal,
    // +0x214..+0x21c with the kind +0x24e, which chooseTask takes up -
    // goToBuilding), and the building's slot it's going to or in (+0x24d)
    int buildingGoal = -1, restSlot = -1;
    // In the water (swimmers: where it wanders), the water and drink checks
    bool waterMode = false;
    int waterCheck = -1, boredCheck = -1;
    int murkyCheck = 0; // (zoo.exe animal +0x2fc: the tank water check's countdown)
    // Its breath held (zoo.exe animal +0x360) and the breath check's
    // countdown (+0x364); going up for air (Task 0x7c: 1 on its way up, 2 up
    // there, its bBreathe next)
    int breath = 0, breathCheck = 0, surfacing = 0;
    // The guests watching it (zoo.exe animal +0x368) and the side they're on
    // (+0x36c, +0x370); the way away from them its next walk takes (+0x37c,
    // 0-7, -1: none) - countGuestCrowd, tick, walkSomewhere
    int guestCrowd = 0, guestCrowdX = 0, guestCrowdY = 0, crowdDir = -1;
    float drinkX = -1, drinkY = -1; // the water it's going to drink at
    float drinkGoX = -1, drinkGoY = -1; // where it stands to drink (its bFindOtherFood's "otherfood")
    // In a tank: how high it swims over the floor (height units), and the
    // height it's swimming to (fWalk's surface, floor, dive, rise, level)
    float swimZ = 0, swimTo = 0;
    // Hunting a guest (loose predators, cPrey 9503): who, since when, the
    // check's countdown; the guest it's caught (held, +0x20d, till that
    // guest's fShowPredator lets it go - zoo.exe 0x4a6043, 0x431af4); hidden
    // (+0x13f clear: slot 0xcc(0), the guest's fHidePredator 0x50e816)
    int prey = -1, preyCheck = 0;
    double chaseStart = 0;
    int mauling = -1;
    bool hidden = false;
    // In its exhibit (zoo.exe 0x43954c, 0x4369b9, 0x4a6043): the animal it's
    // chasing (a prey it lists, or one of its own kind at play - tag), since
    // when; the one it's running from; caught (frozen under the dust ball,
    // by whom); the one it's caught and the dust ball's time left. (Its prey,
    // +0x2e4, forgotten by ZTAnimal::clearPrey 0x4163ae, slot 0x258: -1)
    int huntAnimal = -1, fleeFrom = -1, caughtBy = -1, victim = -1, huntCheck = 0;
    bool tag = false;
    // This chase's stand-off is done (zoo.exe's chase state: once a chase)
    bool preattacked = false;
    // Rampaging (zoo.exe ZTAnimal+0x3b0, "Ramp" in the original's debug
    // overlay) and its tick count (+0x35c): over past cRampageTimeMax
    bool rampaging = false;
    int rampageTicks = 0;
    int rampageBuilding = -1; // transient reachable building target, never a shelter occupancy
    // The tile it's on (-1: not yet): stepping onto another is when a
    // rampage rubbles what stands there (zoo.exe 0x437572)
    int tileX = -1, tileY = -1;
    double huntStart = 0;
    float killIn = 0;
    bool inside = false;
    float insideFor = 0;
    // Darted by a keeper (asleep), crated (waiting to be put in an
    // exhibit), the keeper after it
    bool tranquilised = false, boxed = false;
    float boxTimer = 0;
    // A crate that stays shut: the time to its next check (cBoxedCheck), the
    // time its rattle (box_used) has left, how often it has rattled
    float boxCheck = 0, rattleLeft = 0;
    int rattles = 0;
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
    int habitatCheck = 0, captivityCheck = 0, socialCheck = 0, otherCheck = 0, hungerCheck = 0, zapCheck = 0;
    // What's left of its cTestFenceChance (zoo.exe ZTAnimal+0x3ac; -1 not
    // set yet, 0 it's learnt and leaves electric fences alone)
    int fenceTests = -1;
    // An egg (zoo.exe ZTAnimal+0x395) and its ticks to hatching (+0x398)
    bool egg = false;
    int hatchIn = 0;
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
    // A tank (zoo.exe 0x444827's record +0x50, +0x64, +0x68): the water's
    // depth against what it likes, its cleanliness, its salt; too shallow
    // or deep (red when far off), the wrong water, no land for one that
    // needs it (and its tree, rock and hill wants dropped)
    bool tank = false;
    float depth = 100, clean = 100, salinity = 100;
    bool shallow = false, deep = false, depthRed = false, saltWrong = false, noLand = false;
  };

  void loadTypes(ResourceManager *rm);
  void clear();
  const std::vector<Type> &types() const { return this->typeList; }
  // A species' keeper food, the food item's name (its cKeeperFoodType,
  // type +0x3c8, in food.cfg's order; an enabled mod's cKeeperFood)
  std::string keeperFoodOf(int type) const;
  // Whether a food item is the animal's own kind (zoo.exe: the food's +0x168
  // against the species' +0x3c8 - 0x49fe15, 0x4a0025, 0x4a01f9, 0x4a85da)
  bool ownFood(const Member &m, const ZooItems::Item &i) const;
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
  // (facing: the world facing, 0 to 7, a new one goes down with - World's
  // worldFacing of Adopt's rotation; the one carried keeps its own)
  Fences::Fit canPlace(int type, float x, float y, const WorldMap &map, const Fences &fences,
                       int facing = Original::placedUnitFacing) const;
  // The footprint-wide check (zoo.exe 0x4f7fbd) for one put down at x, y:
  // a big one's every half tile on the map and in the zoo, no fence or
  // cliff inside it (and, from its crate, more: see Animals.cpp)
  bool footprintFits(int type, float x, float y, const WorldMap &map, const Fences &fences,
                     int facing = Original::placedUnitFacing) const;
  // Its facing as the original's +0x12c (0 N, 2 E, 4 S, 6 W) from fx, fy
  static int facingOf(const Member &m);
  // Its footprint in half tiles (class slot 0x94, ZTAnimal 0x410803): an
  // egg's cEggFootprint, a crate's cBoxFootprint, else with `map` its
  // cMapFootprint both ways, without its cFootprintX/Y
  std::pair<int, int> footprint(const Member &m, bool map) const;
  // The tiles it's listed on in the map (0x4137df, slot 0x94 with the flag)
  std::vector<std::pair<int, int>> mapTiles(const Member &m) const;
  float placementDepth(int type,int tank) const;
  int adopt(int type, bool female, float x, float y, const WorldMap &map, const Fences &fences,
            bool restoring = false, int facing = -1);
  // (restoring: an animal of a saved zoo put back as it was - wherever it
  // stood, loose ones too, and as it is: no egg made of a grown dinosaur)
  void remove(int id);
  // What selling it gives back (zoo.exe ZTAnimal::sellValue 0x48d21d): by
  // how happy and how healthy it is, halved again when sick
  double sellValue(int id) const;
  static double sellValueOf(int cost, int happiness, int health, int minHits, int maxHits, bool sick);
  // Picked up (Move Animal) and put down again
  void moveTo(int id, float x, float y);
  // While held: whether it can be put down where it is (green or red)
  void hold(int id, const WorldMap &map, const Fences &fences);
  bool carriedFit = false;
  bool canDrop(int id, float x, float y, const WorldMap &map, const Fences &fences) const;
  void drop(int id, float x, float y, const Fences &fences);

  // ours: one animation of a species in the zoo loaded ahead (true: one was)
  bool warmOne();
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
  // A message about one of them by text number, its name filled in - a
  // female's from the number + 30000 (zoo.exe unitMessageText 0x49cfeb)
  std::string unitText(const Member &m, int textId) const;
  // Babies born and the old dying (in update), done by the world: false
  // when nothing happened
  void lifeEvents(const WorldMap &map, const Fences &fences);
  // A keeper's visit: every animal in the exhibit cheered (cKeeperArrivesChange)
  void keeperArrives(int exhibit);
  // A keeper heals one: health full, not sick
  void heal(int id);
  // "Dr. Frankenstein"'s heal (zoo.exe 0x4702e8): another egg-laying kind
  void becomeAnotherKind(int id);
  // Needs a keeper's care (zoo.exe animalIsSickly 0x4a88cf): health x 100
  // at or under cMaxHits (type +0x234) x (100 - cSicklyAnimalPct, 15)
  bool needsHealing(const Member &m) const {
    return m.health * 100 <= this->typeList[m.type].maxHits * (100 - Original::sicklyAnimalPct);
  }
  // Health as the panel shows it (0-100)
  int shownHealth(const Member &m) const {
    return static_cast<int>(m.health * 100 / std::max(1, this->typeList[m.type].maxHits));
  }
  // A keeper's dart (asleep where it is) and crate
  void tranquilise(int id);
  void box(int id);
  // A tank taken apart: the sea animals on its tiles crated where they are
  // (zoo.exe 0x458c2b with 0: each unit of a water-living type, 0x45da65,
  // through 0x505941 - the land ones and the objects stay as they were)
  void boxSeaAnimals(const std::set<std::pair<int, int>> &tiles);
  // Fences broken getting out, taken (the world breaks them)
  std::vector<Fences::Edge> takeBreaks() {
    std::vector<Fences::Edge> b;
    b.swap(this->breaks);
    return b;
  }
  // How many live in an exhibit (keepers feed and rake for them)
  int countIn(int exhibit) const;
  // Of those, the dinosaurs ([Member] dinosaur: Dinosaur Digs' and Nessie)
  int dinosaursIn(int exhibit) const;
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
  // The same over only the animals keep accepts; -1 when there are none
  // (zoo.exe 0x41da81, goal rule 6: one family's or era's animals)
  int exhibitSuitability(int exhibit, const std::function<bool(const Member &)> &keep) const;
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
  // (an object or animal: the habitat of the tile it's on, and only from a
  // tank the habitats joined to it - zoo.exe 0x4d8cd2 / 0x4d8bd7)
  void snapshot(int exhibit, const WorldMap &map, const Fences &fences, Change change = Change::Ground);
  std::vector<int> smileHabitats(int exhibit, Change change, const WorldMap &map, const Fences &fences) const;
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
  // Animals in tanks are drawn with their tank (drawInTank), not in collect
  std::function<int(float x, float y)> tankAt;
  mutable int onlyTank = -2; // (collect: -2 all but tanks', else that tank's alone)
  // A tank's water depth over its floor (height units)
  std::function<float(int tank)> tankDepth;
  // A tank's water purity set (Fences::setPurity, zoo.exe 0x49340d) and read
  std::function<void(int tank, float purity)> setTankPurity;
  std::function<float(int tank)> tankPurity;
  // A tank's animals, in its two passes: under the water's surface and on it.
  // Marine Mania's art comes in layers: an animation whose art is F has
  // "under" + F beside it, its half below the water; the animation's own art
  // is its part above (a jump's body in the air, the splash). Under the
  // water that half is drawn, on it the animation; one without is all under
  void drawInTank(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map, int tank,
                  bool surface = false);
  mutable bool onlySurface = false; // (collect in a tank: its surface pass)
  // An animation's "under" layer (its half below the water), or none
  Animation *underArt(const Member &m, Animation *a) const;
  // An animation's shadow ("shadow" + its art's name - zoo.exe setupShadow
  // 0x43f81c, "shadow%s"), drawn on the ground under it; or none
  Animation *shadowArt(const Member &m, Animation *a) const;
  // Why canPlace refused (the original's message, a lang string; 0 none)
  mutable int placeMessage = 0;
  // A tank's water level in height units over its floor (tank +0x188: for
  // cDepth and the swimmers' heights)
  std::function<int(int tank)> tankLevel;
  // The height a move in a tank ends at (zoo.exe 0x43dc11)
  float tankTargetHeight(Member &m, const std::string &target);
  // (orStand: a name it hasn't gets its stand - false, nothing)
  Animation *art(const Member &m, const std::string &name, bool orStand = true) const;
  Animation *preview(int type, bool female) const;
  // Its crate: its own box_idle ([<subtype>/Animations], the adult's
  // else - a sea animal's aquabox, a dinosaur's dinobox: ZTAnimalType's
  // +0xd4, 0x4b64cf, names the box state's animation "box_idle"), else
  // the plain objects/box
  Animation *crate(const Member &m) const;
  // "Recently", "A short time ago", "A long time ago"
  static int agoText(double since, bool slept);
  // Its carer as the animal panel shows it (zoo.exe 0x4831b2): 0 the
  // zookeeper, 1 the scientist, 2 the marine specialist - which plaque,
  // recommendations title and background, hunger and suitability help
  int carerIndex(const Member &m) const {
    int k = m.type >= 0 && m.type < static_cast<int>(this->typeList.size()) ? this->typeList[m.type].keeper : 9500;
    return k == 9551 ? 1 : k == 9552 ? 2 : 0;
  }

  double clock() const { return this->now; }

  int selected = -1, hovered = -1, carried = -1;
  // The one being adopted, at the cursor (type < 0: none)
  int previewType = -1;
  bool previewFemale = false;
  int previewFacing = Original::placedUnitFacing; // (world facing, 0 to 7)
  float previewX = 0, previewY = 0;
  Fences::Fit previewFit = Fences::Fit::Ok;
  bool previewPoor = false; // (it fits, but costs more than the cash: red)

  // Guests, for loose predators (zoo.exe 0x43954c): the nearest man guest
  // within a radius not yet attacked (-1: none); where one is; caught
  std::function<int(float x, float y, float radius)> findPrey;
  std::function<bool(int guest, float &x, float &y)> preyAt;
  std::function<void(int guest, const Member &by)> caughtPrey;
  // (whether the guest is still in the set this one caught it into)
  std::function<bool(int guest, int animal)> guestHolds;
  // The caught guest's set's fHidePredator (zoo.exe 0x50e816) and
  // fShowPredator (0x50e6fa) on this one, its attacker
  void hidePredator(int id);
  void showPredator(int id, int guest);
  // A guest taken away (zoo.exe 0x4fa78e(1) on it: fKillPrey 0x60cb3a)
  std::function<void(int guest)> takeGuestAway;
  // An exhibit's viewing areas (zoo.exe: the habitat's list at +0x34): each
  // one's path tiles (+0x40), the exhibit's tiles seen from it (+4) and the
  // guests standing on it (+0x54)
  struct ViewArea {
    std::vector<std::pair<int, int>> viewing, seen;
    int guests = 0;
  };
  std::function<std::vector<ViewArea>(int exhibit)> viewAreas;
  // A rampaging animal onto a tile (zoo.exe 0x437572): what stands on it
  // that can be (cRubbleable) turned to rubble; how many were
  std::function<int(int tileX, int tileY, const Member &by)> rubbleAt;
  // The guests using a building (object id) - its users with the animals
  // in it (zoo.exe building +0x18c)
  std::function<int(int building)> buildingGuests;
  // The keepers at their duties in an exhibit (zoo.exe habitat +0xf0, as
  // Staff::keepersAt counts them); unset: none
  std::function<int(int exhibit)> keepersAtWork;
  // A water tile (fresh or salt water; a waterfall is land)
  static bool isWater(const WorldMap &map, int x, int y);
  // (the ZT1 Lab) A route for one of them from where it stands to a tile, by
  // its own rules (its exhibit's ground, the fences, the slopes), as walkTo
  // would find it: nothing moves
  bool testRoute(int id, int gx, int gy, const WorldMap &map, const Fences &fences,
                 std::vector<std::pair<int, int>> &out) const;
  // (the ZT1 Lab) One of its animations played now, for so long, or one of
  // its behaviour sets started (what it was doing dropped); what it has of
  // each, by its subtype
  void labPlay(int id, const std::string &anim, float seconds);
  void labBehave(int id, const std::string &set);
  std::vector<std::string> labAnims(int id) const;
  std::vector<std::string> labSets(int id) const;
  std::string awarenessReport() const;
  // zoo.exe 0x63cb35: a lion, a tiger and a grizzly have shared an exhibit
  bool bearsSeen() const { return this->lionTigerBears; }

private:
  // The space an animal occupies, from its species' footprint and the height
  // of its own art - an oriented box, not one radius for every species. In a
  // tank the height is what lets two fish pass one above the other.
  struct BodyVolume {
    float halfAcross = 0.22f; // across the way it faces, tiles
    float halfAlong = 0.22f;  // along the way it faces, tiles
    float halfHeight = 0.17f; // up the screen, height units
  };
  BodyVolume bodyVolume(const Member &m) const;
  float bodyOverlap(const Member &a, float ax, float ay, float az, float afx, float afy,
                    const Member &b, float bx, float by, float bz, float bfx, float bfy) const;
  float bodyRadius(const Member &m) const;
  float tankClearance(const Member &m, float x, float y, const Fences &fences, bool planning = false) const;
  const Fences *fencesRef = nullptr;
  bool awareOf(const Member &a, const Member &b) const;
  float bodyHeight(const Member &m, float x, float y, float swim) const;
  float bodyDistance(const Member &a, float ax, float ay, float az,
                     const Member &b, float bx, float by, float bz) const;
  bool destinationFree(const Member &m, float x, float y, float swim, bool reservations) const;
  bool feedingPlace(Member &m, float x, float y, const WorldMap &map, const Fences &fences);
  void consumeFood(Member &m);
  void reactOne(int exhibit, Change change, const std::string &file, int animalType, bool removed,
                const WorldMap &map, const Fences &fences, std::map<int, float> &snap);
  void separateIdle(Member &m, float seconds, const WorldMap &map, const Fences &fences);
  const WorldMap *mapRef = nullptr; // (the map, for the art: swimmers' swim / water idle)
  bool onWater(const Member &m) const;
  bool dropDung(Member &m, const Fences &fences);
  void setAnimRate(Member &m, int pace, int fMoveRate = 100);
  void waterTick(Member &m, const WorldMap &map, const Fences &fences);
  ResourceManager *rm = nullptr;
  ZooItems *items = nullptr;
  const PlacedObjects *objects = nullptr;
  bool useShelter(Member &m, const WorldMap &map, const Fences &fences);
  int shelterUsers(int building) const;
  void leaveShelter(Member &m);
  // What an object is to an animal: its name id and habitat (the keys of
  // cSuitableObjects), its footprint, and what kind of thing it is
  struct ObjectInfo {
    int nameId = 0, habitat = 0, fx = 1, fy = 1, capacity = 0, toy = 0;
    bool foliage = false, rock = false, stink = false, shelter = false, building = false;
    bool hideBuilding = false, rubbleable = true;
    bool rest = false, hideUser = false; // animals use it (animalrest), out of sight
    float adultChange = 0, childChange = 0, hungerChange = 0, energyChange = 0;
    int timeInside = 10;
    // Its [slots] (zoo.exe ZTBuildingType::loadConfig 0x4c0b4d, 0x88 a
    // slot): the id list (+0x18, the kinds' cNameIDs), capacity (+0x30),
    // filter (+0x3c, the m/f/y letters it turns away), isreturnslot (+0x45)
    // and its entrpos pairs (64 a tile, turned with the building)
    struct Slot {
      std::vector<int> ids;
      int capacity = 0;
      std::string filter;
      bool isReturn = false;
      std::vector<std::pair<int, int>> entrances;
    };
    std::vector<Slot> slots;
  };
  mutable std::map<std::string, ObjectInfo> objectInfo;
  const ObjectInfo &infoOf(const std::string &file) const;
  // A building's slot with room for it (zoo.exe buildingSlotHasRoom
  // 0x42ac87), any slot (buildingHasRoom 0x429330); the animals in a slot
  bool slotHasRoom(int building, const ObjectInfo &info, int slot, const Member &m) const;
  bool buildingHasRoom(int building, const ObjectInfo &info, const Member &m) const;
  int slotUsers(int building, int slot) const;
  // Off to the building it means to use (zoo.exe guestGoToBuilding 0x43b2f6)
  bool goToBuilding(Member &m, const WorldMap &map, const Fences &fences);
  struct Evaluation {
    float timerMs = 0;
    float speciesMs = 0; // zoo.exe tankTick 0x4357c6's second timer (+0x88)
    bool dirty = true;
    int tiles = 0, dung = -1;
    std::map<int, Rating> kinds; // per animal type
  };
  std::map<int, Evaluation> evaluations;
  // zoo.exe 0x447dc1 (every 8 s per exhibit, 0x63cb35): a lion, a tiger and
  // a grizzly bear in one exhibit - the Yellow Brick Path's key
  bool lionTigerBears = false;
  void lookAtSpecies(int exhibit);
  const Rating *rating(const Member &m, const WorldMap &map, const Fences &fences);
  void evaluate(int exhibit, const WorldMap &map, const Fences &fences);
  // How many share its exhibit: its own kind (young too), its kind's
  // adults, all adults
  void company(const Member &m, int &kind, int &kindAdults, int &adults) const;
  // (and the tiles of its habitat with the ones joined to it)
  void company(const Member &m, int &kind, int &kindAdults, int &adults, int &tiles) const;
  static bool crowdedBySpecies(const Type &t, int kindAdults, int tiles);
  // Its go at a live electric fence this tick, if it has one (zoo.exe
  // 0x438ebc, 0x4a3383, 0x59c4ba)
  void testFence(Member &m, const Fences &fences);
  // Made an egg (zoo.exe 0x5a5283), and an egg's tick: hatching when it's
  // time and it's healthy enough (0x437860)
  void becomeEgg(Member &m);
  void eggTick(Member &m);
  void healthTick(Member &m);
  void tick(Member &m, const WorldMap &map, const Fences &fences);
  void murkyWaterTick(Member &m, const WorldMap &map, const Fences &fences);
  void countGuestCrowd(Member &m, const Fences &fences);
  static int direction8(int dx, int dy);
  static bool nearDirection(int want, int dir);
  WorldIndex *index = nullptr;
  std::vector<Type> typeList;
  std::vector<Member> list;
  std::map<int, int> counts; // per type: how many were ever adopted
  int nextId = 1;
  mutable std::map<std::string, Animation *> artCache;
  // ours: a species' animations loaded ahead, one at a time on another
  // thread (warmOne; ResourceManager::loadAnimationAhead), once one of it is
  // in the zoo - loaded when an animal first did something, each was a 3-40
  // ms hitch (a dolphin's show 150)
  std::deque<std::string> warmQueue;
  std::string warming; // (the one loading on another thread now)
  std::set<std::string> warmQueued;
  std::vector<char> typeWarmed;
  mutable std::map<const Animation *, Animation *> underCache; // (an art's "under" layer, or none)
  mutable std::map<const Animation *, Animation *> shadowCache; // (an art's shadow, or none)
  Animation *selectArrow = nullptr;
  Animation *smileArt = nullptr, *frownArt = nullptr;
  std::map<int, std::map<int, float>> snaps; // exhibit -> kind -> rating (the ground's)
  // An object's or animal's before-snapshot: one for all, each habitat
  // snapshotted replacing it (zoo.exe's DAT_0063b998, kind -> rating)
  std::map<int, float> snapOne;
  std::vector<std::string> messages;
  std::vector<Fences::Edge> breaks;
  std::vector<int> kills; // caught prey to go (next update)
  std::vector<Notice> notices;                 // message bar
  struct Birth {
    int mother, father, kind;
  };
  std::vector<Birth> births;
  bool mateLost(Member &m);
  Member *findMate(Member &m);
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
  bool mayPerform(const Member &m) const;
  void pushSet(Member &m, const std::string &name);
  // A set asked for next (zoo.exe 0x4301b7 -> setBehaviourSet 0x4300f6):
  // not when it's the set it's in, nor over one asked for already
  bool requestSet(Member &m, const std::string &name);
  // Stops eating (zoo.exe animalStopEating 0x4fd86e)
  void stopEating(Member &m);
  // The tiredness check's body (zoo.exe 0x4384ba, past its countdown)
  void tirednessCheck(Member &m);
  void chooseBehaviour(Member &m);
  void nextStep(Member &m, const WorldMap &map, const Fences &fences);
  void play(Member &m, const std::string &anim, Member::Mode mode, float seconds = 0);
  bool walkTo(Member &m, float gx, float gy, const WorldMap &map, const Fences &fences);
  // Hunting in an exhibit: whether one lists another as prey (an adult
  // hunting); a set's way of moving (its fMove / fRun: anim and speed); the
  // nearest adult within reach that would hunt it, or chases it; running off
  bool hunts(const Member &predator, const Member &prey) const;
  // The stand-off before a chase (zoo.exe 0x42fad3, 0x61b2d6): both have
  // cPreattack and the other isn't an egg; standOff stops them face to face,
  // the hunter with its bPreattack and the other with its bDefense; standingOff
  // while either is still playing
  bool preattacks(const Member &hunter, const Member &other) const;
  void standOff(Member &hunter, Member &other);
  bool standingOff(const Member &m) const;
  void moveOf(const Member &m, const std::string &setName, std::string &anim, float &speed) const;
  const Member *threatTo(const Member &m, float radius) const;
  void runFrom(Member &m, const Member &threat, const WorldMap &map, const Fences &fences);
  void chase(Member &m, const WorldMap &map, const Fences &fences);
  bool busy(const Member &m) const;
  // Whether it may lie down - sleep, or be sick - where it is (zoo.exe
  // 0x440a37)
  bool mayRest(const Member &m) const;
  // Busy with itself (zoo.exe class slot 0x288, 0x420fed): no hunt starts
  bool occupied(const Member &m) const;
  // Under the water in a tank (the original's water state 4, unit +0x258)
  bool underwaterNow(const Member &m) const;
  void breathTick(Member &m);
  bool walkSomewhere(Member &m, const WorldMap &map, const Fences &fences);
  int nearestFood(const Member &m) const;
  int chooseFood(Member &m);
  void faceToward(Member &m, float x, float y);
  float animSeconds(const Member &m, const std::string &anim) const;
  CompassDirection facing(const WorldRenderer &view, const Member &m) const;
};

#endif // ANIMALS_HPP
