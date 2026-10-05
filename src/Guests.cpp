#include "Guests.hpp"
#include "Research.hpp"

#include <algorithm>
#include <cmath>

#include "Animals.hpp"
#include "PlacedObjects.hpp"
#include "ZooItems.hpp"
#include "IniReader.hpp"
#include "Pathfinder.hpp"
#include "PalletManager.hpp"
#include "ResourceManager.hpp"
#include "Sound.hpp"
#include "Utils.hpp"
#include "WorldIndex.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"

namespace {
// zoo.exe and guests.ai (Updates\guests7.ztd)
constexpr float kSpawnEveryMs = 2000.0f;    // economy.cfg [checks] newguest
constexpr double kAdmissionMultiple = 3.5;  // cAdmissionMultiple
constexpr float kTickMs = 1000.0f;          // [AI] mStatusPeriod
constexpr int kViewCheck = 20, kLeaveCheck = 20, kNeedCheck = 30, kChaseCheck = 5;
constexpr int kMaxGuests = 1000;            // [AI] maxGuests
constexpr float kPredatorRadius = 7.0f;     // cPredatorRadius (tiles)
// Thoughts (lang strings)
constexpr int kHungryNoFood = 10081, kThirstyNoDrink = 10083, kNoRestroom = 10086, kTired = 10092;
constexpr int kOverjoyed = 10098, kVeryAngry = 10099, kSpecies = 10100, kCrowded = 10102;
constexpr int kLeaveLow = 10087, kLeaveMed = 10088, kLeaveHigh = 10089, kSeenAll = 10238;
constexpr int kRun = 10245, kGoodValue = 10017;

std::string lower(std::string s) { return Utils::string_to_lower(s); }
std::string stripAni(std::string p) {
  p = lower(p);
  size_t dot = p.rfind(".ani");
  return dot == std::string::npos ? p : p.substr(0, dot);
}
} // namespace

void Guests::load(ResourceManager *rm) {
  this->rm = rm;
  if (!rm || !this->typeList.empty())
    return;
  IniReader *cfg = rm->getIniReader("guests.cfg");
  IniReader *ai = rm->getIniReader("guests/guests.ai");
  if (!ai) {
    delete cfg;
    return;
  }
  std::vector<std::string> keys = {"man", "woman", "girl", "boy"};
  for (const std::string &key : keys) {
    Type t;
    t.key = key;
    t.kind = key == "man" ? Kind::Man : key == "woman" ? Kind::Woman : key == "boy" ? Kind::Boy : Kind::Girl;
    t.female = key == "woman" || key == "girl";
    t.child = key == "boy" || key == "girl";
    std::string ints = key + "/characteristics/integers";
    t.nameId = ai->getInt(ints, "cnameid", ai->getInt("man/characteristics/integers", "cnameid", 0));
    t.speed = ai->getInt(ints, "cslowrate", t.child ? 26 : 30) / 60.0f;
    t.runSpeed = (t.child ? 44 : 56) / 60.0f; // fMove(..,56) adults, 44 children
    // Animations: "walk = walk" (guests/<key>/walk/walk) or a whole path
    std::map<std::string, std::string> names = ai->getSection(key + "/animations");
    if (names.empty())
      names = ai->getSection("man/animations");
    for (const auto &kv : names) {
      std::string v = lower(kv.second);
      v = v.substr(0, v.find(';'));
      while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
        v.pop_back();
      if (v.empty())
        continue;
      t.anims[lower(kv.first)] = v.find('/') != std::string::npos ? stripAni(v) : "guests/" + key + "/" + v + "/" + v;
    }
    // Colours: the full palette, then shirt, pants or skirt, hair, skin
    std::string cr = lower(ai->get(key + "/colorrep", "color"));
    t.fullPal = lower(ai->get(cr, "fullpal"));
    std::vector<std::string> repl = ai->getList(key + "/colorrep", "replace");
    for (size_t i = 0; i < repl.size() && i < 4; i++) {
      std::string list = lower(repl[i]);
      // (adults' hair from their own list, as guests7.ai)
      if (list == "cr_hair" && !t.child && !ai->getList("cr_hair_adult", "pal").empty())
        list = "cr_hair_adult";
      for (std::string p : ai->getList(list, "pal"))
        t.slots[i].push_back(lower(p));
    }
    if (t.kind == Kind::Woman) {
      t.gawkWeights[0] = 40, t.gawkWeights[1] = 20, t.gawkWeights[2] = 40;
    } else if (t.child) {
      t.gawkWeights[0] = 40, t.gawkWeights[1] = 40, t.gawkWeights[2] = 20;
    }
    this->typeList.push_back(t);
  }
  // Their [Sounds]: name -> file and attenuation
  for (const auto &kv : ai->getSection("sounds")) {
    std::string v = kv.second;
    size_t semi = v.find(';');
    this->soundFile[lower(kv.first)] = lower(v.substr(0, semi));
    this->soundAtten[lower(kv.first)] = semi == std::string::npos ? 0 : std::atoi(v.substr(semi + 1).c_str());
  }
  // Mauled by a predator: [BehaviorSetCaughtBy<animal><m|f>] f =
  // fPlayWithSound(<anim>, <sound>) - the sound each plays
  for (const std::string &sec : ai->getSections()) {
    std::string low = lower(sec);
    const std::string pre = "behaviorset\bcaughtby";
    if (low.rfind(pre, 0) != 0)
      continue;
    std::string key = low.substr(pre.size());
    for (const std::string &f : ai->getList(sec, "f")) {
      std::string fl = lower(f);
      size_t a = fl.find("fplaywithsound(");
      if (a == std::string::npos)
        continue;
      size_t comma = fl.find(',', a), close = fl.find(')', a);
      if (comma == std::string::npos || close == std::string::npos)
        continue;
      std::string snd = fl.substr(comma + 1, close - comma - 1);
      snd.erase(0, snd.find_first_not_of(" 	"));
      snd.erase(snd.find_last_not_of(" 	") + 1);
      this->caughtSound[key] = snd;
      break;
    }
  }
  delete ai;
  delete cfg;
  if (rm->hasResource("ui/select/selsmall/selsmall.ani"))
    this->selectArrow = rm->getAnimation("ui/select/selsmall/selsmall");
  SDL_Log("[Guests] %zu guest types", this->typeList.size());
}

void Guests::clear() {
  this->list.clear();
  this->nextId = 1;
  this->income = 0;
  this->arrivals = 0;
  Sound::get().stop(this->bedChannel);
  this->bedChannel = -1;
  this->bedLevel = -1;
}

const Guests::Guest *Guests::guest(int id) const {
  for (const Guest &g : this->list)
    if (g.id == id)
      return &g;
  return nullptr;
}

int Guests::averageHappiness() const {
  if (this->list.empty())
    return 0;
  float sum = 0;
  for (const Guest &g : this->list)
    sum += g.happiness;
  return static_cast<int>((sum / this->list.size() + 100) / 2);
}

// ----------------------------------------------------------------------------
// Art: each guest's animations in its own colours
// ----------------------------------------------------------------------------
Animation *Guests::art(const Guest &g, const std::string &name) const {
  const Type &t = this->typeList[g.type];
  auto it = t.anims.find(name);
  if (it == t.anims.end())
    it = t.anims.find("stand");
  if (it == t.anims.end())
    return nullptr;
  const std::string &path = it->second;
  std::string key = path;
  for (int i = 0; i < 4; i++)
    key += "#" + std::to_string(g.colours[i]);
  auto c = this->artCache.find(key);
  if (c != this->artCache.end())
    return c->second;
  Animation *a = nullptr;
  if (this->rm && this->rm->hasResource(path + ".ani")) {
    PalletManager *pm = this->rm->getPalletManager();
    std::string full = t.fullPal;
    Pallet *base = full.empty() ? nullptr : pm->getPallet(full);
    if (base) {
      // The full palette with its four 16-colour ranges (192, 208, 224,
      // 240) from the guest's shirt, pants or skirt, hair and skin
      Pallet variant = *base;
      for (int s = 0; s < 4; s++) {
        if (t.slots[s].empty())
          continue;
        std::string name = t.slots[s][g.colours[s] % t.slots[s].size()];
        Pallet *p = pm->getPallet(name);
        for (int i = 0; p && i < 16; i++)
          if (p->color_count > static_cast<uint32_t>(i + 1))
            variant.colors[192 + s * 16 + i] = p->colors[i + 1];
      }
      std::string name2 = t.fullPal + "#" + key;
      pm->addPallet(name2, variant);
      PalletManager::setOverride(t.fullPal, name2);
      a = this->rm->getAnimation(path);
      PalletManager::clearOverrides();
    } else {
      a = this->rm->getAnimation(path);
    }
  }
  this->artCache[key] = a;
  return a;
}

Animation *Guests::listIcon(const Guest &g) const {
  const Type &t = this->typeList[g.type];
  static const char *icons[4] = {"guests/lsmguest/lsmguest", "guests/lsfguest/lsfguest",
                                 "guests/lsbguest/lsbguest", "guests/lsgguest/lsgguest"};
  std::string path = icons[std::clamp(static_cast<int>(t.kind), 0, 3)];
  std::string key = path;
  for (int i = 0; i < 4; i++)
    key += "#" + std::to_string(g.colours[i]);
  auto c = this->artCache.find(key);
  if (c != this->artCache.end())
    return c->second;
  Animation *a = nullptr;
  if (this->rm && this->rm->hasResource(path + ".ani")) {
    PalletManager *pm = this->rm->getPalletManager();
    std::string own = path + ".pal";
    Pallet *base = pm->getPallet(own);
    if (base) {
      // (its own palette, the guest's shirt, pants or skirt, hair and skin
      // in the same four ranges as its full one)
      Pallet variant = *base;
      for (int s = 0; s < 4; s++) {
        if (t.slots[s].empty())
          continue;
        std::string slot = t.slots[s][g.colours[s] % t.slots[s].size()];
        Pallet *p = pm->getPallet(slot);
        for (int i = 0; p && i < 16; i++)
          if (p->color_count > static_cast<uint32_t>(i + 1) && variant.color_count > static_cast<uint32_t>(192 + s * 16 + i))
            variant.colors[192 + s * 16 + i] = p->colors[i + 1];
      }
      std::string name2 = own + "#" + key;
      pm->addPallet(name2, variant);
      PalletManager::setOverride(own, name2);
      a = this->rm->getAnimation(path);
      PalletManager::clearOverrides();
    } else {
      a = this->rm->getAnimation(path);
    }
  }
  this->artCache[key] = a;
  return a;
}

// ----------------------------------------------------------------------------
// Ground: guests keep to the zoo's paths
// ----------------------------------------------------------------------------
float Guests::groundAt(float x, float y) const {
  const MapTile *t = this->map->getTile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  if (!t)
    return 0;
  float fx = x - std::floor(x), fy = y - std::floor(y);
  float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
  float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
  return top * (1 - fy) + bottom * fy;
}

bool Guests::walkable(int x, int y) const {
  return this->map->isPath(x, y) && this->fences->exhibitAt(x, y) < 0;
}

bool Guests::canStep(int x, int y, int nx, int ny) const {
  Fences::Edge e = nx != x ? Fences::Edge{false, std::max(x, nx), y} : Fences::Edge{true, x, std::max(y, ny)};
  if (this->fences->at(e))
    return false;
  const MapTile *a = this->map->getTile(x, y), *b = this->map->getTile(nx, ny);
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

bool Guests::walkTo(Guest &g, int tx, int ty, float speed) {
  int sx = static_cast<int>(std::floor(g.x)), sy = static_cast<int>(std::floor(g.y));
  auto pass = [&](int x, int y) { return walkable(x, y); };
  auto step = [&](int x, int y, int nx, int ny) { return canStep(x, y, nx, ny); };
  std::vector<std::pair<int, int>> tiles;
  if (!Pathfinder::find(this->map->getWidth(), this->map->getHeight(), sx, sy, tx, ty, pass, step, tiles, 20000))
    return false;
  // Along the path, a little to one side (they don't all walk the same line)
  std::uniform_real_distribution<float> lane(0.3f, 0.7f);
  std::vector<std::pair<float, float>> points{{g.x, g.y}};
  for (size_t i = 1; i < tiles.size(); i++)
    points.push_back({tiles[i].first + lane(this->rng), tiles[i].second + lane(this->rng)});
  Pathfinder::smooth(points, pass, step);
  g.path.assign(points.begin() + 1, points.end());
  g.pathAt = 0;
  g.speed = speed;
  if (g.path.empty())
    return false;
  g.anim = speed > this->typeList[g.type].speed * 1.2f ? "run_terror" : "walk";
  g.animTime = 0;
  return true;
}

void Guests::play(Guest &g, const std::string &anim, float seconds) {
  g.anim = anim;
  g.animTime = 0;
  g.playFor = seconds;
  g.path.clear();
  g.pathAt = 0;
}

std::vector<std::string> Guests::thoughtsAbout(int animal, int exhibit, size_t most) const {
  std::vector<std::string> out;
  for (const Thought &t : this->thoughtLog) {
    if (out.size() >= most)
      break;
    if ((animal >= 0 && t.animal == animal) || (animal < 0 && exhibit >= 0 && t.exhibit == exhibit))
      out.push_back(t.text);
  }
  return out;
}

void Guests::think(Guest &g, int stringId, const std::string &arg, int animal, int exhibit) {
  if (g.lastThought == stringId && g.thoughtArg == arg)
    return;
  g.lastThought = stringId;
  g.thoughtArg = arg;
  if (!this->rm)
    return;
  std::string s = this->rm->getString(stringId);
  size_t at = s.find("%s");
  if (at != std::string::npos)
    s.replace(at, 2, arg);
  // (a thought it already has moves to the top: no repeats)
  this->thoughtLog.insert(this->thoughtLog.begin(), {s, g.id, animal, exhibit});
  if (this->thoughtLog.size() > 500)
    this->thoughtLog.resize(500);
  g.thoughts.erase(std::remove(g.thoughts.begin(), g.thoughts.end(), s), g.thoughts.end());
  g.thoughts.insert(g.thoughts.begin(), s);
  if (g.thoughts.size() > 10)
    g.thoughts.resize(10);
}

int Guests::timeInParkText(const Guest &g) const {
  // (a game day is 12 s: under a day just arrived, under a week a short
  // time, else a while; thresholds UNCONFIRMED)
  double t = this->clock - g.arrived;
  return t < 12 ? 3420 : t < 84 ? 3421 : 3422;
}

// ----------------------------------------------------------------------------
// Buildings (their .ai: [Satisfies], [Sells], cDefaultCost, cPriceFactor,
// cCapacity, cTimeInside, cHideUser, c*Change; the item's items/<name>.cfg
// deltas). A stand's takings are its price x cPriceFactor (checked: a $1
// snack machine sale is $2 of Concessions in the original).
// ----------------------------------------------------------------------------
const Guests::BuildingInfo &Guests::infoOf(const std::string &file) const {
  auto it = this->buildingInfo.find(file);
  if (it != this->buildingInfo.end())
    return it->second;
  BuildingInfo b;
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(file)) {
      for (const auto &kv : ai->getSection("satisfies"))
        (void)kv;
      // ([Satisfies] lists bare words: "building", "food" ... read raw)
      std::map<std::string, std::string> raw;
      const std::string ints = "characteristics/integers", floats = "characteristics/floats";
      b.capacity = std::max(1, ai->getInt(ints, "ccapacity", 1));
      b.timeInside = static_cast<float>(ai->getInt(ints, "ctimeinside", 3));
      b.hideUser = ai->getInt(ints, "chideuser", 0) != 0;
      b.adultChange = static_cast<float>(ai->getInt(ints, "cadultchange", 0));
      b.childChange = static_cast<float>(ai->getInt(ints, "cchildchange", 0));
      b.hunger = static_cast<float>(ai->getInt(ints, "chungerchange", 0));
      b.thirst = static_cast<float>(ai->getInt(ints, "cthirstchange", 0));
      b.bathroom = static_cast<float>(ai->getInt(ints, "cbathroomchange", 0));
      b.energy = static_cast<float>(ai->getInt(ints, "cenergychange", 0));
      b.usedThought = ai->getInt(ints, "cusedthought", 0);
      b.fx = std::max(1, ai->getInt(ints, "cfootprintx", 2));
      b.fy = std::max(1, ai->getInt(ints, "cfootprinty", 2));
      std::string price = ai->get(floats, "cdefaultcost"), factor = ai->get(floats, "cpricefactor");
      std::string lo = ai->get(floats, "clowcost"), hi = ai->get(floats, "chighcost");
      b.low = lo.empty() ? 1.0f : static_cast<float>(std::atof(lo.c_str()));
      b.high = hi.empty() ? 1.0f : static_cast<float>(std::atof(hi.c_str()));
      b.price = price.empty() ? 0.0f : static_cast<float>(std::atof(price.c_str()));
      b.priceFactor = factor.empty() ? 1.0f : static_cast<float>(std::atof(factor.c_str()));
      b.useSound = lower(ai->get("usesound", "name"));
      b.useAtten = ai->getInt("usesound", "attenuation", 0);
      delete ai;
    }
  // The bare-word sections ([Satisfies], [Sells]), read from the file text
  if (this->rm) {
    int size = 0;
    if (void *data = this->rm->getFileBytes(file, &size)) {
      std::string text(static_cast<const char *>(data), static_cast<size_t>(size));
      free(data);
      std::string section;
      size_t at = 0;
      while (at < text.size()) {
        size_t nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? text.size() : nl + 1;
        line = line.substr(0, line.find(';'));
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
          line.pop_back();
        size_t s0 = line.find_first_not_of(" \t");
        if (s0 == std::string::npos)
          continue;
        line = lower(line.substr(s0));
        if (line[0] == '[') {
          section = line.substr(1, line.find(']') - 1);
          continue;
        }
        if (line.find('=') != std::string::npos)
          continue;
        if (section == "satisfies")
          b.satisfies.insert(line);
        else if (section == "sells" && b.item.empty())
          b.item = line;
      }
    }
  }
  if (!b.item.empty() && this->rm)
    if (IniReader *it2 = this->rm->getIniReader("items/" + b.item + ".cfg")) {
      const std::string c = "characteristics";
      b.itemAdult = static_cast<float>(it2->getInt(c, "adulthappydelta", 0));
      b.itemChild = static_cast<float>(it2->getInt(c, "childhappydelta", 0));
      b.itemHunger = static_cast<float>(it2->getInt(c, "hungerdelta", 0));
      b.itemThirst = static_cast<float>(it2->getInt(c, "thirstdelta", 0));
      b.itemBathroom = static_cast<float>(it2->getInt(c, "bathroomdelta", 0));
      b.itemTrash = it2->getInt(c, "trashwhenconsumed", 0) != 0;
      b.consumedThought = it2->getInt(c, "consumedthoughtid", 0);
      delete it2;
    }
  return this->buildingInfo[file] = b;
}

// The nearest building for a need it can walk to (in cSearchRadius tiles;
// not full): off to its door (a path tile beside it)
bool Guests::seekBuilding(Guest &g, const std::string &need, int radius) {
  if (!this->objects)
    return false;
  const auto &objs = this->objects->objects();
  int best = -1, bx = -1, by = -1;
  float bestD = 1e9f;
  bool full = false;
  for (size_t i = 0; i < objs.size(); i++) {
    const PlacedObjects::Object &o = objs[i];
    if (o.fence)
      continue;
    const BuildingInfo &b = infoOf(PlacedObjects::fileOf(o));
    if (!b.satisfies.count(need))
      continue;
    float d = std::hypot(o.x - g.x, o.y - g.y);
    if (d > radius)
      continue;
    // (a full trash can: none of its rubbish taken)
    if (need == "trash" && o.typeName == "trshcan" && o.fill >= 24)
      continue;
    int users = 0;
    for (const Guest &o2 : this->list)
      users += o2.building == static_cast<int>(i) ? 1 : 0;
    if (users >= b.capacity) {
      full = true;
      continue;
    }
    // Its door: a path tile touching its footprint
    float hx = b.fx / 4.0f, hy = b.fy / 4.0f;
    int x0 = static_cast<int>(std::floor(o.x - hx)) - 1, x1 = static_cast<int>(std::floor(o.x + hx - 0.01f)) + 1;
    int y0 = static_cast<int>(std::floor(o.y - hy)) - 1, y1 = static_cast<int>(std::floor(o.y + hy - 0.01f)) + 1;
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++)
        if (walkable(x, y) && d < bestD) {
          bestD = d;
          best = static_cast<int>(i);
          bx = x;
          by = y;
        }
  }
  if (best < 0) {
    if (full)
      think(g, 10243); // "%s is too crowded."
    return false;
  }
  if (!walkTo(g, bx, by, this->typeList[g.type].speed))
    return false;
  g.building = best;
  g.need = need;
  g.state = State::Walking;
  g.target = -1;
  return true;
}

// At its door: in for cTimeInside seconds (out of sight if cHideUser),
// then what it does for them and what they bought
void Guests::useBuilding(Guest &g) {
  const auto &objs = this->objects->objects();
  if (g.building < 0 || g.building >= static_cast<int>(objs.size())) {
    g.building = -1;
    return;
  }
  const PlacedObjects::Object &o = objs[g.building];
  const BuildingInfo &b = infoOf(PlacedObjects::fileOf(o));
  float d = std::hypot(o.x - g.x, o.y - g.y);
  if (d > 0.01f) {
    g.fx = (o.x - g.x) / d;
    g.fy = (o.y - g.y) / d;
  }
  if (g.need == "trash") {
    // A bin: the rubbish in (a full one: dropped beside it as litter)
    PlacedObjects::Object &can = this->objects->all()[g.building];
    if (can.typeName != "trshcan" || can.fill <= 24) {
      if (can.typeName == "trshcan")
        can.fill++;
    } else if (this->items) {
      this->items->add(ZooItems::Kind::Litter, -1, g.x, g.y);
    }
    g.trash = false;
    g.building = -1;
    g.need.clear();
    play(g, "stand", 0.5f);
    return;
  }
  g.state = State::Using;
  g.hidden = b.hideUser;
  {
    PlacedObjects::Object &in = this->objects->all()[g.building];
    in.visitorsNow++;
    in.visitorsTotal++;
  }
  play(g, g.need == "energy" ? "stand" : "stand", std::max(0.5f, b.timeInside));
  if (!b.useSound.empty() && this->sounds)
    Sound::get().play(b.useSound, b.useAtten + 600);
}

// ----------------------------------------------------------------------------
// Arrival
// ----------------------------------------------------------------------------
double Guests::admit(double admission) {
  if (!hasEntrance() || this->typeList.empty() || static_cast<int>(this->list.size()) >= kMaxGuests)
    return 0;
  Guest g;
  g.id = this->nextId++;
  g.type = std::uniform_int_distribution<int>(0, static_cast<int>(this->typeList.size()) - 1)(this->rng);
  const Type &t = this->typeList[g.type];
  for (int s = 0; s < 4; s++)
    g.colours[s] = t.slots[s].empty() ? 0 : std::uniform_int_distribution<int>(0, static_cast<int>(t.slots[s].size()) - 1)(this->rng);
  std::uniform_real_distribution<float> in(0.3f, 0.7f);
  g.x = this->entranceX + in(this->rng);
  g.y = this->entranceY + in(this->rng);
  g.happiness = 50; // cInitialHappiness
  g.name = (this->rm ? this->rm->getString(9507) : std::string("Guest")) + " " + std::to_string(g.id);
  g.arrived = this->clock;
  // Every other guest starts hungry or thirsty (cRandomStatNth 2: 41-49)
  if (std::uniform_int_distribution<int>(0, 1)(this->rng) == 0) {
    int k = std::uniform_int_distribution<int>(0, 3)(this->rng);
    float v = static_cast<float>(std::uniform_int_distribution<int>(41, 49)(this->rng));
    if (k == 0)
      g.hunger = v;
    else if (k == 1)
      g.thirst = v;
  }
  // Each check's countdown starts somewhere in its period
  auto start = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(this->rng); };
  g.viewCheck = start(kViewCheck);
  g.leaveCheck = start(kLeaveCheck);
  g.hungerCheck = start(kNeedCheck);
  g.thirstCheck = start(kNeedCheck);
  g.bathroomCheck = start(kNeedCheck);
  g.energyCheck = start(kNeedCheck);
  g.chaseCheck = start(kChaseCheck);
  // A favourite animal: any kind (cPreferredAnimal isn't read)
  if (this->animals && !this->animals->types().empty())
    g.favourite = std::uniform_int_distribution<int>(0, static_cast<int>(this->animals->types().size()) - 1)(this->rng);
  double fee = admission * kAdmissionMultiple * (t.child ? 0.5 : 1.0);
  this->list.push_back(g);
  this->income += fee;
  this->arrivals++;
  return fee;
}

// ----------------------------------------------------------------------------
// Where to go next: an exhibit not yet seen (its animals' attractiveness,
// its favourite there +500, the further the better), else a walk; seen
// everything: most leave
// ----------------------------------------------------------------------------
int Guests::pickExhibit(Guest &g, int &vx, int &vy) {
  int best = -1;
  float bestScore = -1e30f;
  for (const Fences::Exhibit &ex : this->fences->exhibits()) {
    if (!ex.named || ex.tank || g.seen.count(ex.id))
      continue;
    float attract = 0;
    bool any = false, favourite = false;
    for (const Animals::Member &m : this->animals->members())
      if (m.exhibit == ex.id) {
        any = true;
        attract += static_cast<float>(this->animals->types()[m.type].attractiveness);
        favourite = favourite || m.type == g.favourite;
      }
    if (!any)
      continue;
    // Its viewing tiles: paths beside it
    std::vector<std::pair<int, int>> view;
    for (auto [x, y] : ex.tiles) {
      const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto &dd : d)
        if (walkable(x + dd[0], y + dd[1]))
          view.push_back({x + dd[0], y + dd[1]});
    }
    if (view.empty())
      continue;
    auto [tx, ty] = view[std::uniform_int_distribution<size_t>(0, view.size() - 1)(this->rng)];
    float d2 = std::min((tx - g.x) * (tx - g.x) + (ty - g.y) * (ty - g.y), 990.0f);
    float score = attract + (favourite ? 500.0f : 0.0f) + 100 - (1000 - d2) / 10;
    if (score > bestScore) {
      bestScore = score;
      best = ex.id;
      vx = tx;
      vy = ty;
    }
  }
  return best;
}

std::set<std::pair<int, int>> Guests::reachableFrom(int x, int y) const {
  std::set<std::pair<int, int>> seen;
  if (!walkable(x, y))
    return seen;
  std::vector<std::pair<int, int>> open = {{x, y}};
  seen.insert({x, y});
  while (!open.empty()) {
    auto [cx, cy] = open.back();
    open.pop_back();
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &dd : d) {
      int nx = cx + dd[0], ny = cy + dd[1];
      if (seen.count({nx, ny}) || !walkable(nx, ny) || !canStep(cx, cy, nx, ny))
        continue;
      seen.insert({nx, ny});
      open.push_back({nx, ny});
    }
  }
  return seen;
}

std::vector<std::pair<int, int>> Guests::viewingTiles(int exhibit) const {
  std::vector<std::pair<int, int>> view;
  const Fences::Exhibit *ex = this->fences ? this->fences->exhibit(exhibit) : nullptr;
  if (!ex)
    return view;
  for (auto [x, y] : ex->tiles) {
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &dd : d) {
      std::pair<int, int> t = {x + dd[0], y + dd[1]};
      if (!ex->tiles.count(t) && walkable(t.first, t.second) &&
          std::find(view.begin(), view.end(), t) == view.end())
        view.push_back(t);
    }
  }
  return view;
}

// (zoo.exe 0x58d79e: not while busy or on another tour, nor over any need's
// threshold; 1 in 10 to the guide's own tile, else a viewing tile near it)
bool Guests::joinTour(int guest, int guide, int exhibit, int tx, int ty) {
  for (Guest &g : this->list) {
    if (g.id != guest)
      continue;
    if (g.guide >= 0 || g.leaving || g.state != State::Walking || g.building >= 0 || g.hidden)
      return false;
    if (g.hunger > 50 || g.thirst > 50 || g.tired > 50 || g.bathroom > 50)
      return false;
    int x = tx, y = ty;
    if (std::uniform_int_distribution<int>(0, 9)(this->rng) != 0) {
      std::vector<std::pair<int, int>> near;
      for (auto [vx, vy] : viewingTiles(exhibit))
        if (std::abs(vx - tx) <= 2 && std::abs(vy - ty) <= 2)
          near.push_back({vx, vy});
      if (!near.empty()) {
        auto p = near[std::uniform_int_distribution<size_t>(0, near.size() - 1)(this->rng)];
        x = p.first;
        y = p.second;
      }
    }
    if (!walkTo(g, x, y, this->typeList[g.type].speed))
      return false;
    g.guide = guide;
    g.tourHeard = false;
    g.target = exhibit;
    g.seen.erase(exhibit);
    return true;
  }
  return false;
}

int Guests::nearestPrey(float x, float y, float radius) const {
  int best = -1;
  float bestD = 1e30f;
  for (const Guest &g : this->list) {
    if (g.x < -999 || g.attacked || g.hidden || g.state == State::Caught || this->typeList[g.type].kind != Kind::Man)
      continue;
    if (std::fabs(g.x - x) > radius || std::fabs(g.y - y) > radius)
      continue;
    float d = (g.x - x) * (g.x - x) + (g.y - y) * (g.y - y);
    if (d < bestD) {
      bestD = d;
      best = g.id;
    }
  }
  return best;
}

bool Guests::preyPosition(int guest, float &x, float &y) const {
  for (const Guest &g : this->list)
    if (g.id == guest) {
      if (g.x < -999 || g.attacked || g.hidden)
        return false;
      x = g.x;
      y = g.y;
      return true;
    }
  return false;
}

void Guests::caught(int guest, const std::string &animalKey, bool female, const std::string &animalName) {
  for (Guest &g : this->list) {
    if (g.id != guest)
      continue;
    g.attacked = true;
    g.state = State::Caught;
    g.caughtStage = 0;
    g.path.clear();
    g.pathAt = 0;
    g.building = -1;
    std::string anim = animalKey + (female ? "f" : "m");
    float secs = 3.0f;
    if (Animation *a = this->art(g, anim))
      if (a->frameTimeMs() > 0 && a->frameCount() > 0)
        secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
    play(g, anim, secs);
    // (its roar: bCaughtBy...'s sound - the cats a soft growl, the bears a roar)
    std::string sound = this->caughtSound.count(anim) ? this->caughtSound[anim] : std::string();
    if (this->sounds && !sound.empty() && this->soundFile.count(sound))
      Sound::get().play(this->soundFile[sound], this->soundAtten[sound] + 600);
    think(g, 10246, animalName);
    return;
  }
}

void Guests::releaseTour(int guide) {
  for (Guest &g : this->list)
    if (g.guide == guide) {
      g.guide = -1;
      g.tourHeard = false;
    }
}

void Guests::tourTalk(int guide, int bonus) {
  for (Guest &g : this->list)
    if (g.guide == guide && !g.tourHeard && g.state == State::Gawking) {
      g.dHappy += static_cast<float>(bonus);
      g.tourHeard = true;
    }
}

void Guests::wander(Guest &g) {
  int sx = static_cast<int>(std::floor(g.x)), sy = static_cast<int>(std::floor(g.y));
  std::uniform_int_distribution<int> off(-12, 12);
  for (int tries = 0; tries < 12; tries++) {
    int x = sx + off(this->rng), y = sy + off(this->rng);
    if (walkable(x, y) && walkTo(g, x, y, this->typeList[g.type].speed))
      return;
  }
  play(g, "stand", 2.0f);
}

void Guests::decide(Guest &g) {
  const Type &t = this->typeList[g.type];
  if (g.leaving) {
    g.state = State::Leaving;
    if (!walkTo(g, this->entranceX, this->entranceY, t.speed))
      g.x = -1000; // (nowhere to walk: gone)
    return;
  }
  g.state = State::Walking;
  // An attraction ("fun" buildings) it hasn't been to, as often as an
  // exhibit it hasn't seen (zoo.exe 0x4282e1)
  if (this->objects) {
    std::vector<int> fun;
    const auto &objs = this->objects->objects();
    for (size_t i = 0; i < objs.size(); i++)
      if (!objs[i].fence && !g.visited.count(objs[i].id) &&
          infoOf(PlacedObjects::fileOf(objs[i])).satisfies.count("fun"))
        fun.push_back(static_cast<int>(i));
    int unseen = 0;
    for (const Fences::Exhibit &e : this->fences->exhibits())
      unseen += e.named && !e.tank && !g.seen.count(e.id) ? 1 : 0;
    if (!fun.empty() &&
        std::uniform_int_distribution<int>(0, unseen + static_cast<int>(fun.size()) - 1)(this->rng) >= unseen) {
      const PlacedObjects::Object &o = objs[fun[std::uniform_int_distribution<size_t>(0, fun.size() - 1)(this->rng)]];
      g.visited.insert(o.id);
      if (seekBuilding(g, "fun", 1000))
        return;
    }
  }
  int vx = -1, vy = -1;
  int ex = pickExhibit(g, vx, vy);
  if (ex >= 0 && walkTo(g, vx, vy, t.speed)) {
    g.target = ex;
    return;
  }
  // Nothing (more) to see
  bool anyExhibit = false;
  for (const Fences::Exhibit &e : this->fences->exhibits())
    anyExhibit = anyExhibit || (e.named && !e.tank);
  if (!g.seen.empty() && ex < 0) {
    g.seen.clear();
    g.visited.clear();
    if (std::uniform_int_distribution<int>(0, 99)(this->rng) < 60) { // cLeaveChanceDone
      think(g, kSeenAll);
      g.leaving = true;
      decide(g);
      return;
    }
  }
  (void)anyExhibit;
  g.target = -1;
  wander(g);
}

// Gawking at an exhibit: longer the happier and more attractive its animals
void Guests::gawk(Guest &g) {
  const Type &t = this->typeList[g.type];
  float sum = 0, attract = 0;
  int n = 0;
  for (const Animals::Member &m : this->animals->members())
    if (m.exhibit == g.target) {
      sum += m.happiness;
      attract += static_cast<float>(this->animals->types()[m.type].attractiveness);
      n++;
    }
  float h = n ? sum / n : 0;
  static const int G[5] = {10, 15, 25, 35, 45}, A[6] = {0, 1, 4, 7, 10, 20};
  std::uniform_int_distribution<int> r6(0, 5);
  float secs = h <= -60 ? G[0] : h <= 20 ? G[1] + r6(this->rng) : h <= 60 ? G[2] + r6(this->rng)
               : h <= 80 ? G[3] + r6(this->rng) : G[4] + r6(this->rng);
  secs += A[attract < 20 ? 0 : attract < 50 ? 1 : attract < 90 ? 2 : attract < 150 ? 3 : attract < 200 ? 4 : 5];
  int total = t.gawkWeights[0] + t.gawkWeights[1] + t.gawkWeights[2];
  int roll = std::uniform_int_distribution<int>(0, total - 1)(this->rng);
  std::string anim = roll < t.gawkWeights[0] ? "gawk1" : roll < t.gawkWeights[0] + t.gawkWeights[1] ? "camera" : "stand";
  // Facing the exhibit
  if (const Fences::Exhibit *ex = this->fences->exhibit(g.target)) {
    float cx = 0, cy = 0;
    for (auto [x, y] : ex->tiles) {
      cx += x + 0.5f;
      cy += y + 0.5f;
    }
    cx /= ex->tiles.size();
    cy /= ex->tiles.size();
    float d = std::hypot(cx - g.x, cy - g.y);
    if (d > 0.01f) {
      g.fx = (cx - g.x) / d;
      g.fy = (cy - g.y) / d;
    }
  }
  play(g, anim, secs);
  g.state = State::Gawking;
  g.seen.insert(g.target);
  // A follower there while its guide talks: cheered by the guide's bonus
  if (g.guide >= 0 && !g.tourHeard && this->guideSpeaking && this->guideSpeaking(g.guide)) {
    g.dHappy += static_cast<float>(this->guideBonusOf ? this->guideBonusOf(g.guide) : 30);
    g.tourHeard = true;
  }
  if (anim == "camera" && this->sounds)
    Sound::get().play("guests/cam1", 1000);
}

// ----------------------------------------------------------------------------
// The status tick
// ----------------------------------------------------------------------------
void Guests::viewing(Guest &g) {
  if (g.state != State::Gawking || g.target < 0)
    return;
  float sum = 0;
  int n = 0;
  std::set<int> kinds;
  for (const Animals::Member &m : this->animals->members())
    if (m.exhibit == g.target) {
      sum += m.happiness;
      kinds.insert(m.type);
      n++;
    }
  if (n == 0)
    return;
  float h = sum / n;
  // (the thought names one of its animals: "%s looks overjoyed")
  std::vector<const Animals::Member *> here;
  for (const Animals::Member &m : this->animals->members())
    if (m.exhibit == g.target)
      here.push_back(&m);
  const Animals::Member *who = here[std::uniform_int_distribution<size_t>(0, here.size() - 1)(this->rng)];
  auto about = [&](int sid) { think(g, sid, who->name, who->id, g.target); };
  if (h >= 81) {
    g.dHappy += 10;
    about(kOverjoyed);
  } else if (h >= 61) {
    g.dHappy += 5;
    about(10281);
  } else if (h >= -60) {
    g.dHappy -= 10;
    about(h >= 0 ? 10282 : 10284);
  } else if (h >= -80) {
    g.dHappy -= 25;
    about(10283);
  } else {
    g.dHappy -= 35;
    about(kVeryAngry);
  }
  if (static_cast<int>(kinds.size()) >= 4) { // cDifferentSpeciesThreshold
    g.dHappy += 5;
    think(g, kSpecies);
  }
  // Crowding: guests by its viewing tiles
  int viewers = 0;
  for (const Guest &o : this->list)
    viewers += o.state == State::Gawking && o.target == g.target ? 1 : 0;
  int tiles = 0;
  if (const Fences::Exhibit *ex = this->fences->exhibit(g.target))
    for (auto [x, y] : ex->tiles) {
      const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto &dd : d)
        tiles += walkable(x + dd[0], y + dd[1]) ? 1 : 0;
    }
  if (tiles > 0 && viewers * 10 / tiles >= 75) { // cCrowdedViewingThreshold
    g.dHappy -= 3;
    think(g, kCrowded);
  }
}

void Guests::tick(Guest &g) {
  // What the last tick's checks found
  g.happiness = std::clamp(g.happiness + g.dHappy, -100.0f, 100.0f);
  g.hunger = std::clamp(g.hunger + g.dHunger, 0.0f, 100.0f);
  g.thirst = std::clamp(g.thirst + g.dThirst, 0.0f, 100.0f);
  g.bathroom = std::clamp(g.bathroom + g.dBathroom, 0.0f, 100.0f);
  g.tired = std::clamp(g.tired + g.dTired, 0.0f, 100.0f);
  g.dHappy = g.dHunger = g.dThirst = g.dBathroom = g.dTired = 0;
  // An escaped animal near: run
  if (--g.chaseCheck < 0) {
    g.chaseCheck = kChaseCheck - 1;
    const Animals::Member *near = nullptr;
    float best = kPredatorRadius;
    // (only those that would eat people: cPrey 9503, grown, not crated)
    for (const Animals::Member &m : this->animals->members())
      if (m.escaped && !m.boxed && !m.baby && this->animals->types()[m.type].eatsPeople) {
        float d = std::hypot(m.x - g.x, m.y - g.y);
        if (d < best) {
          best = d;
          near = &m;
        }
      }
    if (near && g.state != State::Fleeing && g.state != State::Caught) {
      g.dHappy += -100; // cEscapedAnimalChange
      g.sawEscape = true;
      think(g, kRun, near->name);
      g.state = State::Fleeing;
      // Away from it, along the paths
      int sx = static_cast<int>(std::floor(g.x)), sy = static_cast<int>(std::floor(g.y));
      float ax = g.x - near->x, ay = g.y - near->y, ad = std::max(0.01f, std::hypot(ax, ay));
      bool ran = false;
      for (int r = 10; r >= 3 && !ran; r -= 2)
        for (int tries = 0; tries < 6 && !ran; tries++) {
          int x = sx + static_cast<int>(std::lround(ax / ad * r)) + std::uniform_int_distribution<int>(-2, 2)(this->rng);
          int y = sy + static_cast<int>(std::lround(ay / ad * r)) + std::uniform_int_distribution<int>(-2, 2)(this->rng);
          ran = walkable(x, y) && walkTo(g, x, y, this->typeList[g.type].runSpeed);
        }
      const Type &t = this->typeList[g.type];
      std::string scream = t.kind == Kind::Man ? "man_scream" : t.kind == Kind::Woman ? "woman_scream"
                           : t.kind == Kind::Boy ? "boy_scream" : "girl_scream";
      if (this->sounds && this->soundFile.count(scream))
        Sound::get().play(this->soundFile[scream], this->soundAtten[scream] + 800);
      if (!ran) {
        play(g, "jump_terror", 2.0f);
      }
    }
  }
  // Unhappy: may leave
  if (--g.leaveCheck < 0) {
    g.leaveCheck = kLeaveCheck - 1;
    if (!g.leaving) {
      int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
      int h = static_cast<int>(g.happiness);
      if (h <= -20 && h >= -40 && roll < 12) {
        think(g, kLeaveLow);
        g.leaving = true;
      } else if (h < -40 && h >= -80 && roll < 24) {
        think(g, kLeaveMed);
        g.leaving = true;
      } else if (h < -80 && roll < 30) {
        think(g, kLeaveHigh);
        g.leaving = true;
      }
    }
  }
  // Needs: hungrier, thirstier ...; at cThreshold (50) off to the nearest
  // stand, restroom or bench that has room; none: "I'm hungry and can't
  // find any food" and the like
  auto need = [&](int &check, float &delta, float value, int increment, float angry, int wantThought,
                  int noneThought, const char *what) {
    if (--check >= 0)
      return;
    check = kNeedCheck - 1;
    if (g.need != what)
      delta += static_cast<float>(increment);
    if (value >= 100)
      g.dHappy += angry;
    if (value >= 50 && g.building < 0 && !g.leaving && g.state != State::Fleeing) {
      think(g, wantThought);
      if (!seekBuilding(g, what, std::string(what) == "energy" ? 40 : 1000))
        think(g, noneThought);
    }
  };
  need(g.hungerCheck, g.dHunger, g.hunger, 4, -30, 10079, kHungryNoFood, "food");
  need(g.thirstCheck, g.dThirst, g.thirst, 3, -30, 10082, kThirstyNoDrink, "drink");
  need(g.bathroomCheck, g.dBathroom, g.bathroom, 3, -30, 10084, kNoRestroom, "bathroom");
  need(g.energyCheck, g.dTired, g.tired, 3, -15, kTired, 10093, "energy");
  // A souvenir (every cBuySouvenirCheck, 40): happy, a 1 in 10 chance;
  // middling, 1 in 20; then off to a gift shop
  if (--g.souvenirCheck < 0) {
    g.souvenirCheck = 39;
    int h = static_cast<int>((g.happiness + 100) / 2);
    int chance = h >= 71 ? 10 : h >= 31 ? 5 : 0;
    if (!g.souvenir && g.building < 0 && !g.leaving && chance > 0 &&
        std::uniform_int_distribution<int>(0, 99)(this->rng) < chance) {
      think(g, 10090);
      if (!seekBuilding(g, "gift", 1000))
        think(g, 10091);
    }
  }
  // Rubbish in hand: a bin nearby, else dropped as litter (cAngryTrashChange
  // every check while holding it)
  if (--g.trashCheck < 0) {
    g.trashCheck = kNeedCheck - 1;
    if (g.trash) {
      g.dHappy -= 5;
      think(g, 10237);
      if (g.building < 0 && !seekBuilding(g, "trash", 40)) {
        if (this->items)
          this->items->add(ZooItems::Kind::Litter, -1, g.x, g.y);
        g.trash = false;
      }
    }
  }
  if (--g.viewCheck < 0) {
    g.viewCheck = kViewCheck - 1;
    viewing(g);
  }
}

// ----------------------------------------------------------------------------
// Every frame
// ----------------------------------------------------------------------------
void Guests::update(float seconds, int zooRating, double admission) {
  if (!this->map || !this->fences || !this->animals)
    return;
  // Buildings with a guest at them show their "used" art
  if (this->objects) {
    for (PlacedObjects::Object &o : this->objects->all())
      o.inUse = false;
    for (const Guest &g : this->list)
      if (g.state == State::Using && g.building >= 0 && g.building < static_cast<int>(this->objects->all().size()))
        this->objects->all()[g.building].inUse = true;
  }
  // Who comes: the zoo's rating against how much it costs to get in
  this->clock += seconds;
  this->spawnClock += seconds * 1000.0f;
  while (this->spawnClock >= kSpawnEveryMs) {
    this->spawnClock -= kSpawnEveryMs;
    if (!hasEntrance() || this->animals->escapedCount() > 0)
      continue;
    // (none come until an exhibit has animals: zoo.exe 0x424375)
    {
      bool any = false;
      for (const Fences::Exhibit &ex : this->fences->exhibits())
        any = any || ex.animals > 0;
      if (!any)
        continue;
    }
    int cat = admission > 49 ? 0 : admission > 29 ? 1 : admission > 19 ? 2 : admission > 9 ? 3 : 4;
    const int VL = 3, L = 5, M = 25, H = 30, VH = 40;
    static const int table[4][5] = {{VL, M, H, H, VH}, {0, L, M, H, VH}, {0, VL, M, M, H}, {0, 0, VL, L, M}};
    static const bool good[4][5] = {{false, false, false, true, true}, {false, false, false, false, true},
                                    {false, false, false, false, true}, {false, false, false, false, false}};
    int band = zooRating >= 81 ? 0 : zooRating >= 60 ? 1 : zooRating >= 30 ? 2 : 3;
    int chance = table[band][cat];
    if (good[band][cat] && std::uniform_int_distribution<int>(0, 199)(this->rng) == 0)
      this->messages.push_back(kGoodValue);
    if (std::uniform_int_distribution<int>(0, 99)(this->rng) < chance)
      admit(admission);
  }
  for (Guest &g : this->list) {
    g.animTime += seconds;
    g.tickMs += seconds * 1000.0f;
    while (g.tickMs >= kTickMs) {
      g.tickMs -= kTickMs;
      tick(g);
    }
    // Walking
    if (g.pathAt < g.path.size()) {
      auto [tx, ty] = g.path[g.pathAt];
      float dx = tx - g.x, dy = ty - g.y, dist = std::hypot(dx, dy);
      float step = g.speed * seconds;
      if (dist > 0.0001f) {
        g.fx = dx / dist;
        g.fy = dy / dist;
      }
      if (dist <= step) {
        g.x = tx;
        g.y = ty;
        g.pathAt++;
      } else {
        g.x += dx / dist * step;
        g.y += dy / dist * step;
      }
      if (g.pathAt >= g.path.size()) {
        g.path.clear();
        g.pathAt = 0;
        if (g.state == State::Leaving) {
          g.x = -1000; // out of the gate
          // Happy as it goes (over 80): may join the zoo (15%)
          if (g.happiness > 80 && std::uniform_int_distribution<int>(0, 99)(this->rng) < 15)
            this->members++;
        } else if (g.building >= 0 && g.state == State::Walking) {
          useBuilding(g);
        } else if (g.state == State::Walking && g.target >= 0 && this->fences->exhibit(g.target)) {
          gawk(g);
        } else {
          if (g.state == State::Fleeing)
            g.state = State::Walking;
          play(g, "stand", 1.0f);
        }
      }
      continue;
    }
    if (g.animTime < g.playFor)
      continue;
    // Done watching an exhibit (zoo.exe 0x42ea14): the time goes on its
    // viewing time (its popularity), then a donation: its animals healthy,
    // not crowded: cAdmissionMultiple x the time (game time, 100 ns units)
    // x its suitability x 0.01, $10 at most (in practice $10 a view of a
    // suitable exhibit: the code multiplies by cAdmissionMultiple, not
    // cDonationFactor)
    if (g.state == State::Gawking && g.target >= 0) {
      this->exhibitBooks[g.target].viewed += g.playFor;
      float sum = 0;
      int n = 0;
      bool sick = false;
      for (const Animals::Member &m : this->animals->members())
        if (m.exhibit == g.target) {
          sum += m.happiness;
          sick = sick || m.sick;
          n++;
        }
      int viewers = 0, tiles = 0;
      for (const Guest &o : this->list)
        viewers += o.state == State::Gawking && o.target == g.target ? 1 : 0;
      if (const Fences::Exhibit *ex = this->fences->exhibit(g.target))
        for (auto [x, y] : ex->tiles) {
          const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (auto &dd : d)
            tiles += walkable(x + dd[0], y + dd[1]) ? 1 : 0;
        }
      if (n > 0 && !sick && tiles > 0 && viewers * 10 / tiles < 50) {
        // (game time in 100 ns units: a 12 s game day is 864e9)
        double ticks = g.playFor * (864e9 / 12.0);
        int guide = this->tourBonusAt ? this->tourBonusAt(g.target) : 0;
        double d = kAdmissionMultiple * ticks * (this->animals->exhibitSuitability(g.target) + guide) * 0.01;
        if (d > 0) {
          d = std::min(d, 10.0);
          this->donations += d;
          this->exhibitBooks[g.target].donated += d;
        }
      }
      g.target = -1;
    }
    if (g.state == State::Caught) {
      // Mauled: up again (getupall), then on its way
      if (g.caughtStage == 0) {
        g.caughtStage = 1;
        float secs = 1.5f;
        if (Animation *a = this->art(g, "getupall"))
          if (a->frameTimeMs() > 0 && a->frameCount() > 0)
            secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
        play(g, "getupall", secs);
        continue;
      }
      g.state = State::Walking;
      decide(g);
      continue;
    }
    if (g.state == State::Using) {
      // Out again: what the building did, and what they bought
      const auto &objs = this->objects->objects();
      if (g.building >= 0 && g.building < static_cast<int>(objs.size())) {
        const BuildingInfo &b = infoOf(PlacedObjects::fileOf(objs[g.building]));
        bool child = this->typeList[g.type].child;
        float adultChange = b.adultChange, childChange = b.childChange;
        // (an animal house: its program's instead)
        {
          const PlacedObjects::Object &house = objs[g.building];
          if (house.nameId) {
            std::vector<const ResearchProgram *> shows = Research::get().collectionFor(house.nameId);
            if (!shows.empty()) {
              const ResearchProgram *p = shows[std::clamp(house.program, 0, static_cast<int>(shows.size()) - 1)];
              adultChange = static_cast<float>(p->effectVal[1]);
              childChange = static_cast<float>(p->effectVal[2]);
            }
          }
        }
        g.dHappy += child ? childChange : adultChange;
        g.dHunger += b.hunger;
        g.dThirst += b.thirst;
        g.dBathroom += b.bathroom;
        g.dTired += b.energy;
        if (g.need == "energy")
          g.dTired -= g.tired; // (a sit down: rested)
        PlacedObjects::Object &bo = this->objects->all()[g.building];
        // The price (zoo.exe 0x42d1e2, 0x42d858): twice its default or more,
        // too dear: unhappier and nothing bought (still charged); half or
        // less, a bargain: happier
        float price = bo.price >= 0 ? bo.price : b.price;
        bool tooDear = b.price > 0 && price >= b.price * 2.0f;
        bool cheap = b.price > 0 && price <= b.price * 0.5f;
        if (bo.price >= 0) {
          double take = price * b.priceFactor;
          this->concessions += take;
          bo.income += take;
        }
        if (tooDear) {
          think(g, 10073);
          g.dHappy -= 5;
        } else if (cheap) {
          think(g, 10072);
          g.dHappy += 5;
        }
        if (!b.item.empty() && !tooDear) {
          bo.sold[b.item]++;
          if (g.need == "gift")
            g.souvenir = true;
          g.dHappy += child ? b.itemChild : b.itemAdult;
          g.dHunger += b.itemHunger;
          g.dThirst += b.itemThirst;
          g.dBathroom += b.itemBathroom;
          g.trash = g.trash || b.itemTrash;
          if (b.consumedThought)
            think(g, b.consumedThought);
        } else if (b.usedThought) {
          think(g, b.usedThought);
        }
      }
      g.building = -1;
      g.need.clear();
      g.hidden = false;
      g.state = State::Walking;
    }
    decide(g);
  }
  // Gone out of the gate
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [](const Guest &g) { return g.x < -100; }),
                   this->list.end());
}

void Guests::addToIndex(WorldIndex &index) const {
  for (const Guest &g : this->list)
    index.addMover(WorldIndex::Kind::Guest, g.id, g.x, g.y);
}

// ----------------------------------------------------------------------------
// Crowd sounds (crowd.cfg, crowdsnd.cfg; zoo.exe): a bed of crowd noise by
// how many guests there are (quiet from 15, small from 20, medium from 85,
// large from 160; back down at 75 and 150), and now and then a voice or a
// camera (10 to 49 guests every second, 50 to 99 one try in 30, more one
// in 15)
// ----------------------------------------------------------------------------
void Guests::updateSounds(float seconds) {
  if (!this->sounds)
    return;
  // The crowd bed (zoo.exe 0x4354e3), by the guests in the zoo: quiet
  // under 15 (and to start); the small crowd over 20; medium over 85, back
  // to small under 75; large over 160, back to medium under 150
  int n = 0;
  for (const Guest &g : this->list)
    n += g.x > -999 ? 1 : 0;
  int level = this->bedLevel;
  if (n < 15 && level != 0)
    level = 0;
  else if (n > 20 && (level == 0 || level == -1))
    level = 1;
  else if (n > 85 && (level == 1 || level == -1))
    level = 2;
  else if (n > 160 && (level == 2 || level == -1))
    level = 3;
  else if (n < 75 && (level == 2 || level == -1))
    level = 1;
  else if (n < 150 && (level == 3 || level == -1))
    level = 2;
  if (level != this->bedLevel) {
    static const char *beds[4] = {"sounds/quiet", "sounds/crowds", "sounds/crowdm", "sounds/crowdl"};
    Sound::get().stop(this->bedChannel);
    this->bedChannel = level >= 0 ? Sound::get().loop(beds[level], 1500) : -1;
    this->bedLevel = level;
  }
  this->chatterClock += seconds;
  while (this->chatterClock >= 1.0f) {
    this->chatterClock -= 1.0f;
    // (crowdsnd.cfg, zoo.exe 0x43f513: no voices under 10 guests; from 10, a
    // 1 in 100 chance a second - its medium and big groups list the same
    // sounds, so the game's cache gives them small's chance)
    int chance = n < 10 ? 0 : 100;
    if (chance == 0 || std::uniform_int_distribution<int>(0, chance - 1)(this->rng) != 0)
      continue;
    static const std::pair<const char *, int> voices[5] = {
        {"guests/vofem1", 20}, {"guests/vofem4", 10}, {"guests/vomale1", 20}, {"guests/cam1", 30}, {"guests/vomale4", 20}};
    int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
    for (auto [file, prob] : voices)
      if ((roll -= prob) < 0) {
        // (a random point round the middle of the view, nearer more likely)
        std::uniform_int_distribution<int> r400(0, 399), r300(0, 299);
        float dx = static_cast<float>(r400(this->rng) - r400(this->rng));
        float dy = static_cast<float>(r300(this->rng) - r300(this->rng));
        Sound::get().playAt(file, dx, dy);
        break;
      }
  }
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
CompassDirection Guests::facing(const WorldRenderer &view, const Guest &g) const {
  float x0, y0, x1, y1, d;
  view.worldToScreenF(g.x, g.y, 0, x0, y0, d);
  view.worldToScreenF(g.x + g.fx, g.y + g.fy, 0, x1, y1, d);
  float angle = std::atan2(x1 - x0, -(y1 - y0));
  int octant = static_cast<int>(std::lround(angle / (3.14159265f / 4.0f)));
  octant = (octant % 8 + 8) % 8;
  static const CompassDirection dirs[8] = {
      CompassDirection::N, CompassDirection::NE, CompassDirection::E, CompassDirection::SE,
      CompassDirection::S, CompassDirection::SW, CompassDirection::W, CompassDirection::NW};
  return dirs[octant];
}

void Guests::collect(const WorldRenderer &view, const WorldMap &map, std::vector<Fences::Drawable> &out) const {
  (void)map;
  if (this->hideAll)
    return;
  for (const Guest &g : this->list) {
    if (g.hidden)
      continue;
    Animation *a = this->art(g, g.anim);
    if (!a)
      continue;
    float ms = a->frameTimeMs() ? static_cast<float>(a->frameTimeMs()) : 100.0f;
    float sx, sy, depth;
    view.worldToScreenF(g.x, g.y, groundAt(g.x, g.y), sx, sy, depth);
    Fences::Drawable d{depth, sx, sy, a, facing(view, g), SDL_Color{255, 255, 110, 255}, g.id == this->hovered};
    d.frame = static_cast<int>(g.animTime * 1000.0f / ms);
    out.push_back(d);
    if (g.id == this->selected && this->selectArrow)
      out.push_back({depth + 0.0001f, sx, sy - 36.0f, this->selectArrow, CompassDirection::N,
                     SDL_Color{255, 255, 255, 255}, false});
  }
}

int Guests::pick(float px, float py, const WorldRenderer &view, const WorldMap &map) const {
  (void)map;
  if (this->hideAll)
    return -1;
  int found = -1;
  float best = 1e9f;
  for (const Guest &g : this->list) {
    float sx, sy, depth;
    view.worldToScreenF(g.x, g.y, groundAt(g.x, g.y), sx, sy, depth);
    if (px < sx - 7 || px > sx + 7 || py < sy - 26 || py > sy + 3)
      continue;
    float d = std::fabs(px - sx) + std::fabs(py - (sy - 12));
    if (d < best) {
      best = d;
      found = g.id;
    }
  }
  return found;
}
