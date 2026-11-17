#include "Guests.hpp"
#include "ZooSim.hpp"
#include "LabTrace.hpp"
#include "OriginalConstants.hpp"
#include "Research.hpp"
#include "Settings.hpp"

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
  // (zoo.exe 0x4bc8f8: zoo.ini [AI] useAttractionAttractiveness, default 0)
  this->useAttractionAttractiveness = Settings::getInt("AI", "useAttractionAttractiveness", 0) != 0;
  // (zoo.exe ZTGuestType::loadClassConfig 0x4c57d4, read once - its body
  // 0x52196e: these into globals)
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
    // Walking at its cSlowRate (man 33, woman and boy 26, girl 22), by the
    // units' movement rule (zoo.exe 0x435c42, Original::unitTilesPerSecond:
    // whole 64ths of a tile an update), so a man takes 2 steps an update and
    // the others 1 - half his speed. Running from a predator, its
    // bRunFromPredator's fMove(0,0,run_terror,<rate>) (56 adults, 44
    // children: fMove 0x425533 hands the rate to the move, 0x43646e) by the
    // same rule
    t.slowRate = ai->getInt(ints, "cslowrate", ai->getInt("man/characteristics/integers", "cslowrate", 33));
    // (the rested walking speed; walkSpeed gives it by tiredness)
    t.speed = Original::unitTilesPerSecond(Original::guestWalkRate(0));
    t.runSpeed = Original::unitTilesPerSecond(t.child ? 44 : 56);
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
  t.tapWallChance = num("ctapwallchance");
  t.priceHappy = num("cpricehappy1change");
  t.priceAngry = num("cpriceangry1change");
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
    read("cstandandeatchange", t.standAndEat);
    read("cmimicchance", t.mimicChance);
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
    // Tapping a tank's glass: its bTapTank's fPlays, in order
    for (const std::string &f : ai->getList(key + "\\behaviorset\\btaptank", "f")) {
      const size_t open = f.find('('), close = f.find(')');
      if (lower(f.substr(0, open == std::string::npos ? 0 : open)) == "fplay" && close != std::string::npos && close > open)
        t.tapSet.push_back(lower(f.substr(open + 1, close - open - 1)));
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
  // [BehaviorSet\bInWater], bSuitIdle, and a man's bSuitDrown, bSurfDrown;
  // and the buildings' sets, those with an fEnterBuilding (the benches' and
  // picnic tables' [BehaviorSet\bbencheat0] ..., the playground's and games'
  // per kind: [boy\BehaviorSet\bswingfun0], [man\BehaviorSet\bdinoputtfun0])
  for (const std::string &sec : ai->getSections()) {
    const std::string low = lower(sec);
    const size_t at = low.find("behaviorset\\");
    if (at == std::string::npos)
      continue;
    const std::string name = low.substr(at + 12);
    const std::vector<std::string> lines = ai->getList(sec, "f");
    bool enters = false;
    for (const std::string &f : lines)
      enters = enters || lower(f).find("fenterbuilding") != std::string::npos;
    if (name.rfind("breturn", 0) == 0 || name == "binwater" || name == "bsuitidle" || name == "bsuitdrown" ||
        name == "bsurfdrown" || enters)
      this->behaviourSets[low] = lines;
  }
  delete ai;
  delete cfg;
  if (rm->hasResource("ui/select/selsmall/selsmall.ani"))
    this->selectArrow = rm->getAnimation("ui/select/selsmall/selsmall");
  SDL_Log("[Guests] %zu guest types", this->typeList.size());
}

void Guests::clear() {
  this->list.clear();
  this->carried = -1;
  this->thoughtLog.clear(); // (a zoo's own ZTThoughtMgr)
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
// (its colours; smell-sick, +0x336, its skin - the palette's 4th, +0xc - is
// its type's cSickPalName, +0x2cc: ZTGuest::guestDrawPalette 0x43408c)
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

// A guest's walking speed by its tiredness (zoo.exe 0x42250a, its class slot
// 0x1c0: Original::guestWalkRate), by the units' movement rule (0x435c42)
float Guests::walkSpeed(const Guest &g) const {
  return Original::unitTilesPerSecond(Original::guestWalkRate(static_cast<int>(g.tired)));
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
  // (walking: its rate set by its tiredness as the move starts - 0x42250a)
  const bool walking = speed == this->typeList[g.type].speed;
  g.speed = walking ? walkSpeed(g) : speed;
  if (g.path.empty())
    return false;
  g.anim = !walking && speed > this->typeList[g.type].speed * 1.2f ? "run_terror" : "walk";
  g.animTime = 0;
  g.reverse = false;
  return true;
}

void Guests::play(Guest &g, const std::string &anim, float seconds) {
  g.tapStep = -1; // (another set replaces its bTapTank: tapPlay sets it again)
  g.reverse = false;
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

// zoo.exe thoughtAdd 0x4231c1 -> thoughtPush 0x42311a: a thought about an
// animal that's still an egg (+0x395) is "Look at that %s egg! How cool is
// that?" (10250) instead; always onto the front of the zoo's list (the
// ZTThoughtMgr, [0x639090]), repeats kept, the oldest dropped past its cap
// (+0xc: Original::thoughtListCap). Guest Information shows the guest's
// newest 5 of that list (0x46a7c8): ours keeps them on the guest, one
// dropped with the zoo's oldest when it was among them
void Guests::think(Guest &g, int stringId, const std::string &arg, int animal, int exhibit) {
  if (animal >= 0 && this->animals)
    if (const Animals::Member *m = this->animals->member(animal); m && m->egg)
      stringId = 10250;
  g.lastThought = stringId;
  g.thoughtArg = arg;
  if (!this->rm)
    return;
  std::string s = this->rm->getString(stringId);
  size_t at = s.find("%s");
  if (at != std::string::npos)
    s.replace(at, 2, arg);
  this->thoughtLog.insert(this->thoughtLog.begin(), {s, g.id, animal, exhibit});
  ZT_STEP("0x42311a", "Thinks", {"thought", s}, {"string id", stringId});
  g.thoughts.insert(g.thoughts.begin(), s);
  if (g.thoughts.size() > static_cast<size_t>(Original::guestInfoThoughts))
    g.thoughts.resize(Original::guestInfoThoughts);
  if (this->thoughtLog.size() > static_cast<size_t>(Original::thoughtListCap)) {
    const Thought old = this->thoughtLog.back();
    this->thoughtLog.pop_back();
    // (its guest's own list: the dropped one was among its newest 5 if it
    // has 5 or fewer left in the zoo's list with it)
    int left = 0;
    for (const Thought &t : this->thoughtLog)
      if (t.guest == old.guest && ++left >= Original::guestInfoThoughts)
        break;
    if (left < Original::guestInfoThoughts)
      for (Guest &o : this->list)
        if (o.id == old.guest && static_cast<int>(o.thoughts.size()) > left)
          o.thoughts.resize(static_cast<size_t>(left));
  }
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
      b.hideBuilding = ai->getInt(ints, "chidebuilding", 0) != 0;
      b.staysOutside = ai->getInt(ints, "cuserstaysoutside", 0) == 1;
      {
        const size_t slash = file.find_last_of('/'), dot = file.find_last_of('.');
        const size_t from = slash == std::string::npos ? 0 : slash + 1;
        b.typeName = lower(file.substr(from, dot == std::string::npos || dot < from ? std::string::npos : dot - from));
      }
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
      // (no cDefaultCost: economy.cfg's cBuildingUseCostDefault - the type's
      // default before its .ai is read, ZTBuildingType::setDefaults 0x4c042c)
      b.price = price.empty() ? Original::economy().buildingUseCostDefault : static_cast<float>(std::atof(price.c_str()));
      b.commerce = ai->getInt(ints, "ccommerce", 0) == 1;
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
        for (size_t k = 0; k + 1 < e.size(); k += 2)
          s.entrances.push_back({e[k], e[k + 1]});
        if (x.size() >= 2)
          s.xx = x[0], s.xy = x[1];
        s.exitOnWater = ai->getInt(slot, "exitonwater", 0) != 0;
        s.entranceOnWater = ai->getInt(slot, "entranceonwater", 0) != 0;
        s.isReturn = ai->getInt(slot, "isreturnslot", 0) != 0;
        s.ids = ai->getIntList(slot, "id");
        std::vector<int> sp = ai->getIntList(slot, "slotpos");
        if (sp.size() >= 2)
          s.sx = sp[0], s.sy = sp[1];
        s.facing = ai->getInt(slot, "facing", 0);
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
        else if (section == "sells") {
          if (b.item.empty())
            b.item = line;
          b.sells.push_back(line);
        }
      }
    }
  }
  if (!b.item.empty()) {
    const BuildingInfo &it = itemOf(b.item);
    b.itemAdult = it.itemAdult, b.itemChild = it.itemChild, b.itemHunger = it.itemHunger;
    b.itemThirst = it.itemThirst, b.itemBathroom = it.itemBathroom, b.itemEnergy = it.itemEnergy;
    b.itemTrash = it.itemTrash, b.consumedThought = it.consumedThought;
    b.itemId = it.itemId, b.itemNow = it.itemNow, b.itemRemove = it.itemRemove;
  }
  return this->buildingInfo[file] = b;
}

// An item's items/<name>.cfg (zoo.exe ZTShopItem's loader 0x464f00: nameID,
// the six deltas, consumenow, trashwhenconsumed, removewhenconsumed,
// consumedThoughtID) into the item* fields
const Guests::BuildingInfo &Guests::itemOf(const std::string &name) const {
  auto found = this->itemInfo.find(name);
  if (found != this->itemInfo.end())
    return found->second;
  BuildingInfo b;
  b.item = name;
  if (this->rm)
    if (IniReader *it2 = this->rm->getIniReader("items/" + name + ".cfg")) {
      const std::string c = "characteristics";
      b.itemAdult = static_cast<float>(it2->getInt(c, "adulthappydelta", 0));
      b.itemChild = static_cast<float>(it2->getInt(c, "childhappydelta", 0));
      b.itemHunger = static_cast<float>(it2->getInt(c, "hungerdelta", 0));
      b.itemThirst = static_cast<float>(it2->getInt(c, "thirstdelta", 0));
      b.itemBathroom = static_cast<float>(it2->getInt(c, "bathroomdelta", 0));
      b.itemEnergy = static_cast<float>(it2->getInt(c, "energydelta", 0));
      b.itemTrash = it2->getInt(c, "trashwhenconsumed", 0) != 0;
      b.consumedThought = it2->getInt(c, "consumedthoughtid", 0);
      b.itemId = static_cast<uint32_t>(it2->getInt(c, "nameid", 0));
      b.itemNow = it2->getInt(c, "consumenow", 1) != 0;
      b.itemRemove = it2->getInt(c, "removewhenconsumed", 1) != 0;
      delete it2;
    }
  if (b.itemId)
    this->itemNames.emplace(b.itemId, name);
  return this->itemInfo[name] = b;
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
// not full): off to its door (a path tile beside it). only: that building
// alone, its room not looked at (zoo.exe guestPickAttraction 0x4282e1 sends
// the guest to the one drawn, task 0x66; Task66's atTarget 0x4a830c looks
// for room at the door - ours, useBuilding's look at the entrance)
bool Guests::seekBuilding(Guest &g, const std::string &need, int radius, bool crowdedThought, int only) {
  this->seekFull = false;
  if (!this->objects)
    return false;
  const auto &objs = this->objects->objects();
  int best = -1, bx = -1, by = -1, fullAt = -1, bestSlot = -1;
  float bestD = 1e9f, fullD = 1e9f;
  bool full = false;
  for (size_t i = 0; i < objs.size(); i++) {
    const PlacedObjects::Object &o = objs[i];
    if (o.fence || (only >= 0 && static_cast<int>(i) != only))
      continue;
    const BuildingInfo &b = infoOf(PlacedObjects::fileOf(o));
    if (!b.satisfies.count(need))
      continue;
    float d = std::hypot(o.x - g.x, o.y - g.y);
    if (d > radius && only < 0)
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
    if (users >= b.capacity && only < 0) {
      full = true;
      if (d < fullD)
        fullD = d, fullAt = static_cast<int>(i);
      continue;
    }
    // A cUserTracker ride (the dolphin shack) counts its riders out on the
    // water against its capacity (zoo.exe 0x59fe9e: its users list, +0x158,
    // against +0x1c8 - 0x59398a: cStrictBuildingCapacity, else the lake's
    // size from its water door + 7 over 10, 10 at most)
    if (b.tracker && b.returnBuilding && only < 0) {
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
    // A building with a b<name><goal><slot> set (a bench, a picnic table,
    // the playground and games): a free slot - each holds one - the one
    // whose entrance is nearest the guest (zoo.exe 0x42c02b: the slots in
    // turn, each with room (0x42ac87), their entrances' squared distance
    // from it, the nearest it has a route to)
    if (!buildingSet(g, b, need, 0).empty()) {
      int slotAt = -1, sx = -1, sy = -1;
      float nearest = 1e30f;
      for (size_t k = 0; k < b.slots.size(); k++) {
        if (buildingSet(g, b, need, static_cast<int>(k)).empty())
          continue;
        bool taken = false;
        for (const Guest &o2 : this->list)
          taken = taken || (&o2 != &g && o2.building == static_cast<int>(i) && o2.slot == static_cast<int>(k));
        if (taken)
          continue;
        std::vector<std::pair<int, int>> entrances = b.slots[k].entrances;
        if (entrances.empty())
          entrances.push_back({b.slots[k].ex, b.slots[k].ey});
        for (const auto &[px, py] : entrances) {
          float ex, ey;
          buildingPoint(o, px, py, ex, ey);
          const int tx = static_cast<int>(std::floor(ex)), ty = static_cast<int>(std::floor(ey));
          const float d2 = (ex - g.x) * (ex - g.x) + (ey - g.y) * (ey - g.y);
          if (walkable(tx, ty) && d2 < nearest) {
            nearest = d2;
            slotAt = static_cast<int>(k);
            sx = tx;
            sy = ty;
          }
        }
      }
      if (slotAt >= 0 && d < bestD) {
        bestD = d;
        best = static_cast<int>(i);
        bx = sx;
        by = sy;
        bestSlot = slotAt;
      }
      // (the one a pick drew with every slot taken: walked to all the same,
      // its room looked at at the door - Task66's atTarget 0x4a830c)
      if (slotAt >= 0 || only < 0)
        continue;
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
        bestSlot = -1;
      }
  }
  ZT_STEP("", "Looks for a building for a need", {"need", need}, {"search radius", radius},
          {"found", best >= 0 ? "yes" : full ? "all full" : "none in reach"}, {"distance", best >= 0 ? bestD : 0.0f});
  if (best < 0) {
    // (all full: "%s is too crowded." alone, not also "none found" - zoo.exe
    // 0x4fba3a, 0x58c86a; Task66's atTarget 0x4a830c: the %s is the full
    // building - thoughtAdd(0x2803, building). ours: its own name, else its
    // kind's cNameID text)
    this->seekFull = full;
    if (full && crowdedThought)
      think(g, 10243, fullAt >= 0 ? buildingName(objs[static_cast<size_t>(fullAt)]) : std::string()); // "%s is too crowded."
    return false;
  }
  // (Task67_GoToBuilding::whileGoing 0x42a0ab: a route to the entrance
  // point, cNonPathCost its cost off the paths; none, the task over)
  if (!walkTo(g, bx, by, this->typeList[g.type].speed))
    return false;
  g.guide = -1; g.tourHeard = false;
  g.building = best;
  g.slot = bestSlot;
  g.sitStep = -1;
  g.takenIn = false;
  g.need = need;
  g.state = State::Walking;
  g.target = -1;
  return true;
}

// zoo.exe Task68_EnterBuilding::atTarget 0x42cd39: a building with neither
// cHideUser (type +0x194) nor cUserStaysOutside (+0x1a6) gets the guest's
// class slot 0x2b4 (0x5ad535): the behaviour set "b" + the type's name +
// its goal kind (+0x24e, 0x42aeaf) + its slot (+0x24d) started (0x43c6c5) -
// guests.ai's bbencheat0..1, bbenchenergy0..1, bpicniceat0..3 and the
// rest: fPlay(<it>), fEnterBuilding(), fPlayTime(<it>, 30), fPlay(idle),
// fExitBuilding(); the playground's and games', per kind (boy and girl:
// bswingfun0, bdslidefun0, bbouncefun0, baqbouncefun0, bringtossfun0; man
// and woman: bdinoputtfun0 - those slots' id lists take only those kinds).
// The set as the kind has it, its own section first (0x43c6c5 finds it by
// name in the kind's sets). ours: where it has such a set
std::string Guests::buildingSet(const Guest &g, const BuildingInfo &b, const std::string &goal, int slot) const {
  if (b.hideUser || b.staysOutside || b.typeName.empty() || goal.empty() || slot < 0 ||
      g.type < 0 || g.type >= static_cast<int>(this->typeList.size()))
    return std::string();
  const std::string set = "b" + b.typeName + goal + std::to_string(slot);
  if (this->behaviourSets.count(this->typeList[g.type].key + "\\behaviorset\\" + set) ||
      this->behaviourSets.count("behaviorset\\" + set))
    return set;
  return std::string();
}

// A building's set run from line g.sitStep (zoo.exe's behaviour runner,
// 0x436065 / 0x437ccd, over the set startSetNow 0x43c6c5 began):
// - fPlay(<a>): <a> played once (fPlay 0x440b92); fPlayReverse(<a>) the
//   same, last frame first (0x440c82: the swing's getting off);
//   fPlayWithSound(<a>, <sound>) with its [Sounds] sound (0x438399: the
//   dino putt's puttwin, puttlose); fPlayTime(<a>, <s>) whole plays to <s>
//   seconds or more (0x424e7f, playTimeWholeLoops 0x424ee7: a bench's 30)
// - fEnterBuilding(): taken in as the building's user (0x5ad42c -> slot
//   0x2a8 0x5ad3a6 -> buildingAddUser 0x42ce0a: counted, cUsedThought, the
//   held food eaten, the ring toss's prize), at once on to the next line
// - fExitBuilding(): the set over (0x4a777e -> slot 0x2ac 0x4a7705: out,
//   guestBuy 0x42d858) - at the slot's exitpos. Task6a_InsideBuilding
//   (checkArrived 0x42a1db) never ends a visit to such a building itself:
//   cTimeInside counts only where the building hides its user or keeps it
//   outside - the set's own plays are the visit's length
// An animation the kind hasn't fails the set there (0x440b92, 0x440c82,
// 0x4383bb, 0x424e7f). ours: then out as at fExitBuilding (the original's
// guest would stay in Task6a)
bool Guests::buildingSetLine(Guest &g) {
  const auto &objs = this->objects->objects();
  if (g.building < 0 || g.building >= static_cast<int>(objs.size())) {
    g.sitStep = -1;
    return false;
  }
  PlacedObjects::Object &bo = this->objects->all()[g.building];
  const BuildingInfo &b = infoOf(PlacedObjects::fileOf(bo));
  const std::string name = buildingSet(g, b, g.need, g.slot);
  const std::vector<Move> set = name.empty() ? std::vector<Move>() : behaviour(g, name);
  const Type &t = this->typeList[g.type];
  for (; g.sitStep >= 0 && g.sitStep < static_cast<int>(set.size()); g.sitStep++) {
    const Move &m = set[static_cast<size_t>(g.sitStep)];
    if (m.fn == "fenterbuilding") {
      if (!g.takenIn) {
        g.takenIn = true;
        ZT_STEP("0x5ad3a6", "fEnterBuilding: taken in as the building's user", {"set", name}, {"line", g.sitStep});
        addUser(g, bo, b);
        if (!b.useSound.empty() && this->sounds)
          Sound::get().play(b.useSound, b.useAtten + 600);
      }
      continue;
    }
    if (m.fn == "fexitbuilding")
      break;
    Animation *a = t.anims.count(m.anim) ? this->art(g, m.anim) : nullptr;
    if (!a) {
      ZT_STEP("0x440b92", "An animation the kind hasn't: the set fails", {"set", name}, {"anim", m.anim});
      break;
    }
    const float loop = a->frameTimeMs() > 0 && a->frameCount() > 0 ? a->frameCount() * a->frameTimeMs() / 1000.0f : 1.0f;
    float secs = loop;
    if (m.fn == "fplaytime")
      while (m.seconds - secs >= 0.001f)
        secs += loop;
    ZT_STEP("0x440b92", "The set's next play", {"set", name}, {"line", g.sitStep}, {"fn", m.fn}, {"anim", m.anim},
            {"seconds", secs});
    play(g, m.anim, secs);
    g.reverse = m.fn == "fplayreverse";
    if (!m.sound.empty() && this->sounds && this->soundFile.count(m.sound))
      Sound::get().play(this->soundFile[m.sound], this->soundAtten[m.sound] + 600);
    return true;
  }
  g.sitStep = -1;
  g.reverse = false;
  if (g.slot >= 0 && g.slot < static_cast<int>(b.slots.size()))
    buildingPoint(bo, b.slots[static_cast<size_t>(g.slot)].xx, b.slots[static_cast<size_t>(g.slot)].xy, g.x, g.y);
  return false;
}

void Guests::buildingPoint(const PlacedObjects::Object &o, int px, int py, float &x, float &y) {
  const float angle = o.facing * 3.14159265f / 4.0f;
  const float fwdX = std::sin(angle), fwdY = -std::cos(angle);
  const float acrossX = -std::cos(angle), acrossY = -std::sin(angle);
  x = o.x + (px * acrossX + py * fwdX) / 64.0f;
  y = o.y + (px * acrossY + py * fwdY) / 64.0f;
}

std::string Guests::buildingName(const PlacedObjects::Object &o) const {
  std::string name = o.name;
  const int id = infoOf(PlacedObjects::fileOf(o)).nameId;
  if (name.empty() && id > 0 && this->rm)
    name = this->rm->getString(id);
  return name;
}

// zoo.exe ZTBuilding::setup 0x450f17: a building's price (+0x1ac) starts as
// its type's (+0x178: cDefaultCost, else cBuildingUseCostDefault). ours: -1
// for "the type's"
float Guests::priceOf(const PlacedObjects::Object &o, const BuildingInfo &b) const {
  return o.price >= 0 ? o.price : b.price;
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
  // Task67_GoToBuilding::atTarget (zoo.exe 0x42c8af), at the entrance (not
  // back from a ride, Task66's 0x66): the building looked for again with
  // room (class slot 0x2e8) - none: "%s is too crowded." (10243) of it, and
  // the task over. A cCommerce building (type +0x23d) priced at or over
  // cItemExpensive x its default (economy.cfg; [0x638048]+0x118) when it
  // stands on its entrance (0x4373eb): "The prices at the %s are too high"
  // (10073) of it, cPriceAngry1Change (0x533480), and it doesn't go in or
  // pay - the task over (slot 0x178). Else in (task 0x68). (Before it, the
  // walk there: guestGoToBuilding 0x43b2f6 makes task 0x66 for a building
  // without cHoldsOntoUser (type +0x1a4: only the grandstands have it), and
  // Task66's atTarget 0x4a830c looks again for room - none: "%s is too
  // crowded." of it, facing it, the task over; room: task 0x67 to the
  // entrance. ours: one walk to the entrance and this one look - the same
  // look, the same thought)
  if (g.ride == 0) {
    int users = 0;
    for (const Guest &o2 : this->list)
      users += &o2 != &g && o2.building == g.building && o2.state == State::Using ? 1 : 0;
    const float price = priceOf(o, b);
    const bool full = users >= b.capacity;
    const bool tooDear = !full && b.commerce && b.price * Original::economy().itemExpensive <= price;
    ZT_STEP("0x42c8af", "At the entrance: room, and the price against cItemExpensive", {"building", o.typeName},
            {"users", users}, {"cCapacity", b.capacity}, {"price", price}, {"default", b.price},
            {"cItemExpensive", Original::economy().itemExpensive}, {"commerce", b.commerce ? 1 : 0},
            {"verdict", full ? "full" : tooDear ? "too dear: not in" : "in"});
    if (full || tooDear) {
      // (either way, on its door square (0x4373eb), the building onto its
      // visited list: 0x4a840e)
      recordVisit(g, o);
      if (full) {
        think(g, 10243, buildingName(o));
      } else {
        think(g, 10073, buildingName(o));
        g.dHappy += static_cast<float>(this->typeList[g.type].priceAngry);
      }
      g.building = -1;
      g.need.clear();
      play(g, "stand", 0.5f);
      return;
    }
  }
  g.state = State::Using;
  g.hidden = b.hideUser;
  // (Task68_EnterBuilding: begin 0x42cc04, whileGoing 0x42a7cf walks it in
  // from the door - with cUserStaysOutside it stays where it is -
  // checkArrived 0x42a789 one tick in 15 looks for the building again, gone:
  // the task over. ours: from the door straight to its spot)
  // A building with a b<name><goal><slot> set (a bench, a picnic table,
  // the swing, slide, bounce houses, ring toss, dino putt): the set run
  // from its first line (buildingSetLine) at its slot (0x42ce0a,
  // fEnterBuilding: placed at the slotpos, facing the building's turn plus
  // the slot's facing, 0x5ad288 - ours from the set's start: the art runs
  // on unbroken over fEnterBuilding, the dino putt's puttentr's last frame
  // where puttwin's first is); taken in as its user at the set's
  // fEnterBuilding. Any other building takes it in at the door
  // (Task68_EnterBuilding::atTarget 0x42cd39: cHideUser or
  // cUserStaysOutside -> 0x42ce0a there and then). ours: one with neither
  // flag and no set for the kind alike, at the door
  if (g.slot >= 0 && g.slot < static_cast<int>(b.slots.size()) && !buildingSet(g, b, g.need, g.slot).empty()) {
    const BuildingInfo::Slot &s = b.slots[static_cast<size_t>(g.slot)];
    buildingPoint(o, s.sx, s.sy, g.x, g.y);
    const float angle = ((o.facing + s.facing) & 7) * 3.14159265f / 4.0f;
    g.fx = std::sin(angle);
    g.fy = -std::cos(angle);
    g.sitStep = 0;
    g.takenIn = false;
    ZT_STEP("0x5ad535", "At its slot: the building's set begins (b<name><goal><slot>)",
            {"set", buildingSet(g, b, g.need, g.slot)}, {"slot", g.slot});
    if (buildingSetLine(g))
      return;
    if (g.takenIn) { // (its set over at once, after fEnterBuilding: out next)
      play(g, "stand", 0.0f);
      return;
    }
  }
  addUser(g, this->objects->all()[g.building], b);
  play(g, g.need == "energy" ? "stand" : "stand", std::max(0.5f, b.timeInside));
  if (!b.useSound.empty() && this->sounds)
    Sound::get().play(b.useSound, b.useAtten + 600);
}

// zoo.exe ZTBuilding::buildingAddUser 0x42ce0a, its guest's part: the
// building's visitors (+0x190 this month, +0x198 in all) counted for a guest
// not on its ride (class slot 0x264, 0x43a837: +0x284 set). That guest then
// gets the type's cUsedThought (type slot 0x54), and: at a building on the
// "fungift" list (0x41239d with the string 0x63b040 - 0x566f0a: the ring
// toss) one of its Sells at random into its hands, no price judged
// (0x59cd02); anywhere else guestJudgePrice 0x42d1e2 - Sells empty, the
// held item of its goal kind eaten if the building satisfies that kind
// (0x42d74e -> guestConsumeItem 0x42d380: a bench's food); else one of its
// Sells at random (itemTypeByName 0x4a84f3), the price against its default:
// at or over cItemExpensive times it 10073 and cPriceAngry1Change (0x533480),
// nothing bought; at or under cItemCheap times it 10072 and
// cPriceHappy1Change (0x619245); into its hands (holdItem 0x42d54e) and
// consumed there and then if consumenow (item +0x1c). What the building does
// for it (0x42dd23) and the payment come as it leaves (guestBuy 0x42d858).
// (Its litter, trashCanAddLitter 0x42d181: a bin's - ours: useBuilding's
// "trash" kind)
void Guests::addUser(Guest &g, PlacedObjects::Object &bo, const BuildingInfo &b) {
  if (g.ride != 0)
    return;
  bo.visitorsNow++;
  bo.visitorsTotal++;
  if (b.usedThought)
    think(g, b.usedThought);
  const bool funGift = b.satisfies.count("fungift") > 0;
  ZT_STEP("0x42ce0a", "Taken in as the building's user: cUsedThought, then what it buys or eats", {"building", bo.typeName},
          {"cUsedThought", b.usedThought}, {"fungift", funGift ? 1 : 0}, {"sells", static_cast<int>(b.sells.size())});
  if (funGift) {
    if (!b.sells.empty()) {
      const BuildingInfo &won = itemOf(b.sells[std::uniform_int_distribution<size_t>(0, b.sells.size() - 1)(this->rng)]);
      ZT_STEP("0x59cd02", "A fun gift: one of its Sells, no price judged", {"item", won.item});
      bo.sold[won.item]++;
      holdItem(g, won, "fungift"); // (0x59cd02: the kind 0x63b040, "fungift")
    }
    return;
  }
  if (b.sells.empty()) {
    if (b.satisfies.count(g.need) && handFor(g.need) >= 0 && g.inventory[handFor(g.need)])
      if (const BuildingInfo *held = heldItem(g.inventory[handFor(g.need)]))
        consumeItem(g, *held);
    return;
  }
  const BuildingInfo &sold = itemOf(b.sells[std::uniform_int_distribution<size_t>(0, b.sells.size() - 1)(this->rng)]);
  const float price = priceOf(bo, b);
  const Type &gt = this->typeList[g.type];
  const bool tooDear = b.price * Original::economy().itemExpensive <= price;
  const bool cheap = !tooDear && price <= Original::economy().itemCheap * b.price;
  ZT_STEP("0x42d1e2", "What it buys: one of the building's Sells at random, the price judged", {"item", sold.item},
          {"of", static_cast<int>(b.sells.size())}, {"price", price}, {"default", b.price},
          {"verdict", tooDear ? "too dear: nothing" : cheap ? "a bargain" : "fair"}, {"consumenow", sold.itemNow ? 1 : 0});
  if (tooDear) {
    think(g, 10073, buildingName(bo));
    g.dHappy += static_cast<float>(gt.priceAngry);
    return;
  }
  if (cheap) {
    think(g, 10072, buildingName(bo));
    g.dHappy += static_cast<float>(gt.priceHappy);
  }
  bo.sold[sold.item]++;
  if (g.need == "gift") {
    g.souvenir = true;
    g.wantsSouvenir = false; // (holdItem 0x42d54e clears +0x33c)
  }
  if (holdItem(g, sold) && sold.itemNow)
    consumeItem(g, sold);
}

// zoo.exe 0x42d6aa (and holdItem 0x42d54e, releaseItem 0x42d533): a kind's
// hand, ZTGuest +0x26c[]: "food" and "eat" 0, "drink" 1, "fungift" 2,
// "gift" 3, "trash" 4; -1 none
int Guests::handFor(const std::string &kind) {
  if (kind == "food" || kind == "eat")
    return 0;
  if (kind == "drink")
    return 1;
  if (kind == "fungift")
    return 2;
  if (kind == "gift")
    return 3;
  if (kind == "trash")
    return 4;
  return -1;
}

// zoo.exe ZTGuest::holdItem 0x42d54e (and holdItemRest 0x42d617, its other
// kinds; from 0x42d1e2, its kind empty: the
// guest's goal kind, +0x24e): the item's nameID into that kind's hand - a
// food, drink, fun gift or gift over what it held; trash only into an empty
// hand; true if it took it
bool Guests::holdItem(Guest &g, const BuildingInfo &b, const std::string &kind) {
  const int hand = handFor(kind.empty() ? g.need : kind);
  if (hand < 0 || (hand == 4 && g.inventory[4]))
    return false;
  g.inventory[hand] = b.itemId;
  return true;
}

// zoo.exe guestConsumeItem 0x42d380: its consumedThoughtID (item +0x20,
// thoughtAdd); the item's six deltas - its loader 0x464f00 - pending by
// addPendingHappiness 0x42a98f, slot 0x1fc: adult or child happiness,
// hunger, thirst, the restroom, and energydelta onto +0x2c4, the tiredness
// pending; removewhenconsumed (+0x1e): out of its hand (releaseItem
// 0x42d533), then trashwhenconsumed (+0x1d): the wrapper or cup in hand
// (holdTrash 0x59d2c9)
void Guests::consumeItem(Guest &g, const BuildingInfo &item) {
  if (item.consumedThought)
    think(g, item.consumedThought);
  g.dHappy += this->typeList[g.type].child ? item.itemChild : item.itemAdult;
  g.dHunger += item.itemHunger;
  g.dThirst += item.itemThirst;
  g.dBathroom += item.itemBathroom;
  g.dTired += item.itemEnergy;
  ZT_STEP("0x42d380", "Consuming Item: what it does to them", {"item", item.item}, {"dHappy", g.dHappy},
          {"dHunger", g.dHunger}, {"dThirst", g.dThirst});
  if (!item.itemRemove) {
    g.trash = g.trash || item.itemTrash;
    return;
  }
  for (uint32_t &held : g.inventory)
    if (item.itemId && held == item.itemId) {
      held = 0;
      g.trash = g.trash || item.itemTrash;
      break;
    }
}

// zoo.exe itemTypeById 0x449f50: a held item's nameID back to its item (the
// items/ files read first if not all known yet)
const Guests::BuildingInfo *Guests::heldItem(uint32_t id) const {
  if (!id)
    return nullptr;
  auto it = this->itemNames.find(id);
  if (it == this->itemNames.end() && this->rm) {
    for (const std::string &f : this->rm->listResources("items/", ".cfg")) {
      std::string n = f.substr(6, f.size() - 10);
      if (n.find('/') == std::string::npos)
        itemOf(n);
    }
    it = this->itemNames.find(id);
  }
  return it == this->itemNames.end() ? nullptr : &itemOf(it->second);
}

// Off to eat what it holds (zoo.exe Task6b_GoEat: begin 0x59d580,
// checkArrived 0x59d479, atTarget 0x52dbfe): the nearest 'eat' building
// within cSearchRadius (a bench, a picnic table - findNearestOfKindB
// 0x4290f3) with room (guestGoToBuilding 0x43b2f6) - there, task 0x66 to
// it, its goal "eat". None: the task over, its goal "eat" and +0x33f set,
// so chooseTask (0x43a6e4 at 0x52dc3d) makes task 0x6c (Task6c_Eat): a
// walk about first (whileGoing 0x52d955: with no target tile yet, slot
// 27, its route list +0x154 emptied and unitWalkAbout 0x43d948 - every
// tick until one is made; then 0x422127(0, 1)), arrived when it stands on
// the walk's tile (checkArrivedOnTile 0x412603), and then (atTarget
// 0x52d9b8) task 0x6d - eatStanding
void Guests::goEat(Guest &g) {
  const bool seat = seekBuilding(g, "eat", this->searchRadius, false);
  ZT_STEP("0x59d479", "Food in hand: a seat to eat it at (cSearchRadius)?", {"found", seat ? "yes" : "no"});
  if (seat)
    return;
  g.state = State::Walking;
  g.target = -1;
  g.gawkObject = -1;
  g.eatStep = kEatWalkAbout;
  const bool walk = walkAbout(g);
  ZT_STEP("0x52d955", "No seat: a walk about first (Task6c_Eat), then it eats standing",
          {"walk about", walk ? "yes" : "none yet"});
  // ours: none made - it stands an update (Original::updateMs) and tries
  // again (whileGoing tries every tick)
  if (!walk)
    play(g, "stand", static_cast<float>(Original::updateMs) / 1000.0f);
}

// zoo.exe 0x43d948 (unitWalkAbout), for a guest: a tile on a ring round the
// tile it stands on, half widths rand % mRandomXGuest + 1 + BaseX across
// and rand % mRandomYGuest + 1 + BaseY down (2..10, OriginalConstants), one
// passable for it picked at random and routed to; no route, its four
// neighbours from a random one on, not wrapping round (tileNeighbours4
// 0x4171a1), the first passable it can step to; none, false.
// Passable for a guest (ZTGuest's class slot 0x168, 0x428f76, called with
// 1): the tile's +0x83 & 0x40 (0x40fb8d(0)) and a quarter of it free or
// its object passable (slot 0xb4); the guest's +0x1bc 3, or it off a path
// (its tile's +0x83 & 8 clear), or the tile a path; its own habitat not a
// tank (habitatAt slot 0x248, the habitat's slot 0x20), and it in an
// exhibit (habitat +0x2c 0) or the tile on zoo ground (+0x2c set).
// ours: on a path, walkable() (a path out of the exhibits); off one,
// fleeGround() (zoo ground, not blocked, not water); +0x1bc not read
bool Guests::walkAbout(Guest &g) {
  if (!this->map || !this->fences)
    return false;
  const int W = this->map->getWidth(), H = this->map->getHeight();
  const int sx = static_cast<int>(std::floor(g.x)), sy = static_cast<int>(std::floor(g.y));
  const int here = this->fences->exhibitAt(sx, sy);
  if (const Fences::Exhibit *in = this->fences->exhibit(here); in && in->tank)
    return false;
  auto rnd = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(this->rng); };
  const int rx = rnd(Original::aiWalkRandomGuest) + 1 + Original::aiWalkBase;
  const int ry = rnd(Original::aiWalkRandomGuest) + 1 + Original::aiWalkBase;
  const bool onPath = walkable(sx, sy);
  auto passable = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= W || y >= H)
      return false;
    if (onPath)
      return walkable(x, y);
    return here >= 0 ? this->fences->exhibitAt(x, y) == here || fleeGround(x, y) : fleeGround(x, y);
  };
  std::vector<std::pair<int, int>> ring;
  for (int dx = -rx; dx <= rx; dx++)
    for (int dy = -ry; dy <= ry; dy++)
      if ((dx == rx || dx == -rx || dy == ry || dy == -ry) && passable(sx + dx, sy + dy))
        ring.push_back({sx + dx, sy + dy});
  ZT_STEP("0x43d948", "Walk about: a tile on a ring round it, at random", {"half width", rx},
          {"half height", ry}, {"tiles", static_cast<int>(ring.size())});
  const float speed = this->typeList[g.type].speed;
  if (!ring.empty()) {
    auto [gx, gy] = ring[static_cast<size_t>(rnd(static_cast<int>(ring.size())))];
    if (walkTo(g, gx, gy, speed))
      return true;
  }
  const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = rnd(4); i < 4; i++) {
    const int nx = sx + d[i][0], ny = sy + d[i][1];
    if (passable(nx, ny) && canStep(sx, sy, nx, ny) && walkTo(g, nx, ny, speed))
      return true;
  }
  return false;
}

// Eating standing up (Task6d_EatStanding::begin 0x52dd30): the held food
// consumed (0x42d6aa "food", 0x42d380), cStandAndEatChange (guest type
// +0x2ac) onto its pending happiness, and its bEatStanding set played -
// fPlay(eatstanding), fPlay(idle); done when the set is (checkArrived
// 0x52d9c4)
void Guests::eatStanding(Guest &g) {
  if (const BuildingInfo *held = heldItem(g.inventory[0]))
    consumeItem(g, *held);
  else
    g.inventory[0] = 0;
  g.dHappy += static_cast<float>(this->typeList[g.type].standAndEat);
  ZT_STEP("0x52dd30", "No seat: eats standing up (cStandAndEatChange, bEatStanding)",
          {"cStandAndEatChange", this->typeList[g.type].standAndEat});
  g.state = State::Walking;
  g.target = -1;
  g.gawkObject = -1;
  g.eatStep = 0;
  float secs = 1.0f;
  if (Animation *a = this->art(g, "eatstanding"))
    if (a->frameTimeMs() > 0 && a->frameCount() > 0)
      secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
  play(g, "eatstanding", secs);
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
  // shared one: [man\BehaviorSet\bReturnchroom0] over [BehaviorSet\...];
  // zoo.exe ZTGuestType slot 0x38, 0x4223d3: the kind's name +0x98, then
  // +0x10, then none - guests.ai has only the kinds' and the shared
  // sections; bWalk and bZooRoutine kept from the shared ones at load,
  // loadGuestSettings 0x521c12)
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
    } else if ((fn == "fplay" || fn == "fplayreverse") && !args.empty()) {
      m.anim = args[0];
    } else if (fn != "fenterbuilding" && fn != "fexitbuilding") {
      continue;
    }
    m.fn = fn;
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
  // (its rate by the units' movement rule - zoo.exe 0x435c42)
  g.speed = m.speed > 0 ? Original::unitTilesPerSecond(m.speed) : t.speed;
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
  // most), swimming (bInWater); none: in the water (Task83). Its whileGoing
  // (0x4994c3): the set it runs (+0x54) over, started again (slot 0x48) -
  // ours: the set's swim played the whole way (waterTo)
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
  // Its name (ZTGuest::defaultName 0x4f70e4, made by ZTGuestType::create
  // 0x4f6060): text 9507 "Guest", a space and the next number of the
  // guests' one counter (0x4f6cc5: a count per class, 0x63e260 - from 1 on
  // a new map, startMap; kept in the save, saveEntities)
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
  if (this->onArrive)
    this->onArrive();
  return fee;
}

// ----------------------------------------------------------------------------
// Where it has been (zoo.exe +0x260, +0x334)
// ----------------------------------------------------------------------------
// zoo.exe guestRecordVisit 0x4a840e: the place (a tile, +0x34: its grid
// position) pushed onto the end of its visited list, its visit count +0x334
// up by 1, and past 10 places the oldest dropped. Called when it starts
// watching an exhibit (Task70's begin 0x42e527: a random tile of the habitat,
// 0x44149f), is done inside a building (Task6a_InsideBuilding::checkArrived 0x42a1db at 0x42a38a -> class
// slot 0x2ec, 0x42aa88), is turned away at its door (Task67 0x42c8af, Task68
// 0x42cd39, Task66 0x4a830c), and reaches scenery to look at (Task80 0x4406a4)
void Guests::recordVisit(Guest &g, int x, int y) {
  g.visits.push_back({x, y});
  g.visitCount++;
  if (g.visits.size() > Original::guestVisitedPlaces)
    g.visits.erase(g.visits.begin());
}

// (ours: a building or scenery by its own tile - the tile the original's
// saved objects give (and ours write back), inside its footprint)
void Guests::recordVisit(Guest &g, const PlacedObjects::Object &o) {
  recordVisit(g, static_cast<int>(std::floor(o.x)), static_cast<int>(std::floor(o.y)));
}

// zoo.exe guestVisitedHabitat 0x42281c: a place on its list in the habitat
// (0x410bf9: the habitat a grid position is in)
bool Guests::visitedExhibit(const Guest &g, int exhibit) const {
  if (!this->fences)
    return false;
  for (auto [x, y] : g.visits)
    if (this->fences->exhibitAt(x, y) == exhibit)
      return true;
  return false;
}

// zoo.exe 0x4285e2: a place on its list where that object stands, or one of
// its kind (the type's class slot 0x20 alike) - a guest that has been to one
// burger stand of late passes over the others, and looks at one kind of
// scenery once
bool Guests::visitedObject(const Guest &g, const PlacedObjects::Object &o) const {
  if (!this->objects || g.visits.empty())
    return false;
  const std::string file = PlacedObjects::fileOf(o);
  for (auto [x, y] : g.visits)
    for (const PlacedObjects::Object &p : this->objects->objects()) {
      if (p.fence)
        continue;
      const BuildingInfo &b = infoOf(PlacedObjects::fileOf(p));
      int w = std::max(1, (b.fx + 1) / 2), h = std::max(1, (b.fy + 1) / 2);
      if ((p.facing & 6) == 2 || (p.facing & 6) == 6)
        std::swap(w, h);
      const int x0 = static_cast<int>(std::floor(p.x - w / 2.0f)), y0 = static_cast<int>(std::floor(p.y - h / 2.0f));
      const bool on = (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h) ||
                      (x == static_cast<int>(std::floor(p.x)) && y == static_cast<int>(std::floor(p.y)));
      if (on && (p.id == o.id || PlacedObjects::fileOf(p) == file))
        return true;
    }
  return false;
}

// zoo.exe 0x4281fd: the habitats with animals (0x412167(0): +0x94, over the
// habitat manager's list - its tanks too, show tanks with them: read live
// 2026-11-16) plus the 'fun' buildings (0x427f43: the buildings of that
// kind, 0x413db8)
int Guests::placesToVisit() const {
  int n = 0;
  if (this->fences && this->animals)
    for (const Fences::Exhibit &ex : this->fences->exhibits()) {
      if (!ex.named)
        continue;
      for (const Animals::Member &m : this->animals->members())
        if (m.exhibit == ex.id) {
          n++;
          break;
        }
    }
  if (this->objects)
    for (const PlacedObjects::Object &o : this->objects->objects())
      if (!o.fence && infoOf(PlacedObjects::fileOf(o)).satisfies.count("fun"))
        n++;
  return n;
}

// ----------------------------------------------------------------------------
// Where to go next: an exhibit not yet seen (its animals' attractiveness,
// its favourite there +500, the further the better), else a walk; seen
// everything: most leave
// ----------------------------------------------------------------------------
int Guests::pickExhibit(Guest &g, int &vx, int &vy) {
  int best = -1;
  float bestScore = -1e30f;
  // (zoo.exe 0x427b08: the habitats with animals, tanks among them, less a
  // tank with a show - class slot 0x20 true, a tank, and its +4, its show,
  // set: ours, a show tank)
  for (const Fences::Exhibit &ex : this->fences->exhibits()) {
    if (!ex.named || (ex.tank && ex.show) || visitedExhibit(g, ex.id))
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
  // (a predator of guests: ZTGuest::guestPreyOf 0x4302ea -> 0x4138f0 - an
  // animal, its +0x394 clear, its cPrey listing the man (9503, 0x41391d), not
  // an egg (+0x395). ours: +0x394 taken as a baby - not read; not boxed)
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

// Its viewing area (zoo.exe 0x4232f4: 0x43b8a2 with the way kept in +0x328 -
// standing on zoo ground (the habitat of its tile, class slot 0x248
// habitatAt, has +0x2c set: not in an exhibit), its tile's slot for that
// way, an area that sees something). ours, given no slots (no World): the
// area of the exhibit it's at whose viewing tiles hold its tile
bool Guests::viewArea(const Guest &g, ViewArea &out) const {
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  if (this->areaInSlot) {
    if (g.viewWay < 0 || this->fences->exhibitAt(gx, gy) >= 0 || !this->areaInSlot(gx, gy, g.viewWay, out))
      return false;
    return !out.seen.empty(); // (0x43aac6: an area that sees nothing isn't one)
  }
  if (!this->viewAreas || g.target < 0)
    return false;
  for (ViewArea &a : this->viewAreas(g.target))
    if (std::find(a.viewing.begin(), a.viewing.end(), std::make_pair(gx, gy)) != a.viewing.end()) {
      out = std::move(a);
      out.exhibit = g.target;
      return !out.seen.empty();
    }
  return false;
}

// zoo.exe 0x43b92e: from a random way (rand & 7) on round all 8, the first
// whose slot of its tile holds an area that sees something (0x43b8a2,
// 0x43aac6) - kept (+0x328); none, -1
bool Guests::findViewArea(Guest &g) {
  g.viewWay = -1;
  if (!this->areaInSlot) {
    g.viewWay = 0;
    ViewArea a;
    if (viewArea(g, a))
      return true;
    g.viewWay = -1;
    return false;
  }
  const int from = std::uniform_int_distribution<int>(0, 7)(this->rng);
  for (int k = 0; k < 8; k++) {
    g.viewWay = (from + k) & 7;
    ViewArea a;
    if (viewArea(g, a))
      return true;
  }
  g.viewWay = -1;
  return false;
}

// zoo.exe 0x447a00: the area's seen tiles in order, every animal on each
// counted once; a kind (its type's cNameID, type slot 0x20) listed the first
// time it's met (+0x24)
std::vector<int> Guests::areaKinds(const ViewArea &a) const {
  std::vector<int> kinds;
  if (!this->animals)
    return kinds;
  for (const auto &tile : a.seen)
    for (const Animals::Member &m : this->animals->members()) {
      if (static_cast<int>(std::floor(m.x)) != tile.first || static_cast<int>(std::floor(m.y)) != tile.second)
        continue;
      const int id = this->animals->types()[m.type].nameId;
      if (std::find(kinds.begin(), kinds.end(), id) == kinds.end())
        kinds.push_back(id);
    }
  return kinds;
}

// How crowded the viewing area it stands in is (zoo.exe 0x425f6a, its area
// by 0x4232f4): the guests on the area's viewing tiles over how many tiles
// it has, times 10, cut to a whole number; no area, 0. ours: the counts
// brought up to date once a second of game time (World's viewAreas)
int Guests::areaCrowd(const Guest &g) const {
  ViewArea a;
  if (!viewArea(g, a) || a.viewing.empty())
    return 0;
  return static_cast<int>(static_cast<float>(a.guests) / static_cast<float>(a.viewing.size()) * 10.0f);
}

// zoo.exe 0x4232f4 (the guest's area) and the area's counts (0x447a00: every
// animal standing on one of the area's seen tiles, each once - +0x10 how
// many (0x42330b), +0xc how many kinds (0x426013), +0x18 how many sick,
// animal +0x3a7 (0x425f2e, 0x589693))
bool Guests::areaSight(const Guest &g, Sight &out) const {
  out = Sight();
  ViewArea a;
  if (!this->animals || !viewArea(g, a))
    return false;
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
  // Attacked by a predator (+0x33b, set by animalCaughtTarget 0x4a6043):
  // leaving, from its next behaviour set on (ZTGuest::startBehaviour
  // 0x422498, class slot 0x48: +0x339 set, then ZTUnit's 0x43ca83)
  if (g.attacked && !g.leaving) {
    ZT_STEP("0x422498", "Attacked by a predator: leaving the zoo");
    g.leaving = true;
  }
  // Eating standing up (Task6d_EatStanding): its bEatStanding set's
  // fPlay(idle) after the eatstanding play, then done (checkArrived
  // 0x52d9c4: the set over)
  if (g.eatStep == 0 && !g.leaving) {
    g.eatStep = 1;
    float secs = 1.0f;
    if (Animation *a = this->art(g, "idle"))
      if (a->frameTimeMs() > 0 && a->frameCount() > 0)
        secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
    play(g, "idle", secs);
    return;
  }
  // No seat for its food (Task6c_Eat): no walk about made yet - whileGoing
  // 0x52d955 tries again
  if (g.eatStep == kEatWalkAbout && !g.leaving && g.inventory[0]) {
    if (!walkAbout(g))
      play(g, "stand", static_cast<float>(Original::updateMs) / 1000.0f);
    return;
  }
  g.eatStep = -1;
  // Leaving (Task71: begin 0x4a67eb, whileGoing 0x4233ea -> guestLeaveZoo
  // 0x4a6889): to the gate; afraid (+0x33a), "I'm leaving the zoo.  Animals
  // are running free!" (10247) and running there (bRunFromPredator, the
  // emergency off-path cost - "Leaving Zoo - Saw Escaped Animal"); attacked
  // (+0x33b), running there too, the thought only when afraid (0x4a67eb)
  if (g.leaving) {
    g.guide = -1; g.tourHeard = false;
    ZT_STEP("0x4a6889", "Leaving: off to the gate", {"happiness", g.happiness}, {"afraid", g.sawEscape ? "yes" : "no"},
            {"attacked", g.attacked ? "yes" : "no"});
    g.state = State::Leaving;
    if (g.sawEscape)
      think(g, 10247);
    const bool run = g.sawEscape || g.attacked;
    if (!walkTo(g, this->entranceX, this->entranceY, run ? t.runSpeed : t.speed, run))
      g.x = -1000; // (nowhere to walk: gone)
    return;
  }
  // Done being sick (Task79_GuestSick: checkArrived 0x5b05e4 waits while the
  // set it runs is still bSick - its one fPlayWithSound; atTarget 0x5b0621:
  // +0x33e set)
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
  // A show (zoo.exe chooseTask 0x43a6e4 at 0x440eb7: unless its task is
  // Task64's, stopped (slot 0x174) and 0x440f0c asked for the nearest open
  // grandstand - before the attraction pick and "nothing to pick").
  // ours: no Task64 kept, so it's asked at every pick
  if (this->showPick && this->showPick(g))
    return;
  // Seen everything (zoo.exe guestSeenEverything 0x4a6acc: a guest not
  // leaving (task 0x71) nor on the water (0x410fe0: its tile's +0x83 & 3),
  // rand % 100 under cLeaveChanceDone (type +0x258): leaving (+0x339) and
  // "I've seen everything in the zoo; I have to leave now." (10238). ours: a
  // leaving or swimming guest never gets here)
  auto seenEverything = [&]() {
    const int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
    ZT_STEP("0x4a6acc", "Seen everything: leaves? (cLeaveChanceDone)", {"roll", roll},
            {"cLeaveChanceDone", this->typeList[g.type].leaveDone});
    if (roll >= this->typeList[g.type].leaveDone)
      return false;
    think(g, kSeenAll);
    g.leaving = true;
    decide(g);
    return true;
  };
  // Where next (zoo.exe guestPickAttraction 0x4282e1): an exhibit with
  // animals not on its visited list (0x427b08: habitatsWithAnimals 0x440d9a,
  // less guestVisitedHabitat 0x42281c), a 'fun' building whose kind isn't on
  // it (0x427d5c: 0x427f43, less 0x4285e2), or scenery to look at - drawn by
  // weight: the exhibits' count, the buildings' count, and the scenery's (its
  // cEstheticWeight / 100, at least 1: 0x428039)
  if (this->objects) {
    std::vector<int> fun;
    int unseen = 0, scenery = 0, total = 0;
    auto weigh = [&]() {
      fun.clear();
      const auto &objs = this->objects->objects();
      for (size_t i = 0; i < objs.size(); i++)
        if (!objs[i].fence && infoOf(PlacedObjects::fileOf(objs[i])).satisfies.count("fun") &&
            !visitedObject(g, objs[i]))
          fun.push_back(static_cast<int>(i));
      unseen = 0;
      for (const Fences::Exhibit &e : this->fences->exhibits()) {
        if (!e.named || (e.tank && e.show) || visitedExhibit(g, e.id)) // (0x427b08: show tanks left out)
          continue;
        for (const Animals::Member &m : this->animals->members())
          if (m.exhibit == e.id) {
            unseen++;
            break;
          }
      }
      scenery = sceneryWeight(g);
      total = unseen + static_cast<int>(fun.size()) + scenery;
    };
    weigh();
    // Nothing to pick (zoo.exe chooseTask 0x43a6e4 at 0x440ee2: 0x427c88,
    // the same three weights, 0 or less): its visit count +0x334 back to 0
    // (its list kept), guestSeenEverything - not leaving, a walk (task 1)
    if (total <= 0) {
      ZT_STEP("0x43a6e4", "Nothing left to pick: its visit count back to 0", {"visits", g.visitCount});
      g.visitCount = 0;
      if (seenEverything())
        return;
      g.target = -1;
      wander(g);
      return;
    }
    // Task64_PickAttraction::whileGoing (zoo.exe 0x4281be): the places to
    // visit (0x4281fd: the habitats with animals plus the 'fun' buildings) at
    // or under its visit count +0x334 - the visited list emptied, the count
    // back to 0, the task over (class slot 0x60) and guestSeenEverything;
    // else, any places at all, guestPickAttraction. ours: not leaving, it
    // picks at once from its emptied list, as chooseTask's next Task64 would
    const int places = placesToVisit();
    ZT_STEP("0x4281be", "Places to visit against its visit count", {"places", places}, {"visits", g.visitCount});
    if (places <= g.visitCount) {
      g.visits.clear();
      g.visitCount = 0;
      if (seenEverything())
        return;
      weigh();
    }
    const auto &objs = this->objects->objects();
    const int roll = total > 0 ? std::uniform_int_distribution<int>(0, total - 1)(this->rng) : 0;
    if (total > 0 && roll >= unseen && roll < unseen + static_cast<int>(fun.size())) {
      // (zoo.exe 0x42c775: the one drawn - the roll less the exhibits'
      // count, an index into the unvisited list - goal kind +0x24e, task
      // 0x66 to it. With zoo.ini [AI] useAttractionAttractiveness
      // ([0x63b088], read at 0x4bc8f8; not in the game's zoo.ini, so 0)
      // the one scoring highest instead (0x42c77c -> 0x5cfc48:
      // attractionHighestScore 0x60b086 over the unvisited list from the
      // guest's tile, Original::attractionScore - the furthest)
      int drawn = fun[static_cast<size_t>(roll - unseen)];
      if (this->useAttractionAttractiveness) {
        const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
        int bestScore = -1;
        for (int i : fun) {
          const PlacedObjects::Object &f = objs[static_cast<size_t>(i)];
          const int s = Original::attractionScore(static_cast<int>(std::floor(f.x)) - gx,
                                                  static_cast<int>(std::floor(f.y)) - gy);
          if (s > bestScore)
            bestScore = s, drawn = i;
        }
      }
      const PlacedObjects::Object &o = objs[static_cast<size_t>(drawn)];
      ZT_STEP("0x4282e1",
              this->useAttractionAttractiveness ? "Picks the attraction scoring highest (the furthest)"
                                                : "Picks an attraction it hasn't been to (the one drawn)",
              {"attraction", o.typeName}, {"index", roll - unseen}, {"attractions left", static_cast<int>(fun.size())},
              {"exhibits unseen", unseen});
      if (seekBuilding(g, "fun", 1000, true, drawn))
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
  // (ours: the exhibit drawn, or the attraction or scenery drawn not
  // reached - the original's Task64 draws again next tick - an exhibit not
  // on its list, else a walk)
  int vx = -1, vy = -1;
  int ex = pickExhibit(g, vx, vy);
  ZT_STEP("0x428917", "Picks an exhibit it hasn't seen (attractiveness, favourite, distance)", {"exhibit", ex},
          {"visits", g.visitCount}, {"to x", vx}, {"to y", vy});
  if (ex >= 0 && walkTo(g, vx, vy, t.speed)) {
    g.target = ex;
    return;
  }
  g.target = -1;
  wander(g);
}

// Testing an electric fence while watching (zoo.exe 0x425af4, 0x425e30): after
// a play of its bGawk, a cTestFenceChance in 100 (guests.ai: 20) to touch a
// live electric fence on a side of its tile - those with the fencezap
// animation (the man) - and zapped: bTestFence (fencezap, the zap sound),
// cZapHappinessHit (-35), "Yow! I just got electrocuted by %s, this place is
// dangerous." (10249) of its viewing area's exhibit, and the viewing task
// done (class slot 0x48: atTarget, so 0x42ea14 - its time watched, the
// donation)
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
    float was = g.dHappy;
    g.dHappy += static_cast<float>(t.zapHit);
    if (this->sounds && this->soundFile.count("zap"))
      Sound::get().play(this->soundFile["zap"], this->soundAtten["zap"] + 600);
    ViewArea area;
    const Fences::Exhibit *ex = viewArea(g, area) ? this->fences->exhibit(area.exhibit >= 0 ? area.exhibit : g.target) : nullptr;
    const std::string name = ex ? ex->name : std::string();
    if (ex)
      think(g, 10249, name);
    ZT_STEP("0x425af4", "Zapped testing the fence", {"roll", roll}, {"cTestFenceChance", t.testFenceChance},
            {"cZapHappinessHit", t.zapHit}, {"happiness to come", was, g.dHappy}, {"exhibit", name});
    viewDone(g);
    play(g, "fencezap", secs);
    g.state = State::Walking;
    return true;
  }
  return false;
}

// One play of its bGawk (guests.ai [<kind>\BehaviorSet\bGawk]:
// fPlaySetProb(bGawk1, <w>, bGawk2, <w>, bGawk3, <w>) - gawk, camera or
// stand, each an fPlay: its animation once through), facing the exhibit
void Guests::playGawk(Guest &g) {
  const Type &t = this->typeList[g.type];
  int total = std::max(1, t.gawkWeights[0] + t.gawkWeights[1] + t.gawkWeights[2]);
  int roll = std::uniform_int_distribution<int>(0, total - 1)(this->rng);
  std::string anim = roll < t.gawkWeights[0] ? "gawk1" : roll < t.gawkWeights[0] + t.gawkWeights[1] ? "camera" : "stand";
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
  float secs = 2.0f;
  if (Animation *a = this->art(g, anim))
    if (a->frameTimeMs() > 0 && a->frameCount() > 0)
      secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
  play(g, anim, secs);
  g.state = State::Gawking;
  if (anim == "camera" && this->sounds)
    Sound::get().play("guests/cam1", 1000);
}

// Task70_ViewAnimals::begin (zoo.exe 0x42e527), at the viewing tile:
// - its viewing area found (0x43b92e) - none, the task fails and it picks
//   again;
// - an exhibit it has already been to (its visit list, 0x42281c): the list
//   cleared and the task fails;
// - else its bGawk, the gawk time (0x42e793) as the task's time, the start
//   (+0x298, 0x42e8ce), "Viewing animals", and the kinds its area sees
//   (0x42e909: area +0x24) onto its recent-species list (+0x278) - as many
//   oldest dropped as make room for 5, then as many of the new as fit
bool Guests::viewBegin(Guest &g) {
  g.viewEnd = 0;
  if (!findViewArea(g)) {
    ZT_STEP("0x42e527", "Can't watch: no viewing area at its tile", {"exhibit", g.target});
    g.target = -1;
    return false;
  }
  // (on a tour, Task76_GuestFollowGuide: no visited list looked at or
  // added to)
  const bool tour = g.guide >= 0;
  if (!tour && visitedExhibit(g, g.target)) {
    ZT_STEP("0x42e527", "Been to this exhibit: its visited list emptied, its visit count 0", {"exhibit", g.target});
    g.visits.clear();
    g.visitCount = 0;
    g.target = -1;
    return false;
  }
  ViewArea area;
  viewArea(g, area);
  const std::vector<int> kinds = areaKinds(area);
  std::vector<int> &recent = g.recentSpecies;
  const int k = static_cast<int>(kinds.size());
  int n = static_cast<int>(recent.size());
  int add = k;
  if (n + k > 5) {
    const int drop = n + k - 5;
    recent.erase(recent.begin(), recent.begin() + std::min(drop, n));
    n = static_cast<int>(recent.size());
    if (n + add > 5)
      add = 5 - n;
  }
  recent.insert(recent.end(), kinds.begin(), kinds.begin() + std::max(0, add));
  const int secs = gawkSeconds(g);
  g.viewStart = this->clock;
  g.viewEnd = this->clock + secs;
  // (0x42e655: a random tile of the habitat, 0x44149f, onto its visited list)
  if (!tour)
    if (const Fences::Exhibit *ex = this->fences->exhibit(g.target); ex && !ex->tiles.empty()) {
      auto it = ex->tiles.begin();
      std::advance(it, std::uniform_int_distribution<size_t>(0, ex->tiles.size() - 1)(this->rng));
      recordVisit(g, it->first, it->second);
    }
  ZT_STEP("0x42e527", "Viewing animals (its area, the gawk time, the kinds it sees)", {"exhibit", g.target},
          {"way", g.viewWay}, {"seconds", secs}, {"kinds seen", k}, {"recent kinds", static_cast<int>(recent.size())});
  playGawk(g);
  // A follower there while its guide talks: cheered by the guide's bonus
  // (Task76_GuestFollowGuide::atTarget 0x58d0cf: not yet heard (+0x338),
  // its guide (+0x2e4) on task 0x77 - speaking - and its +0x1a8 clear: it
  // faces the guide, its viewing area's guide bonus (0x58a8dc -> 0x426236,
  // +0x58) onto its pending adult and child happiness, heard. whileGoing
  // 0x58ce6c: no route, the task over)
  if (g.guide >= 0 && !g.tourHeard && this->guideSpeaking && this->guideSpeaking(g.guide)) {
    g.dHappy += static_cast<float>(this->guideBonusOf ? this->guideBonusOf(g.guide) : 30);
    g.tourHeard = true;
  }
  return true;
}

// zoo.exe 0x425a15: from the newest of its recent kinds back, the first that
// has a mimic (0x425b6d: chimpanzee, gorilla - bMimic; the dinosaurs that
// roar - bMimic2); none, nothing; else cMimicChance in 100 to play it
bool Guests::mimic(Guest &g) {
  const Type &t = this->typeList[g.type];
  for (int i = static_cast<int>(g.recentSpecies.size()) - 1; i >= 0; i--) {
    const int id = g.recentSpecies[static_cast<size_t>(i)];
    const int which = Original::mimicOf(id);
    if (which == 0)
      continue;
    const int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
    ZT_STEP("0x425a15", "Mimics an animal it saw? (cMimicChance)", {"kind", id}, {"roll", roll},
            {"cMimicChance", t.mimicChance});
    if (roll >= t.mimicChance)
      return false;
    const std::string anim = which == 1 ? "mimic" : "mimic2";
    float secs = 2.0f;
    if (Animation *a = this->art(g, anim))
      if (a->frameTimeMs() > 0 && a->frameCount() > 0)
        secs = a->frameCount() * a->frameTimeMs() / 1000.0f;
    play(g, anim, secs);
    g.state = State::Gawking;
    return true;
  }
  return false;
}

// zoo.exe 0x425a85 (guestTapTank): cTapWallChance in 100; then the sides of
// its tile in turn (0 N, 2 E, 4 S, 6 W) - the piece on that edge, in its own
// tile's slot that way or the next tile's slot facing back. A fence there
// that isn't a tank wall, or a tank wall whose height isn't set (ZTFence
// +0x464, set with its height +0x454 by 0x44ac6e: its tank's wall height,
// ours the tank's wallSub), the next side. The first tank wall found
// decides: its tile's ground (+0x3c) more than 1 above the guest's, or its
// height plus that ground not above the guest's - no tap; else it faces
// that way (+0x12c) and plays bTapTank (0x4a7e94: the set it's already
// playing goes on, not again from its start)
bool Guests::tapTank(Guest &g) {
  const Type &t = this->typeList[g.type];
  const int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
  if (roll >= t.tapWallChance)
    return false;
  const int tx = static_cast<int>(std::floor(g.x)), ty = static_cast<int>(std::floor(g.y));
  const MapTile *own = this->map->getTile(tx, ty);
  if (!own)
    return false;
  static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}; // N E S W (zoo.exe 0, 2, 4, 6)
  for (int k = 0; k < 4; k++) {
    const Fences::Edge e = k == 0 ? Fences::Edge{true, tx, ty}
                         : k == 1 ? Fences::Edge{false, tx + 1, ty}
                         : k == 2 ? Fences::Edge{true, tx, ty + 1}
                                  : Fences::Edge{false, tx, ty};
    const Fences::Piece *p = this->fences->at(e);
    if (!p || p->type < 0 || p->type >= static_cast<int>(this->fences->types().size()))
      continue;
    if (!this->fences->types()[static_cast<size_t>(p->type)].tank)
      continue;
    const Fences::Exhibit *tank = this->fences->exhibit(p->tank);
    const int height = tank ? tank->wallSub : 0;
    if (height == 0)
      continue;
    const bool mine = p->ownerX == tx && p->ownerY == ty;
    const MapTile *wall = mine ? own : this->map->getTile(tx + dirs[k][0], ty + dirs[k][1]);
    if (!wall)
      continue;
    const int ground = static_cast<int>(own->height), wallGround = static_cast<int>(wall->height);
    ZT_STEP("0x425a85", "Taps a tank's glass? (its height over the guest's ground)", {"roll", roll},
            {"cTapWallChance", t.tapWallChance}, {"way", k * 2}, {"wall height", height},
            {"wall ground", wallGround}, {"guest ground", ground});
    if (wallGround > ground + 1 || height + wallGround <= ground)
      return false;
    g.fx = static_cast<float>(dirs[k][0]);
    g.fy = static_cast<float>(dirs[k][1]);
    if (g.tapStep >= 0)
      return true;
    return tapPlay(g, 0);
  }
  return false;
}

// bTapTank's fPlays one after another (each its animation once through);
// one its art hasn't is passed over
bool Guests::tapPlay(Guest &g, int step) {
  const Type &t = this->typeList[g.type];
  for (; step < static_cast<int>(t.tapSet.size()); step++) {
    const std::string &anim = t.tapSet[static_cast<size_t>(step)];
    Animation *a = t.anims.count(anim) ? this->art(g, anim) : nullptr;
    if (!a || a->frameTimeMs() <= 0 || a->frameCount() <= 0)
      continue;
    play(g, anim, a->frameCount() * a->frameTimeMs() / 1000.0f);
    g.tapStep = step;
    g.tapUpdateMs = 0;
    g.state = State::Gawking;
    return true;
  }
  return false;
}

// Task70_ViewAnimals::whileGoing (zoo.exe 0x42325d -> 0x425932), its play
// done and its time not up: mimic an animal (0x425a15), else test the fence
// (0x425af4, which ends the task), else tap a tank's glass (0x425a85), else
// its bGawk again. While it plays bGawk, bMimic or bMimic2 it waits; it
// also checks for 'bTapWall', which no guest plays - so while it taps
// (bTapTank) it looks again every update: the tick (update) calls this then
void Guests::viewNext(Guest &g) {
  if (mimic(g))
    return;
  if (testFence(g))
    return;
  if (tapTank(g))
    return;
  playGawk(g);
}

// Done watching (Task70's atTarget 0x42e9f9 -> guestDoneViewingExhibit
// 0x42ea14): the game time since it began (+0x298) onto the exhibit's
// viewing time (+0x128), then a donation: its viewing area sees an animal,
// none of them sick, not crowded (0x425f6a under 50): cAdmissionMultiple x
// the time (100 ns units) x (its area's habitat's suitability + its area's
// tour guide bonus, +0x58: 0x426236) x 0.01, $10 at most; its area let go
// (+0x328 -1); its needs looked at next tick (+0x33e)
void Guests::viewDone(Guest &g) {
  const double watched = std::max(0.0, this->clock - g.viewStart);
  this->exhibitBooks[g.target].viewed += watched;
  int n = 0;
  bool sick = false;
  int suit = this->animals->exhibitSuitability(g.target), guide = 0;
  ViewArea area;
  if (this->viewAreas || this->areaInSlot) {
    Sight sight;
    if (areaSight(g, sight)) {
      n = sight.animals;
      sick = sight.sick > 0;
    }
    if (viewArea(g, area)) {
      if (area.exhibit >= 0)
        suit = this->animals->exhibitSuitability(area.exhibit);
      guide = this->areaInSlot ? area.guideBonus : this->tourBonusAt ? this->tourBonusAt(g.target) : 0;
    }
  } else {
    for (const Animals::Member &m : this->animals->members())
      if (m.exhibit == g.target) {
        sick = sick || m.sick;
        n++;
      }
  }
  const int crowd = areaCrowd(g);
  if (n > 0 && !sick && crowd < Original::donationCrowdLimit) {
    // (game time in 100 ns units: a 12 s game day is 864e9)
    double ticks = watched * (864e9 / 12.0);
    double d = Original::economy().admissionMultiple * ticks * (suit + guide) * 0.01;
    if (d > 0) {
      d = std::min(d, 10.0);
      this->donations += d;
      this->exhibitBooks[g.target].donated += d;
    }
    ZT_STEP("0x42ea14", "Done watching: a donation (healthy, not crowded)", {"seconds watched", watched},
            {"suitability", suit}, {"tour bonus", guide}, {"donation ($10 at most)", d > 0 ? d : 0.0});
  }
  g.viewWay = -1;
  g.viewEnd = 0;
  g.target = -1;
  g.recheck = true;
}

// ----------------------------------------------------------------------------
// Scenery to look at (zoo.exe guestPickAttraction 0x4282e1, its third
// group). The zoo's list of it (0x639098, filled by 0x451785 from
// 0x4f40af): objects whose kind has a cEstheticWeight over 0, standing
// inside the zoo; none of a kind on its visited list (0x4285e2)
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
    if (b.estheticWeight <= 0 || visitedObject(g, o) ||
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
    if (b.estheticWeight <= 0 || visitedObject(g, o) || !this->fences->insideZoo(ox, oy))
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
  return best->id;
}

// There (Task80::atTarget 0x4a6d59: task 0x81 - Task81_GawkObject, zoo.exe
// begin 0x4a6d68): it faces the object and
// plays bGawkObject1..6 by the object's EstheticBonus for its kind (<0,
// 0-5, 6-10, 11-15, 16-25, 26-100; over 100 nothing - 0x4a6ebd), each a
// draw of gawk, camera or stand, once; and it has the kind's cUsedThought
void Guests::gawkScenery(Guest &g) {
  const int id = g.gawkObject;
  g.gawkObject = -1;
  // (no set played: its task over at once - checkArrived 0x44118f, atTarget
  // 0x441290: +0x33e, its needs looked at next tick)
  g.recheck = true;
  if (!this->objects)
    return;
  const PlacedObjects::Object *obj = nullptr;
  for (const PlacedObjects::Object &o : this->objects->objects())
    if (o.id == id)
      obj = &o;
  if (!obj)
    return;
  // (Task80's checkArrived 0x4406a4: the object's tile onto its visited
  // list, 0x4a840e)
  recordVisit(g, *obj);
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
  g.recheck = false; // (set when the play is over)
  if (b.usedThought)
    think(g, b.usedThought);
  if (anim == "camera" && this->sounds)
    Sound::get().play("guests/cam1", 1000);
}

// ----------------------------------------------------------------------------
// The status tick
// ----------------------------------------------------------------------------
// zoo.exe guestViewingTick 0x43afde: every cViewingAreaCheck while
// watching, what it thinks of what it sees - from its viewing area, found
// again each time (+0x328 cleared, 0x43b92e: a random way first; none:
// nothing), only when the area sees an animal; the exhibit judged is the
// area's (0x423354: its habitat's +0xe8):
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
  if (!findViewArea(g) || !areaSight(g, sight) || sight.animals <= 0)
    return;
  ViewArea area;
  viewArea(g, area);
  const int exhibit = area.exhibit >= 0 ? area.exhibit : g.target;
  const Type &t = this->typeList[g.type];
  auto about = [&](int sid, int lo, int hi) {
    const int id = pickAnimal(exhibit, lo, hi);
    if (id < 0)
      return;
    const Animals::Member *m = this->animals->member(id);
    think(g, sid, m ? m->name : std::string(), id, exhibit);
  };
  if (sight.sick > 0) {
    g.dHappy += static_cast<float>(t.sickAnimalChange);
    if (sight.sick > 1)
      think(g, 10096);
    about(10545, -100, 100);
  }
  const int h = this->animals->exhibitSuitability(exhibit);
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
    // (a dinosaur's dung, dinopoo, by its own .ai: -25 a guest, not -5)
    const BuildingInfo &dinopoo = infoOf("scenery/other/dinopoo.ai");
    const BuildingInfo &trash = infoOf("scenery/other/trash.ai");
    for (const ZooItems::Item &i : this->items->items())
      if (i.kind == ZooItems::Kind::Dung)
        add(ZooItems::dinoDung(i) ? dinopoo : poo, i.x, i.y, 1, 1, this->dungStink);
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
  // (its 5x5 block, tile / 5: ZTGuest::blockEsthetic 0x43b6c0 - the block's
  // map at +4 by the kind's cNameID, none 0 - and blockStink 0x43b84a, the
  // block's +0x10)
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
  // What the last tick's checks found (zoo.exe guestApplyPending 0x43b03a:
  // each += its pending value, held to the type's limits, the pendings 0)
  float wasHappy = g.happiness, wasHunger = g.hunger, wasThirst = g.thirst, wasBathroom = g.bathroom,
        wasTired = g.tired;
  g.happiness = std::clamp(g.happiness + g.dHappy, Original::guestHappinessMin, Original::guestHappinessMax);
  g.hunger = std::clamp(g.hunger + g.dHunger, Original::guestNeedMin, Original::guestNeedMax);
  g.thirst = std::clamp(g.thirst + g.dThirst, Original::guestNeedMin, Original::guestNeedMax);
  g.bathroom = std::clamp(g.bathroom + g.dBathroom, Original::guestNeedMin, Original::guestNeedMax);
  g.tired = std::clamp(g.tired + g.dTired, Original::guestNeedMin, Original::guestNeedMax);
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
        g.eatStep = -1;
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
    // (off for it already - its goal +0x24e that kind, or "eat" for food:
    // no increment and no anger at its limit; zoo.exe 0x43bb49)
    const bool goingFor = g.need == what || (std::string(what) == "food" && g.need == "eat");
    if (!g.recheck) {
      if (--check >= 0)
        return;
      check = period - 1;
      if (!goingFor) {
        delta += static_cast<float>(increment);
        if (value >= limit)
          g.dHappy += static_cast<float>(angry);
      }
    }
    ZT_STEP("", "A need check", {"need", what}, {"value", value}, {"just done with something", g.recheck ? "yes" : "no"},
            {"increment", !g.recheck && !goingFor ? increment : 0},
            {"at its limit: happiness", !g.recheck && !goingFor && value >= limit ? angry : 0}, {"threshold", threshold});
    // (food or a drink in its hands: not off for more - 0x43bbb5, 0x43bd3b)
    const int hand = handFor(what);
    if (hand >= 0 && hand <= 1 && g.inventory[hand])
      return;
    if (value >= threshold && g.building < 0 && !g.leaving && g.state != State::Fleeing && !g.puking &&
        g.eatStep == -1 && g.state != State::Water) {
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
    // Rubbish in hand: a bin within cSearchRadius (guestFindTrashCan
    // 0x59d5d3: the nearest 'trash' building, then guestGoToBuilding), else
    // dropped as litter (guestDropLitter 0x52da40; not on the water - ours:
    // kept till back on land), the task over (+0x33e set - ours: not, as
    // the tick clears it below before the next)
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
  // 0x59cbb8: in a tank - its tile's habitat, ZTUnit::habitat 0x410642,
  // slot 0x20 - "Hey! I don't want to be in the water!" and Task7f, out;
  // ours: an exhibit's water alike - a guest put down in a tank comes this
  // way). Lake water, out of the exhibits: guestEnterWater 0x46b6a3 (task
  // 0x83, no longer leaving) - the original's way there is putting one down
  // (putDown); ours also looks here, as our saves keep no task: a zoo loaded
  // with a guest out on the water, or the ground made water under it
  if (g.state != State::Water && !g.hidden && g.showTank < 0 && g.x > -999 && this->map) {
    const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
    const MapTile *t = this->map->getTile(gx, gy);
    const int ex = this->fences ? this->fences->exhibitAt(gx, gy) : -1;
    const Fences::Exhibit *e = ex >= 0 ? this->fences->exhibit(ex) : nullptr;
    if (t && ((e && e->tank) || t->terrainType == 9 || t->terrainType == 10)) {
      if (swimmable(gx, gy)) {
        enterWater(g);
      } else {
        think(g, 10568);
        outOfWater(g);
      }
    }
  }
}

// zoo.exe ZTGuest::guestEnterWater 0x46b6a3 (class slot 0xc8: a guest put
// down - placeObject 0x4d923c, endPlacing 0x443df7 - picked up with the
// guest panel's PickupGuest, ZUPDATE1's infogst.lyt): out of every
// building's users (0x46b5bf -> 0x5a08ac), off its ride (slot 0x294(0):
// +0x284 = 0) and its ride's set (slot 0x29c: +0x288); then, not on a
// tank's tile (slot 0x268, onTankTile 0x4106c4: tile +0x85 & 0x20) and on
// water (tile +0x83 & 3): no longer leaving (+0x339 = 0), task 0x83
// (Task83_InWater, priority 1: making for land - a man who can't, drowns)
// and "Hey! I don't want to be in the water!" (10568). (On land: its needs
// looked at next tick, +0x33e, unless slots 0x28c and 0x290 - landed)
void Guests::enterWater(Guest &g) {
  g.ride = 0;
  g.rideSet.clear();
  g.leaving = false;
  inWater(g);
  think(g, 10568);
  ZT_STEP("0x46b6a3", "In the lake: no longer leaving, making for land (task 0x83)", {"tries", g.tries});
}

// zoo.exe 0x46b6a3's land branch: its needs looked at on the next tick
// (+0x33e) - unless its class slots 0x28c (ZTGuest::guestOverAThreshold
// 0x58d5eb: hunger, thirst, energy or bathroom at its c<Need>Threshold or
// more) and 0x290 (guestNearNeedBuilding 0x46b7d6: a building within 10
// tiles, gone to) both hold
void Guests::landed(Guest &g) {
  g.state = State::Walking;
  play(g, "stand", 0.0f);
  const Type &t = this->typeList[g.type];
  const bool inNeed = g.hunger >= t.hungerThreshold || g.thirst >= t.thirstThreshold ||
                      g.tired >= t.energyThreshold || g.bathroom >= t.bathroomThreshold;
  if (inNeed && nearNeedBuilding(g)) {
    ZT_STEP("0x46b6a3", "Put down on land in need, a building near: straight there", {"goal", g.need});
    return;
  }
  g.recheck = true;
  ZT_STEP("0x46b6a3", "Put down on land: its needs looked at next tick (+0x33e)", {"in need", inNeed ? "yes" : "no"});
}

// zoo.exe ZTGuest::guestNearNeedBuilding 0x46b7d6 (class slot 0x290) ->
// guestFindNearBuilding 0x46b818 (slot 0x280, radius 10): every building
// (the kind named "building", 0x646390 - 0x551561) in the guest's habitat
// (slot 0x248, habitatAt 0x41060e: both out of the exhibits, or in the same
// one) with room (buildingHasRoom 0x429330 -> 0x42ac87: ours, its users
// under its cCapacity), its tile's squared distance from the guest's
// (0x4118b1: tile +0x34/+0x38) 10 x 10 or less, serving one of a guest's
// kinds (slot 0x27c, 0x618f29: drink, food, bathroom, energy - the first it
// has, 0x41239d - that kind's index its goal, +0x24e); the nearest, a
// random one among ties (0x40f103). Found: its tile the guest's target
// (slot 0x2e4, setField244 0x43e6b3: +0x244; +0x214..+0x21c its point in
// the tile), and the next chooseTask (0x43a6e4 at 0x43b248) sends it there
// (guestGoToBuilding 0x43b2f6). Not by which need: a hungry guest put down
// by a restroom goes in it. ours: sent at once (seekBuilding, that building
// alone); no way to its door, its needs looked at next tick instead
bool Guests::nearNeedBuilding(Guest &g) {
  if (!this->objects || !this->fences)
    return false;
  static const char *const kinds[] = {"drink", "food", "bathroom", "energy"};
  const int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
  const int habitat = this->fences->exhibitAt(gx, gy);
  const auto &objs = this->objects->objects();
  int best = Original::guestPutDownBuildingRadius * Original::guestPutDownBuildingRadius;
  std::vector<std::pair<int, const char *>> nearest;
  for (size_t i = 0; i < objs.size(); i++) {
    const PlacedObjects::Object &o = objs[i];
    if (o.fence)
      continue;
    const BuildingInfo &b = infoOf(PlacedObjects::fileOf(o));
    const char *kind = nullptr;
    for (const char *k : kinds)
      if (!kind && b.satisfies.count(k))
        kind = k;
    if (!kind)
      continue;
    const int ox = static_cast<int>(std::floor(o.x)), oy = static_cast<int>(std::floor(o.y));
    if (this->fences->exhibitAt(ox, oy) != habitat)
      continue;
    int users = 0;
    for (const Guest &o2 : this->list)
      users += o2.building == static_cast<int>(i) ? 1 : 0;
    if (users >= b.capacity)
      continue;
    const int d = (ox - gx) * (ox - gx) + (oy - gy) * (oy - gy);
    if (d > best)
      continue;
    if (d < best)
      nearest.clear();
    best = d;
    nearest.push_back({static_cast<int>(i), kind});
  }
  ZT_STEP("0x46b818", "Put down in need: buildings within 10 tiles in its habitat with room",
          {"nearest", static_cast<int>(nearest.size())}, {"squared distance", nearest.empty() ? 0 : best});
  if (nearest.empty())
    return false;
  const auto &[at, kind] =
      nearest[std::uniform_int_distribution<size_t>(0, nearest.size() - 1)(this->rng)];
  return seekBuilding(g, kind, 1000, false, at);
}

// zoo.exe infoPickUp_click 0x46af10 -> 0x46ab75 (the guest panel's
// PickupGuest, shared with Staff Information's): the panel shut (nothing
// selected, ui_main_4b2322(0,1)), a guest off its ride (slot 0x294(0):
// +0x284 = 0), held (+0x143 = 1) and out of the world until it's put down
// (setPlaceItem 0x4f189a: the zoo view places it, +0x40c) - so nothing it
// does goes on meanwhile. (The type's pickupsound: guests.ai has no
// [Sounds], so none)
void Guests::pickUp(int id) {
  Guest *g = nullptr;
  for (Guest &x : this->list)
    if (x.id == id)
      g = &x;
  if (!g || g->x <= -999)
    return;
  g->ride = 0;
  g->path.clear();
  g->pathAt = 0;
  g->hidden = false; // (shown at the cursor; still a building's user till put down)
  this->carried = id;
  this->carriedFromX = g->x;
  this->carriedFromY = g->y;
  if (this->selected == id)
    this->selected = -1;
  int message = 0;
  this->carriedFit = canPutDown(g->x, g->y, message);
  this->carriedMessage = message;
}

// Where a held guest may be put down (zoo.exe zooViewMouse 0x4ded90, its
// guest branch at 0x4e6922, then placementCheck 0x4f885e):
// - in the zoo (0x40fb8d: 10311 "Zoo objects can only be placed inside the
//   main zoo wall.");
// - nothing on the tile: its four quarters' objects (tile +4..+0x10) none,
//   nor anything standing across it (0x427360: the tile's things whose box
//   crosses the guest's, a path alone not counted) - ours, the placed
//   objects' footprints (WorldIndex); over one, 10555 (0x293b, the thing
//   in the way lit red: 0x4dfc95);
// - its ground flat or a path's slope (tile +0x81 0, 'A', 'P', 0x14 or 5:
//   two corners side by side a step up - WorldMap::pathShapeOk);
// - in a tank (tankCheckPlacement 0x4df688): guests go in (slot 0xc4, the
//   type's +0x50: guests.ai cUnderwater = 1, "Can go into a tank"), not
//   a show tank (10561, 0x2941: the type's +0x6f not set).
// Water out of the exhibits is no bar (its slope aside): it swims.
// measured: Play original 2026-11-17 (sweep3.zoo): held green over open
// sand, a path, another guest, inside a walled enclosure and a tank's open
// water; red over a building and a tank toy; put down in the tank it swam,
// "Hey! I don't want to be in the water!", its panel open again
bool Guests::canPutDown(float x, float y, int &message) const {
  message = 0;
  if (!this->map || !this->fences)
    return false;
  const int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  if (!this->map->getTile(tx, ty))
    return false;
  if (!this->fences->insideZoo(tx, ty)) {
    message = 10311;
    return false;
  }
  if (this->index && this->index->blocked(tx, ty)) {
    message = 10555;
    return false;
  }
  if (!this->map->pathShapeOk(tx, ty))
    return false;
  const int ex = this->fences->exhibitAt(tx, ty);
  if (const Fences::Exhibit *e = ex >= 0 ? this->fences->exhibit(ex) : nullptr)
    if (e->tank && e->show) {
      message = 10561;
      return false;
    }
  return true;
}

// A held guest put down (zoo.exe placeObject 0x4d923c, the picked thing
// placed again: +0x143 = 0; on water or a tank's tile - tile +0x83 & 3,
// +0x85 & 0x20 - in the water, slot 0x274(1): +0x284 |= 0x80000; then
// its class slot 0xc8, ZTGuest::guestEnterWater 0x46b6a3: out of every
// building's users (0x46b5bf -> 0x5a08ac), off its ride (0x294(0)) and its
// ride's set (0x29c); on lake water, not a tank, making for land (task 0x83
// - enterWater); else landed. Then it's selected, its panel open again
// (ui_main_4b2322(it, 1), 0x431712 -> ui_infogst_41a78a))
void Guests::putDown(int id, float x, float y) {
  Guest *g = nullptr;
  for (Guest &k : this->list)
    if (k.id == id)
      g = &k;
  if (this->carried == id)
    this->carried = -1;
  if (!g)
    return;
  g->x = x;
  g->y = y;
  g->path.clear();
  g->pathAt = 0;
  g->building = -1;
  g->need.clear();
  g->hidden = false;
  g->slot = g->sitStep = g->eatStep = -1;
  g->ride = 0;
  g->rideSet.clear();
  g->waterTask = 0;
  const int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  const MapTile *t = this->map ? this->map->getTile(tx, ty) : nullptr;
  const bool water = t && (t->terrainType == 9 || t->terrainType == 10);
  const int ex = this->fences ? this->fences->exhibitAt(tx, ty) : -1;
  const Fences::Exhibit *e = ex >= 0 ? this->fences->exhibit(ex) : nullptr;
  const bool tank = e && e->tank;
  g->inWater = water || tank;
  ZT_STEP("0x4d923c", "Put down", {"x", x}, {"y", y}, {"water", water ? 1 : 0}, {"tank", tank ? 1 : 0});
  if (!tank && water && swimmable(tx, ty))
    enterWater(*g);
  else
    landed(*g);
  this->selected = id;
}

// zoo.exe endPlacing 0x443df7 (the zoo view's placing ended, +0x40c, by
// setPlaceItem 0x4f189a and others): the picked thing back where it was
// (+0x410..), held no more (+0x143 = 0) and its class slot 0xc8 - for a
// guest, guestEnterWater 0x46b6a3, as putting it down there
void Guests::putBack() {
  if (this->carried >= 0)
    putDown(this->carried, this->carriedFromX, this->carriedFromY);
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
    // A cHideBuilding building (the bounce houses) isn't drawn while it has
    // a user - taken in at its set's fEnterBuilding, its users list (+0x164)
    // not empty (ZTBuilding::draw 0x433aaf at 0x4413a5; 0x4a7972 at
    // 0x59df98 alike)
    for (int id : this->usedHidden)
      if (PlacedObjects::Object *o = this->objects->byId(id))
        o->showHidden = false;
    this->usedHidden.clear();
    for (const Guest &g : this->list)
      if (g.state == State::Using && g.takenIn && g.building >= 0 &&
          g.building < static_cast<int>(this->objects->all().size())) {
        PlacedObjects::Object &o = this->objects->all()[g.building];
        if (infoOf(PlacedObjects::fileOf(o)).hideBuilding) {
          o.showHidden = true;
          this->usedHidden.push_back(o.id);
        }
      }
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
    // (zoo.exe guestArrivalCheck 0x424375: the admission band, this+0x48 -
    // the price over economy.cfg's cAdultAdmission in turn; the rating's
    // band; their cell's chance of cCreateGuestChance*, chancePercent
    // 0x4244ab: rand % 100 under it - guestArriveTry 0x4164bd - and a guest
    // made at the gate, 0x4f6e3c; an arrival in a "good value" cell with
    // more than 10 guests in the zoo posts 10017 - 0x429d68)
    const Original::Economy &e = Original::economy();
    const int cat = admission > e.adultAdmission[0] ? 0 : admission > e.adultAdmission[1] ? 1
                  : admission > e.adultAdmission[2] ? 2 : admission > e.adultAdmission[3] ? 3 : 4;
    const int band = zooRating >= 81 ? 0 : zooRating >= 60 ? 1 : zooRating >= 30 ? 2 : 3;
    const int which = Original::guestArrivalChance[band][cat];
    const int chance = which >= 0 ? e.createGuestChance[which] : 0;
    const int roll = std::uniform_int_distribution<int>(0, 99)(this->rng);
    ZT_STEP("0x424375", "A guest at the gate? (admission and rating bands, cCreateGuestChance)", {"admission", admission},
            {"admission band", cat}, {"rating", zooRating}, {"chance", chance}, {"roll", roll});
    if (chance > 0 && roll < chance) {
      admit(admission);
      if (Original::guestArrivalGoodValue[band][cat] && static_cast<int>(this->list.size()) > Original::goodValueGuests)
        this->messages.push_back(kGoodValue);
    }
  }
  for (Guest &g : this->list) {
    // (held: out of the world until put down - zoo.exe 0x46ab75)
    if (g.id == this->carried)
      continue;
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
          // (Task71's checkArrived 0x4233c4: its walk's end on the tile
          // 0x410d04 gives of [0x63805c] - taken as the gate's, not read;
          // elsewhere, a new route, slot 0x4c)
          g.x = -1000; // out of the gate
          // Happy as it goes (over cMembershipJoinHappiness): may join the
          // zoo (cMembershipJoinChance percent): zoo.exe 0x4fa85d, at the
          // gate (+0x2a8 above the cfg's, rand % 100 below), a member booked
          if (g.happiness > Original::economy().membershipJoinHappiness &&
              std::uniform_int_distribution<int>(0, 99)(this->rng) < Original::economy().membershipJoinChance)
            this->members++;
        } else if (g.building >= 0 && g.state == State::Walking) {
          // (Task66/Task67's checkArrived, checkArrivedPathCost 0x4124b1:
          // the path's end - arrived - and a guest's off-path cost +0x230
          // back to cNonPathCost; ours keeps none on the guest: walkTo
          // takes it per route)
          useBuilding(g);
        } else if (g.state == State::Walking && g.gawkObject >= 0) {
          // (there: Task81_GawkObject plays its bGawkObject; the task over
          // once the set is - checkArrived 0x44118f - then atTarget
          // 0x441290 stops it and sets +0x33e: below, when the play ends)
          gawkScenery(g);
        } else if (g.state == State::Walking && g.target >= 0 && this->fences->exhibit(g.target)) {
          // (at its viewing tile - Task65_Guest: whileGoing 0x43aab4 routes
          // it when it has no route, checkArrived 0x43aa96 arrived on the
          // target's tile, its off-path cost back to cNonPathCost; and
          // Task65's atTarget 0x42612f makes task
          // 0x70: Task70_ViewAnimals begins - it can't, and
          // it picks again)
          if (!viewBegin(g))
            play(g, "stand", 0.0f);
        } else if (g.state == State::Walking && g.eatStep == kEatWalkAbout && !g.leaving) {
          // (no seat for its food: on the walk about's tile - Task6c_Eat's
          // checkArrivedOnTile 0x412603 - atTarget 0x52d9b8: task 0x6d)
          eatStanding(g);
        } else {
          if (g.state == State::Fleeing)
            g.state = State::Walking;
          play(g, "stand", 1.0f);
        }
      }
      continue;
    }
    // Watching an exhibit (Task70_ViewAnimals): its time up (checkArrived
    // 0x42321a: class slot 0x44, then atTarget), done - even mid-play; else
    // a play done, the next (whileGoing 0x42325d)
    if (g.state == State::Gawking && g.target >= 0 && g.viewEnd > 0) {
      if (this->clock >= g.viewEnd) {
        viewDone(g);
        g.state = State::Walking;
        decide(g);
      } else if (g.tapStep >= 0) {
        // Tapping (bTapTank): its next fPlay when one is done; and each
        // update (Original::updateMs) whileGoing looks again (0x42325d)
        g.tapUpdateMs += seconds * 1000.0f;
        if (g.animTime >= g.playFor && !tapPlay(g, g.tapStep + 1)) {
          g.tapStep = -1; // (the set over)
          viewNext(g);
        } else if (g.tapUpdateMs >= static_cast<float>(Original::updateMs)) {
          g.tapUpdateMs = 0;
          viewNext(g);
        }
      } else if (g.animTime >= g.playFor) {
        viewNext(g);
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
    // A building's set, on (0x5ad535's b<name><goal><slot>): its play
    // done, the next line (buildingSetLine); its fExitBuilding: out
    if (g.state == State::Using && g.sitStep >= 0) {
      g.sitStep++;
      if (buildingSetLine(g))
        continue;
    }
    if (g.state == State::Using && this->outNow(g))
      continue;
    // Done looking at scenery (Task81_GawkObject: its bGawkObject over -
    // checkArrived 0x44118f - atTarget 0x441290 sets +0x33e)
    if (g.state == State::Gawking && g.target < 0)
      g.recheck = true;
    // ours: loaded holding food (Guest::eatAfterLoad) - off to eat it, as
    // out of the stand (chooseTask 0x43a6e4 at 0x42a8b7)
    if (g.eatAfterLoad) {
      g.eatAfterLoad = false;
      if (g.inventory[0] && g.state == State::Walking && g.building < 0) {
        goEat(g);
        continue;
      }
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

// Out of the building it was in (its time inside over: Task6a_InsideBuilding
// - whileGoing 0x42a2b0 does nothing, atTarget 0x42aa41 makes task 0x69 -
// then Task69_ExitBuilding: whileGoing 0x42a899 walks to one of the slot's
// exit points at random (ours: placed at its exitpos - each stock slot has
// one), checkArrived 0x42a84f, atTarget 0x42dddf -> guestBuy 0x42d858): true
// when it has gone on at once (out on its water ride, off to eat what it
// bought)
bool Guests::outNow(Guest &g) {
  // Out again: what the building did, and what they bought
  const auto &objs = this->objects->objects();
  if (g.building >= 0 && g.building < static_cast<int>(objs.size())) {
    // (its time inside done, Task6a_InsideBuilding::checkArrived 0x42a1db at 0x42a38a -> class
    // slot 0x2ec, 0x42aa88: the building onto its visited list)
    recordVisit(g, objs[g.building]);
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
    // The price against its default (cDefaultCost): at or over
    // cItemExpensive times it too dear - "The prices at the %s are too
    // high" (10073), cPriceAngry1Change; else at or under cItemCheap
    // times it a bargain - 10072, cPriceHappy1Change. A seller judged it
    // as it was taken in, over the item (addUser: 0x42ce0a -> 0x42d1e2);
    // a cCommerce building that sells nothing judges it as it comes out
    // (guestBuy 0x42d858: type +0x200, its Sells, empty -> 0x42c53f).
    // Then a cCommerce
    // building's users pay its price x cPriceFactor (type +0x188) into
    // its takings (+0x1a0, +0x1a8) and the zoo's (0x42df22, 0x40f018);
    // one without cCommerce (restrooms, shelters, the bounce house)
    // charges nothing, whatever its price
    const float price = priceOf(bo, b);
    const Type &gt = this->typeList[g.type];
    const bool judged = b.commerce && b.sells.empty();
    bool tooDear = judged && b.price * Original::economy().itemExpensive <= price;
    bool cheap = judged && !tooDear && price <= Original::economy().itemCheap * b.price;
    if (b.commerce) {
      double take = price * b.priceFactor;
      this->concessions += take;
      bo.income += take;
    }
    ZT_STEP("0x42d858", "The price: too dear (cItemExpensive) or a bargain (cItemCheap)? Paid if cCommerce",
            {"price", price}, {"default", b.price}, {"cItemCheap", Original::economy().itemCheap},
            {"cItemExpensive", Original::economy().itemExpensive}, {"commerce", b.commerce ? 1 : 0},
            {"verdict", tooDear ? "too dear" : cheap ? "a bargain" : "fair"});
    if (tooDear) {
      think(g, 10073, buildingName(bo));
      g.dHappy += static_cast<float>(gt.priceAngry);
    } else if (cheap) {
      think(g, 10072, buildingName(bo));
      g.dHappy += static_cast<float>(gt.priceHappy);
    }
    ZT_STEP("0x42d858", "Out of the building: what it did to them", {"building", bo.typeName}, {"need", g.need},
            {"dHappy", g.dHappy}, {"dHunger", g.dHunger}, {"dThirst", g.dThirst}, {"dBathroom", g.dBathroom},
            {"dTired", g.dTired});
    if (goingOut && !this->forcedOut) {
      rideOut(g, bo, b);
      return true;
    }
    // (what it bought, won or ate - the burger, hot dog, pizza and the
    // like held, to be eaten later: Task6b_GoEat - came as it was taken
    // in: addUser, 0x42ce0a)
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
  // (out again: Task69_ExitBuilding::atTarget 0x42dddf - guestBuy
  // 0x42d858 for the building at its exit square, seen again (slot
  // 0xcc), the task over (slot 0x174); then chooseTask 0x43a6e4 at
  // 0x42a8b7: food in its hands (+0x26c[0]), off to eat it (task 0x6b,
  // priority 4); else +0x33e set: its needs looked at next tick)
  if (g.inventory[0]) {
    goEat(g);
    return true;
  }
  g.recheck = true;
  return false;
}

// zoo.exe 0x5ae745: the guests inside a building (its +0x164 list) put out
// (guestBuy 0x42d858 with 1); those on their way there aren't told
int Guests::putOut(int index) {
  int out = 0;
  for (Guest &g : this->list)
    if (g.building == index && g.state == State::Using) {
      g.sitStep = -1;
      g.reverse = false;
      this->forcedOut = true;
      this->outNow(g);
      this->forcedOut = false;
      out++;
    }
  return out;
}

int Guests::buildingGone(int index) {
  int out = 0;
  for (Guest &g : this->list)
    if (g.building == index && g.state == State::Using) {
      g.sitStep = -1;
      g.reverse = false;
      this->forcedOut = true;
      this->outNow(g);
      this->forcedOut = false;
      out++;
    }
  for (Guest &g : this->list) {
    if (g.building == index) {
      g.building = -1;
      g.need.clear();
      g.path.clear();
      g.pathAt = 0;
      g.recheck = true;
    } else if (g.building > index) {
      g.building--;
    }
  }
  return out;
}

void Guests::addToIndex(WorldIndex &index) const {
  for (const Guest &g : this->list)
    if (g.id != this->carried)
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
    // Held: its likeness at the cursor, green where it can be put down, red
    // where not (measured: the original's, Play original 2026-11-17)
    if (g.id == this->carried) {
      Fences::Drawable h{depth, sx, sy, a, facing(view, g),
                         this->carriedFit ? SDL_Color{0, 255, 0, 255} : SDL_Color{255, 60, 60, 255}, true};
      h.frame = 0;
      out.push_back(h);
      continue;
    }
    Fences::Drawable d{depth, sx, sy, a, facing(view, g), SDL_Color{255, 255, 110, 255}, g.id == this->hovered};
    d.frame = static_cast<int>(g.animTime * 1000.0f / ms);
    if (g.reverse && a->frameCount() > 0) // (fPlayReverse: last frame first)
      d.frame = a->frameCount() - 1 - d.frame % a->frameCount();
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
    if (g.id == this->carried)
      continue;
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
