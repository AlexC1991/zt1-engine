#include "Animals.hpp"

#include <algorithm>
#include <cmath>

#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "Pathfinder.hpp"
#include "PlacedObjects.hpp"
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
// cOtherCheck 40, cHungerCheck 30); an exhibit is rated again every 7 s
constexpr float kTickMs = 1000.0f;
constexpr int kHabitatCheck = 20, kCaptivityCheck = 20, kSocialCheck = 30, kOtherCheck = 40,
              kHungerCheck = 30;
constexpr float kRateEveryMs = 7000.0f;

std::string lower(std::string s) { return Utils::string_to_lower(s); }

std::string stripAni(std::string p) {
  p = lower(p);
  size_t dot = p.rfind(".ani");
  return dot == std::string::npos ? p : p.substr(0, dot);
}

std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\""), b = s.find_last_not_of(" \t\"");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// "fPlaySetProb(bIdle1,20,bHappy1,50)" -> fplaysetprob [bidle1, 20, ...]
bool parseStep(const std::string &text, Animals::Step &out) {
  size_t open = text.find('('), close = text.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open)
    return false;
  out.fn = lower(trim(text.substr(0, open)));
  out.args.clear();
  std::string inner = text.substr(open + 1, close - open - 1);
  size_t at = 0;
  while (at <= inner.size()) {
    size_t comma = inner.find(',', at);
    std::string a = lower(trim(inner.substr(at, comma == std::string::npos ? std::string::npos : comma - at)));
    if (!a.empty())
      out.args.push_back(a);
    if (comma == std::string::npos)
      break;
    at = comma + 1;
  }
  return !out.fn.empty();
}
} // namespace

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
  for (const CatalogItem *item : catalog.inCategory("animals", false)) {
    IniReader *ai = rm->getIniReader(item->file);
    if (!ai)
      continue;
    Type t;
    t.file = item->file;
    t.key = item->type;
    t.nameId = item->nameId;
    t.cost = item->cost;
    t.expansion = item->expansion;
    const std::string ints = "m/characteristics/integers";
    auto num = [&](const char *key, int fallback) { return ai->getInt(ints, key, fallback); };
    t.slow = num("cslowrate", 30) / 60.0f;
    t.medium = num("cmediumrate", 45) / 60.0f;
    t.fast = num("cfastrate", 66) / 60.0f;
    t.initialHappiness = num("cinitialhappiness", 50);
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
    t.hungryHealthChange = num("chungryhealthchange", 5);
    t.sickTime = num("csicktime", 5);
    t.keeperArrivesChange = num("ckeeperarriveschange", 25);
    t.dirtyIncrement = num("cdirtyincrement", 15);
    t.dirtyThreshold = num("cdirtythreshold", 70);
    // (cTimeDeath appears twice in some files: the last one counts)
    {
      std::vector<std::string> td = ai->getList(ints, "ctimedeath"), dc = ai->getList(ints, "cdeathchance");
      t.timeDeath = td.empty() ? 5001 : std::atoi(td.back().c_str());
      t.deathChance = dc.empty() ? 100 : std::atoi(dc.back().c_str());
    }
    t.reproductionChance = num("creproductionchance", 0);
    t.reproductionInterval = num("creproductioninterval", 0);
    t.happyReproduceThreshold = num("chappyreproducethreshold", 100);
    t.offspring = std::max(1, num("coffspring", 1));
    t.babyToAdult = num("cbabytoadult", 0);
    t.smallPoo = num("csmallzoodoo", 0) != 0;
    t.buildingUseChance = num("cbuildingusechance", 0);
    t.maxEnergy = std::max(1, num("cmaxenergy", 100));
    t.swims = num("cswims", 0) != 0;
    t.waterNeeded = num("cwaterneeded", 0);
    t.landNeeded = num("clandneeded", 0);
    t.enterWaterChance = num("centerwaterchance", 0);
    t.enterLandChance = num("centerlandchance", 0);
    t.drinkWaterChance = num("cdrinkwaterchance", 0);
    t.chaseAnimalChance = num("cchaseanimalchance", 0);
    t.genus = num("cgenus", 0);
    t.family = num("cfamily", 0);
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
    t.needShelter = num("cneedshelter", 0) != 0;
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
    t.jumper = num("cisjumper", 0) == 1;
    t.climber = num("cisclimber", 0) == 1;
    t.bash = num("cbashstrength", 0);
    t.crush = num("ccrushesfences", 0);
    for (int s = 0; s < 3; s++) {
      static const char *subs[3] = {"m", "f", "y"};
      std::vector<std::string> list = ai->getList(std::string(subs[s]) + "/characteristics/integers", "cprey");
      if (list.empty() && s > 0)
        list = ai->getList(ints, "cprey");
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
      for (const auto &kv : names) {
        std::string v = lower(trim(kv.second));
        size_t semi = v.find(';');
        if (semi != std::string::npos)
          v = trim(v.substr(0, semi));
        if (v.empty())
          continue;
        t.anims[s][lower(kv.first)] = v.find('/') != std::string::npos ? stripAni(v) : base + "/" + v + "/" + v;
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
    // [AmbientAnims]: a = low, a = high, b = set; in turn
    std::vector<std::string> a = ai->getList("ambientanims", "a"), b = ai->getList("ambientanims", "b");
    for (size_t i = 0; i < b.size() && 2 * i + 1 < a.size(); i++)
      t.ambient.push_back({std::atoi(a[2 * i].c_str()), std::atoi(a[2 * i + 1].c_str()), lower(trim(b[i]))});
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
    if (e && e->tank)
      ex = -1;
    // A crate put down in an exhibit lets its animal out (every
    // cBoxedCheck, 2 ticks: about two seconds)
    if (m.boxed && ex >= 0) {
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
      if (this->rm) {
        std::string s = this->rm->getString(10007); // "%s has escaped."
        size_t at = s.find("%s");
        if (at != std::string::npos)
          s.replace(at, 2, m.name);
        this->messages.push_back(s);
      }
    } else if (ex >= 0) {
      m.escaped = false;
    }
  }
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
      if (!fences.passable(e, t.climber, t.jumper, t.bash, t.crush))
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
  bool jump = !broken && t.jumper && fences.passable(way, false, true, 0, 0);
  bool climb = !broken && !jump && t.climber && fences.passable(way, true, false, 0, 0);
  m.crossAnim = jump ? "jump_high" : climb ? "climb_up" : "";
  m.crossAt = static_cast<int>(m.path.size()) - 1;
  m.breaks = !broken && t.bash > 0 && !climb;
  return true;
}

bool Animals::hunts(const Member &predator, const Member &prey) const {
  if (predator.baby || predator.id == prey.id)
    return false;
  const Type &t = this->typeList[predator.type];
  return t.preyIds[sub(predator)].count(this->typeList[prey.type].nameId) > 0;
}

void Animals::moveOf(const Member &m, const std::string &setName, std::string &anim, float &speed) const {
  const Type &t = this->typeList[m.type];
  anim = "run";
  speed = t.fast;
  if (const std::vector<Step> *steps = this->set(m, setName))
    for (const Step &s : *steps)
      if (s.fn == "fmove" && s.args.size() >= 4) {
        std::string a = s.args[2];
        a.erase(std::remove(a.begin(), a.end(), '"'), a.end());
        if (!a.empty())
          anim = lower(trim(a));
        if (std::atoi(s.args[3].c_str()) > 0)
          speed = std::atoi(s.args[3].c_str()) / 60.0f;
        return;
      } else if (s.fn == "frun") {
        return;
      }
}

// Busy: crated, darted, carried, dying, caught, killing, in a shelter, or
// being seen to by a keeper - not hunted, not hunting
bool Animals::busy(const Member &m) const {
  return m.boxed || m.tranquilised || m.dying || m.id == this->carried || m.caughtBy >= 0 || m.victim >= 0 ||
         m.building >= 0 || m.inside || m.claimedBy >= 0 || m.escaped || m.exhibit < 0;
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
      if (walkTo(m, x + 0.5f, y + 0.5f, map, fences))
        return;
    }
}

// After it (zoo.exe 0x4a5b4d): where it is, or a step or two along where
// it's going, at its bChasePrey pace
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
  std::string anim;
  float speed;
  moveOf(m, "bchaseprey", anim, speed);
  m.stack.clear();
  m.moveAnim = anim;
  m.speed = speed;
  if (!walkTo(m, gx, gy, map, fences))
    m.huntAnimal = -1; // (no way to it: given up)
}

void Animals::tranquilise(int id) {
  Member *m = this->member(id);
  if (!m)
    return;
  m->tranquilised = true;
  // (darted: the chase is over, and a mauling with it)
  m->prey = -1;
  m->maulFor = 0;
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
  m->maulFor = 0;
  m->claimedBy = -1;
  m->stack.clear();
  m->path.clear();
  m->pathAt = 0;
  play(*m, "stand", Member::Mode::Loop, 1.0f);
}

int Animals::escapedCount() const {
  int n = 0;
  for (const Member &m : this->list)
    n += m.escaped ? 1 : 0;
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
Animation *Animals::art(const Member &m, const std::string &nameIn) const {
  const Type &t = this->typeList[m.type];
  int s = sub(m);
  // (a swimmer on a water tile: walking and running are its swim, standing
  // its water idle - zoo.exe 0x41338a, 0x413491)
  std::string name = nameIn;
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
  Animation *a = this->rm && this->rm->hasResource(path + ".ani") ? this->rm->getAnimation(path) : nullptr;
  this->artCache[path] = a;
  return a;
}

Animation *Animals::preview(int type, bool female) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return nullptr;
  Member m;
  m.type = type;
  m.female = female;
  return this->art(m, "stand");
}

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
  if (fences.exhibitAt(x, y) != exhibit)
    return false;
  if (exhibit < 0 && !fences.insideZoo(x, y))
    return false;
  const MapTile *tile = map.getTile(x, y);
  if (!tile)
    return false;
  if (!t.swims && (tile->terrainType == 9 || tile->terrainType == 10))
    return false;
  if (this->index && this->index->blocked(x, y))
    return false;
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
                              const Fences &fences) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return Fences::Fit::Outside;
  int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  int ex = fences.exhibitAt(tx, ty);
  const Fences::Exhibit *e = ex >= 0 ? fences.exhibit(ex) : nullptr;
  // A zoo animal: a land exhibit
  if (!e || e->tank || !standable(tx, ty, this->typeList[type], ex, map, fences))
    return Fences::Fit::Outside;
  // Not on top of another (the original shows red over one)
  for (const Member &m : this->list)
    if (m.id != this->carried && std::fabs(m.x - x) < 0.5f && std::fabs(m.y - y) < 0.5f)
      return Fences::Fit::InTheWay;
  return Fences::Fit::Ok;
}

int Animals::adopt(int type, bool female, float x, float y, const WorldMap &map,
                   const Fences &fences) {
  if (canPlace(type, x, y, map, fences) != Fences::Fit::Ok)
    return -1;
  const Type &t = this->typeList[type];
  Member m;
  m.id = this->nextId++;
  m.type = type;
  m.female = female;
  m.x = x;
  m.y = y;
  m.exhibit = fences.exhibitAt(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  m.name = (this->rm ? this->rm->getString(t.nameId) : t.key) + " " + std::to_string(++this->counts[type]);
  m.happiness = static_cast<float>(std::clamp(t.initialHappiness, -100, 100));
  m.energy = 0;
  m.health = static_cast<float>(t.maxHits);
  m.life = t.timeDeath - 1;
  m.ready = t.reproductionInterval; // (ready at once, as the original)
  // Each check's countdown starts somewhere in its interval
  auto start = [&](int interval) { return std::uniform_int_distribution<int>(0, interval - 1)(this->rng); };
  m.habitatCheck = start(kHabitatCheck);
  m.captivityCheck = start(kCaptivityCheck);
  m.socialCheck = start(kSocialCheck);
  m.otherCheck = start(kOtherCheck);
  m.hungerCheck = start(kHungerCheck);
  m.healthCheck = start(30);
  m.energyCheck = start(30);
  m.reproductionCheck = start(20);
  m.notHappyCheck = start(10);
  this->markDirty(m.exhibit);
  m.lastAte = m.lastSlept = this->now;
  float a = std::uniform_real_distribution<float>(0, 6.2831853f)(this->rng);
  m.fx = std::cos(a);
  m.fy = std::sin(a);
  play(m, "stand", Member::Mode::Loop, 1.0f);
  // (put down in the water: in the water)
  m.waterMode = isWater(map, static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
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

int Animals::refund(int id) const {
  const Member *m = this->member(id);
  if (!m)
    return 0;
  return this->typeList[m->type].cost * shownHappiness(*m) / 100;
}

void Animals::moveTo(int id, float x, float y) {
  if (Member *m = this->member(id)) {
    m->x = x;
    m->y = y;
  }
}

void Animals::hold(int id, const WorldMap &map, const Fences &fences) {
  if (const Member *m = this->member(id))
    this->carriedFit = canDrop(id, m->x, m->y, map, fences);
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

void Animals::pushSet(Member &m, const std::string &name) {
  if (m.stack.size() < 8 && this->set(m, name))
    m.stack.push_back({name, 0});
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

int Animals::nearestFood(const Member &m) const {
  if (!this->items)
    return -1;
  int found = -1;
  float best = 1e9f;
  for (const ZooItems::Item &i : this->items->items()) {
    if (i.kind != ZooItems::Kind::Food || i.exhibit != m.exhibit || i.units <= 0)
      continue;
    float d = std::hypot(i.x - m.x, i.y - m.y);
    if (d < best) {
      best = d;
      found = i.id;
    }
  }
  return found;
}

// How happy it is picks what it does ([AmbientAnims]); hungry with food
// down, it goes to eat; worn out, it sleeps
void Animals::chooseBehaviour(Member &m) {
  const Type &t = this->typeList[m.type];
  // Loose: its escaped behaviour (zoo.exe: no eating or sleeping)
  if (m.escaped && this->set(m, "bescaped")) {
    pushSet(m, "bescaped");
    return;
  }
  if (m.hunger >= t.hungerThreshold && nearestFood(m) >= 0 && this->set(m, "bfindkeeperfood")) {
    pushSet(m, "bfindkeeperfood");
    return;
  }
  // Tired (cEnergyThreshold): to sleep; rested after (zoo.exe 0x4384ba)
  if (m.energy >= t.energyThreshold && !m.escaped && this->set(m, "bsleep")) {
    m.energy = 0;
    m.lastSlept = this->now;
    pushSet(m, "bsleep");
    return;
  }
  // Sick: lies about (bSick)
  if (m.sick && this->set(m, "bsick")) {
    pushSet(m, "bsick");
    return;
  }
  int h = static_cast<int>(std::lround(m.happiness));
  // (a swimmer in the water: its [AmbientAnimsWater], by where it stands -
  // zoo.exe 0x43ca83)
  if (t.swims && onWater(m))
    for (const Ambient &a : t.ambientWater)
      if (h >= a.lo && h <= a.hi && this->set(m, a.set)) {
        pushSet(m, a.set);
        return;
      }
  for (const Ambient &a : t.ambient)
    if (h >= a.lo && h <= a.hi) {
      pushSet(m, a.set);
      return;
    }
  pushSet(m, "bidle1");
}

void Animals::nextStep(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  // (dying: its bDie, then nothing more)
  if (m.stack.empty() && m.dying) {
    m.dieAfter = 0;
    play(m, "stand", Member::Mode::Loop, 1.0f);
    return;
  }
  if (m.stack.empty()) {
    m.drinkX = m.drinkY = -1;
    chooseBehaviour(m);
  }
  for (int guard = 0; guard < 32 && !m.stack.empty(); guard++) {
    const std::vector<Step> *steps = this->set(m, m.stack.back().first);
    if (!steps || m.stack.back().second >= steps->size()) {
      m.stack.pop_back();
      continue;
    }
    Step s = (*steps)[m.stack.back().second++];
    auto arg = [&](size_t i) { return i < s.args.size() ? s.args[i] : std::string(); };
    if (s.fn == "fplay" || s.fn == "fplaywithsound") {
      play(m, arg(0), Member::Mode::Once);
      if (s.fn == "fplaywithsound")
        playSound(m, arg(1));
    } else if (s.fn == "fplaytime") {
      play(m, arg(0), Member::Mode::Loop, std::max(0.5f, static_cast<float>(std::atof(arg(1).c_str()))));
    } else if (s.fn == "fplaypingpong") {
      play(m, arg(0), Member::Mode::PingPong);
    } else if (s.fn == "fplayreverse") {
      play(m, arg(0), Member::Mode::Reverse);
    } else if (s.fn == "fplayset") {
      pushSet(m, arg(0));
      continue;
    } else if (s.fn == "fplaysetprob") {
      // Weighted: set, chance, set, chance ...
      int total = 0;
      for (size_t i = 0; i + 1 < s.args.size(); i += 2)
        if (this->set(m, s.args[i]))
          total += std::max(0, std::atoi(s.args[i + 1].c_str()));
      if (total <= 0)
        continue;
      int roll = std::uniform_int_distribution<int>(0, total - 1)(this->rng);
      for (size_t i = 0; i + 1 < s.args.size(); i += 2) {
        if (!this->set(m, s.args[i]))
          continue;
        roll -= std::max(0, std::atoi(s.args[i + 1].c_str()));
        if (roll < 0) {
          pushSet(m, s.args[i]);
          break;
        }
      }
      continue;
    } else if (s.fn == "fwalk" || s.fn == "fmove" || s.fn == "frun") {
      std::string target = arg(0);
      m.moveAnim = s.fn == "frun" ? "run" : s.fn == "fmove" && !arg(2).empty() ? arg(2) : "walk";
      m.speed = s.fn == "frun" ? t.fast
                : s.fn == "fmove" && !arg(3).empty() ? std::atoi(arg(3).c_str()) / 60.0f
                                                     : t.slow;
      bool walking = false;
      if (target == "keeperfood" || target == "otherfood") {
        m.foodItem = nearestFood(m);
        const ZooItems::Item *food = m.foodItem >= 0 && this->items ? this->items->item(m.foodItem) : nullptr;
        if (!food) {
          // Nothing to eat: the rest of the set is for eating it
          m.stack.pop_back();
          continue;
        }
        // To beside it
        float dx = m.x - food->x, dy = m.y - food->y, d = std::hypot(dx, dy);
        float gx = food->x, gy = food->y;
        if (d > 0.7f) {
          gx += dx / d * 0.6f;
          gy += dy / d * 0.6f;
        }
        walking = walkTo(m, gx, gy, map, fences);
      } else if (target == "0" || target.empty()) {
        walking = walkSomewhere(m, map, fences);
      }
      if (!walking)
        continue;
    } else if (s.fn == "ffacetowardfood") {
      if (const ZooItems::Item *food = m.foodItem >= 0 && this->items ? this->items->item(m.foodItem) : nullptr)
        faceToward(m, food->x, food->y);
      else if (m.drinkX >= 0)
        faceToward(m, m.drinkX, m.drinkY);
      continue;
    } else {
      // (not yet: fDie, fDustBall, fFollow, ...)
      continue;
    }
    return;
  }
  if (!m.stepping)
    play(m, "stand", Member::Mode::Loop, 1.0f);
}

bool Animals::walkTo(Member &m, float gx, float gy, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  int tx = static_cast<int>(std::floor(gx)), ty = static_cast<int>(std::floor(gy));
  // (its own tile always: something just put down on it, it walks out)
  auto pass = [&](int x, int y) { return (x == sx && y == sy) || standable(x, y, t, m.exhibit, map, fences); };
  auto step = [&](int x, int y, int nx, int ny) { return canStep(x, y, nx, ny, map, fences); };
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

// fWalk(0,0): to somewhere else in its exhibit
bool Animals::walkSomewhere(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  if (m.exhibit < 0) {
    // Loose: somewhere about it on the zoo's open ground
    std::uniform_int_distribution<int> off(-8, 8);
    std::uniform_real_distribution<float> in(0.15f, 0.85f);
    int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
    for (int tries = 0; tries < 12; tries++) {
      int x = sx + off(this->rng), y = sy + off(this->rng);
      if (standable(x, y, t, -1, map, fences) && walkTo(m, x + in(this->rng), y + in(this->rng), map, fences))
        return true;
    }
    return false;
  }
  // A way out it can take: it takes it (zoo.exe: no chance roll)
  if (breakOut(m, map, fences))
    return true;
  const Fences::Exhibit *ex = fences.exhibit(m.exhibit);
  if (!ex || ex->tiles.empty())
    return false;
  std::vector<std::pair<int, int>> tiles(ex->tiles.begin(), ex->tiles.end());
  std::uniform_real_distribution<float> in(0.15f, 0.85f);
  for (int tries = 0; tries < 8; tries++) {
    auto [x, y] = tiles[std::uniform_int_distribution<size_t>(0, tiles.size() - 1)(this->rng)];
    if (!standable(x, y, t, m.exhibit, map, fences))
      continue;
    // (a swimmer wanders where it is: the water in the water, else land -
    // zoo.exe 0x4273ec)
    if (t.swims && isWater(map, x, y) != m.waterMode)
      continue;
    if (walkTo(m, x + in(this->rng), y + in(this->rng), map, fences))
      return true;
  }
  return false;
}

bool Animals::isWater(const WorldMap &map, int x, int y) {
  const MapTile *t = map.getTile(x, y);
  return t && (t->terrainType == 9 || t->terrainType == 10);
}

bool Animals::onWater(const Member &m) const {
  return this->mapRef && isWater(*this->mapRef, static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
}

// Into or out of the water; a drink
void Animals::waterTick(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  const Fences::Exhibit *ex = m.exhibit >= 0 ? fences.exhibit(m.exhibit) : nullptr;
  if (!ex || m.escaped || m.boxed || m.dying || m.sleeping || m.building >= 0)
    return;
  std::uniform_int_distribution<int> pct(0, 99);
  // (global_animals.ai: cWaterCheck 40, cBoredCheck 30)
  if (m.waterCheck < 0)
    m.waterCheck = std::uniform_int_distribution<int>(0, 39)(this->rng);
  if (m.boredCheck < 0)
    m.boredCheck = std::uniform_int_distribution<int>(0, 29)(this->rng);
  if (--m.waterCheck <= 0) {
    m.waterCheck = 39;
    std::vector<std::pair<int, int>> water, land;
    for (auto [x, y] : ex->tiles)
      (isWater(map, x, y) ? water : land).push_back({x, y});
    bool go = false;
    std::vector<std::pair<int, int>> *to = nullptr;
    if (!m.waterMode && t.swims && pct(this->rng) < t.enterWaterChance &&
        static_cast<int>(water.size()) > t.waterNeeded) {
      go = true;
      to = &water;
    } else if (m.waterMode && pct(this->rng) < t.enterLandChance && static_cast<int>(land.size()) > t.landNeeded) {
      go = true;
      to = &land;
    }
    if (go) {
      bool wantWater = to == &water;
      m.waterMode = wantWater;
      // (already there: just that)
      if (onWater(m) != wantWater) {
        auto [x, y] = (*to)[std::uniform_int_distribution<size_t>(0, to->size() - 1)(this->rng)];
        m.stack.clear();
        m.moveAnim = "walk";
        m.speed = t.slow;
        walkTo(m, x + 0.5f, y + 0.5f, map, fences);
      }
    }
  }
  if (--m.boredCheck <= 0) {
    m.boredCheck = 29;
    // A drink: beside the nearest fresh water, head down (its eat)
    bool kin = false;
    for (const Member &o : this->list)
      kin = kin || (o.id != m.id && o.exhibit == m.exhibit && o.type == m.type);
    int chase = kin ? t.chaseAnimalChance : 0;
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
        if (gx >= 0 && this->set(m, "beat")) {
          m.stack.clear();
          m.foodItem = -1;
          m.moveAnim = "walk";
          m.speed = t.slow;
          if (walkTo(m, gx, gy, map, fences)) {
            pushSet(m, "beat");
            m.drinkX = bx + 0.5f;
            m.drinkY = by + 0.5f;
          }
        }
      }
    }
  }
}

void Animals::update(float seconds, double simSeconds, const WorldMap &map, const Fences &fences) {
  this->now = simSeconds;
  this->mapRef = &map;
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
    const Type &t = this->typeList[m.type];
    m.animTime += seconds;
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
    // over the prey's gone (no body, no word of it), and its bAfterCaught
    if (m.victim >= 0) {
      m.killIn -= seconds;
      if (m.killIn <= 0) {
        this->kills.push_back(m.victim);
        m.victim = -1;
        m.stack.clear();
        m.stepping = false;
        if (this->set(m, "baftercaught"))
          pushSet(m, "baftercaught");
      }
      continue;
    }
    // Caught up with the animal it's after (on its tile): prey under a dust
    // ball (zoo.exe 0x4a6043); at play, it's over
    if (m.huntAnimal >= 0 && !busy(m)) {
      Member *p = this->member(m.huntAnimal);
      if (p && static_cast<int>(std::floor(p->x)) == static_cast<int>(std::floor(m.x)) &&
          static_cast<int>(std::floor(p->y)) == static_cast<int>(std::floor(m.y)) &&
          std::hypot(p->x - m.x, p->y - m.y) < 0.6f) {
        if (m.tag) {
          m.huntAnimal = -1;
          m.tag = false;
        } else {
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
          // (a swimmer's in the water: its water ball)
          std::string ball = "dustball";
          if (onWater(m) && art(m, "waterball"))
            ball = "waterball";
          float secs = std::max(1.0f, animSeconds(m, ball));
          play(m, ball, Member::Mode::Loop, secs);
          m.killIn = secs;
          playSound(m, "dustball");
          continue;
        }
      }
    }
    // Mauling a guest (bCaughtGuest: 6 s, out of sight - the guest's
    // animation has it); then it stands a while
    if (m.maulFor > 0) {
      m.maulFor -= seconds;
      continue;
    }
    // Caught up with its prey
    if (m.prey >= 0 && this->preyAt && !m.tranquilised && !m.boxed) {
      float px = 0, py = 0;
      if (this->preyAt(m.prey, px, py) && std::hypot(px - m.x, py - m.y) < 0.6f) {
        if (this->caughtPrey)
          this->caughtPrey(m.prey, m);
        m.prey = -1;
        m.maulFor = 6.0f;
        m.path.clear();
        m.pathAt = 0;
        m.stack.clear();
        play(m, "stand", Member::Mode::Loop, 6.0f);
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
      float step = m.speed * seconds;
      if (dist > 0.0001f) {
        m.fx = dx / dist;
        m.fy = dy / dist;
      }
      if (dist <= step) {
        m.x = tx;
        m.y = ty;
        if (static_cast<int>(m.pathAt) == m.crossAt) {
          // (landed: on at its run)
          m.crossAt = -1;
          m.anim = m.moveAnim;
          m.speed = t.fast;
        }
        m.pathAt++;
      } else {
        m.x += dx / dist * step;
        m.y += dy / dist * step;
      }
      if (m.pathAt >= m.path.size()) {
        m.path.clear();
        m.pathAt = 0;
        m.stepping = false;
        // At a shelter or toy: in it (hidden) for cTimeInside ticks, or its
        // play set there
        if (m.building >= 0 && !m.inside && this->objects) {
          for (const PlacedObjects::Object &o : this->objects->objects())
            if (o.id == m.building) {
              const ObjectInfo &info = infoOf(PlacedObjects::fileOf(o));
              std::string playSet = "b" + o.typeName + "animalrest0";
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
    if (m.stepping && m.anim == "eat" && m.foodItem >= 0 && this->items) {
      // A mouthful (cKeeperFoodUnitsEaten) of the food it's at
      float got = this->items->eat(m.foodItem, static_cast<float>(t.foodEaten));
      if (got > 0) {
        m.noFoodSeen = false;
        m.hunger = std::max(0.0f, m.hunger - got * t.foodUnitValue); // (cFoodUnitValue)
        m.food = std::clamp(100.0f - m.hunger, 0.0f, 100.0f);
        m.lastAte = this->now;
      } else {
        m.foodItem = -1;
      }
    }
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
    Animation *a = m.boxed && this->crateArt ? this->crateArt : this->art(m, m.anim);
    if (!a)
      continue;
    // Inside a shelter: not seen
    if ((m.inside && m.insideFor > 0) || m.maulFor > 0)
      continue;
    // Held (Move Animal): its likeness at the cursor, green where it can go
    if (m.id == this->carried) {
      float sx, sy, depth;
      view.worldToScreenF(m.x, m.y, groundAt(map, m.x, m.y), sx, sy, depth);
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
    view.worldToScreenF(m.x, m.y, groundAt(map, m.x, m.y), sx, sy, depth);
    // (the fight's dust ball isn't lit; the one caught is inside it)
    bool lit = m.id == this->hovered && m.victim < 0;
    if (m.caughtBy >= 0)
      depth -= 0.01f;
    // (one-view art - the dust ball - drawn as it is, whichever way it faces)
    CompassDirection side = facing(view, m);
    if (!a->hasFrames(side))
      side = CompassDirection::N;
    Fences::Drawable d{depth, sx, sy, a, side, SDL_Color{255, 255, 110, 255}, lit};
    d.frame = f;
    out.push_back(d);
    // Its smiles and frowns, over it (16 frames, 83 ms each)
    const float faceY = sy - this->typeList[m.type].facesYOffset;
    for (const auto &[smile, t] : m.faces)
      if (Animation *fa = smile ? this->smileArt : this->frownArt) {
        Fences::Drawable fd{depth + 0.00005f, sx, faceY, fa, CompassDirection::N,
                            SDL_Color{255, 255, 255, 255}, false};
        fd.frame = std::min(static_cast<int>(t / 83.0f), std::max(1, fa->frameCount()) - 1);
        out.push_back(fd);
      }
    if (m.id == this->selected && this->selectArrow) {
      int w = 0, h = 0;
      a->queryTexture(CompassDirection::S, &w, &h);
      out.push_back({depth + 0.0001f, sx, sy - std::max(36, h), this->selectArrow,
                     CompassDirection::N, SDL_Color{255, 255, 255, 255}, false});
    }
  }
  // The one being adopted: its likeness at the cursor, green where it can
  // go, red where not
  if (this->previewType >= 0)
    if (Animation *a = this->preview(this->previewType, this->previewFemale)) {
      float sx, sy, depth;
      view.worldToScreenF(this->previewX, this->previewY,
                          groundAt(map, this->previewX, this->previewY), sx, sy, depth);
      SDL_Color tint = this->previewFit == Fences::Fit::Ok ? SDL_Color{0, 255, 0, 255}
                                                           : SDL_Color{255, 60, 60, 255};
      out.push_back({depth, sx, sy, a, CompassDirection::SE, tint, true});
    }
}

int Animals::pick(float px, float py, const WorldRenderer &view, const WorldMap &map) const {
  int found = -1;
  float best = 1e9f;
  for (const Member &m : this->list) {
    if (m.id == this->carried)
      continue;
    float sx, sy, depth;
    view.worldToScreenF(m.x, m.y, groundAt(map, m.x, m.y), sx, sy, depth);
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
  for (auto &[id, e] : this->evaluations)
    if (exhibit < 0 || id == exhibit)
      e.dirty = true;
}

void Animals::evaluate(int exhibit, const WorldMap &map, const Fences &fences) {
  Evaluation &ev = this->evaluations[exhibit];
  ev.dirty = false;
  ev.timerMs = static_cast<float>(std::uniform_int_distribution<int>(0, 199)(this->rng));
  ev.kinds.clear();
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex || ex->tiles.empty())
    return;
  const int N = static_cast<int>(ex->tiles.size());
  ev.tiles = N;
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
      if (!map.isPath(tx, ty) && tile->terrainType < 18)
        r.count[tile->terrainType]++;
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

int Animals::suitability(int id, const WorldMap &map, const Fences &fences) {
  const Member *m = this->member(id);
  const Rating *r = m ? this->rating(*m, map, fences) : nullptr;
  return r ? static_cast<int>(std::ceil(std::max(0.0f, r->score))) : 0;
}

int Animals::exhibitSuitability(int exhibit) const {
  auto ev = this->evaluations.find(exhibit);
  if (ev == this->evaluations.end())
    return 0;
  float sum = 0;
  int n = 0;
  for (const Member &m : this->list) {
    if (m.exhibit != exhibit)
      continue;
    auto k = ev->second.kinds.find(m.type);
    sum += k == ev->second.kinds.end() ? 0.0f : std::ceil(std::max(0.0f, k->second.score));
    n++;
  }
  return n ? static_cast<int>(sum / n) : 0;
}

void Animals::company(const Member &m, int &kind, int &kindAdults, int &adults) const {
  kind = kindAdults = adults = 0;
  for (const Member &o : this->list) {
    if (o.exhibit != m.exhibit || o.id == this->carried)
      continue;
    if (o.type == m.type) {
      kind++;
      kindAdults += o.baby ? 0 : 1;
    }
    adults += o.baby ? 0 : 1;
  }
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
//   adults, cAllCrowdedChange;
// - the others (every 40): one of them very angry, cOtherAnimalAngryChange;
// - hunger (every 30): cHungerIncrement hungrier; hungry with no food,
//   cNoFoodChange.
// ----------------------------------------------------------------------------
void Animals::tick(Member &m, const WorldMap &map, const Fences &fences) {
  const Type &t = this->typeList[m.type];
  m.happiness = std::clamp(m.happiness + m.pending, -100.0f, 100.0f);
  m.pending = 0;
  if (--m.habitatCheck < 0) {
    m.habitatCheck = kHabitatCheck - 1;
    int s = this->suitability(m.id, map, fences);
    if (s >= t.habitatPreference)
      m.pending += t.happyHabitatChange;
    else if (s * 100 >= t.habitatPreference * t.pctHabitat)
      m.pending += t.angryHabitatChange;
    else
      m.pending += t.veryAngryHabitatChange;
    if (const Rating *r = this->rating(m, map, fences))
      m.pending += std::trunc(std::clamp(std::trunc(r->compat), -500.0f, 150.0f) / 10.0f);
  }
  if (--m.captivityCheck < 0) {
    m.captivityCheck = kCaptivityCheck - 1;
    m.pending -= t.captivity;
  }
  if (--m.socialCheck < 0) {
    m.socialCheck = kSocialCheck - 1;
    int kind, kindAdults, adults;
    company(m, kind, kindAdults, adults);
    int tiles = fences.exhibit(m.exhibit) ? static_cast<int>(fences.exhibit(m.exhibit)->tiles.size()) : 0;
    if (kind < t.numberMin)
      m.pending += t.numberMinChange;
    else if (kindAdults > t.numberMax * std::max(1, tiles / t.habitatSize))
      m.pending += t.numberMaxChange;
    if (adults > 0 && tiles / adults < t.animalDensity)
      m.pending += t.allCrowdedChange;
  }
  if (--m.otherCheck < 0) {
    m.otherCheck = kOtherCheck - 1;
    if (t.otherAnimalAngryChange < 0)
      for (const Member &o : this->list)
        if (o.id != m.id && o.exhibit == m.exhibit && o.happiness <= -81) {
          m.pending += t.otherAnimalAngryChange;
          break;
        }
  }
  // Hunger (zoo.exe 0x438a79): hungrier each check; hungry with no food,
  // the first time nothing, after that "%s can't find any food." and, at
  // 100, unhappier and less healthy. And the dung meter: full, a pile.
  if (--m.hungerCheck < 0) {
    m.hungerCheck = kHungerCheck - 1;
    m.hunger = std::min(100.0f, m.hunger + t.hungerIncrement);
    if (m.hunger >= t.hungerThreshold && !m.escaped && nearestFood(m) < 0) {
      if (!m.noFoodSeen) {
        m.noFoodSeen = true;
      } else if (m.hunger >= 100) {
        m.pending += t.noFoodChange;
        m.health = std::max(0.0f, m.health - t.hungryHealthChange);
      }
    }
    m.dirty += t.dirtyIncrement;
    // (loose too: on the zoo's grounds, for the maintenance workers)
    if (m.dirty >= t.dirtyThreshold && this->items && (m.exhibit >= 0 || (m.escaped && !m.boxed))) {
      this->items->add(ZooItems::Kind::Dung, m.exhibit, m.x, m.y);
      m.dirty = 0;
      if (m.exhibit >= 0)
        this->markDirty(m.exhibit);
    }
  }
  // Health (0x4389aa): at 20% (cPctHits) or less, unhappy (cSickChange);
  // others sick, unhappier and maybe caught it; now and then ill anyway
  if (--m.healthCheck < 0) {
    m.healthCheck = 29;
    if (m.health <= 0) {
      m.sick = true;
      m.health = 0;
      m.starve = m.hunger >= 100 ? m.starve + 1 : 0;
    } else {
      m.starve = 0;
    }
    if (m.health * 100 <= t.pctHits * t.maxHits)
      m.pending += t.sickChange;
    bool othersSick = false;
    for (const Member &o : this->list)
      othersSick = othersSick || (o.id != m.id && o.exhibit == m.exhibit && o.exhibit >= 0 && o.sick);
    std::uniform_int_distribution<int> pct(0, 99);
    auto say = [&](int sid) {
      if (!this->rm)
        return;
      std::string s = this->rm->getString(sid);
      size_t at = s.find("%s");
      if (at != std::string::npos)
        s.replace(at, 2, m.name);
      this->notices.push_back({s, 2, m.id}); // (ill: urgent)
    };
    if (othersSick) {
      m.pending += t.otherSickChange;
      if (!m.sick && pct(this->rng) < t.sickChance && pct(this->rng) < 4) {
        m.sick = true;
        m.health = 0;
        say(15001); // "%s has caught an illness from another animal."
      }
    } else if (!m.sick && pct(this->rng) < 10 && pct(this->rng) < 1) {
      m.sick = true;
      m.health = 0;
      say(15000); // "%s is ill."
    }
  }
  // Tiredness (0x4384ba)
  if (--m.energyCheck < 0) {
    m.energyCheck = 29;
    if (m.energy < t.energyThreshold)
      m.energy += t.energyIncrement;
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
    }
    // A happy grown male with a grown female of his kind: a baby
    if (!m.female && !m.baby && m.exhibit >= 0 && t.reproductionChance > 0 &&
        m.ready >= t.reproductionInterval && m.happiness >= t.happyReproduceThreshold) {
      for (Member &mate : this->list)
        if (mate.female && !mate.baby && mate.type == m.type && mate.exhibit == m.exhibit &&
            pct(this->rng) < t.reproductionChance) {
          for (int i = 0; i < t.offspring; i++)
            this->births.push_back({mate.id, m.type});
          m.ready = 0;
          break;
        }
    }
  }
  waterTick(m, map, fences);
  // In its exhibit (zoo.exe 0x438514, every cEscapedCheck 2 ticks): prey run
  // from an adult that hunts them within 7 (or from one chasing them at
  // play); an adult hunter goes after the nearest animal it lists within 5
  // (cPreyRadius) - no chance, no hunger - unless one that hunts it is near
  if (!busy(m)) {
    if (--m.huntCheck <= 0) {
      m.huntCheck = 2;
      const Member *threat = m.sleeping ? nullptr : threatTo(m, 7.0f);
      if (threat) {
        bool fresh = m.fleeFrom != threat->id || m.pathAt >= m.path.size();
        m.fleeFrom = threat->id;
        m.huntAnimal = -1;
        if (fresh)
          runFrom(m, *threat, map, fences);
      } else {
        m.fleeFrom = -1;
      }
      if (m.fleeFrom < 0 && (m.huntAnimal < 0 || m.tag) && !m.baby && !t.preyIds[sub(m)].empty()) {
        const Member *best = nullptr;
        float bestD = 1e30f;
        for (const Member &o : this->list) {
          if (o.exhibit != m.exhibit || o.boxed || o.dying || o.caughtBy >= 0 || o.id == this->carried ||
              o.claimedBy >= 0 || !hunts(m, o))
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
            m.huntStart = this->now;
          m.huntAnimal = best->id;
          m.tag = false;
        }
      }
    }
    // Running: on to another spot away when it gets there
    if (m.fleeFrom >= 0 && m.pathAt >= m.path.size())
      if (const Member *threat = this->member(m.fleeFrom))
        runFrom(m, *threat, map, fences);
    if (m.huntAnimal >= 0) {
      const Member *p = this->member(m.huntAnimal);
      // (gone, crated, out of the exhibit; at play, cChaseTimeOut 10 s)
      if (!p || p->boxed || p->exhibit != m.exhibit || p->caughtBy >= 0 ||
          (m.tag && this->now - m.huntStart > 10.0))
        m.huntAnimal = -1;
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
  if (t.eatsPeople && m.escaped && !m.boxed && !m.baby && !m.tranquilised && !m.dying && m.maulFor <= 0) {
    if (m.prey >= 0) {
      float px = 0, py = 0;
      if (!this->preyAt || !this->preyAt(m.prey, px, py) || this->now - m.chaseStart > 10.0) {
        m.prey = -1;
      } else {
        m.stack.clear();
        m.moveAnim = "run";
        m.speed = t.fast;
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
        m.speed = t.fast;
        walkTo(m, px, py, map, fences);
      }
    }
  }
  // A shelter or toy now and then (zoo.exe 0x438f20: every 20 ticks, a
  // cBuildingUseChance chance)
  if (--m.buildingCheck < 0) {
    m.buildingCheck = 19;
    if (m.building < 0 && !m.escaped && !m.sleeping && t.buildingUseChance > 0 &&
        std::uniform_int_distribution<int>(0, 99)(this->rng) < t.buildingUseChance)
      useShelter(m, map, fences);
  }
  // Very unhappy: "%s is not happy." (every 10 ticks)
  if (--m.notHappyCheck < 0) {
    m.notHappyCheck = 9;
    if (m.happiness <= -61 && this->rm) {
      std::string s = this->rm->getString(10028);
      size_t at = s.find("%s");
      if (at != std::string::npos)
        s.replace(at, 2, m.name);
      this->notices.push_back({s, 2, m.id}); // (red in the original's list)
    }
  }
}

void Animals::lifeEvents(const WorldMap &map, const Fences &fences) {
  (void)map;
  auto say = [&](int sid, const std::string &name, int kind, int animal) {
    if (!this->rm)
      return;
    std::string s = this->rm->getString(sid);
    size_t at = s.find("%s");
    if (at != std::string::npos)
      s.replace(at, 2, name);
    this->notices.push_back({s, kind, animal});
  };
  // Born: a young one by its mother ("Congratulations! %s has given birth.")
  std::vector<std::pair<int, int>> births;
  births.swap(this->births);
  for (auto [mother, kind] : births) {
    const Member *mum = this->member(mother);
    if (!mum)
      continue;
    Member b = *mum;
    const Type &t = this->typeList[kind];
    b.id = this->nextId++;
    b.baby = true;
    b.female = std::uniform_int_distribution<int>(0, 1)(this->rng) == 1;
    b.name = (this->rm ? this->rm->getString(t.nameId) : t.key) + " " + std::to_string(++this->counts[kind]);
    b.x += 0.3f;
    b.happiness = static_cast<float>(std::clamp(t.initialHappiness, -100, 100));
    b.health = static_cast<float>(t.maxHits);
    b.hunger = b.energy = b.dirty = 0;
    b.grow = 0;
    b.life = t.timeDeath - 1;
    b.stack.clear();
    b.path.clear();
    b.faces.clear();
    b.sick = false;
    this->list.push_back(b);
    say(10032, mum->name, 1, mum->id); // (good news)
    this->markDirty(b.exhibit);
  }
  // Died of old age ("%s has died of old age."); the others in its exhibit
  // grieve (cDeathChange -20)
  for (size_t i = 0; i < this->list.size();) {
    Member &m = this->list[i];
    if (!m.dying || (m.dieAfter && !m.stack.empty())) {
      i++;
      continue;
    }
    int ex = m.exhibit;
    say(10242, m.name, 2, -1);
    for (Member &o : this->list)
      if (o.id != m.id && o.exhibit == ex)
        o.pending -= 20;
    this->remove(m.id);
  }
  (void)fences;
}

// Off to a shelter or toy in its exhibit (its [Satisfies] animalrest), the
// nearest with room; inside (cHideUser) for cTimeInside ticks, or (a toy)
// its "b<type>animalrest0" set played there
bool Animals::useShelter(Member &m, const WorldMap &map, const Fences &fences) {
  if (!this->objects)
    return false;
  const PlacedObjects::Object *best = nullptr;
  float bestD = 1e9f;
  for (const PlacedObjects::Object &o : this->objects->objects()) {
    if (o.fence || fences.exhibitAt(static_cast<int>(o.x), static_cast<int>(o.y)) != m.exhibit)
      continue;
    const ObjectInfo &info = infoOf(PlacedObjects::fileOf(o));
    if (!info.rest)
      continue;
    int users = 0;
    for (const Member &a : this->list)
      users += a.building == o.id ? 1 : 0;
    if (users >= std::max(1, info.capacity))
      continue;
    float d = (o.x - m.x) * (o.x - m.x) + (o.y - m.y) * (o.y - m.y);
    if (d < bestD) {
      bestD = d;
      best = &o;
    }
  }
  if (!best)
    return false;
  // To beside it: an open tile round its footprint, its door side (in
  // front) first, then the nearest
  const ObjectInfo &bi = infoOf(PlacedObjects::fileOf(*best));
  float hx = bi.fx / 4.0f, hy = bi.fy / 4.0f;
  int x0 = static_cast<int>(std::floor(best->x - hx)) - 1, x1 = static_cast<int>(std::floor(best->x + hx - 0.01f)) + 1;
  int y0 = static_cast<int>(std::floor(best->y - hy)) - 1, y1 = static_cast<int>(std::floor(best->y + hy - 0.01f)) + 1;
  const Type &t = this->typeList[m.type];
  std::vector<std::pair<float, std::pair<float, float>>> spots;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      if ((y != y0 && y != y1 && x != x0 && x != x1) || !standable(x, y, t, m.exhibit, map, fences))
        continue;
      float score = (y == y1 ? 0.0f : 100.0f) + (x + 0.5f - m.x) * (x + 0.5f - m.x) + (y + 0.5f - m.y) * (y + 0.5f - m.y);
      spots.push_back({score, {x + 0.5f, y + 0.5f}});
    }
  std::sort(spots.begin(), spots.end());
  m.moveAnim = "walk";
  m.speed = t.slow;
  m.stack.clear();
  bool going = false;
  for (const auto &sp : spots)
    if (walkTo(m, sp.second.first, sp.second.second, map, fences)) {
      going = true;
      break;
    }
  if (!going)
    return false;
  m.building = best->id;
  return true;
}

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
  m.inside = false;
  m.insideFor = 0;
}

void Animals::keeperArrives(int exhibit) {
  for (Member &m : this->list)
    if (m.exhibit == exhibit && !m.boxed)
      m.pending += this->typeList[m.type].keeperArrivesChange;
}

void Animals::heal(int id) {
  if (Member *m = this->member(id)) {
    m->health = static_cast<float>(this->typeList[m->type].maxHits);
    m->sick = false;
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
  int kind, kindAdults, adults;
  company(*m, kind, kindAdults, adults);
  int tiles = fences.exhibit(m->exhibit) ? static_cast<int>(fences.exhibit(m->exhibit)->tiles.size()) : 0;
  bool few = kind < t.numberMin;
  flag(4, few, t.numberMinChange < -5);
  flag(5, !few && kindAdults > t.numberMax * std::max(1, tiles / t.habitatSize), t.numberMaxChange < -5);
  float compat = r->compat / 10.0f;
  flag(10, compat < 0, compat <= -10);
  // Trees and rocks: 4 points either way, 8 for red; hills 10 and 20
  auto prefs = [&](int lowBit, float have, int want, float near, float far) {
    bool tooFew = want - near > have || (have == 0 && want > 0);
    flag(lowBit, tooFew, std::fabs(have - want) > far);
    flag(lowBit + 1, !tooFew && want + near < have, std::fabs(have - want) > far);
  };
  prefs(12, r->foliage, t.treePref, 4, 8);
  flag(16, adults > 0 && tiles / adults < t.animalDensity, t.allCrowdedChange < -5);
  prefs(17, r->hills, t.elevationPref, 10, 20);
  prefs(19, r->rockShare, t.rockPref, 4, 8);
  for (int k = 0; k < 17; k++) {
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
void Animals::snapshot(int exhibit, const WorldMap &map, const Fences &fences) {
  if (exhibit < 0)
    return;
  this->evaluate(exhibit, map, fences);
  std::map<int, float> &snap = this->snaps[exhibit];
  snap.clear();
  for (const auto &[kind, r] : this->evaluations[exhibit].kinds)
    snap[kind] = total(r);
}

void Animals::face(int exhibit, int kind, bool smile) {
  for (Member &m : this->list)
    if (m.exhibit == exhibit && m.type == kind && m.id != this->carried)
      m.faces.push_back({smile, 0.0f});
}

void Animals::react(int exhibit, Change change, const std::string &file, int animalType, bool removed,
                    const WorldMap &map, const Fences &fences) {
  if (exhibit < 0 || this->paused)
    return;
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex)
    return;
  this->evaluate(exhibit, map, fences);
  std::map<int, float> &snap = this->snaps[exhibit];
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
