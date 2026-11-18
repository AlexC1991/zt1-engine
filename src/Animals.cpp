#include "Animals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Features.hpp"
#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "LabTrace.hpp"
#include "OriginalConstants.hpp"
#include "Pathfinder.hpp"
#include "PlacedObjects.hpp"
#include "RenameCheats.hpp"
#include "Sound.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include "ZooSim.hpp"

namespace {
constexpr float kDay = static_cast<float>(ZooSim::kSecondsPerDay);
// (not yet measured in the original) its cEnergyIncrement every half day
constexpr float kEnergyPerHalfDay = 1.0f;
// From zoo.exe: animals' status ticks every second (zoo.ini [AI]
// mStatusPeriod, 1000 ms); each check runs every so many ticks (ai.ztd
// animals.ai: cHabitatCheck 20, cCaptivityCheck 20, cSocialCheck 30,
// cOtherCheck 40, cHungerCheck 30, cZapCheck 5, cEnergyCheck 30, cHealthCheck
// 30); an exhibit is rated again
// every 7 s. (zoo.exe loadAnimalSettings 0x45f98b reads each key into its
// global, over its own defaults - 5 for cCaptivityCheck, cOtherCheck,
// cSocialCheck, cHungerCheck, cBreathCheck, cMurkyWaterCheck, cBoredCheck,
// cZapCheck, cBuildingUseCheck; 10 for cKeeperCheck, cHealthCheck,
// cReproductionCheck; 3 for cEnergyCheck, cHabitatCheck, cMatePickDelay -
// so a key animals.ai doesn't have keeps the default: cMurkyWaterCheck 5)
constexpr float kTickMs = 1000.0f;
constexpr int kHabitatCheck = 20, kCaptivityCheck = 20, kSocialCheck = 30, kOtherCheck = 40,
              kHungerCheck = 30, kZapCheck = 5, kEnergyCheck = 30, kHealthCheck = 30;
constexpr float kRateEveryMs = 7000.0f;
// measured: cMurkyWaterCheck (zoo.exe 0x6393e4, read at 0x45fcde) is 5 in
// the running original (Lab memPeek; animals.ai has no such key, every
// other check's global matched animals.ai)
constexpr int kMurkyWaterCheck = 5;
// (ai.ztd animals.ai cMatePickDelay 3, zoo.exe 0x639454: reproduction checks
// before a cMatingType 1 animal takes a new mate)
constexpr int kMatePickDelay = 3;
// (animals.ai cDefenseRadius 3, zoo.exe 0x641560: how near two that both
// cPreattack get before their stand-off)
constexpr float kDefenseRadius = 3.0f;
// The breath check (animals.ai cBreathCheck 30, zoo.exe 0x639450) and the
// breath it's kept within: type +0x208 and +0x204, which no file sets
// (setDefaults 0x4b659e: 0 and 100)
constexpr int kBreathCheck = 30, kBreathMin = 0, kBreathMax = 100;
// ours: how soon an animal whose behaviour line couldn't run picks again
// (the original's does at its next update, every frame)
constexpr float kLineRetry = 0.1f;

std::string lower(std::string s) { return Utils::string_to_lower(s); }

std::string stripAni(std::string p) {
  p = lower(p);
  size_t dot = p.rfind(".ani");
  return dot == std::string::npos ? p : p.substr(0, dot);
}

// Marine Mania art its .ai files name wrongly: the art ships, under a near
// name nothing else points to (re/lab/artkit.py's survey, 41 found, each
// checked to have the same facings and frames as the animation it goes
// with; the shadows among them are its black silhouettes). The named
// folder -> the folder the art is in (Features::misnamedArt)
std::string misnamedArt(const std::string &path, ResourceManager *rm) {
  static const std::map<std::string, std::string> fix = {
      // the hammerhead's underwater thrash and its shadow ("thrsh"/"trsh")
      {"animals/hhshark/m/subthrsh", "animals/hhshark/m/subtrsh"},
      {"animals/hhshark/m/ssubthrs", "animals/hhshark/m/ssubtrsh"},
      {"animals/hhshark/y/subthrsh", "animals/hhshark/y/subtrsh"},
      {"animals/hhshark/y/ssubthrs", "animals/hhshark/y/ssubtrsh"},
      // the marlin's surface thrash, its underwater half and shadow
      {"animals/marlin/m/surftrsh", "animals/marlin/m/surfthrs"},
      {"animals/marlin/m/usurftrs", "animals/marlin/m/usurfthr"},
      {"animals/marlin/m/ssurftrs", "animals/marlin/m/ssurfthr"},
      {"animals/marlin/y/surftrsh", "animals/marlin/y/surfthrs"},
      {"animals/marlin/y/usurftrs", "animals/marlin/y/usurfthr"},
      {"animals/marlin/y/ssurftrs", "animals/marlin/y/ssurfthr"},
      {"animals/morayeel/m/ssurftrs", "animals/morayeel/m/ssurfthr"},
      {"animals/morayeel/y/ssurftrs", "animals/morayeel/y/ssurfthr"},
      // the porpoise's surface eating: its underwater half and shadow
      {"animals/porpoise/m/ssurfeat", "animals/porpoise/m/ssurfea"},
      {"animals/porpoise/m/usurfeat", "animals/porpoise/m/usurfea"},
      // sick shadows
      {"animals/seacow/m/ssubsic", "animals/seacow/m/ssubsick"},
      {"animals/seacow/y/ssubsic", "animals/seacow/y/ssubsick"},
      {"animals/beluga/y/ssubsick", "animals/beluga/y/ssubsic"},
      // the young great white's dive and swim shadows
      {"animals/gwshark/y/ssubdive", "animals/gwshark/y/ssubdiv"},
      {"animals/gwshark/y/ssubswim", "animals/gwshark/y/ssubswi"},
      // the young whale shark's dive and its shadow
      {"animals/whleshrk/y/subdive", "animals/whleshrk/y/dive"},
      {"animals/whleshrk/y/ssubdive", "animals/whleshrk/y/sdive"},
      {"animals/tapir/y/shakhead", "animals/tapir/y/shakehed"},
      // the elephant seal (female, young): thrash, its shadow, sick turn
      {"animals/eleseal/f/subtrsh", "animals/eleseal/f/subthrsh"},
      {"animals/eleseal/f/ssubtrsh", "animals/eleseal/f/ssubthrs"},
      {"animals/eleseal/f/2subsic", "animals/eleseal/f/2subsick"},
      {"animals/eleseal/y/subtrsh", "animals/eleseal/y/subthrsh"},
      {"animals/eleseal/y/ssubtrsh", "animals/eleseal/y/ssubthrs"},
      {"animals/eleseal/y/2subsic", "animals/eleseal/y/2subsick"},
      {"animals/swcroc/y/sdive", "animals/swcroc/y/subdive"},
      // shadowidle: their idle is the subswim art, its shadow ssubswim
      {"animals/beluga/m/ssubswi", "animals/beluga/m/ssubswim"},
      {"animals/beluga/y/ssubswi", "animals/beluga/y/ssubswim"},
      {"animals/narwhal/m/ssubswi", "animals/narwhal/m/ssubswim"},
      {"animals/narwhal/f/ssubswi", "animals/narwhal/f/ssubswim"},
      {"animals/narwhal/y/ssubswi", "animals/narwhal/y/ssubswim"},
      {"animals/dolphin/m/ssubswi", "animals/dolphin/m/ssubswim"},
      {"animals/dolphin/y/ssubswi", "animals/dolphin/y/ssubswim"},
      {"animals/seacow/m/ssubswi", "animals/seacow/m/ssubswim"},
      {"animals/seacow/y/ssubswi", "animals/seacow/y/ssubswim"},
      {"animals/porpoise/m/ssubswi", "animals/porpoise/m/ssubswim"},
      {"animals/porpoise/y/ssubswi", "animals/porpoise/y/ssubswim"},
      {"animals/mmaid/f/ssubswi", "animals/mmaid/f/ssubswim"},
  };
  size_t slash = path.find_last_of('/');
  if (slash == std::string::npos)
    return path;
  auto it = fix.find(path.substr(0, slash));
  if (it == fix.end())
    return path;
  std::string to = it->second + it->second.substr(it->second.find_last_of('/'));
  return rm && rm->hasResource(to + ".ani") ? to : path;
}

std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\""), b = s.find_last_not_of(" \t\"");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// "fPlaySetProb(bIdle1,20,bHappy1,50)" -> fplaysetprob [bidle1, 20, ...]
// zoo.exe 0x437d9b (the line parser, called by 0x435f0a as a line starts):
// the name is what's before the '(' and the line must end with ')'; inside,
// every space and '"' is taken out, then each piece up to a ',' is an
// argument - an empty one too ("fDie()" has one, "a,,b" three) - and it
// stops when nothing is left after a ',', so a trailing comma adds none
// ("fPlaySetProb(bTense1,70,bAngry3,30,)" has four). (Ours: names and
// arguments lower-cased, as everything we look up is - the .ai files spell
// every function name as the original compares it.)
bool parseStep(const std::string &text, Animals::Step &out) {
  std::string line = trim(text);
  size_t open = line.find('(');
  if (open == std::string::npos || line.empty() || line.back() != ')' || line.size() - 1 < open + 1)
    return false;
  out.fn = lower(trim(line.substr(0, open)));
  out.args.clear();
  std::string inner;
  for (char c : line.substr(open + 1, line.size() - open - 2))
    if (c != ' ' && c != '"')
      inner += c;
  while (true) {
    size_t comma = inner.find(',');
    out.args.push_back(lower(inner.substr(0, comma)));
    inner = comma == std::string::npos ? std::string() : inner.substr(comma + 1);
    if (inner.empty())
      break;
  }
  return !out.fn.empty();
}
} // namespace

bool Animals::parseLine(const std::string &text, Step &out) { return parseStep(text, out); }

// ----------------------------------------------------------------------------
// Types
// ----------------------------------------------------------------------------
void Animals::loadTypes(ResourceManager *rm) {
  this->rm = rm;
  if (!this->typeList.empty() || !rm)
    return;
  if (rm->hasResource("ui/select/selsmall/selsmall.ani"))
    this->selectArrow = rm->getAnimation("ui/select/selsmall/selsmall");
  if (rm->hasResource("objects/smile/idle/idle.ani"))
    this->smileArt = rm->getAnimation("objects/smile/idle/idle");
  if (rm->hasResource("objects/frown/idle/idle.ani"))
    this->frownArt = rm->getAnimation("objects/frown/idle/idle");
  if (rm->hasResource("objects/box/idle/idle.ani"))
    this->crateArt = rm->getAnimation("objects/box/idle/idle");
  ItemCatalog &catalog = ItemCatalog::get();
  catalog.load(rm);
  // The class-wide settings (zoo.exe ZTAnimalType::loadClassConfig 0x4b6436,
  // ZUPDATE1 ai4.ztd animals.ai [Characteristics/Integers]) each type's own
  // falls back on: cKeeperArrivesChange 25, cKeeperArrivesChangeAmphibious 35
  // (its own defaults, without the file: 100 and 100)
  int classKeeperChange = 100, classKeeperChangeAmphibious = 100;
  if (IniReader *cls = rm->getIniReader("animals.ai")) {
    classKeeperChange = cls->getInt("characteristics/integers", "ckeeperarriveschange", 100);
    classKeeperChangeAmphibious = cls->getInt("characteristics/integers", "ckeeperarriveschangeamphibious", 100);
    delete cls;
  }
  for (const CatalogItem *item : catalog.inCategory("animals", false)) {
    IniReader *ai = rm->getIniReader(item->file);
    if (!ai)
      continue;
    Type t;
    t.file = item->file;
    t.key = item->type;
    t.nameId = item->nameId;
    t.displayName = item->name;
    t.cost = item->cost;
    t.expansion = item->expansion;
    // ([Member] dinosaur: a scientist's to look after, not a keeper's)
    t.dinosaur = item->members.count("dinosaur") > 0;
    // A value from the adult male's section, else its DefaultSubtype's (a
    // lion's are under f/: its m/ has just the few where the male differs -
    // found with the Lab's hunger script, re/lab/scripts/hunger.cpp)
    const std::string ints = "m/characteristics/integers";
    std::string defSub = lower(trim(ai->get("global", "defaultsubtype", "m")));
    const std::string defInts = (defSub.empty() ? std::string("m") : defSub) + "/characteristics/integers";
    auto sectionOf = [&](const char *key) { return ai->get(ints, key, "").empty() ? defInts : ints; };
    auto num = [&](const char *key, int fallback) { return ai->getInt(sectionOf(key), key, fallback); };
    // (its rates as tiles a second: zoo.exe 0x4365e0 keeps the .ai rate as
    // the unit's speed, +0x1f8, and moveAccumulate 0x435c42 moves it as it
    // does staff - Original::unitTilesPerSecond. measured: in the running
    // original 2026-11-16, an African buffalo at rate 20 went 1 step an
    // update, 0.227-0.231 tiles/s; an allosaur at 120, 8 steps, 1.77-1.88;
    // a humpback swimming at 66, 4 steps, 0.920)
    t.slow = Original::unitTilesPerSecond(num("cslowrate", 30));
    t.medium = Original::unitTilesPerSecond(num("cmediumrate", 45));
    t.fast = Original::unitTilesPerSecond(num("cfastrate", 66));
    t.initialHappiness = num("cinitialhappiness", 50);
    t.showAnimal = !ai->getList("m/tricks", "trick").empty() || !ai->getList(defSub + "/tricks", "trick").empty();
    t.skipTrickHappiness = num("cskiptrickhappiness", 50);
    t.skipTrickChance = num("cskiptrickchance", 10);
    t.hungerThreshold = num("chungerthreshold", 50);
    t.hungerIncrement = num("chungerincrement", 10);
    t.neededFood = std::max(1, num("cneededfood", 100));
    t.foodEaten = num("ckeeperfoodunitseaten", 20);
    t.foodUnitValue = num("cfoodunitvalue", 1);
    t.noFoodChange = num("cnofoodchange", -50);
    t.energyIncrement = num("cenergyincrement", 15);
    t.energyThreshold = num("cenergythreshold", 70);
    t.maxHits = std::max(1, num("cmaxhits", 500));
    t.pctHits = num("cpcthits", 20);
    t.sickChange = num("csickchange", -20);
    t.otherSickChange = num("cotheranimalsickchange", -10);
    t.sickChance = num("csickchance", 10);
    t.sickRandomChance = num("csickrandomchance", 10); // (loader 0x4b7633)
    t.hungryHealthChange = num("chungryhealthchange", 5);
    t.sickTime = num("csicktime", 5);
    // (type +0x2fc and +0x300, loadConfig 0x4b6c51 at 0x4b78ab, 0x4b78d0)
    t.keeperArrivesChange = num("ckeeperarriveschange", classKeeperChange);
    t.keeperArrivesChangeAmphibious = num("ckeeperarriveschangeamphibious", classKeeperChangeAmphibious);
    t.dirtyIncrement = num("cdirtyincrement", 15);
    t.dirtyThreshold = num("cdirtythreshold", 70);
    t.maxDirty = num("cmaxdirty", 100);
    t.minDirty = num("cmindirty", 0);
    t.pooWaterImpact = num("cpoowaterimpact", 1);
    // (the anim speeds: zoo.exe 0x4b5faf, 0x4b5681 - kept only where the file has them)
    const char *paces[3] = {"slow", "medium", "fast"};
    for (int k = 0; k < 3; k++) {
      const std::string key = std::string("c") + paces[k] + "animspeed", wet = key + "water";
      t.animSpeed[k] = num(key.c_str(), 100);
      t.animSpeedWater[k] = num(wet.c_str(), 100);
    }
    // Its dung (zoo.exe 0x4a8f89, the type's +0x3cf, +0x3ce, +0x3d0):
    // cDinoZoodoo, dinopoo rather than poo; cSmallZoodoo its small art,
    // cGiantZoodoo its giant one, the giant winning (each key's last line
    // counts: apato.ai has cGiantZoodoo 1, then 0)
    {
      auto last = [&](const char *key) {
        std::vector<std::string> v = ai->getList(sectionOf(key), key);
        return !v.empty() && std::atoi(v.back().c_str()) == 1;
      };
      t.dinoDung = last("cdinozoodoo");
      t.dungSize = last("cgiantzoodoo") ? 2 : last("csmallzoodoo") ? 1 : 0;
      // (the young's type: its [y/...] keys over the grown one's)
      auto young = [&](const char *key, bool grown) {
        std::vector<std::string> v = ai->getList("y/characteristics/integers", key);
        return v.empty() ? grown : std::atoi(v.back().c_str()) == 1;
      };
      t.youngDungSize = young("cgiantzoodoo", t.dungSize == 2) ? 2 : young("csmallzoodoo", t.dungSize == 1) ? 1 : 0;
    }
    // (cTimeDeath appears twice in some files: the last one counts)
    {
      std::vector<std::string> td = ai->getList(sectionOf("ctimedeath"), "ctimedeath"),
                               dc = ai->getList(sectionOf("cdeathchance"), "cdeathchance");
      t.timeDeath = td.empty() ? 5001 : std::atoi(td.back().c_str());
      t.deathChance = dc.empty() ? 100 : std::atoi(dc.back().c_str());
    }
    t.reproductionChance = num("creproductionchance", 0);
    t.reproductionInterval = num("creproductioninterval", 0);
    t.happyReproduceThreshold = num("chappyreproducethreshold", 100);
    t.matingType = num("cmatingtype", 0);
    t.noMateChange = num("cnomatechange", -19);
    t.offspring = std::max(1, num("coffspring", 1));
    t.babyToAdult = num("cbabytoadult", 0);
    t.smallPoo = num("csmallzoodoo", 0) != 0;
    t.buildingUseChance = num("cbuildingusechance", 0);
    t.maxEnergy = std::max(1, num("cmaxenergy", 100));
    t.swims = num("cswims", 0) != 0;
    t.underwater = num("cunderwater", 0) != 0;
    t.needDepth = num("cdepth", 0);
    t.tall = num("ctall", 0) == 1;
    t.youngTall = ai->getInt("y/characteristics/integers", "ctall", t.tall ? 1 : 0) == 1;
    t.breathThreshold = num("cbreaththreshold", 100); // (0x4b6c51: +0x1fc; setDefaults 0x4b659e: 100)
    t.breathIncrement = num("cbreathincrement", 0);   // (+0x200; 0)
    t.surfaceOnly = num("csurface", 0) == 1; // (0x4b5932: "== 1", into +0x17d)
    t.onlyUnderwater = num("conlyunderwater", 0) == 1; // (0x4b939f: "== 1", +0x17f)
    t.depthMin = num("cdepthmin", 0);                   // (0x4b7edd: +0x35c)
    t.depthMax = num("cdepthmax", 0);                   // (+0x360)
    t.depthChange = num("cdepthchange", -5);            // (+0x364)
    t.salinityChange = num("csalinitychange", -5);      // (+0x368)
    t.salinityHealthChange = num("csalinityhealthchange", -5); // (+0x36c)
    t.murkyThreshold[0] = num("cmurkywaterthreshold", 60);           // (+0x424)
    t.murkyThreshold[1] = num("cverymurkywaterthreshold", 20);       // (+0x428)
    t.murkyThreshold[2] = num("cextremelymurkywaterthreshold", 1);   // (+0x42c)
    t.murkyChange[0] = num("cmurkywaterchange", -5);                 // (+0x430)
    t.murkyChange[1] = num("cverymurkywaterchange", -10);            // (+0x434)
    t.murkyChange[2] = num("cextremelymurkywaterchange", -15);       // (+0x438)
    t.murkyHealthChange[0] = num("cmurkywaterhealthchange", -5);     // (+0x43c)
    t.murkyHealthChange[1] = num("cverymurkywaterhealthchange", -10); // (+0x440)
    t.murkyHealthChange[2] = num("cextremelymurkywaterhealthchange", -15); // (+0x444)
    t.keeper = num("ckeeper", 9500); // (+0x2f0: 9552 the porpoise's, 9551 the allosaurus')
    t.waterNeeded = num("cwaterneeded", 0);
    t.landNeeded = num("clandneeded", 0);
    t.enterWaterChance = num("centerwaterchance", 0);
    t.enterLandChance = num("centerlandchance", 0);
    t.underwaterNeeded = num("cunderwaterneeded", 0);
    t.enterTankChance = num("centertankchance", 0);
    t.drinkWaterChance = num("cdrinkwaterchance", 0);
    t.chaseAnimalChance = num("cchaseanimalchance", 0);
    t.preattack = num("cpreattack", 0) != 0;
    t.manEater = num("cismaneater", 0) != 0;
    t.resetPreyPosition = num("cresetpreyposition", 0) != 0;
    t.footX = num("cfootprintx", 0);
    t.footY = num("cfootprinty", 0);
    // (ZTUnitType::loadConfig 0x4b5681 at 0x4b58b7; ZTAnimalType's 0x4b6c51
    // at 0x4b8658..0x4b86af - the defaults 0x4b5c71, 0x4b659e)
    t.mapFoot = num("cmapfootprint", Original::unitMapFootprint);
    t.boxX = num("cboxfootprintx", Original::animalBoxFootprint);
    t.boxY = num("cboxfootprinty", Original::animalBoxFootprint);
    t.eggX = num("ceggfootprintx", Original::animalEggFootprint);
    t.eggY = num("ceggfootprinty", Original::animalEggFootprint);
    t.rampageThreshold = num("crampagethreshold", 0);
    t.rampageChance = num("crampagechance", 0);
    t.rampageTimeMin = num("crampagetimemin", 0);
    t.rampageTimeMax = num("crampagetimemax", 0);
    t.genus = num("cgenus", 0);
    t.family = num("cfamily", 0);
    t.era = num("cera", 0);
    t.treePref = num("ctreepref", 0);
    t.rockPref = num("crockpref", 0);
    t.elevationPref = num("celevationpref", 0);
    t.habitatPreference = num("chabitatpreference", 50);
    t.pctHabitat = num("cpcthabitat", 10);
    t.happyHabitatChange = num("chappyhabitatchange", 0);
    t.angryHabitatChange = num("cangryhabitatchange", 0);
    t.veryAngryHabitatChange = num("cveryangryhabitatchange", 0);
    t.captivity = num("ccaptivity", 0);
    t.numberMin = num("cnumberanimalsmin", 1);
    t.numberMax = num("cnumberanimalsmax", 100);
    t.numberMinChange = num("cnumberminchange", 0);
    t.numberMaxChange = num("cnumbermaxchange", 0);
    t.habitatSize = std::max(1, num("chabitatsize", 100));
    t.animalDensity = num("canimaldensity", 0);
    t.allCrowdedChange = num("callcrowdedchange", 0);
    t.otherAnimalAngryChange = num("cotheranimalangrychange", 0);
    t.crowd = num("ccrowd", 40);
    t.crowdHappinessChange = num("ccrowdhappinesschange", -1);
    t.needShelter = num("cneedshelter", 0) != 0;
    t.staySick = num("cstaysick", 0) != 0;
    t.keepMoving = num("ckeepmoving", 0) != 0;
    t.needToys = num("cneedtoys", 0) != 0;
    // Its lists: v = key, v = value in turn
    auto pairs = [&](const char *section, std::map<int, int> &out) {
      std::vector<std::string> v = ai->getList(section, "v");
      for (size_t i = 0; i + 1 < v.size(); i += 2)
        out[std::atoi(v[i].c_str())] = std::atoi(v[i + 1].c_str());
    };
    std::map<int, int> terrain;
    pairs("ccompatibleterrain", terrain);
    for (auto [k, v] : terrain)
      if (k >= 0 && k < 18)
        t.compatTerrain[k] = v;
    pairs("csuitableobjects", t.suitable);
    pairs("ccompatibleanimals", t.compatible);
    t.facesYOffset = num("cfacesyoffset", 0);
    t.attractiveness = num("cattractiveness", 0);
    t.keeperFood = std::max(0, num("ckeeperfoodtype", 0));
    if (Features::mods) {
      std::string food = lower(trim(ai->get("m/characteristics/strings", "ckeeperfood")));
      for (const CatalogItem &candidate : catalog.all())
        if (candidate.registrySection == "food" && candidate.type == food && candidate.fromMod)
          t.customFood = food;
    }
    t.jumper = num("cisjumper", 0) == 1;
    t.climber = num("cisclimber", 0) == 1;
    t.bash = num("cbashstrength", 0);
    t.crush = num("ccrushesfences", 0);
    t.testFenceChance = num("ctestfencechance", 0);
    t.testFenceChanceDrop = num("ctestfencechancedrop", 0);
    t.minTestFenceChance = num("cmintestfencechance", 0);
    t.zapHappinessChange = num("czaphappinesschange", 0);
    t.testFenceOffset = num("ctestfenceoffset", 1);
    {
      // (the young's section over the grown one's values)
      const std::string yInts = "y/characteristics/integers";
      auto young = [&](const char *key, int grown) { return ai->getInt(yInts, key, grown); };
      using Original::unitTilesPerSecond;
      t.young.slow = ai->get(yInts, "cslowrate", "").empty() ? t.slow : unitTilesPerSecond(young("cslowrate", 30));
      t.young.medium =
          ai->get(yInts, "cmediumrate", "").empty() ? t.medium : unitTilesPerSecond(young("cmediumrate", 45));
      t.young.fast = ai->get(yInts, "cfastrate", "").empty() ? t.fast : unitTilesPerSecond(young("cfastrate", 66));
      t.young.attractiveness = young("cattractiveness", t.attractiveness);
      t.young.chaseAnimalChance = young("cchaseanimalchance", t.chaseAnimalChance);
      t.young.bash = young("cbashstrength", t.bash);
      t.young.testFenceChance = young("ctestfencechance", t.testFenceChance);
      t.young.jumper = young("cisjumper", t.jumper ? 1 : 0) == 1;
      t.young.climber = young("cisclimber", t.climber ? 1 : 0) == 1;
    }
    t.laysEggs = num("clayseggs", 0) != 0;
    // (cExpansionID: ZTEntityType::loadConfig 0x4b4903 reads it from
    // [Characteristics/Integers] itself, 0x4b4b12 -> 0x4b4803 - not a
    // subtype's section)
    t.expansionId = ai->getInt("characteristics/integers", "cexpansionid", 0);
    t.eatsEggs = num("ceatseggs", 0) != 0;
    t.babiesAttack = num("cbabiesattack", 0) == 1;
    t.timeToHatch = num("ctimetohatch", 0);
    t.minHatchHits = num("cminhatchhits", 0);
    t.minHits = num("cminhits", 0);
    for (int s = 0; s < 3; s++) {
      static const char *subs[3] = {"m", "f", "y"};
      std::vector<std::string> list = ai->getList(std::string(subs[s]) + "/characteristics/integers", "cprey");
      if (list.empty() && s > 0)
        list = ai->getList(sectionOf("cprey"), "cprey");
      for (const std::string &p : list) {
        int id = std::atoi(p.c_str());
        t.eatsPeople = t.eatsPeople || id == 9503;
        if (id > 0 && id != 9503)
          t.preyIds[s].insert(id);
      }
    }
    // [Sounds] name = file, name = attenuation: a bare file is in the
    // animal's folder (ele1: animals/elephant/ele1.wav)
    std::string folder = item->file.substr(0, item->file.find_last_of('.'));
    for (const auto &kv : ai->getSection("sounds")) {
      std::string v = kv.second;
      size_t semi = v.find(';');
      std::string file = lower(trim(v.substr(0, semi)));
      int atten = semi == std::string::npos ? 0 : std::atoi(v.substr(semi + 1).c_str());
      if (file.empty())
        continue;
      if (file.find(".wav") == std::string::npos)
        file = folder + "/" + file + ".wav";
      t.sounds[lower(kv.first)] = {file, atten};
    }
    t.listImage = lower(ai->get("m/characteristics/strings", "clistimagename"));
    // Its animations, per subtype: "walk = walk" (<AnimPath>/walk/walk) or
    // a whole path; a subtype without its own list uses the male's names
    // with its own art
    const char *subs[3] = {"m", "f", "y"};
    for (int s = 0; s < 3; s++) {
      std::string base = lower(ai->get("animpath", subs[s]));
      if (base.empty())
        base = lower(ai->get("animpath", "m"));
      std::string section = std::string(subs[s]) + "/animations";
      std::map<std::string, std::string> names = ai->getSection(section);
      if (names.empty())
        names = ai->getSection("m/animations");
      if (names.empty())names = ai->getSection(defSub+"/animations");
      for (const auto &kv : names) {
        std::string v = lower(trim(kv.second));
        size_t semi = v.find(';');
        if (semi != std::string::npos)
          v = trim(v.substr(0, semi));
        if (v.empty())
          continue;
        std::string path = v.find('/') != std::string::npos ? stripAni(v) : base + "/" + v + "/" + v;
        // (named wrongly in the game's files, its art shipped under another
        // name: that art - Features::misnamedArt)
        if (Features::misnamedArt && !rm->hasResource(path + ".ani"))
          path = misnamedArt(path, rm);
        t.anims[s][lower(kv.first)] = path;
      }
    }
    // Behaviour sets: [m\BehaviorSet\bHappy] f = ... (one per step)
    for (const std::string &section : ai->getSections()) {
      for (int s = 0; s < 3; s++) {
        std::string prefix = std::string(subs[s]) + "\\behaviorset\\";
        if (section.compare(0, prefix.size(), prefix) != 0)
          continue;
        std::vector<Step> steps;
        for (const std::string &line : ai->getList(section, "f")) {
          Step st;
          if (parseStep(line, st))
            steps.push_back(st);
        }
        t.sets[s][section.substr(prefix.size())] = steps;
      }
    }
    // Female-only species (such as the mermaid) use their declared default.
    if(t.sets[0].empty())t.sets[0]=t.sets[defSub=="f"?1:0];
    // Show-only artwork comes from the actual trick definitions, rather than
    // guessing from names such as "swim" or "jump". Some stock ambient sets
    // reuse this artwork; those steps require a reserved, active performer.
    std::string tricksBase="m/tricks";
    if(ai->getList(tricksBase,"trick").empty())tricksBase=defSub+"/tricks";
    for(const auto &key:ai->getList(tricksBase,"trick")) {
      const std::string section=tricksBase+"/"+key;
      const int kind=ai->getInt(section,"type",0);
      if(kind==3 || ai->getInt(section,"visible",1)==0)continue; // feeding remains ordinary care
      const std::string behavior=lower(ai->get(section,"name",key));
      if(kind==0)t.showSets.insert("b"+behavior);
      else if(kind==1)for(int sex=0;sex<3;++sex)for(const auto &[name,steps]:t.sets[sex]) {
        const std::string suffix=behavior+"0";
        if(name.size()>=suffix.size() && name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0)
          t.showSets.insert(name);
      }
    }
    const std::set<std::string> shared={"walk","run","trot","stand","idle","swim","surfswim","subswim",
      "surfidle","subidle","water_idle","surface_rest","surface_2_rest","surface_feed","surface_eat"};
    std::set<std::string> visited;
    std::function<void(const std::string &)> collectTrick=[&](const std::string &name) {
      if(!visited.insert(name).second)return;
      for(int sex=0;sex<3;++sex) {
        auto it=t.sets[sex].find(name);if(it==t.sets[sex].end())continue;
        for(const auto &step:it->second) {
          const auto command=behaviorCommand(step.fn);
          if(command==Command::PlaySet || command==Command::PlaySetTank || command==Command::PlaySetTerrain)
            for(const auto &arg:step.args)collectTrick(arg);
          else if(command==Command::PlaySetProb)
            for(size_t i=0;i<step.args.size();i+=2)collectTrick(step.args[i]);
          const bool moving=command==Command::Move || command==Command::MoveWithSound || command==Command::MoveReverse;
          const bool playing=command==Command::Play || command==Command::PlayWithSound || command==Command::PlayTime ||
            command==Command::PlayReverse || command==Command::PlayPingPong || command==Command::PlayStopAtEnd ||
            command==Command::PlaySpecial || command==Command::SetBase;
          const size_t index=moving?2:0;
          if((moving || playing) && step.args.size()>index && !shared.count(step.args[index]))
            t.showAnimations.insert(step.args[index]);
        }
      }
    };
    for(const auto &name:t.showSets)collectTrick(name);
    // [AmbientAnims]: a = low, a = high, b = set; in turn (zoo.exe
    // ZTUnit::loadAmbientAnims 0x4f53bd, class slot 0x110, each section by
    // 0x4f5465: [AmbientAnims] unless its type's slot 0xc8 says not,
    // [AmbientAnimsWater] for a type that swims (slot 0x44),
    // [AmbientAnimsUnderwater] for one under water (0xc4) not kept to the
    // surface (0xc0), [AmbientAnimsSurface] for 0xc4. ours: each section a
    // file has, whatever its kind)
    std::vector<std::string> a = ai->getList("ambientanims", "a"), b = ai->getList("ambientanims", "b");
    for (size_t i = 0; i < b.size() && 2 * i + 1 < a.size(); i++)
      t.ambient.push_back({std::atoi(a[2 * i].c_str()), std::atoi(a[2 * i + 1].c_str()), lower(trim(b[i]))});
    for (auto [sec, list] : {std::make_pair("ambientanimsunderwater", &t.ambientUnder),
                             std::make_pair("ambientanimssurface", &t.ambientSurface)}) {
      std::vector<std::string> ua = ai->getList(sec, "a"), ub = ai->getList(sec, "b");
      for (size_t i = 0; i < ub.size() && 2 * i + 1 < ua.size(); i++)
        list->push_back({std::atoi(ua[2 * i].c_str()), std::atoi(ua[2 * i + 1].c_str()), lower(trim(ub[i]))});
    }
    std::vector<std::string> wa = ai->getList("ambientanimswater", "a"), wb = ai->getList("ambientanimswater", "b");
    for (size_t i = 0; i < wb.size() && 2 * i + 1 < wa.size(); i++)
      t.ambientWater.push_back({std::atoi(wa[2 * i].c_str()), std::atoi(wa[2 * i + 1].c_str()), lower(trim(wb[i]))});
    delete ai;
    this->typeList.push_back(t);
  }
  SDL_Log("[Animals] %zu animal types", this->typeList.size());
}

int Animals::typeOfFile(const std::string &file) const {
  std::string f = lower(file);
  for (size_t i = 0; i < this->typeList.size(); i++)
    if (this->typeList[i].file == f)
      return static_cast<int>(i);
  return -1;
}

bool Animals::isZooAnimal(int type) const {
  return type >= 0 && type < static_cast<int>(this->typeList.size()) &&
         this->typeList[type].expansion == 0;
}

void Animals::clear() {
  this->list.clear();
  this->counts.clear();
  this->nextId = 1;
  this->selected = this->hovered = this->carried = -1;
}

Animals::Member *Animals::member(int id) {
  for (Member &m : this->list)
    if (m.id == id)
      return &m;
  return nullptr;
}

const Animals::Member *Animals::member(int id) const {
  for (const Member &m : this->list)
    if (m.id == id)
      return &m;
  return nullptr;
}

void Animals::rehome(const WorldMap &map, const Fences &fences) {
  (void)map;
  for (Member &m : this->list) {
    if (m.id == this->carried)
      continue;
    int ex = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
    const Fences::Exhibit *e = ex >= 0 ? fences.exhibit(ex) : nullptr;
    // (a tank is no home to a land animal; a sea animal's it is)
    const Type &type=this->typeList[m.type];
    if (e && e->tank && !type.underwater)
      ex = -1;
    if(type.underwater && !type.onlyUnderwater)
      ex=fences.habitatRoot(ex,map);
    // A crate put down in an exhibit lets its animal out (every
    // cBoxedCheck, 2 ticks: about two seconds)
    if (m.boxed && ex >= 0) {
      if(e && e->show && m.showTank<0)continue; // A cancelled performer must be moved home before unboxing.
      if (m.boxTimer >= 2.0f) {
        m.boxed = false;
        m.escaped = false;
        m.boxTimer = 0;
        play(m, "stand", Member::Mode::Loop, 1.0f);
        this->markDirty(ex);
      } else {
        continue;
      }
    }
    if (ex == m.exhibit)
      continue;
    this->markDirty(m.exhibit);
    this->markDirty(ex);
    m.exhibit = ex;
    // (mid-jump over the fence: it lands first)
    bool overFence = m.crossAt >= 0 && static_cast<int>(m.pathAt) <= m.crossAt && m.pathAt < m.path.size();
    if (!overFence) {
      m.path.clear();
      m.pathAt = 0;
      m.crossAt = -1;
      m.stepping = false;
    }
    m.stack.clear();
    m.foodItem = -1;
    if (ex < 0 && !m.escaped && !m.boxed) {
      m.escaped = true;
      if (this->rm)
        this->messages.push_back(this->unitText(m, 10007)); // "%s has escaped." (escapeAlert 0x58cbf4)
    } else if (ex >= 0) {
      m.escaped = false;
    }
  }
}

// zoo.exe unitMessageText 0x49cfeb: the text by number, the unit's name
// (+0x108) through sprintf - when its type's +0xa4 starts with 'f' (an
// animal type is one per species and sex: 'm' / 'f', measured 2026-11-17)
// the number + 30000 (0x49d066), the female wording where a language has
// one. Every animal message goes through it: postUnitMessage 0x49d18e
// (10028 not happy, 10029 very sick, 10030 no food, 10031 its exhibit,
// 10032 given birth, 15000 / 15001 ill, 10322 hatched), 0x4807c2 (10010
// taken away, 10242 old age) and escapeAlert 0x58cbf4 (10007).
// ours: where the + 30000 text isn't in the game's language files (40322)
// the original's buffer is left unfilled; the number's own text instead
std::string Animals::unitText(const Member &m, int textId) const {
  if (!this->rm)
    return {};
  std::string s;
  if (m.female)
    s = this->rm->getString(textId + 30000);
  if (s.empty())
    s = this->rm->getString(textId);
  if (size_t at = s.find("%s"); at != std::string::npos)
    s.replace(at, 2, m.name);
  return s;
}

// Getting out of its exhibit (zoo.exe 0x446088, 0x43e363): a fence on
// its boundary it can climb, jump or break through; it walks to it and
// over, a basher (or a jumper with any strength) breaking it as it goes
bool Animals::breakOut(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  // (any animal walks out through a broken piece: passable() has it)
  if (m.exhibit < 0)
    return false;
  const Fences::Exhibit *ex = fences.exhibit(m.exhibit);
  if (!ex || ex->tank)
    return false;
  float best = 1e9f;
  Fences::Edge way{false, -1, -1};
  std::pair<int, int> inTile, outTile;
  for (auto [x, y] : ex->tiles) {
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &dd : d) {
      int nx = x + dd[0], ny = y + dd[1];
      if (ex->tiles.count({nx, ny}) || fences.exhibitAt(nx, ny) >= 0 || !fences.insideZoo(nx, ny))
        continue;
      Fences::Edge e = dd[0] ? Fences::Edge{false, std::max(x, nx), y} : Fences::Edge{true, x, std::max(y, ny)};
      // (a gap where a piece was taken away, or a broken one: any animal)
      const Type::Age age = t.forAge(m.baby);
      if (!fences.passable(e, age.climber, age.jumper, age.bash, t.crush))
        continue;
      float dist = std::hypot(x + 0.5f - m.x, y + 0.5f - m.y);
      if (dist < best) {
        best = dist;
        way = e;
        inTile = {x, y};
        outTile = {nx, ny};
      }
    }
  }
  if (way.x < 0 || !walkTo(m, inTile.first + 0.5f, inTile.second + 0.5f, map, fences))
    return false;
  // Over it, to the tile outside
  m.path.push_back({outTile.first + 0.5f, outTile.second + 0.5f});
  m.crossing = way;
  m.crossingFromX = inTile.first;
  m.crossingFromY = inTile.second;
  // How (zoo.exe 0x5db443): a jumper over a jumpable fence jumps, else a
  // climber over a climbable one climbs, else it bashes through (or walks
  // through a broken one); a bash, or a jump by an animal with any bash
  // strength, breaks it (0x613a1c)
  bool broken = !fences.at(way) || fences.broken(way);
  const Type::Age age = t.forAge(m.baby);
  bool jump = !broken && age.jumper && fences.passable(way, false, true, 0, 0);
  bool climb = !broken && !jump && age.climber && fences.passable(way, true, false, 0, 0);
  m.crossAnim = jump ? "jump_high" : climb ? "climb_up" : "";
  m.crossAt = static_cast<int>(m.path.size()) - 1;
  m.breaks = !broken && age.bash > 0 && !climb;
  return true;
}

bool Animals::hunts(const Member &predator, const Member &prey) const {
  if (predator.id == prey.id)
    return false;
  const Type &t = this->typeList[predator.type];
  // (an egg: only by one that eats eggs, of another kind - zoo.exe
  // 0x43971c, cEatsEggs)
  if (prey.egg)
    return !prey.eaten && t.eatsEggs && predator.type != prey.type;
  if (predator.baby)
    return false;
  return t.preyIds[sub(predator)].count(this->typeList[prey.type].nameId) > 0;
}

// Both stand off (zoo.exe 0x42fad3): the hunter's kind and the other's have
// cPreattack, and the other isn't an egg
bool Animals::preattacks(const Member &hunter, const Member &other) const {
  return this->typeList[hunter.type].preattack && this->typeList[other.type].preattack && !other.egg;
}

// The stand-off (zoo.exe 0x4a4c0c -> its state 0x12 at 0x61b2d6): within
// cDefenseRadius both stop where they are, face to face; the hunter plays
// its bPreattack ("Started preattack"), the other its bDefense ("Started
// defense") - then the chase goes on
void Animals::standOff(Member &hunter, Member &other) {
  ZT_STEP("0x61b2d6", "Stands off its prey before the chase: bPreattack against its bDefense", {"prey", other.name},
          {"distance", std::hypot(other.x - hunter.x, other.y - hunter.y)}, {"cDefenseRadius", kDefenseRadius});
  hunter.preattacked = true;
  for (Member *a : {&hunter, &other}) {
    a->stack.clear();
    a->path.clear();
    a->pathAt = 0;
    a->stepping = false;
  }
  faceToward(hunter, other.x, other.y);
  faceToward(other, hunter.x, hunter.y);
  pushSet(hunter, "bpreattack");
  pushSet(other, "bdefense");
}

// The stand-off is the original's Job18_Defend (job 0x12, jobId 0x61b2a7;
// ctor 0x61b25b - an animal's only -, dtor 0x61b2ad): each tick its
// checkArrived (0x61b4c6) keeps it while the animal's set is still its
// bPreattack (the hunter, +0x3a4) or bDefense (the other, +0x24c); then
// atTarget (0x61b5d9) lets both go (+0x20d) and the job ends - here, the
// set off the stack
bool Animals::standingOff(const Member &m) const {
  for (const auto &s : m.stack)
    if (s.first == "bpreattack" || s.first == "bdefense")
      return true;
  return false;
}

void Animals::moveOf(const Member &m, const std::string &setName, std::string &anim, float &speed) const {
  const Type &t = this->typeList[m.type];
  anim = "run";
  speed = t.forAge(m.baby).fast;
  if (const std::vector<Step> *steps = this->set(m, setName))
    for (const Step &s : *steps)
      if (s.fn == "fmove" && s.args.size() >= 4) {
        std::string a = s.args[2];
        a.erase(std::remove(a.begin(), a.end(), '"'), a.end());
        if (!a.empty())
          anim = lower(trim(a));
        if (std::atoi(s.args[3].c_str()) > 0)
          speed = Original::unitTilesPerSecond(std::atoi(s.args[3].c_str()));
        return;
      } else if (s.fn == "frun") {
        return;
      }
}

// Busy: crated, darted, carried, dying, caught, killing, in a shelter, or
// being seen to by a keeper - not hunted, not hunting
bool Animals::busy(const Member &m) const {
  return m.boxed || m.tranquilised || m.dying || m.id == this->carried || m.caughtBy >= 0 || m.victim >= 0 ||
         m.building >= 0 || m.inside || m.claimedBy >= 0 || m.escaped || m.exhibit < 0 || m.showTank >= 0;
}

// The original's water state 4 (unit +0x258, set by 0x410857 from where it
// is in its tank; read by the breath check 0x4389f7, the surfacing test
// 0x43750c and the underwater mood list 0x43ca83 through unitWaterState
// 0x410919, class slot 0x2c8, which returns it). ours: under the water as
// the mood sets take it - in a tank, below its surface band
bool Animals::underwaterNow(const Member &m) const {
  const int tank = this->tankAt ? this->tankAt(m.x, m.y) : -1;
  return tank >= 0 && this->tankDepth && m.swimZ < this->tankDepth(tank) - 0.35f;
}

// zoo.exe 0x440a37 (animalStartBehaviour's, animalCheckTiredness's and
// animalCheckHealth's test before bSleep or bSick): one that lives only
// underwater (cOnlyUnderwater, type +0x17f through class slot 0x170) may
// anywhere; any other not while it's in water mode or tank mode (+0x380, 1
// or 2 - ours: waterMode), on a water tile (tile +0x83 & 3) or in a tank
// (its water state, slot 0x2c8); and none while it's an egg (+0x395) or on a
// show (slot 0x22c - ours: showTank)
bool Animals::mayRest(const Member &m) const {
  const Type &t = this->typeList[m.type];
  if (!t.onlyUnderwater && (m.waterMode || onWater(m) || (this->tankAt && this->tankAt(m.x, m.y) >= 0)))
    return false;
  return !m.egg && m.showTank < 0;
}

// zoo.exe 0x420fed (ZTAnimal's class slot 0x288, which chooseTask 0x436f5c
// asks before the in-exhibit hunt): true while fleeing (ZTUnit's 0x420f65,
// +0x25c - ours: fleeFrom); while the set it plays (+0x54) is bSick, bSleep,
// bEat or bTestFence; while it's after keeper food (+0x39f, Task 0x72 - ours:
// its bFindKeeperFood), water (+0x3a1, Task 0x7a - ours: drinkX), air (Task
// 0x7c - ours: surfacing) or a building (+0x3b1 - ours: rampageBuilding); on
// a show (Tasks 0x85, 0x86 - ours: showTank, busy too)
bool Animals::occupied(const Member &m) const {
  if (m.fleeFrom >= 0 || m.drinkX >= 0 || m.surfacing || m.rampageBuilding >= 0 || m.showTank >= 0)
    return true;
  if (!m.stack.empty()) {
    const std::string &set = m.stack.front().first;
    if (set == "bsick" || set == "bsleep" || set == "beat" || set == "btestfence")
      return true;
  }
  return std::any_of(m.stack.begin(), m.stack.end(), [](const auto &f) { return f.first == "bfindkeeperfood"; });
}

// The nearest adult in its exhibit that hunts it within the radius (zoo.exe
// 0x4369b9: 7, cPredatorRadius - whatever it's doing), or one chasing it
// at play
const Animals::Member *Animals::threatTo(const Member &m, float radius) const {
  const Member *best = nullptr;
  float bestD = radius * radius;
  for (const Member &o : this->list) {
    if (o.exhibit != m.exhibit || busy(o))
      continue;
    bool chasing = o.huntAnimal == m.id;
    if (!chasing && !hunts(o, m))
      continue;
    float d = (o.x - m.x) * (o.x - m.x) + (o.y - m.y) * (o.y - m.y);
    if (d < bestD) {
      bestD = d;
      best = &o;
    }
  }
  return best;
}

// Away from it (zoo.exe 0x4a639e): a tile it can stand on, on the ring
// cPredatorRadius + 2 (9) out, the way away from it or a turn either side;
// nearer in if none
void Animals::runFrom(Member &m, const Member &threat, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  float ax = m.x - threat.x, ay = m.y - threat.y;
  float a = (std::fabs(ax) + std::fabs(ay) < 0.01f) ? std::uniform_real_distribution<float>(0, 6.2832f)(this->rng)
                                                    : std::atan2(ay, ax);
  int mx = static_cast<int>(std::floor(m.x)), my = static_cast<int>(std::floor(m.y));
  std::string anim;
  float speed;
  moveOf(m, "brunfrompredator", anim, speed);
  for (int r = 9; r >= 1; r--)
    for (float turn : {0.0f, 0.7854f, -0.7854f}) {
      // (to the ring: the larger of the two steps is r)
      float cx = std::cos(a + turn), cy = std::sin(a + turn);
      float k = r / std::max(std::fabs(cx), std::fabs(cy));
      int x = mx + static_cast<int>(std::lround(cx * k)), y = my + static_cast<int>(std::lround(cy * k));
      if (!standable(x, y, t, m.exhibit, map, fences))
        continue;
      m.stack.clear();
      m.moveAnim = anim;
      m.speed = speed;
      setAnimRate(m, 2); // (bRunFromPredator's fRun - 0x4a5194)
      if (walkTo(m, x + 0.5f, y + 0.5f, map, fences))
        return;
    }
}

// After it (zoo.exe 0x4a5b4d): where it is, or a step or two along where
// it's going, at its bChasePrey pace. Task 0x74: begin 0x4a4ef2 ("Started
// chasing", the prey +0x2e4, its start time +0x138); whileGoing 0x4a4d6c
// routes it (an egg: to its tile, 0x49f6e8) and plays bChasePrey - every
// stock one fMove(prey,0,"run",76) - "Chasing"; no way there: 0x4a5e00(1),
// "Stopped catching prey", the chase flags (+0x3a4, +0x3a5) cleared
void Animals::chase(Member &m, const WorldMap &map, const Fences &fences) {
  const Member *p = this->member(m.huntAnimal);
  if (!p)
    return;
  float gx = p->x, gy = p->y;
  int ahead = std::min<int>(3, static_cast<int>(p->path.size()) - static_cast<int>(p->pathAt) - 1);
  if (ahead > 0) {
    int k = std::uniform_int_distribution<int>(0, ahead)(this->rng);
    auto [nx, ny] = p->path[p->pathAt + k];
    float dn = (nx - m.x) * (nx - m.x) + (ny - m.y) * (ny - m.y);
    float dp = (p->x - m.x) * (p->x - m.x) + (p->y - m.y) * (p->y - m.y);
    if (dn <= dp) {
      gx = nx;
      gy = ny;
    }
  }
  {
    // Approach the body's edge; the catch starts at contact, not inside it.
    float dx=m.x-p->x,dy=m.y-p->y,d=std::hypot(dx,dy);
    float spacing=bodyRadius(m)+bodyRadius(*p)+0.12f;
    if(d<0.001f){dx=1;dy=0;d=1;}
    gx=p->x+dx/d*spacing;gy=p->y+dy/d*spacing;
    if(tankAt && tankAt(m.x,m.y)>=0)m.swimTo=p->swimZ;
  }
  std::string anim;
  float speed;
  moveOf(m, "bchaseprey", anim, speed);
  m.stack.clear();
  m.moveAnim = anim;
  m.speed = speed;
  setAnimRate(m, -1); // (bChasePrey's fMove(prey,0,run,76): no 5th, 100 - 0x425533)
  if (!walkTo(m, gx, gy, map, fences))
    m.huntAnimal = -1; // (no way to it: given up)
}

// zoo.exe fHidePredator 0x50e879 -> 0x50e816: the guest's attacker
// (0x412259: its +0x22c, a unit not +0x143/+0x142), an animal: slot
// 0xcc(0) - +0x13f cleared (not drawn: setupShadow 0x43f81c skips it; its
// panel 0x43f0c5)
void Animals::hidePredator(int id) {
  if (Member *m = this->member(id))
    m->hidden = true;
}

// zoo.exe fShowPredator 0x50e764 -> 0x50e6fa: the guest's attacker, an
// animal: animalEatenMessage 0x50e648 (attacker, guest) - a guest victim of
// a type with cIsManEater (+0x3d5) is eaten: slot 0xcc(0) and
// unitMarkRemoved 0x4fa78e(1), no message (11002 is for a staff victim);
// any other guest nothing. Then the guest's slot 0x254 (0x431af4): the
// attacker shown (slot 0xcc(1)), +0x3a5 and +0x20d (frozen) cleared and its
// goal cleared (slot 0x174, ZTAnimal::clearGoal 0x43e784)
void Animals::showPredator(int id, int guest) {
  Member *m = this->member(id);
  if (!m)
    return;
  if (this->typeList[m->type].manEater && this->takeGuestAway) {
    ZT_STEP("0x50e648", "A man-eater: the guest is eaten", {"animal", m->name});
    this->takeGuestAway(guest);
  }
  m->hidden = false;
  if (m->mauling == guest) {
    m->mauling = -1;
    m->prey = -1;
    m->stack.clear();
    m->path.clear();
    m->pathAt = 0;
    m->stepping = false;
  }
}

void Animals::tranquilise(int id) {
  Member *m = this->member(id);
  if (!m)
    return;
  m->tranquilised = true;
  // (darted: the chase is over, and a mauling with it)
  m->prey = -1;
  m->mauling = -1;
  m->hidden = false;
  m->stack.clear();
  m->path.clear();
  m->pathAt = 0;
  m->energy = static_cast<float>(this->typeList[m->type].maxEnergy);
  play(*m, "lie_down", Member::Mode::Once);
}

void Animals::box(int id) {
  Member *m = this->member(id);
  if (!m)
    return;
  m->boxed = true;
  m->tranquilised = false;
  m->escaped = false;
  m->prey = -1;
  m->mauling = -1;
  m->hidden = false;
  m->claimedBy = -1;
  m->stack.clear();
  m->path.clear();
  m->pathAt = 0;
  play(*m, "stand", Member::Mode::Loop, 1.0f);
}

void Animals::boxSeaAnimals(const std::set<std::pair<int, int>> &tiles) {
  for (Member &m : this->list) {
    if (m.boxed || m.id == this->carried || !this->typeList[m.type].underwater)
      continue;
    if (tiles.count({static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y))}))
      this->box(m.id);
  }
}

int Animals::escapedCount() const {
  int n = 0;
  for (const Member &m : this->list)
    n += m.escaped ? 1 : 0;
  return n;
}

int Animals::dinosaursIn(int exhibit) const {
  int n = 0;
  for (const Member &m : this->list)
    n += m.exhibit == exhibit && m.id != this->carried && this->typeList[m.type].dinosaur ? 1 : 0;
  return n;
}

int Animals::countIn(int exhibit) const {
  int n = 0;
  for (const Member &m : this->list)
    n += m.exhibit == exhibit && m.id != this->carried ? 1 : 0;
  return n;
}

// ----------------------------------------------------------------------------
// Art
// ----------------------------------------------------------------------------
Animation *Animals::underArt(const Member &m, Animation *a) const {
  auto c = this->underCache.find(a);
  if (c != this->underCache.end())
    return c->second;
  // (its art's own name - the path's last part, surface_jump_spin's is
  // jumpspin - and "under" + that among the type's animations)
  Animation *u = nullptr;
  const Type &t = this->typeList[m.type];
  for (const auto &[path, art] : this->artCache)
    if (art == a) {
      // (only the animal's own art has layers: a crate's "idle" isn't its
      // idle)
      // (and the tank's fight clouds - surfc's undersurfc, subc's shadowsubc
      // - but not a crate's plain "idle")
      if (path.rfind("animals/", 0) != 0 && path.rfind("objects/surfcld/", 0) != 0 &&
          path.rfind("objects/subcld/", 0) != 0)
        break;
      std::string f = path.substr(path.find_last_of("/\\") + 1);
      for (int s : {sub(m), 0}) {
        auto it = t.anims[s].find("under" + f);
        if (it != t.anims[s].end() && !it->second.empty()) {
          u = this->rm && this->rm->hasResource(it->second + ".ani") ? this->rm->getAnimation(it->second) : nullptr;
          break;
        }
      }
      break;
    }
  this->underCache[a] = u;
  return u;
}

Animation *Animals::shadowArt(const Member &m, Animation *a) const {
  auto c = this->shadowCache.find(a);
  if (c != this->shadowCache.end())
    return c->second;
  // (as underArt: the art's own name - subswim's, huntl's - and "shadow" +
  // that among the type's animations: shadowsubswim, shadowhuntl)
  Animation *sh = nullptr;
  const Type &t = this->typeList[m.type];
  for (const auto &[path, art] : this->artCache)
    if (art == a) {
      // (and the tank's fight clouds - surfc's undersurfc, subc's shadowsubc
      // - but not a crate's plain "idle")
      if (path.rfind("animals/", 0) != 0 && path.rfind("objects/surfcld/", 0) != 0 &&
          path.rfind("objects/subcld/", 0) != 0)
        break;
      std::string f = path.substr(path.find_last_of("/\\") + 1);
      for (int s : {sub(m), 0}) {
        auto it = t.anims[s].find("shadow" + f);
        if (it != t.anims[s].end() && !it->second.empty()) {
          sh = this->rm && this->rm->hasResource(it->second + ".ani") ? this->rm->getAnimation(it->second) : nullptr;
          break;
        }
      }
      break;
    }
  this->shadowCache[a] = sh;
  return sh;
}

Animation *Animals::art(const Member &m, const std::string &nameIn, bool orStand) const {
  const Type &t = this->typeList[m.type];
  int s = sub(m);
  // (a swimmer on a water tile: walking and running are its swim, standing
  // its water idle - zoo.exe 0x41338a, 0x413491)
  std::string name = nameIn;
  // A tank has separate surface and submerged locomotion. Amphibious
  // species retain land walk art, so their submerged move needs an alias too.
  // (zoo.exe ZTUnit::unitSlowMoveName 0x4135fd, class slot 0x120: under the
  // water, +0x258 4, its sub-swim - a rising or diving one by +0x208 1 / -1,
  // ours the plain subswim -; at the surface, 3, its surface swim; on water
  // its swim; else its type's walk, slot 0xa8)
  if (t.underwater && this->tankAt && this->tankAt(m.x, m.y) >= 0 && this->tankDepth) {
    bool top=m.swimZ >= this->tankDepth(this->tankAt(m.x,m.y))-0.35f;
    std::string alias;
    if(name=="walk" || name=="run" || name=="trot")alias=top?"surfswim":"subswim";
    else if(name=="stand" || name=="idle")alias=top?"surfidle":"subidle";
    if(!alias.empty() && (t.anims[s].count(alias) || t.anims[0].count(alias)))name=alias;
  }
  if (t.swims && onWater(m)) {
    if (name == "walk" || name == "run" || name == "trot")
      name = "swim";
    else if (name == "stand" || name == "idle")
      name = "water_idle";
  }
  auto it = t.anims[s].find(name);
  if (it == t.anims[s].end() && s != 0) {
    // (a subtype without that animation: the adult's)
    auto adult = t.anims[0].find(name);
    if (adult != t.anims[0].end()) {
      it = adult;
      s = 0;
    }
  }
  if (it == t.anims[s].end()) {
    if (!orStand)
      return nullptr;
    it = t.anims[s].find("stand");
    if (it == t.anims[s].end())
      it = t.anims[s].find("idle");
    if (it == t.anims[s].end())
      return nullptr;
  }
  const std::string &path = it->second;
  auto c = this->artCache.find(path);
  if (c != this->artCache.end())
    return c->second;
  // (the one loading ahead: waited for, not read a second time)
  Animation *ahead = nullptr;
  if (this->rm && this->rm->takeAnimationAhead(path, ahead, true))
    return this->artCache[path] = ahead;
  Animation *a = this->rm && this->rm->hasResource(path + ".ani") ? this->rm->getAnimation(path) : nullptr;
  this->artCache[path] = a;
  return a;
}

Animation *Animals::crate(const Member &m) const {
  if (m.type >= 0 && m.type < static_cast<int>(this->typeList.size())) {
    const Type &t = this->typeList[m.type];
    int s = sub(m);
    if (t.anims[s].count("box_idle") || t.anims[0].count("box_idle"))
      if (Animation *a = this->art(m, "box_idle"))
        return a;
  }
  return this->crateArt;
}

Animation *Animals::preview(int type, bool female) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return nullptr;
  Member m;
  m.type = type;
  m.female = female;
  return this->art(m, "stand");
}

// A play's length (zoo.exe 0x42514c: an animation's frames, less its
// background frame - our frame count holds none - x its frame time in
// ms). ours: no art, a second; no frame time, 100 ms
float Animals::animSeconds(const Member &m, const std::string &anim) const {
  Animation *a = this->art(m, anim);
  if (!a)
    return 1.0f;
  float ms = a->frameTimeMs() ? static_cast<float>(a->frameTimeMs()) : 100.0f;
  return std::max(1, a->frameCount()) * ms / 1000.0f;
}

// ----------------------------------------------------------------------------
// Ground: where an animal can be
// ----------------------------------------------------------------------------
float Animals::groundAt(const WorldMap &map, float x, float y) const {
  const MapTile *t = map.getTile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  if (!t)
    return 0;
  float fx = x - std::floor(x), fy = y - std::floor(y);
  float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
  float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
  return top * (1 - fy) + bottom * fy;
}

// Its exhibit's ground: not water (unless it swims), nothing standing there
bool Animals::standable(int x, int y, const Type &t, int exhibit, const WorldMap &map,
                        const Fences &fences) const {
  // Loose (no exhibit): the zoo's open ground; else its exhibit's
  int physical=fences.exhibitAt(x,y);
  if (physical != exhibit && !(t.underwater && !t.onlyUnderwater &&
      fences.habitatRoot(physical,map)==fences.habitatRoot(exhibit,map)))
    return false;
  if (exhibit < 0 && !fences.insideZoo(x, y))
    return false;
  const MapTile *tile = map.getTile(x, y);
  if (!tile)
    return false;
  if (!t.swims && (tile->terrainType == 9 || tile->terrainType == 10))
    return false;
  if (this->index && this->index->blocked(x, y)) {
    // Floor scenery does not seal the water column above it. Land walkers
    // still avoid it; tank swimmers can pass over plants and rocks.
    const auto *pool=physical>=0?fences.exhibit(physical):nullptr;
    if(!(t.underwater && pool && pool->tank &&
         this->index->at(x,y)==WorldIndex::What::Object))return false;
  }
  return true;
}

// From a tile to its neighbour: no fence between, no cliff
bool Animals::canStep(int x, int y, int nx, int ny, const WorldMap &map, const Fences &fences) const {
  Fences::Edge e = nx != x ? Fences::Edge{false, std::max(x, nx), y}
                           : Fences::Edge{true, x, std::max(y, ny)};
  if (fences.at(e) && !fences.broken(e))
    return false;
  const MapTile *a = map.getTile(x, y), *b = map.getTile(nx, ny);
  if (!a || !b)
    return false;
  int a0, a1, b0, b1;
  if (nx > x) {
    a0 = a->cornerHeight[CORNER_X1Y0]; a1 = a->cornerHeight[CORNER_X1Y1];
    b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X0Y1];
  } else if (nx < x) {
    a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X0Y1];
    b0 = b->cornerHeight[CORNER_X1Y0]; b1 = b->cornerHeight[CORNER_X1Y1];
  } else if (ny > y) {
    a0 = a->cornerHeight[CORNER_X0Y1]; a1 = a->cornerHeight[CORNER_X1Y1];
    b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X1Y0];
  } else {
    a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X1Y0];
    b0 = b->cornerHeight[CORNER_X0Y1]; b1 = b->cornerHeight[CORNER_X1Y1];
  }
  return a0 == b0 && a1 == b1;
}

Fences::Fit Animals::canPlace(int type, float x, float y, const WorldMap &map,
                              const Fences &fences, int facing) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return Fences::Fit::Outside;
  int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  int ex = fences.exhibitAt(tx, ty);
  const Fences::Exhibit *e = ex >= 0 ? fences.exhibit(ex) : nullptr;
  const Type &t = this->typeList[type];
  this->placeMessage = 0;
  // A big one's whole footprint (0x4f7fbd, from animalPlacementRule
  // 0x4f86fb before tankCheckPlacement, which runs only when it fits)
  const bool footOk = footprintFits(type, x, y, map, fences, facing);
  // In a tank (zoo.exe 0x4df688): one that lives underwater ("This animal
  // doesn't swim in tanks..."), with water ("...a tank with water in it.")
  // deep enough for it (cDepth: "...a deeper tank.")
  if (e && e->tank) {
    if (!footOk)
      return Fences::Fit::InTheWay;
    if (e->show && !t.showAnimal) {
      this->placeMessage = 10561;
      return Fences::Fit::Outside;
    }
    if (!t.underwater) {
      this->placeMessage = 10553;
      return Fences::Fit::Outside;
    }
    int level = this->tankLevel ? this->tankLevel(ex) : 0;
    ZT_STEP("0x4df688", "In a tank: its water against what it needs", {"lives underwater (cUnderwater)", t.underwater ? 1 : 0},
            {"tank water (height units)", level}, {"needs (cDepth)", t.needDepth},
            {"verdict", level <= 0 ? "no water" : level < t.needDepth ? "not deep enough" : "deep enough"});
    if (level <= 0) {
      this->placeMessage = 10554;
      return Fences::Fit::Outside;
    }
    if (level < t.needDepth) {
      this->placeMessage = 10560;
      return Fences::Fit::Outside;
    }
  } else if (e && t.underwater && !t.swims) {
    // (a sea animal on land: a tank's its only home)
    this->placeMessage = 10554;
    return Fences::Fit::Outside;
  }
  // On water that isn't a tank (zoo.exe 0x4e1564: the tile's water flags,
  // +0x83 & 3), one that doesn't swim (slot 0xbc): a sea animal 10554
  // ("...a tank with water in it."), an egg 10570 ("Eggs must be placed on
  // land."), any other 10569 ("This animal cannot swim and must be placed
  // on land.")
  if (!(e && e->tank) && !t.swims && isWater(map, tx, ty)) {
    const Member *held = this->carried >= 0 ? this->member(this->carried) : nullptr;
    this->placeMessage = t.underwater ? 10554 : held && held->egg ? 10570 : 10569;
    return Fences::Fit::Outside;
  }
  // A zoo animal: a land exhibit
  if (!e || !standable(tx, ty, t, ex, map, fences))
    return Fences::Fit::Outside;
  // (the footprint's refusal has no message: the exhibit's, above, wins)
  if (!footOk)
    return Fences::Fit::InTheWay; // (red, no message)
  // A steep tile (zoo.exe zooViewMouse 0x4ded90's animal branch, 0x4e1279
  // -> 0x5ad00a -> 0x5cd93c: the slope code +0x81 not flat nor a plain
  // slope - WorldMap::pathShapeOk) refuses one whose footprint covers 4 or
  // more half-tile squares (its +0x150, x times y of class slot 0x94's
  // footprint, UnitBase::addedToWorld 0x412cdc), with no message; a small
  // one is put down anyway. The one being placed: a held egg its egg's
  // footprint, a held crate its box's. (One over 2 half tiles either way
  // got the footprint-wide check 0x4f7fbd above: footprintFits)
  {
    const Member *held = this->carried >= 0 ? this->member(this->carried) : nullptr;
    int fx = t.footX, fy = t.footY;
    if (held && held->type == type && held->egg)
      fx = t.eggX, fy = t.eggY;
    else if (held && held->type == type && held->boxed)
      fx = t.boxX, fy = t.boxY;
    if (fx * fy >= Original::animalSteepFootprintArea && !map.pathShapeOk(tx, ty))
      return Fences::Fit::InTheWay; // (red, no message: +0x14 left unset)
  }
  // Not on top of another (the original shows red over one)
  for (const Member &m : this->list)
    if (m.id != this->carried && std::fabs(m.x - x) < 0.5f && std::fabs(m.y - y) < 0.5f)
      return Fences::Fit::InTheWay;
  return Fences::Fit::Ok;
}

// zoo.exe 0x4f7fbd (world, unit, position, lenient, unit to skip - the
// check objects get too), as animalPlacementRule 0x4f86fb calls it for an
// animal whose footprint (slot 0x94: its own cFootprintX/Y, its crate's or
// its egg's) is over 2 half tiles either way: lenient unless it's in its
// crate (+0x394), no unit skipped. Over slot 0x98's footprint (type +0xc0,
// the loader's copy of cFootprintX/Y - the animal's own, even in its crate
// or egg), a half tile apart from (1 - n) / 4 of a tile back of the point,
// each point's tile (0x40fa48: off the map, the point fails):
// - from its crate only: not water unless it swims (slot 0xbc: an egg
//   doesn't; else the type's) - water refuses the whole footprint at once;
//   the tile's quarter under the point empty (tile +4..); things on the tile
//   crossing its box (slot 0x24) skipped for an animal (slot 0xb4: 1); a
//   footprint (slot 0x94) over 1 either way: the tile flat (+0x81 0) and at
//   the first tile's height (+0x3c)
// - in the zoo (0x40fb8d(1): +0x83 & 0x40, or editing a map)
// - slot 0x98's footprint over 2 either way: the edges inside the
//   footprint - toward -y (side 0) past the first two rows, +x (2) before
//   the last two columns, +y (4) before the last two rows, -x (6) past the
//   first two columns - with no fence (tile +0x14 + side * 2) and not a
//   cliff (tile +0x82 >> side & 3 over 1)
// It refuses with no message of its own (+0x14 left as it was).
// Both footprints swap x and y when its facing +0x12c & 3 isn't 0 (slot
// 0x98 0x4272d4; slot 0x94 0x410803 - crate, egg - and 0x40f916): a new
// one's facing is zooViewMouse 0x4e00d3's ((Adopt's rotation 0x63bb50,
// given the view at +0x370 by 0x4df56d - the view's rotation) * 2 - 4) & 7,
// here `facing` (World::worldFacing of the panel's); one picked up and put
// down again keeps its own (that branch needs setPlaceItem's new-unit flag)
// ours: the quarter is our tile index's whole tile
bool Animals::footprintFits(int type, float x, float y, const WorldMap &map, const Fences &fences,
                            int facing) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return false;
  const Type &t = this->typeList[type];
  const Member *held = this->carried >= 0 ? this->member(this->carried) : nullptr;
  if (held && held->type != type)
    held = nullptr;
  const bool egg = held && held->egg, boxed = held && held->boxed;
  if (held)
    facing = facingOf(*held);
  const bool turned = (facing & Original::footprintSwapMask) != 0;
  int fx = t.footX, fy = t.footY;
  if (egg)
    fx = t.eggX, fy = t.eggY;
  else if (boxed)
    fx = t.boxX, fy = t.boxY;
  if (turned)
    std::swap(fx, fy);
  if (fx <= Original::animalFootprintCheckOver && fy <= Original::animalFootprintCheckOver)
    return true; // (0x4f86fb doesn't call it)
  const int gx = turned ? t.footY : t.footX, gy = turned ? t.footX : t.footY;
  if (fx == 0 || fy == 0)
    return false;
  const bool lenient = !boxed, swims = !egg && t.swims;
  const bool flatToo = fx > Original::footprintFlatOver || fy > Original::footprintFlatOver;
  const bool edges = gx > Original::footprintEdgesOver || gy > Original::footprintEdgesOver;
  auto cliff = [&](const MapTile *tile, int side) {
    // (byte 6 of a map's tile holds side 0 in its top two bits: 0x4c864e)
    return ((tile->edgeBits >> (6 - side)) & 3) > Original::footprintCliffCodeMax;
  };
  bool ok = true;
  int firstHeight = 0;
  bool haveFirst = false;
  for (int i = 0; i < gx && ok; i++) {
    for (int j = 0; j < gy && ok; j++) {
      const float px = x + (1 - gx) * 0.25f + i * 0.5f, py = y + (1 - gy) * 0.25f + j * 0.5f;
      const int tx = static_cast<int>(std::floor(px)), ty = static_cast<int>(std::floor(py));
      const MapTile *tile = map.getTile(tx, ty);
      if (!lenient && !swims && tile && isWater(map, tx, ty))
        return false;
      if (!tile || !fences.insideZoo(tx, ty)) {
        ok = false;
        continue;
      }
      if (!lenient) {
        ok = !(this->index && this->index->blocked(tx, ty));
        if (flatToo) {
          const bool flat = tile->cornerHeight[0] == tile->height && tile->cornerHeight[1] == tile->height &&
                            tile->cornerHeight[2] == tile->height && tile->cornerHeight[3] == tile->height;
          if (!haveFirst)
            firstHeight = tile->height, haveFirst = true;
          ok = ok && flat && tile->height == firstHeight;
        }
      }
      if (edges) {
        const bool up = j > 1, right = i < gx - 2, down = j < gy - 2, left = i > 1;
        if ((up && fences.at(Fences::Edge{true, tx, ty})) || (right && fences.at(Fences::Edge{false, tx + 1, ty})) ||
            (down && fences.at(Fences::Edge{true, tx, ty + 1})) || (left && fences.at(Fences::Edge{false, tx, ty})))
          ok = false;
        if ((up && cliff(tile, 0)) || (right && cliff(tile, 2)) || (down && cliff(tile, 4)) || (left && cliff(tile, 6)))
          ok = false;
      }
    }
  }
  ZT_STEP("0x4f7fbd", "A big animal's whole footprint checked", {"species", t.key}, {"facing (+0x12c)", facing},
          {"along x (half tiles)", gx}, {"along y", gy}, {"from its crate", boxed ? 1 : 0},
          {"verdict", ok ? "fits" : "doesn't fit"});
  return ok;
}

// The facing +0x12c (0 to 7: 0 N, 2 E, 4 S, 6 W, as the original's and
// World::worldFacing's) nearest its fx, fy. ours (ours keeps a vector)
int Animals::facingOf(const Member &m) {
  return (static_cast<int>(std::lround(std::atan2(m.fx, -m.fy) / (3.14159265f / 4.0f))) % 8 + 8) % 8;
}

// zoo.exe ZTAnimal's class slot 0x94 (0x410803): an egg (+0x395) its type's
// cEggFootprint (+0x410), one in its crate (+0x394) its cBoxFootprint
// (+0x1d8), whatever the flag; else ZTUnit::footprint 0x41070b - with the
// flag cMapFootprint (type +0x150) both ways (0 high), without it the
// entity's own (0x40f916: cFootprintX/Y, type +0xb4). The original swaps x
// and y for an odd facing (+0x12c & 3); ours keeps them as the type gives
// them - every footprint read here is square or only its area is used
std::pair<int, int> Animals::footprint(const Member &m, bool map) const {
  const Type &t = this->typeList[m.type];
  if (m.egg)
    return {t.eggX, t.eggY};
  if (m.boxed)
    return {t.boxX, t.boxY};
  return map ? std::pair<int, int>{t.mapFoot, t.mapFoot} : std::pair<int, int>{t.footX, t.footY};
}

// zoo.exe 0x4137df (its counterpart 0x4402ed takes it off): the unit on the
// map's tiles (0x410b0f: the tile's list, +0x85 |= 1) - from its tile (its
// position / 64, toward 0), the footprint with the flag in whole tiles,
// (n + 1) / 2 each way, back half of that: cMapFootprint 2 its own tile, 4
// (apato.ai, a big crate) the 2 x 2 from one tile back
std::vector<std::pair<int, int>> Animals::mapTiles(const Member &m) const {
  const auto [fx, fy] = footprint(m, true);
  const int w = (fx + 1) / 2, h = (fy + 1) / 2;
  const int x0 = static_cast<int>(std::floor(m.x)) - w / 2, y0 = static_cast<int>(std::floor(m.y)) - h / 2;
  std::vector<std::pair<int, int>> out;
  for (int i = 0; i < w; i++)
    for (int j = 0; j < h; j++)
      out.push_back({x0 + i, y0 + j});
  return out;
}

// The height a move in a tank ends at, over its floor (zoo.exe 0x43dc11,
// ZTUnit::tankTargetHeight - class slot 0x1f8): whole height units, from
// cDepth (type +0x68) up to the water level (tank +0x188), by the move's
// target; a cSurface type (slot 0xc0) keeps to the surface
float Animals::tankTargetHeight(Member &m, const std::string &target) {
  const Type &t = this->typeList[m.type];
  int physicalTank=this->tankAt?this->tankAt(m.x,m.y):m.exhibit;
  const int top = this->tankLevel && physicalTank>=0 ? this->tankLevel(physicalTank) : 0;
  const int low = t.needDepth;
  // (where it is, in whole units over the floor - FUN_0041426f's z / 16 -
  // kept between the two)
  const int here = std::max(0, static_cast<int>(m.swimZ));
  const int cur = here > top ? top : std::max(here, low);
  auto roll = [&](int n) { return std::uniform_int_distribution<int>(0, std::max(1, n) - 1)(this->rng); };
  int z;
  if (target == "surface" || t.surfaceOnly)
    z = top;
  else if (target == "dive")
    z = low < cur ? low + roll(cur - low) : cur;
  else if (target == "rise")
    z = cur < top ? top - roll(top - cur) : cur;
  else if (target == "level")
    z = cur;
  else if (target == "floor")
    z = low;
  else
    z = low + 1 < top ? low + roll(top - low + 1) : cur;
  ZT_STEP("0x43dc11", "Its height in the tank", {"name", m.name}, {"target", target.empty() ? "anywhere" : target},
          {"lowest (cDepth)", low}, {"water level", top}, {"now", cur}, {"goes to", z});
  return static_cast<float>(z);
}

float Animals::placementDepth(int type,int tank) const {
  if(type<0 || type>=static_cast<int>(typeList.size()) || tank<0 || !tankDepth)return 0;
  const auto &t=typeList[type];if(!t.underwater)return 0;
  const float top=std::max(0.0f,tankDepth(tank));
  const float low=std::clamp(static_cast<float>(t.needDepth),0.0f,top);
  return t.surfaceOnly?top:std::clamp(top-2.0f,low,top);
}

int Animals::adopt(int type, bool female, float x, float y, const WorldMap &map,
                   const Fences &fences, bool restoring, int facing) {
  ZT_FN("", "Adopt an animal");
  Fences::Fit fit = canPlace(type, x, y, map, fences, facing >= 0 ? facing : Original::placedUnitFacing);
  ZT_STEP("", "Can it go there?", {"species", type >= 0 && type < static_cast<int>(this->typeList.size()) ? this->typeList[type].key : "?"},
          {"x", x}, {"y", y}, {"fit", fit == Fences::Fit::Ok ? "yes" : fit == Fences::Fit::InTheWay ? "another's there" : "no"},
          {"message", this->placeMessage});
  if (fit != Fences::Fit::Ok && !(restoring && type >= 0 && type < static_cast<int>(this->typeList.size())))
    return -1;
  const Type &t = this->typeList[type];
  Member m;
  m.id = this->nextId++;
  m.type = type;
  m.female = female;
  m.x = x;
  m.y = y;
  m.exhibit = fences.exhibitAt(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  if(t.underwater && !t.onlyUnderwater)m.exhibit=fences.habitatRoot(m.exhibit,map);
  m.waterMode=t.swims && (isWater(map,static_cast<int>(x),static_cast<int>(y)) ||
                         (this->tankAt && this->tankAt(x,y)>=0));
  if (this->tankAt && this->tankAt(x, y) >= 0)
    m.swimZ = m.swimTo = this->placementDepth(type,this->tankAt(x,y));
  m.name = (!t.displayName.empty() ? t.displayName : this->rm ? this->rm->getString(t.nameId) : t.key) + " " +
           std::to_string(++this->counts[type]);
  // (the default name set through setName 0x4e1d6b - defaultName 0x4e1b58
  // at 0x4e1d54 - so through the rename cheats 0x44940b: only the name
  // table can match a default name; not while a zoo is read in)
  if (!restoring)
    RenameCheats::applyNameTable(m.name, t.key, m.female ? "f" : "m", nullptr, 0);
  m.happiness = static_cast<float>(std::clamp(t.initialHappiness, -100, 100));
  m.energy = 0;
  m.health = static_cast<float>(t.maxHits);
  m.life = t.timeDeath - 1;
  m.ready = t.reproductionInterval; // (ready at once, as the original)
  m.matePick = kMatePickDelay - 1;   // (zoo.exe setup 0x453d63)
  // Each check's countdown starts somewhere in its interval
  auto start = [&](int interval) { return std::uniform_int_distribution<int>(0, interval - 1)(this->rng); };
  m.habitatCheck = start(kHabitatCheck);
  m.captivityCheck = start(kCaptivityCheck);
  m.socialCheck = start(kSocialCheck);
  m.otherCheck = start(kOtherCheck);
  m.hungerCheck = start(kHungerCheck);
  m.zapCheck = start(kZapCheck);
  m.murkyCheck = start(kMurkyWaterCheck);
  m.breathCheck = start(kBreathCheck); // (zoo.exe 0x454037: rand % cBreathCheck)
  m.healthCheck = start(30);
  m.energyCheck = start(30);
  m.reproductionCheck = start(20);
  m.notHappyCheck = start(10);
  this->markDirty(m.exhibit);
  m.lastAte = m.lastSlept = this->now;
  // Facing as it was put down (zoo.exe zooViewMouse 0x4e00d3: +0x12c from
  // Adopt's rotation and the view's, World::worldFacing); ours: none given
  // (the check fixtures, a zoo read in) any way
  if (facing >= 0) {
    const float a = (facing & 7) * 3.14159265f / 4.0f;
    m.fx = std::sin(a);
    m.fy = -std::cos(a);
  } else {
    float a = std::uniform_real_distribution<float>(0, 6.2831853f)(this->rng);
    m.fx = std::cos(a);
    m.fy = std::sin(a);
  }
  play(m, "stand", Member::Mode::Loop, 1.0f);
  // (put down in the water: in the water)
  m.waterMode = isWater(map, static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
  ZT_STEP("", "Its starting values", {"name", m.name}, {"exhibit", m.exhibit},
          {"happiness (cInitialHappiness)", m.happiness}, {"health (cMaxHits)", m.health}, {"energy", m.energy},
          {"life (cTimeDeath)", m.life});
  // A dinosaur comes as an egg (zoo.exe 0x4e2cf8: cLaysEggs)
  if (t.laysEggs && !restoring)
    becomeEgg(m);
  this->list.push_back(m);
  return m.id;
}

void Animals::remove(int id) {
  if (const Member *m = this->member(id))
    this->markDirty(m->exhibit);
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [&](const Member &m) { return m.id == id; }),
                   this->list.end());
  if (this->selected == id)
    this->selected = -1;
  if (this->hovered == id)
    this->hovered = -1;
  if (this->carried == id)
    this->carried = -1;
}

// zoo.exe ZTAnimal::sellValue 0x48d21d (class slot 0x2a4, called by
// sellObject 0x50a432): its cost (type slot 0xbc) x 0.8 x ((happiness
// (+0x2a8) + 100) x 0.005 + (health (+0x2b8) - cMinHits (type +0x238)) /
// cMaxHits (type +0x234)) x 0.5, halved again when sick (+0x3a7). Happiness
// and health are whole numbers there; the money stays a float, as there.
// (The formula alone: the Lab's formula check runs it against zoo.exe's)
double Animals::sellValueOf(int cost, int happiness, int health, int minHits, int maxHits, bool sick) {
  // (the FPU's order, in its wider precision; a float when sellObject keeps it.
  // cMaxHits 0 would divide by nothing there: ours is 1 at least)
  double v = cost * double(Original::sellFactor);
  double happy = (happiness + double(Original::sellHappinessOffset)) * Original::sellHappinessScale;
  double healthy = double(health - minHits) / std::max(1, maxHits);
  v = v * (happy + healthy) * Original::sellHalf;
  if (sick)
    v *= Original::sellHalf;
  return static_cast<float>(v);
}

double Animals::sellValue(int id) const {
  const Member *m = this->member(id);
  if (!m)
    return 0;
  const Type &t = this->typeList[m->type];
  double v = sellValueOf(t.cost, static_cast<int>(std::lround(m->happiness)), static_cast<int>(std::lround(m->health)),
                         t.minHits, t.maxHits, m->sick);
  ZT_STEP("0x48d21d", "Its sell value", {"cost", t.cost}, {"happiness", m->happiness}, {"health", m->health},
          {"cMinHits", t.minHits}, {"cMaxHits", t.maxHits}, {"sick", m->sick ? 1.0 : 0.0}, {"value", v});
  return v;
}

void Animals::moveTo(int id, float x, float y) {
  if (Member *m = this->member(id)) {
    m->x = x;
    m->y = y;
  }
}

void Animals::hold(int id, const WorldMap &map, const Fences &fences) {
  if (Member *m = this->member(id)) {
    const int tank=this->tankAt?this->tankAt(m->x,m->y):-1;
    m->swimZ=m->swimTo=this->placementDepth(m->type,tank);
    this->carriedFit = canDrop(id, m->x, m->y, map, fences);
  }
}

bool Animals::canDrop(int id, float x, float y, const WorldMap &map, const Fences &fences) const {
  const Member *m = this->member(id);
  return m && canPlace(m->type, x, y, map, fences) == Fences::Fit::Ok;
}

void Animals::drop(int id, float x, float y, const Fences &fences) {
  Member *m = this->member(id);
  if (!m)
    return;
  m->x = x;
  m->y = y;
  m->swimZ=m->swimTo=this->placementDepth(m->type,this->tankAt?this->tankAt(x,y):-1);
  this->markDirty(m->exhibit);
  m->exhibit = fences.exhibitAt(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  this->markDirty(m->exhibit);
  m->stack.clear();
  m->path.clear();
  m->foodItem = -1;
  play(*m, "stand", Member::Mode::Loop, 1.0f);
  this->carried = -1;
}

void Animals::addToIndex(WorldIndex &index) const {
  for (const Member &m : this->list)
    if (m.id != this->carried)
      index.addMover(WorldIndex::Kind::Animal, m.id, m.x, m.y);
}

int Animals::agoText(double since, bool slept) {
  // zoo.exe 0x467a14: in game hours (a real second is two): under 1440
  // "Recently", to 8640 "A short time ago", else "A long time ago"
  int base = slept ? 3119 : 3116;
  double hours = since * 2.0;
  return hours < 1440 ? base : hours <= 8640 ? base + 1 : base + 2;
}

// ----------------------------------------------------------------------------
// Behaviour
// ----------------------------------------------------------------------------
const std::vector<Animals::Step> *Animals::set(const Member &m, const std::string &name) const {
  const Type &t = this->typeList[m.type];
  for (int s : {sub(m), 0}) {
    auto it = t.sets[s].find(name);
    if (it != t.sets[s].end())
      return &it->second;
  }
  return nullptr;
}

bool Animals::mayPerform(const Member &m) const {
  if(m.showTank<0 || !m.showTrick || !tankAt || tankAt(m.x,m.y)!=m.showTank)return false;
  const auto *tank=fencesRef?fencesRef->exhibit(m.showTank):nullptr;
  return tank && tank->show;
}

void Animals::pushSet(Member &m, const std::string &name) {
  if(typeList[m.type].showSets.count(name) && !mayPerform(m))return;
  if (m.stack.size() < 8 && this->set(m, name)) {
    if(name=="beat")m.mealTaken=false;
    m.stack.push_back({name, 0});
  }
}

// zoo.exe 0x4301b7: false when the set it's in (+0x54) is that one; else
// setBehaviourSet 0x4300f6, which writes the next set (+0xdc) only over one
// that may be replaced (+0xe8 - ours: none asked for yet)
bool Animals::requestSet(Member &m, const std::string &name) {
  if (!m.stack.empty() && m.stack.front().first == name)
    return false;
  if (!m.nextSet.empty())
    return false;
  m.nextSet = name;
  return true;
}

// zoo.exe animalStopEating 0x4fd86e: eating (+0x3a9) cleared, its goal - the
// food (+0x38c) - cleared (slot 0x174), and the set asked for next (+0xdc)
// emptied and made replaceable (+0xe8 = 1): the bEat its meal kept asking for,
// or a bSleep or bSick asked for before the meal. "Stopped Eating". The set
// it's in plays on to its end
void Animals::stopEating(Member &m) {
  m.eating = false;
  m.foodItem = -1;
  m.nextSet.clear();
  ZT_STEP("0x4fd86e", "Stopped Eating", {"hunger", m.hunger});
}

// zoo.exe 0x4384ba past its countdown (cEnergyCheck), awake: where it may
// lie down (0x440a37), not loose unless darted (+0x3a3, +0x393) and not
// chasing (+0x3a4 - ours: a guest or an animal it's after), and at
// cEnergyThreshold (type +0x324): bSleep asked for next, and if that took,
// asleep (+0x3a8) and "Sleepy"; else cEnergyIncrement x +0x2d8 more (+0x2c4,
// which 0x4378df adds, capped at cMaxEnergy) - none while it waits to sleep
void Animals::tirednessCheck(Member &m) {
  const Type &t = this->typeList[m.type];
  const bool chasing = m.prey >= 0 || m.huntAnimal >= 0;
  if (mayRest(m) && !(m.escaped && !m.tranquilised) && !chasing && m.energy >= t.energyThreshold) {
    if (requestSet(m, "bsleep")) {
      m.sleeping = true;
      ZT_STEP("0x4384ba", "Tired: bSleep next", {"energy", m.energy}, {"cEnergyThreshold", t.energyThreshold});
    }
    return;
  }
  float was = m.energy;
  m.energy = std::min(m.energy + t.energyIncrement, static_cast<float>(t.maxEnergy));
  ZT_STEP("0x4384ba", "Tiredness (every 30)", {"energy", was, m.energy}, {"cEnergyIncrement", t.energyIncrement},
          {"cEnergyThreshold", t.energyThreshold});
}

void Animals::play(Member &m, const std::string &anim, Member::Mode mode, float seconds) {
  m.anim = anim;
  m.mode = mode;
  m.animTime = 0;
  m.playFor = mode == Member::Mode::Loop ? seconds
              : mode == Member::Mode::PingPong ? 2 * animSeconds(m, anim)
                                               : animSeconds(m, anim);
  m.path.clear();
  m.pathAt = 0;
  m.stepping = true;
}

void Animals::faceToward(Member &m, float x, float y) {
  float dx = x - m.x, dy = y - m.y, d = std::hypot(dx, dy);
  if (d > 0.001f) {
    m.fx = dx / d;
    m.fy = dy / d;
  }
}

// zoo.exe 0x5080ee's table (0x64e6ec): cKeeperFoodType in food.cfg's
// order, then Dinosaur Digs' food01.cfg and Marine Mania's food02.cfg
std::string Animals::keeperFoodOf(int type) const {
  static const char *chows[14] = {"herbchow", "carnchow", "fruichow", "bambchow", "graschow",
                                  "fishchow", "inschow",  "dinocarn", "dinogras", "krill",
                                  "fish",     "chum",     "seagrass", "urchin"};
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return chows[0];
  const Type &t = this->typeList[type];
  if (!t.customFood.empty())
    return t.customFood;
  return chows[std::clamp(t.keeperFood, 0, 13)];
}

// zoo.exe 0x49fe15 / 0x4a0025 / 0x4a01f9 and 0x4a85da: a food object counts
// for an animal only when its kind (+0x168) is the species' keeper food
// (+0x3c8)
bool Animals::ownFood(const Member &m, const ZooItems::Item &i) const {
  return i.kind == ZooItems::Kind::Food && i.food == keeperFoodOf(m.type);
}

// Its own food in its exhibit, the nearest (hungry with none: zoo.exe
// habitatKeeperFoodCheck 0x4a8624 counts only its keeper food, 0x4a85da)
int Animals::nearestFood(const Member &m) const {
  if (!this->items)
    return -1;
  int found = -1;
  float best = 1e9f;
  for (const ZooItems::Item &i : this->items->items()) {
    if (i.kind != ZooItems::Kind::Food || i.exhibit != m.exhibit || i.units <= 0 || !ownFood(m, i))
      continue;
    float d = std::hypot(i.x - m.x, i.y - m.y);
    if (d < best) {
      best = d;
      found = i.id;
    }
  }
  return found;
}

// The food it goes to (zoo.exe Task 0x72's whileGoing 0x43849c, as it sets
// off): three times in four the nearest (0x49fe15 - tile to tile, the first
// of equals), else three in four of the rest the one with the fewest units
// left (0x4a0025), else any one of them at random (0x4a01f9). Each takes only
// food of its own kind (the food's +0x168 against the species' +0x3c8), and
// the keeper puts down each species' own (keeperFoodDuty 0x49efa6)
int Animals::chooseFood(Member &m) {
  if (!this->items)
    return -1;
  std::vector<const ZooItems::Item *> foods;
  for (const ZooItems::Item &i : this->items->items())
    if (i.kind == ZooItems::Kind::Food && i.exhibit == m.exhibit && i.units > 0 && ownFood(m, i))
      foods.push_back(&i);
  if (foods.empty())
    return -1;
  std::uniform_int_distribution<int> four(0, 3);
  const int tx = static_cast<int>(std::floor(m.x)), ty = static_cast<int>(std::floor(m.y));
  const ZooItems::Item *pick = nullptr;
  if (four(this->rng) != 0) {
    long best = 0x7fffffff;
    for (const ZooItems::Item *f : foods) {
      const long dx = static_cast<int>(std::floor(f->x)) - tx, dy = static_cast<int>(std::floor(f->y)) - ty;
      if (dx * dx + dy * dy < best) {
        best = dx * dx + dy * dy;
        pick = f;
      }
    }
    ZT_STEP("0x49fe15", "Picks the nearest food", {"food", static_cast<double>(pick->id)});
  } else if (four(this->rng) != 0) {
    float least = 1e30f;
    for (const ZooItems::Item *f : foods)
      if (f->units < least) {
        least = f->units;
        pick = f;
      }
    ZT_STEP("0x4a0025", "Picks the food with the fewest units left", {"food", static_cast<double>(pick->id)});
  } else {
    pick = foods[std::uniform_int_distribution<size_t>(0, foods.size() - 1)(this->rng)];
    ZT_STEP("0x4a01f9", "Picks a food at random", {"food", static_cast<double>(pick->id)});
  }
  return pick->id;
}

// How happy it is picks what it does ([AmbientAnims]); hungry with food
// down, it goes to eat; worn out, it sleeps
void Animals::chooseBehaviour(Member &m) {
  const Type &t = this->typeList[m.type];
  // Loose and rampaging: bEscapedRampage, before anything else (zoo.exe
  // 0x437102: an escaped animal's set is bEscapedRampage while it rampages,
  // bEscaped otherwise - ahead of sleep and its moods)
  if (m.escaped && m.rampaging && this->set(m, "bescapedrampage")) {
    pushSet(m, "bescapedrampage");
    ZT_STEP("0x437102", "Chooses: loose and rampaging", {"behaviour", "bescapedrampage"});
    return;
  }
  // Loose: its escaped behaviour (zoo.exe: no eating or sleeping)
  if (m.escaped && this->set(m, "bescaped")) {
    pushSet(m, "bescaped");
    ZT_STEP("", "Chooses: loose, its escaped behaviour", {"behaviour", "bescaped"});
    return;
  }
  if (m.hunger >= t.hungerThreshold && nearestFood(m) >= 0 && this->set(m, "bfindkeeperfood")) {
    // (a find set that only goes to the food - a sea animal's - eats after
    // with its bEat)
    bool eats = false;
    for (const Step &st : *this->set(m, "bfindkeeperfood"))
      for (const std::string &a : st.args)
        eats = eats || a == "beat";
    if (!eats && this->set(m, "beat"))
      pushSet(m, "beat");
    m.foodItem = -1; // (chosen afresh as it sets off: chooseFood)
    pushSet(m, "bfindkeeperfood");
    ZT_STEP("", "Chooses: hungry with food down, goes to eat", {"behaviour", "bfindkeeperfood"}, {"hunger", m.hunger},
            {"cHungerThreshold", t.hungerThreshold});
    return;
  }
  // Its building goal (zoo.exe chooseTask 0x436f5c: after its food and
  // drink, 0x43735c -> guestGoToBuilding 0x43b2f6), unless already in a
  // building's task (0x437392)
  if (m.buildingGoal >= 0 && m.building < 0 && !m.escaped && this->mapRef && this->fencesRef &&
      goToBuilding(m, *this->mapRef, *this->fencesRef))
    return;
  // Out of air: up for it (zoo.exe chooseTask 0x436f5c, after its food and
  // drink: 0x43750c - under the water, water state 4, with its breath at
  // cBreathThreshold - Task 0x7c, Task7c_SurfaceForAir)
  if (underwaterNow(m) && m.breath >= t.breathThreshold && this->set(m, "bbreathe")) {
    m.surfacing = 1;
    pushSet(m, "bbreathe");
    ZT_STEP("0x43750c", "Chooses: out of air, up to breathe", {"behaviour", "bbreathe"}, {"breath", m.breath},
            {"cBreathThreshold", t.breathThreshold});
    return;
  }
  // (sick: no set of its own here - the health check 0x4389aa asks for
  // bSick next, nextStep starts it)
  // Rampaging: its bHabitatRampage, or bEscapedRampage when it's out
  // (zoo.exe 0x438514 sets it; which set, by the names it ends there) -
  // before a sleep (zoo.exe 0x437102: the rampage set is set first, and
  // only with no set yet does it look at its tiredness)
  if (m.rampaging) {
    const char *rs = m.escaped ? "bescapedrampage" : "bhabitatrampage";
    if (this->set(m, rs)) {
      pushSet(m, rs);
      ZT_STEP("0x438514", "Chooses: rampaging", {"behaviour", rs}, {"happiness", m.happiness});
      return;
    }
  }
  // Asleep with no bSleep running or asked for (a loaded zoo - ours: the
  // asked-for set isn't saved): its bSleep
  if (m.sleeping && !m.escaped && mayRest(m) && this->set(m, "bsleep")) {
    m.rampaging = false; // (a sleep ends a rampage: zoo.exe 0x4375d3, as bSleep - or bHatch - starts)
    m.rampageTicks = 0;
    pushSet(m, "bsleep");
    ZT_STEP("0x437102", "Chooses: asleep, its bSleep", {"behaviour", "bsleep"});
    return;
  }
  // Tired (cEnergyThreshold) where it may lie down (0x440a37: not in the
  // water or a tank, unless it lives only underwater): zoo.exe 0x437102
  // starts no set but runs the tiredness check 0x4384ba - its countdown one
  // an update - till that asks for bSleep, which starts next (nextStep)
  if (!m.sleeping && mayRest(m) && m.energy >= t.energyThreshold && this->set(m, "bsleep")) {
    if (--m.energyCheck < 0) {
      m.energyCheck = kEnergyCheck - 1;
      tirednessCheck(m);
    }
    if (!m.nextSet.empty())
      return; // (nextStep starts it)
    // (nothing yet: as it was, till its next update - ours: kLineRetry)
    play(m, m.anim, Member::Mode::Loop, kLineRetry);
    ZT_STEP("0x437102", "Tired: waits for the tiredness check", {"energy", m.energy},
            {"countdown", static_cast<double>(m.energyCheck)});
    return;
  }
  int h =static_cast<int>(std::lround(m.happiness));
  // (in a tank: its surface or underwater list, by where it swims)
  if (t.underwater && this->tankAt && this->tankAt(m.x, m.y) >= 0) {
    bool top = this->tankDepth && m.swimZ >= this->tankDepth(this->tankAt(m.x,m.y)) - 0.35f;
    for (const Ambient &a : top ? t.ambientSurface : t.ambientUnder)
      if (h >= a.lo && h <= a.hi && this->set(m, a.set)) {
        pushSet(m, a.set);
        ZT_STEP("", "Chooses: by its happiness, in its tank", {"behaviour", a.set}, {"happiness", h}, {"from", a.lo},
                {"to", a.hi});
        return;
      }
  }
  // (a swimmer in the water: its [AmbientAnimsWater], by where it stands -
  // zoo.exe 0x43ca83)
  if (t.swims && onWater(m))
    for (const Ambient &a : t.ambientWater)
      if (h >= a.lo && h <= a.hi && this->set(m, a.set)) {
        pushSet(m, a.set);
        ZT_STEP("0x43ca83", "Chooses: by its happiness, in the water", {"behaviour", a.set}, {"happiness", h},
                {"from", a.lo}, {"to", a.hi});
        return;
      }
  // (zoo.exe 0x43cb33, only with no set running or asked for: the first
  // [AmbientAnims] entry whose range holds its happiness)
  for (const Ambient &a : t.ambient)
    if (h >= a.lo && h <= a.hi) {
      pushSet(m, a.set);
      ZT_STEP("0x43cb33", "Chooses: by its happiness ([AmbientAnims])", {"behaviour", a.set}, {"happiness", h},
              {"from", a.lo}, {"to", a.hi});
      return;
    }
  // ours: none fits - its bIdle1 (0x43cb33 starts nothing, and with an
  // empty list asks its type's slot 0x14)
  pushSet(m, "bidle1");
  ZT_STEP("", "Chooses: nothing fits, idles", {"behaviour", "bidle1"}, {"happiness", h});
}

Animals::Command Animals::behaviorCommand(const std::string &name) {
  static const std::map<std::string, Command> commands = {
      {"fendtrick", Command::EndTrick},
      {"fplay", Command::Play},
      {"fplaywithsound", Command::PlayWithSound},
      {"fplaytime", Command::PlayTime},
      {"fplaypingpong", Command::PlayPingPong},
      {"fplayreverse", Command::PlayReverse},
      {"fhalt", Command::Halt},
      {"fsetbase", Command::SetBase},
      {"fkeepcontrol", Command::KeepControl},
      {"fdie", Command::Die},
      {"fplayset", Command::PlaySet},
      {"fplaysettank", Command::PlaySetTank},
      {"fplaysetprob", Command::PlaySetProb},
      {"fplaysetterrain", Command::PlaySetTerrain},
      {"fwalk", Command::Walk},
      {"fmove", Command::Move},
      {"frun", Command::Run},
      {"ftrot", Command::Trot},
      {"fmovewithsound", Command::MoveWithSound},
      {"fmovereverse", Command::MoveReverse},
      {"ffacetowardfood", Command::FaceTowardFood},
      {"fhatch", Command::Hatch},
      {"fdustball", Command::Dustball},
      {"fenterbuilding", Command::EnterBuilding},
      {"fexitbuilding", Command::ExitBuilding},
      {"fcaughtprey", Command::CaughtPrey},
      {"ffacetowardtarget", Command::FaceTowardTarget},
      {"fplaystopatend", Command::PlayStopAtEnd},
      {"ftargetbuilding", Command::TargetBuilding},
      {"fplayspecial", Command::PlaySpecial},
      {"fplaysp", Command::PlaySpecial}
  };
  auto it = commands.find(name);
  return it == commands.end() ? Command::Unknown : it->second;
}

void Animals::nextStep(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  auto objectById = [&](int id) -> const PlacedObjects::Object * {
    if (objects) for (const auto &o : objects->objects()) if (o.id == id) return &o;
    return nullptr;
  };
  // Discard stale performance queues, for example after a show is cancelled
  // or an older save resumes a trick outside its assigned show tank.
  if(!mayPerform(m) && std::any_of(m.stack.begin(),m.stack.end(),[&](const auto &frame){return t.showSets.count(frame.first)!=0;})) {
    m.stack.clear();m.path.clear();m.pathAt=0;m.stepping=false;
  }
  // (dying: its bDie, then nothing more)
  if (m.stack.empty() && m.dying) {
    m.dieAfter = 0;
    play(m, "stand", Member::Mode::Loop, 1.0f);
    return;
  }
  // (an egg sits there, unless it's hatching; an eaten one stays broken)
  if (m.egg && m.stack.empty()) {
    if (m.eaten) {
      play(m, "egg_break", Member::Mode::Once); // (held on its last frame)
      m.animTime = m.playFor;
      m.playFor += 3600.0f;
    } else
      play(m, "egg_idle", Member::Mode::Loop, 3600.0f);
    return;
  }
  if (m.stack.empty() && m.showTank >= 0) {
    if (m.showTrick) { m.showDone = true; m.showTrick = false; }
    play(m, "surfidle", Member::Mode::Loop, 1.0f);
    return;
  }
  // (an fPlay's next animation, while its step is still the one running)
  if (!m.playQueue.empty()) {
    if (!m.stack.empty() && m.playQueueAt == std::make_pair(m.stack.size(), m.stack.back().second)) {
      std::string next = m.playQueue.front();
      m.playQueue.erase(m.playQueue.begin());
      play(m, next, Member::Mode::Once);
      return;
    }
    m.playQueue.clear();
  }
  if (m.stack.empty()) {
    m.drinkX = m.drinkY = -1;
    // Its set over: the one asked for next first, else it chooses (zoo.exe
    // unit update 0x435cfb: no set running, +0xdc empty -> startBehaviour,
    // class slot 0x48; else 0x43c6c5 starts +0xdc and setBehaviourSet puts
    // "" back, replaceable)
    if (m.nextSet.empty())
      chooseBehaviour(m);
    if (m.stack.empty() && !m.nextSet.empty()) {
      const std::string next = m.nextSet;
      m.nextSet.clear();
      // (bSleep, bSick: no rampage while it lies there - zoo.exe 0x4375e2)
      if (next == "bsleep" || next == "bsick") {
        m.rampaging = false;
        m.rampageTicks = 0;
      }
      pushSet(m, next);
      ZT_STEP("0x435cfb", "Starts the set asked for next", {"behaviour", next});
    }
  }
  for (int guard = 0; guard < 32 && !m.stack.empty(); guard++) {
    const std::vector<Step> *steps = this->set(m, m.stack.back().first);
    if (!steps || m.stack.back().second >= steps->size()) {
      // Primates' bEat uses mouth/arm animations, not an animation named
      // "eat". Completing the feeding behaviour must still consume food.
      if(m.stack.back().first=="beat" && !m.mealTaken)consumeFood(m);
      // (bSleep over: "Refreshed after sleeping" - its energy back to its
      // least, type +0x24c, 0 - and when it last slept, now: zoo.exe
      // 0x4384ba)
      if(m.stack.back().first=="bsleep" && m.sleeping) {
        m.sleeping=false;
        m.energy=0;
        m.lastSlept=this->now;
        ZT_STEP("0x4384ba", "Refreshed after sleeping", {"energy", 0.0});
      }
      m.stack.pop_back();
      continue;
    }
    // Up for air (Task 0x7c): first up to the surface, somewhere in its
    // tank (its whileGoing 0x498cf5 till it's there), then its bBreathe -
    // and, that started, its breath back to its least (atTarget 0x4e8ac4:
    // type +0x208)
    if (m.surfacing && (m.stack.back().first != "bbreathe" || m.stack.back().second != 0))
      m.surfacing = 0;
    if (m.surfacing == 1) {
      m.surfacing = 2;
      m.swimTo = this->tankTargetHeight(m, "surface");
      m.moveAnim = "walk";
      m.speed = this->typeList[m.type].forAge(m.baby).slow;
      if (walkSomewhere(m, map, fences)) {
        ZT_STEP("0x498cf5", "Going to surface for air", {"breath", m.breath});
        return;
      }
    }
    if (m.surfacing == 2) {
      m.surfacing = 0;
      m.breath = kBreathMin;
      ZT_STEP("0x4e8ac4", "At the surface: bBreathe", {"breath", m.breath});
    }
    Step s = (*steps)[m.stack.back().second++];
    const Command command = behaviorCommand(s.fn);
    auto arg = [&](size_t i) { return i < s.args.size() ? s.args[i] : std::string(); };
    // A line its handler can't run (zoo.exe: the handler returns false;
    // 0x435f0a takes that as the set failing, 0x435e57 ends the set and each
    // set it was played from in turn): every set it's in is over, its
    // animation going on as it was till it picks again. measured
    // 2026-11-15 in Play original (Lab, everything-ff02-units.zoo, the
    // runner's line at unit +0x48 / runner +0x34): Mexican wolves in bCalm1
    // left it as it reached fPlay(lie_paw) - an animation they haven't - and
    // started a new mood set, never playing the lines after it
    auto failLine = [&](const char *at) {
      ZT_STEP(at, "A line that can't run: its sets end", {"line", s.fn}, {"arguments", static_cast<double>(s.args.size())});
      m.stack.clear();
      m.playQueue.clear();
      play(m, m.anim, Member::Mode::Loop, kLineRetry);
    };
    const bool moving=command==Command::Move || command==Command::MoveWithSound || command==Command::MoveReverse;
    const bool playing=command==Command::Play || command==Command::PlayWithSound || command==Command::PlayTime ||
      command==Command::PlayReverse || command==Command::PlayPingPong || command==Command::PlayStopAtEnd ||
      command==Command::PlaySpecial || command==Command::SetBase;
    if(!mayPerform(m) && ((moving && t.showAnimations.count(arg(2))) ||
        (playing && t.showAnimations.count(arg(0)))))continue;
    if (command == Command::EndTrick) {
      // zoo.exe fEndTrick 0x5a6a22 -> 0x5a69e6: the unit's class slot 0x2bc
      // (its trick over), the line done - every stock show set's last line
      if(!mayPerform(m))continue;
      m.showDone = true; m.showTrick = false; m.stack.clear();
      play(m, "surfidle", Member::Mode::Loop, 1.0f);
      return;
    } else if (command == Command::Play) {
      // zoo.exe 0x440b92: every animation the line names that the animal
      // has, one after another ("fPlay(stamp,stamp)" stamps twice); names it
      // hasn't are passed over - and with none it has, the set fails
      std::vector<std::string> anims;
      for (const std::string &a : s.args)
        if (art(m, a, false))
          anims.push_back(a);
      if (anims.empty()) {
        failLine("0x440b92");
        return;
      }
      play(m, anims.front(), Member::Mode::Once);
      m.playQueue.assign(anims.begin() + 1, anims.end());
      m.playQueueAt = {m.stack.size(), m.stack.back().second};
    } else if (command == Command::PlayWithSound || command == Command::PlayStopAtEnd) {
      // (fPlayStopAtEnd 0x5a55a9 plays through fPlay 0x440b92: an animation
      // it hasn't, the set fails. So does fPlayWithSound: 0x438399 ->
      // 0x4383bb looks the animation up by name, 0x410ffe, and with none
      // returns false - its sound, looked up after, isn't heard)
      if (!art(m, arg(0), false)) {
        failLine(command == Command::PlayStopAtEnd ? "0x5a55a9" : "0x4383bb");
        return;
      }
      play(m, arg(0), Member::Mode::Once);
      if (command == Command::PlayWithSound)
        playSound(m, arg(1));
    } else if (command == Command::PlaySpecial) {
      // fPlaySpecial / fPlaySp (0x58a333): animation, sound, duration,
      // repeat count, normal/reverse/pingpong/wait. Stock crocodile and
      // Deinosuchus sets use it to play an animation with a splash sound.
      const std::string mode = arg(4);
      const float duration = std::max(0.0f, static_cast<float>(std::atof(arg(2).c_str())));
      const int repeats = std::max(0, std::atoi(arg(3).c_str()));
      const auto playback = mode == "reverse" ? Member::Mode::Reverse
                          : mode == "pingpong" ? Member::Mode::PingPong
                          : duration > 0 || repeats > 1 ? Member::Mode::Loop : Member::Mode::Once;
      play(m, arg(0), playback, duration);
      if (duration <= 0 && repeats > 1) m.playFor = repeats * animSeconds(m, arg(0));
      if (!arg(1).empty() && arg(1) != "0") playSound(m, arg(1));
    } else if (command == Command::PlayTime) {
      // zoo.exe 0x424e7f: exactly two arguments, the time a whole number of
      // seconds (its number x 1000 ms); 0x424ee7 plays the animation whole
      // times, once and again while what's left of the time is 1 ms or more
      // - so the time rounded up to whole plays. Another count, or an
      // animation it hasn't, the set fails. measured 2026-11-15 (Play
      // original, a wild dog's bAngry1 fPlayTime(stand,15)): about 14-15 s
      if (s.args.size() != 2 || !art(m, arg(0), false)) {
        failLine("0x424e7f");
        return;
      }
      const int wantMs = std::atoi(arg(1).c_str()) * 1000;
      const int loopMs = static_cast<int>(std::lround(animSeconds(m, arg(0)) * 1000.0f));
      int plays = 1;
      for (int left = wantMs - loopMs; left >= 1 && loopMs > 0; left -= loopMs)
        plays++;
      play(m, arg(0), Member::Mode::Loop, plays * loopMs / 1000.0f);
    } else if (command == Command::PlayPingPong || command == Command::PlayReverse) {
      // (both through fPlay 0x440b92 - fPlayPingPong 0x42631a, fPlayReverse
      // 0x440c82: an animation it hasn't, the set fails)
      if (!art(m, arg(0), false)) {
        failLine(command == Command::PlayPingPong ? "0x42631a" : "0x440c82");
        return;
      }
      play(m, arg(0), command == Command::PlayPingPong ? Member::Mode::PingPong : Member::Mode::Reverse);
    } else if (command == Command::Halt) {
      // Stops where it is (zoo.exe 0x601da3: stopMoving)
      m.path.clear();
      m.pathAt = 0;
      m.swimTo = m.swimZ;
      continue;
    } else if (command == Command::SetBase) {
      // Its animation from now on, held (0x59639b: the named animation set
      // on frame 0 as its base - a sick fish lying in the water)
      play(m, arg(0), Member::Mode::Loop, 1.0f);
      continue;
    } else if (command == Command::KeepControl) {
      // Keeps this set going on its base (behaviourRunFnMoves 0x4a751e:
      // true while it has a unit, its line never done) till a set is
      // started over it - healed, a cStaySick animal's bDoneSick
      // (Job4_HealAnimal::atTarget 0x5eccd0 -> 0x43c6c5); dying, its bDie
      if (!m.dying) {
        m.stack.back().second--;
        play(m, m.anim, Member::Mode::Loop, 1.0f);
        ZT_STEP("0x4a751e", "Keeps control: lies sick on its base", {"anim", m.anim});
        return;
      }
      continue;
    } else if (command == Command::Die) {
      // (its death is the set ending: dieAfter, as bDie's end on land - fDie
      // 0x60cbb3 is each stock death set's last line)
      continue;
    } else if (command == Command::PlaySet) {
      // zoo.exe 0x426dd4: one argument, a set it has (0x43ddc1 finds none
      // to play otherwise: the set fails)
      if (s.args.size() != 1 || !this->set(m, arg(0))) {
        failLine("0x426dd4");
        return;
      }
      pushSet(m, arg(0));
      continue;
    } else if (command == Command::PlaySetTank) {
      // zoo.exe fPlaySetTank 0x5b2be5: exactly two arguments, else the
      // line fails; the second while it's under the water (its water state,
      // +0x258, is 4 - ours: underwaterNow), the first anywhere else, out
      // of a tank too; a set it hasn't plays nothing and the line fails
      // (0x43ddc1)
      const std::string pick = s.args.size() == 2 ? (underwaterNow(m) ? arg(1) : arg(0)) : "";
      if (pick.empty() || !this->set(m, pick)) {
        failLine("0x5b2be5");
        return;
      }
      pushSet(m, pick);
      continue;
    } else if (command == Command::PlaySetProb) {
      // Weighted: set, chance, set, chance ... (zoo.exe 0x43df67, its roll
      // 0x43e0d3): an even number of arguments, at least two, whose chances
      // add up to exactly 100 - otherwise nothing is picked and the set
      // fails. A roll of 1..100 against the first chance; past it, the
      // chance left is rolled again (1..what's left) against the next, and
      // so on. A set picked that the animal doesn't have plays nothing
      // (0x43ddc1) and the set fails too - its mood is picked again
      std::vector<std::pair<std::string, int>> picks;
      int total = 0;
      if (s.args.size() >= 2 && s.args.size() % 2 == 0)
        for (size_t i = 0; i + 1 < s.args.size(); i += 2) {
          picks.push_back({s.args[i], std::atoi(s.args[i + 1].c_str())});
          total += picks.back().second;
        }
      if (picks.empty() || total != 100) {
        failLine("0x43df67");
        return;
      }
      int left = 100;
      int roll = std::uniform_int_distribution<int>(1, 100)(this->rng);
      std::string picked;
      for (size_t i = 0; i < picks.size(); i++) {
        if (roll <= picks[i].second) {
          picked = picks[i].first;
          break;
        }
        left -= picks[i].second;
        if (left > 0 && left < 101)
          roll = std::uniform_int_distribution<int>(1, left)(this->rng);
      }
      if (picked.empty() || !this->set(m, picked)) {
        ZT_STEP("0x43e0d3", "fPlaySetProb picked a set it hasn't: the set fails", {"set", picked});
        failLine("0x43e0d3");
        return;
      }
      pushSet(m, picked);
      continue;
    } else if (command == Command::PlaySetTerrain) {
      // zoo.exe fPlaySetTerrain: the behaviour runner's own class slot 0x14
      // (behaviourRunFnMoves 0x431632; the runner is the one manager
      // 0x525961 makes, class table 0x635324, so the slot is 0x4314d6).
      // Four arguments: by its water state (+0x258) under the water (4) the
      // fourth, at a tank's surface (3) the third, else on a water tile
      // (tile +0x83 & 3) the second, on land the first. Any other count
      // goes to the base's slot 0x14 (table 0x63536c: 0x601d2d): exactly
      // two, the second on a water tile and the first anywhere else - in a
      // tank too - and any other count fails the line. A set it hasn't
      // plays nothing and the line fails (0x43ddc1). (ours: water state 3/4
      // is in a tank above/below underwaterNow's line)
      const bool inTank = this->tankAt && this->tankAt(m.x, m.y) >= 0;
      const bool under = underwaterNow(m);
      const size_t n = s.args.size();
      std::string pick, where;
      if (n == 4) {
        where = inTank ? (under ? "tank, under" : "tank surface") : onWater(m) ? "water" : "land";
        pick = inTank ? (under ? arg(3) : arg(2)) : onWater(m) ? arg(1) : arg(0);
      } else if (n == 2) {
        where = onWater(m) ? "water" : "not water";
        pick = onWater(m) ? arg(1) : arg(0);
      }
      ZT_STEP(n == 4 ? "0x4314d6" : "0x601d2d", "A set by where it is (fPlaySetTerrain)", {"where", where}, {"set", pick});
      if (pick.empty() || !this->set(m, pick)) {
        failLine(n == 4 ? "0x4314d6" : "0x601d2d");
        return;
      }
      pushSet(m, pick);
      continue;
    } else if (command == Command::Walk || command == Command::Move || command == Command::Run || command == Command::Trot || command == Command::MoveWithSound ||
               command == Command::MoveReverse) {
      std::string target = arg(0);
      // Its arguments (zoo.exe): fWalk/fSwim 0x436606, fTrot 0x4a31de and
      // fRun 0x4a5310 exactly two; fMove 0x425533 (and fMoveReverse
      // 0x4a75a7 through it) four or five; fMoveWithSound 0x4248fa five or
      // six - and a first one that isn't empty. Otherwise the set fails
      const size_t n = s.args.size();
      const bool fits = command == Command::Move || command == Command::MoveReverse ? n == 4 || n == 5
                        : command == Command::MoveWithSound                          ? n == 5 || n == 6
                                                                                     : n == 2;
      if (!fits || target.empty()) {
        failLine(command == Command::Walk ? "0x436606" : command == Command::Trot ? "0x4a31de"
                 : command == Command::Run ? "0x4a5310" : command == Command::MoveWithSound ? "0x4248fa"
                                                                                            : "0x425533");
        return;
      }
      // fMove(where, n, animation, speed); fMoveWithSound the same and a
      // sound (a dolphin's jumps, a shark's frenzy); fMoveReverse (not yet
      // played backwards); fTrot at its medium rate, trotting. (fMove's
      // speed is a rate as cSlowRate is: zoo.exe 0x425533 passes it to
      // 0x43646e, which sets it as the unit's speed, class slot 0x1c0)
      bool asMove = command == Command::Move || command == Command::MoveWithSound || command == Command::MoveReverse;
      bool hasTrot = t.anims[sub(m)].count("trot") || t.anims[0].count("trot");
      m.moveAnim = command == Command::Run ? "run"
                   : command == Command::Trot ? (hasTrot ? "trot" : "run")
                   : asMove && !arg(2).empty() ? arg(2)
                                               : "walk";
      m.speed = command == Command::Run ? t.forAge(m.baby).fast
                : command == Command::Trot ? t.forAge(m.baby).medium
                : asMove && !arg(3).empty() ? Original::unitTilesPerSecond(std::atoi(arg(3).c_str()))
                                            : t.forAge(m.baby).slow;
      // (its animation's rate: fMove's 5th argument, 100 without -
      // 0x425533; ours: fMoveWithSound's 100 too; fWalk/fSwim, fTrot and
      // fRun the type's own)
      if (command == Command::Move || command == Command::MoveReverse || command == Command::MoveWithSound)
        setAnimRate(m, -1, command != Command::MoveWithSound && !arg(4).empty() ? std::atoi(arg(4).c_str()) : 100);
      else
        setAnimRate(m, command == Command::Run ? 2 : command == Command::Trot ? 1 : 0);
      // fMove's 2nd argument, a time (zoo.exe 0x425533 reads it x 1000, in
      // ms; 0x43646e): with none, the move's animation is the whole move's
      // (its queue one entry, looped). With one, two queues: the move's
      // animation (class slot 0x130 by its name) once, then again while
      // the time less one play (0x42514c: its length) is still over 0 -
      // so ceil(time / length) plays, at least one (0x49c3a6..0x49c4b5,
      // 0x5aa019) - and after them (0x42296d puts the second queue after
      // the first) its plain move animation, slot 0x134 (0x4a9871 through
      // slot 0x12c, 0x4a9bb9), for the rest of the way; the move's rate
      // (slot 0x1c4) stays. Every stock one is a sea animal's fMove(level,
      // N, ...) - a shark's bEat, an orca's bSubroll. The plain one is by
      // the line's speed (0x4a9bb9 asks the AI manager, table 0x635324:
      // slot 0x28 0x4a9b17 - fMoveSlow, fWalk or fMove(.., "walk") - slot
      // 0x120's walk; slot 0x2c 0x4a9f38 - fMoveMedium, fTrot or fMove(..,
      // "trot"), moveFnBySpeed 0x4a9f7d its tail - slot 0x124 0x4a30da's
      // trot; slot 0x30 0x4aa05a -
      // fMoveFast, fRun or fMove(.., "run") - slot 0x128's run; none of
      // them, the walk). A play's length (0x42514c(0)): the frames of the
      // animation's first entry, less its background frame (+0x18), x its
      // frame time in ms (+0) - ours animSeconds; plays counted in whole ms
      // as the original's. (A trot without trot art, run
      m.moveAnimLeft = -1;
      m.moveAnimAfter = command == Command::Move && arg(2) == "run" ? "run"
                        : command == Command::Move && arg(2) == "trot" ? (hasTrot ? "trot" : "run")
                                                                       : "walk";
      if (asMove && std::atoi(arg(1).c_str()) > 0) {
        const float len = animSeconds(m, m.moveAnim);
        const int lenMs = static_cast<int>(std::lround(len * 1000.0f));
        const int timeMs = std::atoi(arg(1).c_str()) * 1000;
        const int plays = Original::timedMovePlays(timeMs, lenMs);
        const float time = timeMs / 1000.0f;
        m.moveAnimLeft = plays * len;
        ZT_STEP("0x43646e", "fMove with a time: its animation for whole plays, then its plain move", {"anim", m.moveAnim},
                {"seconds", static_cast<double>(time)}, {"plays", plays});
      }
      if (command == Command::MoveWithSound && !arg(4).empty())
        playSound(m, arg(4));
      bool walking = false;
      if (target == "otherfood" && m.drinkX >= 0) {
        // (a drink: to the water - Task 0x7a's target)
        walking = walkTo(m, m.drinkGoX, m.drinkGoY, map, fences);
      } else if (target == "keeperfood" || target == "otherfood") {
        // (the food chosen as it sets off; on its way, kept while it lasts)
        const ZooItems::Item *kept = m.foodItem >= 0 && this->items ? this->items->item(m.foodItem) : nullptr;
        if (!kept || kept->units <= 0 || kept->exhibit != m.exhibit)
          m.foodItem = chooseFood(m);
        const ZooItems::Item *food = m.foodItem >= 0 && this->items ? this->items->item(m.foodItem) : nullptr;
        if (!food) {
          // Nothing to eat: the rest of the set is for eating it
          m.stack.pop_back();
          continue;
        }
        // (in a tank: a height as any move's there - 0x43dc11)
        if (this->tankAt && this->tankAt(m.x, m.y) >= 0)
          m.swimTo = this->tankTargetHeight(m, target);
        walking = feedingPlace(m, food->x, food->y, map, fences);
        if (!walking) {
          // Retry this approach; never advance to eating remotely.
          --m.stack.back().second;
          play(m, m.anim, Member::Mode::Loop, 0.75f);
          return;
        }
      } else if (target == "0" || target.empty()) {
        if (this->tankAt && this->tankAt(m.x, m.y) >= 0)
          m.swimTo = this->tankTargetHeight(m, target);
        walking = walkSomewhere(m, map, fences);
      } else if ((target == "level" || target == "floor") && arg(1) == "1" && m.foodItem >= 0 && this->items &&
                 this->items->item(m.foodItem) && !t.keepMoving) {
        // (at the food: its eating move there, down by it. Not a cKeepMoving
        // kind - every stock fMove(level, 1, submerged_eat): its food task,
        // 0x72, is replaced by Task 1 at the food before its bEat
        // (Task72_FindKeeperFood::atTarget 0x4fd66a, slot 0x1d8(1, 0)), so
        // its "level" is somewhere else in the tank at its height, below.
        // ours: kept for a mod's non-cKeepMoving kind)
        const ZooItems::Item *food = this->items->item(m.foodItem);
        m.swimTo = this->tankTargetHeight(m, target);
        walking = feedingPlace(m, food->x, food->y, map, fences);
        if (!walking) {
          --m.stack.back().second;
          play(m, m.anim, Member::Mode::Loop, 0.75f);
          return;
        }
      } else if (target == "surface" || target == "floor" || target == "dive" || target == "rise" ||
                 target == "level") {
        // In a tank: somewhere else in it, at the surface, down at the floor,
        // deeper, higher or as deep as it is (0x43dc11)
        m.swimTo = this->tankTargetHeight(m, target);
        walking = walkSomewhere(m, map, fences);
      }
      if (!walking)
        continue;
    } else if (command == Command::FaceTowardFood) {
      // zoo.exe fFaceTowardFood 0x426665 -> 0x42662c: an animal faces its
      // goal (+0x38c - its food; ours: the water it drinks at, for Task
      // 0x7a) through 0x42661b -> 0x426d1a; none, nothing. The line done
      if (const ZooItems::Item *food = m.foodItem >= 0 && this->items ? this->items->item(m.foodItem) : nullptr)
        faceToward(m, food->x, food->y);
      else if (m.drinkX >= 0)
        faceToward(m, m.drinkX, m.drinkY);
      continue;
    } else if (command == Command::FaceTowardTarget) {
      // zoo.exe fFaceTowardTarget 0x426ed9 -> 0x426e50: an animal faces
      // its target unit (0x42fa2c: unitTarget 0x4a4b53, +0x178 -> faceToward
      // 0x426d4d) - ours: the animal or guest it hunts or caught, else the
      // building it's after; staff and guests their +0x234 (0x426f21)
      if (const Member *target = member(m.huntAnimal >= 0 ? m.huntAnimal : m.victim))
        faceToward(m, target->x, target->y);
      else if (m.prey >= 0 && preyAt) {
        float x, y; if (preyAt(m.prey, x, y)) faceToward(m, x, y);
      } else if (objects) {
        if (const auto *target = objectById(m.rampageBuilding >= 0 ? m.rampageBuilding : m.building))
          faceToward(m, target->x, target->y);
      }
      continue;
    } else if (command == Command::CaughtPrey) {
      // zoo.exe fCaughtPrey 0x5a593f -> 0x588c2d: its prey (its chase's,
      // 0x4a4b53, else slot 0x25c) eaten - the message (0x50e648), the
      // prey's +0x22c its hunter, the prey's slot 0x254; none, its prey
      // forgotten (slot 0x258); the line done either way.
      // 0x588c2d removes the victim once. Our dust-ball completion already
      // queues that removal before starting bCaughtPrey; never kill twice.
      if (m.victim >= 0) {
        if (std::find(kills.begin(), kills.end(), m.victim) == kills.end()) kills.push_back(m.victim);
        m.victim = -1; m.killIn = 0;
      }
      continue;
    } else if (command == Command::TargetBuilding) {
      // zoo.exe fTargetBuilding 0x5a5b71: rampaging (+0x3b0) it goes for
      // its building (animalGoToTargetBuilding); not, the line is done and
      // its target (+0x3b1) cleared
      if (!m.rampaging || !objects) { m.rampageBuilding = -1; continue; }
      // Its target (zoo.exe animalGoToTargetBuilding 0x5a5b0a ->
      // animalFindTargetBuilding 0x5a5a90 -> findNearestOfKindB 0x4290f3):
      // of the 'building' kind, in the same habitat as itself (class slot
      // 0x248, ZTUnit::habitatAt 0x41060e - ours Fences::exhibitAt: the
      // building's tile against its own - a loose one
      // the zoo's grounds, a rampage in its exhibit its own buildings), the
      // nearest by whole tiles, ties at random; then a way to it (0x422238)
      // or no target at all - not the next nearest. Only one with a slot
      // with room for it (0x429330 -> 0x42ac87: a building with slots whose
      // id list holds its kind, under its capacities). (ours: rubbleable
      // ones only.) Then it approaches its footprint and rubbles it as
      // stepping onto its tile
      std::vector<std::pair<float, int>> targets;
      const int here = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
      int nearest = std::numeric_limits<int>::max();
      for (const auto &o : objects->objects()) {
        if (o.className != "building" || !infoOf(PlacedObjects::fileOf(o)).rubbleable ||
            !buildingHasRoom(o.id, infoOf(PlacedObjects::fileOf(o)), m))
          continue;
        const int ox = static_cast<int>(std::floor(o.x)), oy = static_cast<int>(std::floor(o.y));
        if (fences.exhibitAt(ox, oy) != here)
          continue;
        const int dx = ox - static_cast<int>(std::floor(m.x)), dy = oy - static_cast<int>(std::floor(m.y));
        const int d2 = dx * dx + dy * dy;
        if (d2 < nearest) {
          nearest = d2;
          targets.clear();
        }
        if (d2 == nearest)
          targets.push_back({0.0f, o.id});
      }
      if (targets.size() > 1) {
        const auto keep = targets[std::uniform_int_distribution<size_t>(0, targets.size() - 1)(this->rng)];
        targets = {keep};
      }
      bool routed = false;
      for (auto [distance, id] : targets) {
        const auto *o = objectById(id); if (!o) continue;
        auto info = infoOf(PlacedObjects::fileOf(*o));
        if ((o->facing & 6) == 2 || (o->facing & 6) == 6) std::swap(info.fx, info.fy);
        const float hx = std::max(1, info.fx) / 4.0f, hy = std::max(1, info.fy) / 4.0f;
        const float ox = o->x, oy = o->y;
        std::vector<std::pair<float, float>> spots = {
          {ox-hx-0.51f, std::clamp(m.y, oy-hy, oy+hy)},
          {ox+hx+0.51f, std::clamp(m.y, oy-hy, oy+hy)},
          {std::clamp(m.x, ox-hx, ox+hx), oy-hy-0.51f},
          {std::clamp(m.x, ox-hx, ox+hx), oy+hy+0.51f}};
        std::sort(spots.begin(), spots.end(), [&](auto a, auto b) {
          return std::hypot(a.first-m.x,a.second-m.y)<std::hypot(b.first-m.x,b.second-m.y);
        });
        for (auto [x, y] : spots) {
          m.rampageBuilding = id;
          if (std::hypot(x-m.x, y-m.y) < 0.2f) {
            faceToward(m, ox, oy);
            if (rubbleAt) rubbleAt(static_cast<int>(std::floor(std::clamp(m.x, ox-hx+0.01f, ox+hx-0.01f))),
                                   static_cast<int>(std::floor(std::clamp(m.y, oy-hy+0.01f, oy+hy-0.01f))), m);
            m.rampageBuilding = -1; // (there: Task 0x7b's atTarget 0x61ad83 clears +0x3b1, the task over)
            routed = true;
            break;
          }
          moveOf(m, m.escaped ? "bescapedrampage" : "bhabitatrampage", m.moveAnim, m.speed);
          if (walkTo(m, x, y, map, fences)) {
            --m.stack.back().second; // finish the command only after arrival
            return;
          }
        }
        if (routed) break;
      }
      m.rampageBuilding = -1;
      continue;
    } else if (command == Command::Hatch) {
      // Out of the egg: a baby (zoo.exe bHatch's last step, fHatch 0x60cacb ->
      // 0x60cae0 -> 0x613d24, done at once: 10322 "Congratulations! %s has
      // hatched", priority 4 as a birth's - good news)
      m.egg = false;
      this->markDirty(m.exhibit);
      if (this->rm)
        this->notices.push_back({this->unitText(m, 10322), 1, m.id});
      ZT_STEP("0x437860", "Hatched: a baby", {"name", m.name});
      continue;
    } else if (command == Command::Dustball) {
      // zoo.exe fDustBall 0x588f69 -> animalDustBallEnd 0x588d47: the ball
      // played, then the prey eaten (0x588c2d). ours: (bCaughtPrey's: the catch already showed it - the ball by where the
      // hunter is, zoo.exe 0x588d47 - and its set runs on after it)
      continue;
    } else if (command == Command::EnterBuilding || command == Command::ExitBuilding) {
      if(m.showTank>=0 && m.building>=0 && this->objects) {
        if(auto *o=const_cast<PlacedObjects*>(this->objects)->byId(m.building))
          o->showHidden=s.fn=="fenterbuilding" && infoOf(PlacedObjects::fileOf(*o)).hideBuilding;
      }
      // (an object's animalrest set - an ice floe's, driftwood's. zoo.exe
      // fEnterBuilding 0x5ad42c -> 0x5ad3f2 -> class slot 0x2a8, 0x5ad3a6:
      // the building at its goal tile (+0x214, slot 0x2e8 0x42a5af); with
      // its goal kind (+0x24e) that one, message 0x2722 to itself (slot
      // 0x2c0, 0x50c2ae); then it stops (slot 0x198, 0x427664) - the line
      // done. fExitBuilding 0x4a777e -> 0x4a7744 -> slot 0x2ac, 0x4a7705:
      // that building again, slot 0x2ec, then guestBuy 0x42d858 - its use
      // of it, the building's changes to it. Ours: in it from arriving,
      // stopped there; the changes as it leaves - leaveShelter)
      continue;
    } else {
      // zoo.exe behaviourRunFn 0x436065 compares the line's name with each
      // handler's in turn, then behaviourRunFnMoves 0x437ccd with the rest;
      // a name neither knows is false - the line fails (0x435f0a). The
      // stock fFollow(keeper) (bCaught) and the misspelt fPlayWithSoud /
      // fPlayWith are such names. ours: names the original does know that
      // we don't run yet are passed over
      static const std::set<std::string> known = {
          "fwalk", "fswim", "ftrot", "frun", "fspray", "fkeeperroutine", "fguestroutine", "fguideroutine",
          "fmaintroutine", "ffacetowardfood", "ffacetowardtarget", "fshowpredator", "fhidepredator",
          "fenterbuilding", "fexitbuilding", "fdustball", "fkillprey", "fcaughtprey", "fdie", "ftargetbuilding",
          "fhatch", "fendtrick", "freturntobuilding", "fplaysettank", "fplay", "fsetbase", "fplaywithbase",
          "fmoveentitydelta", "fmoveentityspecial", "fplaytime", "fplaysound", "fplayspecial", "fplaysp",
          "fplaysetprob", "fplaysetterrain", "fplayreverse", "fplaypingpong", "fpingpong", "fplaywithsound",
          "fplaysfx", "fplaystopatend", "fplayset", "fmoveslow", "fmovemedium", "fmovefast", "fmove",
          "fmovereverse", "fmovewithsound", "ffacedirection", "ffly", "fflyreverse", "fkeepcontrol", "fhalt"};
      if (!known.count(s.fn)) {
        failLine("0x437ccd");
        return;
      }
      // zoo.exe fFaceDirection 0x601dbc: exactly one argument, not empty,
      // to 0x60cac6 - which does nothing (true), the line done; otherwise
      // the line fails. fSpray 0x60caad: 0x60cac6 whatever its arguments
      if (s.fn == "ffacedirection" && (s.args.size() != 1 || s.args[0].empty())) {
        failLine("0x601dbc");
        return;
      }
      // zoo.exe fPlaySound 0x601a62: exactly one argument, else the line
      // fails; the sound played through 0x601c7f (the play-with-sound core
      // 0x588f7e, its animation "0" - 0x63ba4c). ours: the sound heard, the
      // animation running on (the core's queue with "0" not read; no stock
      // set uses fPlaySound)
      // zoo.exe fKillPrey 0x60cb25 -> 0x60cb3a: an animal (0x638690) with a
      // target (ZTUnit::unitTarget 0x4a4b53: +0x178, a unit not being
      // removed) - the target's class slot 0xcc(0) (+0x13f cleared, its
      // panel shown again if selected: 0x43f0c5 / 0x42a9fa) and 0x4fa78e(1),
      // taken away (removed at the sweep 0x434df7) - the line done; none,
      // false: the line fails. ours: its target the one its dust ball holds,
      // else the animal or the guest it's chasing
      if (s.fn == "fkillprey") {
        int prey = -1;
        for (int id : {m.victim, m.huntAnimal})
          if (prey < 0 && id >= 0 && std::find(this->kills.begin(), this->kills.end(), id) == this->kills.end())
            if (const Member *p = this->member(id); p && !p->takenAway)
              prey = id;
        if (prey >= 0) {
          this->member(prey)->takenAway = true;
          if (m.victim == prey) { m.victim = -1; m.killIn = 0; }
          ZT_STEP("0x60cb3a", "fKillPrey: its target taken away", {"prey", this->member(prey)->name});
        } else if (m.prey >= 0 && this->takeGuestAway) {
          this->takeGuestAway(m.prey);
          ZT_STEP("0x60cb3a", "fKillPrey: its target (a guest) taken away", {"guest", m.prey});
          m.prey = -1;
        } else {
          failLine("0x60cb3a");
          return;
        }
      }
      // zoo.exe fReturnToBuilding 0x60cc66 -> 0x60cc7b: a unit (0x6386b0) -
      // its class slot 0x2b0 (0x5a05e7: findNearestOfKind 0x5a033a of kind
      // "fun" (0x63af90, made from 0x6395b4 by 0x5666c0) - its own habitat's,
      // with room, of the type id its slot 0x298 gives - the goal set) - its
      // result the line's. An animal's slot 0x298 is 0x4010e1, 0, and no
      // building type's id (+0x104, slot 0x20 - 0x597d07's counter from 19000)
      // is 0: none found, false - the line fails
      if (s.fn == "freturntobuilding") {
        failLine("0x60cc7b");
        return;
      }
      if (s.fn == "fplaysound") {
        if (s.args.size() != 1) {
          failLine("0x601a62");
          return;
        }
        playSound(m, s.args[0]);
      }
      // zoo.exe fMoveEntityDelta 0x601aa5 / fMoveEntitySpecial 0x601b73:
      // exactly three arguments (numbers, slot 0x34), else the line fails;
      // with three, the unit moved through 0x6005c3 and the line done.
      // 0x6005c3 adds them to its place (+0x114/+0x118: 64 a tile; +0x11c:
      // 16 a level) and puts it there (0x4a746a - its check reads the place
      // already moved, so a failed one doesn't move it back). Delta: the
      // numbers as they are (and +0x20d, frozen, cleared - ours has no such
      // flag; its icon 0x4b1e62 rebuilt - ours draws from the place).
      // Special: the first two turned by its way (+0x12c: 0 -y, 2 +x, 4 +y,
      // 6 -x) - 2 (b,-a), 6 (-b,a), 0 (-a,-b), else (a,b) - from its tile's
      // centre (- place % 64 + 32). ours: the height kept only swimming in a
      // tank (on land it stands on the ground); kept on the map
      if (s.fn == "fmoveentitydelta" || s.fn == "fmoveentityspecial") {
        if (s.args.size() != 3) {
          failLine(s.fn == "fmoveentitydelta" ? "0x601aa5" : "0x601b73");
          return;
        }
        int a = std::atoi(s.args[0].c_str()), b = std::atoi(s.args[1].c_str());
        const int c = std::atoi(s.args[2].c_str());
        const int px = static_cast<int>(std::floor(m.x * 64.0f)), py = static_cast<int>(std::floor(m.y * 64.0f));
        int dx = a, dy = b;
        if (s.fn == "fmoveentityspecial") {
          const int way = (static_cast<int>(std::lround(std::atan2(m.fx, -m.fy) / (3.14159265f / 4.0f))) % 8 + 8) % 8;
          if (way == 2) { dx = b; dy = -a; }
          else if (way == 6) { dx = -b; dy = a; }
          else if (way == 0) { dx = -a; dy = -b; }
          dx += 32 - px % 64;
          dy += 32 - py % 64;
        }
        m.x = std::clamp((px + dx) / 64.0f, 0.0f, std::max(0.0f, map.getWidth() - 0.001f));
        m.y = std::clamp((py + dy) / 64.0f, 0.0f, std::max(0.0f, map.getHeight() - 0.001f));
        if (this->tankAt && this->tankAt(m.x, m.y) >= 0)
          m.swimZ = m.swimTo = std::max(0.0f, m.swimZ + c / 16.0f);
        m.path.clear();
        m.pathAt = 0;
        ZT_STEP(s.fn == "fmoveentitydelta" ? "0x601aa5" : "0x601b73", "Moved by its script line", {"animal", m.name});
      }
      continue;
    }
    return;
  }
  if (!m.stepping)
    play(m, "stand", Member::Mode::Loop, 1.0f);
}

bool Animals::walkTo(Member &m, float gx, float gy, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  // As before 2026.10.28: a route is planned against the ground, the fences
  // and the slopes, never against other animals - animals may pass one
  // another, and separateIdle() is what keeps them from idling on a spot.
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  int tx = static_cast<int>(std::floor(gx)), ty = static_cast<int>(std::floor(gy));
  // (its own tile always: something just put down on it, it walks out)
  // A performer of a marine show swims through the portal into the show tank
  // and back out again, so both tanks of the pair count as its own ground and
  // the portal is a way through the wall they share. The original keeps the
  // tanks an animal may cross into in its own set (ZTAnimal+0x15c, a std::set
  // of tank ids): Task85_GoToShowTank's ctor holds a pointer to that set
  // (zoo.exe 0x5a3c4f) and both show tasks accept a tank only if the set has
  // it (showTankSetHas, 0x5a3585; Task85 0x5a3897, Task86 0x5a375c). That
  // set is exactly the show tank and the home tank this checks.
  auto pass = [&](int x, int y) {
    if (x == sx && y == sy) return true;
    if (standable(x, y, t, m.exhibit, map, fences)) return true;
    return m.showTank >= 0 &&
           (standable(x, y, t, m.showTank, map, fences) ||
            (m.showHome >= 0 && standable(x, y, t, m.showHome, map, fences)));
  };
  auto step = [&](int x, int y, int nx, int ny) {
    Fences::Edge e = nx != x ? Fences::Edge{false, std::max(x, nx), y}
                             : Fences::Edge{true, x, std::max(y, ny)};
    int pool=-1,land=-1;
    if(t.underwater && !t.onlyUnderwater && t.swims && fences.shoreAccess(e,map,pool,land))return true;
    if (canStep(x, y, nx, ny, map, fences)) return true;
    if (m.showTank < 0) return false;
    int show = -1, home = -1;
    return fences.showPortal(e, show, home) && (show == m.showTank || home == m.showTank);
  };
  std::vector<std::pair<int, int>> tiles;
  if (!Pathfinder::find(map.getWidth(), map.getHeight(), sx, sy, tx, ty, pass, step, tiles, 4000))
    return false;
  std::vector<std::pair<float, float>> points;
  points.push_back({m.x, m.y});
  for (size_t i = 1; i + 1 < tiles.size(); i++)
    points.push_back({tiles[i].first + 0.5f, tiles[i].second + 0.5f});
  points.push_back({gx, gy});
  Pathfinder::smooth(points, pass, step);
  m.path.assign(points.begin() + 1, points.end());
  m.pathAt = 0;
  if (m.path.empty())
    return false;
  m.anim = m.moveAnim;
  m.mode = Member::Mode::Loop;
  m.animTime = 0;
  m.stepping = true;
  return true;
}

bool Animals::testRoute(int id, int gx, int gy, const WorldMap &map, const Fences &fences,
                        std::vector<std::pair<int, int>> &out) const {
  const Member *m = this->member(id);
  if (!m)
    return false;
  const Type &t = this->typeList[m->type];
  int sx = static_cast<int>(std::floor(m->x)), sy = static_cast<int>(std::floor(m->y));
  auto pass = [&](int x, int y) { return (x == sx && y == sy) || standable(x, y, t, m->exhibit, map, fences); };
  auto step = [&](int x, int y, int nx, int ny) { return canStep(x, y, nx, ny, map, fences); };
  return Pathfinder::find(map.getWidth(), map.getHeight(), sx, sy, gx, gy, pass, step, out, 4000);
}

void Animals::labPlay(int id, const std::string &anim, float seconds) {
  Member *m = this->member(id);
  if (!m)
    return;
  m->stack.clear();
  m->path.clear();
  m->pathAt = 0;
  play(*m, anim, Member::Mode::Loop, seconds > 0 ? seconds : std::max(1.0f, animSeconds(*m, anim)));
  m->stepping = true;
}

void Animals::labBehave(int id, const std::string &set) {
  Member *m = this->member(id);
  if (!m || !this->set(*m, set))
    return;
  m->stack.clear();
  m->path.clear();
  m->pathAt = 0;
  m->stepping = false;
  pushSet(*m, set);
}

std::vector<std::string> Animals::labAnims(int id) const {
  std::vector<std::string> out;
  if (const Member *m = this->member(id))
    for (const auto &kv : this->typeList[m->type].anims[sub(*m)])
      out.push_back(kv.first);
  return out;
}

std::vector<std::string> Animals::labSets(int id) const {
  std::vector<std::string> out;
  // (its own, and the male's it falls back on - set(): a female or young
  // one with no sets of its own plays the male's)
  if (const Member *m = this->member(id))
    for (int s : {sub(*m), 0})
      for (const auto &kv : this->typeList[m->type].sets[s])
        if (std::find(out.begin(), out.end(), kv.first) == out.end())
          out.push_back(kv.first);
  return out;
}

// fWalk(0,0): to somewhere else in its exhibit. In the original a move line
// with no target (fMove's 0x43646e: no task, its move done, no route) asks
// for a move (+0x20e), task 1 is set (Task1::tick 0x436244, slot 0x184
// allowing), and task 1's tick walks it about (ZTAnimal's
// walkOnWhenRouteDone 0x43e440 -> unitWalkAbout 0x43d948): its own pick
// first (slot 0x1f4, 0x43e363: a way out, away from a crowd - below), then
// a tile on a ring round it, half widths rand % RandomX / RandomY + 1 +
// BaseX / BaseY (2..6, OriginalConstants) - passable for it (slot 0x168,
// 0x4273ec: its exhibit, a swimmer's own water or land) - at random and
// routed to; no route, its four neighbours from a random one on, not
// wrapping round (tileNeighbours4 0x4171a1), the first it can step to
bool Animals::walkSomewhere(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  const int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  std::uniform_real_distribution<float> in(0.15f, 0.85f);
  // The ring walk (the end of unitWalkAbout 0x43d948): loose, on the zoo's
  // open ground (standable with no exhibit)
  auto ringWalk = [&]() {
    auto rnd = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(this->rng); };
    const int rx = rnd(Original::aiWalkRandom) + 1 + Original::aiWalkBase;
    const int ry = rnd(Original::aiWalkRandom) + 1 + Original::aiWalkBase;
    auto passable = [&](int x, int y) {
      if (!standable(x, y, t, m.exhibit, map, fences))
        return false;
      // (a swimmer wanders where it is: the water in the water, else land -
      // zoo.exe 0x4273ec)
      const bool wet = isWater(map, x, y) || (tankAt && tankAt(x + 0.5f, y + 0.5f) >= 0);
      return !t.swims || wet == m.waterMode;
    };
    std::vector<std::pair<int, int>> ring;
    for (int dx = -rx; dx <= rx; dx++)
      for (int dy = -ry; dy <= ry; dy++)
        if ((dx == rx || dx == -rx || dy == ry || dy == -ry) && passable(sx + dx, sy + dy))
          ring.push_back({sx + dx, sy + dy});
    ZT_STEP("0x43d948", "Walk about: a tile on a ring round it, at random", {"half width", rx},
            {"half height", ry}, {"tiles", static_cast<int>(ring.size())});
    if (!ring.empty()) {
      auto [x, y] = ring[static_cast<size_t>(rnd(static_cast<int>(ring.size())))];
      if (walkTo(m, x + in(this->rng), y + in(this->rng), map, fences))
        return true;
    }
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = rnd(4); i < 4; i++) {
      const int nx = sx + d[i][0], ny = sy + d[i][1];
      if (passable(nx, ny) && canStep(sx, sy, nx, ny, map, fences) &&
          walkTo(m, nx + in(this->rng), ny + in(this->rng), map, fences))
        return true;
    }
    return false;
  };
  if (m.exhibit < 0)
    return ringWalk();
  // A way out it can take: it takes it (zoo.exe: no chance roll)
  if (breakOut(m, map, fences))
    return true;
  // Away from the guests crowding it (zoo.exe 0x43e363 -> 0x42f6a9): one of
  // its exhibit's tiles that way, or either way next to it (0x43e511), at
  // random - none, the ring walk below - and the way's forgotten
  if (m.crowdDir >= 0) {
    int dir = m.crowdDir;
    m.crowdDir = -1;
    if (const Fences::Exhibit *here = fences.exhibit(m.exhibit)) {
      int ax = static_cast<int>(std::floor(m.x)), ay = static_cast<int>(std::floor(m.y));
      std::vector<std::pair<int, int>> way;
      for (auto [x, y] : here->tiles)
        if (nearDirection(dir, direction8(x - ax, y - ay)))
          way.push_back({x, y});
      if (!way.empty()) {
        auto [x, y] = way[std::uniform_int_distribution<size_t>(0, way.size() - 1)(this->rng)];
        std::uniform_real_distribution<float> in(0.15f, 0.85f);
        // ours: a tile it can't stand on or get to - an ordinary walk
        if (standable(x, y, t, m.exhibit, map, fences) && walkTo(m, x + in(this->rng), y + in(this->rng), map, fences))
          return true;
      }
    }
  }
  const Fences::Exhibit *ex = fences.exhibit(m.exhibit);
  if (!ex || ex->tiles.empty())
    return false;
  return ringWalk();
}

bool Animals::isWater(const WorldMap &map, int x, int y) {
  const MapTile *t = map.getTile(x, y);
  return t && (t->terrainType == 9 || t->terrainType == 10);
}

bool Animals::onWater(const Member &m) const {
  return this->mapRef && isWater(*this->mapRef, static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
}

// Into or out of the water; a drink
// zoo.exe 0x4a8f89 (animalDropDung): on a tank's tile (tile +0x85 & 0x20)
// with its habitat (slot 0x24c, its exhibit) a tank, no pile: the tank's
// purity less the type's cPooWaterImpact (+0x448, set through 0x49340d); on
// water (tile +0x83 & 3 - terrain 9 or 10, as habitatNearestWater reads it)
// nothing, and it says so (0: the meter stays full); else a pile - poo, or
// dinopoo for cDinoZoodoo, sized by cSmallZoodoo/cGiantZoodoo. ours: on a
// tank's tile whose habitat isn't that tank (the land part of a habitat
// joined to it) nothing either, as on water
// A move's animation rate (zoo.exe 0x413754 sets unit +0x1fc as the move
// starts; 0x435bbe plays the move's animation at it, in percent): fMove's
// own (0x425533, pace -1), else the walk's cSlowAnimSpeed (0x4136bf), the
// trot's cMediumAnimSpeed (0x4a319c), the run's cFastAnimSpeed (0x4a5194) -
// their Water ones on a tank's tile (slot 0x268) or water (0x410fe0, tile
// +0x83 & 3; the walk's in a tank, water state 3 or 4)
void Animals::setAnimRate(Member &m, int pace, int fMoveRate) {
  const Type &t = this->typeList[m.type];
  if (pace < 0) {
    m.animRate = fMoveRate;
  } else {
    const bool wet = (this->tankAt && this->tankAt(m.x, m.y) >= 0) || onWater(m);
    m.animRate = (wet ? t.animSpeedWater : t.animSpeed)[std::clamp(pace, 0, 2)];
  }
  m.animRateFor = m.moveAnim;
  m.moveAnimLeft = -1; // (a new move: its animation for the whole of it, unless fMove's time says otherwise)
}

bool Animals::dropDung(Member &m, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  const int tank = this->tankAt ? this->tankAt(m.x, m.y) : -1;
  const Fences::Exhibit *home = m.exhibit >= 0 ? fences.exhibit(m.exhibit) : nullptr;
  if (tank >= 0 && home && home->tank) {
    if (!this->setTankPurity || !this->tankPurity)
      return false;
    const float was = this->tankPurity(m.exhibit);
    this->setTankPurity(m.exhibit, was - float(t.pooWaterImpact));
    ZT_STEP("0x4a8f89", "Dung in the tank's water: no pile, the water dirtier", {"purity", was, was - t.pooWaterImpact},
            {"cPooWaterImpact", t.pooWaterImpact});
    return true;
  }
  if (tank >= 0 || onWater(m)) {
    ZT_STEP("0x4a8f89", "On water: no dung, the meter stays full");
    return false;
  }
  this->items->addDung(m.exhibit, m.x, m.y, t.dinoDung, m.baby ? t.youngDungSize : t.dungSize);
  if (m.exhibit >= 0)
    this->markDirty(m.exhibit);
  ZT_STEP("0x4a8f89", "Drops a dung pile", {"x", m.x}, {"y", m.y});
  return true;
}

void Animals::waterTick(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  const Fences::Exhibit *ex = m.exhibit >= 0 ? fences.exhibit(m.exhibit) : nullptr;
  if (!ex || m.escaped || m.boxed || m.dying || m.sleeping || m.building >= 0 || m.showTank >= 0)
    return;
  std::uniform_int_distribution<int> pct(0, 99);
  // (global_animals.ai: cWaterCheck 40, cBoredCheck 30)
  if (m.waterCheck < 0)
    m.waterCheck = std::uniform_int_distribution<int>(0, 39)(this->rng);
  if (m.boredCheck < 0)
    m.boredCheck = std::uniform_int_distribution<int>(0, 29)(this->rng);
  if (--m.waterCheck <= 0) {
    m.waterCheck = 39;
    std::vector<std::pair<int,int>> water,land,tanks;
    auto group=t.underwater && !t.onlyUnderwater ? fences.habitatGroup(m.exhibit,map) : std::set<int>{m.exhibit};
    for(int id:group)if(const auto *part=fences.exhibit(id))
      for(auto [x,y]:part->tiles)
        (part->tank?tanks:isWater(map,x,y)?water:land).push_back({x,y});
    int currentTank=tankAt?tankAt(m.x,m.y):-1;
    bool wet=currentTank>=0 || onWater(m);
    std::vector<std::pair<int,int>> *to=nullptr;
    if(wet && !t.onlyUnderwater && pct(this->rng)<t.enterLandChance &&
       static_cast<int>(land.size())>t.landNeeded)to=&land;
    else if(!wet && t.underwater && pct(this->rng)<t.enterTankChance &&
            static_cast<int>(tanks.size())>t.underwaterNeeded)to=&tanks;
    else if(!wet && t.swims && pct(this->rng)<t.enterWaterChance &&
            static_cast<int>(water.size())>t.waterNeeded)to=&water;
    if(to && !to->empty()) {
      auto [x,y]=(*to)[std::uniform_int_distribution<size_t>(0,to->size()-1)(this->rng)];
      bool previous=m.waterMode;
      m.waterMode=to!=&land;
      m.stack.clear();m.moveAnim="walk";m.speed=t.forAge(m.baby).slow;
      int targetTank=tankAt?tankAt(x+0.5f,y+0.5f):-1;
      const auto *pool=fences.exhibit(currentTank>=0?currentTank:targetTank);
      if(pool)m.swimTo=static_cast<float>(fences.waterLevel(*pool));
      if(!walkTo(m,x+0.5f,y+0.5f,map,fences))m.waterMode=previous;
    } else if(m.pathAt>=m.path.size())m.waterMode=wet;
  }
  if (--m.boredCheck <= 0) {
    m.boredCheck = 29;
    // A drink: beside the nearest fresh water, head down (its eat)
    bool kin = false;
    for (const Member &o : this->list)
      kin = kin || (o.id != m.id && o.exhibit == m.exhibit && o.type == m.type);
    int chase = kin ? t.forAge(m.baby).chaseAnimalChance : 0;
    // (zoo.exe 0x438ad7: a drink against a game of tag with one of its own
    // kind - the nearest adult within 5 - not when hungry; harmless, over
    // when they touch or after cChaseTimeOut 10 s)
    bool drink = false;
    if (m.hunger < t.hungerThreshold && t.drinkWaterChance + chase > 0 && m.huntAnimal < 0 && m.fleeFrom < 0) {
      drink = std::uniform_int_distribution<int>(1, t.drinkWaterChance + chase)(this->rng) <= t.drinkWaterChance;
      if (!drink && !m.baby) {
        const Member *mate = nullptr;
        float bestD = 1e30f;
        for (const Member &o : this->list) {
          if (o.id == m.id || o.type != m.type || o.exhibit != m.exhibit || o.baby || busy(o))
            continue;
          if (std::fabs(o.x - m.x) > 5.0f || std::fabs(o.y - m.y) > 5.0f)
            continue;
          float d = (o.x - m.x) * (o.x - m.x) + (o.y - m.y) * (o.y - m.y);
          if (d < bestD) {
            bestD = d;
            mate = &o;
          }
        }
        if (mate) {
          m.huntAnimal = mate->id;
          m.huntStart = this->now;
          m.tag = true;
        }
      }
    }
    if (drink && t.drinkWaterChance > 0 && !m.waterMode && !onWater(m)) {
      int bx = -1, by = -1;
      float best = 1e30f;
      for (auto [x, y] : ex->tiles) {
        const MapTile *tile = map.getTile(x, y);
        if (!tile || tile->terrainType != 9)
          continue;
        float d = (x + 0.5f - m.x) * (x + 0.5f - m.x) + (y + 0.5f - m.y) * (y + 0.5f - m.y);
        if (d < best) {
          best = d;
          bx = x;
          by = y;
        }
      }
      if (bx >= 0) {
        // (a land tile beside it, the nearest)
        float gx = -1, gy = -1, near = 1e30f;
        const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto &dd : d4) {
          int x = bx + dd[0], y = by + dd[1];
          if (isWater(map, x, y) || !standable(x, y, t, m.exhibit, map, fences))
            continue;
          float d = (x + 0.5f - m.x) * (x + 0.5f - m.x) + (y + 0.5f - m.y) * (y + 0.5f - m.y);
          if (d < near) {
            near = d;
            gx = x + 0.5f + (bx - x) * 0.4f;
            gy = y + 0.5f + (by - y) * 0.4f;
          }
        }
        // (zoo.exe Task 0x7a, whileGoing 0x4a0c55: the nearest water tile of
        // its habitat that isn't terrain 10 (0x4a0d77), then its
        // bFindOtherFood - every stock one fWalk(otherfood,0), most then
        // fPlaySet(bEat) - "Going to drink water"; with no such set, no
        // drink. At the water, atTarget 0x4265a0 starts its bEat if the set
        // didn't. ours: "otherfood" is the land beside the water, as near it
        // as it stands)
        if (gx >= 0 && this->set(m, "bfindotherfood")) {
          m.stack.clear();
          m.path.clear();
          m.pathAt = 0;
          m.foodItem = -1;
          m.drinkX = bx + 0.5f;
          m.drinkY = by + 0.5f;
          m.drinkGoX = gx;
          m.drinkGoY = gy;
          bool eats = false;
          for (const Step &st : *this->set(m, "bfindotherfood"))
            for (const std::string &a : st.args)
              eats = eats || a == "beat";
          if (!eats && this->set(m, "beat"))
            pushSet(m, "beat");
          pushSet(m, "bfindotherfood");
          m.stepping = false;
          ZT_STEP("0x4a0c55", "Going to drink water", {"x", m.drinkX}, {"y", m.drinkY});
        }
      }
    }
  }
}

bool Animals::warmOne() {
  if (!this->rm)
    return false;
  // (the one loading: collected once it's done; the next one started)
  if (!this->warming.empty()) {
    Animation *done = nullptr;
    if (!this->rm->takeAnimationAhead(this->warming, done, false)) {
      if (this->rm->loadingAhead(this->warming))
        return true; // (still loading)
    } else if (!this->artCache.count(this->warming))
      this->artCache[this->warming] = done;
    this->warming.clear();
  }
  while (!this->warmQueue.empty()) {
    const std::string path = this->warmQueue.front();
    this->warmQueue.pop_front();
    if (this->artCache.count(path))
      continue;
    if (!this->rm->hasResource(path + ".ani")) {
      this->artCache[path] = nullptr;
      continue;
    }
    if (!this->rm->loadAnimationAhead(path)) {
      this->warmQueue.push_front(path); // (another's loading)
      return true;
    }
    this->warming = path;
    return true;
  }
  return false;
}

void Animals::update(float seconds, double simSeconds, const WorldMap &map, const Fences &fences) {
  this->now = simSeconds;
  this->mapRef = &map;
  this->fencesRef = &fences;
  // (each species in the zoo: its animations queued to load ahead, warmOne)
  if (this->typeWarmed.size() != this->typeList.size())
    this->typeWarmed.assign(this->typeList.size(), 0);
  for (const Member &m : this->list)
    if (m.type >= 0 && m.type < static_cast<int>(this->typeList.size()) && !this->typeWarmed[m.type]) {
      this->typeWarmed[m.type] = 1;
      for (const auto &set : this->typeList[m.type].anims)
        for (const auto &entry : set)
          if (!this->artCache.count(entry.second) && this->warmQueued.insert(entry.second).second)
            this->warmQueue.push_back(entry.second);
    }
  // Prey caught last time: gone
  {
    std::vector<int> kills;
    kills.swap(this->kills);
    for (int id : kills)
      remove(id);
  }
  // Exhibits are rated again every 7 seconds, and when their poo changes
  for (auto &[id, e] : this->evaluations) {
    e.timerMs += seconds * 1000.0f;
    if (e.timerMs >= kRateEveryMs)
      e.dirty = true;
    // (an exhibit made bigger or smaller: rated again at once)
    if (const Fences::Exhibit *ex = fences.exhibit(id))
      if (static_cast<int>(ex->tiles.size()) != e.tiles)
        e.dirty = true;
    if (this->items) {
      int dung = static_cast<int>(this->items->ofKind(ZooItems::Kind::Dung, id).size());
      if (dung != e.dung)
        e.dirty = true;
    }
  }
  for (Member &m : this->list) {
    ZT_WHO("animal", m.id);
    const Type &t = this->typeList[m.type];
    // (moving, its move's animation at its rate - zoo.exe 0x435bbe: the
    // time x unit +0x1fc / 100)
    const bool movingAnim = m.pathAt < m.path.size() && !m.animRateFor.empty() && m.anim == m.animRateFor;
    m.animTime += movingAnim ? seconds * static_cast<float>(m.animRate) / 100.0f : seconds;
    // (fMove with a time: its plays done, on with its plain move animation
    // at the same rate - zoo.exe 0x43646e's second queue)
    if (movingAnim && m.moveAnimLeft >= 0) {
      m.moveAnimLeft -= seconds * static_cast<float>(m.animRate) / 100.0f;
      if (m.moveAnimLeft <= 0) {
        m.moveAnimLeft = -1;
        m.moveAnim = m.anim = m.animRateFor = m.moveAnimAfter;
        m.animTime = 0;
      }
    }
    // Lowering a tank also lowers its swimmers; an old target must not leave
    // one floating above the new surface. Trick animations control their own rise.
    if(t.underwater && !m.showTrick && !m.boxed) {
      const auto *pool=fences.exhibit(fences.exhibitAt(static_cast<int>(std::floor(m.x)),static_cast<int>(std::floor(m.y))));
      if(pool && pool->tank && pool->water>0) {
        float surface=static_cast<float>(fences.waterLevel(*pool));
        m.swimZ=std::clamp(m.swimZ,0.0f,surface);
        m.swimTo=std::clamp(m.swimTo,0.0f,surface);
      }
    }
    // Its faces float up and go after 1.25 s
    for (auto &f : m.faces)
      f.second += seconds * 1000.0f;
    m.faces.erase(std::remove_if(m.faces.begin(), m.faces.end(),
                                 [](const std::pair<bool, float> &f) { return f.second >= 1250.0f; }),
                  m.faces.end());
    // Its status: a tick a second (a held one waits)
    if (m.id != this->carried) {
      m.tickMs += seconds * 1000.0f;
      while (m.tickMs >= kTickMs) {
        m.tickMs -= kTickMs;
        tick(m, map, fences);
      }
    }
    m.food = std::clamp(100.0f - m.hunger, 0.0f, 100.0f);
    if (m.id == this->carried)
      continue;
    // In a shelter: out of sight until its time's up (ticks: seconds)
    if (m.inside && m.insideFor > 0) {
      m.insideFor -= seconds;
      if (m.insideFor <= 0)
        leaveShelter(m);
      continue;
    }
    if (m.inside && m.stack.empty() && !m.stepping)
      leaveShelter(m); // (a toy's set done)
    // Crated: waits (in an exhibit, a couple of seconds, then out)
    if (m.boxed) {
      int ex = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
      m.boxTimer = ex >= 0 ? m.boxTimer + seconds : 0;
      continue;
    }
    // Caught: still under the dust ball till it's over
    if (m.caughtBy >= 0) {
      if (!this->member(m.caughtBy))
        m.caughtBy = -1;
      continue;
    }
    // The dust ball (bCaughtPrey: fDustBall, zoo.exe 0x588d47): when it's
    // over the prey's gone (no body, no word of it), and its bCaughtPrey
    // runs on from there - a tiger's picks bAfterCaught on land, bWaterActive
    // in water; a shark's, fDustBall then fPlaySetTank(bAcsurf, bAcsub), its
    // after-the-catch at the surface or under it (the dust ball itself was
    // this catch's: fDustBall is passed over). No bCaughtPrey: bAfterCaught
    if (m.victim >= 0) {
      m.killIn -= seconds;
      if (m.killIn <= 0) {
        ZT_STEP("0x588d47", "The dust ball's over: its prey is gone", {"prey",
                this->member(m.victim) ? this->member(m.victim)->name : std::string("?")});
        this->kills.push_back(m.victim);
        m.victim = -1;
        m.stack.clear();
        m.stepping = false;
        const char *after = this->set(m, "bcaughtprey") ? "bcaughtprey" : "baftercaught";
        if (this->set(m, after))
          pushSet(m, after);
      }
      continue;
    }
    // Caught up with the animal it's after (on its tile): prey under a dust
    // ball (zoo.exe 0x4a6043); at play - its target an animal of its own
    // species, ZTUnit::targetIsOwnKind 0x4a4bb7 (ours: m.tag, set as the
    // game began) - it's over. (Task74's atTarget
    // 0x4a4dd2 asks 0x50e4e8 first: no catch while its set is bCaughtGuest,
    // bCaughtPrey or bCaughtEgg, nor of a guest whose slot 0x260 says no -
    // ours: its chase is over once it catches, huntAnimal -1)
    if (m.huntAnimal >= 0 && !busy(m) && !standingOff(m)) {
      Member *p = this->member(m.huntAnimal);
      bool contact=p && bodyDistance(m,m.x,m.y,bodyHeight(m,m.x,m.y,m.swimZ),*p,p->x,p->y,
                       bodyHeight(*p,p->x,p->y,p->swimZ)) <= 1.08f;
      if (contact) {
        if (m.tag) {
          m.huntAnimal = -1;
          m.tag = false;
        } else if (p->egg) {
          // An egg: it breaks (its bEatenEgg) and is left a shell for 15
          // ticks; the hunter plays its bCaughtEgg (zoo.exe 0x4a6043,
          // 0x5a5606) - no dust ball
          ZT_STEP("0x4a6043", "Eats an egg", {"egg", p->name});
          p->eaten = true;
          p->hatchIn = 15;
          p->stack.clear();
          p->stepping = false;
          if (this->set(*p, "beatenegg"))
            pushSet(*p, "beatenegg");
          m.huntAnimal = -1;
          m.path.clear();
          m.pathAt = 0;
          m.stack.clear();
          m.stepping = false;
          if (this->set(m, "bcaughtegg"))
            pushSet(m, "bcaughtegg");
          continue;
        } else {
          ZT_STEP("0x4a6043", "Catches its prey: under a dust ball", {"prey", p->name},
                  {"chased for (s)", this->now - m.huntStart});
          p->caughtBy = m.id;
          p->x = m.x;
          p->y = m.y;
          p->path.clear();
          p->pathAt = 0;
          p->stack.clear();
          p->stepping = false;
          p->fleeFrom = -1;
          p->huntAnimal = -1;
          play(*p, "stand", Member::Mode::Loop, 3600.0f);
          m.victim = p->id;
          m.huntAnimal = -1;
          m.path.clear();
          m.pathAt = 0;
          m.stack.clear();
          // (by where it is - zoo.exe 0x588d47 on unit +0x258: in a tank at
          // its surface the surface ball, under it the submerged ball; a
          // swimmer in the water its water ball; else the dust ball - and
          // the dust ball if it hasn't the one asked for)
          std::string ball = "dustball";
          if (this->tankAt && this->tankAt(m.x, m.y) >= 0) {
            bool top = this->tankDepth && m.swimZ >= this->tankDepth(this->tankAt(m.x,m.y)) - 0.35f;
            ball = top ? "surfaceball" : "submergedball";
          } else if (onWater(m)) {
            ball = "waterball";
          }
          if (ball != "dustball" && !this->typeList[m.type].anims[sub(m)].count(ball) &&
              !this->typeList[m.type].anims[0].count(ball))
            ball = "dustball";
          float secs = std::max(1.0f, animSeconds(m, ball));
          play(m, ball, Member::Mode::Loop, secs);
          m.killIn = secs;
          playSound(m, "dustball");
          continue;
        }
      }
    }
    // Mauling a guest: held (+0x20d) till the guest's set lets it go
    // (Guests::caughtNext's fShowPredator -> showPredator). ours: a guest
    // gone from its set meanwhile (left, taken away, a load) lets it go too
    if (m.mauling >= 0) {
      if (this->guestHolds && this->guestHolds(m.mauling, m.id))
        continue;
      m.mauling = -1;
      m.hidden = false;
    }
    // Caught up with its prey (zoo.exe animalCaughtTarget 0x4a6043, a
    // guest): the guest stopped and frozen, its attacker (+0x22c) this one,
    // its bCaughtBy set started (0x50ed8f), attacked (+0x33b) and thinking
    // 10246; this one's prey forgotten (slot 0x258), its bCaughtGuest
    // (fPlayTime(stand, 6)) and frozen (+0x3a5, +0x20d). No message: 11002
    // "%1s has been eaten by %2s." is a staff victim's (0x50e648 posts it
    // only for 0x638710, ZTStaff)
    if (m.prey >= 0 && this->preyAt && !m.tranquilised && !m.boxed) {
      float px = 0, py = 0;
      if (this->preyAt(m.prey, px, py) && std::hypot(px - m.x, py - m.y) < 0.6f) {
        ZT_STEP("0x4a6043", "Catches a guest", {"guest", m.prey});
        const int guest = m.prey;
        m.prey = -1;
        m.mauling = guest;
        m.path.clear();
        m.pathAt = 0;
        m.stack.clear();
        play(m, "stand", Member::Mode::Loop, 3600.0f);
        if (this->caughtPrey)
          this->caughtPrey(guest, m);
        continue;
      }
    }
    if (m.tranquilised) {
      if (m.animTime >= m.playFor && m.anim != "sleep" && m.anim != "lie_idle")
        play(m, this->typeList[m.type].anims[0].count("sleep") ? "sleep" : "lie_idle", Member::Mode::Loop, 3600.0f);
      continue;
    }
    // Over the fence it's crossing: the moment it's on the far side, the
    // fence is broken if it bashed (or jumped with any bash strength) its
    // way out (zoo.exe 0x613a1c, brokfenc.wav)
    if (m.crossing.x >= 0) {
      int tx = static_cast<int>(std::floor(m.x)), ty = static_cast<int>(std::floor(m.y));
      bool farSide = m.crossing.alongX ? ((m.crossingFromY < m.crossing.y) ? ty >= m.crossing.y : ty < m.crossing.y)
                                       : ((m.crossingFromX < m.crossing.x) ? tx >= m.crossing.x : tx < m.crossing.x);
      if (farSide) {
        if (m.breaks)
          this->breaks.push_back(m.crossing);
        m.crossing = {false, -1, -1};
        m.breaks = false;
      }
    }
    separateIdle(m,seconds,map,fences);
    // Walking
    if (m.pathAt < m.path.size()) {
      auto [tx, ty] = m.path[m.pathAt];
      float dx = tx - m.x, dy = ty - m.y, dist = std::hypot(dx, dy);
      // Over the fence: its jump (or climb) once across, as long as that
      // takes
      if (static_cast<int>(m.pathAt) == m.crossAt && !m.crossAnim.empty() && m.crossing.x >= 0) {
        float secs = 1.0f;
        if (Animation *a = art(m, m.crossAnim))
          if (a->frameTimeMs() > 0 && a->frameCount() > 0)
            secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
        m.anim = m.crossAnim;
        m.mode = Member::Mode::Loop;
        m.animTime = 0;
        m.speed = std::max(0.2f, dist / secs);
        m.crossAnim.clear();
      }
      // The swimming this engine has always had (v2026.10.20 and before): a
      // straight run down the planned path at m.speed, passing through
      // anything in the way, rising or falling to swimTo as it goes. The
      // 10.28 steering turned each step into a search for a heading that
      // avoided other animals, which in a tank reads as fish milling about
      // rather than swimming. The one addition kept from 10.28 is that an
      // animal with nothing to do steps off another one (separateIdle).
      const int previousTank=tankAt?tankAt(m.x,m.y):-1;
      const bool amphibious=t.underwater && !t.onlyUnderwater && t.swims;
      if(amphibious && previousTank>=0 && !m.waterMode && !m.path.empty() &&
         (!tankAt || tankAt(m.path.back().first,m.path.back().second)<0)) {
        if(const auto *pool=fences.exhibit(previousTank)) {
          m.swimTo=static_cast<float>(fences.waterLevel(*pool));
          m.swimZ+=std::clamp(m.swimTo-m.swimZ,-seconds*2.0f,seconds*2.0f);
          if(std::abs(m.swimZ-m.swimTo)>0.001f)continue;
        }
      }
      float step = m.speed * seconds;
      if (dist > 0.0001f) {
        m.fx = dx / dist;
        m.fy = dy / dist;
      }
      // (swimming up or down as it goes: there by the end of the leg)
      if (m.swimZ != m.swimTo) {
        float frac = dist > step ? step / dist : 1.0f;
        m.swimZ += (m.swimTo - m.swimZ) * frac;
      }
      if (dist <= step) {
        m.x = tx;
        m.y = ty;
        if (static_cast<int>(m.pathAt) == m.crossAt) {
          // (landed: on at its run)
          m.crossAt = -1;
          m.anim = m.moveAnim;
          m.speed = t.forAge(m.baby).fast;
        }
        m.pathAt++;
      } else {
        m.x += dx / dist * step;
        m.y += dy / dist * step;
      }
      if(amphibious) {
        const int currentTank=tankAt?tankAt(m.x,m.y):-1;
        if(previousTank>=0 && currentTank<0)m.swimZ=m.swimTo=0;
        else if(previousTank<0 && currentTank>=0)
          if(const auto *pool=fences.exhibit(currentTank))
            m.swimZ=m.swimTo=static_cast<float>(fences.waterLevel(*pool));
      }
      // Onto another tile (zoo.exe 0x437572): rampaging - and not going
      // over a fence, that's its own - it turns to rubble whatever stands
      // there that can be (each thing once)
      {
        int ix = static_cast<int>(std::floor(m.x)), iy = static_cast<int>(std::floor(m.y));
        if (ix != m.tileX || iy != m.tileY) {
          bool first = m.tileX < 0;
          m.tileX = ix;
          m.tileY = iy;
          if (!first && m.rampaging && m.crossing.x < 0 && this->rubbleAt) {
            int n = this->rubbleAt(ix, iy, m);
            if (n > 0)
              ZT_STEP("0x437572", "Rampages onto a tile: what stands there is rubble", {"tile x", ix}, {"tile y", iy},
                      {"objects", n});
          }
        }
      }
      if (m.pathAt >= m.path.size()) {
        m.path.clear();
        m.pathAt = 0;
        m.stepping = false;
        // (a move is done on arriving - zoo.exe 0x43646e; no pause at the
        // end of an eating move)
        // At a shelter or toy: in it (hidden) for cTimeInside ticks, or its
        // play set there
        if (m.building >= 0 && !m.inside && this->objects) {
          for (const PlacedObjects::Object &o : this->objects->objects())
            if (o.id == m.building) {
              const ObjectInfo &info = infoOf(PlacedObjects::fileOf(o));
              // (its slot's set, b<name><goal><slot> - the slot +0x24d: an
              // ice floe's big slot 0, the penguins' small ones 1-4)
              std::string playSet = "b" + o.typeName + "animalrest" + std::to_string(std::max(0, m.restSlot));
              // (full by now - others went in first: no entrance with room,
              // 0x43b2f6 -> 0x42c02b, so not in. ours: one sent with no slot -
              // a show trick's toy - by the building's room alone, as before)
              if (m.restSlot < 0 ? shelterUsers(o.id) >= info.capacity : !slotHasRoom(o.id, info, m.restSlot, m)) {
                ZT_STEP("0x42c02b", "At the building, but it's full: not in", {"capacity", info.capacity});
                leaveShelter(m);
                break;
              }
              m.inside = true;
              m.insideFor = info.hideUser ? static_cast<float>(info.timeInside) : 0.0f;
              if (!info.hideUser && this->set(m, playSet))
                pushSet(m, playSet);
              else if (!info.hideUser)
                leaveShelter(m);
            }
        }
        // Over the fence it went for: broken behind it if it bashed through
        if (m.crossing.x >= 0) {
          if (m.breaks)
            this->breaks.push_back(m.crossing);
          m.crossing = {false, -1, -1};
          m.breaks = false;
        }
      }
      continue;
    }
    // Playing
    if (m.stepping && m.animTime < m.playFor)
      continue;
    if (m.stepping && (m.anim == "eat" || m.anim.rfind("eat_",0)==0 ||
        m.anim.find("_eat")!=std::string::npos || m.anim.find("_feed")!=std::string::npos ||
        m.anim=="feed" || m.anim.rfind("feed_",0)==0))
      consumeFood(m);
    m.stepping = false;
    nextStep(m, map, fences);
  }
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
CompassDirection Animals::facing(const WorldRenderer &view, const Member &m) const {
  float x0, y0, x1, y1, d;
  view.worldToScreenF(m.x, m.y, 0, x0, y0, d);
  view.worldToScreenF(m.x + m.fx, m.y + m.fy, 0, x1, y1, d);
  float angle = std::atan2(x1 - x0, -(y1 - y0));
  int octant = static_cast<int>(std::lround(angle / (3.14159265f / 4.0f)));
  octant = (octant % 8 + 8) % 8;
  static const CompassDirection dirs[8] = {
      CompassDirection::N, CompassDirection::NE, CompassDirection::E, CompassDirection::SE,
      CompassDirection::S, CompassDirection::SW, CompassDirection::W, CompassDirection::NW};
  return dirs[octant];
}

void Animals::collect(const WorldRenderer &view, const WorldMap &map,
                      std::vector<Fences::Drawable> &out) const {
  for (const Member &m : this->list) {
    // (in a tank: drawn with the tank, unless that's being drawn now)
    bool inTank = this->tankAt && this->tankAt(m.x, m.y) >= 0;
    if (this->onlyTank != -2 ? (!inTank || this->tankAt(m.x, m.y) != this->onlyTank) : inTank)
      continue;
    Animation *a = m.boxed ? this->crate(m) : this->art(m, m.anim);
    if (!a)
      continue;
    // Inside a shelter: not seen
    if ((m.inside && m.insideFor > 0) || m.hidden)
      continue;
    // Held (Move Animal): its likeness at the cursor, green where it can go
    if (m.id == this->carried) {
      float sx, sy, depth;
      view.worldToScreenF(m.x, m.y, groundAt(map, m.x, m.y)+(inTank?m.swimZ:0), sx, sy, depth);
      if(inTank) {
        Animation *under=!m.boxed?this->underArt(m,a):nullptr;
        if(this->onlySurface && !under)continue;
        if(!this->onlySurface && under)a=under;
      }
      SDL_Color tint = this->carriedFit ? SDL_Color{0, 255, 0, 255} : SDL_Color{255, 60, 60, 255};
      Fences::Drawable g{depth, sx, sy, a, m.boxed ? CompassDirection::SE : facing(view, m), tint, true};
      g.frame = 0;
      out.push_back(g);
      continue;
    }
    float ms = a->frameTimeMs() ? static_cast<float>(a->frameTimeMs()) : 100.0f;
    int count = std::max(1, a->frameCount());
    int f = static_cast<int>(m.animTime * 1000.0f / ms);
    switch (m.mode) {
    case Member::Mode::Once: f = std::min(f, count - 1); break;
    case Member::Mode::Reverse: f = count - 1 - std::min(f, count - 1); break;
    case Member::Mode::PingPong:
      f %= 2 * count;
      f = f < count ? f : 2 * count - 1 - f;
      break;
    case Member::Mode::Loop: f %= count; break;
    }
    float sx, sy, depth;
    view.worldToScreenF(m.x, m.y, groundAt(map, m.x, m.y) + (inTank ? m.swimZ : 0.0f), sx, sy, depth);
    // (the fight's dust ball isn't lit; the one caught is inside it)
    bool lit = m.id == this->hovered && m.victim < 0;
    if (m.caughtBy >= 0)
      depth -= 0.01f;
    // (one-view art - the dust ball - drawn as it is, whichever way it faces)
    CompassDirection side = facing(view, m);
    if (!a->hasFrames(side))
      side = CompassDirection::N;
    // In a tank: under the water, its "under" layer if it has one (and then
    // the animation itself on the surface pass); without, all of it under
    Animation *under = this->onlyTank != -2 && !m.boxed ? this->underArt(m, a) : nullptr;
    // Its shadow on the tank's floor under it, on its frame (zoo.exe
    // setupShadow 0x43f81c: "shadow%s", at the ground under the unit, the
    // shadow palette at 0.25)
    if (!Features::dayNightLighting && this->onlyTank != -2 && !this->onlySurface && !m.boxed)
      if (Animation *sh = this->shadowArt(m, a)) {
        float fx, fy, fd;
        view.worldToScreenF(m.x, m.y, groundAt(map, m.x, m.y), fx, fy, fd);
        Fences::Drawable sd{depth - 0.3f, fx, fy, sh, sh->hasFrames(side) ? side : CompassDirection::N,
                            SDL_Color{0, 0, 0, 64}, true};
        sd.frame = std::min(f, std::max(1, sh->frameCount()) - 1);
        out.push_back(sd);
      }
    if (this->onlySurface && !under)
      continue;
    if (under && !this->onlySurface) {
      Fences::Drawable ud{depth, sx, sy, under, under->hasFrames(side) ? side : CompassDirection::N,
                          SDL_Color{255, 255, 110, 255}, lit};
      ud.frame = std::min(f, std::max(1, under->frameCount()) - 1);
      out.push_back(ud);
      continue;
    }
    Fences::Drawable d{depth, sx, sy, a, side, SDL_Color{255, 255, 110, 255}, lit};
    d.frame = f;
    // (on the surface pass: what leaves the water isn't cut to the tank -
    // leaps, flips, spins, dives, sprays and splashes, a breach or a blow,
    // ring, hoop and ball tricks, tail walks, a frenzy, the stage's beach
    // tricks and the sea lion's horn and wave toys; swimming, waving as it
    // swims, resting and eating stay inside)
    if (this->onlySurface) {
      static const char *leaves[] = {"jump", "flip", "flop", "spin", "dive", "spit", "splash", "blowhole", "breach",
                                     "beach", "tail_walk", "ring", "dunk", "hoop", "ball", "frenzy", "horn", "exit"};
      const std::string &n = m.anim;
      for (const char *w : leaves)
        d.airborne = d.airborne || n.find(w) != std::string::npos;
      d.airborne = d.airborne || n == "wave";
    }
    // A big one (its footprint - cFootprintX across, cFootprintY along the
    // way it faces, in half tiles - over a tile): drawn in strips a quarter
    // tile wide, each sorted at the front of its footprint in that strip,
    // as a big building is. Sorted at one point, a T. rex four tiles long
    // came over the fence beside it while it stood or played in place
    const Type &ty = this->typeList[m.type];
    float frontDepth = depth;
    if (!m.boxed && (ty.footX > 2 || ty.footY > 2)) {
      float ux = m.fx, uy = m.fy, len = std::hypot(ux, uy);
      if (len < 0.001f)
        ux = 0, uy = 1, len = 1;
      ux /= len;
      uy /= len;
      float halfAlong = ty.footY / 4.0f, halfAcross = ty.footX / 4.0f;
      const float gz = groundAt(map, m.x, m.y);
      float px[4], py[4], cxs[4], cds[4];
      for (int k = 0; k < 4; k++) {
        float al = (k == 0 || k == 3) ? -halfAlong : halfAlong, ac = (k < 2) ? -halfAcross : halfAcross;
        px[k] = m.x + ux * al - uy * ac;
        py[k] = m.y + uy * al + ux * ac;
        float csy;
        view.worldToScreenF(px[k], py[k], gz, cxs[k], csy, cds[k]);
      }
      // (the footprint's front edge, seen from above: the most forward depth
      // at a screen x - its outline's lower edges)
      auto front = [&](float at) {
        float best = -1e30f;
        for (int k = 0; k < 4; k++) {
          int j = (k + 1) % 4;
          float x0 = cxs[k], x1 = cxs[j];
          if ((at < std::min(x0, x1)) || (at > std::max(x0, x1)))
            continue;
          float t = std::fabs(x1 - x0) > 0.001f ? (at - x0) / (x1 - x0) : 0.0f;
          best = std::max(best, cds[k] + t * (cds[j] - cds[k]));
        }
        return best > -1e29f ? best : depth;
      };
      float L = *std::min_element(cxs, cxs + 4), R = *std::max_element(cxs, cxs + 4);
      float step = std::max(4.0f, view.getTileWidth() / 4.0f);
      int strips = std::max(1, static_cast<int>(std::ceil((R - L) / step)));
      // (the picture's own edges: its head and tail reach past its footprint)
      float picL = L, picR = R, picT = 0, picB = 0;
      if (!a->anchoredBounds(sx, sy, side, picL, picT, picR, picB))
        picL = L, picR = R;
      for (int k = 0; k < strips; k++) {
        float xa = L + k * step, xb = std::min(R, xa + step);
        Fences::Drawable s = d;
        s.depth = front((xa + xb) * 0.5f) - 0.02f;
        s.clip = true;
        s.clipX0 = k == 0 ? std::min(picL, xa) - 1.0f : xa;
        s.clipX1 = k == strips - 1 ? std::max(picR, xb) + 1.0f : xb;
        // (and which side of the fences it's on: Fences::sideOfFences)
        s.animalStrip = true;
        s.wx = m.x;
        s.wy = m.y;
        frontDepth = std::max(frontDepth, s.depth);
        out.push_back(s);
      }
    } else {
      out.push_back(d);
    }
    // Its smiles and frowns, over it (16 frames, 83 ms each)
    const float faceY = sy - this->typeList[m.type].facesYOffset;
    for (const auto &[smile, t] : m.faces)
      if (Animation *fa = smile ? this->smileArt : this->frownArt) {
        Fences::Drawable fd{frontDepth + 0.00005f, sx, faceY, fa, CompassDirection::N,
                            SDL_Color{255, 255, 255, 255}, false};
        fd.frame = std::min(static_cast<int>(t / 83.0f), std::max(1, fa->frameCount()) - 1);
        out.push_back(fd);
      }
    if (m.id == this->selected && this->selectArrow) {
      int w = 0, h = 0;
      a->queryTexture(CompassDirection::S, &w, &h);
      out.push_back({frontDepth + 0.0001f, sx, sy - std::max(36, h), this->selectArrow,
                     CompassDirection::N, SDL_Color{255, 255, 255, 255}, false});
    }
  }
  // The one being adopted: its likeness at the cursor, green where it can
  // go, red where not
  if (this->previewType >= 0) {
    Member preview;preview.type=this->previewType;preview.female=this->previewFemale;
    preview.x=this->previewX;preview.y=this->previewY;
    // (drawn facing as it'll go down: setPlaceItem 0x4f189a / 0x4e00d3's
    // +0x12c into the frame, animSetFrame)
    {const float a=(this->previewFacing&7)*3.14159265f/4.0f;preview.fx=std::sin(a);preview.fy=-std::cos(a);}
    const int tank=this->tankAt?this->tankAt(preview.x,preview.y):-1;
    const bool include=this->onlyTank==-2?tank<0:tank==this->onlyTank;
    preview.swimZ=this->placementDepth(preview.type,tank);
    if(include)if(Animation *a=this->art(preview,"stand")) {
      Animation *under=tank>=0?this->underArt(preview,a):nullptr;
      if(!this->onlySurface || under) {
        if(!this->onlySurface && under)a=under;
        float sx,sy,depth;
        view.worldToScreenF(preview.x,preview.y,groundAt(map,preview.x,preview.y)+preview.swimZ,sx,sy,depth);
        const SDL_Color tint=this->previewFit==Fences::Fit::Ok && !this->previewPoor?SDL_Color{0,255,0,255}:SDL_Color{255,60,60,255};
        out.push_back({depth,sx,sy,a,this->facing(view,preview),tint,true});
      }
    }
  }
}

namespace {
struct PreparedTankAnimals {
  bool ready=false,maskReady=false;
  std::vector<Fences::Drawable> out;
  std::vector<std::pair<float,float>> hull;
};
bool tankAnimalFrame=false,fastTankSprites=true;
std::map<int,PreparedTankAnimals> tankAnimalCache;
}
// Cache only within one World::draw. Direct inspector/tests still prepare afresh.
void beginTankAnimalFrame(bool active) {tankAnimalCache.clear();tankAnimalFrame=active;}
void tankAnimalFastPaths(bool enabled) {fastTankSprites=enabled;}
void Animals::drawInTank(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map, int tank,
                         bool surface) {
  PreparedTankAnimals local;
  auto &prepared=tankAnimalFrame?tankAnimalCache[tank*2+(surface?1:0)]:local;
  auto &out=prepared.out;
  if(!prepared.ready) {
    this->onlyTank=tank;this->onlySurface=surface;
    this->collect(view,map,out);this->onlyTank=-2;this->onlySurface=false;
    std::stable_sort(out.begin(),out.end(),[](const Fences::Drawable &a,const Fences::Drawable &b){return a.depth<b.depth;});
    std::vector<Fences::Drawable> unique;
    for(const auto &d:out) {
      if(!d.art)continue;
      if(d.animalStrip && std::any_of(unique.begin(),unique.end(),[&](const auto &old){
        return old.art==d.art && old.sx==d.sx && old.sy==d.sy && old.side==d.side && old.frame==d.frame;
      }))continue;
      unique.push_back(d);
    }
    out=std::move(unique);prepared.ready=true;
  }
  if(out.empty())return;
  // (a big one comes as strips, for sorting against the fences outside a
  // tank: in one it's drawn once - each strip drawn whole, a whale shark
  // was drawn ten times over itself, its see-through edges darkened)
  // Submerged artwork stays inside the glass, and so does a swimmer's surface
  // layer (its back and head). Only surface art that leaves the water - leaps,
  // sprays, splashes and show tricks (Drawable::airborne) - may rise past the
  // rim.
//
// A body is drawn as its art, which is far bigger than the tile its centre
// stands on - a whale shark's four tiles of fish hang off its anchor on both
// sides. Movement stays free (animals walk and turn as they always did), so a
// big one by a wall does have more body than water; the original simply drew
// all of it, its head and tail over the glass and out across the ground.
//
// The water's screen shape is a hexagon, not a rectangle: the surface's diamond
// with its fronts drawn down from it. One clip rectangle round the lot would
// have to reach from the far surface edge to the floor's near edge, and that
// reaches back over the ground in front of the tank - the very place the
// overflow showed. So the tank is cut into columns, each as tall as its own
// tile's water goes on screen, and the animals are drawn once per column: what
// falls outside every column is not painted at all.
//
// Only this tank's animals are in `out` (onlyTank), so the columns cover them.
// The water's outline on screen: the corners of the surface of every water
// tile, and of the floor beneath them. A tile's own box is NOT the tank's
// shape - the tank's sides slope, so a box round a tile reaches past the water
// and up over the grass beside and behind it, which is the top edge a big
// fish's back still came out of. The outline of all the tiles together is the
// water, so that is the shape the animals are clipped to.
if(!prepared.maskReady) {
std::vector<std::pair<float, float>> rimPts, floorPts;
// (the surface pass too: a swimmer's back and head by a wall stay inside
// it - only what leaves the water, Drawable::airborne, isn't cut)
if (Features::tankClip && fencesRef) {
  const Fences::Exhibit *ex = fencesRef->exhibit(tank);
  if (ex && ex->tank && !ex->tiles.empty()) {
    // (the water's surface and the floor it stands on, both measured from the
    // tank's OWN floor - not from the ground round it, which a raised tank
    // stands above and a sunken one lies under. waterLevel is the water's
    // height over the floor in whole units, the same figure animals' swim
    // depth is measured against, so the clip and the swimmers agree.)
    const float floor = static_cast<float>(ex->floorHeight);
    const float level = ex->level > -1e8f ? ex->level : floor + static_cast<float>(fencesRef->waterLevel(*ex));
    for (const auto &t : ex->tiles) {
      const std::pair<float, float> corners[4] = {{static_cast<float>(t.first), static_cast<float>(t.second)},
                                                  {t.first + 1.0f, static_cast<float>(t.second)},
                                                  {t.first + 1.0f, t.second + 1.0f},
                                                  {static_cast<float>(t.first), t.second + 1.0f}};
      for (const auto &c : corners) {
        float px = 0, py = 0, pd = 0;
        view.worldToScreenF(c.first, c.second, level, px, py, pd);
        rimPts.push_back({px, py});
        view.worldToScreenF(c.first, c.second, floor, px, py, pd);
        floorPts.push_back({px, py});
      }
    }

  }
}
// (the outline round a set of points: a monotone chain, the water's silhouette
// being convex)
auto outlineOf = [](std::vector<std::pair<float, float>> pts) {
  std::vector<std::pair<float, float>> hull;
  if (pts.size() < 3)
    return hull;
  std::sort(pts.begin(), pts.end());
  auto cross = [](const std::pair<float, float> &o, const std::pair<float, float> &a,
                  const std::pair<float, float> &b) {
    return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
  };
  hull.resize(pts.size() * 2 + 1);
  size_t k = 0;
  for (size_t i = 0; i < pts.size(); i++) {
    while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0)
      k--;
    hull[k++] = pts[i];
  }
  for (size_t i = pts.size() - 1, mark = k + 1; i-- > 0;) {
    while (k >= mark && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0)
      k--;
    hull[k++] = pts[i];
  }
  hull.resize(k - 1);
  return hull;
};
// (ONE outline round the surface's corners AND the floor's: a tank is a
// box, and what a box looks like on screen is exactly the outline round
// its eight corners. Two outlines, the surface's and the floor's, were
// taken separately and merged column by column before - and away from the
// middle, toward the left and right ends, the surface's diamond has
// already narrowed while the floor's has not, so the column reached
// higher and lower there than the tank ever does: a shark's tail showed
// on the grass at whichever angle put it out at that end.)
std::vector<std::pair<float, float>> allPts = rimPts;
allPts.insert(allPts.end(), floorPts.begin(), floorPts.end());
prepared.hull=outlineOf(allPts);prepared.maskReady=true;
}
const auto &hullTank=prepared.hull;
// (how high and how low the outline reaches at one screen x)
auto spanAt = [](const std::vector<std::pair<float, float>> &poly, float x, float &lo, float &hi) {
  lo = 1e9f, hi = -1e9f;
  bool any = false;
  for (size_t i = 0; i < poly.size(); i++) {
    const std::pair<float, float> &a = poly[i], &b = poly[(i + 1) % poly.size()];
    if (x < std::min(a.first, b.first) - 0.001f || x > std::max(a.first, b.first) + 0.001f)
      continue;
    const float t = std::fabs(b.first - a.first) > 0.001f ? (x - a.first) / (b.first - a.first) : 0.0f;
    const float y = a.second + t * (b.second - a.second);
    lo = std::min(lo, y);
    hi = std::max(hi, y);
    any = true;
  }
  return any;
};
// (the tank's body, column by column: the outline's own top and bottom at
// that screen x, so a column never reaches past the tank's ends)
float sxScale = 1.0f, syScale = 1.0f;
SDL_GetRenderScale(renderer, &sxScale, &syScale);
const bool hadClip = SDL_RenderClipEnabled(renderer);
SDL_Rect oldClip;
// Parent tank strips use device pixels. Read and restore in that same space.
SDL_SetRenderScale(renderer, 1.0f, 1.0f);
SDL_GetRenderClipRect(renderer, &oldClip);
SDL_SetRenderScale(renderer, sxScale, syScale);
std::vector<SDL_FRect> bounds;
float visibleLeft=1e9f,visibleRight=-1e9f;
for(const auto &d:out) {
  float left,top,right,bottom;
  if(!d.art->anchoredBounds(d.sx,d.sy,d.side,left,top,right,bottom)){left=-1e9f;right=1e9f;top=-1e9f;bottom=1e9f;}
  bounds.push_back({left,top,right-left,bottom-top});
  visibleLeft=std::min(visibleLeft,left);visibleRight=std::max(visibleRight,right);
}
if(hadClip) {
  visibleLeft=std::max(visibleLeft,oldClip.x/sxScale);
  visibleRight=std::min(visibleRight,(oldClip.x+oldClip.w)/sxScale);
}
std::vector<std::pair<std::pair<float, float>, std::pair<float, float>>> columns;
if (!hullTank.empty()) {
  float minX = hullTank[0].first, maxX = hullTank[0].first;
  for (const auto &p : hullTank) {
    minX = std::min(minX, p.first);
    maxX = std::max(maxX, p.first);
  }
  float pixelScaleX=1, pixelScaleY=1;
  SDL_GetRenderScale(renderer,&pixelScaleX,&pixelScaleY);
  const float step = 1.0f/std::max(0.001f,pixelScaleX);
  minX+=std::max(0.0f,std::floor((visibleLeft-minX)/step))*step;
  maxX=std::min(maxX,visibleRight+step);
  for (float x = minX; x < maxX; x += step) {
    float lo = 0, hi = 0;
    if (!spanAt(hullTank, std::min(maxX,x+step*0.5f), lo, hi))
      continue;
    // (each column ends exactly where the next begins: overlapping them draws
    // a see-through thing - a tank floor's shadow - twice in the seam, which
    // darkened it into a band)
    columns.push_back({{x, lo}, {std::min(maxX, x + step), hi}});
  }
}

std::vector<SDL_Rect> strips;
  if (!columns.empty()) {
    // (in device pixels: the scale off while a clip is set, as PlacedObjects.
    // The columns are laid edge to edge to the very pixel - one's right edge
    // is the next one's left - because a column that overlapped its neighbour
    // painted whatever is see-through twice in the seam, and a tank floor's
    // shadow, at a quarter strength, came out as a dark band)
    auto px = [&](float v) { return static_cast<int>(std::lround(v * sxScale)); };
    int left = px(columns.front().first.first);
    for (size_t i = 0; i < columns.size(); i++) {
      const auto &c = columns[i];
      const float t = c.first.second, b = c.second.second;
      const int right = i + 1 == columns.size() ? px(c.second.first) : px(columns[i + 1].first.first);
      SDL_Rect rc = {left, static_cast<int>(std::floor(t * syScale)), right - left,
                     static_cast<int>(std::ceil((b - t) * syScale)) + 1};
      left = right;
      if (rc.w <= 0 || rc.h <= 0) continue;
      if (hadClip) {
        SDL_Rect both;
        if (!SDL_GetRectIntersection(&rc, &oldClip, &both)) continue;
        rc = both;
      }
      strips.push_back(rc);
    }
  }
  if (strips.empty()) {
    // No intersection with an active tank mask means nothing is visible.
    // Drawing everything here would bypass the glass clip at its edges.
    if (!hullTank.empty()) {
      for (Fences::Drawable &d : out)
        if (d.art && d.airborne)
          d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tinted ? &d.tint : nullptr, d.frame);
      return;
    }
    std::vector<const Fences::Drawable *> drawn;
    for (Fences::Drawable &d : out) {
      if (!d.art) continue;
      if(d.animalStrip) {
        bool again=false;
        for(const auto *old:drawn)again |= old->art==d.art && old->sx==d.sx && old->sy==d.sy && old->frame==d.frame;
        if(again)continue;
        drawn.push_back(&d);
      }
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tinted ? &d.tint : nullptr, d.frame);
    }
    return;
  }
  // A sprite wholly inside the strips needs no per-strip redraw: one draw
  // inside the parent clip paints the same pixels. ours (#179): asked of the
  // strips themselves, in device pixels with a pixel's margin round its box -
  // asked of the water's outline instead, a whale shark facing 3 at 1.5x was
  // "inside" where the strips (each column's span taken at its middle, then
  // rounded to whole pixels) cut a few of its edge pixels
  auto covered=[&](const SDL_FRect &box) {
    if(box.w>1e8f || box.h>1e8f)return false;
    int l=static_cast<int>(std::floor(box.x*sxScale))-1,t=static_cast<int>(std::floor(box.y*syScale))-1;
    int r=static_cast<int>(std::ceil((box.x+box.w)*sxScale))+1,b=static_cast<int>(std::ceil((box.y+box.h)*syScale))+1;
    if(hadClip) {
      l=std::max(l,oldClip.x);t=std::max(t,oldClip.y);
      r=std::min(r,oldClip.x+oldClip.w);b=std::min(b,oldClip.y+oldClip.h);
    }
    if(l>=r || t>=b)return true; // (none of it shows either way)
    int x=l;
    for(const auto &rc:strips) {
      if(rc.x+rc.w<=x)continue;
      if(rc.x>x || rc.y>t || rc.y+rc.h<b)return false; // (a gap, or cut above or below)
      x=rc.x+rc.w;
      if(x>=r)return true;
    }
    return false;
  };
  for(size_t i=0;i<out.size();++i) {
    auto &d=out[i];const auto &box=bounds[i];
    const float right=box.x+box.w,bottom=box.y+box.h;
    SDL_SetRenderScale(renderer,1,1);
    if(d.airborne || (fastTankSprites && covered(box))) {
      SDL_SetRenderClipRect(renderer,hadClip?&oldClip:nullptr);
      SDL_SetRenderScale(renderer,sxScale,syScale);
      d.art->drawAnchored(renderer,d.sx,d.sy,d.side,d.tinted?&d.tint:nullptr,d.frame);
    } else for(const auto &rc:strips) {
      if(rc.x+rc.w<=box.x*sxScale || rc.x>=right*sxScale || rc.y+rc.h<=box.y*syScale || rc.y>=bottom*syScale)continue;
      SDL_SetRenderScale(renderer,1,1);SDL_SetRenderClipRect(renderer,&rc);
      SDL_SetRenderScale(renderer,sxScale,syScale);
      d.art->drawAnchored(renderer,d.sx,d.sy,d.side,d.tinted?&d.tint:nullptr,d.frame);
    }
  }
  SDL_SetRenderScale(renderer,1,1);
  SDL_SetRenderClipRect(renderer, hadClip ? &oldClip : nullptr);
  SDL_SetRenderScale(renderer, sxScale, syScale);
}

int Animals::pick(float px, float py, const WorldRenderer &view, const WorldMap &map) const {
  int found = -1;
  float best = 1e9f;
  for (const Member &m : this->list) {
    if (m.id == this->carried)
      continue;
    float sx, sy, depth;
    view.worldToScreenF(m.x, m.y, groundAt(map, m.x, m.y) + (this->tankAt && this->tankAt(m.x, m.y) >= 0 ? m.swimZ : 0.0f), sx, sy, depth);
    int w = 24, h = 24;
    if (Animation *a = this->art(m, m.anim))
      a->queryTexture(facing(view, m), &w, &h);
    float hw = std::max(8.0f, w * 0.4f), top = std::max(12.0f, h * 0.9f);
    if (px < sx - hw || px > sx + hw || py < sy - top || py > sy + 6)
      continue;
    float d = std::fabs(px - sx) + std::fabs(py - (sy - top * 0.5f));
    if (d < best) {
      best = d;
      found = m.id;
    }
  }
  return found;
}

// ----------------------------------------------------------------------------
// The exhibit as an animal rates it (zoo.exe 0x444827, scores at 0x415dd7,
// read in the disassembly): Exhibit Suitability is
//   0.3 terrain + 0.2 objects + 0.1 (trees + rocks + hills + shelters + toys)
// shown rounded up, never under 0. Each part is out of 100:
// - terrain: for each terrain type it wants, the share of the exhibit up to
//   what it wants; a type it dislikes (negative) costs that much if any is
//   there at all (path tiles count for neither);
// - objects: each object's value (cSuitableObjects of its habitat and of its
//   name id), shared out over the quarter tiles it stands on, summed over
//   the exhibit, divided by 100 x (a fortieth of its tiles, at least 1);
//   below 0 it counts fifty times over;
// - trees, rocks, hills: how far the exhibit's foliage tiles, rocky
//   quarter tiles and slopes are from its preferences (cTreePref,
//   cRockPref, cElevationPref), 8, 6 and 1 points off for each percent;
// - shelters and toys: too few of them for the animals that need them, or
//   too many going unused, cost points.
// ----------------------------------------------------------------------------
const Animals::ObjectInfo &Animals::infoOf(const std::string &file) const {
  auto it = this->objectInfo.find(file);
  if (it != this->objectInfo.end())
    return it->second;
  ObjectInfo o;
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(file)) {
      const std::string ints = "characteristics/integers";
      o.nameId = ai->getInt(ints, "cnameid", 0);
      o.rubbleable = ai->getInt(ints, "crubbleable", 1) != 0;
      o.habitat = ai->getInt(ints, "chabitat", 0);
      o.fx = std::max(1, ai->getInt(ints, "cfootprintx", 1));
      o.fy = std::max(1, ai->getInt(ints, "cfootprinty", 1));
      o.foliage = ai->getInt(ints, "cfoliage", 0) != 0;
      o.rock = ai->getInt(ints, "crock", 0) != 0;
      o.stink = ai->getInt(ints, "cstink", 0) > 0;
      o.shelter = ai->getInt(ints, "cshelter", 0) != 0;
      o.capacity = ai->getInt(ints, "ccapacity", 0);
      o.toy = ai->getInt(ints, "ctoysatisfaction", 0);
      o.building = file.find("/building/") != std::string::npos;
      o.adultChange = static_cast<float>(ai->getInt(ints, "cadultchange", 0));
      o.childChange = static_cast<float>(ai->getInt(ints, "cchildchange", 0));
      o.hungerChange = static_cast<float>(ai->getInt(ints, "chungerchange", 0));
      o.energyChange = static_cast<float>(ai->getInt(ints, "cenergychange", 0));
      o.timeInside = ai->getInt(ints, "ctimeinside", 10);
      o.hideUser = ai->getInt(ints, "chideuser", 0) != 0;
      o.hideBuilding = ai->getInt(ints, "chidebuilding", 0) != 0;
      // (its [slots]: zoo.exe ZTBuildingType::loadConfig 0x4c0b4d - id
      // 0x4c08dc, isreturnslot 0x4c09ce, filter 0x4c0aea, entrpos
      // 0x4c0b4d, capacity 0x4c0cd8)
      for (const std::string &name : ai->getList("slots", "name")) {
        ObjectInfo::Slot s;
        s.ids = ai->getIntList(name, "id");
        s.capacity = ai->getInt(name, "capacity", 0);
        s.filter = lower(ai->get(name, "filter"));
        s.isReturn = ai->getInt(name, "isreturnslot", 0) != 0;
        std::vector<int> e = ai->getIntList(name, "entrpos");
        for (size_t k = 0; k + 1 < e.size(); k += 2)
          s.entrances.push_back({e[k], e[k + 1]});
        o.slots.push_back(s);
      }
      delete ai;
    }
  // [Satisfies] animalrest (shelters, toys): read from the file's text
  if (this->rm) {
    int size = 0;
    if (void *data = this->rm->getFileBytes(file, &size)) {
      std::string text = lower(std::string(static_cast<const char *>(data), static_cast<size_t>(size)));
      free(data);
      size_t at = text.find("[satisfies]");
      if (at != std::string::npos) {
        size_t end = text.find('[', at + 1);
        o.rest = text.substr(at, end == std::string::npos ? std::string::npos : end - at).find("animalrest") !=
                 std::string::npos;
      }
    }
  }
  return this->objectInfo[file] = o;
}

void Animals::markDirty(int exhibit) {
  int root=exhibit;
  if(exhibit>=0 && mapRef && fencesRef)root=fencesRef->habitatRoot(exhibit,*mapRef);
  for (auto &[id, e] : this->evaluations)
    if (exhibit < 0 || id == exhibit || id==root ||
        (mapRef && fencesRef && fencesRef->habitatRoot(id,*mapRef)==root))
      e.dirty = true;
}

void Animals::evaluate(int exhibit, const WorldMap &map, const Fences &fences) {
  ZT_FN("", "Rate an exhibit for the animals in it");
  ZT_WHO("exhibit", exhibit);
  Evaluation &ev = this->evaluations[exhibit];
  ev.dirty = false;
  ev.timerMs = static_cast<float>(std::uniform_int_distribution<int>(0, 199)(this->rng));
  ev.kinds.clear();
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex || ex->tiles.empty())
    return;
  Fences::Exhibit joined=*ex;
  if(!ex->tank) {
    for(int id:fences.habitatGroup(exhibit,map))if(const auto *part=fences.exhibit(id))
      joined.tiles.insert(part->tiles.begin(),part->tiles.end());
    ex=&joined;
  }
  const int N = static_cast<int>(ex->tiles.size());
  ev.tiles = static_cast<int>(fences.exhibit(exhibit)->tiles.size());
  // What stands in it, quarter tile by quarter tile
  struct Placed {
    const ObjectInfo *info;
    float area;
  };
  std::map<std::pair<int, int>, std::vector<int>> slotsOfTile; // tile -> objects, a slot each
  std::vector<Placed> placed;
  auto put = [&](const ObjectInfo &info, float cx, float cy, int facing) {
    int fx = info.fx, fy = info.fy;
    if ((facing & 6) == 2 || (facing & 6) == 6)
      std::swap(fx, fy);
    int index = static_cast<int>(placed.size());
    placed.push_back({&info, static_cast<float>(info.fx * info.fy)});
    // Its quarter tiles (half-tile cells), round its middle
    float left = cx - fx / 4.0f, top = cy - fy / 4.0f;
    for (int j = 0; j < fy; j++)
      for (int i = 0; i < fx; i++) {
        float qx = left + (i + 0.5f) * 0.5f, qy = top + (j + 0.5f) * 0.5f;
        int tx = static_cast<int>(std::floor(qx)), ty = static_cast<int>(std::floor(qy));
        if (ex->tiles.count({tx, ty}))
          slotsOfTile[{tx, ty}].push_back(index);
      }
  };
  if (this->objects)
    for (const PlacedObjects::Object &o : this->objects->objects())
      if (!o.fence)
        put(infoOf(PlacedObjects::fileOf(o)), o.x, o.y, o.facing);
  // Its poo (scenery/other/poo.ai)
  ev.dung = 0;
  if (this->items)
    for (const ZooItems::Item &i : this->items->items())
      if (i.kind == ZooItems::Kind::Dung && i.exhibit == exhibit) {
        put(infoOf("scenery/other/poo.ai"), i.x, i.y, 0);
        ev.dung++;
      }
  // Its shelters and toys, as the tile scan finds them
  std::vector<int> buildings;
  for (auto &[tile, slots] : slotsOfTile)
    for (int k : slots) {
      const ObjectInfo &o = *placed[k].info;
      if (o.building && (o.shelter || o.toy > 0) &&
          std::find(buildings.begin(), buildings.end(), k) == buildings.end())
        buildings.push_back(k);
    }
  // The animals in it, and their kinds
  std::vector<const Member *> here;
  std::vector<int> kinds;
  for (const Member &m : this->list)
    if (m.exhibit == exhibit && m.id != this->carried) {
      here.push_back(&m);
      if (std::find(kinds.begin(), kinds.end(), m.type) == kinds.end())
        kinds.push_back(m.type);
    }
  auto value = [&](const Type &t, const ObjectInfo &o) {
    auto at = [&](int key) {
      auto it = t.suitable.find(key);
      return it == t.suitable.end() ? 0 : it->second;
    };
    return at(o.habitat) + at(o.nameId);
  };
  // Shelters and toys are claimed animal by animal (as they come)
  std::vector<int> shelterRoom(buildings.size()), toyRoom(buildings.size());
  for (size_t i = 0; i < buildings.size(); i++) {
    const ObjectInfo &o = *placed[buildings[i]].info;
    shelterRoom[i] = o.shelter ? o.capacity : 0;
    toyRoom[i] = std::max(0, o.toy);
  }
  std::map<int, float> extraObjects;
  std::map<int, int> noShelter, noToy;
  for (const Member *m : here) {
    const Type &t = this->typeList[m->type];
    for (int pass = 0; pass < 2; pass++) {
      bool needs = pass == 0 ? t.needShelter : t.needToys;
      std::vector<int> &room = pass == 0 ? shelterRoom : toyRoom;
      bool claimed = false;
      for (size_t i = 0; i < buildings.size(); i++) {
        const Placed &b = placed[buildings[i]];
        int v = value(t, *b.info);
        extraObjects[m->type] += std::trunc(v * 100.0f / b.area);
        if (v >= 0 && needs && room[i] > 0) {
          room[i]--;
          claimed = true;
          break;
        }
      }
      if (!claimed && needs)
        (pass == 0 ? noShelter : noToy)[m->type]++;
    }
  }
  int totalNoShelter = 0;
  for (auto &[k, n] : noShelter)
    totalNoShelter += n;
  for (int kind : kinds) {
    const Type &t = this->typeList[kind];
    Rating r;
    float objectSum = extraObjects[kind];
    int foliageTiles = 0, rockSlots = 0, pooTiles = 0;
    float hills = 0;
    std::map<int, int> disliked;
    for (auto [tx, ty] : ex->tiles) {
      const MapTile *tile = map.getTile(tx, ty);
      if (!tile)
        continue;
      int terrain=tile->terrainType;
      if(const auto *part=fences.exhibit(fences.exhibitAt(tx,ty));part && part->tank)
        terrain=part->salt?10:9;
      if (!map.isPath(tx, ty) && terrain < 18)
        r.count[terrain]++;
      bool foliage = false, poo = false;
      auto slots = slotsOfTile.find({tx, ty});
      if (slots != slotsOfTile.end())
        for (int k : slots->second) {
          const Placed &o = placed[k];
          int v = value(t, *o.info);
          objectSum += std::trunc(v * 100.0f / o.area);
          if (o.info->stink)
            poo = true;
          if (v >= 0) {
            if (o.info->rock)
              rockSlots++;
            if (o.info->foliage)
              foliage = true;
          } else if (o.info->nameId) {
            disliked[o.info->nameId] = v;
          }
        }
      foliageTiles += foliage ? 1 : 0;
      pooTiles += poo ? 1 : 0;
      // Its slope, and (not confirmed) each cliff edge to more of the exhibit
      int lo = 1 << 30, hi = -(1 << 30);
      for (int c = 0; c < 4; c++) {
        lo = std::min(lo, static_cast<int>(tile->cornerHeight[c]));
        hi = std::max(hi, static_cast<int>(tile->cornerHeight[c]));
      }
      hills += static_cast<float>(hi - lo);
      const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto &dd : d) {
        int nx = tx + dd[0], ny = ty + dd[1];
        Fences::Edge e = dd[0] ? Fences::Edge{false, std::max(tx, nx), ty}
                               : Fences::Edge{true, tx, std::max(ty, ny)};
        if (ex->tiles.count({nx, ny}) && !fences.at(e) && !canStep(tx, ty, nx, ny, map, fences))
          hills += 1;
      }
    }
    // Terrain
    for (int k = 0; k < 18; k++) {
      int p = t.compatTerrain[k];
      r.pct[k] = r.count[k] * 100.0f / N;
      if (p < 0) {
        if (r.count[k] > 0)
          r.terrain += p;
      } else {
        r.terrain += std::min(static_cast<float>(p), r.pct[k]);
      }
    }
    r.terrain = std::min(r.terrain, 100.0f);
    // Objects
    r.objects = std::min(objectSum / (100.0f * std::max(1.0f, N * 0.025f)), 100.0f);
    if (r.objects < 0)
      r.objects *= 50;
    // Trees, rocks, hills
    r.foliage = foliageTiles * 100.0f / N;
    r.rockShare = rockSlots * 100.0f / (4 * N);
    r.hills = hills * 50.0f / N;
    r.trees = std::min(100 - 8 * std::clamp(std::fabs(r.foliage - t.treePref), 0.0f, 50.0f), 100.0f);
    r.rocks = std::min(100 - 6 * std::clamp(std::fabs(r.rockShare - t.rockPref), 0.0f, 50.0f), 100.0f);
    r.elevation = std::min(100 - std::clamp(std::fabs(r.hills - t.elevationPref), 0.0f, 50.0f), 100.0f);
    // Shelters and toys
    r.noShelter = noShelter[kind];
    r.noToy = noToy[kind];
    for (size_t i = 0; i < buildings.size(); i++) {
      const ObjectInfo &o = *placed[buildings[i]].info;
      if (value(t, o) < 0)
        continue;
      if (o.shelter && o.capacity > 0 && shelterRoom[i] == o.capacity)
        r.unusedShelters++;
      if (o.toy > 0 && toyRoom[i] == o.toy)
        r.unusedToys++;
    }
    float pen = 0;
    if (r.noShelter > 0) {
      if (r.noShelter >= 4 && r.noShelter <= 6)
        pen = 15.0f * r.noShelter;
      else if (r.noShelter > 6)
        pen = 100.0f * totalNoShelter - 600;
    } else if (r.unusedShelters >= 1) {
      pen = r.unusedShelters > 2 ? 50.0f * r.unusedShelters + 100 : 0;
      if (r.unusedShelters > 3)
        pen *= r.unusedShelters - 3;
    }
    r.shelters = std::min(100 - pen, 100.0f);
    pen = 0;
    if (r.noToy > 0) {
      if (r.noToy > 3)
        pen = 15.0f * r.noToy;
    } else if (r.unusedToys >= 2) {
      pen = r.unusedToys > 2 ? 50.0f * r.unusedToys + 100 : 0;
      if (r.unusedToys > 2)
        pen *= r.unusedToys - 2;
    }
    r.toys = std::min(100 - pen, 100.0f);
    r.score = 0.3f * r.terrain + 0.2f * r.objects +
              0.1f * (r.trees + r.rocks + r.elevation + r.shelters + r.toys);
    // A tank (zoo.exe 0x444827 from 0x445e17; summed at 0x415e69): no
    // terrain, its water instead -
    // - depth: the water's level (tank +0x188) against cDepthMin..Max, 20
    //   points off a unit out (up to 30), red from 50 off;
    // - cleanliness: the water's purity (tank +0x1a8);
    // - salinity (0x4936df): a salt tank for one liking salt water terrain
    //   at least as much as fresh, a fresh one for the rest - 100, else -400;
    // 0.15 depth + 0.15 cleanliness + 0.1 salinity + 0.1 each of objects,
    // trees, rocks, hills, shelters and toys. One that needs land too
    // (cUnderwater, not cOnlyUnderwater: type slot 0xcc) with none: 50 off
    // (0x498b93), its hills taken as right (0x445f8b)
    if (ex->tank) {
      r.tank = true;
      r.clean = ex->purity;
      const int level = fences.waterLevel(*ex);
      if (t.depthMin != t.depthMax) {
        float off = 0;
        if (level < t.depthMin) {
          off = static_cast<float>(t.depthMin - level);
          r.shallow = true;
        } else if (level > t.depthMax) {
          off = static_cast<float>(level - t.depthMax);
          r.deep = true;
        }
        off = std::clamp(off, 0.0f, 30.0f) * 20.0f;
        r.depthRed = off >= 50.0f;
        r.depth = std::min(100.0f - off, 100.0f);
      }
      const bool salty = t.compatTerrain[10] >= t.compatTerrain[9];
      r.saltWrong = ex->salt ? !salty : salty;
      r.salinity = r.saltWrong ? -400.0f : 100.0f;
      r.noLand = t.underwater && !t.onlyUnderwater && fences.habitatRoot(exhibit,map)==exhibit;
      if (r.noLand)
        r.elevation = 100.0f;
      r.score = 0.15f * r.depth + 0.15f * r.clean + 0.1f * r.salinity +
                0.1f * (r.objects + r.trees + r.rocks + r.elevation + r.shelters + r.toys) - (r.noLand ? 50.0f : 0.0f);
      ZT_STEP("0x415e69", "A tank's rating", {"species", t.key}, {"water level", level}, {"cDepthMin", t.depthMin},
              {"cDepthMax", t.depthMax}, {"depth", r.depth}, {"cleanliness", r.clean}, {"salinity", r.salinity},
              {"needs land", r.noLand ? 1 : 0}, {"score", r.score});
    }
    // The other kinds of animal there (each kind once): what it thinks of
    // them, their genus and their family
    for (int other : kinds) {
      if (other == kind)
        continue;
      const Type &o = this->typeList[other];
      for (int key : {o.nameId, o.genus, o.family}) {
        auto it = t.compatible.find(key);
        if (it != t.compatible.end())
          r.compat += it->second;
      }
    }
    r.poo = static_cast<float>(pooTiles) / N;
    for (auto [id, v] : disliked)
      r.dislikes.push_back({id, v});
    ZT_STEP("", "Its rating of the exhibit (suitability)", {"species", t.key}, {"score", r.score},
            {"terrain (x0.3)", r.terrain}, {"objects (x0.2)", r.objects}, {"trees", r.trees}, {"rocks", r.rocks},
            {"elevation", r.elevation}, {"shelters", r.shelters}, {"toys", r.toys}, {"compat", r.compat});
    ev.kinds[kind] = r;
  }
}

const Animals::Rating *Animals::rating(const Member &m, const WorldMap &map, const Fences &fences) {
  if (m.exhibit < 0)
    return nullptr;
  Evaluation &ev = this->evaluations[m.exhibit];
  if (ev.dirty || !ev.kinds.count(m.type))
    evaluate(m.exhibit, map, fences);
  auto it = ev.kinds.find(m.type);
  return it == ev.kinds.end() ? nullptr : &it->second;
}

// zoo.exe animalExhibitRating 0x411afd: its habitat's score for it
// (exhibitSuitabilityScores 0x415dd7(this, 1)), floored at 0 (0x630d5c) and
// rounded up (0x415f2e under control word 0x1b3f, round up); 0 for one not
// rated
int Animals::suitability(int id, const WorldMap &map, const Fences &fences) {
  const Member *m = this->member(id);
  const Rating *r = m ? this->rating(*m, map, fences) : nullptr;
  return r ? static_cast<int>(std::ceil(std::max(0.0f, r->score))) : 0;
}

int Animals::exhibitSuitability(int exhibit) const {
  return std::max(0, this->exhibitSuitability(exhibit, nullptr));
}

int Animals::exhibitSuitability(int exhibit, const std::function<bool(const Member &)> &keep) const {
  auto ev = this->evaluations.find(exhibit);
  if (ev == this->evaluations.end())
    return keep ? -1 : 0;
  float sum = 0;
  int n = 0;
  for (const Member &m : this->list) {
    if (m.exhibit != exhibit || (keep && !keep(m)))
      continue;
    auto k = ev->second.kinds.find(m.type);
    sum += k == ev->second.kinds.end() ? 0.0f : std::ceil(std::max(0.0f, k->second.score));
    n++;
  }
  return n ? static_cast<int>(sum / n) : keep ? -1 : 0;
}

// zoo.exe 0x4386a9 (animalCheckCrowding) counts with its habitat's joined
// ones too (0x4388d7, 0x43c376 - habitatAnimalsOfSpecies 0x43c248: the
// habitat's animals, +0x6c, of the species, its rating redone first if
// stale - through animalCountOwnSpecies 0x43c175,
// its species' adults, type +0x1ec - 0x438e14 called with 1: a land exhibit and
// the tanks it shores on), and measures the room as their tiles together
// (0x44097c(1))
void Animals::company(const Member &m, int &kind, int &kindAdults, int &adults) const {
  int tiles = 0;
  company(m, kind, kindAdults, adults, tiles);
}

void Animals::company(const Member &m, int &kind, int &kindAdults, int &adults, int &tiles) const {
  kind = kindAdults = adults = tiles = 0;
  std::set<int> group{m.exhibit};
  if (m.exhibit >= 0 && this->mapRef && this->fencesRef)
    group = this->fencesRef->habitatGroup(m.exhibit, *this->mapRef);
  if (this->fencesRef)
    for (int id : group)
      if (const Fences::Exhibit *ex = this->fencesRef->exhibit(id))
        tiles += static_cast<int>(ex->tiles.size());
  for (const Member &o : this->list) {
    if (!group.count(o.exhibit) || o.id == this->carried)
      continue;
    if (o.type == m.type) {
      kind++;
      kindAdults += o.baby ? 0 : 1;
    }
    adults += o.baby ? 0 : 1;
  }
}

// Too many of its own kind's adults for the room (zoo.exe 0x4386a9 with
// animalSpaceRatio 0x43c1be): more than cNumberAnimalsMax for every
// cHabitatSize tiles, the tiles over cHabitatSize as a fraction, never under
// 1 (0x635490 = 1.0)
bool Animals::crowdedBySpecies(const Type &t, int kindAdults, int tiles) {
  const float room = std::max(1.0f, static_cast<float>(tiles) / static_cast<float>(t.habitatSize));
  return static_cast<float>(kindAdults) > room * static_cast<float>(t.numberMax);
}

// Made an egg (zoo.exe 0x5a5283): young, still, its egg art; cTimeToHatch
// ticks to go - 5 once a guest has been named "Eggman" (0x63cfa1, at
// 0x5a5318 -> 0x5dc046) - and health just under what hatching takes, plus
// those ticks (cMinHatchHits, or 60% of cMaxHits - cMinHits), no more than
// cMaxHits
void Animals::becomeEgg(Member &m) {
  const Type &t = this->typeList[m.type];
  m.egg = true;
  m.baby = true;
  m.grow = 0;
  m.hatchIn = RenameCheats::flags().eggman ? Original::Cheat::eggmanHatchTicks : t.timeToHatch;
  int need = t.minHatchHits ? t.minHatchHits : (t.maxHits - t.minHits) * 60 / 100;
  m.health = static_cast<float>(std::min(need - 1 + m.hatchIn, t.maxHits));
  m.stack.clear();
  m.path.clear();
  m.pathAt = 0;
  m.stepping = false;
  play(m, "egg_idle", Member::Mode::Loop, 3600.0f);
  ZT_STEP("0x5a5283", "Laid as an egg", {"cTimeToHatch", t.timeToHatch}, {"health", m.health},
          {"to hatch it needs", need});
}

// An egg's tick (zoo.exe 0x437860): healthy enough (cMinHatchHits, or 60%
// of cMaxHits - cMinHits) and in an exhibit, a tick nearer hatching; when
// it's time, its bHatch (the egg cracks, then fHatch: a baby)
void Animals::eggTick(Member &m) {
  const Type &t = this->typeList[m.type];
  // (eaten: the shell's gone when its countdown's out - zoo.exe 0x437860)
  if (m.eaten) {
    if (m.hatchIn-- < 1 && std::find(this->kills.begin(), this->kills.end(), m.id) == this->kills.end())
      this->kills.push_back(m.id);
    return;
  }
  int need = t.minHatchHits ? t.minHatchHits : (t.maxHits - t.minHits) * 60 / 100;
  if (m.health < need)
    return;
  // (the countdown's last tick: 0x437860 hatches when it was under 1)
  if (m.hatchIn-- >= 1 || m.exhibit < 0 || !m.stack.empty())
    return;
  ZT_FN("0x437860", "The egg hatches");
  ZT_STEP("0x437860", "Time to hatch", {"health", m.health}, {"it needs", need});
  // (its egg_idle ended: bHatch starts at once - zoo.exe 0x613ca6 starts it
  // through 0x4a7e94, forced)
  m.stepping = false;
  if (this->set(m, "bhatch"))
    pushSet(m, "bhatch");
  else
    m.egg = false; // ours: no bHatch to play, it's out at once
}

// Testing a live electric fence (zoo.exe 0x438ebc, every cZapCheck): while it has
// some of its cTestFenceChance left, a cTestFenceChance in 100 to look
// cTestFenceOffset tiles north, east, south and west - still in its exhibit -
// for a live electric fence on that tile's far side (0x4a3383, 0x42ec8b);
// found, it stops what it's doing, faces it and plays bTestFence (the zap),
// cZapHappinessChange, and uses up cTestFenceChanceDrop of its chance: under
// cMinTestFenceChance there's none left, it's learnt (0x59c4ba)
void Animals::testFence(Member &m, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  // (a young one whose [y/...] has no chance doesn't test: its chance is
  // taken up once it's grown)
  const int chance = t.forAge(m.baby).testFenceChance;
  if (m.fenceTests < 0 && chance > 0)
    m.fenceTests = chance;
  if (--m.zapCheck >= 0)
    return;
  m.zapCheck = kZapCheck - 1;
  if (m.fenceTests <= 0 || chance <= 0 || m.exhibit < 0 || m.escaped || m.boxed || m.tranquilised ||
      m.dying || m.sleeping || m.building >= 0 || m.huntAnimal >= 0 || m.fleeFrom >= 0 || m.caughtBy >= 0 ||
      m.crossing.x >= 0 || !this->set(m, "btestfence"))
    return;
  ZT_FN("0x438ebc", "Tests an electric fence");
  int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
  if (roll >= chance)
    return;
  static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}; // N E S W (zoo.exe 0, 2, 4, 6)
  static const char *names[4] = {"N", "E", "S", "W"};
  int tx = static_cast<int>(std::floor(m.x)), ty = static_cast<int>(std::floor(m.y));
  int o = std::max(1, t.testFenceOffset);
  for (int d = 0; d < 4; d++) {
    int cx = tx + dirs[d][0] * o, cy = ty + dirs[d][1] * o;
    if (fences.exhibitAt(cx, cy) != m.exhibit)
      continue;
    Fences::Edge e = dirs[d][0] ? Fences::Edge{false, dirs[d][0] > 0 ? cx + 1 : cx, cy}
                                : Fences::Edge{true, cx, dirs[d][1] > 0 ? cy + 1 : cy};
    if (!fences.liveElectric(e))
      continue;
    m.stack.clear();
    m.path.clear();
    m.pathAt = 0;
    m.stepping = false;
    faceToward(m, m.x + dirs[d][0], m.y + dirs[d][1]);
    pushSet(m, "btestfence");
    int had = m.fenceTests;
    m.fenceTests -= t.testFenceChanceDrop;
    if (m.fenceTests < t.minTestFenceChance)
      m.fenceTests = 0;
    float was = m.pending;
    m.pending += t.zapHappinessChange;
    ZT_STEP("0x438ebc", "Zapped testing the fence", {"facing", names[d]}, {"roll", roll},
            {"cTestFenceChance", chance}, {"chance left", had, m.fenceTests},
            {"cZapHappinessChange", t.zapHappinessChange}, {"pending", was, m.pending});
    return;
  }
}

// The guests watching it (zoo.exe 0x4479fd, each viewing area counted in
// turn; its three totals zeroed first, in the rating 0x444827 at 0x446dd7):
// from each of its exhibit's viewing areas that sees the tile it's on, the
// area's guests (0x447957: any guest on its viewing tiles, whatever it's
// doing), a tenth less (truncated) for every tile to one of the area's
// viewing tiles picked at random ((int) of the straight distance,
// 0x41180a), at least 1 if there are any; and the same added to the side
// they're on (0x411654), +0x36c if that way is 1-3 (else taken off) and
// +0x370 if 7, 0 or 1 (else taken off).
// measured: the original counts each exhibit's again every 2-4 s of play
// (ExavatorZoo.zoo in Play original, the animals' +0x368 polled); ours
// every status tick (1 s)
void Animals::countGuestCrowd(Member &m, const Fences &fences) {
  m.guestCrowd = m.guestCrowdX = m.guestCrowdY = 0;
  if (!this->viewAreas || m.egg)
    return;
  int x = static_cast<int>(std::floor(m.x)), y = static_cast<int>(std::floor(m.y));
  int ex = fences.exhibitAt(x, y);
  if (ex < 0)
    return;
  for (const ViewArea &area : this->viewAreas(ex)) {
    if (area.viewing.empty() || std::find(area.seen.begin(), area.seen.end(), std::make_pair(x, y)) == area.seen.end())
      continue;
    auto [vx, vy] = area.viewing[std::uniform_int_distribution<size_t>(0, area.viewing.size() - 1)(this->rng)];
    int dx = vx - x, dy = vy - y;
    int tiles = static_cast<int>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
    int w = area.guests;
    for (int i = 0; i < tiles; i++)
      w = static_cast<int>(w * Original::guestCrowdFalloff);
    if (area.guests != 0 && w == 0)
      w = 1;
    m.guestCrowd += w;
    int dir = direction8(dx, dy);
    m.guestCrowdX += dir >= 1 && dir <= 3 ? w : -w;
    m.guestCrowdY += dir == 7 || dir == 0 || dir == 1 ? w : -w;
  }
}

// zoo.exe 0x4116a9: the way from one tile to another by their difference,
// 0 to 7 round from -y (-1: the same tile)
int Animals::direction8(int dx, int dy) {
  if (dx == 0)
    return dy < 0 ? 0 : dy > 0 ? 4 : -1;
  if (dx > 0)
    return dy < 0 ? 1 : dy == 0 ? 2 : 3;
  return dy > 0 ? 5 : dy == 0 ? 6 : 7;
}

// zoo.exe 0x43e511: that way, or either way next to it
bool Animals::nearDirection(int want, int dir) {
  int diff = (dir - want + 8) % 8;
  return dir >= 0 && want >= 0 && want < 8 && (diff == 0 || diff == 1 || diff == 7);
}

// The tank water check (zoo.exe 0x438a37, every cMurkyWaterCheck ticks, its
// countdown at animal +0x2fc): in a tank, the water's purity (tank +0x1a8)
// under its cExtremelyMurky / cVeryMurky / cMurkyWaterThreshold, that
// band's happiness and health change; then, for one not at its show (slot
// 0x22c), the water's level (tank +0x188) outside cDepthMin..cDepthMax (when
// they differ), cDepthChange; and the wrong water (0x4936df, as the rating's
// saltWrong), cSalinityChange and cSalinityHealthChange. The depth one is
// also skipped by the original when the tank's +4 is set - not worked out;
// not asked here
void Animals::murkyWaterTick(Member &m, const WorldMap &map, const Fences &fences) {
  (void)map;
  if (m.murkyCheck-- != 0)
    return;
  m.murkyCheck = kMurkyWaterCheck - 1;
  const Fences::Exhibit *ex = m.exhibit >= 0 ? fences.exhibit(m.exhibit) : nullptr;
  if (!ex || !ex->tank)
    return;
  ZT_FN("0x438a37", "The tank water check");
  const Type &t = this->typeList[m.type];
  const float pending = m.pending, health = m.health;
  const int purity = static_cast<int>(ex->purity);
  for (int band = 2; band >= 0; band--)
    if (purity < t.murkyThreshold[band]) {
      m.pending += t.murkyChange[band];
      m.health += t.murkyHealthChange[band];
      break;
    }
  if (m.showTank < 0 && t.depthMin != t.depthMax) {
    const int level = fences.waterLevel(*ex);
    if (level < t.depthMin || t.depthMax < level)
      m.pending += t.depthChange;
  }
  const bool salty = t.compatTerrain[10] >= t.compatTerrain[9];
  if (ex->salt ? !salty : salty) {
    m.pending += t.salinityChange;
    m.health += t.salinityHealthChange;
  }
  m.health = std::clamp(m.health, 0.0f, static_cast<float>(t.maxHits));
  ZT_STEP("0x438a37", "The tank's water", {"purity", purity}, {"water level", fences.waterLevel(*ex)},
          {"pending", pending, m.pending}, {"health", health, m.health});
}

// ----------------------------------------------------------------------------
// Happiness (zoo.exe 0x4378df, 0x437620): every tick what the last checks
// found is added (kept to -100..100), then the checks that are due add to
// the next:
// - its exhibit (every 20): suitability at least cHabitatPreference,
//   cHappyHabitatChange; at least cPctHabitat percent of it,
//   cAngryHabitatChange; less, cVeryAngryHabitatChange; and a tenth of what
//   it thinks of the other kinds of animal there (-500 to 150 of it);
// - captivity (every 20): less cCaptivity;
// - company (every 30): too few of its kind, cNumberMinChange; else too
//   many for the space, cNumberMaxChange; too little room for all the
//   adults, cAllCrowdedChange; more guests watching it than its cCrowd,
//   cCrowdHappinessChange;
// - the others (every 40): one of them very angry, cOtherAnimalAngryChange;
// - an electric fence (every 5): its test, cZapHappinessChange;
// - hunger (every 30): cHungerIncrement hungrier; hungry with no food,
//   cNoFoodChange.
// ----------------------------------------------------------------------------
// Air (zoo.exe 0x4389f7, each status tick): its countdown down one; got to
// 0 under the water (water state 4), cBreathIncrement more breath, kept
// 0..100, and the countdown again. At 0 anywhere else, the original's
// countdown runs on below 0 and never comes round again - its breath stays
// as it is from then on (0x438a1d tests for 0 only; copied as it is).
// measured: in the running original 2026-11-16 every dolphin, orca, beluga
// and porpoise of a loaded zoo had its countdown (+0x364) below 0, -6 to
// -200 and one less each second, its breath stuck at 0-30, and none went
// up for air in 75 s; land animals' countdowns were below 0 too
void Animals::breathTick(Member &m) {
  const Type &t = this->typeList[m.type];
  if (m.breathCheck-- != 0 || !underwaterNow(m))
    return;
  m.breathCheck = kBreathCheck - 1;
  int was = m.breath;
  m.breath = std::clamp(m.breath + t.breathIncrement, kBreathMin, kBreathMax);
  if (m.breath != was)
    ZT_STEP("0x4389f7", "Breath held longer", {"breath", was, m.breath}, {"cBreathIncrement", t.breathIncrement},
            {"cBreathThreshold", t.breathThreshold});
}

// Health (zoo.exe animalCheckHealth 0x4389aa, every cHealthCheck): at 20%
// (cPctHits) or less, unhappy (cSickChange); others sick, unhappier and
// maybe caught it; now and then ill anyway. An egg runs it too
// (animalUpdate 0x437620: animalEggTick, then this - its illness rolls
// passed over)
void Animals::healthTick(Member &m) {
  const Type &t = this->typeList[m.type];
  if (--m.healthCheck >= 0)
    return;
  ZT_FN("0x4389aa", "Health check (every 30)");
  m.healthCheck = kHealthCheck - 1;
  ZT_STEP("0x4389aa", "Its health", {"health", m.health}, {"cMaxHits", t.maxHits}, {"cPctHits", t.pctHits},
          {"sick", m.sick ? 1.0 : 0.0});
  // zoo.exe 0x4389aa (0x4806ea): health at 0, sick (+0x3a7) and its
  // health cMinHits (type +0x238); the checks in a row this happens with
  // its hunger at its most (type +0x240, 100 - 0x4b661f, no config key)
  // counted (+0x358), any other check back to 0
  // Then 0x4139c9(0,0): its heading (+0x380) land - 0 set at once
  // (0x42fbd8), no walk (its second argument 0); a cOnlyUnderwater one
  // (slot 0x170) asks for 2 instead (ours: its waterMode isn't read)
  if (m.health <= 0) {
    m.sick = true;
    m.health = static_cast<float>(t.minHits);
    if (!t.onlyUnderwater)
      m.waterMode = false;
    m.starve = m.hunger >= 100 ? m.starve + 1 : 0;
  } else {
    m.starve = 0;
  }
  // zoo.exe 0x4389aa (0x4807bd): under cSickTime (type +0x330) such
  // checks, a sick one posts 10029 "%s is very sick." (urgency 2). At
  // cSickTime it's "Being treated poorly" instead (0x480843 -> 0x4808ac):
  // in an exhibit (slot 0x24c) with no keeper at its duties there
  // (habitat +0xf0 under 1), taken away (0x4fa78e(1), +0x13c). (0x4808ac
  // first re-rates the exhibit when its +0x2d asks - ours counts the
  // keepers when asked)
  if (m.starve >= t.sickTime) {
    ZT_STEP("0x4808ac", "Being treated poorly", {"checks", m.starve}, {"cSickTime", t.sickTime});
    if (m.exhibit >= 0 && (this->keepersAtWork ? this->keepersAtWork(m.exhibit) : 0) < 1) {
      m.takenAway = true;
      ZT_STEP("0x4fa78e", "No keeper at work there: taken away");
    }
  } else if (m.sick && this->rm) {
    this->notices.push_back({this->unitText(m, 10029), 2, m.id});
  }
  // Poorly (health x 100 <= cPctHits x cMaxHits), not loose (+0x3a3) or
  // chasing (+0x3a4): where it may lie down (0x440a37), bSick asked for
  // next (0x4301b7 - not taken while it's in its bSick or another set is
  // asked for, and then nothing more); "Not feeling well" and cSickChange
  if (m.health * 100 <= t.pctHits * t.maxHits && !m.escaped && m.prey < 0 && m.huntAnimal < 0 &&
      (!mayRest(m) || requestSet(m, "bsick"))) {
    float was = m.pending;
    m.pending += t.sickChange;
    ZT_STEP("0x4389aa", "Poorly: unhappy", {"pending", was, m.pending}, {"cSickChange", t.sickChange},
            {"next", m.nextSet});
  }
  // (an egg: none - 0x42373d goes to 0x423797 for +0x395)
  if (!m.egg) {
    bool othersSick = false;
    for (const Member &o : this->list)
      othersSick = othersSick || (o.id != m.id && o.exhibit == m.exhibit && o.exhibit >= 0 && o.sick);
    // (the illness rolls: 0x42373d, ill to cMinHits as above - 0x430fa3,
    // 0x5daadd)
    std::uniform_int_distribution<int> pct(0, 99);
    auto say = [&](int sid) {
      if (!this->rm)
        return;
      this->notices.push_back({this->unitText(m, sid), 2, m.id}); // (ill: urgent)
    };
    if (othersSick) {
      m.pending += t.otherSickChange;
      if (!m.sick && pct(this->rng) < t.sickChance && pct(this->rng) < 4) {
        m.sick = true;
        m.health = static_cast<float>(t.minHits);
        if (!t.onlyUnderwater)
          m.waterMode = false; // (0x4139c9(0,0), as above)
        say(15001); // "%s has caught an illness from another animal."
        ZT_STEP("0x4389aa", "Caught an illness from another", {"sick", 0, 1}, {"health", m.health});
      }
    } else if (!m.sick && pct(this->rng) < t.sickRandomChance && pct(this->rng) < 1) {
      m.sick = true;
      m.health = static_cast<float>(t.minHits);
      if (!t.onlyUnderwater)
        m.waterMode = false; // (0x4139c9(0,0), as above)
      say(15000); // "%s is ill."
      ZT_STEP("0x4389aa", "Fell ill (cSickRandomChance in 100, then 1 in 100)", {"sick", 0, 1}, {"health", m.health});
    }
  }
  // zoo.exe 0x4389aa (0x423797): taken away (class slot 0xac), "Being
  // removed" and 10010 "%s has been taken away due to poor treatment."
  // through 0x4807c2 (urgency 2; a female's 40010). ours: the message
  // holds no place (the original's its tile) - the animal is gone
  if (m.takenAway && this->rm)
    this->notices.push_back({this->unitText(m, 10010), 2, -1});
}

void Animals::tick(Member &m, const WorldMap &map, const Fences &fences) {
  // (an egg only waits to hatch - and its health is checked: animalUpdate
  // 0x437620 runs animalEggTick, then animalCheckHealth 0x4389aa)
  if (m.egg) {
    eggTick(m);
    healthTick(m);
    return;
  }
  ZT_FN("0x4378df", "Its status tick (once a second)");
  const Type &t = this->typeList[m.type];
  {
    float was = m.happiness, add = m.pending;
    m.happiness = std::clamp(m.happiness + m.pending, -100.0f, 100.0f);
    m.pending = 0;
    ZT_STEP("0x4378df", "Happiness takes what the last checks found", {"happiness", was, m.happiness},
            {"pending", add, 0});
  }
  if (--m.habitatCheck < 0) {
    m.habitatCheck = kHabitatCheck - 1;
    float was = m.pending;
    int s = this->suitability(m.id, map, fences);
    const char *verdict;
    if (s >= t.habitatPreference)
      m.pending += t.happyHabitatChange, verdict = "suits it (cHappyHabitatChange)";
    else if (s * 100 >= t.habitatPreference * t.pctHabitat)
      m.pending += t.angryHabitatChange, verdict = "not quite (cAngryHabitatChange)";
    else {
      m.pending += t.veryAngryHabitatChange, verdict = "far from it (cVeryAngryHabitatChange)";
      // zoo.exe animalCheckExhibitLike 0x438c47 (0x431701): far from it,
      // in an exhibit (class slot 0x24c; its +0x2c clear - not read),
      // postUnitMessage 10031 "%s doesn't like its exhibit." (urgency 2) -
      // not an egg (+0x395) nor busy in a show (unitBusyInShow 0x437402)
      if (m.exhibit >= 0 && !m.egg && m.showTank < 0 && this->rm)
        this->notices.push_back({this->unitText(m, 10031), 2, m.id});
    }
    ZT_STEP("0x4378df", "Habitat check (every 20)", {"suitability", s}, {"cHabitatPreference", t.habitatPreference},
            {"cPctHabitat", t.pctHabitat}, {"verdict", verdict}, {"pending", was, m.pending});
    if (const Rating *r = this->rating(m, map, fences)) {
      float before = m.pending;
      m.pending += std::trunc(std::clamp(std::trunc(r->compat), -500.0f, 150.0f) / 10.0f);
      if (m.pending != before)
        ZT_STEP("0x4378df", "What it thinks of the others there", {"compat", r->compat}, {"pending", before, m.pending});
    }
  }
  if (--m.captivityCheck < 0) {
    m.captivityCheck = kCaptivityCheck - 1;
    float was = m.pending;
    m.pending -= t.captivity;
    ZT_STEP("0x4378df", "Captivity (every 20)", {"cCaptivity", t.captivity}, {"pending", was, m.pending});
  }
  countGuestCrowd(m, fences);
  if (--m.socialCheck < 0) {
    m.socialCheck = kSocialCheck - 1;
    float was = m.pending;
    int kind, kindAdults, adults, tiles;
    company(m, kind, kindAdults, adults, tiles);
    if (kind < t.numberMin)
      m.pending += t.numberMinChange;
    else if (crowdedBySpecies(t, kindAdults, tiles))
      m.pending += t.numberMaxChange;
    if (adults > 0 && tiles / adults < t.animalDensity)
      m.pending += t.allCrowdedChange;
    ZT_STEP("0x4378df", "Company (every 30)", {"its kind here", kind}, {"cNumberMin", t.numberMin},
            {"cNumberMax", t.numberMax}, {"tiles", tiles}, {"cHabitatSize", t.habitatSize}, {"adults", adults},
            {"cAnimalDensity", t.animalDensity}, {"pending", was, m.pending});
    // Crowded by guests (zoo.exe 0x43885b, applied at 0x421ce8; not at its
    // show, slot 0x22c): more watching it than its cCrowd,
    // cCrowdHappinessChange ("Feeling crowded by guests"), and its next walk
    // goes away from them (0x421d9c: from the signs of their side; both 0,
    // the way it had)
    if (m.showTank < 0 && m.guestCrowd > t.crowd) {
      float before = m.pending;
      m.pending += t.crowdHappinessChange;
      int a = m.guestCrowdX, b = m.guestCrowdY;
      if (a == 0) {
        if (b != 0)
          m.crowdDir = b > 0 ? 4 : 0;
      } else if (b == 0) {
        m.crowdDir = a > 0 ? 6 : 2;
      } else if (a > 0) {
        m.crowdDir = b > 0 ? 5 : 7;
      } else {
        m.crowdDir = b > 0 ? 3 : 1;
      }
      ZT_STEP("0x4386a9", "Feeling crowded by guests (cCrowd)", {"watching", m.guestCrowd}, {"cCrowd", t.crowd},
              {"away", m.crowdDir}, {"pending", before, m.pending});
    }
  }
  if (--m.otherCheck < 0) {
    m.otherCheck = kOtherCheck - 1;
    // zoo.exe animalCheckAngryNeighbours 0x438e70 (0x4216c5): its habitat's
    // animals with +0x3aa - happiness -81 or below, animalUpdateHappiness
    // 0x437a10 - counted by animalRateExhibit 0x444827 (+0x98), less itself;
    // none on zoo ground (habitat +0x2c: not in an exhibit). ours: counted
    // now, not at the last rating; joined habitats (+0x8) not modelled
    if (t.otherAnimalAngryChange < 0 && m.exhibit >= 0)
      for (const Member &o : this->list)
        if (o.id != m.id && o.exhibit == m.exhibit && o.happiness <= -81) {
          float was = m.pending;
          m.pending += t.otherAnimalAngryChange;
          ZT_STEP("0x4378df", "Another here is very angry", {"other", o.name}, {"pending", was, m.pending});
          break;
        }
  }
  // "Not enough keepers" (zoo.exe animalCheckKeepers 0x438d31, from
  // animalUpdate 0x437620 every cKeeperCheck, 30): the animal's +0x378 under
  // its type's +0x28c - not an egg, slot 0x22c - and cNotEnoughKeepersChange
  // (+0x290). Both are 1 in every game - +0x378 from ZTAnimal::setup
  // 0x453e59, +0x28c from setDefaults 0x4b659e, no key reads it (measured:
  // a type's +0x28c read 1 in the running original) - so it never fires:
  // nothing to do here. (Our zoos once saved +0x378 as 0, which made it
  // fire in the original: ZooReader's zooAnimalOut writes 1)
  testFence(m, fences);
  // Eating (zoo.exe animalUpdate 0x437620 -> animalEatFood 0x4fd951 each
  // tick while it eats): at the food, in its bEat, a mouthful every tick -
  // cKeeperFoodUnitsEaten, no more than is left (0x4fdb3a), each unit
  // cFoodUnitValue less hunger - till its hunger is down to its least (type
  // +0x244, 0 - setDefaults 0x4b659e) or the food's gone. (A bEat's 9 s of
  // eat_down is 9 mouthfuls; before, ours took one a bEat and stayed hungry)
  // Eating with its hunger down to its least: animalStopEating 0x4fd86e
  // (animalUpdate 0x437620, ahead of the mouthful)
  if (m.eating && m.hunger <= 0)
    stopEating(m);
  // A cKeepMoving kind (type +0x44c - the sharks and big fish): one
  // mouthful a trip to the food. Its eating isn't set, so neither
  // animalUpdate nor animalCheckHunger (0x438a79) calls animalEatFood again;
  // only Task72_FindKeeperFood::atTarget 0x4fd66a did, once, and the task
  // is replaced (slot 0x1d8(1, 0)) - its next mouthful is its next trip
  if (m.foodItem >= 0 && m.hunger > 0 && !(t.keepMoving && m.mealTaken) &&
      std::any_of(m.stack.begin(), m.stack.end(), [](const auto &f) { return f.first == "beat"; }))
    consumeFood(m);
  if (m.eating) {
    // animalEatFood 0x4fd951: loose (+0x3a3) or none left of its food
    // (0x4fdb3a) - animalStopEating. Else, with units still left after the
    // mouthful (0x4fdbc9 returns the food's +0x154), bEat asked for next -
    // forced over whatever was asked, and not replaceable (setBehaviourSet
    // 0x4300f6 with 0, 1), so a bSleep or bSick asked for during the meal
    // isn't taken - unless bEat is asked already or it can't lie down here
    // (0x440a37). (A cKeepMoving kind never gets here: eating isn't set)
    const auto *food = this->items ? this->items->item(m.foodItem) : nullptr;
    if (m.escaped || !food || food->units <= 0) {
      stopEating(m);
    } else if (m.nextSet != "beat" && mayRest(m)) {
      m.nextSet = "beat";
      ZT_STEP("0x4fd951", "Food left: bEat asked for next (forced)", {"units", food->units});
    }
  }
  // Hunger (zoo.exe 0x438a79): hungrier each check; hungry with no food,
  // the first time nothing, after that "%s can't find any food." and, at
  // 100, unhappier and less healthy. And the dung meter: full, a pile.
  if (--m.hungerCheck < 0) {
    ZT_FN("0x438a79", "Hunger check (every 30)");
    m.hungerCheck = kHungerCheck - 1;
    float was = m.hunger;
    m.hunger = std::min(100.0f, m.hunger + t.hungerIncrement);
    ZT_STEP("0x438a79", "Hungrier", {"hunger", was, m.hunger}, {"cHungerIncrement", t.hungerIncrement});
    if (m.hunger >= t.hungerThreshold && !m.escaped && nearestFood(m) < 0) {
      if (!m.noFoodSeen) {
        m.noFoodSeen = true;
        ZT_STEP("0x438a79", "Hungry, no food: first time, nothing happens", {"cHungerThreshold", t.hungerThreshold});
      } else {
        // zoo.exe 0x49c069: postUnitMessage 10030 "%s can't find any food."
        // (urgency 2) each check after the first - not while it's busy in a
        // show (unitBusyInShow 0x437402, +0x24f, +0x250, tasks 0x85/0x86)
        if (m.showTank < 0 && this->rm) {
          this->notices.push_back({this->unitText(m, 10030), 2, m.id});
          ZT_STEP("0x49c069", "\"Can't find any food\" message");
        }
        if (m.hunger >= 100) {
          float wasPending = m.pending, wasHealth = m.health;
          m.pending += t.noFoodChange;
          m.health = std::max(0.0f, m.health - t.hungryHealthChange);
          ZT_STEP("0x438a79", "Starving: unhappier, less healthy", {"pending", wasPending, m.pending},
                  {"cNoFoodChange", t.noFoodChange}, {"health", wasHealth, m.health},
                  {"cHungryHealthChange", t.hungryHealthChange});
        }
      }
    }
    // The dung meter (zoo.exe 0x438a79 at 0x416460): cDirtyIncrement goes
    // on its pending (+0x2cc), which the next happiness tick adds to the
    // meter (+0x2c8, 0x4378df), kept to cMinDirty..cMaxDirty - so this
    // check looks at the meter without it. At cDirtyThreshold, a drop
    // (0x4a8f89); only one that took puts the meter back to 0
    float wasDirty = m.dirty;
    bool dropped = false;
    // (loose too: on the zoo's grounds, for the maintenance workers)
    if (m.dirty >= t.dirtyThreshold && this->items && (m.exhibit >= 0 || (m.escaped && !m.boxed)))
      dropped = dropDung(m, fences);
    m.dirty = std::clamp((dropped ? 0.0f : m.dirty) + float(t.dirtyIncrement), float(t.minDirty), float(t.maxDirty));
    ZT_STEP("0x438a79", "The dung meter", {"dirty", wasDirty, m.dirty}, {"cDirtyIncrement", t.dirtyIncrement},
            {"cDirtyThreshold", t.dirtyThreshold}, {"dropped", dropped ? 1 : 0});
  }
  murkyWaterTick(m, map, fences);
  breathTick(m);
  healthTick(m);
  // Tiredness (0x4384ba, every cEnergyCheck 30): not while it sleeps; else
  // cEnergyIncrement x animal +0x2d8 more (measured: +0x2d8 is 1 on every
  // animal sampled in the running original, 2026-11-16), no more than
  // cMaxEnergy (animalUpdateHappiness
  // 0x4378df adds it, capped at type +0x248) - over cEnergyThreshold too,
  // where it can't sleep
  // (asleep, its countdown waits and each tick it asks whether it's still
  // in its bSleep - cut short, it's refreshed as if it had woken)
  if (m.sleeping) {
    m.energyCheck = kEnergyCheck - 1;
    if (!m.stack.empty() && m.nextSet != "bsleep" &&
        std::none_of(m.stack.begin(), m.stack.end(), [](const auto &f) { return f.first == "bsleep"; })) {
      m.sleeping = false;
      m.energy = 0;
      m.lastSlept = this->now;
      ZT_STEP("0x4384ba", "Refreshed after sleeping (its bSleep cut short)", {"energy", 0.0});
    }
  } else if (--m.energyCheck < 0) {
    m.energyCheck = kEnergyCheck - 1;
    tirednessCheck(m);
  }
  // Old age and babies (0x4385e5), every 20 ticks
  m.ready = std::min(m.ready + 1, std::max(1, t.reproductionInterval));
  if (m.baby && t.babyToAdult > 0 && ++m.grow >= t.babyToAdult)
    m.baby = false;
  if (--m.reproductionCheck < 0) {
    m.reproductionCheck = 19;
    m.life -= 20;
    std::uniform_int_distribution<int> pct(0, 99);
    if (m.life <= 0 && !m.dying && pct(this->rng) < t.deathChance) {
      // bDie (lies down, sleeps a while), then gone
      m.dying = true;
      m.stack.clear();
      m.path.clear();
      m.stepping = false;
      m.dieAfter = this->set(m, "bdie") ? 1 : 0;
      if (m.dieAfter)
        pushSet(m, "bdie");
      ZT_STEP("0x4385e5", "Old age: it's dying", {"life", m.life}, {"cDeathChance", t.deathChance});
    }
    // A grown one keeps one mate (zoo.exe 0x4385e5): none, it looks for
    // one; its mate gone or in another exhibit, it takes another - at once,
    // or (cMatingType 1) after cMatePickDelay checks, unhappier at first by
    // cNoMateChange - and has no baby this check
    bool lostMate = false;
    if (!m.baby && !m.dying) {
      lostMate = this->mateLost(m);
      if (!lostMate && m.mate < 0) {
        this->findMate(m);
      } else if (lostMate) {
        if (t.matingType == 1) {
          if (m.matePick == kMatePickDelay - 1) {
            m.pending += t.noMateChange;
            ZT_STEP("0x4385e5", "Lost its mate", {"cNoMateChange", t.noMateChange});
          }
          if (m.matePick-- == 0) {
            this->findMate(m);
            m.matePick = kMatePickDelay - 1;
          }
        } else {
          this->findMate(m);
          m.matePick = kMatePickDelay - 1;
        }
      }
    }
    // A happy grown male, ready again, with his mate: cReproductionChance in
    // 100 of a baby by her (zoo.exe 0x4385e5, animalCheckReproduction
    // 0x43103b - one try a check, whatever else is in the exhibit). Once a
    // keeper has been named "Dr. Dolittle" (0x63c2e3), a grizzly or black
    // bear needn't be happy enough (0x421966 -> 0x5da846) and its next check
    // skips the roll (0x431117 -> 0x5db99f, the flag cleared at 0x5db9c7)
    const bool bear =
        t.nameId == Original::Cheat::grizzlyNameId || t.nameId == Original::Cheat::blackBearNameId;
    const bool dolittle = RenameCheats::flags().dolittle && bear;
    if (!lostMate && !m.female && !m.baby && !m.dying && m.exhibit >= 0 && t.reproductionChance > 0 &&
        m.ready >= t.reproductionInterval && (m.happiness >= t.happyReproduceThreshold || dolittle)) {
      Member *mate = this->findMate(m);
      const int roll = mate ? pct(this->rng) : 100;
      if (mate && dolittle) {
        RenameCheats::dolittleUsed();
        ZT_STEP("0x5db99f", "Dr. Dolittle: the breeding roll skipped", {"animal", m.name});
      }
      if (mate && (dolittle || t.reproductionChance > roll)) {
        for (int i = 0; i < t.offspring; i++)
          this->births.push_back({mate->id, m.id, m.type});
        m.ready = 0;
        ZT_STEP("0x4385e5", "A baby on the way", {"mother", mate->name}, {"cOffspring", t.offspring},
                {"cReproductionChance", t.reproductionChance}, {"happiness", m.happiness},
                {"cHappyReproduceThreshold", t.happyReproduceThreshold});
      }
    }
  }
  waterTick(m, map, fences);
  // Rampaging (zoo.exe 0x438514, each status tick): at or under its
  // cRampageThreshold happiness, a cRampageChance in 100 to start, for
  // cRampageTimeMin..cRampageTimeMax ticks ("Rampaging"); over when it's
  // happier again or the time's up ("Stopping rampaging"), its rampage set
  // ended. (Not an egg, not crated)
  if (!m.egg && !m.boxed && !m.dying) {
    if (m.rampaging) {
      m.rampageTicks++;
      if (m.happiness > t.rampageThreshold || m.rampageTicks > t.rampageTimeMax) {
        ZT_STEP("0x438514", "Stops rampaging", {"happiness", m.happiness}, {"cRampageThreshold", t.rampageThreshold},
                {"ticks", m.rampageTicks}, {"cRampageTimeMax", t.rampageTimeMax});
        m.rampaging = false;
        m.rampageTicks = 0;
        for (const auto &s : m.stack)
          if (s.first == "bhabitatrampage" || s.first == "bescapedrampage") {
            m.stack.clear();
            m.path.clear();
            m.pathAt = 0;
            m.stepping = false;
            break;
          }
      }
    } else if (t.rampageChance >= 1 && t.rampageTimeMin >= 1 && m.happiness <= t.rampageThreshold &&
               std::uniform_int_distribution<int>(0, 99)(this->rng) < t.rampageChance &&
               t.rampageTimeMax >= t.rampageTimeMin) {
      // (its ticks start so many short of cRampageTimeMax: it lasts
      // cRampageTimeMin..cRampageTimeMax ticks)
      int span = t.rampageTimeMax - t.rampageTimeMin;
      m.rampaging = true;
      m.rampageTicks = span - std::uniform_int_distribution<int>(0, span)(this->rng);
      ZT_STEP("0x438514", "Starts rampaging", {"happiness", m.happiness}, {"cRampageThreshold", t.rampageThreshold},
              {"cRampageChance", t.rampageChance}, {"lasts (ticks)", t.rampageTimeMax - m.rampageTicks});
    }
  }
  // In its exhibit (zoo.exe 0x438514, every cEscapedCheck 2 ticks): prey run
  // from an adult that hunts them within 7 (or from one chasing them at
  // play); an adult hunter goes after the nearest animal it lists within 5
  // (cPreyRadius) - no chance, no hunger - unless one that hunts it is near,
  // or it's busy with itself (0x420fed: sick, asleep, eating...)
  if (!busy(m)) {
    if (--m.huntCheck <= 0) {
      m.huntCheck = 2;
      // (standing its ground in a stand-off: no running till it's over)
      const Member *threat = m.sleeping || standingOff(m) ? nullptr : threatTo(m, 7.0f);
      if (threat) {
        ZT_STEP("0x438514", "A hunter's near: runs from it (within 7)", {"hunter", threat->name},
                {"distance", std::hypot(threat->x - m.x, threat->y - m.y)});
        bool fresh = m.fleeFrom != threat->id || m.pathAt >= m.path.size();
        m.fleeFrom = threat->id;
        m.huntAnimal = -1;
        if (fresh)
          runFrom(m, *threat, map, fences);
      } else {
        // (Task 0x75's checkArrived 0x4a5471 through chaseOver 0x4a5508: the
        // flight is over once no hunter is found (slot 0x2fc) - or one of
        // its own kind chasing it at play past cChaseTimeOut, which ours
        // ends from the chaser's side; then "Stopping Avoiding", the
        // chaser's prey forgotten (slot 0x258). atTarget 0x61ae0d is
        // 0x431834, the task's end)
        m.fleeFrom = -1;
      }
      if (m.fleeFrom < 0 && (m.huntAnimal < 0 || m.tag) && !m.baby && !occupied(m) &&
          (!t.preyIds[sub(m)].empty() || t.eatsEggs)) {
        const Member *best = nullptr;
        float bestD = 1e30f;
        // (one in its bCaughtBy or bDefense set can't be caught: zoo.exe
        // ZTUnit::caughtBy 0x4a60b9, class slot 0x260, false while its set's
        // name +0x54 holds either)
        auto catchable = [](const Member &o) {
          if (o.stack.empty())
            return true;
          const std::string &now = o.stack.back().first;
          return now.find("bcaughtby") == std::string::npos && now.find("bdefense") == std::string::npos;
        };
        for (const Member &o : this->list) {
          if (o.exhibit != m.exhibit || o.boxed || o.dying || o.caughtBy >= 0 || o.id == this->carried ||
              o.claimedBy >= 0 || !catchable(o) || !hunts(m, o))
            continue;
          if (std::fabs(o.x - m.x) > 5.0f || std::fabs(o.y - m.y) > 5.0f)
            continue;
          float d = (o.x - m.x) * (o.x - m.x) + (o.y - m.y) * (o.y - m.y);
          if (d < bestD) {
            bestD = d;
            best = &o;
          }
        }
        if (best) {
          if (m.huntAnimal != best->id)
            ZT_STEP("0x438514", "Goes after prey it lists (cPrey, within 5: cPreyRadius)", {"prey", best->name},
                    {"distance", std::sqrt(bestD)}, {"at play (tag)", m.tag ? 1 : 0});
          if (m.huntAnimal != best->id) {
            m.huntStart = this->now;
            m.preattacked = false; // (a new chase: a stand-off of its own)
          }
          m.huntAnimal = best->id;
          m.tag = false;
        }
      }
    }
    // Running: on to another spot away when it gets there (Task 0x75's
    // checkArrived 0x4a5471: its route down to under 2 points, the leg's
    // over and whileGoing 0x4a557f plays bRunFromPredator to a new ring
    // tile, 0x4a639e - "Avoiding Predator")
    if (m.fleeFrom >= 0 && m.pathAt >= m.path.size() && !standingOff(m))
      if (const Member *threat = this->member(m.fleeFrom))
        runFrom(m, *threat, map, fences);
    if (m.huntAnimal >= 0) {
      Member *p = this->member(m.huntAnimal);
      // (gone, crated, out of the exhibit; at play, cChaseTimeOut 10 s)
      if (!p || p->boxed || p->exhibit != m.exhibit || p->caughtBy >= 0 ||
          (m.tag && this->now - m.huntStart > 10.0))
        m.huntAnimal = -1;
      else if (standingOff(m))
        ; // (its bPreattack first: the chase after)
      // (both cPreattack, within cDefenseRadius: zoo.exe 0x50eae2)
      else if (!m.tag && !m.preattacked && preattacks(m, *p) &&
               std::hypot(p->x - m.x, p->y - m.y) <= kDefenseRadius)
        standOff(m, *p);
      else
        chase(m, map, fences);
    }
  } else {
    m.huntAnimal = -1;
    m.fleeFrom = -1;
  }
  // A loose predator hunts (cEscapedCheck 2 ticks; cPreyRadius 5): the
  // nearest man in reach, chased at its run (cFastRate), given up after
  // cChaseTimeOut 10 s
  if (t.eatsPeople && m.escaped && !m.boxed && !m.baby && !m.tranquilised && !m.dying && m.mauling < 0) {
    if (m.prey >= 0) {
      float px = 0, py = 0;
      if (!this->preyAt || !this->preyAt(m.prey, px, py) || this->now - m.chaseStart > 10.0) {
        m.prey = -1;
      } else {
        m.stack.clear();
        m.moveAnim = "run";
        m.speed = t.forAge(m.baby).fast;
        walkTo(m, px, py, map, fences);
      }
    } else if (--m.preyCheck <= 0) {
      m.preyCheck = 2;
      int g = this->findPrey ? this->findPrey(m.x, m.y, 5.0f) : -1;
      float px = 0, py = 0;
      if (g >= 0 && this->preyAt && this->preyAt(g, px, py)) {
        m.prey = g;
        m.chaseStart = this->now;
        m.stack.clear();
        m.moveAnim = "run";
        m.speed = t.forAge(m.baby).fast;
        walkTo(m, px, py, map, fences);
      }
    }
  }
  // A shelter or toy now and then (zoo.exe 0x438f20: every 20 ticks, a
  // cBuildingUseChance chance, with no building goal yet - 0x43735c)
  if (--m.buildingCheck < 0) {
    m.buildingCheck = 19;
    if (m.building < 0 && m.buildingGoal < 0 && !m.escaped && !m.sleeping && t.buildingUseChance > 0 &&
        std::uniform_int_distribution<int>(0, 99)(this->rng) < t.buildingUseChance) {
      ZT_STEP("0x438f20", "Goes to use a shelter or toy", {"cBuildingUseChance", t.buildingUseChance});
      useShelter(m, map, fences);
    }
  }
  // Very unhappy: "%s is not happy." (every 10 ticks)
  if (--m.notHappyCheck < 0) {
    m.notHappyCheck = 9;
    if (m.happiness <= -61 && this->rm) {
      std::string s = this->unitText(m, 10028);
      this->notices.push_back({s, 2, m.id}); // (red in the original's list)
      ZT_STEP("", "\"Not happy\" message", {"happiness", m.happiness}, {"message", s});
    }
  }
}

// zoo.exe 0x4219fb: its mate is no more, or is in another exhibit (both
// let go then) - not when the mate's got out
bool Animals::mateLost(Member &m) {
  if (m.mate < 0)
    return false;
  Member *p = this->member(m.mate);
  if (p) {
    if (p->exhibit < 0 || p->exhibit == m.exhibit)
      return false;
    m.mate = -1;
    p->mate = -1;
  }
  return true;
}

// zoo.exe 0x430a19: its mate; with none (or lost), one picked at random in
// its exhibit - grown, of its kind, the other sex, without a mate, and not
// its mother, father or young - and the two paired
Animals::Member *Animals::findMate(Member &m) {
  if (!this->mateLost(m) && m.mate >= 0)
    if (Member *p = this->member(m.mate))
      return p;
  if (m.exhibit < 0)
    return nullptr;
  m.mate = -1;
  std::vector<Member *> free;
  for (Member &o : this->list)
    if (o.id != m.id && o.exhibit == m.exhibit && o.type == m.type && !o.baby && o.female != m.female &&
        o.mate < 0 && o.mother != m.id && o.father != m.id && o.id != m.mother && o.id != m.father)
      free.push_back(&o);
  if (free.empty())
    return nullptr;
  Member *p = free[std::uniform_int_distribution<size_t>(0, free.size() - 1)(this->rng)];
  m.mate = p->id;
  p->mate = m.id;
  ZT_STEP("0x430a19", "Took a mate", {"mate", p->name}, {"choices", static_cast<int>(free.size())});
  return p;
}

void Animals::lifeEvents(const WorldMap &map, const Fences &fences) {
  // Where a mother lays her egg (zoo.exe animalLayEgg 0x60da6b): on rings
  // outward from 3 tiles away (|dx| + |dy| = 3, 4, ... 50) the first tile
  // of her exhibit that's dry ground (not water, not a tank) with nothing
  // standing on it. (The original also scores a tile's objects; ours takes
  // the first free one.) None: by her, as before
  auto eggSpot = [&](const Member &mum, float &ex, float &ey) {
    const int mx = static_cast<int>(std::floor(mum.x)), my = static_cast<int>(std::floor(mum.y));
    const Type &t = this->typeList[mum.type];
    for (int r = 3; r <= 50; r++)
      for (int dx = -r; dx <= r; dx++)
        for (int side = 0; side < 2; side++) {
          const int dy = side == 0 ? -(r - std::abs(dx)) : r - std::abs(dx);
          const int x = mx + dx, y = my + dy;
          if (x < 0 || y < 0 || x >= map.getWidth() || y >= map.getHeight())
            continue;
          int at = fences.exhibitAt(x, y);
          if (at < 0 || isWater(map, x, y) || (this->tankAt && this->tankAt(x + 0.5f, y + 0.5f) >= 0))
            continue;
          if (t.underwater && !t.onlyUnderwater)
            at = fences.habitatRoot(at, map);
          if (at != mum.exhibit)
            continue;
          bool taken = false;
          for (const Member &o : this->list)
            taken = taken || (static_cast<int>(std::floor(o.x)) == x && static_cast<int>(std::floor(o.y)) == y);
          if (taken)
            continue;
          ex = x + 0.5f;
          ey = y + 0.5f;
          return true;
        }
    return false;
  };
  // (the text made from the animal it's about: unitText, zoo.exe 0x49cfeb)
  auto say = [&](const std::string &s, int kind, int animal) {
    if (this->rm)
      this->notices.push_back({s, kind, animal});
  };
  // Born: a young one by its mother ("Congratulations! %s has given birth.")
  std::vector<Birth> births;
  births.swap(this->births);
  for (auto [mother, father, kind] : births) {
    const Member *mum = this->member(mother);
    if (!mum)
      continue;
    const Type &t = this->typeList[kind];
    const std::string born = this->unitText(*mum, 10032); // (hers: 0x4219f1 posts on the mother)
    const int mumId = mum->id;
    // A new one by its mother, made as any other (not a copy of her: none
    // of what she was doing - asleep, in a shelter, hunting - comes with it)
    float bx = mum->x + 0.3f, by = mum->y;
    const bool egg = t.laysEggs && eggSpot(*mum, bx, by);
    const bool female = std::uniform_int_distribution<int>(0, 1)(this->rng) == 1;
    const int id = this->adopt(kind, female, bx, by, map, fences, true);
    Member *b = this->member(id);
    if (!b)
      continue;
    b->baby = true;
    b->grow = 0;
    if (!egg && (mum = this->member(mumId))) {
      b->fx = mum->fx;
      b->fy = mum->fy;
    }
    // (its mother and father, no mate yet - zoo.exe 0x47f14b)
    b->mother = mother;
    b->father = father;
    // (an egg-layer's young: an egg near its mother - zoo.exe 0x60da6b)
    if (t.laysEggs)
      becomeEgg(*b);
    say(born, 1, mumId); // (good news)
    this->markDirty(b->exhibit);
  }
  // Died of old age ("%s has died of old age."); the others in its exhibit
  // grieve (cDeathChange -20). (zoo.exe fDie 0x60cbb3 -> 0x60cbc8 posts 10242,
  // urgency 2, as its line runs: the last line of every stock death set that
  // has one - ours: at the set's end. The bDie sets without one play a death
  // set that has it through fPlaySetTank/Terrain)
  // Taken away (zoo.exe 0x434df7: a unit with +0x13c set is removed from
  // the world, 0x5bdda8) - no grieving, no money back
  for (size_t i = 0; i < this->list.size();) {
    if (this->list[i].takenAway)
      this->remove(this->list[i].id);
    else
      i++;
  }
  for (size_t i = 0; i < this->list.size();) {
    Member &m = this->list[i];
    if (!m.dying || (m.dieAfter && !m.stack.empty())) {
      i++;
      continue;
    }
    int ex = m.exhibit;
    say(this->unitText(m, 10242), 2, -1);
    for (Member &o : this->list)
      if (o.id != m.id && o.exhibit == ex)
        o.pending -= 20;
    this->remove(m.id);
  }
  (void)fences;
}

// Off to a shelter or toy in its exhibit (its [Satisfies] animalrest), the
// nearest with room; inside (cHideUser) for cTimeInside ticks, or (a toy)
// its "b<type>animalrest0" set played there. zoo.exe animalCheckBuildingUse
// 0x438f20 -> findNearestOfKindB 0x4290f3 (kind "animalrest", 0x565054):
// buildings of the kind in its own habitat (slot 0x248), with room
// (0x429330 -> 0x42ac87: its users, +0x18c, under the type's cCapacity,
// +0x16c - those in it, counted at the door), nearest by whole tiles
// squared (the building's tile against its own), ties at random; that sets
// its goal (+0x214..+0x21c, the kind +0x24e) - only that: chooseTask
// 0x436f5c takes it up later (0x43735c -> guestGoToBuilding 0x43b2f6,
// goToBuilding), after its food and drink, ahead of air (chooseBehaviour)
bool Animals::useShelter(Member &m, const WorldMap &map, const Fences &fences) {
  (void)map;
  if (!this->objects)
    return false;
  std::vector<const PlacedObjects::Object *> nearest;
  int bestD = std::numeric_limits<int>::max();
  const int mx = static_cast<int>(std::floor(m.x)), my = static_cast<int>(std::floor(m.y));
  for (const PlacedObjects::Object &o : this->objects->objects()) {
    if (o.fence || fences.exhibitAt(static_cast<int>(o.x), static_cast<int>(o.y)) != m.exhibit)
      continue;
    const ObjectInfo &info = infoOf(PlacedObjects::fileOf(o));
    if (!info.rest || !buildingHasRoom(o.id, info, m))
      continue;
    const int dx = static_cast<int>(std::floor(o.x)) - mx, dy = static_cast<int>(std::floor(o.y)) - my;
    const int d = dx * dx + dy * dy;
    if (d < bestD)
      nearest.clear();
    if (d <= bestD) {
      bestD = d;
      nearest.push_back(&o);
    }
  }
  if (nearest.empty())
    return false;
  const PlacedObjects::Object *best =
      nearest[std::uniform_int_distribution<size_t>(0, nearest.size() - 1)(this->rng)];
  m.buildingGoal = best->id;
  ZT_STEP("0x438f20", "Found a building: its goal", {"building", best->id});
  return true;
}

// zoo.exe guestGoToBuilding 0x43b2f6, for an animal: an entrance with room
// (0x42c02b: each slot with room, 0x42ac87, in turn; its entrances,
// 0x42b101 - entrpos turned with the building - by squared distance from
// it, the nearest it has a route to) -> the slot (+0x24d); it walks there
// (Task 0x66 - 0x67 for a type with +0x1a4: ours walks the same). None: the
// goal cleared (0x5adc8a: an animal's slot 0x264 is 0x4016d1, false - no
// Task 0x83; class slot 0x174 clears it), and it doesn't go
bool Animals::goToBuilding(Member &m, const WorldMap &map, const Fences &fences) {
  const int id = m.buildingGoal;
  m.buildingGoal = -1;
  const PlacedObjects::Object *o = nullptr;
  if (this->objects)
    for (const PlacedObjects::Object &p : this->objects->objects())
      if (p.id == id)
        o = &p;
  if (!o)
    return false;
  const ObjectInfo &info = infoOf(PlacedObjects::fileOf(*o));
  struct Spot {
    float d2;
    int slot;
    float x, y;
  };
  std::vector<Spot> spots;
  // (an entrpos: across and forward from its middle, 64 a tile, turned
  // with it - as Guests::buildingPoint)
  const float angle = o->facing * 3.14159265f / 4.0f;
  const float fwdX = std::sin(angle), fwdY = -std::cos(angle);
  const float acrossX = -std::cos(angle), acrossY = -std::sin(angle);
  for (size_t k = 0; k < info.slots.size(); k++) {
    if (!slotHasRoom(o->id, info, static_cast<int>(k), m))
      continue;
    for (const auto &[px, py] : info.slots[k].entrances) {
      const float ex = o->x + (px * acrossX + py * fwdX) / 64.0f, ey = o->y + (px * acrossY + py * fwdY) / 64.0f;
      const float tx = std::floor(ex) + 0.5f, ty = std::floor(ey) + 0.5f;
      spots.push_back({(ex - m.x) * (ex - m.x) + (ey - m.y) * (ey - m.y), static_cast<int>(k), tx, ty});
    }
  }
  std::stable_sort(spots.begin(), spots.end(), [](const Spot &a, const Spot &b) { return a.d2 < b.d2; });
  const Type &t = this->typeList[m.type];
  for (const Spot &s : spots) {
    m.moveAnim = "walk";
    m.speed = t.forAge(m.baby).slow;
    m.stack.clear();
    if (walkTo(m, s.x, s.y, map, fences)) {
      m.building = id;
      m.restSlot = s.slot;
      ZT_STEP("0x43b2f6", "Going to building", {"building", id}, {"slot", s.slot});
      return true;
    }
  }
  ZT_STEP("0x5adc8a", "No entrance with room it can reach: goal cleared", {"building", id});
  return false;
}

// zoo.exe buildingSlotHasRoom 0x42ac87, for an animal: the building's users
// (+0x18c) under its cCapacity (+0x16c); the slot there, its users (+4)
// under its capacity (+0x30); then 0x42ae68: its id list (+0x18) empty or
// holding its kind (class slot 0x20, the cNameID), not a return slot (+0x45
// - an animal's slot 0x264, 0x4016d1, is false), and its filter (+0x3c)
// not holding its letter (unit +0xa4: m, f or y - 0x409ee3, a strstr).
// The route to an entrance (0x42aad5) is goToBuilding's walk
bool Animals::slotHasRoom(int building, const ObjectInfo &info, int slot, const Member &m) const {
  const int guests = this->buildingGuests ? this->buildingGuests(building) : 0;
  if (shelterUsers(building) + guests >= info.capacity)
    return false;
  if (slot < 0 || slot >= static_cast<int>(info.slots.size()))
    return false;
  const ObjectInfo::Slot &s = info.slots[static_cast<size_t>(slot)];
  if (slotUsers(building, slot) >= s.capacity)
    return false;
  if (!s.ids.empty() &&
      std::find(s.ids.begin(), s.ids.end(), this->typeList[m.type].nameId) == s.ids.end())
    return false;
  if (s.isReturn)
    return false;
  const char letter = m.baby ? 'y' : m.female ? 'f' : 'm';
  return s.filter.find(letter) == std::string::npos;
}

// zoo.exe buildingHasRoom 0x429330: any of its slots (+0x170) with room
bool Animals::buildingHasRoom(int building, const ObjectInfo &info, const Member &m) const {
  for (size_t k = 0; k < info.slots.size(); k++)
    if (slotHasRoom(building, info, static_cast<int>(k), m))
      return true;
  return false;
}

// A slot's users (zoo.exe the building's slot +4): the animals in it
int Animals::slotUsers(int building, int slot) const {
  int users = 0;
  for (const Member &a : this->list)
    users += a.inside && a.building == building && a.restSlot == slot ? 1 : 0;
  return users;
}

// A shelter's users (zoo.exe building +0x18c): those in it
int Animals::shelterUsers(int building) const {
  int users = 0;
  for (const Member &a : this->list)
    users += a.inside && a.building == building ? 1 : 0;
  return users;
}

// Out of its shelter, the building's changes to it (fExitBuilding 0x4a777e ->
// guestBuy 0x42d858 -> ZTAnimal::addPendingHappiness 0x42648c, class slot
// 0x1fc): the adult figure for 'm'/'f', the young one for 'y' onto its
// pending happiness (+0x2ac); the hunger (+0x2b4) and energy (+0x2c4)
// changes onto their pendings, which the next happiness tick (0x4378df)
// applies. ours: hunger and energy applied at once, kept in range
void Animals::leaveShelter(Member &m) {
  if (m.inside && this->objects)
    for (const PlacedObjects::Object &o : this->objects->objects())
      if (o.id == m.building) {
        const ObjectInfo &info = infoOf(PlacedObjects::fileOf(o));
        m.pending += m.baby ? info.childChange : info.adultChange;
        m.hunger = std::clamp(m.hunger + info.hungerChange, 0.0f, 100.0f);
        m.energy = std::max(0.0f, m.energy + info.energyChange);
      }
  m.building = -1;
  m.restSlot = -1;
  m.inside = false;
  m.insideFor = 0;
}

// A keeper's visit (zoo.exe animalUpdate 0x437620 with +0x39c set, at
// 0x49f579): an amphibious type's - type slot 0xcc, 0x410e41: cUnderwater
// (+0x17e) without cOnlyUnderwater (+0x17f) - cKeeperArrivesChangeAmphibious
// (+0x300), any other's cKeeperArrivesChange (+0x2fc), onto +0x2ac
void Animals::keeperArrives(int exhibit) {
  for (Member &m : this->list)
    if (m.exhibit == exhibit && !m.boxed) {
      const Type &t = this->typeList[m.type];
      m.pending += t.underwater && !t.onlyUnderwater ? t.keeperArrivesChangeAmphibious : t.keeperArrivesChange;
    }
}

// zoo.exe Job4_HealAnimal::atTarget 0x4702e8's "Dr. Frankenstein" part:
// the types of "animals" of its subtype (world slot 0x14 with its type's
// +0xa4: every kind, its sex) that lay eggs (cLaysEggs, +0x404) and aren't
// its own (slot 0x20, cNameID); one of them at random (rand() % their
// count); it becomes that kind (slot 0x7c, 0x602929: its type and art -
// the rest of it, its name, health and happiness, stays) and its exhibit
// is rated again (+0x31). ours: one type holds both sexes, so every
// egg-layer's type that isn't its own
void Animals::becomeAnotherKind(int id) {
  Member *m = this->member(id);
  if (!m || m->type < 0 || m->type >= static_cast<int>(this->typeList.size()))
    return;
  const int own = this->typeList[m->type].nameId;
  std::vector<int> kinds;
  for (int i = 0; i < static_cast<int>(this->typeList.size()); i++)
    if (this->typeList[i].laysEggs && this->typeList[i].nameId != own)
      kinds.push_back(i);
  if (kinds.empty())
    return;
  const int pick = kinds[std::uniform_int_distribution<int>(0, 0x7fff)(this->rng) % static_cast<int>(kinds.size())];
  ZT_STEP("0x4702e8", "Dr. Frankenstein: it becomes another kind", {"animal", m->name},
          {"from", this->typeList[m->type].key}, {"to", this->typeList[pick].key});
  m->type = pick;
  markDirty(m->exhibit);
}

void Animals::heal(int id) {
  if (Member *m = this->member(id)) {
    m->health = static_cast<float>(this->typeList[m->type].maxHits);
    m->sick = false;
    // (cStaySick, type +0x44d: bDoneSick started at once over the set it's
    // in - Job4_HealAnimal::atTarget 0x5eccd0 -> 0x43c6c5; the others carry
    // on, +0x20d cleared)
    if (this->typeList[m->type].staySick && this->set(*m, "bdonesick")) {
      m->stack.clear();
      m->playQueue.clear();
      m->stepping = false;
      pushSet(*m, "bdonesick");
      ZT_STEP("0x5eccd0", "Healed, cStaySick: bDoneSick at once", {"animal", m->name});
    }
  }
}

// ----------------------------------------------------------------------------
// Zookeeper Recommendations (zoo.exe 0x483724, flags from 0x447164): the
// problems in red first, then what it doesn't like badly (red), the rest
// (black), then what it doesn't like a little; nothing to say: "This
// exhibit is well suited for %s." Message n is lang string 9901 + n.
// ----------------------------------------------------------------------------
std::vector<std::pair<std::string, bool>> Animals::advice(int id, const WorldMap &map, const Fences &fences) {
  std::vector<std::pair<std::string, bool>> out;
  const Member *m = this->member(id);
  if (!m || !this->rm)
    return out;
  auto text = [&](int sid, const std::string &arg2 = "") {
    std::string s = this->rm->getString(sid);
    size_t at = s.find("%s");
    if (at != std::string::npos)
      s.replace(at, 2, m->name);
    at = s.find("%s");
    if (at != std::string::npos)
      s.replace(at, 2, arg2);
    return s;
  };
  const Rating *r = this->rating(*m, map, fences);
  if (!r) {
    out.push_back({text(9963), true}); // "%s needs to be put in a suitable exhibit."
    return out;
  }
  const Type &t = this->typeList[m->type];
  std::vector<std::pair<int, bool>> bits; // message, red
  auto flag = [&](int bit, bool on, bool red) {
    if (on)
      bits.push_back({bit, red});
  };
  flag(0, m->hunger >= t.hungerThreshold && nearestFood(*m) < 0, true);
  int kind, kindAdults, adults, tiles;
  company(*m, kind, kindAdults, adults, tiles);
  bool few = kind < t.numberMin;
  flag(4, few, t.numberMinChange < -5);
  flag(5, !few && crowdedBySpecies(t, kindAdults, tiles), t.numberMaxChange < -5);
  float compat = r->compat / 10.0f;
  flag(10, compat < 0, compat <= -10);
  // Trees and rocks: 4 points either way, 8 for red; hills 10 and 20
  auto prefs = [&](int lowBit, float have, int want, float near, float far) {
    bool tooFew = want - near > have || (have == 0 && want > 0);
    flag(lowBit, tooFew, std::fabs(have - want) > far);
    flag(lowBit + 1, !tooFew && want + near < have, std::fabs(have - want) > far);
  };
  // (one needing land, in a tank alone: its tree, rock and hill wants aren't
  // asked - 0x445f8b)
  if (!r->noLand) {
    prefs(12, r->foliage, t.treePref, 4, 8);
    prefs(17, r->hills, t.elevationPref, 10, 20);
    prefs(19, r->rockShare, t.rockPref, 4, 8);
  }
  flag(16, adults > 0 && tiles / adults < t.animalDensity, t.allCrowdedChange < -5);
  // A tank's (0x447164: Marine Mania's messages in the base game's unused
  // slots): too shallow (9909), too deep (9910), the wrong water (9912),
  // too dirty under 50 (9956, red under 25), no land (9964, red)
  if (r->tank) {
    flag(8, r->shallow, r->depthRed);
    flag(9, r->deep, r->depthRed);
    flag(11, r->saltWrong, true);
    flag(55, r->clean < 50, r->clean < 25);
    flag(63, r->noLand, true);
  }
  // (terrain: a land exhibit's only)
  for (int k = 0; k < 17 && !r->tank; k++) {
    int p = t.compatTerrain[k];
    if (p >= 0) {
      bool red = std::fabs(r->pct[k] - p) > 6;
      flag(21 + 2 * k, r->pct[k] + 3 < p, red);
      flag(22 + 2 * k, r->pct[k] - 3 > p, red);
    } else {
      flag(22 + 2 * k, r->count[k] > 0, false);
    }
  }
  flag(57, r->noShelter == 0 && r->unusedShelters >= 1, r->unusedShelters > 3);
  flag(58, r->noShelter > 0, r->noShelter > 7);
  flag(59, r->noToy == 0 && r->unusedToys >= 2, r->unusedToys > 2);
  flag(60, r->noToy > 0, false);
  flag(61, r->poo > 0.05f, r->poo > 0.10f);
  // Crowded by guests (zoo.exe 0x43885b sets it, 0x42c400: red when its
  // cCrowdHappinessChange is under -5)
  flag(56, m->showTank < 0 && m->guestCrowd > t.crowd, t.crowdHappinessChange < -5);
  // addition (Features::suitabilityAdvice): the habitat check (zoo.exe
  // 0x438c47) makes an animal unhappier whenever its exhibit's suitability
  // is under its cHabitatPreference, but the original's flags (0x447164)
  // name trees, rocks and hills only when well off what it likes, and never
  // the objects - no code found in zoo.exe sets 9915's bit, "There are not
  // enough native objects..." (its mask searched for; the flags read live)
  // - so an animal losing happiness to its exhibit can be told "This
  // exhibit is well suited" (measured: a player's Herrerasaurus at 79 of
  // the 90 they want, in the original and here). Under its preference,
  // each part of the rating that costs it a point or more of suitability
  // is named with the original's own line (red from 5 points), and the
  // costliest one at least
  if (Features::suitabilityAdvice && !r->tank && this->suitability(m->id, map, fences) < t.habitatPreference) {
    std::vector<std::pair<int, float>> parts; // message, suitability lost
    if (r->objects >= 0) // (under nothing: what it dislikes, listed already)
      parts.push_back({14, 0.2f * (100 - r->objects)});
    parts.push_back({r->foliage < t.treePref ? 12 : 13, 0.1f * (100 - r->trees)});
    parts.push_back({r->rockShare < t.rockPref ? 19 : 20, 0.1f * (100 - r->rocks)});
    parts.push_back({r->hills < t.elevationPref ? 17 : 18, 0.1f * (100 - r->elevation)});
    for (int k = 0; k < 17; k++)
      if (t.compatTerrain[k] > 0 && r->pct[k] < t.compatTerrain[k])
        parts.push_back({21 + 2 * k, 0.3f * (t.compatTerrain[k] - r->pct[k])});
    std::stable_sort(parts.begin(), parts.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    for (size_t i = 0; i < parts.size(); i++) {
      if (parts[i].second <= 0 || (i > 0 && parts[i].second < 1))
        continue;
      const bool red = parts[i].second >= 5;
      auto had = std::find_if(bits.begin(), bits.end(), [&](const auto &b) { return b.first == parts[i].first; });
      if (had == bits.end())
        bits.push_back({parts[i].first, red});
      else
        had->second = had->second || red;
    }
  }
  std::sort(bits.begin(), bits.end());
  auto dislikes = [&](bool red) {
    for (auto [nameId, v] : r->dislikes)
      if (red ? v < -5 : v >= -5)
        out.push_back({text(9990, this->rm->getString(nameId + 55000)), red});
  };
  for (auto [bit, red] : bits)
    if (red)
      out.push_back({text(9901 + bit), true});
  dislikes(true);
  for (auto [bit, red] : bits)
    if (!red)
      out.push_back({text(9901 + bit), false});
  dislikes(false);
  if (out.empty())
    out.push_back({text(9900), false});
  return out;
}

// ----------------------------------------------------------------------------
// Smiles and frowns
// ----------------------------------------------------------------------------
// zoo.exe habitatObjectAdded 0x4d8cd2 (before) and habitatObjectRemoved
// 0x4d8bd7 (after), from placeObject 0x4d9088: the habitat of the thing's
// tile, and only when that habitat is a tank (slot 0x20) and the thing an
// animal or one of the 'objects' kinds, the habitats joined to it (0x41620d
// on habitat +0x8; ours: its habitatGroup, the land it's joined to and the
// tanks joined there). A change on land doesn't reach a pool joined to it.
// The ground (exhibitRatingSmiles3 0x4dbc79, its own list of habitats) still
// takes the whole group - ours
std::vector<int> Animals::smileHabitats(int exhibit, Change change, const WorldMap &map,
                                        const Fences &fences) const {
  std::vector<int> out;
  if (exhibit < 0)
    return out;
  if (change == Change::Ground) {
    for (int id : fences.habitatGroup(exhibit, map))
      out.push_back(id);
    return out;
  }
  out.push_back(exhibit);
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (ex && ex->tank)
    for (int id : fences.habitatGroup(exhibit, map))
      if (id != exhibit)
        out.push_back(id);
  return out;
}

// exhibitRatingSmiles2 0x4d8a4d: the snapshot (DAT_0063b998, kind -> its
// rating, 0x4d92db) cleared and refilled for each habitat in turn - so after
// a tank's joined habitats only the last one's is kept (ours: in exhibit id
// order; the original's set is by address)
void Animals::snapshot(int exhibit, const WorldMap &map, const Fences &fences, Change change) {
  if (exhibit < 0)
    return;
  for (int id : this->smileHabitats(exhibit, change, map, fences)) {
    this->evaluate(id, map, fences);
    auto &snap = change == Change::Ground ? this->snaps[id] : this->snapOne;
    snap.clear();
    for (const auto &[kind, r] : this->evaluations[id].kinds)
      snap[kind] = total(r);
  }
}

void Animals::face(int exhibit, int kind, bool smile) {
  for (Member &m : this->list)
    if (m.exhibit == exhibit && m.type == kind && m.id != this->carried)
      m.faces.push_back({smile, 0.0f});
}

void Animals::react(int exhibit, Change change, const std::string &file, int animalType, bool removed,
                    const WorldMap &map, const Fences &fences) {
  if(exhibit<0)return;
  // (exhibitRatingSmiles1 0x4d89b2 -> 0x4d93a3: each habitat's kinds
  // compared with the one snapshot; a kind it doesn't hold counts as not
  // rated before. A tank with habitats joined to it - habitat +0xc, the
  // count of its set at +0x8 - skips its own kinds: its pool's animals show
  // nothing, the land joined to it reacts)
  for(int id:this->smileHabitats(exhibit,change,map,fences)) {
    if(change!=Change::Ground)
      if(const Fences::Exhibit *e=fences.exhibit(id);e&&e->tank&&fences.habitatGroup(id,map).size()>1)continue;
    reactOne(id,change,file,animalType,removed,map,fences,change==Change::Ground?this->snaps[id]:this->snapOne);
  }
  if(change!=Change::Ground)this->snapOne.clear();
}

void Animals::reactOne(int exhibit, Change change, const std::string &file, int animalType, bool removed,
                    const WorldMap &map, const Fences &fences, std::map<int, float> &snap) {
  if (exhibit < 0 || this->paused)
    return;
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex)
    return;
  this->evaluate(exhibit, map, fences);
  bool smiled = false, frowned = false;
  for (const auto &[kind, r] : this->evaluations[exhibit].kinds) {
    const Type &t = this->typeList[kind];
    float now = total(r);
    float old = snap.count(kind) ? snap[kind] : 0.0f;
    int verdict = 0;
    if (change == Change::Ground) {
      verdict = now > old ? 1 : now < old ? -1 : 0;
    } else {
      // (a kind that wasn't rated before isn't compared)
      if (old == 0.0f)
        now = 0.0f;
      if (now != old) {
        verdict = now > old ? 1 : -1;
      } else {
        // The same: what it thinks of the thing itself
        int v = 0;
        auto at = [](const std::map<int, int> &list, int key) {
          auto it = list.find(key);
          return it == list.end() ? 0 : it->second;
        };
        if (change == Change::Object) {
          const ObjectInfo &o = infoOf(file);
          v = at(t.suitable, o.habitat) + at(t.suitable, o.nameId);
        } else if (animalType >= 0 && animalType < static_cast<int>(this->typeList.size())) {
          const Type &a = this->typeList[animalType];
          v = at(t.compatible, a.nameId) + at(t.compatible, a.genus) + at(t.compatible, a.family);
        }
        if (removed)
          v = -v;
        verdict = v > 0 ? 1 : v < 0 ? -1 : 0;
      }
    }
    if (verdict != 0) {
      face(exhibit, kind, verdict > 0);
      smiled |= verdict > 0;
      frowned |= verdict < 0;
    }
  }
  if (smiled)
    Sound::get().play("sounds/smile", 1200);
  if (frowned)
    Sound::get().play("sounds/frown", 1000);
  // (an object's or animal's snapshot is kept for the joined habitats after
  // it - react clears it)
  if (change == Change::Ground)
    snap.clear();
}

// ----------------------------------------------------------------------------
// Sounds
// ----------------------------------------------------------------------------
bool Animals::soundOf(int type, const std::string &key, std::string &file, int &atten) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return false;
  auto it = this->typeList[type].sounds.find(lower(key));
  if (it == this->typeList[type].sounds.end())
    return false;
  file = it->second.first;
  atten = it->second.second;
  return true;
}

// fPlayWithSound: heard from the middle of the view
void Animals::playSound(const Member &m, const std::string &key) {
  std::string file;
  int atten = 0;
  float dx = 0, dy = 0;
  if (soundOf(m.type, key, file, atten) && this->viewOffset && this->viewOffset(m.x, m.y, dx, dy))
    Sound::get().playAt(file, dx, dy, atten);
}
