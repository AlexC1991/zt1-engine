#include "Guests.hpp"
#include "ZooSim.hpp"
#include "LabTrace.hpp"
#include "OriginalConstants.hpp"
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
constexpr float kTickMs = 1000.0f;          // [AI] mStatusPeriod
constexpr int kMaxGuests = 1000;            // [AI] maxGuests
// Thoughts (lang strings)
constexpr int kHungryNoFood = 10081, kThirstyNoDrink = 10083, kNoRestroom = 10086, kTired = 10092;
constexpr int kOverjoyed = 10098, kVeryAngry = 10099, kSpecies = 10100, kCrowded = 10102;
constexpr int kLeaveLow = 10087, kLeaveMed = 10088, kLeaveHigh = 10089, kSeenAll = 10238;
constexpr int kRun = 10245, kGoodValue = 10017, kOutOfExhibit = 10248, kFeelSick = 10285;
constexpr int kSickSkin = 1000; // (ours: the skin slot's colour meaning cSickPalName)

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
  // (what counts as hungry, thirsty... for the lists and the advisor)
  this->uiThreshold = ai->getInt("characteristics/integers", "cuithreshold", 75);
  // (zoo.exe ZTGuestType::loadClassConfig 0x52196e: these into globals)
  this->searchRadius = ai->getInt("characteristics/integers", "csearchradius", this->searchRadius);
  this->randomStatNth = ai->getInt("characteristics/integers", "crandomstatnth", this->randomStatNth);
  {
    static const char *stat[4] = {"hunger", "thirst", "energy", "bathroom"};
    for (int k = 0; k < 4; k++) {
      this->randomMin[k] = ai->getInt("characteristics/integers", std::string("crandom") + stat[k] + "min", this->randomMin[k]);
      this->randomMax[k] = ai->getInt("characteristics/integers", std::string("crandom") + stat[k] + "max", this->randomMax[k]);
    }
  }
  // (ztunit.ai: every unit's - zoo.exe reads them into globals at 0x4603a6)
  if (IniReader *unit = rm->getIniReader("ztunit.ai")) {
    this->predatorRadius = unit->getInt("characteristics/integers", "cpredatorradius", this->predatorRadius);
    this->nonPathCost = unit->getInt("characteristics/integers", "cnonpathcost", this->nonPathCost);
    this->nonPathCostEmergency =
        unit->getInt("characteristics/integers", "cnonpathcostemergency", this->nonPathCostEmergency);
    delete unit;
  }
  // (dung's cStink, ZTSceneryType +0x11c: what blockGridRefresh adds up)
  if (IniReader *poo = rm->getIniReader("scenery/other/poo.ai")) {
    this->dungStink = poo->getInt("characteristics/integers", "cstink", this->dungStink);
    delete poo;
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
    auto num = [&](const char *k) {
      return ai->getInt(ints, k, ai->getInt("characteristics/integers", k, ai->getInt("man/characteristics/integers", k, 0)));
    };
    t.testFenceChance = num("ctestfencechance");
    t.zapHit = num("czaphappinesshit");
    // (zoo.exe ZTGuestType::guestTypeLoad 0x4c4cb9: these keys are only in
    // man's section; the other kinds take its values, as a subtype takes
    // its DefaultSubtype's - measured for animals, re/lab/scripts/hunger.cpp)
    auto read = [&](const char *k, int &v) {
      for (const char *sec : {ints.c_str(), "characteristics/integers", "man/characteristics/integers"})
        if (!ai->get(sec, k).empty()) {
          v = num(k);
          return;
        }
    };
    read("chungercheck", t.hungerCheck), read("cthirstycheck", t.thirstCheck), read("cbathroomcheck", t.bathroomCheck);
    read("cbuysouvenircheck", t.souvenirCheck), read("cenergycheck", t.energyCheck), read("ctrashcheck", t.trashCheck);
    read("cchasecheck", t.chaseCheck), read("cleavezoocheck", t.leaveCheck), read("cviewingareacheck", t.viewingCheck);
    read("cenvironmenteffectcheck", t.environmentCheck);
    read("chungerincrement", t.hungerIncrement), read("cthirstincrement", t.thirstIncrement);
    read("cbathroomincrement", t.bathroomIncrement), read("cenergyincrement", t.energyIncrement);
    read("chungerthreshold", t.hungerThreshold), read("cthirstthreshold", t.thirstThreshold);
    read("cbathroomthreshold", t.bathroomThreshold), read("cenergythreshold", t.energyThreshold);
    read("cangryfoodchange", t.angryFood), read("cangrythirstchange", t.angryThirst);
    read("cangrybathroomchange", t.angryBathroom), read("cangryenergychange", t.angryEnergy), read("cmaxenergy", t.maxEnergy);
    read("cleavechancelow", t.leaveLow), read("cleavechancemed", t.leaveMed), read("cleavechancehigh", t.leaveHigh);
    read("cleavechancedone", t.leaveDone), read("cbuysouvenirchancemed", t.souvenirMed);
    read("cbuysouvenirchancehigh", t.souvenirHigh), read("cangrytrashchange", t.angryTrash);
    read("cescapedanimalchange", t.escapedChange), read("cinitialhappiness", t.initialHappiness);
    read("cobjectestheticthreshold", t.estheticThreshold), read("chappyestheticchange", t.happyEsthetic);
    read("ccrowdedviewingthreshold", t.crowdedThreshold), read("ccrowdedviewingchange", t.crowdedChange);
    read("cstinkthreshold", t.stinkThreshold), read("csickchange", t.sickChange);
    read("csawanimalreset", t.sawReset);
    read("csickchance", t.sickChance);
    read("csickanimalchange", t.sickAnimalChange), read("chappyanimalchange1", t.happyAnimal1);
    read("chappyanimalchange2", t.happyAnimal2), read("cangryanimalchange1", t.angryAnimal1);
    read("cangryanimalchange2", t.angryAnimal2), read("cangryanimalchange3", t.angryAnimal3);
    read("cdifferentspeciesthreshold", t.speciesThreshold), read("cdifferentspecieschange", t.speciesChange);
    // (the lists: cGawkTime five times, cGawkAttrTime six - ZTGuestType
    // +0x2d0, +0x2dc)
    for (const char *sec : {ints.c_str(), "characteristics/integers", "man/characteristics/integers"}) {
      std::vector<int> g = ai->getIntList(sec, "cgawktime"), a = ai->getIntList(sec, "cgawkattrtime");
      if (g.size() >= 5 && a.size() >= 6) {
        t.gawkTime = g;
        t.gawkAttrTime = a;
        break;
      }
    }
    // Watching: its bGawk set's weights (gawk, camera, stand)
    {
      std::string f = ai->get(key + "\\behaviorset\\bgawk", "f");
      int w[3], n = 0;
      size_t open = f.find('(');
      std::string list = open == std::string::npos ? "" : f.substr(open + 1);
      for (size_t at = 0, k = 0; at <= list.size() && n < 3; k++) {
        size_t c = list.find(',', at);
        std::string item = list.substr(at, c == std::string::npos ? std::string::npos : c - at);
        if (k % 2 == 1)
          w[n++] = std::atoi(item.c_str());
        if (c == std::string::npos)
          break;
        at = c + 1;
      }
      if (n == 3 && w[0] + w[1] + w[2] > 0)
        for (int k = 0; k < 3; k++)
          t.gawkWeights[k] = w[k];
    }
    t.sickPal = lower(ai->get(key + "/characteristics/strings", "csickpalname"));
    // Looking at scenery: [<kind>\BehaviorSet\bGawkObj1..6]
    // f = fPlaySetProb(bGawk1,<w>,bGawk2,<w>,bGawk3,<w>)
    for (int band = 0; band < 6; band++) {
      std::string f = ai->get(key + "\\behaviorset\\bgawkobj" + std::to_string(band + 1), "f");
      int w[3], n = 0;
      size_t open = f.find('(');
      std::string list = open == std::string::npos ? "" : f.substr(open + 1);
      for (size_t at = 0, k = 0; at <= list.size() && n < 3; k++) {
        size_t c = list.find(',', at);
        std::string item = list.substr(at, c == std::string::npos ? std::string::npos : c - at);
        if (k % 2 == 1)
          w[n++] = std::atoi(item.c_str());
        if (c == std::string::npos)
          break;
        at = c + 1;
      }
      if (n == 3)
        for (int k = 0; k < 3; k++)
          t.gawkObjWeights[band][k] = w[k];
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
  // Mauled by a predator: [BehaviorSet\bCaughtBy<animal><m|f>] f =
  // fPlayWithSound(<anim>, <sound>) - the sound each plays
  for (const std::string &sec : ai->getSections()) {
    std::string low = lower(sec);
    const std::string pre = "behaviorset\\bcaughtby";
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
  // The water's behaviour sets (Marine Mania): each ride's
  // [<kind>\BehaviorSet\bReturn<building><program>] (zoo.exe 0x597028 makes
  // the name: "bReturn", the building type's name, its program +0x188),
  // [BehaviorSet\bInWater], bSuitIdle, and a man's bSuitDrown, bSurfDrown
  for (const std::string &sec : ai->getSections()) {
    const std::string low = lower(sec);
    const size_t at = low.find("behaviorset\\");
    if (at == std::string::npos)
      continue;
    const std::string name = low.substr(at + 12);
    if (name.rfind("breturn", 0) == 0 || name == "binwater" || name == "bsuitidle" || name == "bsuitdrown" ||
        name == "bsurfdrown")
      this->behaviourSets[low] = ai->getList(sec, "f");
  }
  delete ai;
  delete cfg;
  if (rm->hasResource("ui/select/selsmall/selsmall.ani"))
    this->selectArrow = rm->getAnimation("ui/select/selsmall/selsmall");
  SDL_Log("[Guests] %zu guest types", this->typeList.size());
}

void Guests::clear() {
  this->list.clear();
  this->notices.clear();
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
void Guests::wornColours(const Guest &g, int out[4]) const {
  std::copy(std::begin(g.colours), std::end(g.colours), out);
  if (g.smelled && g.type >= 0 && g.type < static_cast<int>(this->typeList.size()) &&
      !this->typeList[g.type].sickPal.empty())
    out[3] = kSickSkin;
}

Animation *Guests::art(const Guest &g, const std::string &name) const {
  // ours: what it was last time (the key below made and looked up for every
  // guest, drawn and lit, every frame)
  int worn[4];
  wornColours(g, worn);
  if (g.artMemo && g.artMemoGen == this->artGen && g.artMemoType == g.type && g.artMemoName == name &&
      std::equal(std::begin(worn), std::end(worn), std::begin(g.artMemoColours)))
    return g.artMemo;
  Animation *found = this->artUncached(g, name);
  g.artMemo = found;
  g.artMemoName = name;
  g.artMemoType = g.type;
  std::copy(std::begin(worn), std::end(worn), std::begin(g.artMemoColours));
  g.artMemoGen = this->artGen;
  return found;
}

Animation *Guests::artUncached(const Guest &g, const std::string &name) const {
  const Type &t = this->typeList[g.type];
  auto it = t.anims.find(name);
  if (it == t.anims.end())
    it = t.anims.find("stand");
  if (it == t.anims.end())
    return nullptr;
  const std::string &path = it->second;
  int worn[4];
  wornColours(g, worn);
  std::string key = path;
  for (int i = 0; i < 4; i++)
    key += "#" + std::to_string(worn[i]);
  auto c = this->artCache.find(key);
  if (c != this->artCache.end())
    return c->second;
  Animation *a = this->outfit(t, worn, path, true);
  this->artCache[key] = a;
  return a;
}

// A guest's animation in its outfit: copied: from its art loaded once
// (indexArt), the colours put in; or (copied false, and when that can't be
// made) loaded in the outfit's palette, as it was
Animation *Guests::outfit(const Type &t, const int colours[4], const std::string &path, bool copied) const {
  if (!this->rm || !this->rm->hasResource(path + ".ani"))
    return nullptr;
  PalletManager *pm = this->rm->getPalletManager();
  std::string full = t.fullPal;
  Pallet *base = full.empty() ? nullptr : pm->getPallet(full);
  if (!base)
    return this->rm->getAnimation(path);
  // The full palette with its four 16-colour ranges (192, 208, 224,
  // 240) from the guest's shirt, pants or skirt, hair and skin
  Pallet variant = *base;
  for (int s = 0; s < 4; s++) {
    if (t.slots[s].empty())
      continue;
    std::string name = s == 3 && colours[s] == kSickSkin && !t.sickPal.empty()
                           ? t.sickPal : t.slots[s][colours[s] % t.slots[s].size()];
    Pallet *p = pm->getPallet(name);
    for (int i = 0; p && i < 16; i++)
      if (p->color_count > static_cast<uint32_t>(i + 1))
        variant.colors[192 + s * 16 + i] = p->colors[i + 1];
  }
  std::string key = path;
  for (int i = 0; i < 4; i++)
    key += "#" + std::to_string(colours[i]);
  if (copied) {
    std::shared_ptr<Animation> &index = this->indexArt[t.fullPal + "|" + path];
    if (!index) {
      // (its palette: entry i drawn as the colour i + 1, each its own - the
      // loader makes every pixel its entry's colour, in full alpha. Entry 1
      // keeps its black where the palette has it: that black is the art's
      // shadow, its pixels marked as the shadow's)
      Pallet numbered = *base;
      for (uint32_t i = 0; i < 256; i++)
        numbered.colors[i] = i + 1;
      if (base->color_count > 1 && (base->colors[1] & 0xFFFFFF) == 0)
        numbered.colors[1] = base->colors[1];
      const std::string name = t.fullPal + "#numbered";
      pm->addPallet(name, numbered);
      PalletManager::setOverride(t.fullPal, name);
      index.reset(this->rm->getAnimationUnowned(path));
      PalletManager::clearOverrides();
      if (!index)
        index = std::make_shared<Animation>(); // (none: loaded as before)
    }
    // (a colour i + 1 is entry i's in the outfit; black - the shadow's, or
    // entry 1's - stays black, as loaded)
    if (Animation *copy = index->recolouredCopy([&variant](uint32_t v) {
          const uint32_t n = v & 0xFFFFFF;
          return n == 0 || n > 256 ? v : (variant.colors[n - 1] | 0xFF000000);
        }))
      return this->rm->adoptAnimation(copy);
  }
  std::string name2 = t.fullPal + "#" + key;
  pm->addPallet(name2, variant);
  PalletManager::setOverride(t.fullPal, name2);
  Animation *a = this->rm->getAnimation(path);
  PalletManager::clearOverrides();
  return a;
}

std::string Guests::outfitCheck() const {
  int compared = 0, same = 0, copies = 0;
  std::string firstBad;
  for (const Type &t : this->typeList) {
    for (const auto &[anim, path] : t.anims) {
      for (int k = 0; k < 3; k++) {
        const int colours[4] = {k * 3 + 1, k * 5 + 2, k * 7, k * 2 + 3};
        Animation *copy = this->outfit(t, colours, path, true);
        Animation *loaded = this->outfit(t, colours, path, false);
        if (!copy && !loaded)
          continue;
        ++compared;
        if (copy && loaded && copy->sameArt(*loaded))
          ++same;
        else if (firstBad.empty())
          firstBad = path;
        delete copy;
        delete loaded;
      }
    }
  }
  // (and made as copies: each one's art loaded once, in its numbered palette)
  for (const auto &[name, index] : this->indexArt)
    copies += index && index->frameCount() > 0;
  return std::string(compared && same == compared && copies > 0 ? "PASS " : "FAIL ") +
         "a guest's outfit made as a copy of its art in the outfit's colours is the art loaded in them (" +
         std::to_string(same) + " of " + std::to_string(compared) + " the same, every frame's pixels, shadows and anchors; " + std::to_string(copies) + " animations loaded once for them" +
         (firstBad.empty() ? "" : "; first different: " + firstBad) + ")";
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

bool Guests::testRoute(int sx, int sy, int gx, int gy, std::vector<std::pair<int, int>> &out) const {
  if (!this->map || !this->fences)
    return false;
  auto pass = [&](int x, int y) { return walkable(x, y); };
  auto step = [&](int x, int y, int nx, int ny) { return canStep(x, y, nx, ny); };
  return Pathfinder::find(this->map->getWidth(), this->map->getHeight(), sx, sy, gx, gy, pass, step, out, 20000);
}

void Guests::labPlay(int id, const std::string &anim, float seconds) {
  for (Guest &g : this->list)
    if (g.id == id) {
      g.path.clear();
      g.pathAt = 0;
      play(g, anim, seconds > 0 ? seconds : 2.0f);
    }
}

std::vector<std::string> Guests::labAnims(int id) const {
  std::vector<std::string> out;
  if (const Guest *g = this->guest(id))
    for (const auto &kv : this->typeList[g->type].anims)
      out.push_back(kv.first);
  return out;
}

void Guests::labSet(int id, const std::string &field, float v) {
  for (Guest &g : this->list)
    if (g.id == id) {
      if (field == "happiness") g.happiness = v;
      else if (field == "hunger") g.hunger = v;
      else if (field == "thirst") g.thirst = v;
      else if (field == "bathroom") g.bathroom = v;
      else if (field == "tired") g.tired = v;
    }
}

void Guests::labLeave(int id) {
  for (Guest &g : this->list)
    if (g.id == id) {
      g.leaving = true;
      decide(g);
    }
}

bool Guests::walkTo(Guest &g, int tx, int ty, float speed, bool emergency) {
  int sx = static_cast<int>(std::floor(g.x)), sy = static_cast<int>(std::floor(g.y));
  // Running from a predator (zoo.exe chooseTask's task 0x75 sets +0x230 to
  // cNonPathCostEmergency): open ground as well as the paths, a step off
  // them costing cNonPathCostEmergency. ours: left on open ground after
  // running, back the same way, a step off the paths costing cNonPathCost
  const bool open = emergency || !walkable(sx, sy);
  const float offCost = static_cast<float>(emergency ? this->nonPathCostEmergency : this->nonPathCost);
  auto pass = [&](int x, int y) { return open ? fleeGround(x, y) : walkable(x, y); };
  auto step = [&](int x, int y, int nx, int ny) { return canStep(x, y, nx, ny); };
  auto cost = [&](int x, int y) { return walkable(x, y) ? 1.0f : std::max(1.0f, offCost); };
  std::vector<std::pair<int, int>> tiles;
  if (!Pathfinder::find(this->map->getWidth(), this->map->getHeight(), sx, sy, tx, ty, pass, step, tiles, 20000,
                        open ? Pathfinder::Cost(cost) : Pathfinder::Cost()))
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
  ZT_STEP("", "Thinks", {"thought", s}, {"string id", stringId});
  g.thoughts.erase(std::remove(g.thoughts.begin(), g.thoughts.end(), s), g.thoughts.end());
  g.thoughts.insert(g.thoughts.begin(), s);
  if (g.thoughts.size() > 10)
    g.thoughts.resize(10);
}

int Guests::timeInParkText(const Guest &g) const {
  // guestInfoRefresh -> 0x46a791 compares zoo-calendar hours since
  // arrival against 1440, 5760 and 8640 (60, 240 and 360 days).
  const double days = std::max(0.0, this->clock - g.arrived) / ZooSim::kSecondsPerDay;
  return 3420 + (days < 60 ? 0 : days < 240 ? 1 : days < 360 ? 2 : 3);
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
      for (const std::string &slot : ai->getList("slots", "name")) {
        std::vector<int> e = ai->getIntList(slot, "entrpos");
        for (size_t k = 0; k + 1 < e.size(); k += 2)
          b.doors.push_back({e[k], e[k + 1]});
        // (a water ride's slots: zoo.exe ZTBuildingType::loadConfig 0x4c0b4d
        // reads exitonwater, entranceonwater, isreturnslot, exitpos and the
        // id list - 0x4c09ce..0x4c0c7a, 0x4c08dc)
        BuildingInfo::Slot s;
        std::vector<int> x = ai->getIntList(slot, "exitpos");
        if (e.size() >= 2)
          s.ex = e[0], s.ey = e[1];
        if (x.size() >= 2)
          s.xx = x[0], s.xy = x[1];
        s.exitOnWater = ai->getInt(slot, "exitonwater", 0) != 0;
        s.entranceOnWater = ai->getInt(slot, "entranceonwater", 0) != 0;
        s.isReturn = ai->getInt(slot, "isreturnslot", 0) != 0;
        s.ids = ai->getIntList(slot, "id");
        b.slots.push_back(s);
      }
      b.nameId = ai->getInt(ints, "cnameid", 0);
      b.returnBuilding = ai->getInt(ints, "creturnbuilding", 0) == 1;
      b.idler = ai->getInt(ints, "cidler", 0) == 1;
      b.viewer = ai->getInt(ints, "cexhibitviewer", 0) == 1;
      b.tracker = ai->getInt(ints, "cusertracker", 0) == 1;
      b.strictCap = ai->getInt(ints, "cstrictbuildingcapacity", 0);
      const auto positions=ai->getIntList(ai->get("slot","cHasDrawSlots","drawslots"),"p");
      for(size_t i=0;i+2<positions.size();i+=3)
        b.drawSlots.push_back({positions[i]/64.0f,positions[i+1]/64.0f,positions[i+2]/16.0f});
      // (scenery guests look at: ZTSceneryType +0x120 cEstheticWeight, +0x135
      // cGawkOnlyFromFront; [EstheticBonus] v = <guest kind's cNameID>, v = <bonus>)
      b.estheticWeight = ai->getInt(ints, "cestheticweight", 0);
      b.gawkFront = ai->getInt(ints, "cgawkonlyfromfront", 0) != 0;
      std::vector<int> bonus = ai->getIntList("estheticbonus", "v");
      for (size_t k = 0; k + 1 < bonus.size(); k += 2)
        b.estheticBonus[bonus[k]] = bonus[k + 1];
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

std::vector<std::pair<int, int>> Guests::doorTiles(const PlacedObjects::Object &o) const {
  std::vector<std::pair<int, int>> tiles;
  const BuildingInfo &b = infoOf(PlacedObjects::fileOf(o));
  // (its front the way it faces, its x across it)
  float angle = o.facing * 3.14159265f / 4.0f;
  float fwdX = std::sin(angle), fwdY = -std::cos(angle);
  float acrossX = -std::cos(angle), acrossY = -std::sin(angle);
  for (const auto &[ex, ey] : b.doors) {
    float x = o.x + (ex * acrossX + ey * fwdX) / 64.0f, y = o.y + (ex * acrossY + ey * fwdY) / 64.0f;
    std::pair<int, int> t{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
    if (std::find(tiles.begin(), tiles.end(), t) == tiles.end())
      tiles.push_back(t);
  }
  return tiles;
}

bool Guests::positionShowSeat(Guest &g,const PlacedObjects::Object &stand,int seat) const {
  const auto &positions=infoOf(PlacedObjects::fileOf(stand)).drawSlots;
  if(seat<0 || static_cast<size_t>(seat)>=positions.size())return false;
  const float x=positions[seat].x,y=positions[seat].y;
  const float angle=stand.facing*3.14159265f/4.0f;
  // The art's across/forward axes turn with the building, not the camera.
  g.x=stand.x-x*std::cos(angle)+y*std::sin(angle);
  g.y=stand.y-x*std::sin(angle)-y*std::cos(angle);
  g.showZ=groundAt(stand.x,stand.y)+positions[seat].z-groundAt(g.x,g.y);
  g.fx=std::sin(angle);g.fy=-std::cos(angle);
  return true;
}

// The nearest building for a need it can walk to (in cSearchRadius tiles;
// not full): off to its door (a path tile beside it)
bool Guests::seekBuilding(Guest &g, const std::string &need, int radius) {
  this->seekFull = false;
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
    if (need == "trash" && o.trashCan && o.fill >= 24)
      continue;
    // (a slot with an id list takes only those kinds of guest - the boat and
    // dolphin shacks' men and women: children have no boat or dolphin art)
    {
      bool kindOk = true;
      for (const BuildingInfo::Slot &s : b.slots)
        if (!s.isReturn && !s.ids.empty())
          kindOk = std::find(s.ids.begin(), s.ids.end(), this->typeList[g.type].nameId) != s.ids.end();
      if (!kindOk)
        continue;
    }
    int users = 0;
    for (const Guest &o2 : this->list)
      users += o2.building == static_cast<int>(i) ? 1 : 0;
    if (users >= b.capacity) {
      full = true;
      continue;
    }
    // A cUserTracker ride (the dolphin shack) counts its riders out on the
    // water against its capacity (zoo.exe 0x59fe9e: its users list, +0x158,
    // against +0x1c8 - 0x59398a: cStrictBuildingCapacity, else the lake's
    // size from its water door + 7 over 10, 10 at most)
    if (b.tracker && b.returnBuilding) {
      int riders = 0;
      for (const Guest &o2 : this->list)
        riders += o2.ride && o2.rideFrom == o.id ? 1 : 0;
      int cap = b.strictCap;
      if (cap <= 0) {
        int W = 0;
        for (const BuildingInfo::Slot &s : b.slots)
          if (s.exitOnWater) {
            float wx, wy;
            slotPoint(o, s.xx, s.xy, wx, wy);
            W = waterSize(static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wy)));
          }
        cap = std::min(10, (W + 7) / 10);
      }
      if (riders >= cap) {
        full = true;
        continue;
      }
    }
    // Its door: the tile at its entrpos (one on a path); a building with no
    // entrpos (a bin), a path tile touching its footprint
    std::vector<std::pair<int, int>> doors = doorTiles(o);
    if (doors.empty()) {
      int fx = b.fx, fy = b.fy;
      if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
        std::swap(fx, fy);
      float hx = fx / 4.0f, hy = fy / 4.0f;
      int x0 = static_cast<int>(std::floor(o.x - hx)) - 1, x1 = static_cast<int>(std::floor(o.x + hx - 0.01f)) + 1;
      int y0 = static_cast<int>(std::floor(o.y - hy)) - 1, y1 = static_cast<int>(std::floor(o.y + hy - 0.01f)) + 1;
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
          doors.push_back({x, y});
    }
    for (const auto &[x, y] : doors)
      if (walkable(x, y) && d < bestD) {
        bestD = d;
        best = static_cast<int>(i);
        bx = x;
        by = y;
      }
  }
  ZT_STEP("", "Looks for a building for a need", {"need", need}, {"search radius", radius},
          {"found", best >= 0 ? "yes" : full ? "all full" : "none in reach"}, {"distance", best >= 0 ? bestD : 0.0f});
  if (best < 0) {
    // (all full: "%s is too crowded." alone, not also "none found" - zoo.exe
    // 0x4fba3a, 0x58c86a)
    this->seekFull = full;
    if (full)
      think(g, 10243); // "%s is too crowded."
    return false;
  }
  if (!walkTo(g, bx, by, this->typeList[g.type].speed))
    return false;
  g.guide = -1; g.tourHeard = false;
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
    if (!can.trashCan || can.fill <= 24) {
      if (can.trashCan)
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
// Marine Mania's water rides: the changing room (chroom), the boat shack
// (boatshak: its programs the paddle boat, kayak and swan boat) and the
// dolphin shack (dolshack). In at the land door, out onto the water (its
// slot's exitonwater exitpos), a ride of legs over the lake (Task82), back
// in through the return slot (entranceonwater) and out on land. Without a
// way back: in the water, making for land (Task83); a man who can't,
// drowns (Task84)
// ----------------------------------------------------------------------------
bool Guests::swimmable(int x, int y) const {
  // ours: the lake's tiles (terrain 9, 10 - the original's tile flags +0x83 &
  // 3) out of the exhibits (a tank's water is its animals')
  if (!this->map || !this->fences)
    return false;
  const MapTile *t = this->map->getTile(x, y);
  return t && (t->terrainType == 9 || t->terrainType == 10) && this->fences->exhibitAt(x, y) < 0;
}

void Guests::slotPoint(const PlacedObjects::Object &o, int px, int py, float &x, float &y) const {
  // (a slot's point: from its middle, turned the way it was built - as
  // doorTiles; ZTBuildingType::loadConfig 0x4c0b4d)
  const float angle = o.facing * 3.14159265f / 4.0f;
  const float fwdX = std::sin(angle), fwdY = -std::cos(angle);
  const float acrossX = -std::cos(angle), acrossY = -std::sin(angle);
  x = o.x + (px * acrossX + py * fwdX) / 64.0f;
  y = o.y + (px * acrossY + py * fwdY) / 64.0f;
}

int Guests::waterSize(int x, int y) const {
  // zoo.exe 0x5938c7: the water reached from a ride's water door (its tile
  // and the lake joined to it). ours: tiles side by side, counted to 100 -
  // past 93 the ride's legs are 10 whatever its size
  if (!this->map || !swimmable(x, y))
    return 0;
  std::set<std::pair<int, int>> seen{{x, y}};
  std::vector<std::pair<int, int>> todo{{x, y}};
  while (!todo.empty() && seen.size() < 100) {
    auto [cx, cy] = todo.back();
    todo.pop_back();
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &dd : d) {
      const int nx = cx + dd[0], ny = cy + dd[1];
      if (swimmable(nx, ny) && seen.insert({nx, ny}).second)
        todo.push_back({nx, ny});
    }
  }
  return static_cast<int>(std::min<size_t>(seen.size(), 100));
}

std::vector<Guests::Move> Guests::behaviour(const Guest &g, const std::string &set) const {
  // A behaviour set as its kind has it (its own section first, then the
  // shared one: [man\BehaviorSet\bReturnchroom0] over [BehaviorSet\...])
  std::vector<Move> out;
  const std::string s = lower(set);
  auto it = this->behaviourSets.find(this->typeList[g.type].key + "\\behaviorset\\" + s);
  if (it == this->behaviourSets.end())
    it = this->behaviourSets.find("behaviorset\\" + s);
  if (it == this->behaviourSets.end())
    return out;
  for (const std::string &line : it->second) {
    const size_t open = line.find('('), close = line.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open)
      continue;
    std::string fn = lower(line.substr(0, open));
    fn.erase(0, fn.find_first_not_of(" \t"));
    fn.erase(fn.find_last_not_of(" \t") + 1);
    std::vector<std::string> args;
    std::string list = line.substr(open + 1, close - open - 1);
    for (size_t at = 0;;) {
      const size_t c = list.find(',', at);
      std::string a = lower(list.substr(at, c == std::string::npos ? std::string::npos : c - at));
      a.erase(0, a.find_first_not_of(" \t"));
      a.erase(a.find_last_not_of(" \t") + 1);
      args.push_back(a);
      if (c == std::string::npos)
        break;
      at = c + 1;
    }
    Move m;
    if ((fn == "fmovewithsound" || fn == "fmove") && args.size() >= 4) {
      m.anim = args[2];
      m.speed = std::atoi(args[3].c_str());
      if (fn == "fmovewithsound" && args.size() >= 5)
        m.sound = args[4];
    } else if (fn == "fplaytime" && args.size() >= 2) {
      m.anim = args[0];
      m.seconds = static_cast<float>(std::atof(args[1].c_str()));
    } else if (fn == "fplaywithsound" && args.size() >= 2) {
      m.anim = args[0];
      m.sound = args[1];
    } else if (fn == "fplay" && !args.empty()) {
      m.anim = args[0];
    } else {
      continue;
    }
    out.push_back(m);
  }
  return out;
}

bool Guests::waterTo(Guest &g, int tx, int ty, const Move &m) {
  // A move over the water (fMoveWithSound: its animation at its speed, its
  // sound as it sets off); the last tile may be land (a door, the shore).
  // ours: the route by the lake's tiles, no fence crossed
  if (!this->map || !this->fences)
    return false;
  const int sx = static_cast<int>(std::floor(g.x)), sy = static_cast<int>(std::floor(g.y));
  if (sx == tx && sy == ty)
    return false;
  auto pass = [&](int x, int y) { return swimmable(x, y) || (x == tx && y == ty) || (x == sx && y == sy); };
  auto step = [&](int x, int y, int nx, int ny) {
    const Fences::Edge e = nx != x ? Fences::Edge{false, std::max(x, nx), y} : Fences::Edge{true, x, std::max(y, ny)};
    return !this->fences->at(e);
  };
  std::vector<std::pair<int, int>> tiles;
  if (!Pathfinder::find(this->map->getWidth(), this->map->getHeight(), sx, sy, tx, ty, pass, step, tiles, 20000))
    return false;
  std::uniform_real_distribution<float> lane(0.3f, 0.7f);
  std::vector<std::pair<float, float>> points{{g.x, g.y}};
  for (size_t i = 1; i < tiles.size(); i++)
    points.push_back({tiles[i].first + lane(this->rng), tiles[i].second + lane(this->rng)});
  Pathfinder::smooth(points, pass, step);
  if (points.size() < 2)
    return false;
  g.path.assign(points.begin() + 1, points.end());
  g.pathAt = 0;
  const Type &t = this->typeList[g.type];
  g.speed = m.speed > 0 ? m.speed / 60.0f : t.speed;
  // (a kind without the ride's art - a child in a boat - swims: its "swim",
  // what a unit walking on water plays, 0x41338a)
  g.anim = t.anims.count(m.anim) ? m.anim : "swim";
  g.animTime = 0;
  g.playFor = 0;
  if (!m.sound.empty() && this->sounds && this->soundFile.count(m.sound))
    Sound::get().play(this->soundFile[m.sound], this->soundAtten[m.sound] + 600);
  return true;
}

void Guests::endRide(Guest &g) {
  // Off its ride: +0x284 and its behaviour cleared, out of the water
  // (guestBuy 0x596f10, Task83_InWater::atTarget 0x5a5d73)
  g.ride = 0;
  g.rideSet.clear();
  g.inWater = false;
  g.rideFrom = -1;
  g.waterTask = 0;
  g.legs = g.tries = g.drownStep = 0;
  g.idling = g.idler = g.viewer = false;
  if (g.state == State::Water)
    g.state = State::Walking;
}

void Guests::rideOut(Guest &g, const PlacedObjects::Object &o, const BuildingInfo &b) {
  // zoo.exe guestBuy 0x42d858, a guest not on a ride at a cReturnBuilding
  // (0x596e91): the ride's type kept (+0x284, its cNameID), its behaviour
  // set bReturn<name><program> (0x597028), counted out if it tracks its
  // users (0x48cd9b); placed at the exitonwater slot's exitpos; then the
  // ride, Task82 (ctor 0x5acdba: cIdler +0x20, cExhibitViewer +0x22; begin
  // 0x596fef: its legs, 0x596fb4 - the lake's size + 7 over 10, 10 at most)
  std::string stem = lower(PlacedObjects::fileOf(o));
  if (size_t slash = stem.find_last_of("/\\"); slash != std::string::npos)
    stem = stem.substr(slash + 1);
  if (size_t dot = stem.rfind('.'); dot != std::string::npos)
    stem = stem.substr(0, dot);
  g.ride = b.nameId ? b.nameId : 1;
  g.rideSet = "breturn" + stem + std::to_string(std::max(0, o.program));
  if (behaviour(g, g.rideSet).empty())
    g.rideSet = "breturn" + stem + "0";
  g.rideFrom = o.id;
  for (const BuildingInfo::Slot &s : b.slots)
    if (s.exitOnWater) {
      slotPoint(o, s.xx, s.xy, g.x, g.y);
      break;
    }
  g.fx = g.x - o.x;
  g.fy = g.y - o.y;
  if (float d = std::hypot(g.fx, g.fy); d > 0.01f)
    g.fx /= d, g.fy /= d;
  g.building = -1;
  g.need.clear();
  g.hidden = false;
  g.target = -1;
  g.gawkObject = -1;
  g.guide = -1;
  g.tourHeard = false;
  g.path.clear();
  g.pathAt = 0;
  g.state = State::Water;
  g.waterTask = kRideTask;
  g.idler = b.idler;
  g.viewer = b.viewer;
  g.idling = false;
  g.idleUntil = this->clock;
  const int W = waterSize(static_cast<int>(std::floor(g.x)), static_cast<int>(std::floor(g.y)));
  g.legs = std::min(10, (W + 7) / 10);
  g.playFor = 0;
  g.animTime = 0;
  ZT_STEP("0x42d858", "Out onto the water: its ride", {"ride", g.rideSet}, {"lake tiles", W}, {"legs", g.legs},
          {"idler", g.idler ? "yes" : "no"}, {"exhibit viewer", g.viewer ? "yes" : "no"});
}

void Guests::rideLeg(Guest &g) {
  // Task82's whileGoing (0x59fe56 -> 0x5a01ef): a leg fewer; a cIdler (the
  // changing room's swimmers) idles every other leg - bSuitIdle, cut short at
  // rand() % 7 + 6 seconds - but swims its last; a leg: a lake tile at
  // rand() % 10 - 5 of its own (rand() % 6 - 3 for an idler) it can reach,
  // 30 tries; a cExhibitViewer (a boat) then looks at what's ahead of it
  g.legs--;
  if (g.idler) {
    g.idling = !g.idling;
    if (g.legs <= 0)
      g.idling = false;
  } else {
    g.idling = false;
  }
  const Type &t = this->typeList[g.type];
  if (g.idling) {
    const float secs = static_cast<float>(std::uniform_int_distribution<int>(0, 0x7fff)(this->rng) % 7 + 6);
    g.idleUntil = this->clock + secs;
    std::vector<Move> idle = behaviour(g, "bsuitidle");
    const std::string anim = !idle.empty() && t.anims.count(idle[0].anim) ? idle[0].anim : "water_idle";
    play(g, anim, secs);
  } else {
    std::vector<Move> moves = behaviour(g, g.rideSet);
    Move m = moves.empty() ? Move{"swim", "", 56, 0} : moves[0];
    const int range = g.idler ? 6 : 10, half = range / 2;
    const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
    std::uniform_int_distribution<int> r(0, 0x7fff);
    bool went = false;
    for (int k = 0; k < 30 && !went; k++) {
      const int x = gx + r(this->rng) % range - half, y = gy + r(this->rng) % range - half;
      went = swimmable(x, y) && waterTo(g, x, y, m);
    }
    // (ours: no leg found - a second at rest on the water)
    if (!went)
      play(g, t.anims.count("water_idle") ? "water_idle" : "stand", 1.0f);
  }
  ZT_STEP("0x5a01ef", "A ride's next leg", {"legs left", g.legs}, {"idling", g.idling ? "yes" : "no"},
          {"animation", g.anim});
  // The boat's look ahead (0x5a0706 -> 0x5a07bc, 0x5a0774): the habitat 3
  // tiles the way it faces; one of its animals at 81-100 happiness, "%s
  // looks overjoyed."
  if (g.viewer && this->fences && this->animals) {
    const int dx = g.fx > 0.38f ? 1 : g.fx < -0.38f ? -1 : 0, dy = g.fy > 0.38f ? 1 : g.fy < -0.38f ? -1 : 0;
    const int ex = this->fences->exhibitAt(static_cast<int>(std::floor(g.x)) + 3 * dx,
                                           static_cast<int>(std::floor(g.y)) + 3 * dy);
    if (ex >= 0) {
      std::vector<const Animals::Member *> happy;
      for (const Animals::Member &a : this->animals->members())
        if (a.exhibit == ex && a.happiness >= 81 && a.happiness <= 100)
          happy.push_back(&a);
      if (!happy.empty()) {
        const Animals::Member *a = happy[std::uniform_int_distribution<size_t>(0, happy.size() - 1)(this->rng)];
        think(g, 10098, a->name, a->id, ex);
      }
    }
  }
}

void Guests::rideBack(Guest &g) {
  // Task82_atTarget 0x5a05c8: the ride over, the nearest building of its
  // ride's type (ZTGuest slot 0x2b0, 0x5a05e7: findNearestOfKind by +0x284)
  // and back to its return slot on the water, as it rode
  // (guestGoToBuilding 0x43b2f6 on a ride: 0x5a069f); none, or no way
  // there: in the water (Task83)
  int best = -1, bx = 0, by = 0;
  float bestD = 1e9f;
  if (this->objects) {
    const auto &objs = this->objects->objects();
    for (size_t i = 0; i < objs.size(); i++) {
      const PlacedObjects::Object &o = objs[i];
      if (o.fence)
        continue;
      const BuildingInfo &b = infoOf(PlacedObjects::fileOf(o));
      if (!b.returnBuilding || (b.nameId ? b.nameId : 1) != g.ride)
        continue;
      // (a cUserTracker - the dolphin shack - takes back only its own riders:
      // the building's use check 0x42ac87, 0x59ff13 - its type the ride's,
      // and the guest in its users list, +0x158)
      if (b.tracker && o.id != g.rideFrom)
        continue;
      for (const BuildingInfo::Slot &s : b.slots) {
        if (!s.isReturn)
          continue;
        float x, y;
        slotPoint(o, s.ex, s.ey, x, y);
        const float d = std::hypot(x - g.x, y - g.y);
        if (d < bestD) {
          bestD = d;
          best = static_cast<int>(i);
          bx = static_cast<int>(std::floor(x));
          by = static_cast<int>(std::floor(y));
        }
      }
    }
  }
  std::vector<Move> moves = behaviour(g, g.rideSet);
  const Move m = moves.empty() ? Move{"swim", "", 56, 0} : moves[0];
  const bool there = best >= 0 && static_cast<int>(std::floor(g.x)) == bx && static_cast<int>(std::floor(g.y)) == by;
  ZT_STEP("0x5a05e7", "The ride over: back to the nearest of its kind", {"found", best >= 0 ? "yes" : "no"},
          {"distance", best >= 0 ? bestD : 0.0f});
  if (best >= 0 && (there || waterTo(g, bx, by, m))) {
    g.building = best;
    g.need = "fun";
    g.waterTask = kRideBackTask;
    g.idling = false;
    if (there)
      g.playFor = 0;
    return;
  }
  inWater(g);
}

void Guests::inWater(Guest &g) {
  // Task83_InWater's ctor (0x5a5f69): in the water (+0x284 |= 0x80000,
  // 0x274(1)), rand() % 3 + 2 tries to reach land
  g.state = State::Water;
  g.waterTask = kInWaterTask;
  g.inWater = true;
  g.tries = std::uniform_int_distribution<int>(0, 0x7fff)(this->rng) % 3 + 2;
  g.building = -1;
  g.need.clear();
  g.hidden = false;
  g.path.clear();
  g.pathAt = 0;
  g.playFor = 0;
  ZT_STEP("0x5a5f69", "In the water: making for land", {"tries", g.tries});
}

void Guests::drown(Guest &g) {
  // Task84_Drown::begin (0x61a705): a man (+0x320 female and +0x324 child
  // both 0, as ZTGuest::setup 0x4f6372 sets them by kind) - "%s is
  // drowning!" (10528, kind 2), "Help! I'm going to drown...." (10566), off
  // his ride (0x294(0)), and bSuitDrown if he came out of the changing room
  // (his set bReturnchroom0), else bSurfDrown. Anyone else is put on the
  // nearest land - rings round it, 150 at most, the first tile in the zoo,
  // not water, not a tank, out of the exhibits (0x61aaff, 0x4f7fbd) - and is
  // off the ride, out of the water
  const Type &t = this->typeList[g.type];
  if (!t.female && !t.child) {
    if (this->rm) {
      std::string s = this->rm->getString(10528);
      if (size_t at = s.find("%s"); at != std::string::npos)
        s.replace(at, 2, g.name);
      this->notices.push_back({s, 2, g.id});
    }
    think(g, 10566);
    g.rideSet = g.rideSet == "breturnchroom0" ? "bsuitdrown" : "bsurfdrown";
    g.ride = 0;
    g.state = State::Water;
    g.waterTask = kDrownTask;
    g.drownStep = 0;
    g.path.clear();
    g.pathAt = 0;
    g.playFor = 0;
    ZT_STEP("0x61a705", "Drowning", {"set", g.rideSet});
    return;
  }
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  for (int r = 1; r <= 150; r++)
    for (int y = gy - r; y <= gy + r; y++)
      for (int x = gx - r; x <= gx + r; x++) {
        if (std::max(std::abs(x - gx), std::abs(y - gy)) != r || !fleeGround(x, y))
          continue;
        g.x = x + 0.5f;
        g.y = y + 0.5f;
        endRide(g);
        g.path.clear();
        g.pathAt = 0;
        g.recheck = true;
        ZT_STEP("0x61a93c", "Not a man: put on the nearest land", {"x", x}, {"y", y});
        decide(g);
        return;
      }
  // (ours: nowhere at all - at rest on the water a while, then again)
  play(g, t.anims.count("water_idle") ? "water_idle" : "stand", 2.0f);
}

void Guests::outOfWater(Guest &g) {
  // Task7f_GuestInWater::begin (0x49a7ae): out, to the way out (a tank's
  // edge - ours: the nearest path tile it can swim to, 20 tiles round at
  // most), swimming (bInWater); none: in the water (Task83)
  g.guide = -1;
  g.tourHeard = false;
  g.building = -1;
  g.need.clear();
  g.target = -1;
  g.gawkObject = -1;
  g.state = State::Water;
  g.waterTask = kOutOfWaterTask;
  std::vector<Move> moves = behaviour(g, "binwater");
  const Move m = moves.empty() ? Move{"swim", "", 56, 0} : moves[0];
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  for (int r = 1; r <= 20; r++)
    for (int y = gy - r; y <= gy + r; y++)
      for (int x = gx - r; x <= gx + r; x++)
        if (std::max(std::abs(x - gx), std::abs(y - gy)) == r && walkable(x, y) && waterTo(g, x, y, m))
          return;
  inWater(g);
}

void Guests::waterNext(Guest &g) {
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  const MapTile *tile = this->map ? this->map->getTile(gx, gy) : nullptr;
  const bool wet = tile && (tile->terrainType == 9 || tile->terrainType == 10);
  const Type &t = this->typeList[g.type];
  switch (g.waterTask) {
  case kRideTask:
    // Task82's checkArrived (0x59fe03): a leg or an idle over - the next,
    // or with its legs below 0, the ride's over
    if (g.legs < 0)
      rideBack(g);
    else
      rideLeg(g);
    return;
  case kRideBackTask:
    // At its return slot: in (as any building - cTimeInside, then guestBuy
    // on a ride: out on land)
    if (g.building >= 0 && this->objects && g.building < static_cast<int>(this->objects->objects().size())) {
      useBuilding(g);
      if (g.state == State::Using)
        return;
    }
    inWater(g);
    return;
  case kInWaterTask: {
    // Task83's checkArrived (0x5a5ccc) and atTarget (0x5a5d73): on land with
    // tries left, out (the ride and the water cleared); none left, drowning
    if (!wet) {
      if (g.tries > 0) {
        ZT_STEP("0x5a5d73", "Made it to land", {"tries left", g.tries});
        endRide(g);
        g.recheck = true;
        decide(g);
      } else {
        drown(g);
      }
      return;
    }
    if (g.tries <= 0) {
      drown(g);
      return;
    }
    // Its whileGoing (0x5a5d09): 10 rounds of 30 tiles at rand() % 10 - 5
    // of its own - land, in the zoo, reachable - as it rode (its set; none,
    // bInWater); none, straight on east (x + 1...) to the first land
    // (0x5e8c61); none there either, a try fewer (0x5e8d47)
    std::vector<Move> moves = behaviour(g, g.rideSet.empty() ? std::string("binwater") : g.rideSet);
    if (moves.empty())
      moves = behaviour(g, "binwater");
    const Move m = moves.empty() ? Move{"swim", "", 56, 0} : moves[0];
    std::uniform_int_distribution<int> r(0, 0x7fff);
    auto land = [&](int x, int y) {
      const MapTile *lt = this->map->getTile(x, y);
      return lt && lt->terrainType != 9 && lt->terrainType != 10 && fleeGround(x, y);
    };
    for (int round = 0; round < 10; round++)
      for (int k = 0; k < 30; k++) {
        const int x = gx + r(this->rng) % 10 - 5, y = gy + r(this->rng) % 10 - 5;
        if (land(x, y) && waterTo(g, x, y, m))
          return;
      }
    int x = gx + 1;
    while (x < this->map->getWidth()) {
      const MapTile *lt = this->map->getTile(x, gy);
      if (!lt || (lt->terrainType != 9 && lt->terrainType != 10))
        break;
      x++;
    }
    if (x < this->map->getWidth() && land(x, gy) && waterTo(g, x, gy, m))
      return;
    g.tries--;
    ZT_STEP("0x5e8d47", "No way to land: a try fewer", {"tries left", g.tries});
    if (g.tries <= 0)
      drown(g);
    else
      play(g, t.anims.count("water_idle") ? "water_idle" : "stand", 0.0f);
    return;
  }
  case kDrownTask: {
    // bSuitDrown / bSurfDrown: fPlayWithSound(<drown>, mandrown), fPlay
    // (<drown>), twice; then (Task84's checkArrived 0x61ab50, atTarget
    // 0x61abb2) "%s has drowned!" (10251, kind 2) and he's gone (0x4fa78e)
    std::vector<Move> seq = behaviour(g, g.rideSet);
    if (g.drownStep < static_cast<int>(seq.size())) {
      const Move &m = seq[g.drownStep++];
      const std::string anim = t.anims.count(m.anim) ? m.anim : "water_idle";
      float secs = 1.5f;
      if (Animation *a = this->art(g, anim))
        if (a->frameTimeMs() > 0 && a->frameCount() > 0)
          secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
      play(g, anim, secs);
      if (!m.sound.empty() && this->sounds && this->soundFile.count(m.sound))
        Sound::get().play(this->soundFile[m.sound], this->soundAtten[m.sound] + 600);
      return;
    }
    if (this->rm) {
      std::string s = this->rm->getString(10251);
      if (size_t at = s.find("%s"); at != std::string::npos)
        s.replace(at, 2, g.name);
      this->notices.push_back({s, 2, g.id});
    }
    ZT_STEP("0x61abb2", "Drowned", {"guest", g.name});
    g.x = -1000; // (taken out of the zoo)
    return;
  }
  case kOutOfWaterTask:
    // Task7f's atTarget (0x49a4ec): out - its next task, no longer afraid
    if (!wet) {
      g.waterTask = 0;
      g.state = State::Walking;
      g.sawPredator = false;
      g.recheck = true;
      decide(g);
    } else {
      outOfWater(g);
    }
    return;
  default:
    inWater(g);
    return;
  }
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
  // Set up as zoo.exe ZTGuest::setup 0x4f6372: cInitialHappiness; every
  // cRandomStatNth-th guest (a roll of 0 in it) one need of four picked
  // (hunger, thirst, energy, bathroom) starts in its cRandom<Need>Min..Max
  // (the max itself never: rand % (max - min) + min), when that range is
  // whole (min below max); the rest start at nothing
  g.happiness = static_cast<float>(t.initialHappiness);
  g.name = (this->rm ? this->rm->getString(9507) : std::string("Guest")) + " " + std::to_string(g.id);
  g.arrived = this->clock;
  if (this->randomStatNth > 0 && std::uniform_int_distribution<int>(0, this->randomStatNth - 1)(this->rng) == 0) {
    const int k = std::uniform_int_distribution<int>(0, 3)(this->rng);
    if (this->randomMin[k] < this->randomMax[k]) {
      const float v = static_cast<float>(
          std::uniform_int_distribution<int>(this->randomMin[k], this->randomMax[k] - 1)(this->rng));
      float *need[4] = {&g.hunger, &g.thirst, &g.tired, &g.bathroom};
      *need[k] = v;
    }
  }
  // Each check's countdown starts somewhere in its period (rand() % its
  // c...Check)
  auto start = [&](int n) { return n > 0 ? std::uniform_int_distribution<int>(0, n - 1)(this->rng) : 0; };
  g.hungerCheck = start(t.hungerCheck);
  g.thirstCheck = start(t.thirstCheck);
  g.bathroomCheck = start(t.bathroomCheck);
  g.leaveCheck = start(t.leaveCheck);
  g.souvenirCheck = start(t.souvenirCheck);
  g.energyCheck = start(t.energyCheck);
  g.chaseCheck = start(t.chaseCheck);
  g.trashCheck = start(t.trashCheck);
  g.viewCheck = start(t.viewingCheck);
  g.environmentCheck = start(t.environmentCheck);
  g.sawResetCheck = t.sawReset; // (+0x314: cSawAnimalReset itself, 0x4f69a5)
  // A favourite animal (zoo.exe 0x4f60f5, ZTGuest +0x31c): any species but
  // the special ones (cIsSpecialAnimal = 1: ZTAnimalType +0x3d1)
  if (this->animals && !this->animals->types().empty()) {
    const auto &types = this->animals->types();
    if (this->specialFor != types.size()) {
      this->specialFor = types.size();
      this->specialAnimals.clear();
      if (this->rm)
        for (size_t k = 0; k < types.size(); k++)
          if (IniReader *ai = this->rm->getIniReader(types[k].file)) {
            bool special = ai->getInt("characteristics/integers", "cisspecialanimal", 0) == 1;
            for (const char *sub : {"m", "f"})
              special = special || ai->getInt(std::string(sub) + "/characteristics/integers", "cisspecialanimal", 0) == 1;
            if (special)
              this->specialAnimals.insert(static_cast<int>(k));
            delete ai;
          }
    }
    std::vector<int> pick;
    for (size_t k = 0; k < types.size(); k++)
      if (!this->specialAnimals.count(static_cast<int>(k)))
        pick.push_back(static_cast<int>(k));
    if (!pick.empty())
      g.favourite = pick[std::uniform_int_distribution<size_t>(0, pick.size() - 1)(this->rng)];
  }
  double fee = admission * Original::economy().admissionMultiple * (t.child ? 0.5 : 1.0);
  {
    ZT_WHO("guest", g.id);
    ZT_STEP("0x424375", "Arrives at the gate", {"name", g.name}, {"type", t.key}, {"happiness", g.happiness},
            {"hunger", g.hunger}, {"thirst", g.thirst}, {"admission paid", fee});
  }
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
        attract += static_cast<float>(this->animals->types()[m.type].forAge(m.baby).attractiveness); // (a young one's own: its [y/...])
        favourite = favourite || m.type == g.favourite;
      }
    if (!any)
      continue;
    // Where it'd watch from (zoo.exe 0x428741(0)): one of its viewing areas
    // that sees some of it (0x43aac6), at random - none, not this one; then
    // one of that area's viewing tiles (+0x40), at random. ours, with no
    // areas given: any path tile beside it
    std::vector<std::pair<int, int>> view;
    if (this->viewAreas) {
      std::vector<ViewArea> seeing;
      for (ViewArea &a : this->viewAreas(ex.id))
        if (!a.seen.empty() && !a.viewing.empty())
          seeing.push_back(std::move(a));
      if (seeing.empty())
        continue;
      view = seeing[std::uniform_int_distribution<size_t>(0, seeing.size() - 1)(this->rng)].viewing;
    } else {
      for (auto [x, y] : ex.tiles) {
        const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto &dd : d)
          if (walkable(x + dd[0], y + dd[1]))
            view.push_back({x + dd[0], y + dd[1]});
      }
    }
    if (view.empty())
      continue;
    // (zoo.exe 0x428917: a random viewing tile's squared distance in whole
    // tiles, at most 990 (0x3de); the habitat's attractiveness (+0xf8), 500
    // more with its favourite species, less (1000 - d2) / 10 in whole
    // numbers, plus 100 - so a nearer exhibit scores lower, and guests go
    // out across the zoo; the highest taken)
    auto [tx, ty] = view[std::uniform_int_distribution<size_t>(0, view.size() - 1)(this->rng)];
    const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
    const int d2 = std::min((tx - gx) * (tx - gx) + (ty - gy) * (ty - gy), 990);
    float score = attract + (favourite ? 500.0f : 0.0f) - static_cast<float>((1000 - d2) / 10) + 100;
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
    // (zoo.exe guestJoinTour 0x58d79e: not if busy - ZTGuest::isBusy
    // 0x58d9c0: off to, going into, in or out of a building (tasks
    // 0x67-0x6a), going to eat or eating (0x6b-0x6d), leaving (0x71,
    // +0x339), fleeing (0x75) or having seen a predator (+0x24c), sick (0x79)
    // or smell-sick (+0x336) - nor at any need's threshold (0x58d5eb:
    // c<Need>Threshold or more). Watching an exhibit isn't busy: a group
    // just talked to can follow on. ours: not on another tour, mauled or
    // in a show either)
    const Type &t = this->typeList[g.type];
    if (g.guide >= 0 || g.leaving || g.building >= 0 || g.hidden || g.showTank >= 0 || g.smelled || g.puking ||
        g.sawPredator || g.state == State::Fleeing || g.state == State::Caught || g.state == State::Leaving ||
        g.state == State::Water)
      return false;
    if (g.hunger >= t.hungerThreshold || g.thirst >= t.thirstThreshold || g.tired >= t.energyThreshold ||
        g.bathroom >= t.bathroomThreshold)
      return false;
    // Where to (zoo.exe 0x58d809): 1 in 10 the guide's own spot; else the
    // four tiles round it (0x4171a1: north, east, south, west) from a random
    // one on to the last, then the first (0x58d87c-0x58dabd), the first out
    // of any exhibit (its habitat's +0x2c) it has a route to
    auto routeTo = [&](int x, int y) {
      if (walkTo(g, x, y, t.speed))
        return true;
      // ours: already on the tile it's sent to - no walk to make (walkTo
      // has no path for it): there next frame, as if it had walked
      if (static_cast<int>(std::floor(g.x)) != x || static_cast<int>(std::floor(g.y)) != y)
        return false;
      g.path = {{g.x, g.y}};
      g.pathAt = 0;
      g.speed = t.speed;
      return true;
    };
    bool routed = false;
    if (std::uniform_int_distribution<int>(0, 9)(this->rng) == 0) {
      routed = routeTo(tx, ty);
    } else {
      const std::pair<int, int> tiles[4] = {{tx, ty - 1}, {tx + 1, ty}, {tx, ty + 1}, {tx - 1, ty}};
      const int n = 4;
      for (int k = std::uniform_int_distribution<int>(0, n - 1)(this->rng); k <= n && !routed; k++)
        if (this->fences->exhibitAt(tiles[k % n].first, tiles[k % n].second) < 0)
          routed = routeTo(tiles[k % n].first, tiles[k % n].second);
    }
    if (!routed)
      return false;
    g.state = State::Walking;
    g.guide = guide;
    g.tourHeard = false;
    g.target = exhibit;
    g.seen.erase(exhibit);
    return true;
  }
  return false;
}

// Ground a guest may run over from a predator: the paths, or open ground in
// the zoo - not in an exhibit, on water or where something stands (ours: as
// a keeper's open ground)
bool Guests::fleeGround(int x, int y) const {
  if (walkable(x, y))
    return true;
  if (!this->map || !this->fences || this->fences->exhibitAt(x, y) >= 0 || !this->fences->insideZoo(x, y))
    return false;
  if (this->index && this->index->blocked(x, y))
    return false;
  const MapTile *t = this->map->getTile(x, y);
  return t && t->terrainType != 9 && t->terrainType != 10 &&
         this->fences->tileFit(x, y, *this->map) == Fences::Fit::Ok;
}

// The predator a guest sees (zoo.exe animalFindPredator 0x4369b9, the
// guest's class slot 0x2fc): one that would eat it - an animal whose cPrey
// has Man (9503), whatever kind of guest, not an egg (ZTGuest slot 0x304,
// 0x4138f0; ours also not young or crated). The one it's already fleeing
// while within cPredatorRadius tiles either way; else, out on the zoo's
// ground, the nearest loose one, its tiles' squared distance under
// cPredatorRadius squared; in an exhibit, the nearest in that exhibit
int Guests::findPredator(const Guest &g) const {
  if (!this->animals || !this->fences)
    return -1;
  auto hunts = [&](const Animals::Member &m) {
    return !m.boxed && !m.baby && !m.egg && this->animals->types()[m.type].eatsPeople;
  };
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  const int r = this->predatorRadius;
  if (g.predator >= 0)
    if (const Animals::Member *m = this->animals->member(g.predator))
      if (hunts(*m) && std::abs(static_cast<int>(std::floor(m->x)) - gx) <= r &&
          std::abs(static_cast<int>(std::floor(m->y)) - gy) <= r)
        return m->id;
  const int ex = this->fences->exhibitAt(gx, gy);
  const Animals::Member *best = nullptr;
  int bestD = r * r;
  for (const Animals::Member &m : this->animals->members()) {
    if (!hunts(m))
      continue;
    const int mx = static_cast<int>(std::floor(m.x)), my = static_cast<int>(std::floor(m.y));
    if (ex < 0 ? !m.escaped : (this->fences->exhibitAt(mx, my) != ex || std::abs(mx - gx) > r || std::abs(my - gy) > r))
      continue;
    const int d = (mx - gx) * (mx - gx) + (my - gy) * (my - gy);
    if (d < bestD) {
      bestD = d;
      best = &m;
    }
  }
  return best ? best->id : -1;
}

// How crowded the viewing area it stands in is (zoo.exe 0x425f6a, its area
// by 0x4232f4): the guests on the area's viewing tiles over how many tiles
// it has, times 10, cut to a whole number; no area, 0. ours: the area of the
// exhibit it's at whose viewing tiles hold its tile; the counts brought up
// to date once a second of game time (World's viewAreas)
int Guests::areaCrowd(const Guest &g) const {
  if (!this->viewAreas || g.target < 0)
    return 0;
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  for (const ViewArea &a : this->viewAreas(g.target))
    if (!a.viewing.empty() && std::find(a.viewing.begin(), a.viewing.end(), std::make_pair(gx, gy)) != a.viewing.end())
      return static_cast<int>(static_cast<float>(a.guests) / static_cast<float>(a.viewing.size()) * 10.0f);
  return 0;
}

// zoo.exe 0x4232f4 (the guest's area: 0x43b92e, from its tile's slots, a
// random way first - ours: its exhibit's area with its tile among the
// viewing tiles, one at most) and the area's counts (0x447a00: every
// animal standing on one of the area's seen tiles, each once - +0x10
// how many (0x42330b), +0xc how many kinds (0x426013), +0x18 how many sick,
// animal +0x3a7 (0x425f2e, 0x589693))
bool Guests::areaSight(const Guest &g, Sight &out) const {
  out = Sight();
  if (!this->viewAreas || g.target < 0 || !this->animals)
    return false;
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  for (const ViewArea &a : this->viewAreas(g.target)) {
    if (std::find(a.viewing.begin(), a.viewing.end(), std::make_pair(gx, gy)) == a.viewing.end())
      continue;
    if (a.seen.empty())
      return false; // (0x43aac6: an area that sees nothing isn't one)
    const std::set<std::pair<int, int>> seen(a.seen.begin(), a.seen.end());
    std::set<int> kinds;
    for (const Animals::Member &m : this->animals->members()) {
      if (!seen.count({static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y))}))
        continue;
      out.animals++;
      kinds.insert(m.type);
      out.sick += m.sick ? 1 : 0;
    }
    out.species = static_cast<int>(kinds.size());
    return true;
  }
  return false;
}

// zoo.exe 0x426049: a random start in the exhibit's animal list (rand %
// its size); the first animal from there on whose happiness (+0x2a8) is
// lo..hi, else the last such before it
int Guests::pickAnimal(int exhibit, int lo, int hi) {
  std::vector<const Animals::Member *> list;
  for (const Animals::Member &m : this->animals->members())
    if (m.exhibit == exhibit)
      list.push_back(&m);
  if (list.empty())
    return -1;
  const size_t r = std::uniform_int_distribution<size_t>(0, list.size() - 1)(this->rng);
  int found = -1;
  for (size_t i = 0; i < list.size(); i++) {
    const int h = static_cast<int>(list[i]->happiness);
    if (h < lo || h > hi)
      continue;
    found = list[i]->id;
    if (r <= i)
      return found;
  }
  return found;
}

// zoo.exe 0x42e793: how long a guest watches - by its exhibit's average
// suitability (habitat +0xe8, 0x423354: 0x444827 averages its animals'
// species' ratings) its type's cGawkTime: up to -60 the first (its lower
// limit, type +0x1ec, is never reached: suitability isn't under 0), to 20
// the second, to 60, to 80, to 100, each with 0-5 more at random (else
// 10-13); then cGawkAttrTime by the exhibit's attractiveness (+0xf8): under
// 20, 50, 90, 150, 200, or more
int Guests::gawkSeconds(const Guest &g) {
  const Type &t = this->typeList[g.type];
  const int h = this->animals->exhibitSuitability(g.target);
  std::uniform_int_distribution<int> r6(0, 5);
  int secs = std::uniform_int_distribution<int>(0, 3)(this->rng) + 10;
  if (h <= -60)
    secs = t.gawkTime[0];
  else if (h <= 20)
    secs = t.gawkTime[1] + r6(this->rng);
  else if (h <= 60)
    secs = t.gawkTime[2] + r6(this->rng);
  else if (h <= 80)
    secs = t.gawkTime[3] + r6(this->rng);
  else if (h <= 100)
    secs = t.gawkTime[4] + r6(this->rng);
  int attract = 0;
  for (const Animals::Member &m : this->animals->members())
    if (m.exhibit == g.target)
      attract += static_cast<int>(this->animals->types()[m.type].forAge(m.baby).attractiveness); // (a young one's own: its [y/...])
  secs += t.gawkAttrTime[attract < 20 ? 0 : attract < 50 ? 1 : attract < 90 ? 2 : attract < 150 ? 3 : attract < 200 ? 4 : 5];
  return secs;
}

int Guests::nearestPrey(float x, float y, float radius) const {
  int best = -1;
  float bestD = 1e30f;
  for (const Guest &g : this->list) {
    // (ours: not one out on the water - its ride isn't let go for it)
    if (g.x < -999 || g.attacked || g.hidden || g.state == State::Caught || g.state == State::Water ||
        this->typeList[g.type].kind != Kind::Man)
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
    g.guide = -1; g.tourHeard = false;
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
  ZT_FN("", "Decides where to go next");
  const Type &t = this->typeList[g.type];
  if (g.leaving) {
    g.guide = -1; g.tourHeard = false;
    ZT_STEP("", "Leaving: off to the gate", {"happiness", g.happiness});
    g.state = State::Leaving;
    if (!walkTo(g, this->entranceX, this->entranceY, t.speed))
      g.x = -1000; // (nowhere to walk: gone)
    return;
  }
  // Done being sick (Task79_GuestSick::atTarget 0x5b0621: +0x33e set)
  if (g.puking) {
    g.puking = false;
    g.recheck = true;
  }
  // Sick from a smell (zoo.exe chooseTask 0x5b0509: +0x336 set, not in a
  // building): cSickChance in 100, then Task79_GuestSick (begin 0x5b0635:
  // its bSick - fPlayWithSound(puke, <kind>_puke) - and "Ohh, I don't feel
  // so good. I think I'm going to ..." (10285))
  if (g.smelled && g.building < 0) {
    const int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
    ZT_STEP("0x5b0509", "Smell-sick: is it sick? (cSickChance)", {"roll", roll}, {"cSickChance", t.sickChance});
    if (roll < t.sickChance && t.anims.count("puke")) {
      g.state = State::Walking;
      g.path.clear();
      g.pathAt = 0;
      g.target = -1;
      g.gawkObject = -1;
      g.guide = -1;
      g.tourHeard = false;
      g.puking = true;
      float secs = 2.0f;
      if (Animation *a = this->art(g, "puke"))
        if (a->frameTimeMs() > 0 && a->frameCount() > 0)
          secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
      play(g, "puke", secs);
      const std::string sound = t.key + "_puke";
      if (this->sounds && this->soundFile.count(sound))
        Sound::get().play(this->soundFile[sound], this->soundAtten[sound] + 600);
      think(g, kFeelSick);
      return;
    }
  }
  g.state = State::Walking;
  g.gawkObject = -1;
  // Where next (zoo.exe guestPickAttraction 0x4282e1): an exhibit it
  // hasn't seen, an attraction ("fun" buildings) it hasn't been to, or
  // scenery to look at - drawn by weight: the unseen exhibits' count, the
  // attractions' count, and the scenery's (its cEstheticWeight / 100, at
  // least 1: 0x428039)
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
    const int scenery = sceneryWeight(g);
    const int total = unseen + static_cast<int>(fun.size()) + scenery;
    const int roll = total > 0 ? std::uniform_int_distribution<int>(0, total - 1)(this->rng) : 0;
    if (total > 0 && roll >= unseen && roll < unseen + static_cast<int>(fun.size())) {
      const PlacedObjects::Object &o = objs[fun[std::uniform_int_distribution<size_t>(0, fun.size() - 1)(this->rng)]];
      ZT_STEP("0x4282e1", "Picks an attraction it hasn't been to", {"attraction", o.typeName},
              {"attractions left", static_cast<int>(fun.size())}, {"exhibits unseen", unseen});
      g.visited.insert(o.id);
      if (seekBuilding(g, "fun", 1000))
        return;
    } else if (total > 0 && roll >= unseen + static_cast<int>(fun.size())) {
      int sx = -1, sy = -1;
      const int id = pickScenery(g, sx, sy);
      ZT_STEP("0x42945f", "Picks scenery to look at", {"object", id}, {"scenery weight", scenery}, {"to x", sx},
              {"to y", sy});
      if (id >= 0 && walkTo(g, sx, sy, t.speed)) {
        g.gawkObject = id;
        g.target = -1;
        return;
      }
    }
  }
  int vx = -1, vy = -1;
  int ex = pickExhibit(g, vx, vy);
  ZT_STEP("", "Picks an exhibit it hasn't seen (attractiveness, favourite, distance)", {"exhibit", ex},
          {"seen", static_cast<int>(g.seen.size())}, {"to x", vx}, {"to y", vy});
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
    g.gawkedKinds.clear();
    if (std::uniform_int_distribution<int>(0, 99)(this->rng) < this->typeList[g.type].leaveDone) {
      ZT_STEP("", "Seen it all: leaves (cLeaveChanceDone)");
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

// Testing an electric fence instead of watching (zoo.exe 0x425af4, 0x425e30):
// come to an exhibit, a cTestFenceChance in 100 (guests.ai: 20) to touch a
// live electric fence on a side of its tile - those with the fencezap
// animation (the man) - and zapped: bTestFence (fencezap, the zap sound),
// cZapHappinessHit (-35) and "Yow! I just got electrocuted by %s, this
// place is dangerous." (10249)
bool Guests::testFence(Guest &g) {
  const Type &t = this->typeList[g.type];
  if (t.testFenceChance <= 0 || !t.anims.count("fencezap"))
    return false;
  int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
  if (roll >= t.testFenceChance)
    return false;
  int tx = static_cast<int>(std::floor(g.x)), ty = static_cast<int>(std::floor(g.y));
  static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}; // N E S W (zoo.exe 0, 2, 4, 6)
  for (const auto &d : dirs) {
    Fences::Edge e = d[0] ? Fences::Edge{false, d[0] > 0 ? tx + 1 : tx, ty}
                          : Fences::Edge{true, tx, d[1] > 0 ? ty + 1 : ty};
    if (!this->fences->liveElectric(e))
      continue;
    ZT_FN("0x425af4", "Tests an electric fence");
    g.fx = static_cast<float>(d[0]);
    g.fy = static_cast<float>(d[1]);
    float secs = 2.0f;
    if (Animation *a = this->art(g, "fencezap"))
      if (a->frameTimeMs() > 0 && a->frameCount() > 0)
        secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
    play(g, "fencezap", secs);
    g.seen.insert(g.target);
    g.target = -1;
    float was = g.dHappy;
    g.dHappy += static_cast<float>(t.zapHit);
    if (this->sounds && this->soundFile.count("zap"))
      Sound::get().play(this->soundFile["zap"], this->soundAtten["zap"] + 600);
    const Fences::Piece *p = this->fences->at(e);
    std::string fence = p ? this->fences->types()[p->type].name : "";
    think(g, 10249, fence);
    ZT_STEP("0x425af4", "Zapped testing the fence", {"roll", roll}, {"cTestFenceChance", t.testFenceChance},
            {"cZapHappinessHit", t.zapHit}, {"happiness to come", was, g.dHappy}, {"fence", fence});
    return true;
  }
  return false;
}

// Gawking at an exhibit (zoo.exe Task70_ViewAnimals::begin 0x42e527): for
// as long as 0x42e793 says (gawkSeconds), playing its bGawk set
void Guests::gawk(Guest &g) {
  const Type &t = this->typeList[g.type];
  const float secs = static_cast<float>(gawkSeconds(g));
  const int h = this->animals->exhibitSuitability(g.target);
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
  ZT_STEP("0x42e793", "Watches the exhibit (longer, the more suitable and attractive)", {"exhibit", g.target},
          {"suitability", h}, {"seconds", secs}, {"doing", anim});
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
// Scenery to look at (zoo.exe guestPickAttraction 0x4282e1, its third
// group). The zoo's list of it (0x639098, filled by 0x451785 from
// 0x4f40af): objects whose kind has a cEstheticWeight over 0, standing
// inside the zoo; a guest looks at one kind only once (0x4285e2)
// ----------------------------------------------------------------------------
int Guests::sceneryWeight(const Guest &g) const {
  if (!this->objects || !this->fences)
    return 0;
  int sum = 0;
  for (const PlacedObjects::Object &o : this->objects->objects()) {
    if (o.fence)
      continue;
    const std::string file = PlacedObjects::fileOf(o);
    const BuildingInfo &b = infoOf(file);
    if (b.estheticWeight <= 0 || g.gawkedKinds.count(file) ||
        !this->fences->insideZoo(static_cast<int>(std::floor(o.x)), static_cast<int>(std::floor(o.y))))
      continue;
    sum += b.estheticWeight;
  }
  // (zoo.exe 0x428039: the sum / 100, at least 1 when there's any)
  return sum > 0 ? std::max(1, sum / 100) : 0;
}

// The one it goes to (zoo.exe 0x42945f): within a squared distance of 1000
// of the guest, the highest ((cEstheticWeight + 100) x 10 - d2) / 200 in
// whole numbers; it stands on a tile round its footprint - a path tile if
// there's one, at random (0x4297dc) - only in front of it when its kind is
// cGawkOnlyFromFront
int Guests::pickScenery(Guest &g, int &tx, int &ty) {
  if (!this->objects || !this->fences)
    return -1;
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  const PlacedObjects::Object *best = nullptr;
  int bestScore = -1;
  for (const PlacedObjects::Object &o : this->objects->objects()) {
    if (o.fence)
      continue;
    const std::string file = PlacedObjects::fileOf(o);
    const BuildingInfo &b = infoOf(file);
    const int ox = static_cast<int>(std::floor(o.x)), oy = static_cast<int>(std::floor(o.y));
    if (b.estheticWeight <= 0 || g.gawkedKinds.count(file) || !this->fences->insideZoo(ox, oy))
      continue;
    const int d2 = (ox - gx) * (ox - gx) + (oy - gy) * (oy - gy);
    if (d2 >= 1000)
      continue;
    const int score = ((b.estheticWeight + 100) * 10 - d2) / 200;
    if (score > bestScore) {
      bestScore = score;
      best = &o;
    }
  }
  if (!best)
    return -1;
  const BuildingInfo &b = infoOf(PlacedObjects::fileOf(*best));
  // Its footprint in tiles (cFootprint in half tiles), turned with it
  int w = (b.fx + 1) / 2, h = (b.fy + 1) / 2;
  const int facing = best->facing & 6;
  if (facing == 2 || facing == 6)
    std::swap(w, h);
  const int x0 = static_cast<int>(std::floor(best->x - w / 2.0f)), y0 = static_cast<int>(std::floor(best->y - h / 2.0f));
  const int x1 = x0 + w, y1 = y0 + h;
  std::vector<std::pair<int, int>> paths, others;
  for (int y = y0 - 1; y <= y1; y++)
    for (int x = x0 - 1; x <= x1; x++) {
      if (x != x0 - 1 && x != x1 && y != y0 - 1 && y != y1)
        continue;
      if (b.gawkFront && !(facing == 0 ? y == y0 - 1 : facing == 4 ? y == y1 : facing == 2 ? x == x1 : x == x0 - 1))
        continue;
      if (!walkable(x, y))
        continue;
      (this->map && this->map->isPath(x, y) ? paths : others).push_back({x, y});
    }
  const auto &from = paths.empty() ? others : paths;
  if (from.empty())
    return -1;
  auto spot = from[std::uniform_int_distribution<size_t>(0, from.size() - 1)(this->rng)];
  tx = spot.first;
  ty = spot.second;
  g.gawkedKinds.insert(PlacedObjects::fileOf(*best));
  return best->id;
}

// There (Task81_GawkObject, zoo.exe 0x4a6d68): it faces the object and
// plays bGawkObject1..6 by the object's EstheticBonus for its kind (<0,
// 0-5, 6-10, 11-15, 16-25, 26-100; over 100 nothing - 0x4a6ebd), each a
// draw of gawk, camera or stand, once; and it has the kind's cUsedThought
void Guests::gawkScenery(Guest &g) {
  const int id = g.gawkObject;
  g.gawkObject = -1;
  if (!this->objects)
    return;
  const PlacedObjects::Object *obj = nullptr;
  for (const PlacedObjects::Object &o : this->objects->objects())
    if (o.id == id)
      obj = &o;
  if (!obj)
    return;
  const Type &t = this->typeList[g.type];
  const BuildingInfo &b = infoOf(PlacedObjects::fileOf(*obj));
  auto it = b.estheticBonus.find(t.nameId);
  const int bonus = it == b.estheticBonus.end() ? 0 : it->second;
  const int band = bonus < 0 ? 0 : bonus <= 5 ? 1 : bonus <= 10 ? 2 : bonus <= 15 ? 3 : bonus <= 25 ? 4 : bonus <= 100 ? 5 : -1;
  if (band < 0)
    return;
  const int *w = t.gawkObjWeights[band];
  const int total = std::max(1, w[0] + w[1] + w[2]);
  const int roll = std::uniform_int_distribution<int>(0, total - 1)(this->rng);
  const std::string anim = roll < w[0] ? "gawk1" : roll < w[0] + w[1] ? "camera" : "stand";
  const float d = std::hypot(obj->x - g.x, obj->y - g.y);
  if (d > 0.01f) {
    g.fx = (obj->x - g.x) / d;
    g.fy = (obj->y - g.y) / d;
  }
  float secs = 2.0f;
  if (Animation *a = this->art(g, anim))
    if (a->frameTimeMs() > 0 && a->frameCount() > 0)
      secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
  ZT_STEP("0x4a6d68", "Looks at scenery (bGawkObject by its EstheticBonus)", {"object", obj->typeName},
          {"bonus", bonus}, {"band", band + 1}, {"doing", anim});
  play(g, anim, secs);
  g.state = State::Gawking;
  g.target = -1;
  if (b.usedThought)
    think(g, b.usedThought);
  if (anim == "camera" && this->sounds)
    Sound::get().play("guests/cam1", 1000);
}

// ----------------------------------------------------------------------------
// The status tick
// ----------------------------------------------------------------------------
// zoo.exe guestViewingTick 0x43afde: every cViewingAreaCheck while
// watching, what it thinks of what it sees - from the viewing area it
// stands in (0x43b92e; none: nothing), only when the area sees an animal:
// - sick ones seen: cSickAnimalChange; more than one, "There seems to be a
//   lot of sick animals in this exhibit." (10096); and "%s seems to be
//   sick." (10545) of one of the exhibit's animals at random
// - by the exhibit's average suitability (0x423354, habitat +0xe8): 81 up
//   cHappyAnimalChange1, 61-80 cHappyAnimalChange2, -60-60
//   cAngryAnimalChange1, -80 to -61 cAngryAnimalChange2, under
//   cAngryAnimalChange3 - each with its thought (10098, 10281, 10282 or
//   under 0 10284, 10283, 10099) of an animal whose happiness is in that
//   band, when one is (0x426049)
// - kinds seen at cDifferentSpeciesThreshold or more:
//   cDifferentSpeciesChange, "I like the way they have grouped animals in
//   this zoo." (10100)
// - its area crowded (0x425f6a) at cCrowdedViewingThreshold or more:
//   cCrowdedViewingChange and "I can't see %s.  It's too crowded." (10102)
//   of any of its animals
void Guests::viewing(Guest &g) {
  if (g.state != State::Gawking || g.target < 0)
    return;
  ZT_FN("0x43afde", "What it thinks of what it's watching");
  Sight sight;
  if (!areaSight(g, sight) || sight.animals <= 0)
    return;
  const Type &t = this->typeList[g.type];
  auto about = [&](int sid, int lo, int hi) {
    const int id = pickAnimal(g.target, lo, hi);
    if (id < 0)
      return;
    const Animals::Member *m = this->animals->member(id);
    think(g, sid, m ? m->name : std::string(), id, g.target);
  };
  if (sight.sick > 0) {
    g.dHappy += static_cast<float>(t.sickAnimalChange);
    if (sight.sick > 1)
      think(g, 10096);
    about(10545, -100, 100);
  }
  const int h = this->animals->exhibitSuitability(g.target);
  if (h >= 81) {
    g.dHappy += static_cast<float>(t.happyAnimal1);
    about(kOverjoyed, 81, 100);
  } else if (h >= 61) {
    g.dHappy += static_cast<float>(t.happyAnimal2);
    about(10281, 61, 80);
  } else if (h >= -60) {
    g.dHappy += static_cast<float>(t.angryAnimal1);
    if (h < 0)
      about(10284, -61, -1);
    else
      about(10282, 0, 60);
  } else if (h >= -80) {
    g.dHappy += static_cast<float>(t.angryAnimal2);
    about(10283, -80, -61);
  } else {
    g.dHappy += static_cast<float>(t.angryAnimal3);
    about(kVeryAngry, -100, -81);
  }
  ZT_STEP("0x43afde", "What it sees", {"animals seen", sight.animals}, {"kinds", sight.species}, {"sick", sight.sick},
          {"suitability", h}, {"dHappy", g.dHappy});
  if (sight.species >= t.speciesThreshold) {
    g.dHappy += static_cast<float>(t.speciesChange);
    think(g, kSpecies);
    ZT_STEP("0x43afde", "Many kinds seen: happier (cDifferentSpeciesThreshold)", {"kinds", sight.species});
  }
  // Crowding (zoo.exe 0x43ba98): its viewing area's crowd (0x425f6a) at
  // cCrowdedViewingThreshold or more
  const int crowd = areaCrowd(g);
  if (crowd >= t.crowdedThreshold) {
    g.dHappy += static_cast<float>(t.crowdedChange);
    about(kCrowded, -100, 100);
    ZT_STEP("0x43ba98", "Crowded: unhappier (cCrowdedViewingThreshold)", {"crowd", crowd},
            {"cCrowdedViewingThreshold", t.crowdedThreshold});
  }
}

// The blocks guests judge where they are by (zoo.exe blockGridRefresh
// 0x41ffdc, every 7.5 s by timer7500 0x43525b): the map in 5x5-tile blocks,
// each cleared, then every guest standing in it counted, and every object in
// it (the tiles' four cells) adding its [EstheticBonus] for each guest
// kind (cNameID 9503-9506) and its cStink (ZTSceneryType +0x11c), each over
// the cells it covers (the object's +0x150). Dung (poo.ai: cStink 1, -5 for
// every kind) and litter (trash.ai: -15) are such objects in the original.
// measured: the original's grid ([0x639004]+0x18, Play original, an
// everything zoo) - its six non-empty blocks' esthetic and stink equal
// ours, and blocks holding only fences are empty: fences don't count
void Guests::refreshBlocks() {
  ZT_FN("0x41ffdc", "The 5x5-tile blocks counted again (guests, esthetic, stink)");
  this->blocksW = this->map ? (this->map->getWidth() + 4) / 5 : 0;
  this->blocksH = this->map ? (this->map->getHeight() + 4) / 5 : 0;
  this->blocks.assign(static_cast<size_t>(this->blocksW) * this->blocksH, Block());
  auto at = [&](float x, float y) -> Block * {
    int bx = static_cast<int>(std::floor(x)) / 5, by = static_cast<int>(std::floor(y)) / 5;
    if (!(x >= 0 && y >= 0) || bx < 0 || by < 0 || bx >= this->blocksW || by >= this->blocksH)
      return nullptr;
    return &this->blocks[static_cast<size_t>(by) * this->blocksW + bx];
  };
  for (const Guest &g : this->list)
    if (Block *b = at(g.x, g.y))
      b->guests++;
  // (a thing's bonus and stink over its cells: half-tiles, its footprint)
  auto add = [&](const BuildingInfo &info, float cx, float cy, int fx, int fy, int stink) {
    fx = std::max(1, fx), fy = std::max(1, fy);
    const float cells = static_cast<float>(fx * fy);
    for (int j = 0; j < fy; j++)
      for (int i = 0; i < fx; i++) {
        Block *b = at(cx - fx / 4.0f + (i + 0.5f) / 2.0f, cy - fy / 4.0f + (j + 0.5f) / 2.0f);
        if (!b)
          continue;
        for (const auto &[kind, bonus] : info.estheticBonus)
          b->esthetic[kind] += bonus / cells;
        b->stink += stink / cells;
      }
  };
  if (this->objects)
    for (const PlacedObjects::Object &o : this->objects->objects()) {
      if (o.fence)
        continue;
      const BuildingInfo &info = infoOf(PlacedObjects::fileOf(o));
      int fx = info.fx, fy = info.fy;
      if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
        std::swap(fx, fy);
      add(info, o.x, o.y, fx, fy, 0);
    }
  if (this->items) {
    const BuildingInfo &poo = infoOf("scenery/other/poo.ai");
    const BuildingInfo &trash = infoOf("scenery/other/trash.ai");
    for (const ZooItems::Item &i : this->items->items())
      if (i.kind == ZooItems::Kind::Dung)
        add(poo, i.x, i.y, 1, 1, this->dungStink);
      else if (i.kind == ZooItems::Kind::Litter)
        add(trash, i.x, i.y, 1, 1, 0);
  }
}

const Guests::Block *Guests::blockAt(float x, float y) const {
  int bx = static_cast<int>(std::floor(x)) / 5, by = static_cast<int>(std::floor(y)) / 5;
  if (!(x >= 0 && y >= 0) || bx < 0 || by < 0 || bx >= this->blocksW || by >= this->blocksH ||
      this->blocks.size() != static_cast<size_t>(this->blocksW) * this->blocksH)
    return nullptr;
  return &this->blocks[static_cast<size_t>(by) * this->blocksW + bx];
}

// What the guest makes of where it is (zoo.exe guestSurroundingsTick
// 0x43abac, every cEnvironmentEffectCheck ticks): its block's esthetic for
// its kind at cObjectEstheticThreshold or more, cHappyEstheticChange and
// "This area is quite lovely." (10103); its guests x 10 / 25 (0x43b7e3) at
// cCrowdedViewingThreshold or more, cCrowdedViewingChange and "This zoo is
// awfully crowded." (10104); its stink at cStinkThreshold or more, once
// until it's out of it (+0x336), cSickChange and "This place really smells
// bad!" (10241)
void Guests::surroundings(Guest &g) {
  const Type &t = this->typeList[g.type];
  const Block *b = blockAt(g.x, g.y);
  float esthetic = 0, stink = 0;
  int crowd = 0;
  if (b) {
    auto it = b->esthetic.find(t.nameId);
    esthetic = it == b->esthetic.end() ? 0.0f : it->second;
    crowd = b->guests * 10 / 25;
    stink = b->stink;
  }
  ZT_STEP("0x43abac", "Its surroundings: esthetic, crowd, stink", {"esthetic", esthetic},
          {"cObjectEstheticThreshold", t.estheticThreshold}, {"crowd", crowd},
          {"cCrowdedViewingThreshold", t.crowdedThreshold}, {"stink", stink}, {"cStinkThreshold", t.stinkThreshold});
  if (static_cast<float>(t.estheticThreshold) <= esthetic) {
    g.dHappy += static_cast<float>(t.happyEsthetic);
    think(g, 10103);
  }
  if (crowd >= t.crowdedThreshold) {
    g.dHappy += static_cast<float>(t.crowdedChange);
    think(g, 10104);
  }
  if (static_cast<float>(t.stinkThreshold) <= stink) {
    if (!g.smelled) {
      g.smelled = true;
      g.dHappy += static_cast<float>(t.sickChange);
      think(g, 10241);
    }
  } else {
    g.smelled = false;
  }
}

void Guests::tick(Guest &g) {
  ZT_FN("", "A guest's status tick (once a second)");
  // What the last tick's checks found
  float wasHappy = g.happiness, wasHunger = g.hunger, wasThirst = g.thirst, wasBathroom = g.bathroom,
        wasTired = g.tired;
  g.happiness = std::clamp(g.happiness + g.dHappy, -100.0f, 100.0f);
  g.hunger = std::clamp(g.hunger + g.dHunger, 0.0f, 100.0f);
  g.thirst = std::clamp(g.thirst + g.dThirst, 0.0f, 100.0f);
  g.bathroom = std::clamp(g.bathroom + g.dBathroom, 0.0f, 100.0f);
  g.tired = std::clamp(g.tired + g.dTired, 0.0f, 100.0f);
  ZT_STEP("", "Takes what the last checks found", {"happiness", wasHappy, g.happiness}, {"hunger", wasHunger, g.hunger},
          {"thirst", wasThirst, g.thirst}, {"bathroom", wasBathroom, g.bathroom}, {"tired", wasTired, g.tired});
  g.dHappy = g.dHunger = g.dThirst = g.dBathroom = g.dTired = 0;
  const Type &type = this->typeList[g.type];
  // Calming down (zoo.exe needsTick 0x43ab00 at +0x314, every
  // cSawAnimalReset ticks): afraid no more once no animal is loose (out on
  // the zoo's ground) or, in an exhibit, no predator is near
  if (--g.sawResetCheck < 0) {
    g.sawResetCheck = type.sawReset - 1;
    if (g.sawEscape) {
      const bool inExhibit = this->fences->exhibitAt(static_cast<int>(std::floor(g.x)), static_cast<int>(std::floor(g.y))) >= 0;
      if (inExhibit ? findPredator(g) < 0 : this->animals->escapedCount() == 0)
        g.sawEscape = false;
    }
  }
  // A predator near (zoo.exe 0x43abf9, every cChaseCheck; not while
  // already fleeing - task 0x75 - or in the water, 0x7f): none, no longer
  // "saw a predator" (+0x24c); one, afraid (+0x33a), no longer leaving
  // (+0x339 cleared), cEscapedAnimalChange, and "RUN, %s has escaped!" out
  // on the zoo's ground or "I need to get out of this exhibit!" in an
  // exhibit; then away from it (chooseTask 0x50d079: task 0x75 with
  // cNonPathCostEmergency, the predator kept at +0x22c)
  if (--g.chaseCheck < 0) {
    g.chaseCheck = type.chaseCheck - 1;
    if (g.state != State::Fleeing && g.state != State::Caught && g.state != State::Water) {
      const Animals::Member *near = this->animals->member(findPredator(g));
      if (!near) {
        g.sawPredator = false;
      } else {
        const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
        const bool inExhibit = this->fences->exhibitAt(gx, gy) >= 0;
        ZT_STEP("0x43abf9", "A predator near: afraid, no longer leaving, runs (cEscapedAnimalChange)",
                {"animal", near->name}, {"was leaving", g.leaving ? "yes" : "no"}, {"in an exhibit", inExhibit ? "yes" : "no"});
        g.dHappy += static_cast<float>(type.escapedChange);
        g.sawEscape = true;
        g.sawPredator = true;
        g.leaving = false;
        g.predator = near->id;
        g.sawResetCheck = type.sawReset - 1;
        if (inExhibit)
          think(g, kOutOfExhibit);
        else
          think(g, kRun, near->name);
        // Off its tour, out of its errand
        g.guide = -1;
        g.tourHeard = false;
        g.building = -1;
        g.need.clear();
        g.target = -1;
        g.gawkObject = -1;
        g.state = State::Fleeing;
        // ours: where to - away from it, over the paths or open ground
        float ax = g.x - near->x, ay = g.y - near->y, ad = std::max(0.01f, std::hypot(ax, ay));
        bool ran = false;
        for (int r = 10; r >= 3 && !ran; r -= 2)
          for (int tries = 0; tries < 6 && !ran; tries++) {
            int x = gx + static_cast<int>(std::lround(ax / ad * r)) + std::uniform_int_distribution<int>(-2, 2)(this->rng);
            int y = gy + static_cast<int>(std::lround(ay / ad * r)) + std::uniform_int_distribution<int>(-2, 2)(this->rng);
            ran = fleeGround(x, y) && walkTo(g, x, y, type.runSpeed, true);
          }
        std::string scream = type.kind == Kind::Man ? "man_scream" : type.kind == Kind::Woman ? "woman_scream"
                             : type.kind == Kind::Boy ? "boy_scream" : "girl_scream";
        if (this->sounds && this->soundFile.count(scream))
          Sound::get().play(this->soundFile[scream], this->soundAtten[scream] + 800);
        if (!ran)
          play(g, "jump_terror", 2.0f);
      }
    }
  }
  // Unhappy: may leave (zoo.exe guestDecideLeave 0x43ac4f, every
  // cLeaveZooCheck - or at once when just done with something, +0x33e): at
  // -40..-20 cLeaveChanceLow, -80..-41 cLeaveChanceMed, -100..-81
  // cLeaveChanceHigh in 100
  if (g.recheck || --g.leaveCheck < 0) {
    g.leaveCheck = type.leaveCheck - 1;
    if (!g.leaving) {
      int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
      int h = static_cast<int>(g.happiness);
      if (h <= -20 && h >= -40 && roll < type.leaveLow) {
        think(g, kLeaveLow);
        g.leaving = true;
      } else if (h < -40 && h >= -80 && roll < type.leaveMed) {
        think(g, kLeaveMed);
        g.leaving = true;
      } else if (h < -80 && roll < type.leaveHigh) {
        think(g, kLeaveHigh);
        g.leaving = true;
      }
      ZT_STEP("0x43ac4f", "Unhappy enough to leave? (cLeaveChanceLow/Med/High)", {"happiness", h},
              {"roll", roll}, {"leaves", g.leaving ? "yes" : "no"});
    }
  }
  // Needs (zoo.exe guestRestroomTick 0x43acd4, guestHungerTick 0x43ad57,
  // guestThirstTick 0x43adda, guestTiredTick 0x43ae5d, in that order): every
  // c<Need>Check ticks, unless it's already off for it, c<Need>Increment
  // more, and at its limit (100, cMaxEnergy) c<Angry>Change; then - at that
  // check, or at once when just done with something (+0x33e, the countdown
  // held) - at c<Need>Threshold or more, "I'm hungry." and off to the
  // nearest that has room (anywhere: 0xb504; a seat within cSearchRadius);
  // none, "I can't find anything to eat." or, all full, "%s is too crowded."
  // only
  auto need = [&](int &check, int period, float &delta, float value, int increment, int threshold, int limit,
                  int angry, int wantThought, int noneThought, const char *what) {
    if (!g.recheck) {
      if (--check >= 0)
        return;
      check = period - 1;
      if (g.need != what)
        delta += static_cast<float>(increment);
      if (value >= limit)
        g.dHappy += static_cast<float>(angry);
    }
    ZT_STEP("", "A need check", {"need", what}, {"value", value}, {"just done with something", g.recheck ? "yes" : "no"},
            {"increment", !g.recheck && g.need != what ? increment : 0},
            {"at its limit: happiness", !g.recheck && value >= limit ? angry : 0}, {"threshold", threshold});
    if (value >= threshold && g.building < 0 && !g.leaving && g.state != State::Fleeing && !g.puking &&
        g.state != State::Water) {
      think(g, wantThought);
      const bool seat = std::string(what) == "energy";
      if (!seekBuilding(g, what, seat ? this->searchRadius : 1000)) {
        if (!this->seekFull)
          think(g, noneThought);
        // (no seat: guestFindSeat 0x5adfac sets +0x33e - the checks after
        // this one look at once)
        if (seat)
          g.recheck = true;
      }
    }
  };
  need(g.bathroomCheck, type.bathroomCheck, g.dBathroom, g.bathroom, type.bathroomIncrement, type.bathroomThreshold,
       100, type.angryBathroom, 10084, kNoRestroom, "bathroom");
  need(g.hungerCheck, type.hungerCheck, g.dHunger, g.hunger, type.hungerIncrement, type.hungerThreshold, 100,
       type.angryFood, 10079, kHungryNoFood, "food");
  need(g.thirstCheck, type.thirstCheck, g.dThirst, g.thirst, type.thirstIncrement, type.thirstThreshold, 100,
       type.angryThirst, 10082, kThirstyNoDrink, "drink");
  need(g.energyCheck, type.energyCheck, g.dTired, g.tired, type.energyIncrement, type.energyThreshold, type.maxEnergy,
       type.angryEnergy, kTired, 10093, "energy");
  // The trash check (zoo.exe 0x43aedc), every cTrashCheck (30) while it isn't
  // off to a building: cAngryTrashChange (-5) unless it's on its way to a
  // trash can (+0x24e, its goal, isn't "trash") - every guest, holding
  // litter or not: unlike its needs, no measure first. Seen live (Play
  // original): every guest loses 5 each ~2.5 days, oldest lowest. Just done
  // with something (+0x33e): no countdown and no loss, only the litter
  if (g.building < 0 && (g.recheck || --g.trashCheck < 0)) {
    if (!g.recheck) {
      g.trashCheck = type.trashCheck - 1;
      ZT_STEP("0x43aedc", "Not off to a trash can: unhappier (cAngryTrashChange)", {"dHappy", g.dHappy, g.dHappy + type.angryTrash});
      g.dHappy += static_cast<float>(type.angryTrash);
    }
    // Rubbish in hand: a bin nearby, else dropped as litter (not on the
    // water: guestDropLitter 0x52da40; ours: kept till back on land)
    if (g.trash && g.state != State::Water) {
      think(g, 10237);
      if (g.building < 0 && !seekBuilding(g, "trash", this->searchRadius)) {
        if (this->items)
          this->items->add(ZooItems::Kind::Litter, -1, g.x, g.y);
        g.trash = false;
      }
    }
  }
  // A souvenir (zoo.exe guestSouvenirTick 0x43af5b, every cBuySouvenirCheck
  // or at once when just done with something): its happiness (-100..100)
  // 71-100, cBuySouvenirChanceHigh in 100; 31-70, cBuySouvenirChanceMed;
  // then off to a gift shop. Wanting one is anew each time: +0x33c cleared,
  // then set when the roll says so
  if (g.recheck || --g.souvenirCheck < 0) {
    if (!g.recheck)
      g.souvenirCheck = type.souvenirCheck - 1;
    const int h = static_cast<int>(g.happiness);
    const int chance = h >= 71 && h <= 100 ? type.souvenirHigh : h >= 31 && h <= 70 ? type.souvenirMed : 0;
    if (!g.souvenir && g.building < 0 && !g.leaving && !g.puking && g.state != State::Water) {
      g.wantsSouvenir = false;
      const int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
      ZT_STEP("0x43af5b", "Wants a souvenir? (by its happiness)", {"happiness", h}, {"chance", chance}, {"roll", roll});
      if (roll < chance) {
        g.wantsSouvenir = true;
        think(g, 10090);
        if (!seekBuilding(g, "gift", 1000) && !this->seekFull)
          think(g, 10091);
      }
    }
  }
  // (zoo.exe needsTick 0x43ab00: +0x33e cleared before the viewing check)
  g.recheck = false;
  if (--g.viewCheck < 0) {
    g.viewCheck = type.viewingCheck - 1;
    viewing(g);
  }
  // Its surroundings (zoo.exe 0x43abac, every cEnvironmentEffectCheck)
  if (--g.environmentCheck < 0) {
    g.environmentCheck = type.environmentCheck - 1;
    surroundings(g);
  }
  // In the water and not on a ride (zoo.exe chooseTask 0x43a6e4 at
  // 0x59cbb8: in a tank - "Hey! I don't want to be in the water!" and
  // Task7f, out; ours: a lake tile alike - a zoo loaded with a guest out on
  // the water, the ground made water under it)
  if (g.state != State::Water && !g.hidden && g.showTank < 0 && g.x > -999 && this->map) {
    const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
    const MapTile *t = this->map->getTile(gx, gy);
    if (t && (t->terrainType == 9 || t->terrainType == 10)) {
      think(g, 10568);
      outOfWater(g);
    }
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
  // The blocks counted again every 7.5 s (zoo.exe timer7500 0x43525b: 7500 ms
  // of game time)
  this->blockMs += seconds * 1000.0f;
  if (this->blocks.empty() || this->blockMs >= 7500.0f) {
    this->blockMs = 0;
    refreshBlocks();
  }
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
    ZT_WHO("guest", g.id);
    g.animTime += seconds;
    g.tickMs += seconds * 1000.0f;
    while (g.tickMs >= kTickMs) {
      g.tickMs -= kTickMs;
      tick(g);
    }
    if (g.showTank >= 0)
      continue; // Show routes, seating and exit behavior retain the guest.
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
        if (g.state == State::Water) {
          // (its leg done: the next frame says what next - waterNext)
          g.playFor = 0;
        } else if (g.state == State::Leaving) {
          g.x = -1000; // out of the gate
          // Happy as it goes (over cMembershipJoinHappiness): may join the
          // zoo (cMembershipJoinChance percent): zoo.exe 0x4fa85d, at the
          // gate (+0x2a8 above the cfg's, rand % 100 below), a member booked
          if (g.happiness > Original::economy().membershipJoinHappiness &&
              std::uniform_int_distribution<int>(0, 99)(this->rng) < Original::economy().membershipJoinChance)
            this->members++;
        } else if (g.building >= 0 && g.state == State::Walking) {
          useBuilding(g);
        } else if (g.state == State::Walking && g.gawkObject >= 0) {
          // (there: Task81_GawkObject's atTarget 0x441290 stops it and sets
          // +0x33e - its needs looked at on the next tick)
          g.recheck = true;
          gawkScenery(g);
        } else if (g.state == State::Walking && g.target >= 0 && this->fences->exhibit(g.target)) {
          // (at the exhibit: Task70_ViewAnimals::atTarget 0x42e9f9 sets +0x33e)
          g.recheck = true;
          if (!testFence(g))
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
    // On the water: its ride's next leg, back, out or under (Task82, Task83,
    // Task84, Task7f)
    if (g.state == State::Water) {
      waterNext(g);
      continue;
    }
    // Done watching an exhibit (zoo.exe 0x42ea14): the time goes on its
    // viewing time (its popularity), then a donation: its animals healthy,
    // not crowded: cAdmissionMultiple x the time (game time, 100 ns units)
    // x its suitability x 0.01, $10 at most (in practice $10 a view of a
    // suitable exhibit: the code multiplies by cAdmissionMultiple, not
    // cDonationFactor)
    if (g.state == State::Gawking && g.target >= 0) {
      this->exhibitBooks[g.target].viewed += g.playFor;
      // (what its viewing area sees: an animal, none of them sick -
      // 0x42330b, 0x425f2e; ours, given no areas: the exhibit's animals)
      int n = 0;
      bool sick = false;
      Sight sight;
      if (this->viewAreas) {
        if (areaSight(g, sight)) {
          n = sight.animals;
          sick = sight.sick > 0;
        }
      } else {
        for (const Animals::Member &m : this->animals->members())
          if (m.exhibit == g.target) {
            sick = sick || m.sick;
            n++;
          }
      }
      // (not crowded: its viewing area's crowd (0x425f6a) under 50 -
      // zoo.exe 0x42ead9)
      const int crowd = areaCrowd(g);
      if (n > 0 && !sick && crowd < Original::donationCrowdLimit) {
        // (game time in 100 ns units: a 12 s game day is 864e9)
        double ticks = g.playFor * (864e9 / 12.0);
        int guide = this->tourBonusAt ? this->tourBonusAt(g.target) : 0;
        double d = Original::economy().admissionMultiple * ticks * (this->animals->exhibitSuitability(g.target) + guide) * 0.01;
        if (d > 0) {
          d = std::min(d, 10.0);
          this->donations += d;
          this->exhibitBooks[g.target].donated += d;
        }
        ZT_STEP("0x42ea14", "Done watching: a donation (healthy, not crowded)", {"seconds watched", g.playFor},
                {"suitability", this->animals->exhibitSuitability(g.target)}, {"tour bonus", guide},
                {"donation ($10 at most)", d > 0 ? d : 0.0});
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
        // A water ride (cReturnBuilding): through it onto the water first, what
        // it does for them only once back in (zoo.exe guestBuy 0x42d858: the
        // price both times; 0x42dd23, its effects, only for a guest on its
        // ride - +0x284 set, 0x43a837)
        const bool goingOut = b.returnBuilding && g.ride == 0;
        if (!goingOut) {
          g.dHappy += child ? childChange : adultChange;
          g.dHunger += b.hunger;
          g.dThirst += b.thirst;
          g.dBathroom += b.bathroom;
          g.dTired += b.energy;
        }
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
        ZT_STEP("0x42d1e2", "The price: too dear (2x) or a bargain (half)?", {"price", price}, {"default", b.price},
                {"verdict", tooDear ? "too dear" : cheap ? "a bargain" : "fair"});
        if (tooDear) {
          think(g, 10073);
          g.dHappy -= 5;
        } else if (cheap) {
          think(g, 10072);
          g.dHappy += 5;
        }
        ZT_STEP("0x42d858", "Out of the building: what it did to them", {"building", bo.typeName}, {"need", g.need},
                {"dHappy", g.dHappy}, {"dHunger", g.dHunger}, {"dThirst", g.dThirst}, {"dBathroom", g.dBathroom},
                {"dTired", g.dTired}, {"bought", !b.item.empty() && !tooDear ? b.item : std::string("nothing")});
        if (goingOut) {
          rideOut(g, bo, b);
          continue;
        }
        if (!b.item.empty() && !tooDear) {
          bo.sold[b.item]++;
          if (g.need == "gift") {
            g.souvenir = true;
            g.wantsSouvenir = false; // (holdItem 0x42d54e clears +0x33c)
          }
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
        // Back from the ride: off the water at the return slot's exitpos, on
        // land (guestBuy 0x596f10: +0x284 and its behaviour cleared, out of
        // the water - 0x274(0))
        if (b.returnBuilding && g.ride) {
          for (const BuildingInfo::Slot &s : b.slots)
            if (s.isReturn)
              slotPoint(bo, s.xx, s.xy, g.x, g.y);
          endRide(g);
        }
      }
      g.building = -1;
      g.need.clear();
      g.hidden = false;
      g.state = State::Walking;
      // (out again: guestBuy 0x42d858 and Task69_ExitBuilding set +0x33e)
      g.recheck = true;
    }
    decide(g);
  }
  // Gone out of the gate
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [](const Guest &g) { return g.x < -100; }),
                   this->list.end());
  // Art in colours no guest wears now: freed. Each guest's art is all its
  // frames in its own colours; kept for good, a zoo left running filled
  // memory until the Lab's 32-bit engine couldn't make a picture (its view
  // froze). The list icons stay (a panel may be showing one)
  this->artSweepIn -= seconds;
  if (this->artSweepIn <= 0.0f) {
    this->artSweepIn = 15.0f;
    std::set<std::string> worn;
    for (const Guest &g : this->list) {
      int c[4];
      wornColours(g, c);
      std::string k;
      for (int i = 0; i < 4; i++)
        k += "#" + std::to_string(c[i]);
      worn.insert(k);
    }
    for (auto it = this->artCache.begin(); it != this->artCache.end();) {
      size_t at = it->first.find('#');
      if (at != std::string::npos && it->first.rfind("guests/ls", 0) != 0 && !worn.count(it->first.substr(at))) {
        delete it->second;
        it = this->artCache.erase(it);
        ++this->artGen;
      } else {
        ++it;
      }
    }
  }
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
    view.worldToScreenF(g.x, g.y, groundAt(g.x, g.y) + g.showZ, sx, sy, depth);
    if(g.showSeat>=0 && objects)if(const auto *stand=objects->byId(g.showStand)) {
      const auto &info=infoOf(PlacedObjects::fileOf(*stand));
      std::pair<int,int> fp{info.fx,info.fy};
      if((stand->facing&6)==2 || (stand->facing&6)==6)std::swap(fp.first,fp.second);
      const float hx=fp.first/4.0f,hy=fp.second/4.0f;
      float front=depth;
      for(float x:{stand->x-hx,stand->x+hx})for(float y:{stand->y-hy,stand->y+hy}) {
        float px,py,d;view.worldToScreenF(x,y,0,px,py,d);front=std::max(front,d);
      }
      // The stand is sorted in strips at its footprint's front edge. Its
      // seated occupants belong above those strips, not behind the steps.
      depth=front+0.02f+depth*0.00001f;
    }
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
    view.worldToScreenF(g.x, g.y, groundAt(g.x, g.y) + g.showZ, sx, sy, depth);
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

Guests::Census Guests::census() const {
  Census c;
  long happy = 0;
  for (const Guest &g : this->list) {
    if (g.x <= -999)
      continue; // (not in the zoo yet)
    c.guests++;
    happy += static_cast<long>(g.happiness);
    c.hungry += g.hunger > this->uiThreshold ? 1 : 0;
    c.thirsty += g.thirst > this->uiThreshold ? 1 : 0;
    c.bathroom += g.bathroom > this->uiThreshold ? 1 : 0;
    c.tired += g.tired > this->uiThreshold ? 1 : 0;
    c.gift += g.wantsSouvenir ? 1 : 0;
    c.trash += g.trash ? 1 : 0;
  }
  c.happiness = c.guests ? static_cast<int>(happy / c.guests) : 0;
  return c;
}
