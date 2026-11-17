#include "Staff.hpp"
#include "ItemCatalog.hpp"
#include "LabTrace.hpp"
#include "OriginalConstants.hpp"
#include "Guests.hpp"
#include "Shows.hpp"
#include "PlacedObjects.hpp"
#include "RenameCheats.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <queue>

#include "IniReader.hpp"
#include "Pathfinder.hpp"
#include "Features.hpp"
#include "Animals.hpp"
#include "Sound.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"

// (a staff member's job, for the Lab's steps: Staff::Job in order)
static const char *const kJobNames[] = {"nothing", "fix a fence", "service a filter", "sweep litter", "visit an exhibit", "catch an animal", "give a tour", "empty a bin", "bag dung", "run from an animal"};

namespace {
constexpr float kIdleSeconds = 2.0f;   // fPlayTime(idle,2)
constexpr int kWanderRadius = 5;       // tiles, for a walk about
// Lang strings: what they're doing. The maintenance worker's by its job
// (zoo.exe ZTMaintenanceWorker::statusText 0x4725ea, +0x174: 7-12 10500-10505,
// 16-17 10509-10510, 26-27 10515-10516; none, on task 1, 10506); the tour
// guide's (ZTTourGuide::statusText 0x61b756: task 0x76 10400 with the
// habitat's name, speaking - task 0x77 or its bSpeak set, 0x61b964 - 10401,
// else 10402)
constexpr int kMonitoring = 10304, kGuideMonitoring = 10402, kMaintMonitoring = 10506;
constexpr float kStatusMs = 1000.0f; // [AI] mStatusPeriod: a unit's status tick (as Guests')
constexpr int kGoingToExhibit = 10400, kSpeaking = 10401; // a tour guide's
constexpr int kGoingToCan = 10502, kEmptyingCan = 10503;
constexpr int kGoingToCleanUp = 10504, kCleaningUp = 10505; // (dung outside exhibits)
constexpr int kGoingToFix = 10509, kFixing = 10510;
constexpr int kGoingToFilter = 10515, kServicing = 10516;
constexpr int kWaitingAtBase = 10316;
constexpr int kPlacingFood = 10300, kRaking = 10302, kGoingTo = 10303,
              kExiting = 10306, kCleaningTank = 10327;
constexpr int kGoingToSweep = 10500, kSweeping = 10501;
constexpr float kMurkyPurity = 60.0f; // tanks.cfg murkyWaterPurity: cleaned below it

// ours: an .ani path without its extension
std::string stripAni(std::string p) {
  p = Utils::string_to_lower(p);
  size_t dot = p.rfind(".ani");
  return dot == std::string::npos ? p : p.substr(0, dot);
}
} // namespace

// ----------------------------------------------------------------------------
// Types
// ----------------------------------------------------------------------------
// ours: reads each staff type's .ai (its cKeys, animations, sounds, the DRT
// base's slot) into our tables, its Kind standing for the type's class -
// the original makes each type's object through that class's create (class
// slot 0x5c: ZTStaffType 0x6112d9, ZTKeeperType 0x457b4e, ZTMaintType
// 0x4c5ead, ZTTourGuideType 0x59b550, ZTHelicopterType 0x4e5725, whose class
// name 0x4aa6a8 and isKindOfName 0x4aa866 tell it apart)
void Staff::loadTypes(ResourceManager *rm) {
  this->rm = rm;
  this->viewing.setResources(rm);
  if (!this->typeList.empty() || !rm)
    return;
  // (ztunit.ai: every unit's - zoo.exe reads it into 0x6394f4 at 0x4603a6)
  if (IniReader *unit = rm->getIniReader("ztunit.ai")) {
    this->predatorRadius = unit->getInt("characteristics/integers", "cpredatorradius", this->predatorRadius);
    delete unit;
  }
  if (rm->hasResource("ui/select/selsmall/selsmall.ani"))
    this->selectArrow = rm->getAnimation("ui/select/selsmall/selsmall");
  ItemCatalog &catalog = ItemCatalog::get();
  catalog.load(rm);
  for (const CatalogItem &item : catalog.all()) {
      if (item.registrySection != "staff") continue;
      IniReader *ai = rm->getIniReader(item.file);
      if (!ai)
        continue;
      Type t;
      t.file = Utils::string_to_lower(item.file);
      size_t slash = t.file.find_last_of('/'), dot = t.file.find_last_of('.');
      t.key = t.file.substr(slash + 1, dot - slash - 1);
      std::string type = Utils::string_to_lower(ai->get("global", "type", t.key));
      t.kind = type == "maint"        ? Kind::Maint
               : type == "tour"       ? Kind::Guide
               : type == "scient"     ? Kind::Scientist
               : type == "trainer"    ? Kind::Trainer
               : type == "helicopter" ? Kind::Helicopter
                                      : Kind::Keeper;
      std::string ints = t.kind == Kind::Helicopter ? "characteristics/integers"
                                                    : "m/characteristics/integers";
      t.nameId = ai->getInt(ints, "cnameid", 0);
      t.salary = ai->getInt(ints, "cpurchasecost", ai->getInt(ints, "cmonthlycost", 0));
      t.wage = ai->getInt(ints, "cmonthlycost", ai->getInt(ints, "cpurchasecost", 0));
      t.dutiesText = ai->getInt(ints, "cdutiestextid", 0);
      t.slowRate = ai->getInt(ints, "cslowrate", 33);
      t.fastRate = ai->getInt(ints, "cfastrate", 66);
      // (zoo.exe 0x435c42: whole 64ths of a tile an update - #152)
      t.speed = Original::unitTilesPerSecond(t.slowRate);
      // (ZTMaintType::loadConfig 0x4c3d44: cCleanTrashRadius, cFixFenceModifier
      // and cClearInvalidListInterval - no shipped maint.ai sets the first two;
      // training raises them)
      if (t.kind == Kind::Maint) {
        t.clearInvalidMs = ai->getInt(ints, "cclearinvalidlistinterval", t.clearInvalidMs);
        t.cleanTrashRadius = ai->getInt(ints, "ccleantrashradius", t.cleanTrashRadius);
        t.fixFenceModifier = ai->getInt(ints, "cfixfencemodifier", t.fixFenceModifier);
      }
      // (ZTKeeperType::loadConfig 0x4c34ae: cCleanTankPct +0x204 and
      // cCleanTankThreshold +0x208, over setDefaults 0x4c32bd's 10 and 100 -
      // no shipped file sets them)
      t.cleanTankPct = ai->getInt(ints, "ccleantankpct", t.cleanTankPct);
      t.cleanTankThreshold = ai->getInt(ints, "ccleantankthreshold", t.cleanTankThreshold);
      for (const std::string &d : ai->getList(ints, "cdirt"))
        t.dirt.push_back(std::atoi(d.c_str()));
      t.chaseCheck = std::max(1, ai->getInt(ints, "cchasecheck", t.chaseCheck));
      t.underwater = ai->getInt(ints, "cunderwater", 0) != 0;
      for (const std::string &line : ai->getList("behaviorset\\brunfrompredator", "f")) {
        std::string fn = Utils::string_to_lower(line);
        fn.erase(std::remove_if(fn.begin(), fn.end(), [](unsigned char ch) { return std::isspace(ch); }), fn.end());
        if (fn.rfind("fmove(", 0) == 0) {
          std::vector<std::string> args;
          std::string a;
          for (char ch : fn.substr(6))
            if (ch == ',' || ch == ')') { args.push_back(a); a.clear(); } else a += ch;
          if (args.size() >= 4) {
            if (!args[2].empty()) t.fleeAnim = args[2];
            if (std::atoi(args[3].c_str()) > 0) t.fleeRate = std::atoi(args[3].c_str());
          }
        }
      }
      if (t.kind == Kind::Guide) {
        // (ZTTourGuideType::loadConfig 0x4c406f: these six keys; over
        // setDefaults 0x4c3ee2's cInformGuestTime 5 and cTourGuideBonus 5)
        t.tourBonus = ai->getInt(ints, "ctourguidebonus", 5);
        t.followChance = ai->getInt(ints, "cfollowchance", 60);
        t.maxGroup = ai->getInt(ints, "cmaxgroupsize", 15);
        t.crowdCheck = std::max(1, ai->getInt(ints, "ccrowdcheck", 5));
        t.crowdRadius = ai->getInt(ints, "ccrowdradius", 1);
        t.informTime = ai->getInt(ints, "cinformguesttime", 5);
      }
      // Its animations: "walk = walk" (staff/<type>/<sub>/walk/walk) or a
      // whole path
      for (int sex = 0; sex < 2; sex++) {
        const char *sub = sex == 0 ? "m" : "f";
        std::string section = t.kind == Kind::Helicopter ? "animations"
                                                         : std::string(sub) + "/animations";
        for (const auto &a : ai->getSection(section)) {
          std::string v = Utils::string_to_lower(a.second);
          if (v.empty())
            continue;
          std::string path = v.find('/') != std::string::npos
                                 ? stripAni(v)
                                 : "staff/" + type + "/" + sub + "/" + v + "/" + v;
          t.anims[sex][Utils::string_to_lower(a.first)] = path;
        }
        std::string strs = std::string(sub) + "/characteristics/strings";
        t.listImage[sex] = Utils::string_to_lower(ai->get(strs, "clistimagename"));
        t.infoImage[sex] = Utils::string_to_lower(ai->get(strs, "cinfoimagename"));
      }
      if (t.listImage[1].empty())
        t.listImage[1] = t.listImage[0];
      // Men and women: whichever has its walking art (the subtype lists,
      // bare "m" / "f" lines, aren't key = value)
      for (int sex = 0; sex < 2; sex++) {
        auto w = t.anims[sex].find(t.kind == Kind::Helicopter ? "idle" : "walk");
        t.sexes[sex] = w != t.anims[sex].end() && rm->hasResource(w->second + ".ani");
      }
      if ((!t.sexes[0] && !t.sexes[1]) || t.kind == Kind::Helicopter) {
        t.sexes[0] = true;
        t.sexes[1] = false;
      }
      // Colour replacement: hair and skin palettes swapped in
      t.fullPal = Utils::string_to_lower(ai->get("cr_color", "fullpal"));
      t.hair = ai->getList("cr_hair", "pal");
      for (const auto &kv : ai->getSection("sounds")) {
        std::string v = kv.second;
        size_t semi = v.find(';');
        t.sounds[Utils::string_to_lower(kv.first)] = {
            Utils::string_to_lower(v.substr(0, semi)),
            semi == std::string::npos ? 0 : std::atoi(v.substr(semi + 1).c_str())};
      }
      t.skin = ai->getList("cr_skin", "pal");
      // Its behaviour sets: the work animations it plays (fPlay,
      // fPlayWithSound, fPlayReverse; fFaceTowardTarget is ours' face());
      // a set with any other step isn't taken, ours plays its own then
      for (const std::string &section : ai->getSections()) {
        if (section.rfind("behaviorset\\", 0) != 0)
          continue;
        std::vector<Type::Step> steps;
        bool known = true;
        for (const std::string &line : ai->getList(section, "f")) {
          std::string fn = Utils::string_to_lower(line);
          fn.erase(std::remove_if(fn.begin(), fn.end(), [](unsigned char c) { return std::isspace(c); }), fn.end());
          const size_t open = fn.find('('), close = fn.find(')');
          if (open == std::string::npos || close == std::string::npos || close < open) {
            known = false;
            break;
          }
          const std::string name = fn.substr(0, open), args = fn.substr(open + 1, close - open - 1);
          const size_t comma = args.find(',');
          Type::Step step;
          step.anim = args.substr(0, comma);
          if (name == "ffacetowardtarget")
            continue;
          if (name == "fplaywithsound" && comma != std::string::npos)
            step.sound = args.substr(comma + 1);
          else if (name == "fplayreverse")
            step.reverse = true;
          else if (name != "fplay") {
            known = false;
            break;
          }
          steps.push_back(step);
        }
        if (known && !steps.empty())
          t.sets[section.substr(section.find('\\') + 1)] = steps;
      }
      // cWeaponRange (keeper.ai, scient.ai, trainer.ai, helicopt.ai: 5) -
      // the keeper type's +0x1ec, zoo.exe 0x50cffd
      t.weaponRange = ai->getInt(ints, "cweaponrange", static_cast<int>(t.weaponRange));
      if (t.kind == Kind::Helicopter) {
        t.weaponRange = ai->getInt(ints, "cweaponrange", 5);
        // cMinHeight / cMaxHeight (its type's +0x114 / +0x118, ZTHelicopter
        // slots 0x1d0 / 0x1d4: 0x48c2cc, 0x48c2f7) - the height it keeps
        // over the ground in level flight (playAscendDescend 0x48c0c6)
        t.minHeight = ai->getInt(ints, "cminheight", t.minHeight);
        t.maxHeight = ai->getInt(ints, "cmaxheight", t.maxHeight);
        // The rates of its flights: each set's fFly(<anim>, <rate>) (fFly
        // 0x499886 -> 0x499815, the rate through class slot 0x1c0 to
        // moveAccumulate 0x435c42, as a walker's)
        auto flyRate = [&](const char *set, int fallback) {
          for (const std::string &f : ai->getList(std::string("behaviorset\\") + set, "f")) {
            const std::string fn = Utils::string_to_lower(f);
            const size_t open = fn.find("ffly("), comma = fn.find(',', open), close = fn.find(')', comma);
            if (open != std::string::npos && comma != std::string::npos && close != std::string::npos)
              return std::atoi(fn.substr(comma + 1, close - comma - 1).c_str());
          }
          return fallback;
        };
        t.ascendRate = flyRate("bascend", t.ascendRate);
        t.descendRate = flyRate("bdescend", t.descendRate);
        t.flyRate = flyRate("bfly", t.flyRate);
        t.chaseRate = flyRate("bchaseanimal", t.chaseRate);
        // (ZTHelicopterType::loadConfig 0x4c42ff: cLoopSoundName +500 and
        // cLoopSoundAtten +0x1f8, helicopt.ai's 1200)
        t.rotorSound = Utils::string_to_lower(ai->get("characteristics/strings", "cloopsoundname"));
        t.rotorAtten = ai->getInt(ints, "cloopsoundatten", 0);
        auto shots = ai->getList("sounds", "fire_weapon");
        t.fireSound = shots.empty() ? "staff/heligun1.wav" : Utils::string_to_lower(shots.front());
        t.listImage[0] = Utils::string_to_lower(ai->get("characteristics/strings", "clistimagename"));
        t.infoImage[0] = Utils::string_to_lower(ai->get("characteristics/strings", "cinfoimagename"));
      }
      delete ai;
      if (t.kind == Kind::Helicopter) {
        t.baseFile = "scenery/building/helibase.ai";
        if (IniReader *b = rm->getIniReader(t.baseFile)) {
          t.footX = std::max(1, (b->getInt("characteristics/integers", "cfootprintx", 10) + 1) / 2);
          t.footY = std::max(1, (b->getInt("characteristics/integers", "cfootprinty", 8) + 1) / 2);
          const auto slot = b->getList("slot0", "slotpos");
          // Same facing-0 slot transform as Guests::doorTiles: across=-X, forward=-Y.
          if (slot.size() >= 2) { t.padX = -std::stof(slot[0]) / 64; t.padY = -std::stof(slot[1]) / 64; }
          t.padZ = b->getInt("slot0", "slotposz", 80) / 16.0f;
          // (its exitpos / exitposz: the slot's +0x24, ZTBuildingType::
          // loadConfig 0x4c0b4d, read by 0x4a7007 for take-off and the way
          // home - Task69 0x48d3ed, Task67 0x5a45f0)
          const auto exit = b->getList("slot0", "exitpos");
          if (exit.size() >= 2) { t.exitX = -std::stof(exit[0]) / 64; t.exitY = -std::stof(exit[1]) / 64; }
          t.exitZ = b->getInt("slot0", "exitposz", 160) / 16.0f;
          t.padFacing = b->getInt("slot0", "facing", 7);
          delete b;
        }
        if (rm->hasResource("objects/helibase/idle/idle.ani"))
          t.base = rm->getAnimation("objects/helibase/idle/idle");
        if (rm->hasResource("objects/helibase/used/used.ani"))
          t.baseUsed = rm->getAnimation("objects/helibase/used/used");
      }
      this->typeList.push_back(t);
  }
  SDL_Log("[Staff] %zu staff types", this->typeList.size());
}

// ours: a type by its .ai file
int Staff::typeOfFile(const std::string &file) const {
  std::string f = Utils::string_to_lower(file);
  for (size_t i = 0; i < this->typeList.size(); i++)
    if (this->typeList[i].file == f || (!this->typeList[i].baseFile.empty() &&
                                        this->typeList[i].baseFile == f))
      return static_cast<int>(i);
  return -1;
}

// ours: everyone gone (a new zoo)
void Staff::clear() {
  for (auto &m : this->list) { releaseCatch(m); Sound::get().stop(m.rotorChannel); }
  this->list.clear();
  this->counts.clear();
  // (a zoo loaded afresh: no exhibit visited yet - the last zoo's visits
  // kept a reloaded tank "not due" for the service interval)
  this->lastVisit.clear();
  this->notices.clear();
  this->selected = this->hovered = this->carried = -1;
  // The work list emptied (maintWorkListClear 0x5b11a2, from a load or a
  // new zoo: 0x501303, 0x4c6a77); the zoo's work is posted again at the next
  // look (Staff::noticeWork, as worldAfterLoad's reposts)
  this->work.clear();
  this->postedItems.clear();
  this->binsFull.clear();
  this->fencesWorn.clear();
  this->filterPosted.clear();
}

// zoo.exe maintWorkListBuild 0x434e30: an entry {job, tile, no holder,
// flag} for a message to maintenance; flagged ones to the list's front
// (0x4e81d2), the rest to its back
void Staff::postWork(Job job, int x, int y, int item, Fences::Edge edge, bool flagged) {
  WorkEntry w;
  w.id = this->nextWorkId++;
  w.job = job;
  w.x = x;
  w.y = y;
  w.item = item;
  w.edge = edge;
  w.flagged = flagged;
  if (flagged)
    this->work.insert(this->work.begin(), w);
  else
    this->work.push_back(w);
}

Staff::WorkEntry *Staff::workEntry(int id) {
  for (WorkEntry &w : this->work)
    if (w.id == id)
      return &w;
  return nullptr;
}

// zoo.exe ZTMaintenanceWorker::maintTakeEntry 0x4a97c0: the entry it held
// (+0x2c0), if any, taken off the list and freed - done or not - then this
// one held (Task78 0x4a1e8d: the entry's +8 set to it)
void Staff::takeEntry(Member &m, int id) {
  if (m.workEntry >= 0 && m.workEntry != id) {
    const int old = m.workEntry;
    this->work.erase(std::remove_if(this->work.begin(), this->work.end(),
                                    [&](const WorkEntry &w) { return w.id == old; }),
                     this->work.end());
  }
  m.workEntry = id;
  if (WorkEntry *w = this->workEntry(id))
    w->holder = m.id;
}

// zoo.exe 0x4a97e5 (maintTakeEntry(0)): the job done, its entry freed
void Staff::finishEntry(Member &m) {
  if (m.workEntry < 0)
    return;
  const int old = m.workEntry;
  this->work.erase(std::remove_if(this->work.begin(), this->work.end(),
                                  [&](const WorkEntry &w) { return w.id == old; }),
                   this->work.end());
  m.workEntry = -1;
}

// ours: the filter whose footprint's top-left tile this is (an entry keeps
// its tile, as the original's +4)
int Staff::filterAt(const Fences &fences, int x, int y) const {
  const auto &fl = fences.filters();
  for (size_t i = 0; i < fl.size(); i++)
    if (fl[i].x == x && fl[i].y == y)
      return static_cast<int>(i);
  return -1;
}

// Nothing to do there any more (the checks of the jobs that free it: the
// dung gone, Job11 0x4a3f13; the can emptied, Job10 0x481981; the fence
// mended or gone, Job16 0x59a497)
// (ours: litter gone and a filter serviced or gone the same way)
bool Staff::workGone(const WorkEntry &e, const Fences &fences) const {
  switch (e.job) {
  case Job::Litter:
  case Job::Dung:
    return !this->items || !this->items->item(e.item);
  case Job::Trash: {
    if (!this->objects)
      return true;
    for (const PlacedObjects::Object &o : this->objects->objects())
      if (o.id == e.item)
        return !(o.trashCan && o.fill > 12);
    return true;
  }
  case Job::Fence:
    return !fences.worn(e.edge);
  case Job::Filter: {
    const int i = filterAt(fences, e.x, e.y);
    if (i < 0)
      return true;
    const Fences::Filter &f = fences.filters()[i];
    return f.health > fences.filterType(f.file).decayedHealth;
  }
  default:
    return true;
  }
}

// What's been posted to maintenance since the last look, onto the work list
// (0x434e30), each once, as it appears:
// - litter (10031): a guest's dropped (guestDropLitter 0x52da40) or a full
//   can's overflow (0x50c212);
// - dung on the zoo's grounds (10032, animalDropDung 0x4a8f89);
// - a trash can whose fill reaches 13 (10030: a guest's trash in, 0x59d758,
//   posts on +0x19c == 0xd - once, until it's emptied and fills again);
// - a fence piece worn down to its cDecayedLife (10033, ZTFence::update
//   0x434b1d) - for each tile beside it it can be stood on, its own tile's
//   flagged (read in the running game 2026-11-11: pairs a tile apart, the
//   owner's flagged);
// - a filter decayed to cDecayedHealth (10035) and broken, its health 0
//   (10036, flagged) (filterDecayTick 0x615c29).
// After clear() (a load, a new zoo) all of the zoo's work is posted afresh
// (worldAfterLoad 0x4534e6 -> 0x593f2f: each object's slot 0x70 - litter
// and dung 0x593f72, cans over 12 0x593fd3, worn fences 0x594060, filters
// 0x5afba6)
// (ours: what's new is noticed at the next look or tick rather than posted
// where it happens; on one tick, fences, filters, cans, dung, then litter)
void Staff::noticeWork(const WorldMap &map, const Fences &fences) {
  // Fences
  for (auto it = this->fencesWorn.begin(); it != this->fencesWorn.end();)
    it = fences.worn(*it) ? std::next(it) : this->fencesWorn.erase(it);
  for (const auto &kv : fences.pieces()) {
    const Fences::Edge &e = kv.first;
    if (!fences.worn(e) || this->fencesWorn.count(e))
      continue;
    this->fencesWorn.insert(e);
    int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
    for (auto [tx, ty] : {std::make_pair(ax, ay), std::make_pair(e.x, e.y)})
      if (walkable(tx, ty, map, fences))
        postWork(Job::Fence, tx, ty, -1, e, tx == kv.second.ownerX && ty == kv.second.ownerY);
  }
  // Filters: decayed (10035), broken (10036, flagged)
  {
    std::map<std::pair<int, int>, int> now;
    for (const Fences::Filter &f : fences.filters()) {
      const Fences::FilterType &k = fences.filterType(f.file);
      const int state = f.health <= 0 ? 2 : f.health <= k.decayedHealth ? 1 : 0;
      const std::pair<int, int> at{f.x, f.y};
      auto was = this->filterPosted.find(at);
      const int before = was == this->filterPosted.end() ? 0 : was->second;
      if (state == 1 && before == 0)
        postWork(Job::Filter, f.x, f.y, -1, {false, -1, -1}, false);
      else if (state == 2 && before < 2)
        postWork(Job::Filter, f.x, f.y, -1, {false, -1, -1}, true);
      now[at] = state;
    }
    this->filterPosted.swap(now);
  }
  // Trash cans: reaching 13
  if (this->objects) {
    std::set<int> full;
    for (const PlacedObjects::Object &o : this->objects->objects())
      if (o.trashCan && o.fill > 12) {
        full.insert(o.id);
        if (!this->binsFull.count(o.id))
          postWork(Job::Trash, static_cast<int>(std::floor(o.x)), static_cast<int>(std::floor(o.y)), o.id,
                   {false, -1, -1}, false);
      }
    this->binsFull.swap(full);
  }
  // Dung on the grounds, then litter
  if (this->items) {
    std::set<int> seen;
    for (const ZooItems::Item &it : this->items->items()) {
      if (it.kind == ZooItems::Kind::Food)
        continue;
      seen.insert(it.id);
      if (this->postedItems.count(it.id))
        continue;
      if (it.kind == ZooItems::Kind::Dung && it.exhibit < 0)
        postWork(Job::Dung, static_cast<int>(std::floor(it.x)), static_cast<int>(std::floor(it.y)), it.id,
                 {false, -1, -1}, false);
    }
    for (const ZooItems::Item &it : this->items->items())
      if (it.kind == ZooItems::Kind::Litter && it.exhibit < 0 && !this->postedItems.count(it.id))
        postWork(Job::Litter, static_cast<int>(std::floor(it.x)), static_cast<int>(std::floor(it.y)), it.id,
                 {false, -1, -1}, false);
    this->postedItems.swap(seen);
  }
}

// ours (checks): the list emptied and the zoo's work posted again, as a
// load does (0x5b11a2, then worldAfterLoad's reposts)
void Staff::rebuildWorkList(const WorldMap &map, const Fences &fences) {
  for (Member &m : this->list)
    m.workEntry = -1;
  this->work.clear();
  this->postedItems.clear();
  this->binsFull.clear();
  this->fencesWorn.clear();
  this->filterPosted.clear();
  noticeWork(map, fences);
}

// ours: one by its id
Staff::Member *Staff::member(int id) {
  for (Member &m : this->list)
    if (m.id == id)
      return &m;
  return nullptr;
}

const Staff::Member *Staff::member(int id) const {
  return const_cast<Staff *>(this)->member(id);
}

int Staff::monthlyWages() const {
  int total = 0;
  for (const Member &m : this->list)
    total += this->typeList[m.type].wage;
  return total;
}

// ----------------------------------------------------------------------------
// Art: each member's animations in its own colours
// ----------------------------------------------------------------------------
// In a tank (trainer.ai): under the water walking is its swim (bWalksub
// subswim) and standing subidle; at the surface surfswim (bWalksurf) and
// surfidle
std::string Staff::inWater(const Member &m, const std::string &name) const {
  if (m.water == 3) {
    if (name == "walk" || name == "run")
      return "subswim";
    if (name == "idle" || name == "stand")
      return "subidle";
  } else if (m.water == 2) {
    if (name == "walk" || name == "run")
      return "surfswim";
    if (name == "idle" || name == "stand")
      return "surfidle";
  }
  return name;
}

// ours: the art an animation draws with, its colours swapped in (cached)
Animation *Staff::art(const Member &m, const std::string &asked) const {
  const std::string name = inWater(m, asked);
  const Type &t = this->typeList[m.type];
  int sex = m.female && t.sexes[1] ? 1 : 0;
  auto it = t.anims[sex].find(name);
  if (it == t.anims[sex].end()) {
    it = t.anims[sex].find("idle");
    if (it == t.anims[sex].end())
      return nullptr;
  }
  const std::string &path = it->second;
  bool recolor = !t.fullPal.empty() && !t.hair.empty() && !t.skin.empty();
  std::string key = path + (recolor ? "#" + std::to_string(m.hair) + "_" + std::to_string(m.skin) : "");
  auto c = this->artCache.find(key);
  if (c != this->artCache.end())
    return c->second;
  Animation *a = nullptr;
  if (this->rm && this->rm->hasResource(path + ".ani")) {
    if (recolor) {
      // The full palette with this member's hair (entries 224-239) and skin
      // (240-255) from their 16-colour palettes (whose entry 0 is unused)
      PalletManager *pm = this->rm->getPalletManager();
      std::string full = t.fullPal, hair = t.hair[m.hair % t.hair.size()],
                  skin = t.skin[m.skin % t.skin.size()];
      Pallet *base = pm->getPallet(full);
      Pallet *hp = pm->getPallet(hair);
      Pallet *sp = pm->getPallet(skin);
      if (base) {
        Pallet variant = *base;
        for (int i = 0; i < 16; i++) {
          if (hp && hp->color_count > static_cast<uint32_t>(i + 1))
            variant.colors[224 + i] = hp->colors[i + 1];
          if (sp && sp->color_count > static_cast<uint32_t>(i + 1))
            variant.colors[240 + i] = sp->colors[i + 1];
        }
        std::string name = full + "#" + std::to_string(m.hair) + "_" + std::to_string(m.skin);
        pm->addPallet(name, variant);
        PalletManager::setOverride(full, name);
      }
      a = this->rm->getAnimation(path);
      PalletManager::clearOverrides();
    } else {
      a = this->rm->getAnimation(path);
    }
  }
  this->artCache[key] = a;
  return a;
}

// ours: the art shown at the cursor while hiring
Animation *Staff::preview(int type) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return nullptr;
  Member m;
  m.type = type;
  return this->art(m, "idle");
}

// ours: how long one play of an animation takes
float Staff::animSeconds(const Member &m, const std::string &anim) const {
  Animation *a = this->art(m, anim);
  if (!a)
    return 1.0f;
  float ms = a->frameTimeMs() ? static_cast<float>(a->frameTimeMs()) : 100.0f;
  return std::max(1, a->frameCount()) * ms / 1000.0f;
}

// ----------------------------------------------------------------------------
// Ground: where staff can walk
// ----------------------------------------------------------------------------
// ours: the ground's height under a point
float Staff::groundAt(const WorldMap &map, float x, float y) const {
  const MapTile *t = map.getTile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  if (!t)
    return 0;
  float fx = x - std::floor(x), fy = y - std::floor(y);
  float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
  float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
  return top * (1 - fy) + bottom * fy;
}

// Zoo ground: inside the main wall, not water, not in an exhibit, not
// under a building or a tank filter
bool Staff::walkable(int x, int y, const WorldMap &map, const Fences &fences, int access) const {
  int ex = fences.exhibitAt(x, y);
  // A visiting marine specialist swims above floor scenery. It must not
  // be trapped beside the ladder by the plants it is meant to tend.
  if(this->index && this->index->blocked(x,y)) {
    const auto *pool=fences.exhibit(ex);
    if(!(ex==access && pool && pool->tank && pool->water>0 &&
         this->index->at(x,y)==WorldIndex::What::Object))return false;
  }
  if (ex >= 0) {
    // In an exhibit only on a visit to it (a tank: all of it, swimming)
    if (ex != access)
      return false;
    if (const Fences::Exhibit *e = fences.exhibit(ex))
      if (e->tank)
        return true;
    const MapTile *t = map.getTile(x, y);
    return t && t->terrainType != 9 && t->terrainType != 10;
  }
  if (fences.tileFit(x, y, map) != Fences::Fit::Ok)
    return false;
  for (const Fences::Filter &f : fences.filters()) {
    const Fences::FilterType &k = fences.filterType(f.file);
    if (x >= f.x && x < f.x + k.footprintX && y >= f.y && y < f.y + k.footprintY)
      return false;
  }
  // DRT bases
  for (const Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    if (t.kind != Kind::Helicopter || m.id == this->carried)
      continue;
    int x0 = static_cast<int>(std::floor(m.x - t.footX / 2.0f));
    int y0 = static_cast<int>(std::floor(m.y - t.footY / 2.0f));
    if (x >= x0 && x < x0 + t.footX && y >= y0 && y < y0 + t.footY)
      return false;
  }
  return true;
}

// From a tile to its neighbour: no fence on the edge between, no cliff
bool Staff::canStep(int x, int y, int nx, int ny, const WorldMap &map, const Fences &fences,
                    int access) const {
  Fences::Edge e = nx != x ? Fences::Edge{false, std::max(x, nx), y}
                           : Fences::Edge{true, x, std::max(y, ny)};
  // Fences stop them; an exhibit's gate lets in its keeper on a visit
  if (const Fences::Piece *p = fences.at(e))
    if (!(p->gate && access >= 0 && fences.exhibitOf(e) == access))
      return false;
  const MapTile *a = map.getTile(x, y), *b = map.getTile(nx, ny);
  if (!a || !b)
    return false;
  // The corners they share must meet
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
  // Where the shared corners disagree at all the map has a cliff there
  // (the renderer draws a wall): nobody walks up or down it
  return a0 == b0 && a1 == b1;
}

Fences::Fit Staff::canPlace(float x, float y, const WorldMap &map, const Fences &fences) const {
  int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  if (!walkable(tx, ty, map, fences))
    return Fences::Fit::Outside;
  return Fences::Fit::Ok;
}

std::pair<float, float> Staff::placementPoint(int type, float x, float y) const {
  if (type >= 0 && type < static_cast<int>(typeList.size()) && typeList[type].kind == Kind::Helicopter) {
    const auto &t = typeList[type];
    return {std::floor(x - t.footX * 0.5f) + t.footX * 0.5f,
            std::floor(y - t.footY * 0.5f) + t.footY * 0.5f};
  }
  return {x, y};
}

Fences::Fit Staff::canPlaceType(int type, float x, float y, const WorldMap &map,
                                const Fences &fences) const {
  if (type >= 0 && type < static_cast<int>(this->typeList.size()) &&
      this->typeList[type].kind == Kind::Helicopter) {
    // Preview, collision and placed anchor all use the same grid-aligned point.
    const auto point = placementPoint(type, x, y); x = point.first; y = point.second;
    const Type &t = this->typeList[type];
    int x0 = static_cast<int>(std::floor(x - t.footX / 2.0f));
    int y0 = static_cast<int>(std::floor(y - t.footY / 2.0f));
    const MapTile *first = map.getTile(x0, y0);
    if (!first) return Fences::Fit::Outside;
    const int height = first->cornerHeight[0];
    for (int ty = y0; ty < y0 + t.footY; ty++)
      for (int tx = x0; tx < x0 + t.footX; tx++) {
        if (!walkable(tx, ty, map, fences)) return Fences::Fit::Outside;
        if (map.isPath(tx, ty)) return Fences::Fit::InTheWay;
        const auto *tile = map.getTile(tx, ty);
        for (int corner = 0; corner < 4; corner++) if (tile->cornerHeight[corner] != height) return Fences::Fit::Outside;
      }
    for (const Member &m : this->list)
      if (m.id != this->carried && m.x >= x0 && m.x < x0 + t.footX && m.y >= y0 && m.y < y0 + t.footY)
        return Fences::Fit::InTheWay;
    return Fences::Fit::Ok;
  }
  return this->canPlaceMember(x, y, map, fences);
}

Fences::Fit Staff::canPlaceMember(float x, float y, const WorldMap &map,
                                  const Fences &fences) const {
  int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  if (!walkable(tx, ty, map, fences))
    return Fences::Fit::Outside;
  // Not on top of another
  for (const Member &m : this->list)
    if (std::fabs(m.x - x) < 0.35f && std::fabs(m.y - y) < 0.35f)
      return Fences::Fit::Outside;
  return Fences::Fit::Ok;
}

// A* over the tiles (8 ways, no corner cutting) that keeps to paths:
// open ground costs kOffPath times a path tile, so a route takes the path
// network to the point nearest its goal and leaves it only for the last
// stretch. Straightened only within a run of path or a run of open ground
// (never cutting a corner off a path).
constexpr float kOffPath = 6.0f;
bool Staff::findPath(Member &m, int gx, int gy, const WorldMap &map, const Fences &fences,
                     float endX, float endY) {
  if (Features::elevatedPaths && this->walkways && !this->walkways->all().empty())
    return findLayeredPath(m, gx, gy, map, fences, endX, endY);
  m.pathLayers.clear();
  m.layer = 0;
  // Inside an exhibit it may always walk out of it (through its gate)
  int access = m.access;
  if (access < 0)
    access = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
  auto pass = [&](int x, int y) { return walkable(x, y, map, fences, access); };
  auto step = [&](int x, int y, int nx, int ny) {
    return canStep(x, y, nx, ny, map, fences, access);
  };
  // (staffpaths off: every tile alike, as the original)
  auto onPathTile = [&](int x, int y) { return !Features::staffPaths || map.isPath(x, y); };
  auto cost = [&](int x, int y) { return onPathTile(x, y) ? 1.0f : kOffPath; };
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  std::vector<std::pair<int, int>> tiles;
  if (!Pathfinder::find(map.getWidth(), map.getHeight(), sx, sy, gx, gy, pass, step, tiles, 40000,
                        cost)) {
    if (this->logRoutes)
      SDL_Log("[Path] %s: %d,%d -> %d,%d  NO ROUTE  (%s)", m.name.c_str(), sx, sy, gx, gy,
              dutyText(m).c_str());
    return false;
  }
  // Waypoints at tile middles (a little loose off the paths), in runs of
  // path / open ground, each run straightened on its own kind of tile
  std::uniform_real_distribution<float> jitter(0.4f, 0.6f);
  std::vector<std::pair<float, float>> path;
  std::vector<std::pair<float, float>> run{{m.x, m.y}};
  bool runOnPath = onPathTile(sx, sy);
  auto flush = [&] {
    bool onPath = runOnPath;
    auto same = [&](int x, int y) { return pass(x, y) && onPathTile(x, y) == onPath; };
    if (Features::smoothRoutes)
      Pathfinder::smooth(run, same, step);
    for (size_t k = 1; k < run.size(); k++)
      path.push_back(run[k]);
  };
  for (size_t i = 1; i < tiles.size(); i++) {
    auto [x, y] = tiles[i];
    bool onPath = onPathTile(x, y);
    std::pair<float, float> p = i + 1 == tiles.size()
                                    ? std::make_pair(endX, endY)
                                    : onPath ? std::make_pair(x + 0.5f, y + 0.5f)
                                             : std::make_pair(x + jitter(this->rng), y + jitter(this->rng));
    if (onPath != runOnPath) {
      flush();
      run = {run.back()};
      runOnPath = onPath;
    }
    run.push_back(p);
  }
  flush();
  if (path.empty())
    path.push_back({endX, endY}); // (already on the goal tile)
  m.path = path;
  m.pathAt = 0;
  m.routeTiles = tiles;
  m.goalX = gx;
  m.goalY = gy;
  if (this->logRoutes) {
    int onPath = 0;
    for (auto [x, y] : tiles)
      onPath += map.isPath(x, y) ? 1 : 0;
    SDL_Log("[Path] %s: %d,%d -> %d,%d  %zu tiles (%d on paths), %zu legs  (%s)", m.name.c_str(), sx,
            sy, gx, gy, tiles.size(), onPath, m.path.size(), dutyText(m).c_str());
  }
  return true;
}

bool Staff::testRoute(int sx, int sy, int gx, int gy, const WorldMap &map, const Fences &fences,
                      std::vector<std::pair<int, int>> &out) const {
  int access = fences.exhibitAt(sx, sy);
  auto pass = [&](int x, int y) { return walkable(x, y, map, fences, access); };
  auto step = [&](int x, int y, int nx, int ny) { return canStep(x, y, nx, ny, map, fences, access); };
  auto cost = [&](int x, int y) { return !Features::staffPaths || map.isPath(x, y) ? 1.0f : kOffPath; };
  return Pathfinder::find(map.getWidth(), map.getHeight(), sx, sy, gx, gy, pass, step, out, 40000, cost);
}

// ours: the ZT1 Lab plays one of its animations
void Staff::labPlay(int id, const std::string &anim, int times) {
  if (Member *m = this->member(id)) {
    m->path.clear();
    m->pathAt = 0;
    play(*m, anim, std::max(1, times));
  }
}

// ours: the ZT1 Lab lists its animations
std::vector<std::string> Staff::labAnims(int id) const {
  std::vector<std::string> out;
  if (const Member *m = this->member(id))
    for (const auto &kv : this->typeList[m->type].anims[m->female ? 1 : 0])
      out.push_back(kv.first);
  return out;
}

// ours: the height it stands at (a deck, the ground)
float Staff::heightOf(const Member &m, const WorldMap &map) const {
  if (m.layer == 1 && this->walkways) {
    int tx = static_cast<int>(std::floor(m.x)), ty = static_cast<int>(std::floor(m.y));
    if (this->walkways->at(tx, ty))
      return this->walkways->heightAt(tx, ty, m.x - tx, m.y - ty);
  }
  // (in a tank, or climbing: over the ground, the tank's floor)
  return groundAt(map, m.x, m.y) + m.z;
}

// With walkways about: A* over two layers, the ground and the decks above
// it. Onto a deck where a stair's foot meets the ground at the same
// heights; deck to deck where their edges join; a deck crosses a fence
// only well above it. Decks count as path.
bool Staff::findLayeredPath(Member &m, int gx, int gy, const WorldMap &map, const Fences &fences,
                            float endX, float endY) {
  const int W = map.getWidth(), H = map.getHeight(), N = W * H;
  int access = m.access;
  if (access < 0)
    access = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
  auto pass = [&](int x, int y) { return walkable(x, y, map, fences, access); };
  auto stepOk = [&](int x, int y, int nx, int ny) {
    return canStep(x, y, nx, ny, map, fences, access);
  };
  auto onPathTile = [&](int x, int y) { return !Features::staffPaths || map.isPath(x, y); };
  const Walkways &ww = *this->walkways;
  auto groundEdge = [&](int x, int y, int dir, int &a, int &b) {
    const MapTile *t = map.getTile(x, y);
    if (!t)
      return false;
    int c0, c1;
    Walkways::edgeCorners(dir, c0, c1);
    a = t->cornerHeight[c0];
    b = t->cornerHeight[c1];
    return true;
  };
  auto next = [&](int node, std::vector<std::pair<int, float>> &out) {
    int layer = node / N, idx = node % N, x = idx % W, y = idx / W;
    if (layer == 0) {
      // Ground: as ever, eight ways
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          if (!dx && !dy)
            continue;
          int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= W || ny >= H)
            continue;
          bool goal = nx == gx && ny == gy;
          if (!goal && !pass(nx, ny))
            continue;
          float c = 1.0f;
          if (dx && dy) {
            if (!pass(nx, y) || !pass(x, ny) || !stepOk(x, y, nx, y) || !stepOk(nx, y, nx, ny) ||
                !stepOk(x, y, x, ny) || !stepOk(x, ny, nx, ny))
              continue;
            c = 1.41421356f;
          } else if (!stepOk(x, y, nx, ny)) {
            continue;
          }
          out.push_back({ny * W + nx, c * (onPathTile(nx, ny) ? 1.0f : kOffPath)});
        }
      // Up a stair whose foot meets this tile
      for (int dir = 0; dir < 4; dir++) {
        int dx, dy;
        Walkways::step(dir, dx, dy);
        int nx = x + dx, ny = y + dy, a, b, p, q;
        if (!ww.edgeHeights(nx, ny, (dir + 2) & 3, a, b) || !groundEdge(x, y, dir, p, q))
          continue;
        if (a == p && b == q && !fences.at(dir & 1 ? Fences::Edge{false, std::max(x, nx), y}
                                                    : Fences::Edge{true, x, std::max(y, ny)}))
          out.push_back({N + ny * W + nx, 1.0f});
      }
    } else {
      // On a deck: along joined decks, or off a stair's foot
      for (int dir = 0; dir < 4; dir++) {
        int dx, dy;
        Walkways::step(dir, dx, dy);
        int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H)
          continue;
        Fences::Edge e = dir & 1 ? Fences::Edge{false, std::max(x, nx), y}
                                 : Fences::Edge{true, x, std::max(y, ny)};
        int a, b;
        ww.edgeHeights(x, y, dir, a, b);
        if (ww.joins(x, y, dir)) {
          // Over a fence only well above it
          int p, q;
          groundEdge(x, y, dir, p, q);
          if (!fences.at(e) || std::min(a, b) - std::max(p, q) >= 3)
            out.push_back({N + ny * W + nx, 1.0f});
          continue;
        }
        int p, q;
        if (!ww.at(nx, ny) && groundEdge(nx, ny, (dir + 2) & 3, p, q) && a == p && b == q &&
            !fences.at(e) && (pass(nx, ny) || (nx == gx && ny == gy)))
          out.push_back({ny * W + nx, onPathTile(nx, ny) ? 1.0f : kOffPath});
      }
    }
  };
  auto guess = [&](int node) {
    int idx = node % N, x = idx % W, y = idx / W;
    float dx = static_cast<float>(std::abs(x - gx)), dy = static_cast<float>(std::abs(y - gy));
    return std::max(dx, dy) + 0.41421356f * std::min(dx, dy);
  };
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  if (sx < 0 || sy < 0 || sx >= W || sy >= H)
    return false;
  bool startOnDeck = m.layer == 1 && ww.at(sx, sy);
  int start = (startOnDeck ? N : 0) + sy * W + sx, goal = gy * W + gx;
  std::vector<int> nodes;
  if (!Pathfinder::findGraph(2 * N, start, goal, next, guess, nodes)) {
    if (this->logRoutes)
      SDL_Log("[Path] %s: %d,%d -> %d,%d  NO ROUTE  (%s)", m.name.c_str(), sx, sy, gx, gy,
              dutyText(m).c_str());
    return false;
  }
  // Waypoints at the tile middles, each with its layer; ground runs
  // straightened as before, deck runs left as laid
  m.path.clear();
  m.pathLayers.clear();
  m.routeTiles.clear();
  std::uniform_real_distribution<float> jitter(0.4f, 0.6f);
  int decks = 0;
  for (size_t i = 0; i < nodes.size(); i++) {
    int layer = nodes[i] / N, idx = nodes[i] % N, x = idx % W, y = idx / W;
    m.routeTiles.push_back({x, y});
    decks += layer;
    if (i == 0)
      continue;
    bool last = i + 1 == nodes.size();
    float px = last ? endX : (layer || map.isPath(x, y) ? x + 0.5f : x + jitter(this->rng));
    float py = last ? endY : (layer || map.isPath(x, y) ? y + 0.5f : y + jitter(this->rng));
    m.path.push_back({px, py});
    m.pathLayers.push_back(layer);
  }
  if (m.path.empty()) {
    m.path.push_back({endX, endY});
    m.pathLayers.push_back(0);
  }
  m.pathAt = 0;
  m.goalX = gx;
  m.goalY = gy;
  if (this->logRoutes)
    SDL_Log("[Path] %s: %d,%d -> %d,%d  %zu tiles (%d on walkways), %zu points  (%s)", m.name.c_str(),
            sx, sy, gx, gy, nodes.size(), decks, m.path.size(), dutyText(m).c_str());
  return true;
}

// ours: everyone into the world index each frame
void Staff::addToIndex(WorldIndex &index) const {
  for (const Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    if (m.id == this->carried) continue;
    if (t.kind == Kind::Helicopter) {
      // Its base stands in the way
      int x0 = static_cast<int>(std::floor(m.x - t.footX / 2.0f));
      int y0 = static_cast<int>(std::floor(m.y - t.footY / 2.0f));
      index.addBlock(x0, y0, t.footX, t.footY, WorldIndex::What::Base);
      continue;
    }
    index.addMover(WorldIndex::Kind::Staff, m.id, m.x, m.y);
  }
}

// zoo.exe 0x43d948 (unitWalkAbout; slot 0x310 0x49dafe calls it when the
// chooser finds nothing to do, after task 1): a tile on a ring round the
// tile it stands on, half widths rand % mRandomXStaff + 1 + BaseX across and
// rand % mRandomYStaff + 1 + BaseY down (2..10, OriginalConstants), one
// passable for it (class slot 0x168: ZTStaff 0x423ac2 - nothing standing on
// the tile is zoo ground (0x4146d4 -> 0x40fb8d: +0x83 & 0x40), no water; a
// tour guide's 0x61b639, on a path, keeps to paths - and so do the others,
// measured, below) picked at random and routed to; no route, its four
// neighbours from a random one on, not wrapping round (tileNeighbours4
// 0x4171a1), the first passable within the step-cost limit (0x41456c); none,
// it stays where it is.
// ours: passable is walkable() with the exhibit it stands in as its access,
// so a keeper whose duties are done walks about in it or out through its
// gate; the neighbours' order and canStep stand for 0x4171a1's and the step
// cost
bool Staff::walkAbout(Member &m, const WorldMap &map, const Fences &fences) {
  const int W = map.getWidth(), H = map.getHeight();
  const int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  const int here = fences.exhibitAt(sx, sy);
  const Fences::Exhibit *in = fences.exhibit(here);
  m.access = in && !in->tank ? here : -1;
  auto rnd = [&](int n) { return std::uniform_int_distribution<int>(0, n - 1)(this->rng); };
  const int rx = rnd(Original::aiWalkRandomStaff) + 1 + Original::aiWalkBase;
  const int ry = rnd(Original::aiWalkRandomStaff) + 1 + Original::aiWalkBase;
  // (measured: 2026-11-17, #243 - every staff member's walk about that
  // started on a path ended on one, 38 of 38 for zookeepers and maintenance
  // workers in two zoos with open grass beside the paths; one started on
  // grass ended on grass. 0x423ac2's own test passes grass, so the rule is
  // the tour guide's, 0x61b639, for them all)
  const bool pathOnly = map.isPath(sx, sy);
  auto passable = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H && walkable(x, y, map, fences, m.access) &&
           (!pathOnly || map.isPath(x, y));
  };
  std::vector<std::pair<int, int>> ring;
  for (int dx = -rx; dx <= rx; dx++)
    for (int dy = -ry; dy <= ry; dy++)
      if ((dx == rx || dx == -rx || dy == ry || dy == -ry) && passable(sx + dx, sy + dy))
        ring.push_back({sx + dx, sy + dy});
  ZT_STEP("0x43d948", "Walk about: a tile on a ring round it, at random", {"half width", rx},
          {"half height", ry}, {"tiles", static_cast<int>(ring.size())});
  if (!ring.empty()) {
    auto [gx, gy] = ring[static_cast<size_t>(rnd(static_cast<int>(ring.size())))];
    if (findPath(m, gx, gy, map, fences, gx + 0.5f, gy + 0.5f))
      return true;
  }
  const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = rnd(4); i < 4; i++) {
    const int nx = sx + d[i][0], ny = sy + d[i][1];
    if (passable(nx, ny) && canStep(sx, sy, nx, ny, map, fences, m.access) &&
        findPath(m, nx, ny, map, fences, nx + 0.5f, ny + 0.5f))
      return true;
  }
  return false;
}

// measured: 2026-11-17 (#243, the Lab's Play original, the unit read 20
// times a second): staff with nothing to do don't stand between walks on a
// timer of their own. The keeper's chooser (zoo.exe 0x423d04) runs every
// frame of the unit's update (0x43494e calls class slot 0x38 each frame
// unless the drop pause +0x14c runs) and acts as soon as a walk ends, so
// work is looked for on arrival; there's no every-few-seconds check (the
// type's cWorkCheck goes into +0x288 at hire, 0x457a83, and nothing counts
// it). Nothing to do: a keeper in a zoo with habitats (keeperGoToHabitat
// 0x49d539, task 0x65, whose pick finds none: 0x49dafe) stands one play of
// its idle animation before the next walk about - 4.14-4.22 s each of 8
// times, the zookeeper's stand being 33 frames at 125 ms (fKeeperRoutine
// 0x4a8763 -> 0x421eb7 queues that play); with no habitats at all (0x49d524
// counts none: the zoo's own ground isn't one, read live) it's task 1
// "Can't find a habitat" and it walks on at once (0.1-0.2 s, 14 walks), as
// a maintenance worker does (0.1-0.2 s, 15 walks). (The routine plays
// nothing at a keeper's duties, task 0x6e - 0x49ee0f - or going for an
// escaped animal, 0x6f - 0x50de9f -, nor a guide's at a viewing area,
// 0x58abff: those only set a developer's status text, as the debug overlays,
// class slot 0x64, 0x610f8f and 0x61b61d, show; no counterpart)
// The set it plays is ZTStaff::startBehaviour 0x416781 (class slot 0x48):
// ZTUnit's 0x43ca83 with no land list, so its type's [AmbientAnims]
// (0x43cb33, the first range holding its happiness, only with no set
// running) - or [AmbientAnimsUnderwater] / [AmbientAnimsSurface] /
// [AmbientAnimsWater] by its water state; every staff type's lists are
// -100..100 bZooRoutine, the routine above.
// Tour guides (measured 2026-11-17, Lab Play original, two guides by a
// zebra paddock with no trip to take - B:/zt1-shots/r10-staffshows/
// poll-guide*.txt): each walk about (task 1) ends in one play of its idle
// animation, 'stand' (2.63-2.81 s, 21 frames x 125 ms; the other guide's
// 4.0 s), then the next walk: its bZooRoutine is fGuideRoutine 0x58a998 ->
// 0x58a929, which, off a talk (task 0x77), plays the current animation
// once (unitPlayCurrentAnim 0x421eb7). A trip's legs (task 0x76) pause
// 0.2 s. (Not measured in a zoo with no exhibits: the routine doesn't
// look at habitats, so it stands there too.)
bool Staff::idleOriginal(const Member &m) const {
  if (Features::staffPaths)
    return false;
  const Kind k = this->typeList[m.type].kind;
  return k == Kind::Keeper || k == Kind::Scientist || k == Kind::Trainer || k == Kind::Maint || k == Kind::Guide;
}
void Staff::idleStep(Member &m, const WorldMap &map, Fences &fences) {
  const Kind k = this->typeList[m.type].kind;
  if (m.walksLeft > 0) {
    // (the stand's over: off on the walk about)
    m.walksLeft = 0;
    wander(m, map, fences);
    return;
  }
  if (m.workCheck <= 0) {
    const bool found = findWork(m, map, fences);
    ZT_STEP("0x423d04", "The chooser, on arrival: work?",
            {"found", found ? kJobNames[std::clamp(static_cast<int>(m.job), 0, 9)] : "nothing"},
            {"duty", this->rm ? dutyText(m) : std::string()});
    if (found)
      return;
  }
  bool habitats = false;
  for (const Fences::Exhibit &e : fences.exhibits())
    habitats = habitats || !e.pocket;
  if (((k == Kind::Keeper || k == Kind::Scientist || k == Kind::Trainer) && habitats) || k == Kind::Guide) {
    m.idleLeft = std::max(0.1f, animSeconds(m, "idle"));
    m.walksLeft = 1;
    m.anim = "idle";
    m.animTime = 0;
    ZT_STEP("0x4a8763", "Nothing to do: one play of its idle animation", {"seconds", m.idleLeft});
    return;
  }
  wander(m, map, fences);
}

// fWalk, kept to the paths: on a path, off along it to another bit of
// path a few tiles away; off one, to the nearest path; no path near, it
// stands where it is (nobody roams open ground with nothing there).
// staffpaths off: the original's walk about (0x43d948)
void Staff::wander(Member &m, const WorldMap &map, const Fences &fences) {
  if (!Features::staffPaths) {
    if (!walkAbout(m, map, fences)) {
      m.idleLeft = kIdleSeconds * 2;
      m.walksLeft = 0;
    }
    return;
  }
  const int W = map.getWidth(), H = map.getHeight();
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  // (staffpaths off: all ground counts as path, so they wander it freely)
  auto pathLike = [&](int x, int y) { return !Features::staffPaths || map.isPath(x, y); };
  bool onPath = pathLike(sx, sy);
  std::vector<std::pair<int, int>> reach;
  std::vector<char> seen(static_cast<size_t>(W) * H, 0);
  std::queue<std::pair<int, int>> q;
  q.push({sx, sy});
  if (sx >= 0 && sy >= 0 && sx < W && sy < H)
    seen[sy * W + sx] = 1;
  const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  const int radius = !Features::staffPaths ? kWanderRadius : onPath ? kWanderRadius + 3 : 15;
  while (!q.empty()) {
    auto [x, y] = q.front();
    q.pop();
    if (onPath && std::abs(x - sx) + std::abs(y - sy) >= 2)
      reach.push_back({x, y});
    if (!onPath && pathLike(x, y)) {
      reach = {{x, y}}; // the nearest path
      break;
    }
    for (auto &dd : d) {
      int nx = x + dd[0], ny = y + dd[1];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H || seen[ny * W + nx])
        continue;
      if (std::abs(nx - sx) > radius || std::abs(ny - sy) > radius)
        continue;
      if (onPath && !pathLike(nx, ny))
        continue;
      if (!walkable(nx, ny, map, fences, m.access) ||
          !canStep(x, y, nx, ny, map, fences, m.access))
        continue;
      seen[ny * W + nx] = 1;
      q.push({nx, ny});
    }
  }
  if (reach.empty()) {
    m.idleLeft = kIdleSeconds * 2;
    m.walksLeft = 0;
    return;
  }
  auto [gx, gy] = reach[std::uniform_int_distribution<size_t>(0, reach.size() - 1)(this->rng)];
  findPath(m, gx, gy, map, fences, gx + 0.5f, gy + 0.5f);
}

// The sound its work plays (fPlayWithSound in its behaviour sets: sweep,
// fix, feed_herb, feed_carn, rake), once a go, heard from where it is
void Staff::workSound(const Member &m, const std::string &anim) const {
  static const std::map<std::string, std::string> keyOf = {
      {"sweep", "sweep"}, {"fix", "fix"}, {"feedh", "feed_herb"}, {"feedc", "feed_carn"},
      {"clean", "rake"}, {"bag", "sweep"}, {"heal", "heal_animal"}, {"empty", "empty_trash"},
      // (marine specialists in a tank: bFeedTank, bClean, bSubheal)
      {"subfeed", "feed_carn"}, {"subclean", "subpoo"}, {"subheal", "heal_animal"}};
  auto k = keyOf.find(anim);
  if (k == keyOf.end())
    return;
  const Type &t = this->typeList[m.type];
  auto s = t.sounds.find(k->second);
  float dx = 0, dy = 0;
  if (s != t.sounds.end() && this->viewOffset && this->viewOffset(m.x, m.y, dx, dy))
    Sound::get().playAt(s->second.first, dx, dy, s->second.second);
}

// A [Sounds] key's sound, heard from where it is (fPlayWithSound)
void Staff::soundAt(const Member &m, const std::string &key) const {
  const Type &t = this->typeList[m.type];
  auto s = t.sounds.find(key);
  float dx = 0, dy = 0;
  if (s != t.sounds.end() && this->viewOffset && this->viewOffset(m.x, m.y, dx, dy))
    Sound::get().playAt(s->second.first, dx, dy, s->second.second);
}

// ours: a work animation queued so many times
void Staff::play(Member &m, const std::string &anim, int times) {
  workSound(m, anim);
  m.anim = anim;
  m.animTime = 0;
  m.playsLeft = times;
  m.steps.clear();
  m.reverse = false;
  m.path.clear();
}

// A work's behaviour set from its type's .ai, step by step (bHeal, bClean,
// bBagDino...: zoo.exe's jobs start them by name - Job2 0x5084b3, Job4
// 0x47002e, Job12 0x50e211); a type without it plays ours' anim so many
// times
void Staff::playSet(Member &m, const std::string &set, const std::string &anim, int times) {
  const Type &t = this->typeList[m.type];
  auto it = t.sets.find(set);
  if (it == t.sets.end()) {
    play(m, anim, times);
    return;
  }
  m.steps = it->second;
  m.playsLeft = static_cast<int>(m.steps.size());
  m.animTime = 0;
  m.path.clear();
  startStep(m);
}

// The set's next step: its animation (backwards for fPlayReverse), its sound
void Staff::startStep(Member &m) {
  if (m.playsLeft <= 0 || static_cast<size_t>(m.playsLeft) > m.steps.size())
    return;
  const Type::Step &step = m.steps[m.steps.size() - static_cast<size_t>(m.playsLeft)];
  m.anim = step.anim;
  m.reverse = step.reverse;
  if (!step.sound.empty())
    soundAt(m, step.sound);
}

void Staff::climb(Member &m, float x, float y, float z, bool down, bool inside, int water, const WorldMap &map) {
  m.climbFrom[0] = m.x;
  m.climbFrom[1] = m.y;
  m.climbFrom[2] = groundAt(map, m.x, m.y) + m.z;
  m.climbTo[0] = x;
  m.climbTo[1] = y;
  m.climbTo[2] = groundAt(map, x, y) + z;
  m.climbTime = 0;
  m.climbing = true;
  m.climbDown = down;
  m.climbInside = inside;
  m.climbWater = water;
  m.water = 1;
  m.anim = "climb";
  m.animTime = 0;
  m.playsLeft = 0;
  m.path.clear();
  m.pathAt = 0;
  if (std::hypot(x - m.x, y - m.y) > 0.01f)
    // A descending climber backs down: keep facing the rungs, rather
    // than turning away from the wall with the direction of travel.
    face(m, down ? 2 * m.x - x : x, down ? 2 * m.y - y : y);
}

// fMaintRoutine: a worn fence or a run-down tank filter it's assigned to,
// the nearest it can reach
bool Staff::findWork(Member &m, const WorldMap &map, Fences &fences) {
  ZT_FN("", "Finding work");
  const Type &t = this->typeList[m.type];
  // zoo.exe 0x50cd14: keepers catch zoo animals; scientists contain
  // dinosaurs only after the helicopter has tranquilized them.
  if ((t.kind == Kind::Keeper || t.kind == Kind::Scientist) && catchWork(m, map, fences))
    return true;
  if (t.kind == Kind::Keeper || t.kind == Kind::Scientist || t.kind == Kind::Trainer)
    return keeperWork(m, map, fences);
  if (t.kind == Kind::Guide)
    return tourWork(m, map, fences);
  if (t.kind != Kind::Maint)
    return false;
  // The zoo's work list (zoo.exe 0x638fa0, kept by Staff::noticeWork from
  // what was posted to maintenance, 0x434e30): 10030 a bin over 12 full
  // (job 9), 10031 litter (7), 10032 dung on the grounds (0xb), 10033 /
  // 10034 a worn fence's tile (0x10), 10035 a filter decayed past
  // cFilterDecayedHealth / 10036 one broken, its health 0 (0x1a). 10034 and
  // 10036 are flagged (+0xc) and at the list's front, the rest at its back.
  // Task78_MaintFindWork (0x439cd0, the loop at 0x4a1c8d) takes, among the
  // entries whose duty is ticked (9 trash, 7/0xb sweep, 0x10 fences, 0x1a
  // filters), the nearest by squared tile distance from the worker's tile to
  // the entry's (0x4a1d98), the first in the list's order on a tie; once the
  // best is flagged the look stops at the first unflagged one, so urgent
  // work comes before all other. An entry a worker holds (+8) is taken only
  // by one strictly nearer to it than its holder (0x4a1ee2) - so never by its
  // own holder - and the holder drops its job (0x49e56b: slot 0x174)
  noticeWork(map, fences);
  struct Entry {
    bool flagged;
    Job job;
    Fences::Edge edge;
    int item;   // the filter (its index now), bin, litter or dung
    int ex, ey; // the entry's tile
    long long dist;
    Member *holder;
    int entry; // the work list entry's id
  };
  const int mx = static_cast<int>(std::floor(m.x)), my = static_cast<int>(std::floor(m.y));
  auto tileDist = [](int ax, int ay, int bx, int by) {
    return static_cast<long long>(ax - bx) * (ax - bx) + static_cast<long long>(ay - by) * (ay - by);
  };
  // (an entry whose tile it found no way to before is passed over until the
  // list is cleared: 0x4a1d4c, the worker's +0x27c)
  // (staffInvalidHas 0x4167a9: a find over +0x27c..+0x280)
  auto invalid = [&](int ex, int ey) {
    return std::find(m.invalid.begin(), m.invalid.end(), std::make_pair(ex, ey)) != m.invalid.end();
  };
  // Work that's gone (litter swept with its neighbour's, a can emptied, a
  // fence mended): its job frees the entry when it finds nothing there
  // (Job16_GoToFence 0x59a497, Job11_GoToZoodoo 0x4a3f13, Job10's begin
  // 0x481981 -> 0x4a97e5)
  // (ours: freed as the look comes to it, where the original takes it and
  // its job frees it on the next tick)
  for (size_t i = 0; i < this->work.size();) {
    if (!workGone(this->work[i], fences)) {
      i++;
      continue;
    }
    if (Member *h = this->member(this->work[i].holder))
      if (h->workEntry == this->work[i].id)
        h->workEntry = -1;
    this->work.erase(this->work.begin() + static_cast<std::ptrdiff_t>(i));
  }
  std::vector<Entry> entries;
  for (const WorkEntry &w : this->work) {
    const int duty = w.job == Job::Trash ? 0 : w.job == Job::Fence ? 2 : w.job == Job::Filter ? 3 : 1;
    if (!m.duties[duty] || invalid(w.x, w.y))
      continue;
    Member *h = w.holder >= 0 ? this->member(w.holder) : nullptr;
    const long long dist = tileDist(mx, my, w.x, w.y);
    if (h) {
      // (ours: a holder already at its work isn't taken from - its distance
      // is the least there can be)
      if (h->job == w.job && h->pathAt >= h->path.size())
        continue;
      const int hx = static_cast<int>(std::floor(h->x)), hy = static_cast<int>(std::floor(h->y));
      if (!(dist < tileDist(hx, hy, w.x, w.y)))
        continue;
    }
    const int item = w.job == Job::Filter ? filterAt(fences, w.x, w.y) : w.item;
    entries.push_back({w.flagged, w.job, w.edge, item, w.x, w.y, dist, h, w.id});
  }
  // Flagged first (the list's front), then nearest
  std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    if (a.flagged != b.flagged) return a.flagged;
    return a.dist < b.dist;
  });
  ZT_STEP("0x439cd0", "Maintenance: the work list, urgent first, then the nearest (squared tiles)",
          {"jobs", static_cast<int>(entries.size())},
          {"nearest", entries.empty() ? "none" : kJobNames[std::clamp(static_cast<int>(entries.front().job), 0, 9)]},
          {"urgent", entries.empty() ? 0 : entries.front().flagged ? 1 : 0},
          {"distance", entries.empty() ? 0.0f : std::sqrt(static_cast<float>(entries.front().dist))});
  auto takeOver = [&](const Entry &s) {
    Member *h = s.holder;
    if (h) {
      ZT_STEP("0x4a1ee2", "Nearer than the worker that had it: taken over, the other drops its job");
      // (0x49e56b: the holder's +0x2c0 and the entry's +8 cleared, slot 0x174)
      if (h->workEntry == s.entry)
        h->workEntry = -1;
      if (WorkEntry *w = this->workEntry(s.entry))
        w->holder = -1;
      h->job = Job::None;
      h->path.clear();
      h->pathAt = 0;
      h->jobItem = h->jobFilter = -1;
      h->jobEdge = {false, -1, -1};
      h->anim = "idle";
      h->animTime = 0;
      h->idleLeft = 0;
      h->workCheck = 0;
      h->dutyId = kMaintMonitoring;
      h->dutyArg.clear();
    }
    takeEntry(m, s.entry);
  };
  // No way to it: its tile goes on the worker's list (the jobs' route
  // failures: 0x4a20e1, 0x4a289f, 0x595af4, 0x5ec29f, 0x61b0e6 -> 0x4a97ed),
  // the entry let go and left on the list (0x5ec2cf: +8 and +0x2c0 cleared)
  // (ours: the next entry is tried in the same look, where the original
  // takes it on its next tick)
  auto noWay = [&](const Entry &s) {
    if (!invalid(s.ex, s.ey))
      m.invalid.push_back({s.ex, s.ey});
  };
  for (const Entry &s : entries) {
    // (litter: Job7_GoToTrashPile's whileGoing 0x52cfa0 - a route to its
    // tile, 0x422238; none, the tile onto its list, 0x4a97ed, and the job's
    // off: +0x2a4, the entry let go)
    if (s.job == Job::Litter || s.job == Job::Dung) {
      const ZooItems::Item *it = this->items->item(s.item);
      if (it && findPath(m, s.ex, s.ey, map, fences, it->x, it->y)) {
        takeOver(s);
        m.job = s.job;
        m.jobItem = s.item;
        m.dutyId = s.job == Job::Dung ? kGoingToCleanUp : kGoingToSweep;
        return true;
      }
      noWay(s);
      continue;
    }
    if (s.job == Job::Trash) {
      // zoo.exe 0x4271a1 (Job9_GoToTrashCan's whileGoing): a route to the
      // can's own tile; none, and that tile goes on the list and one of its
      // four neighbours (tileNeighbours4 0x4171a1), picked at random, is
      // tried; no way there either, and it goes on the list too and the
      // job's off
      auto reach = [&](int tx, int ty) {
        return walkable(tx, ty, map, fences) && findPath(m, tx, ty, map, fences, tx + 0.5f, ty + 0.5f);
      };
      bool there = reach(s.ex, s.ey);
      if (!there) {
        noWay(s);
        const int d[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        const auto &dd = d[std::uniform_int_distribution<int>(0, 3)(this->rng)];
        const int nx = s.ex + dd[0], ny = s.ey + dd[1];
        there = reach(nx, ny);
        if (!there && !invalid(nx, ny)) {
          ZT_STEP("0x4271a1", "Couldn't reach the trash can's tile or the neighbour tried: both on its list",
                  {"x", nx}, {"y", ny});
          m.invalid.push_back({nx, ny});
        }
      }
      if (there) {
        takeOver(s);
        m.job = Job::Trash;
        m.jobItem = s.item;
        m.dutyId = kGoingToCan;
        return true;
      }
      continue;
    }
    if (s.job == Job::Filter) {
      if (s.item < 0 || s.item >= static_cast<int>(fences.filters().size()))
        continue;
      // A tile beside its footprint, the nearest first
      const Fences::Filter &f = fences.filters()[s.item];
      const Fences::FilterType &k = fences.filterType(f.file);
      std::vector<std::pair<long long, std::pair<int, int>>> sides;
      for (int ty = f.y - 1; ty <= f.y + k.footprintY; ty++)
        for (int tx = f.x - 1; tx <= f.x + k.footprintX; tx++) {
          bool edge = tx == f.x - 1 || ty == f.y - 1 || tx == f.x + k.footprintX || ty == f.y + k.footprintY;
          bool corner = (tx == f.x - 1 || tx == f.x + k.footprintX) && (ty == f.y - 1 || ty == f.y + k.footprintY);
          if (edge && !corner && walkable(tx, ty, map, fences))
            sides.push_back({tileDist(mx, my, tx, ty), {tx, ty}});
        }
      std::stable_sort(sides.begin(), sides.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
      for (const auto &sd : sides) {
        auto [tx, ty] = sd.second;
        if (findPath(m, tx, ty, map, fences, tx + 0.5f, ty + 0.5f)) {
          takeOver(s);
          m.job = Job::Filter;
          m.jobFilter = s.item;
          m.jobEdge = {false, -1, -1};
          m.dutyId = kGoingToFilter;
          return true;
        }
      }
      noWay(s);
      continue;
    }
    // A fence: stand at the tile's side nearest the piece
    float ex = s.ex + 0.5f, ey = s.ey + 0.5f;
    float fx = s.edge.alongX ? s.edge.x + 0.5f : static_cast<float>(s.edge.x);
    float fy = s.edge.alongX ? static_cast<float>(s.edge.y) : s.edge.y + 0.5f;
    ex += (fx - ex) * 0.5f;
    ey += (fy - ey) * 0.5f;
    if (findPath(m, s.ex, s.ey, map, fences, ex, ey)) {
      if (s.flagged)
        ZT_STEP("0x439cd0", "A worn fence's own side: before any other work", {"distance", std::sqrt(static_cast<float>(s.dist))});
      takeOver(s);
      m.job = Job::Fence;
      m.jobEdge = s.edge;
      m.jobFilter = -1;
      m.dutyId = kGoingToFix;
      return true;
    }
    noWay(s);
  }
  return false;
}

// ----------------------------------------------------------------------------
// Keepers, scientists, marine specialists (fKeeperRoutine): their assigned
// exhibits in turn. An exhibit needs a visit when its food is running low
// or there's dung to rake (a tank: food, or its water is dirty). Keepers
// walk to its gate ("Going to Exhibit 1"), in ("Entering Exhibit 1"), put
// food down ("Placing food", feedh or feedc three times), rake each pile
// ("Raking up poo", clean four times) and come out ("Exiting exhibit").
// Marine specialists work from the gate's platform: food in, then the
// water cleaned (cCleanTankPct a go, "Cleaning the tank").
// ----------------------------------------------------------------------------
// ours: turns it toward a point
void Staff::face(Member &m, float x, float y) {
  float d = std::hypot(x - m.x, y - m.y);
  if (d > 0.001f) {
    m.fx = (x - m.x) / d;
    m.fy = (y - m.y) / d;
  }
}

std::string Staff::foodFor(int exhibit, const Fences &fences, bool &carnivore) const {
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  // (carnivore: the keeper's food type +0x2a4 is 1, carnchow - zoo.exe
  // 0x5084b3 plays bFeedCarn only then; the same index makes the food it
  // puts down, 0x48f7ca -> 0x5080ee's table 0x64e6ec, so dinocarn and chum
  // go down with bFeedHerb)
  carnivore = ex && ex->testCarnivore && !ex->testDino && !ex->tank;
  if (!ex)
    return "herbchow";
  // Real animals: the first one's keeper food (food.cfg's order)
  if (this->animals)
    for (const Animals::Member &a : this->animals->members())
      if (a.exhibit == exhibit) {
        // (food.cfg, then Dinosaur Digs' food01.cfg and Marine Mania's
        // food02.cfg after it: cKeeperFoodType counts on through them)
        const std::string food = this->animals->keeperFoodOf(a.type);
        carnivore = food == "carnchow";
        return food;
      }
  return ex->tank ? "fish" : ex->testDino ? (ex->testCarnivore ? "dinocarn" : "dinogras")
                                          : (ex->testCarnivore ? "carnchow" : "herbchow");
}

// zoo.exe 0x423f00: not an egg left by its mother (+0x395 with +0x3a6), and
// the animal type's keeper (cKeeper, +0x2f0: 9500 a zookeeper, 9551 a
// scientist, 9552 a marine specialist) is this staff member's type (its
// slot 0x20)
bool Staff::caresFor(const Member &m, int animalType) const {
  if (!this->animals || animalType < 0 || animalType >= static_cast<int>(this->animals->types().size()))
    return false;
  const int keeper = this->animals->types()[animalType].keeper;
  const Kind kind = this->typeList[m.type].kind;
  return keeper == 9551 ? kind == Kind::Scientist : keeper == 9552 ? kind == Kind::Trainer : kind == Kind::Keeper;
}

// The animal to feed (zoo.exe 0x49f01a, from keeperFoodDuty 0x49efa6): of
// the habitat's animals in turn (+0x6c..+0x70), those it looks after
// (0x423f00; ours: not crated), not on its +0x270 list (no way to them),
// with none of their own keeper food there (0x4a85da with the type's
// cKeeperFoodType +0x3c8, under 1), not eggs (+0x395) and in this habitat
// (0x4161da) - the first of them hungry (+0x2b0 at its cHungerThreshold,
// type +0x20c) at once, else one of them at random
int Staff::animalToFeed(const Member &m, int exhibit) {
  if (!this->animals || !this->items)
    return -1;
  std::vector<int> fits;
  for (const Animals::Member &a : this->animals->members()) {
    if (a.exhibit != exhibit || a.boxed || a.egg || !caresFor(m, a.type) ||
        std::find(m.unreachable.begin(), m.unreachable.end(), a.id) != m.unreachable.end() ||
        this->items->foodIn(exhibit, this->animals->keeperFoodOf(a.type)) > 0)
      continue;
    if (a.hunger >= this->animals->types()[a.type].hungerThreshold)
      return a.id;
    fits.push_back(a.id);
  }
  if (fits.empty())
    return -1;
  return fits[std::uniform_int_distribution<size_t>(0, fits.size() - 1)(this->rng)];
}

// zoo.exe ZTKeeper slot 0x324 (0x42404e): the dirt object's type id (its
// slot 0x20, cNameID) on the keeper type's +0x20c list, cDirt
bool Staff::cleans(const Member &m, const ZooItems::Item &dung) const {
  if (dung.kind != ZooItems::Kind::Dung || m.type < 0 || m.type >= static_cast<int>(this->typeList.size()) || !this->items)
    return false;
  const std::vector<int> &dirt = this->typeList[m.type].dirt;
  return std::find(dirt.begin(), dirt.end(), this->items->dirtType(dung)) != dirt.end();
}

std::vector<int> Staff::dungFor(const Member &m, int exhibit) const {
  std::vector<int> out;
  if (!this->items)
    return out;
  for (int id : this->items->ofKind(ZooItems::Kind::Dung, exhibit))
    if (const ZooItems::Item *it = this->items->item(id))
      if (cleans(m, *it))
        out.push_back(id);
  return out;
}

bool Staff::needsVisit(const Member &m, int exhibit, const Fences &fences) const {
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex || ex->animalCount() <= 0 || !this->items)
    return false;
  // Real animals (zoo.exe 0x49d202): one to heal (health 85% or less), one
  // hungry with no food, any poo, or not visited for a while - never
  // visited, or longer ago than the service interval (habitatMgr +100
  // against the habitat's +0xec stamp), food there or not: a marine
  // specialist went into a tank with food in it only when an animal starved
  if (ex->animals > 0 && this->animals) {
    bool heal = false, starving = false;
    float food = this->items->foodIn(exhibit);
    // (hungry with none of its own food: animalHungryNoFood 0x49d3e1 ->
    // habitatKeeperFoodCheck 0x4a8624, its keeper food +0x3c8 alone)
    for (const Animals::Member &a : this->animals->members())
      if (a.exhibit == exhibit && !a.boxed) {
        heal = heal || this->animals->needsHealing(a);
        starving = starving || (a.hunger >= this->animals->types()[a.type].hungerThreshold &&
                                this->items->foodIn(exhibit, this->animals->keeperFoodOf(a.type)) <= 0);
      }
    if(ex->show)starving=false;
    bool poo = !dungFor(m, exhibit).empty(); // (0x49d375: slot 0x324, its cDirt)
    auto last = this->lastVisit.find(exhibit);
    bool due = last == this->lastVisit.end() || this->clock - last->second > kRevisitSeconds;
    // (a tank: its water murky too - under tanks.cfg murkyWaterPurity 60,
    // zoo.exe 0x49f22b)
    bool murky = ex->tank && ex->water > 0 && ex->purity < kMurkyPurity;
    ZT_STEP("0x49d202", "Does it need a visit? (one to heal, one starving, dung, murky water, or due)",
            {"exhibit", exhibit}, {"to heal", heal ? 1 : 0}, {"starving", starving ? 1 : 0}, {"dung", poo ? 1 : 0},
            {"murky", murky ? 1 : 0}, {"due", due ? 1 : 0}, {"food there", food},
            {"visit", heal || starving || poo || due || murky ? "yes" : "no"});
    return heal || starving || poo || due || murky;
  }
  float low = ex->animalCount() * ZooItems::kFoodPerAnimal * 0.5f;
  bool hungry = !ex->show && this->items->foodIn(exhibit) < low;
  if (ex->tank)
    return hungry || (ex->water > 0 && ex->purity < kMurkyPurity);
  (void)m;
  return hungry || !dungFor(m, exhibit).empty();
}

// zoo.exe 0x444827 (animalRateExhibit, when the habitat's +0x2d says to
// recount; read through 0x4a89cb / 0x42f5c9): +0xf0 counts the ZTKeeper-class
// units on the habitat's tiles whose task is 0x6e (their duties there), +0x132
// is set when there's any. +0xf4 counts the staff headed for it: one up as
// Task65_KeeperGoToHabitat's whileGoing (0x423f57) sets off for it, one down
// (staffNotifyHabitat 0x4a879f, through ZTKeeper slot 0x2d8 0x4167db) when
// the keeper's destination changes - its next pick - so a keeper at its duties
// there is counted in both.
// ours: our visit's phases stand for the tasks - 0 (and a tank's 30, climbing
// in, task 0x7e) on the way, the work inside task 0x6e; once ours walks or
// climbs out (6, 24, 26, 27, 29) the original's keeper has picked its next
// habitat already, so it's counted in neither. Inside is the original's
// inAssignedHabitat (0x49f6b7): the habitat at its destination's tile is the
// one at its own
void Staff::keepersAt(int exhibit, int self, const Fences &fences, int &inDuties, int &headed) const {
  inDuties = headed = 0;
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  for (const Member &o : this->list) {
    const Kind k = this->typeList[o.type].kind;
    if (o.id == self || o.job != Job::Visit || o.visit != exhibit ||
        !(k == Kind::Keeper || k == Kind::Scientist || k == Kind::Trainer))
      continue;
    if (o.phase == 6 || o.phase == 24 || o.phase == 26 || o.phase == 27 || o.phase == 29)
      continue;
    headed++;
    const bool inside = ex && ex->tank ? o.tank == exhibit && o.phase != 0 && o.phase != 30
                                       : o.phase != 0 && fences.exhibitAt(static_cast<int>(std::floor(o.x)),
                                                                          static_cast<int>(std::floor(o.y))) == exhibit;
    inDuties += inside ? 1 : 0;
  }
}

bool Staff::keeperWork(Member &m, const WorldMap &map, Fences &fences) {
  // Exhibits gone (a fence taken out) leave the schedule
  m.exhibits.erase(std::remove_if(m.exhibits.begin(), m.exhibits.end(),
                                  [&](int ex) { return !fences.exhibit(ex); }),
                   m.exhibits.end());
  // Its own exhibits; unassigned, every exhibit it can look after (as the
  // original: "Going to Exhibit 2" with no assignment)
  std::vector<int> list = m.exhibits;
  if (list.empty()) {
    const Type &t = this->typeList[m.type];
    for (const Fences::Exhibit &e : fences.exhibits()) {
      if (!e.named || e.animalCount() <= 0)
        continue;
      // (scientists the dinosaurs, keepers the zoo animals: their
      // assignment messages 10324 and 10320)
      bool fits = t.kind == Kind::Trainer ? e.tank : t.kind == Kind::Scientist ? e.forScientist() : e.forKeeper();
      if (fits)
        list.push_back(e.id);
    }
  }
  // Most in need first - no distance (zoo.exe 0x49d7c7, Task 0x65's target,
  // sorted by the comparison at 0x49da35): the animals to heal (habitat
  // +0xa0, likely), then the hungry ones with no food (0x49d9e2 counting
  // 0x49d3e1: hunger at the type's threshold, no food), then the dung there
  // (0x4a8940), then the longest since a keeper's visit (zooTime +0xec
  // against the habitat's +0xec); assigned ones sorted the same way
  {
    struct Need {
      int heal = 0, hungry = 0, dung = 0;
      double since = 1e300;
    };
    std::map<int, Need> need;
    for (int id : list) {
      Need n;
      if (this->animals)
        for (const Animals::Member &a : this->animals->members())
          if (a.exhibit == id && !a.boxed) {
            const float food = this->items ? this->items->foodIn(id, this->animals->keeperFoodOf(a.type)) : 0.0f;
            n.heal += this->animals->needsHealing(a) ? 1 : 0;
            n.hungry += a.hunger >= this->animals->types()[a.type].hungerThreshold && food <= 0 ? 1 : 0;
          }
      if (this->items)
        n.dung = static_cast<int>(dungFor(m, id).size()); // (0x4a8940: slot 0x324, its cDirt)
      auto last = this->lastVisit.find(id);
      if (last != this->lastVisit.end())
        n.since = this->clock - last->second;
      need[id] = n;
    }
    std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
      const Need &x = need[a], &y = need[b];
      if (x.heal != y.heal) return x.heal > y.heal;
      if (x.hungry != y.hungry) return x.hungry > y.hungry;
      if (x.dung != y.dung) return x.dung > y.dung;
      return x.since > y.since;
    });
  }
  // Two looks (zoo.exe 0x49d7c7), neither taking the habitat it stands in
  // (slot 0x24c): first one with work, no keeper at its duties there
  // (0x4a89cb: +0x132) and none headed for it (+0xf4 0); then one with work
  // where those at their duties plus those headed for it (+0xf0 + +0xf4 -
  // one at work there counted in both) are under 2, a keeper last there over
  // 1000 ticks ago (zooTime +0xec against the habitat's +0xec; a tick a
  // second here, as kRevisitSeconds)
  const int standingIn = m.tank >= 0 ? m.tank
                                     : fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
  for (int pass = 0; pass < 2; pass++)
  for (int ex : list) {
    if (ex == standingIn || !needsVisit(m, ex, fences))
      continue;
    int inDuties = 0, headed = 0;
    keepersAt(ex, m.id, fences, inDuties, headed);
    ZT_STEP("0x49d7c7", "Keepers there: at their duties (+0xf0) and headed for it (+0xf4)", {"pass", pass + 1},
            {"exhibit", fences.exhibit(ex)->name}, {"at duties", inDuties}, {"headed", headed});
    if (pass == 0 ? inDuties > 0 || headed > 0 : inDuties + headed >= 2)
      continue;
    if (pass == 1) {
      auto last = this->lastVisit.find(ex);
      if (last != this->lastVisit.end() && this->clock - last->second <= 1000.0)
        continue;
    }
    // This is the one picked (0x49d7c7). Its gate: the tile outside it and
    // the one in. No way to it, and Task65 (0x423f57 -> 0x58c060) says so
    // and ends the look - the next exhibit waits for the next one
    bool blocked = false;
    for (const auto &kv : fences.pieces()) {
      if (!kv.second.gate || fences.exhibitOf(kv.first) != ex)
        continue;
      const Fences::Edge &e = kv.first;
      int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
      int bx = e.x, by = e.y;
      bool aIn = fences.exhibitAt(ax, ay) == ex;
      int ox = aIn ? bx : ax, oy = aIn ? by : ay, ix = aIn ? ax : bx, iy = aIn ? ay : by;
      // (something standing in front of the gate: 0x4e8537's 3; ours
      // doesn't test the tile inside)
      if (!walkable(ox, oy, map, fences)) {
        blocked = true;
        continue;
      }
      m.outX = ox + 0.5f;
      m.outY = oy + 0.5f;
      m.inX = ix + 0.5f;
      m.inY = iy + 0.5f;
      m.access = -1;
      m.dutyId = kGoingTo;
      m.dutyArg = fences.exhibit(ex)->name;
      if (!findPath(m, ox, oy, map, fences, m.outX, m.outY)) {
        m.dutyId = kMonitoring;
        m.dutyArg.clear();
        continue;
      }
      m.job = Job::Visit;
      m.visit = ex;
      m.phase = 0;
      // (its duties there begin: the +0x270 and +0x27c lists emptied,
      // zoo.exe 0x49f3ca)
      m.invalid.clear();
      m.unreachable.clear();
      ZT_STEP("", "Off to visit the exhibit, by its gate", {"exhibit", fences.exhibit(ex)->name}, {"gate x", ox}, {"gate y", oy});
      return true;
    }
    pathTrouble(m, fences.exhibit(ex)->name, blocked);
    m.dutyId = kMonitoring;
    m.dutyArg.clear();
    return false;
  }
  return false;
}

// zoo.exe 0x4e85e8 (staffPathTrouble, from Task65_KeeperGoToHabitat
// 0x423f57 when the keeper has no way to the habitat's gate): the gate
// tested (0x4e8537) - fenced on both sides (1), something on either of its
// tiles (3) or past the staff's route distance (4) - "The entrance to %s is
// blocked and is not accessible to zoo staff." (11001); else "%s is having
// trouble getting to %s." (11004), the keeper's name and the habitat's;
// either posted with priority 2 (0x49cec0)
// (ours: only 3 is told apart - 1 and 4 need the original's route distance
// fields, issue #145 - so a gate with no route to it says 11004)
void Staff::pathTrouble(const Member &m, const std::string &exhibit, bool blocked) {
  ZT_STEP("0x4e85e8", "No way to the exhibit's gate: the keeper says so", {"exhibit", exhibit},
          {"message", blocked ? 11001 : 11004});
  if (!this->rm)
    return;
  std::string s = this->rm->getString(blocked ? 11001 : 11004);
  std::vector<const std::string *> args;
  if (!blocked)
    args.push_back(&m.name);
  args.push_back(&exhibit);
  size_t from = 0;
  for (const std::string *arg : args)
    if (size_t at = s.find("%s", from); at != std::string::npos) {
      s.replace(at, 2, *arg);
      from = at + arg->size();
    }
  SDL_Log("[Staff] %s", s.c_str());
  this->notices.push_back({s, 2, m.id});
}

// zoo.exe exhibitAccessCheck 0x44e4eb (run after OK on a new exhibit's
// name - the check queued at gameMgr +0x34, 0x435a0d - and not while
// loading): a temporary router, a zookeeper (9500) or for a tank a marine
// specialist (9552) with +0x2a8 set, walks the exhibit's boundary pairs
// from the gate's (exhibitBoundaryStep 0x487f34), each tested by
// gateCrossCheck 0x46676a: fenced on both sides (1 - every plain fence),
// something on either tile (3), the step across impassable for the router
// (4: staffStepCost 0x41456c at DAT_00635494 - 1, the zoo wall too), else
// crossable (0). A crossable pair is routed by the pathfinder ([0x638098]
// +0x10, slot 0) from the zoo's entrance (0x410d04) to its outer tile: no
// route, code 6; a route to a tile on the zoo's own ground (the grid's
// habitat +0x2c), accessible - done, no message; one whose tile is in
// another exhibit, code 5 (no message). The first pair's code decides, the
// gate's: 1 and 3 post 11001, 4 and 6 post 11000 (the table at 0x467048),
// with 0x49cec0 and 5. measured: a gate opening onto grass ringed by rocks
// says "... is inaccessible to zoo staff." in the original (2026-11-16,
// #145); a pen whose gate opens into another exhibit says nothing (round 4).
// ours: the gate's pair alone - our only crossable boundary pieces are
// gates, and pieces facing each other on one edge don't occur
int Staff::exhibitAccess(int exhibit, const WorldMap &map, const Fences &fences) {
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex || !this->rm)
    return 0;
  int ex0 = -1, ey0 = -1;
  if (!this->guests || !this->guests->entrance(ex0, ey0))
    return 0;
  int routerType = -1;
  for (size_t i = 0; i < this->typeList.size(); i++)
    if (this->typeList[i].kind == (ex->tank ? Kind::Trainer : Kind::Keeper)) {
      routerType = static_cast<int>(i);
      break;
    }
  if (routerType < 0)
    return 0;
  // (ours: an entrance tile staff can't stand on - the route starts beside
  // it, inside the wall)
  if (!walkable(ex0, ey0, map, fences)) {
    bool found = false;
    for (int r = 1; r <= 2 && !found; r++)
      for (int dy = -r; dy <= r && !found; dy++)
        for (int dx = -r; dx <= r && !found; dx++)
          if (fences.insideZoo(ex0 + dx, ey0 + dy) && fences.exhibitAt(ex0 + dx, ey0 + dy) < 0 &&
              walkable(ex0 + dx, ey0 + dy, map, fences)) {
            ex0 += dx;
            ey0 += dy;
            found = true;
          }
  }
  Member router;
  router.type = routerType;
  router.x = ex0 + 0.5f;
  router.y = ey0 + 0.5f;
  int code = 0;
  bool decided = false;
  for (const auto &kv : fences.pieces()) {
    if (!kv.second.gate || fences.exhibitOf(kv.first) != exhibit)
      continue;
    const Fences::Edge &e = kv.first;
    const int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
    const bool aIn = fences.exhibitAt(ax, ay) == exhibit;
    const int ox = aIn ? e.x : ax, oy = aIn ? e.y : ay;
    if (!walkable(ox, oy, map, fences)) {
      code = 3; // (something on the gate's outer tile)
    } else {
      router.path.clear();
      router.pathAt = 0;
      if (!findPath(router, ox, oy, map, fences, ox + 0.5f, oy + 0.5f))
        code = 6;
      else if (fences.exhibitAt(ox, oy) < 0)
        code = 0;
      else
        code = 5;
    }
    decided = true;
    if (code == 0)
      break;
  }
  ZT_STEP("0x44e4eb", "A new exhibit's staff access check", {"exhibit", ex->name}, {"code", decided ? code : -1},
          {"from x", ex0}, {"from y", ey0});
  SDL_Log("[Staff] access check of %s from %d,%d: code %d", ex->name.c_str(), ex0, ey0, decided ? code : -1);
  const int msg = code == 1 || code == 3 ? 11001 : code == 4 || code == 6 ? 11000 : 0;
  if (!msg)
    return 0;
  std::string s = this->rm->getString(msg);
  if (size_t at = s.find("%s"); at != std::string::npos)
    s.replace(at, 2, ex->name);
  SDL_Log("[Staff] %s", s.c_str());
  this->notices.push_back({s, 5, -1});
  return msg;
}

// The next step of a visit, when the last walk or animation is done
void Staff::visitStep(Member &m, const WorldMap &map, Fences &fences) {
  ZT_FN("", "A keeper's visit: the next part");
  const Type &t = this->typeList[m.type];
  Fences::Exhibit *ex = fences.exhibit(m.visit);
  auto done = [&] {
    // (a visit cut short in a tank: out on the bank by its gate)
    if (m.tank >= 0 || m.water != 0) {
      m.x = m.outX;
      m.y = m.outY;
      m.tank = -1;
      m.water = 0;
      m.z = 0;
      m.climbing = m.sweeping = false;
    }
    m.job = Job::None;
    m.visit = -1;
    m.access = -1;
    m.anim = "idle";
    m.idleLeft = 0.5f;
    m.dutyId = kMonitoring;
    m.dutyArg.clear();
    // (its duties in the habitat over: both lists emptied, zoo.exe 0x49ebe7)
    m.invalid.clear();
    m.unreachable.clear();
  };
  if (!ex || !this->items) {
    done();
    return;
  }
  // A platform can move when another tank or building covers its bank.
  // End an active dive safely at the new bank; the next duty plans a fresh entry.
  if(ex->tank && m.tank==m.visit) {
    for(const auto &[edge,piece]:fences.pieces()) {
      if(!piece.gate || fences.exhibitOf(edge)!=m.visit)continue;
      const int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
      const bool aIn=fences.exhibitAt(ax,ay)==m.visit;
      const float ix=(aIn?ax:edge.x)+0.5f,iy=(aIn?ay:edge.y)+0.5f;
      if(std::hypot(m.inX-ix,m.inY-iy)<0.01f)break;
      m.inX=ix;m.inY=iy;
      m.outX=(aIn?edge.x:ax)+0.5f;m.outY=(aIn?edge.y:ay)+0.5f;
      m.path.clear();m.pathAt=0;m.playsLeft=0;
      done();return;
    }
  }
  // Food down (zoo.exe keeperFoodDuty 0x49efa6, each time its duties come
  // round, before healing and raking - Task 0x6e 0x42400a): an animal to
  // feed (0x49f01a), its keeper food (unit +0x2a4), and keeperDropFood
  // (0x507ff8): what the animals on that food need (cNeededFood, type
  // +0x220, each) less what's there of it (0x4a85da), over 0
  bool hungry;
  bool carnivore = false;
  std::string food;
  if (ex->animals > 0 && this->animals) {
    if (m.phase == 1 || m.phase == 20) { // (just inside, or at a tank's surface)
      m.feedAnimal = ex->show ? -1 : animalToFeed(m, m.visit);
      m.dropFood.clear();
      if (const Animals::Member *a = this->animals->member(m.feedAnimal))
        m.dropFood = this->animals->keeperFoodOf(a->type);
    }
    food = m.dropFood.empty() ? foodFor(m.visit, fences, carnivore) : m.dropFood;
    carnivore = food == "carnchow";
    float need = 0;
    for (const Animals::Member &a : this->animals->members())
      if (a.exhibit == m.visit && !a.boxed && this->animals->keeperFoodOf(a.type) == food)
        need += static_cast<float>(this->animals->types()[a.type].neededFood);
    hungry = m.feedAnimal >= 0 && need - this->items->foodIn(m.visit, food) > 0;
  } else {
    food = foodFor(m.visit, fences, carnivore);
    hungry = this->items->foodIn(m.visit) < ex->animalCount() * ZooItems::kFoodPerAnimal * 0.5f;
  }
  if(ex->show)hungry=false; // Performers receive show rewards; ordinary food piles belong in home tanks.
  ZT_STEP("0x507ff8", "Food down? (what its animals need, cNeededFood each, against what's there)",
          {"phase", m.phase}, {"needs food", hungry ? "yes" : "no"}, {"food there", this->items->foodIn(m.visit)}, {"chow", food});
  // What a pile costs (its cPurchaseCost), paid as it goes down
  auto buy = [&] {
    auto it = this->chowPrice.find(food);
    if (it == this->chowPrice.end()) {
      int price = 0;
      if (this->rm)
        if (IniReader *ai = this->rm->getIniReader(ZooItems::foodFile(food))) {
          price = ai->getInt("characteristics/integers", "cpurchasecost", 0);
          delete ai;
        }
      it = this->chowPrice.emplace(food, price).first;
    }
    double price = this->foodPrice ? this->foodPrice(food, it->second) : it->second;
    this->upkeep += price;
    ex->upkeepNow += price;
    ex->upkeepTotal += price;
  };
  // A tank (marine specialists): over the wall at its gate onto the water,
  // under it for the work, up and out again - as the original was seen
  // doing it: Task 0x7e Job20_ClimbUp (0x49ae22), Task 0x6e's duties
  // (0x42400a: Job29_ClimbDownB first when at the surface, 0x49f141), the
  // tank's sweep (Job24_GoCleanTank, Job25_CleanTank), then Task 0x7f
  // Job23_GoToPlatformExitTank, Job28_ClimbUpB, Job21_ClimbDown
  const bool tank = ex->tank;
  const float depth = tank && this->tankDepth ? this->tankDepth(m.visit) : 0.0f;
  const bool murky = tank && ex->water > 0 && ex->purity < kMurkyPurity;
  // A tile of the tank at random (where a sweep goes)
  auto anyTankTile = [&](float &tx, float &ty) {
    if (ex->tiles.empty())
      return false;
    auto it = ex->tiles.begin();
    std::advance(it, std::uniform_int_distribution<size_t>(0, ex->tiles.size() - 1)(this->rng));
    auto [x, y] = *it;
    tx = x + 0.5f;
    ty = y + 0.5f;
    return true;
  };
  if (tank)
    switch (m.phase) {
    case 0: { // outside the gate: up the wall to its rim (Job20_ClimbUp
      // 0x49ae22: put at the wall's edge - tankClimbTop 0x499e80 - then up
      // it to the rim, 0x499f95: the walls' height, tankEntryPoint's z;
      // bClimbUp: fFly(climb, 33))
      m.access = m.visit;
      m.tank = m.visit;
      // (still "Going to %s": task 0x7e - ZTKeeper::statusText 0x471399
      // gives 10303 for tasks 0x65 and 0x7e; zoo.exe never shows 10307)
      m.dutyId = kGoingTo;
      m.dutyArg = ex->name;
      if (this->animals)
        this->animals->keeperArrives(m.visit);
      const float ex0 = (m.inX + m.outX) * 0.5f, ey0 = (m.inY + m.outY) * 0.5f;
      const float ground = groundAt(map, m.outX, m.outY);
      m.x = ex0 + (m.outX - ex0) * 0.2f;
      m.y = ey0 + (m.outY - ey0) * 0.2f;
      m.z = ground - groundAt(map, m.x, m.y);
      ZT_STEP("0x49ae22", "Climbs up the tank's wall to its rim (Job20_ClimbUp, bClimbUp)",
              {"tank", ex->name}, {"rim", ex->wallTop()}, {"ground", ground}, {"water depth", depth});
      m.phase = 30;
      climb(m, ex0, ey0, ex->wallTop() - groundAt(map, ex0, ey0), false, false, 1, map);
      return;
    }
    case 30: { // at the rim: down the inside onto the water (Job21_ClimbDown,
      // 0x49a944: from the rim to tankClimbTop - the water's surface;
      // bClimbDown: fFlyReverse(climb, 33))
      const float ex0 = (m.inX + m.outX) * 0.5f, ey0 = (m.inY + m.outY) * 0.5f;
      const float sx = ex0 + (m.inX - ex0) * 0.2f, sy = ey0 + (m.inY - ey0) * 0.2f;
      ZT_STEP("0x49a944", "Climbs down the inside onto the water (Job21_ClimbDown, bClimbDown)",
              {"tank", ex->name}, {"surface", ex->floorHeight + depth});
      m.phase = 20;
      climb(m, sx, sy, ex->floorHeight + depth - groundAt(map, sx, sy), true, true, 2, map);
      return;
    }
    case 20: { // at the surface: under the water for the work, if there's any
      bool heal = false;
      if (this->animals)
        for (const Animals::Member &a : this->animals->members())
          heal = heal || (a.exhibit == m.visit && !a.boxed && this->animals->needsHealing(a));
      bool dung = !dungFor(m, m.visit).empty();
      ZT_STEP("0x42400a", "At the surface: work under the water? (heal, food, dung, murky water)",
              {"to heal", heal ? 1 : 0}, {"needs food", hungry ? 1 : 0}, {"dung", dung ? 1 : 0},
              {"purity", ex->purity}, {"murky", murky ? 1 : 0});
      if (heal || hungry || dung || murky) {
        // (bClimbDown: fFlyReverse(climb, 33), Job29_ClimbDownB)
        m.phase = 1;
        climb(m, m.x, m.y, 0.0f, true, true, 3, map);
        return;
      }
      // (nothing under it: back up the wall from the surface and out)
      m.phase = 27;
      visitStep(m, map, fences);
      return;
    }
    case 22: { // the water murky: off to sweep it (Job24_GoCleanTank)
      float tx, ty;
      m.dutyId = kCleaningTank;
      m.dutyArg.clear();
      if (anyTankTile(tx, ty) &&
          findPath(m, static_cast<int>(std::floor(tx)), static_cast<int>(std::floor(ty)), map, fences, tx, ty)) {
        m.phase = 23;
        return;
      }
      m.phase = 24;
      visitStep(m, map, fences);
      return;
    }
    case 23: { // a sweep across it (Job25_CleanTank, bCleanTankSub: moving
      // with subclean). zoo.exe 0x5961e4 picks bCleanTankSurf when the
      // specialist's water state +0x258 is 3, bCleanTankSub when 4 (fPlaySetTank
      // 0x5b2be5's surface and under the water: ours' m.water 2 and 3); it's
      // under the water by now (case 20 took it down), so always the Sub
      float tx, ty;
      if (anyTankTile(tx, ty) &&
          findPath(m, static_cast<int>(std::floor(tx)), static_cast<int>(std::floor(ty)), map, fences, tx, ty)) {
        m.sweeping = true;
        m.phase = 25;
        return;
      }
      m.phase = 25;
      visitStep(m, map, fences);
      return;
    }
    case 25: // swept: cCleanTankPct cleaner (zoo.exe 0x47f61f -> 0x49bd60: the
      // water's purity +0x1a8 up by the type's +0x204), again while it's under
      // cCleanTankThreshold (Job25_CleanTank's atTarget 0x47f64a: +0x208)
      m.sweeping = false;
      ex->purity = std::min(100.0f, ex->purity + static_cast<float>(t.cleanTankPct));
      ZT_STEP("0x47f64a", "A sweep of the tank (cCleanTankPct cleaner)", {"purity", ex->purity},
              {"cCleanTankPct", t.cleanTankPct}, {"cCleanTankThreshold", t.cleanTankThreshold});
      m.phase = ex->purity < static_cast<float>(t.cleanTankThreshold) ? 23 : 24;
      visitStep(m, map, fences);
      return;
    case 24: // done: back under the platform (Job23_GoToPlatformExitTank)
      // (a sweep or the way to one given up: zoo.exe 0x5ec593 "Stopped
      // cleaning the tank", 0x5ec663 "Stopped going to clean the tank" - the
      // tasks' stop handlers only set the unit's debug text, 0x404fcf)
      m.dutyId = kExiting;
      m.dutyArg.clear();
      m.phase = 27;
      if (findPath(m, static_cast<int>(std::floor(m.inX)), static_cast<int>(std::floor(m.inY)), map, fences,
                   m.inX, m.inY))
        return;
      [[fallthrough]];
    case 27: { // up the inside of the wall to its rim (Job28_ClimbUpB:
      // bClimbUp)
      const float ex0 = (m.inX + m.outX) * 0.5f, ey0 = (m.inY + m.outY) * 0.5f;
      m.phase = 26;
      climb(m, ex0, ey0, ex->wallTop() - groundAt(map, ex0, ey0), false, true, 1, map);
      return;
    }
    case 26: { // from the rim down the outside to the ground (Job21_ClimbDown:
      // tankClimbTop toward the tile outside, its ground; bClimbDown)
      const float ex0 = (m.inX + m.outX) * 0.5f, ey0 = (m.inY + m.outY) * 0.5f;
      const float sx = ex0 + (m.outX - ex0) * 0.2f, sy = ey0 + (m.outY - ey0) * 0.2f;
      m.dutyId = kExiting;
      m.phase = 29;
      climb(m, sx, sy, groundAt(map, m.outX, m.outY) - groundAt(map, sx, sy), true, false, 0, map);
      return;
    }
    case 29: // out
      m.tank = -1;
      m.water = 0;
      m.z = 0;
      this->lastVisit[m.visit] = this->clock;
      done();
      return;
    default:
      break; // (food, healing, dung: as a keeper's, under the water)
    }
  switch (m.phase) {
  case 0: { // outside the gate: in through it
    // zoo.exe 0x49f644 (Task65_KeeperGoToHabitat's atTarget): not a tank,
    // a keeper at its duties there (0x4a89cb: +0x132) and more than one
    // (+0xf0 over 1) - "Aborted going to habitat", the job ended (slot 0x174)
    // ours: looked at by the gate, where the original's keeper is just
    // inside it
    int inDuties = 0, headed = 0;
    keepersAt(m.visit, m.id, fences, inDuties, headed);
    if (inDuties > 1) {
      ZT_STEP("0x49f644", "Aborted going to habitat: more than one keeper at their duties there",
              {"exhibit", ex->name}, {"at duties", inDuties});
      done();
      return;
    }
  }
    m.access = m.visit;
    m.phase = 1;
    // (still "Going to %s" till it's inside: Task 0x65 runs to a tile in
    // the habitat - 0x4a8a23 - and ZTKeeper::statusText 0x471399 gives 10303
    // for it; 10307 "Entering %s" is in no push in zoo.exe)
    m.dutyId = kGoingTo;
    m.dutyArg = ex->name;
    if (this->animals)
      this->animals->keeperArrives(m.visit);
    if (!findPath(m, static_cast<int>(m.inX), static_cast<int>(m.inY), map, fences, m.inX, m.inY))
      done();
    return;
  case 1: // just inside: food, if it's low - to a free tile within 3 of
    // it, at random (zoo.exe 0x4e7a7d: nothing on it, not the gate's own
    // tile, not one on its list of tiles it found no way to, +0x27c), else
    // the nearest free one. Job1_GoDropFood (0x4993b9) takes that one spot:
    // no way to it, and the tile goes on the list ("Couldn't reach place to
    // put food") and the food job ends; once the list holds 20 tiles it's
    // emptied, the animal goes on its +0x270 list and the keeper says it
    // can't get there (11004, priority 2); both lists are emptied as its
    // duties in a habitat begin and end. Job1's atTarget (0x508396) then
    // starts Job2_PutDownFood (0x508448) at the spot.
    if (hungry) {
      int kx = static_cast<int>(std::floor(m.x)), ky = static_cast<int>(std::floor(m.y));
      int gx = static_cast<int>(std::floor(m.inX)), gy = static_cast<int>(std::floor(m.inY));
      std::vector<std::pair<int, int>> near, all;
      for (auto [x, y] : ex->tiles) {
        if ((x == gx && y == gy) || !walkable(x, y, map, fences, m.visit))
          continue;
        if (std::find(m.invalid.begin(), m.invalid.end(), std::make_pair(x, y)) != m.invalid.end())
          continue;
        bool taken = false;
        for (int id : this->items->ofKind(ZooItems::Kind::Food, m.visit))
          if (const ZooItems::Item *it = this->items->item(id))
            taken = taken || (static_cast<int>(std::floor(it->x)) == x && static_cast<int>(std::floor(it->y)) == y);
        for (int id : this->items->ofKind(ZooItems::Kind::Dung, m.visit))
          if (const ZooItems::Item *it = this->items->item(id))
            taken = taken || (static_cast<int>(std::floor(it->x)) == x && static_cast<int>(std::floor(it->y)) == y);
        if (taken)
          continue;
        all.push_back({x, y});
        if ((x - kx) * (x - kx) + (y - ky) * (y - ky) < 10)
          near.push_back({x, y});
      }
      std::shuffle(near.begin(), near.end(), this->rng);
      std::sort(all.begin(), all.end(), [&](const std::pair<int, int> &a, const std::pair<int, int> &b) {
        return (a.first - kx) * (a.first - kx) + (a.second - ky) * (a.second - ky) <
               (b.first - kx) * (b.first - kx) + (b.second - ky) * (b.second - ky);
      });
      near.insert(near.end(), all.begin(), all.end());
      if (!near.empty()) {
        auto [x, y] = near.front();
        if (findPath(m, x, y, map, fences, x + 0.5f, y + 0.5f)) {
          m.dropX = x + 0.5f;
          m.dropY = y + 0.5f;
          m.phase = 9;
          m.dutyId = kPlacingFood;
          m.dutyArg.clear();
          return;
        }
        ZT_STEP("0x4993b9", "Couldn't reach place to put food: the tile on its list", {"x", x}, {"y", y},
                {"tiles on the list", static_cast<int>(m.invalid.size()) + 1});
        m.invalid.push_back({x, y});
        if (m.invalid.size() >= 20) {
          m.invalid.clear();
          // (the job's own animal, the one being fed - 0x49efa6's out)
          if (this->animals)
            for (const Animals::Member &a : this->animals->members())
              if (a.exhibit == m.visit && !a.boxed && (m.feedAnimal < 0 || a.id == m.feedAnimal)) {
                m.unreachable.push_back(a.id); // (staffAddUnreachable 0x6110b6)
                pathTrouble(m, a.name, false);
                break;
              }
        }
      }
    }
    m.phase = 3;
    visitStep(m, map, fences);
    return;
  case 9: // at the spot: the food down (bFeedHerb, or bFeedCarn for
    // carnchow alone; a tank's bFeedTank, subfeed three times - zoo.exe
    // 0x5084b3)
    m.phase = 2;
    if (tank)
      play(m, "subfeed", 3);
    else
      playSet(m, carnivore ? "bfeedcarn" : "bfeedherb", carnivore ? "feedc" : "feedh", 3);
    return;
  case 2: // food down where it stands
    if(!ex->show) {
      this->items->addFood(m.visit, m.dropX, m.dropY, ex->animalCount(), food);
      buy();
    }
    ZT_STEP("0x4e7a7d", "Puts the food down", {"chow", food}, {"x", m.dropX}, {"y", m.dropY},
            {"food there now", this->items->foodIn(m.visit)});
    // (its duties come round again, the food duty first: another species
    // with none of its own food is fed next - Task 0x6e 0x42400a; ours:
    // the lab's stand-in animals had theirs)
    m.phase = ex->animals > 0 && this->animals ? 1 : 3;
    visitStep(m, map, fences);
    return;
  case 7: { // healed (bHeal): health full; its price's 20% charged
    if (this->animals)
      if (const Animals::Member *a = this->animals->member(m.jobAnimal)) {
        double vet = this->animals->types()[a->type].cost * 0.2;
        this->upkeep += vet;
        ex->upkeepNow += vet;
        ex->upkeepTotal += vet;
        // Once a scientist has been named "Dr. Frankenstein" (0x64e83a), 1
        // heal in 10 (Job4_HealAnimal::atTarget 0x4702e8: rand() % 10)
        // makes it another kind: one of the egg-layers' types (cLaysEggs,
        // +0x404) of its subtype (world slot 0x14: "animals" of its +0xa4)
        // that isn't its own (cNameID), at random; slot 0x7c (0x602929) its
        // type and art, its exhibit to be rated again (+0x31) - then healed
        // as that one
        if (RenameCheats::flags().frankenstein &&
            std::uniform_int_distribution<int>(0, 0x7fff)(this->rng) % Original::Cheat::frankensteinOneIn == 0)
          this->animals->becomeAnotherKind(a->id);
        this->animals->heal(a->id);
        ZT_STEP("", "Heals an animal (20% of its price)", {"animal", a->name}, {"vet bill", vet});
      }
    m.jobAnimal = -1;
    m.phase = 3;
    visitStep(m, map, fences);
    return;
  }
  case 8: { // beside the one to heal
    const Animals::Member *a = this->animals ? this->animals->member(m.jobAnimal) : nullptr;
    if (a)
      face(m, a->x, a->y);
    m.phase = 7;
    // ("Healing animal", or "Visiting eggs" for an egg: the keeper's status
    // line, ZTKeeper::statusText 0x471399, jobs 3 and 4)
    m.dutyId = a && a->egg ? 10321 : 10301;
    // (a tank's: bHeal's fPlaySetTank(bSurfheal, bSubheal), under the
    // water; an egg, bHealEgg - the scientist's healegg, then backwards:
    // Job4_HealAnimal's begin, zoo.exe 0x47002e, by the animal's +0x395)
    if (tank)
      play(m, "subheal", 1);
    else
      playSet(m, a && a->egg ? "bhealegg" : "bheal", "heal", 1);
    return;
  }
  case 3: { // one to heal first, then dung to rake: the nearest pile
    // Job3_GoHealAnimal (0x508b3c): the animal is 0x4705b9's - of the
    // habitat's animals needing it (0x423f00), the nearest by squared tile
    // distance from the keeper, the first on a tie, leaving out those on its
    // +0x270 list. No way to it: onto that list ("Couldn't reach animal to
    // heal"), the job ends and the keeper says so (11004, priority 2, the
    // keeper's name and the animal's). The list is emptied as its duties in
    // a habitat begin (0x49f3ca) and end (0x49ebe7). Job3's atTarget
    // (0x470010) starts Job4_HealAnimal (0x470121)
    if (this->animals) {
      const Animals::Member *nearest = nullptr;
      long long best = 0;
      const int kx = static_cast<int>(std::floor(m.x)), ky = static_cast<int>(std::floor(m.y));
      for (const Animals::Member &a : this->animals->members()) {
        if (a.exhibit != m.visit || a.boxed || !this->animals->needsHealing(a) ||
            std::find(m.unreachable.begin(), m.unreachable.end(), a.id) != m.unreachable.end())
          continue;
        const long long dx = static_cast<int>(std::floor(a.x)) - kx, dy = static_cast<int>(std::floor(a.y)) - ky;
        if (!nearest || dx * dx + dy * dy < best) {
          nearest = &a;
          best = dx * dx + dy * dy;
        }
      }
      if (nearest) {
        m.jobAnimal = nearest->id;
        m.phase = 8;
        m.dutyId = nearest->egg ? 10321 : 10301; // (0x471399)
        if (findPath(m, static_cast<int>(std::floor(nearest->x)), static_cast<int>(std::floor(nearest->y)), map,
                     fences, nearest->x + 0.3f, nearest->y + 0.3f))
          return;
        ZT_STEP("0x508b3c", "Couldn't reach animal to heal: on its list, and it says so", {"animal", nearest->name});
        m.unreachable.push_back(nearest->id); // (staffAddUnreachable 0x6110b6)
        m.jobAnimal = -1;
        pathTrouble(m, nearest->name, false);
        m.phase = 3;
        visitStep(m, map, fences);
        return;
      }
    }
    m.jobItem = -1;
    float best = 1e9f;
    // (keeperPickDung 0x4a2ab8: only the dirt its cDirt lists, slot 0x324)
    for (int id : dungFor(m, m.visit)) {
      const ZooItems::Item *it = this->items->item(id);
      // (not one on a tile it found no way to: 0x4a2ab8 reads +0x27c)
      if (std::find(m.invalid.begin(), m.invalid.end(),
                    std::make_pair(static_cast<int>(std::floor(it->x)), static_cast<int>(std::floor(it->y)))) !=
          m.invalid.end())
        continue;
      float d = std::hypot(it->x - m.x, it->y - m.y);
      if (d < best) {
        best = d;
        m.jobItem = id;
      }
    }
    // Job5_GoCleanDirt (0x412629): no way to the pile, and its tile goes on
    // the keeper's list of tiles it found no way to (+0x27c: "Couldn't reach
    // dirt to clean"), the job ends and the keeper says so (11004, priority
    // 2, its name and the habitat's); the pile stays. Job5's atTarget
    // (0x426adb) starts Job6_CleanDirt (0x42f860)
    if (m.jobItem >= 0) {
      const ZooItems::Item *it = this->items->item(m.jobItem);
      m.phase = 4;
      m.dutyId = kRaking;
      if (findPath(m, static_cast<int>(std::floor(it->x)), static_cast<int>(std::floor(it->y)), map,
                   fences, it->x + 0.2f, it->y + 0.2f))
        return;
      ZT_STEP("0x412629", "Couldn't reach dirt to clean: the tile on its list, the pile stays", {"pile", m.jobItem});
      m.invalid.push_back({static_cast<int>(std::floor(it->x)), static_cast<int>(std::floor(it->y))});
      m.jobItem = -1;
      pathTrouble(m, ex->name, false);
      m.phase = 3;
      visitStep(m, map, fences);
      return;
    }
    // A tank: its water swept if it's murky, then out by the platform
    if (tank) {
      m.phase = murky ? 22 : 24;
      visitStep(m, map, fences);
      return;
    }
    // Out again. measured: 2026-11-16 (#216), the original's zookeeper,
    // its duties in a paddock done, is in task 1 at once where it stands
    // (no exit task for a land habitat: Task 0x6e ends, the chooser 0x423d04
    // finds nothing to go to, slot 0x310 0x49dafe gives task 1), "Monitoring
    // zoo" (ZTKeeper::statusText 0x471399: 10304 for task 1 - 10306 only for
    // a tank's task 0x7f), and its walk about (0x43d948, a tile in a ring
    // round it) had it outside the paddock 10 s later.
    if (!Features::staffPaths) {
      done();
      walkAbout(m, map, fences);
      return;
    }
    // addition (staffpaths): out by the gate, to keep to the paths
    m.phase = 6;
    m.dutyId = kMonitoring;
    m.dutyArg.clear();
    if (!findPath(m, static_cast<int>(m.outX), static_cast<int>(m.outY), map, fences, m.outX,
                  m.outY))
      done();
    return;
  }
  case 4: // at the pile: rake it (bClean: a keeper's clean four times, a
    // scientist's clean twice with the vac, then idle). zoo.exe 0x42f8cb
    // (Job6_CleanDirt's atTarget): the dirt there, if it cleans it (slot
    // 0x324), faced (faceToward 0x426d4d) and bClean; none there (raked by
    // another), the job ends (slot 0x2d8)
    if (const ZooItems::Item *it = this->items->item(m.jobItem)) {
      face(m, it->x, it->y);
    } else {
      m.jobItem = -1;
      m.phase = 3;
      visitStep(m, map, fences);
      return;
    }
    m.phase = 5;
    // (a marine specialist's bClean: subpoo twice)
    if (tank)
      play(m, "subclean", 2);
    else
      playSet(m, "bclean", "clean", 4);
    return;
  case 5: // raked
    ZT_STEP("", "Rakes up a dung pile", {"pile", m.jobItem});
    if (const ZooItems::Item *it = this->items->item(m.jobItem))
      this->dungRecycled(it->x, it->y);
    this->items->remove(m.jobItem);
    m.jobItem = -1;
    m.phase = 3;
    visitStep(m, map, fences);
    return;
  default: // out
    this->lastVisit[m.visit] = this->clock;
    done();
    return;
  }
}

// ----------------------------------------------------------------------------
// Tour guides. The trip (zoo.exe 0x58ce7e, the guide's task chooser: task
// 0x76 via 0x58ae04 when the zoo has habitats; the target picked on the
// task's first tick, 0x58cf31 -> 0x58b0a3): a habitat at random - one of
// its assigned ones, else any (slot 0x328, 0x58af73) - none with animals,
// the trip's given up (0x58ae2c: entries with +0x54 > 0) and another picked
// next time; then one of its viewing areas at random (0x58cf31: rand %
// count). On the way, every cCrowdCheck ticks (tick 0x58ceb9), guests within
// cCrowdRadius may follow (0x58d349: cFollowChance, raised 15 by each man
// near a woman guide and 10 by each woman near a man as the guests are
// looked at - never put back within one look; cMaxGroupSize at most); there
// with a group, the talk (task 0x77: speak, looped to cover cInformGuestTime)
// and its followers' happiness up cTourGuideBonus; with none, the trip ends
// (0x58ad88)
// ----------------------------------------------------------------------------
bool Staff::tourWork(Member &m, const WorldMap &map, Fences &fences) {
  if (!this->guests)
    return false;
  ZT_FN("0x58ce7e", "A tour guide picks a trip");
  std::vector<int> choices;
  if (!m.exhibits.empty()) {
    choices = m.exhibits;
  } else {
    // Unassigned: every habitat with animals, tanks too (0x58af73 ->
    // 0x440d9a: the habitat list's entries whose +0x94 - its animal count,
    // zeroed and counted up animal by animal in the rating 0x444827 - isn't
    // 0)
    for (const Fences::Exhibit &ex : fences.exhibits())
      if (ex.named && ex.animals > 0)
        choices.push_back(ex.id);
  }
  if (choices.empty())
    return false;
  int pick = choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(this->rng)];
  // (one without animals: no trip this time - zoo.exe 0x58ae2c, 0x58b0a3)
  const Fences::Exhibit *picked = fences.exhibit(pick);
  if (!picked || picked->animals <= 0)
    return false;
  // Its viewing areas with guests at them (0x58ae2c: +0x54 over 0) that see
  // some of it (0x43aac6); none, no trip. Then, 50/50 (0x58b0a3), one of them
  // at random (0x428741) or the one whose viewing tile, one picked at random
  // in each, is nearest the guide (0x58b46e: squared tiles, the first on a
  // tie)
  this->viewing.refresh(map, fences);
  this->viewing.countGuests(*this->guests);
  const auto &areas = this->viewing.areas();
  std::vector<int> looked;
  for (int a : this->viewing.of(pick))
    if (areas[a].guests > 0 && !areas[a].seen.empty() && !areas[a].viewing.empty())
      looked.push_back(a);
  ZT_STEP("0x58ae2c", "Its viewing areas with guests at them", {"areas", static_cast<int>(this->viewing.of(pick).size())},
          {"with guests", static_cast<int>(looked.size())});
  if (looked.empty())
    return false;
  int area = -1;
  if (std::uniform_int_distribution<int>(0, 1)(this->rng) == 0) {
    area = looked[std::uniform_int_distribution<size_t>(0, looked.size() - 1)(this->rng)];
    ZT_STEP("0x428741", "One of them at random");
  } else {
    const int gx = static_cast<int>(std::floor(m.x)), gy = static_cast<int>(std::floor(m.y));
    long long best = 0;
    for (int a : looked) {
      const auto &v = areas[a].viewing;
      const auto t = v[std::uniform_int_distribution<size_t>(0, v.size() - 1)(this->rng)];
      const long long d = static_cast<long long>(t.first - gx) * (t.first - gx) +
                          static_cast<long long>(t.second - gy) * (t.second - gy);
      if (area < 0 || d < best) {
        area = a;
        best = d;
      }
    }
    ZT_STEP("0x58b46e", "The nearest of them", {"squared tiles", static_cast<float>(best)});
  }
  // (a tile of it at random - zoo.exe 0x58cf31: rand % count)
  const auto &tiles = areas[area].viewing;
  std::pair<int, int> spot = tiles[std::uniform_int_distribution<size_t>(0, tiles.size() - 1)(this->rng)];
  if (!findPath(m, spot.first, spot.second, map, fences, spot.first + 0.5f, spot.second + 0.5f))
    return false;
  // A new trip: the last group goes, a new one gathers
  this->guests->releaseTour(m.id);
  m.followers.clear();
  m.job = Job::Tour;
  m.visit = pick;
  m.goalX = spot.first;
  m.goalY = spot.second;
  m.speaking = false;
  m.dutyId = kGoingToExhibit;
  const Fences::Exhibit *ex = fences.exhibit(pick);
  m.dutyArg = ex ? ex->name : "";
  m.crowdClock = static_cast<float>(std::uniform_int_distribution<int>(0, this->typeList[m.type].crowdCheck - 1)(this->rng));
  this->gatherFollowers(m);
  return true;
}

void Staff::gatherFollowers(Member &m) {
  const Type &t = this->typeList[m.type];
  if (!this->guests) return;
  m.followers.erase(std::remove_if(m.followers.begin(), m.followers.end(), [&](int id) {
    const auto *g = this->guests->guest(id);
    return !g || g->guide != m.id || g->leaving || g->building >= 0 ||
           g->state == Guests::State::Fleeing || g->state == Guests::State::Caught;
  }), m.followers.end());
  if (static_cast<int>(m.followers.size()) >= t.maxGroup) return;
  int gx = static_cast<int>(std::floor(m.x)), gy = static_cast<int>(std::floor(m.y));
  // (zoo.exe 0x58d349: the chance is raised by each guest that matches and
  // kept for the rest of the look - one chance for the whole scan)
  // The tiles within cCrowdRadius in the original's order: x from the
  // guide's minus the radius up, and down each column y (0x58d349's two
  // loops). (A second way in there - a guest not on a tour, +0x337 0, whose
  // +0x2e4 is this guide - never comes up: the guest's clearGoal 0x422763
  // and its setup 0x4f65e5 clear both together; read 2026-11-15)
  int chance = t.followChance;
  std::vector<const Guests::Guest *> near;
  for (const Guests::Guest &g : this->guests->guests()) {
    int x = static_cast<int>(std::floor(g.x)), y = static_cast<int>(std::floor(g.y));
    if (std::abs(x - gx) <= t.crowdRadius && std::abs(y - gy) <= t.crowdRadius)
      near.push_back(&g);
  }
  std::stable_sort(near.begin(), near.end(), [](const Guests::Guest *a, const Guests::Guest *b) {
    int ax = static_cast<int>(std::floor(a->x)), bx = static_cast<int>(std::floor(b->x));
    if (ax != bx) return ax < bx;
    return static_cast<int>(std::floor(a->y)) < static_cast<int>(std::floor(b->y));
  });
  for (const Guests::Guest *gp : near) {
    const Guests::Guest &g = *gp;
    if (static_cast<int>(m.followers.size()) >= t.maxGroup)
      break;
    bool woman = this->guests->types()[g.type].female;
    chance += (m.female && !woman ? 15 : 0) + (!m.female && woman ? 10 : 0);
    if (std::uniform_int_distribution<int>(0, 99)(this->rng) >= chance)
      continue;
    if (this->guests->joinTour(g.id, m.id, m.visit, m.goalX, m.goalY))
      m.followers.push_back(g.id);
  }
}

// Dung taken away: paid for (Recycling) if there's a compost anywhere, and
// the nearest compost's takings (its Building Information) up its price
// zoo.exe 0x4a2c98 (Recycling: cZooDooRecyclingAmount a pile with a compost)
void Staff::dungRecycled(float x, float y) {
  if (!this->objects)
    return;
  PlacedObjects::Object *nearest = nullptr;
  float best = 1e30f;
  for (PlacedObjects::Object &o : this->objects->all()) {
    if (o.typeName != "compost")
      continue;
    float d = std::fabs(o.x - x) + std::fabs(o.y - y);
    if (d < best) {
      best = d;
      nearest = &o;
    }
  }
  if (!nearest)
    return;
  nearest->income += std::max(0.0f, nearest->price);
  nearest->visitorsNow++;
  nearest->visitorsTotal++;
  this->recycling += Original::economy().zooDooRecycling; // economy.cfg cZooDooRecyclingAmount
}

bool Staff::guideSpeaking(int id) const {
  for (const Member &m : this->list)
    if (m.id == id)
      return m.job == Job::Tour && m.speaking;
  return false;
}

int Staff::guideBonusAt(const std::vector<std::pair<int, int>> &tiles) const {
  for (const Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    if (t.kind != Kind::Guide || m.id == this->carried)
      continue;
    std::pair<int, int> at = {static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y))};
    if (std::find(tiles.begin(), tiles.end(), at) != tiles.end())
      return t.tourBonus;
  }
  return 0;
}

int Staff::assign(int id, int exhibit, const Fences &fences) {
  Member *m = this->member(id);
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!m || !ex)
    return 0;
  Kind k = this->typeList[m->type].kind;
  if (k == Kind::Trainer) {
    if (!ex->tank)
      return 10328; // "Marine specialists can only be assigned to tank exhibits with marine animals."
  } else if (k == Kind::Scientist) {
    if (ex->animalCount() > 0 && !ex->forScientist())
      return 10324; // "Scientists can only be assigned to land exhibits with dinosaurs."
  } else if (k == Kind::Keeper) {
    if (ex->animalCount() > 0 && !ex->forKeeper())
      return 10320; // "Zookeepers can only be assigned to land exhibits with zoo animals."
  } else if (k == Kind::Guide) {
    // (no checks for tour guides: zoo.exe 0x423e01)
    if (std::find(m->exhibits.begin(), m->exhibits.end(), exhibit) == m->exhibits.end())
      m->exhibits.push_back(exhibit);
    m->workCheck = 0;
    return 0;
  } else {
    return 0;
  }
  bool linkedPerformers=false;
  if(k==Kind::Trainer && ex->show)for(const auto &[edge,piece]:fences.pieces()) {
    int show=-1,home=-1;
    if(fences.showPortal(edge,show,home) && show==exhibit)
      if(const auto *linked=fences.exhibit(home);linked && linked->animalCount()>0){linkedPerformers=true;break;}
  }
  if (ex->animalCount() <= 0 && !linkedPerformers)
    return 10329; // Empty tanks remain invalid unless this show has linked performers.
  if (std::find(m->exhibits.begin(), m->exhibits.end(), exhibit) == m->exhibits.end())
    m->exhibits.push_back(exhibit);
  m->workCheck = 0;
  return 0;
}

// ours: the dev console sends one to a tile
bool Staff::walkTo(int id, int x, int y, const WorldMap &map, const Fences &fences) {
  Member *m = this->member(id);
  if (!m)
    return false;
  m->job = Job::None;
  m->playsLeft = 0;
  m->walksLeft = 0;
  m->workCheck = 30.0f; // (not straight off to other work)
  return findPath(*m, x, y, map, fences, x + 0.5f, y + 0.5f);
}

// ----------------------------------------------------------------------------
// Escaped animals (zoo.exe 0x50d05f, 0x50da26, 0x50d345): the nearest
// unclaimed one, run to within 5 tiles (cWeaponRange), dart it ("Tranquilizing
// %s", gun.wav), run to it and crate it where it lies ("Containing %s"); the
// crate waits for the player to put it in an exhibit
// ----------------------------------------------------------------------------
bool Staff::catchWork(Member &m, const WorldMap &map, const Fences &fences) {
  if (!this->animals)
    return false;
  ZT_FN("0x50d05f", "An escaped animal to catch?");
  const Animals::Member *target = nullptr;
  float best = 1e9f;
  for (const Animals::Member &a : this->animals->members()) {
    if (!a.escaped || a.boxed || a.dying || a.id == this->animals->carried)
      continue;
    const bool dinosaur = this->animals->types()[a.type].dinosaur;
    const Kind kind = this->typeList[m.type].kind;
    if (dinosaur ? (kind != Kind::Scientist || !a.tranquilised) : kind != Kind::Keeper) continue;
    if (a.claimedBy >= 0 && a.claimedBy != m.id && this->member(a.claimedBy))
      continue;
    const MapTile *t = map.getTile(static_cast<int>(a.x), static_cast<int>(a.y));
    if (t && (t->terrainType == 9 || t->terrainType == 10))
      continue;
    float d = std::fabs(a.x - m.x) + std::fabs(a.y - m.y);
    if (d < best) {
      best = d;
      target = &a;
    }
  }
  if (!target)
    return false;
  ZT_STEP("0x50d05f", "After the nearest escaped animal", {"animal", target->name}, {"distance", best},
          {"tranquilised", target->tranquilised ? 1 : 0});
  if (Animals::Member *a = this->animals->member(target->id))
    a->claimedBy = m.id;
  m.job = Job::Catch;
  m.jobAnimal = target->id;
  m.phase = target->tranquilised ? 1 : 0;
  m.dutyId = m.phase ? 10309 : 10308;
  m.dutyArg = target->name;
  m.repath = 0;
  m.walksLeft = 0;
  catchStep(m, 0, map, fences);
  return true;
}

void Staff::catchStep(Member &m, float seconds, const WorldMap &map, const Fences &fences) {
  Animals::Member *a = this->animals ? this->animals->member(m.jobAnimal) : nullptr;
  const bool dinosaur = a && this->animals->types()[a->type].dinosaur;
  const bool permitted = dinosaur ? this->typeList[m.type].kind == Kind::Scientist && a->tranquilised
                                  : this->typeList[m.type].kind == Kind::Keeper;
  if (!a || a->boxed || a->dying || !permitted || (!a->escaped && !a->tranquilised)) {
    if (a && a->claimedBy == m.id)
      a->claimedBy = -1;
    m.job = Job::None;
    m.path.clear();
    m.anim = "idle";
    m.idleLeft = 0.5f;
    m.dutyId = kMonitoring;
    return;
  }
  // In range (zoo.exe 0x50cf5b): the squared distance between their tiles
  // at most the range squared - Job13_Tranquilize's checkArrived (0x50cffd)
  // with the keeper type's cWeaponRange (+0x1ec), Job15_BoxAnimal's
  // (0x50d2d8) with 1, back to Job13 if it's woken (+0x393)
  const long long tdx = static_cast<int>(std::floor(a->x)) - static_cast<int>(std::floor(m.x)),
                  tdy = static_cast<int>(std::floor(a->y)) - static_cast<int>(std::floor(m.y));
  const long long d2 = tdx * tdx + tdy * tdy;
  const long long range = static_cast<long long>(this->typeList[m.type].weaponRange);
  if (m.phase == 1 && !a->tranquilised)
    m.phase = 0;
  if (m.phase == 0 && d2 <= range * range) {
    // In range: stop, face it, fire
    ZT_STEP("0x50cffd", "In range (cWeaponRange): fires a dart", {"animal", a->name},
            {"tile distance", std::sqrt(static_cast<float>(d2))}, {"cWeaponRange", static_cast<int>(range)});
    face(m, a->x, a->y);
    m.dutyId = 10308;
    // (bFireWeapon: fire, with its fire_weapon sound)
    playSet(m, "bfireweapon", "fire", 1);
    Sound::get().play("staff/gun", 1000);
    return;
  }
  if (m.phase == 1 && d2 <= 1) {
    ZT_STEP("0x50d345", "Crates it where it lies", {"animal", a->name});
    this->animals->box(a->id);
    m.job = Job::None;
    m.path.clear();
    m.anim = "idle";
    m.idleLeft = 0.5f;
    m.dutyId = kMonitoring;
    return;
  }
  // Running after it (a fresh route now and then, as it moves)
  m.repath -= seconds;
  if (m.repath <= 0 || m.pathAt >= m.path.size()) {
    m.repath = 1.0f;
    int gx = static_cast<int>(std::floor(a->x)), gy = static_cast<int>(std::floor(a->y));
    if (!findPath(m, gx, gy, map, fences, a->x, a->y)) {
      // Nowhere to run to it: give up for now
      a->claimedBy = -1;
      m.job = Job::None;
      m.idleLeft = 2.0f;
      m.dutyId = kMonitoring;
    }
  }
}

// ours: its status line from the lang string it's set (dutyId, %s); none
// set (0), the empty string - as the base ZTStaff::statusText 0x61122f and
// the classes' own for what they don't name (0x638084)
std::string Staff::dutyText(const Member &m) const {
  std::string s = this->rm && m.dutyId ? this->rm->getString(m.dutyId) : "";
  size_t at = s.find("%s");
  if (at != std::string::npos)
    s.replace(at, 2, m.dutyArg);
  return s;
}

// ----------------------------------------------------------------------------
// Hiring
// ----------------------------------------------------------------------------
// (zoo.exe ZTUnitType::create 0x6151b3, class slot 0x5c: a new unit of its
// class, then ZTUnit::setup 0x4f5c58, slot 0x10c - its base's, three of
// its fields cleared. ours: a Member, set up here)
int Staff::hire(int type, float x, float y, const WorldMap &map, const Fences &fences) {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return -1;
  if (canPlaceType(type, x, y, map, fences) != Fences::Fit::Ok)
    return -1;
  const Type &t = this->typeList[type];
  const auto point = placementPoint(type, x, y); x = point.first; y = point.second;
  Member m;
  m.id = this->nextId++;
  m.type = type;
  m.female = t.sexes[1] && (!t.sexes[0] || std::uniform_int_distribution<int>(0, 1)(this->rng) == 1);
  if (!t.hair.empty())
    m.hair = std::uniform_int_distribution<int>(0, static_cast<int>(t.hair.size()) - 1)(this->rng);
  if (!t.skin.empty())
    m.skin = std::uniform_int_distribution<int>(0, static_cast<int>(t.skin.size()) - 1)(this->rng);
  int n = ++this->counts[type];
  const CatalogItem *item = ItemCatalog::get().find(t.file);
  m.name = (item ? item->name : this->rm ? this->rm->getString(t.nameId) : t.key) + " " + std::to_string(n);
  m.x = x;
  m.y = y;
  m.fx = 0;
  m.fy = 1;
  m.idleLeft = 1.0f;
  m.walksLeft = 0;
  // (ZTStaff::setup 0x457a83: its look for a hunter first comes after
  // rand() % cChaseCheck status ticks, +0x28c; a keeper's own, ZTKeeper::
  // setup 0x457b70, runs it then zeroes its own fields +0x294..+0x2a4 - ours
  // starts from an empty Member)
  m.chaseCount = std::uniform_int_distribution<int>(0, std::max(1, t.chaseCheck) - 1)(this->rng);
  m.dutyId = t.kind == Kind::Helicopter ? kWaitingAtBase
             : t.kind == Kind::Guide    ? kGuideMonitoring
             : t.kind == Kind::Maint    ? kMaintMonitoring
                                        : kMonitoring;
  // Maintenance workers start on all their duties; keepers on none
  this->list.push_back(m);
  return m.id;
}

// zoo.exe keeperRemoved 0x458759 (ZTKeeper slot 0x78, not a shutdown):
// the show the specialist works (+0x254, the show tank's habitat id,
// 0x41ebfd) loses it (0x5a9c96) and stops (showStopEvent 0x612d20); then
// the habitat's told (staffNotifyHabitat 0x4a879f) and the base removal
// 0x4586f6
void Staff::fire(int id) {
  if (auto *m = member(id)) { releaseCatch(*m); Sound::get().stop(m->rotorChannel); }
  // A maintenance worker taken away lets its work list entry go, left on
  // the list for another (zoo.exe 0x502078, ZTMaintenanceWorker slot 0x78:
  // not a shutdown - its +0x2c0 and the entry's +8 cleared)
  for (WorkEntry &w : this->work)
    if (w.holder == id)
      w.holder = -1;
  if (this->shows)
    this->shows->specialistRemoved(id);
  if (this->guests)
    this->guests->releaseTour(id);
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [&](const Member &m) { return m.id == id; }),
                   this->list.end());
  if (this->selected == id)
    this->selected = -1;
  if (this->carried == id)
    this->carried = -1;
}

// ours: put down again after Move Staff Member, its work dropped
void Staff::moveTo(int id, float x, float y) {
  if (Member *m = this->member(id)) {
    releaseCatch(*m);
    Sound::get().stop(m->rotorChannel); m->rotorChannel = -1;
    m->heliStage = HeliStage::Parked;
    const auto point = placementPoint(m->type, x, y);
    m->x = point.first;
    m->y = point.second;
    m->path.clear();
    m->job = Job::None;
    m->visit = m->access = -1;
    m->playsLeft = 0;
    m->layer = 0;
    m->pathLayers.clear();
    m->anim = "idle";
    m->idleLeft = kIdleSeconds;
    m->tank = -1;
    m->water = 0;
    m->z = 0;
    m->climbing = m->sweeping = false;
  }
}

// zoo.exe animalFindPredator 0x4369b9 (a staff member's slot 0x2fc) with
// its prey test, slot 0x304 0x4391a2: an animal (not an egg, +0x395) of a
// grown subtype (m or f - a young one only with cBabiesAttack, type
// +0x3d4) whose cPrey lists the staff type's id (0x41391d: 9500 a
// zookeeper, 9501 a maintenance worker, 9551 a scientist, 9552 a marine
// specialist), not darted asleep (+0x393), and only while the staff member
// stands on the zoo's ground (0x42670b: its habitat's +0x2c) - so in an
// exhibit it sees none. The one it's already running from (+0x22c) while
// within cPredatorRadius tiles either way; else the nearest loose one (the
// list at 0x638fb0), its tiles' squared distance under cPredatorRadius
// squared. (ours also leaves out crated ones, as the guests' look)
int Staff::findThreat(const Member &m, const Fences &fences) const {
  if (!this->animals || m.type < 0 || m.type >= static_cast<int>(this->typeList.size()))
    return -1;
  const int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  if (fences.exhibitAt(sx, sy) >= 0)
    return -1;
  const int id = this->typeList[m.type].nameId;
  auto hunts = [&](const Animals::Member &a) {
    if (a.type < 0 || a.type >= static_cast<int>(this->animals->types().size()) || a.boxed || a.egg ||
        a.tranquilised)
      return false;
    const Animals::Type &at = this->animals->types()[a.type];
    if (a.baby && !at.babiesAttack)
      return false;
    return at.preyIds[a.baby ? 2 : a.female ? 1 : 0].count(id) > 0;
  };
  const int r = this->predatorRadius;
  if (m.predator >= 0)
    if (const Animals::Member *a = this->animals->member(m.predator))
      if (hunts(*a) && std::abs(static_cast<int>(std::floor(a->x)) - sx) <= r &&
          std::abs(static_cast<int>(std::floor(a->y)) - sy) <= r)
        return a->id;
  int best = -1, bestD = r * r;
  for (const Animals::Member &a : this->animals->members()) {
    if (!a.escaped || !hunts(a))
      continue;
    const int dx = static_cast<int>(std::floor(a.x)) - sx, dy = static_cast<int>(std::floor(a.y)) - sy;
    if (dx * dx + dy * dy < bestD) {
      bestD = dx * dx + dy * dy;
      best = a.id;
    }
  }
  return best;
}

// Task 0x75 (Task75_AvoidPredator, "Avoiding Predator"): the keeper's and
// the maintenance worker's chooseTask (0x423d04, 0x439c33) take it first,
// priority 3, while +0x24c is set, so what it was doing is dropped. Its
// whileGoing (0x4a557f) gets a target from slot 0x30c (animalFleeTarget
// 0x4a639e: a tile on the ring cPredatorRadius + 2 out, the way away from
// it or a turn of 45 degrees either side; nearer in if none - as the
// animals' Animals::runFrom) and runs there by bRunFromPredator. There
// (atTarget 0x431834 does nothing for staff) the task's over; still having
// seen it, the chooser runs again.
// The tour guide's chooser (0x58ce7e) does the same, but first lets its
// whole group go (0x58b362 at 100: every guest following it, +0x2e4, leaves
// the tour) and ends its trip (+0x2a0).
// The status line on task 0x75: setting a task zeroes the job (taskStateSet
// 0x41322b -> 0x413304: state +0x18, the unit's +0x174), and with job 0 on
// a task they don't name the keeper's statusText (0x471399) and the
// maintenance worker's (0x4725ea) give the empty string (0x638084); the
// guide's (0x61b756), not on 0x76/0x77 and not speaking (0x61b964), 10402
bool Staff::flee(Member &m, const WorldMap &map, Fences &fences) {
  const Animals::Member *threat = this->animals ? this->animals->member(m.predator) : nullptr;
  if (!threat)
    return false;
  const Kind kind = this->typeList[m.type].kind;
  m.dutyId = kind == Kind::Guide ? kGuideMonitoring : 0;
  m.dutyArg.clear();
  if (kind == Kind::Guide && this->guests) {
    this->guests->releaseTour(m.id);
    m.followers.clear();
    m.speaking = false;
    m.visit = -1;
  }
  // (what it was doing, dropped)
  if (m.job == Job::Catch)
    releaseCatch(m);
  if (m.job == Job::Visit) {
    m.visit = -1;
    m.access = -1;
    m.phase = 0;
    m.dropFood.clear();
    m.feedAnimal = -1;
    m.invalid.clear();
    m.unreachable.clear();
  }
  if (m.job == Job::Tour && this->guests) {
    this->guests->releaseTour(m.id);
    m.followers.clear();
    m.speaking = false;
  }
  m.job = Job::None;
  m.jobItem = -1;
  m.playsLeft = 0;
  m.steps.clear();
  m.reverse = false;
  m.path.clear();
  m.pathAt = 0;
  const float ax = m.x - threat->x, ay = m.y - threat->y;
  const float a = std::fabs(ax) + std::fabs(ay) < 0.01f ? std::uniform_real_distribution<float>(0, 6.2832f)(this->rng)
                                                       : std::atan2(ay, ax);
  const int mx = static_cast<int>(std::floor(m.x)), my = static_cast<int>(std::floor(m.y));
  for (int r = this->predatorRadius + 2; r >= 1; r--)
    for (float turn : {0.0f, 0.7854f, -0.7854f}) {
      const float cx = std::cos(a + turn), cy = std::sin(a + turn);
      const float k = r / std::max(std::fabs(cx), std::fabs(cy));
      const int x = mx + static_cast<int>(std::lround(cx * k)), y = my + static_cast<int>(std::lround(cy * k));
      if (fences.exhibitAt(x, y) >= 0 || !walkable(x, y, map, fences, -1))
        continue;
      if (findPath(m, x, y, map, fences, x + 0.5f, y + 0.5f)) {
        m.job = Job::Flee;
        m.idleLeft = 0;
        ZT_STEP("0x4a557f", "Runs from an animal that would hunt it (task 0x75, bRunFromPredator)",
                {"animal", threat->name}, {"to x", x}, {"to y", y});
        return true;
      }
    }
  m.anim = "idle";
  m.idleLeft = 0.5f;
  return false;
}

// ours: lets go of the animal it was after
void Staff::releaseCatch(Member &m) {
  if (auto *a = animals ? animals->member(m.jobAnimal) : nullptr)
    if (a->claimedBy == m.id) a->claimedBy = -1;
  m.jobAnimal = -1;
  if (m.job == Job::Catch) m.job = Job::None;
}

// The helicopter's tasks (zoo.exe ZTHelicopter::chooseTask 0x506807 and the
// task classes it makes), in the order the original runs them:
//   0x6a waiting at base: each tick its pick (slot 0x32c, 0x423dae) - one
//     found, take-off (Task6a_HeliWaitAtBase 0x50687c, 0x48d3e1);
//   0x69 take-off: its destination the base's exitpos (0x4a705e), bSpinup
//     played through, then the rotor loop on (0x506990(1)) and bAscend - a
//     straight line to it; there, the base lets it go and it's on to 0x6f at
//     its animal (Task69_HeliTakeoff 0x48d3ed, 0x48c081, 0x48c045, 0x48c4d1);
//   0x6f tranquilizing (Task6f 0x50d5dc, 0x50cf14, 0x50d434) through Job13
//     (0x50d05f, 0x50cffd, 0x50db25) and Job14 (0x50da26, 0x50dd6e): to the
//     animal's tile with bChaseAnimal; within cWeaponRange whole tiles
//     (staffInRange 0x50cf5b) bFireWeapon, played through, then the dart;
//     the animal gone or not one it may go after (0x50cd14) ends it, and
//     chooseTask picks again - another animal, or home (0x67);
//   0x67 back to base: bFly to the exitpos; any animal found on the way,
//     0x6f at it; on the exitpos's tile, landing (Task67 0x5a45f0, 0x5a4577,
//     0x5a4556, 0x5a47e7);
//   0x68 landing: bDescend in a straight line to its slotpos (0x5ad347),
//     bSpindown played through, the rotor loop off (0x506990(0)), waiting
//     (Task68 0x5a491d, 0x5a479a, 0x5a47f3).
// Its moves are a walker's (moveAccumulate 0x435c42: max(1, rate x 67 / 1000)
// steps an update, 64 to a tile, 16 to a height level) at its sets' fFly
// rates. The base never moves with the aircraft.
void Staff::helicopterStep(Member &m, float seconds, const WorldMap &map, const Fences &fences) {
  const Type &t = typeList[m.type];
  if (m.heliStage == HeliStage::Parked) {
    const auto anchor = placementPoint(m.type, m.x, m.y); m.x = anchor.first; m.y = anchor.second;
  }
  const float padX = m.x + t.padX, padY = m.y + t.padY;
  const float ground = groundAt(map, m.x, m.y), padZ = ground + t.padZ;
  const float exitX = m.x + t.exitX, exitY = m.y + t.exitY, exitZ = ground + t.exitZ;
  // (its status line, ZTHelicopter::statusText 0x48c9ef, by its task:
  // takeoff 0x69 - bSpinup, bAscend - 10318; tranquilizing 0x6f 10308 with
  // the animal's name (+0x290); returning 0x67 10319; landing 0x68 -
  // bDescend, bSpindown - 10317; waiting at base 0x6a 10316)
  auto stage = [&](HeliStage s, const char *anim, int duty) {
    m.heliStage = s; m.anim = anim; m.animTime = 0; m.dutyId = duty;
  };
  // zoo.exe 0x50cd14 for the helicopter (type 9550): a loose dinosaur not
  // darted (+0x393), not on a water tile (tile +0x83 & 3)
  auto valid = [&](const Animals::Member *a) {
    if (!(a && a->type >= 0 && a->type < static_cast<int>(animals->types().size()) &&
          animals->types()[a->type].dinosaur && a->escaped && !a->boxed &&
          !a->tranquilised && !a->dying && !a->egg && a->id != animals->carried))
      return false;
    const MapTile *tile = map.getTile(static_cast<int>(std::floor(a->x)), static_cast<int>(std::floor(a->y)));
    return !(tile && (tile->terrainType == 9 || tile->terrainType == 10));
  };
  auto padFacing = [&] {
    const float angle = t.padFacing * 3.14159265f / 4;
    m.fx = std::sin(angle); m.fy = -std::cos(angle);
  };
  // zoo.exe 0x423dae (slot 0x32c, the helicopter's as the keeper's): of
  // the loose animals (0x638fb0) not claimed by a unit still there
  // (+0x374), those it may go after (0x50cd14), the nearest by whole tiles
  // |dx| + |dy| from its own tile, the first on a tie
  auto pick = [&]() -> Animals::Member * {
    if (!animals) return nullptr;
    Animals::Member *nearest = nullptr;
    int best = 0x7fffffff;
    const int hx = static_cast<int>(std::floor(m.heliX)), hy = static_cast<int>(std::floor(m.heliY));
    for (const auto &candidate : animals->members()) {
      if (!valid(&candidate)) continue;
      if (candidate.claimedBy >= 0 && candidate.claimedBy != m.id && member(candidate.claimedBy)) continue;
      const int ax = static_cast<int>(std::floor(candidate.x)), ay = static_cast<int>(std::floor(candidate.y));
      const int distance = std::abs(ax - hx) + std::abs(ay - hy);
      if (distance < best) { best = distance; nearest = animals->member(candidate.id); }
    }
    return nearest;
  };
  auto claim = [&](Animals::Member *a) {
    if (m.jobAnimal != a->id) releaseCatch(m);
    a->claimedBy = m.id; m.job = Job::Catch; m.jobAnimal = a->id; m.dutyArg = a->name;
  };
  // Task6f's start (0x50d5dc): Job13 at its animal, the route made fresh
  auto tranquilize = [&](Animals::Member *a) {
    claim(a); m.heliToX = m.heliToY = -1;
    stage(HeliStage::Chase, "fly", 10308);
  };
  // Task67's start (0x5a45f0): bFly to the base's exitpos
  auto goHome = [&] {
    releaseCatch(m); m.dutyArg.clear();
    stage(HeliStage::Return, "fly", 10319);
  };
  // chooseTask 0x506807 with no task (its body 0x506855): an animal - 0x6f
  // (priority 3), else 0x67 (priority 0). (With no base it takes itself
  // away, 0x4fa78e(1); ours is one with its base)
  auto chooseTask = [&] {
    if (Animals::Member *a = pick()) tranquilize(a);
    else goHome();
  };
  // the steps it moves this tick at a rate (moveAccumulate 0x435c42)
  auto steps = [&](int rate) {
    return seconds * Original::unitTilesPerSecond(rate) * Original::unitStepsPerTile;
  };
  // 0x499be7 (bAscend, bDescend): a straight line to the point, x and y in
  // 64ths of a tile and z in 16ths of a level alike
  auto flyLine = [&](float x, float y, float z, int rate) {
    const float dx = (x - m.heliX) * 64, dy = (y - m.heliY) * 64, dz = (z - m.heliZ) * 16;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz), step = steps(rate);
    if (distance <= step) { m.heliX = x; m.heliY = y; m.heliZ = z; return true; }
    m.heliX += dx / distance * step / 64; m.heliY += dy / distance * step / 64; m.heliZ += dz / distance * step / 16;
    return false;
  };
  // playAscendDescend 0x48c0c6 (bFly, bChaseAnimal): across to the point,
  // then its height over the ground there (over a tank's water, over the
  // water) kept from cMinHeight to cMaxHeight - under the ground a whole
  // step up, else at most half a step towards it. It faces the point
  // (heliSetDestination 0x48bf41 sets the facing from where it is towards
  // the destination; on the straight flight, the same each step)
  auto flyLevel = [&](float x, float y, int rate) {
    const float dx = (x - m.heliX) * 64, dy = (y - m.heliY) * 64;
    const float distance = std::sqrt(dx * dx + dy * dy), step = steps(rate);
    if (distance > 0.001f) { m.fx = dx / distance; m.fy = dy / distance; }
    if (distance <= step) { m.heliX = x; m.heliY = y; }
    else { m.heliX += dx / distance * step / 64; m.heliY += dy / distance * step / 64; }
    float surface = groundAt(map, m.heliX, m.heliY);
    const int ex = fences.exhibitAt(static_cast<int>(std::floor(m.heliX)), static_cast<int>(std::floor(m.heliY)));
    if (const Fences::Exhibit *e = ex >= 0 ? fences.exhibit(ex) : nullptr)
      if (e->tank && tankDepth) surface += tankDepth(ex);
    const float height = (m.heliZ - surface) * 16, half = step / 2;
    if (height < 0) m.heliZ += step / 16;
    else if (height < t.minHeight) m.heliZ += std::min(t.minHeight - height, half) / 16;
    else if (height > t.maxHeight) m.heliZ -= std::min(height - t.maxHeight, half) / 16;
    return distance <= step;
  };
  auto tileOf = [](float v) { return static_cast<int>(std::floor(v)); };
  auto *target = animals ? animals->member(m.jobAnimal) : nullptr;
  switch (m.heliStage) {
  case HeliStage::Parked: { // 0x6a: waiting at base
    m.heliX = padX; m.heliY = padY; m.heliZ = padZ; padFacing();
    m.anim = "idle"; m.dutyId = kWaitingAtBase;
    Animals::Member *a = pick();
    if (!a) return;
    claim(a);
    stage(HeliStage::SpinUp, "warmup", 10318);
    SDL_Log("[DRT] %s dispatched for %s", m.name.c_str(), m.dutyArg.c_str());
    return;
  }
  case HeliStage::SpinUp: // 0x69: bSpinup played through, the loop on
    // (heliSetFlying 0x506990(1) -> zoo.exe 0x48d645: cLoopSoundName looped,
    // unless it's playing already, +0x2a0)
    if (m.animTime >= animSeconds(m, "warmup")) {
      if (!t.rotorSound.empty() && m.rotorChannel < 0) m.rotorChannel = Sound::get().loop(t.rotorSound, t.rotorAtten);
      stage(HeliStage::Ascend, "level", 10318);
    }
    break;
  case HeliStage::Ascend: // 0x69: bAscend to the exitpos; then 0x6f
    if (flyLine(exitX, exitY, exitZ, t.ascendRate)) {
      if (valid(target)) tranquilize(target);
      else chooseTask();
    }
    break;
  case HeliStage::Chase: { // 0x6f, Job13
    if (!valid(target)) { chooseTask(); break; }
    const long long tdx = tileOf(target->x) - tileOf(m.heliX), tdy = tileOf(target->y) - tileOf(m.heliY);
    const long long range = static_cast<long long>(t.weaponRange);
    if (tdx * tdx + tdy * tdy <= range * range) {
      // Job13's atTarget 0x50db25: faces it, bFireWeapon (fPlayWithSound
      // fire, fire_weapon, then fPlay level)
      const float d = std::max(0.001f, std::hypot(target->x - m.heliX, target->y - m.heliY));
      m.fx = (target->x - m.heliX) / d; m.fy = (target->y - m.heliY) / d;
      stage(HeliStage::Fire, "fire", 10308);
      Sound::get().play(t.fireSound, 1000);
      break;
    }
    // (no route: to the animal's tile; there, a fresh one - 0x50d05f,
    // 0x50cffd's slot 0x4c)
    if (m.heliToX < 0) { m.heliToX = tileOf(target->x) + 0.5f; m.heliToY = tileOf(target->y) + 0.5f; }
    flyLevel(m.heliToX, m.heliToY, t.chaseRate);
    if (tileOf(m.heliX) == tileOf(m.heliToX) && tileOf(m.heliY) == tileOf(m.heliToY)) m.heliToX = m.heliToY = -1;
    break;
  }
  case HeliStage::Fire: // 0x6f, Job14: bFireWeapon through, then the dart
    if (m.anim == "fire" && m.animTime >= animSeconds(m, "fire")) { m.anim = "level"; m.animTime = 0; }
    else if (m.anim == "level" && m.animTime >= animSeconds(m, "level")) {
      // (Job14_RetrieveAnimal::atTarget 0x50dd6e: darted, in range or not;
      // an animal: its base's recoveries one up first, the helicopter's
      // slot 0x320, 0x48c434 - the DRT panel's 1409)
      if (valid(target)) {
        ++m.recoveries;
        animals->tranquilise(target->id);
        SDL_Log("[DRT] %s tranquilized %s; ready for scientist containment", m.name.c_str(), target->name.c_str());
      }
      releaseCatch(m);
      chooseTask();
    }
    break;
  case HeliStage::Return: // 0x67: home, or an animal found on the way
    if (Animals::Member *a = pick()) { tranquilize(a); break; }
    flyLevel(exitX, exitY, t.flyRate);
    if (tileOf(m.heliX) == tileOf(exitX) && tileOf(m.heliY) == tileOf(exitY))
      stage(HeliStage::Descend, "level", 10317);
    break;
  case HeliStage::Descend: // 0x68: bDescend to its slotpos, then bSpindown
    if (flyLine(padX, padY, padZ, t.descendRate)) { padFacing(); stage(HeliStage::SpinDown, "warmup", 10317); }
    break;
  case HeliStage::SpinDown:
    if (m.animTime >= animSeconds(m, "warmup")) {
      Sound::get().stop(m.rotorChannel); m.rotorChannel = -1;
      stage(HeliStage::Parked, "idle", kWaitingAtBase);
      m.dutyArg.clear();
      SDL_Log("[DRT] %s landed at its unchanged base", m.name.c_str());
    }
    break;
  }
}

// ----------------------------------------------------------------------------
// Each one's day: walk about, stand a while, go to work when there is some
// ----------------------------------------------------------------------------

// How fast a walker goes (zoo.exe moveAccumulate 0x435c42 at the rate its
// move set, class slot 0x1c0): fleeing its flee rate (cFastRate if none),
// running after an animal cFastRate, else cSlowRate. A dinosaur scientist,
// once one has been named "Dr. Scholl": every rate twice over (ZTKeeper's
// setMoveSpeed 0x41678b, with 0x63c2e4 set and its type 0x254f); Dr.
// Scholl himself that again till his walk ends (moveBoost)
float Staff::tilesPerSecond(const Member &m, bool fleeing, bool running) const {
  const Type &t = this->typeList[m.type];
  const int boost = (RenameCheats::flags().scholl && t.nameId == Original::Cheat::scientistNameId
                         ? Original::Cheat::schollRateFactor
                         : 1) *
                    m.moveBoost;
  return fleeing   ? Original::unitTilesPerSecond((t.fleeRate > 0 ? t.fleeRate : t.fastRate) * boost)
         : running ? Original::unitTilesPerSecond(t.fastRate * boost)
         : boost == 1 ? t.speed
                      : Original::unitTilesPerSecond(t.slowRate * boost);
}

void Staff::update(float seconds, const WorldMap &map, Fences &fences) {
  this->clock += seconds;
  noticeWork(map, fences);
  for (Member &m : this->list) {
    ZT_WHO("staff", m.id);
    const Type &t = this->typeList[m.type];
    // Exhibits that are gone (opened up, bulldozed) come off its list. A
    // keeper's (zookeeper, scientist, marine specialist - the ZTKeeper class)
    // also come off once they're down to under 2 tiles: ZTKeeper::tick
    // 0x423d84 first runs 0x423d95, which asks each habitat on its list
    // (+0x260) staffCanAssignToExhibit 0x423e01(keeper, 0, 1) and on 1 takes
    // it off (0x50c884); 1 is the habitat's tile list (+0x40) counted under 2
    // (0x423e97). (2 - the wrong kind of exhibit, nothing there it looks
    // after - doesn't drop it)
    const bool keeperClass = t.kind == Kind::Keeper || t.kind == Kind::Scientist || t.kind == Kind::Trainer;
    m.exhibits.erase(std::remove_if(m.exhibits.begin(), m.exhibits.end(),
                                    [&](int ex) {
                                      const Fences::Exhibit *e = fences.exhibit(ex);
                                      return e == nullptr || (keeperClass && e->tiles.size() < 2);
                                    }),
                     m.exhibits.end());
    m.animTime += seconds;
    m.baseAnimTime += seconds;
    m.workCheck -= seconds;
    // zoo.exe 0x439c06 (ZTMaintenanceWorker's update): every
    // cClearInvalidListInterval ms its list of work it couldn't get to is
    // emptied
    if (t.kind == Kind::Maint) {
      m.invalidClear -= seconds;
      if (m.invalidClear <= 0) {
        m.invalid.clear();
        m.invalidClear = t.clearInvalidMs / 1000.0f;
      }
    }
    if (m.id == this->carried) {
      if (t.kind == Kind::Helicopter) {
        releaseCatch(m); Sound::get().stop(m.rotorChannel); m.rotorChannel = -1;
        m.heliStage = HeliStage::Parked; m.anim = "idle";
      }
      continue;
    }
    if (m.showTank >= 0)
      continue; // Shows advances the normal route and trainer cues.
    if (t.kind == Kind::Helicopter) {
      helicopterStep(m, seconds, map, fences);
      continue;
    }
    // Climbing (in or out of a tank, down under its water or up): along a
    // line over one play of the climb animation
    if (m.climbing) {
      m.climbTime += seconds;
      float len = std::max(0.1f, animSeconds(m, "climb"));
      float k = std::min(1.0f, m.climbTime / len);
      m.x = m.climbFrom[0] + (m.climbTo[0] - m.climbFrom[0]) * k;
      m.y = m.climbFrom[1] + (m.climbTo[1] - m.climbFrom[1]) * k;
      m.z = m.climbFrom[2] + (m.climbTo[2] - m.climbFrom[2]) * k - groundAt(map, m.x, m.y);
      if (k >= 1.0f) {
        m.climbing = false;
        m.water = m.climbWater;
        m.z = m.water == 0 ? 0.0f : std::max(0.0f, m.z);
        m.anim = "idle";
        m.animTime = 0;
        if (m.job == Job::Visit)
          visitStep(m, map, fences);
      }
      continue;
    }
    // An animal that would hunt it (zoo.exe ZTStaff::tick 0x440602, each
    // status tick, as a guest's: [AI] mStatusPeriod 1000 ms): every
    // cChaseCheck ticks, unless it's running from one already (task 0x75),
    // it looks (slot 0x2fc) - "Saw escaped animal", +0x24c - and the
    // keeper's and maintenance worker's choosers take task 0x75 before
    // anything else (0x423d04, 0x439c33). Tour guides too: ZTTourGuide::tick
    // 0x58ceb9 gathers its followers every few ticks and always goes on to
    // this staff tick, on a tour or not, and its chooser 0x58ce7e takes task
    // 0x75 the same way (the helicopter isn't hunted)
    if ((t.kind == Kind::Keeper || t.kind == Kind::Scientist || t.kind == Kind::Trainer || t.kind == Kind::Maint ||
         t.kind == Kind::Guide) &&
        m.tank < 0 && m.water == 0) {
      m.statusMs += seconds * 1000.0f;
      bool looked = false;
      while (m.statusMs >= kStatusMs) {
        m.statusMs -= kStatusMs;
        if (--m.chaseCount < 0) {
          m.chaseCount = t.chaseCheck - 1;
          if (m.job != Job::Flee) {
            m.predator = findThreat(m, fences);
            m.sawThreat = m.predator >= 0;
            looked = true;
          }
        }
      }
      if (looked && m.sawThreat) {
        ZT_STEP("0x440602", "Saw escaped animal", {"animal", this->animals && this->animals->member(m.predator)
                                                               ? this->animals->member(m.predator)->name : std::string()});
        flee(m, map, fences);
        if (m.job == Job::Flee)
          continue;
      }
    }
    // Playing a work animation
    if (m.playsLeft > 0) {
      float len = animSeconds(m, m.anim);
      if (m.animTime >= len) {
        m.animTime -= len;
        if (m.playsLeft > 1 && m.steps.empty())
          workSound(m, m.anim);
        if (--m.playsLeft > 0 && !m.steps.empty())
          startStep(m);
        if (m.playsLeft == 0) {
          m.steps.clear();
          m.reverse = false;
          // The work's done
          if (m.job == Job::Visit) {
            visitStep(m, map, fences);
            continue;
          }
          if (m.job == Job::Catch) {
            // The dart's in: asleep; now to crate it
            if (this->animals)
              this->animals->tranquilise(m.jobAnimal);
            m.phase = 1;
            m.dutyId = 10309;
            m.anim = "idle";
            catchStep(m, 0, map, fences);
            continue;
          }
          ZT_STEP("", "The work's done", {"job", kJobNames[std::clamp(static_cast<int>(m.job), 0, 9)]});
          if (m.job == Job::Fence) {
            // zoo.exe 0x59a51d (Job17_FixFence's atTarget 0x59a596): the
            // piece mended; with the type's cFixFenceModifier over 0 (the
            // maintenance training), the pieces either side of it along the
            // fence too (the tiles two directions round, 0x432236)
            fences.repair(m.jobEdge);
            if (t.fixFenceModifier > 0) {
              const Fences::Edge &e = m.jobEdge;
              for (int d : {-1, 1})
                fences.repair(e.alongX ? Fences::Edge{true, e.x + d, e.y} : Fences::Edge{false, e.x, e.y + d});
              ZT_STEP("0x59a51d", "Trained (cFixFenceModifier): the pieces either side mended too",
                      {"cFixFenceModifier", t.fixFenceModifier});
            }
          }
          else if (m.job == Job::Filter)
            fences.serviceFilter(m.jobFilter);
          else if (m.job == Job::Litter && this->items) {
            // The litter on its tile and the four beside it (all eight round
            // it with cCleanTrashRadius: zoo.exe 0x52d76a)
            if (const ZooItems::Item *it = this->items->item(m.jobItem)) {
              int lx = static_cast<int>(std::floor(it->x)), ly = static_cast<int>(std::floor(it->y));
              bool all = t.cleanTrashRadius > 0;
              for (int id : this->items->ofKind(ZooItems::Kind::Litter, -1)) {
                const ZooItems::Item *o = this->items->item(id);
                int dx = std::abs(static_cast<int>(std::floor(o->x)) - lx), dy = std::abs(static_cast<int>(std::floor(o->y)) - ly);
                if (id != m.jobItem && dx <= 1 && dy <= 1 && (all || dx + dy <= 1))
                  this->items->remove(id);
              }
            }
            this->items->remove(m.jobItem);
          }
          else if (m.job == Job::Dung && this->items) {
            if (const ZooItems::Item *it = this->items->item(m.jobItem))
              this->dungRecycled(it->x, it->y);
            this->items->remove(m.jobItem);
          } else if (m.job == Job::Trash && this->objects) {
            if (PlacedObjects::Object *can = this->objects->byId(m.jobItem)) {
              int cx = static_cast<int>(std::floor(can->x)), cy = static_cast<int>(std::floor(can->y));
              for (PlacedObjects::Object &o : this->objects->all())
                if (o.trashCan && static_cast<int>(std::floor(o.x)) == cx &&
                    static_cast<int>(std::floor(o.y)) == cy)
                  o.fill = 0;
            }
          }
          // Its work list entry freed (0x4a97e5, from Job8_Sweep 0x52d8fb,
          // Job10 0x59be4e, Job12 0x50e47f, Job17 0x59a626, Job27 0x595da7)
          if (t.kind == Kind::Maint)
            finishEntry(m);
          m.job = Job::None;
          m.anim = "idle";
          m.idleLeft = 0.5f;
          m.dutyId = t.kind == Kind::Maint ? kMaintMonitoring : kMonitoring;
        }
      }
      continue;
    }
    // A tour guide: a look for followers every cCrowdCheck ticks (on the way
    // and while talking); the talk (speak, looped - see where it starts)
    if (m.job == Job::Tour) {
      m.crowdClock -= seconds;
      if (m.crowdClock <= 0 && (m.pathAt < m.path.size() || m.speaking)) {
        m.crowdClock += static_cast<float>(t.crowdCheck);
        this->gatherFollowers(m);
      }
      if (m.speaking) {
        m.speakLeft -= seconds;
        const char *talk = "speak";
        if (m.anim != talk) {
          m.anim = talk;
          m.animTime = 0;
        }
        if (m.speakLeft <= 0) {
          m.speaking = false;
          if (this->guests) this->guests->releaseTour(m.id);
          m.followers.clear();
          m.job = Job::None;
          m.anim = "idle";
          m.idleLeft = 0.5f;
          m.dutyId = kGuideMonitoring;
          m.dutyArg.clear();
          m.workCheck = 0;
        }
        continue;
      }
    }
    // After an escaped animal: running, checking how near it is
    if (m.job == Job::Catch) {
      catchStep(m, seconds, map, fences);
      if (m.job != Job::Catch || m.playsLeft > 0)
        continue;
    }
    // Walking a path. Litter gone before it gets there (another worker swept
    // it): "Done with trash piles", the job ends (Job7_GoToTrashPile's
    // checkArrived, zoo.exe 0x52cef4, each tick: no trash left at its
    // target - 0x4a2408 "objectsothertrash"); dung the same way
    // (keeperDoneZoodoo 0x4a25df: no "objectsotherpoo" there, the job ends)
    if (m.pathAt < m.path.size() && (m.job == Job::Litter || m.job == Job::Dung) && this->items &&
        !this->items->item(m.jobItem)) {
      ZT_STEP(m.job == Job::Litter ? "0x52cef4" : "0x4a25df",
              m.job == Job::Litter ? "Done with trash piles: the litter's gone before it got there"
                                   : "The dung's gone before it got there: the job ends");
      m.path.clear();
      m.pathAt = 0;
      m.job = Job::None;
      m.jobItem = -1;
      m.anim = "idle";
      m.idleLeft = 0.5f;
      m.dutyId = kMaintMonitoring;
      m.workCheck = 0;
      continue;
    }
    if (m.pathAt < m.path.size()) {
      auto [tx, ty] = m.path[m.pathAt];
      float dx = tx - m.x, dy = ty - m.y, dist = std::hypot(dx, dy);
      // (running after an animal: cFastRate, by the same steps as
      // walking - zoo.exe 0x435c42)
      bool running = m.job == Job::Catch;
      // (running from an animal: its bRunFromPredator's anim and rate)
      const bool fleeing = m.job == Job::Flee;
      float step = tilesPerSecond(m, fleeing, running) * seconds;
      const char *moving = fleeing ? t.fleeAnim.c_str() : running ? "run" : m.sweeping ? "subclean" : "walk";
      if (m.anim != moving) {
        m.anim = moving;
        m.animTime = 0;
      }
      if (dist > 0.0001f) {
        m.fx = dx / dist;
        m.fy = dy / dist;
      }
      if (m.pathAt < m.pathLayers.size())
        m.layer = m.pathLayers[m.pathAt];
      if (dist <= step) {
        m.x = tx;
        m.y = ty;
        m.pathAt++;
      } else {
        float nx = m.x + dx / dist * step, ny = m.y + dy / dist * step;
        // Stepping round anyone close in front (sideways, if there's room)
        if (this->index)
          for (const WorldIndex::Mover &o : this->index->near(m.x, m.y, 0.6f)) {
            if (o.id == m.id || o.kind != WorldIndex::Kind::Staff)
              continue;
            float ox = o.x - m.x, oy = o.y - m.y, od = std::hypot(ox, oy);
            if (od < 0.001f || (ox * dx + oy * dy) / (od * dist) < 0.3f)
              continue;
            // Their side of the line: go the other way
            float side = (dx * oy - dy * ox) > 0 ? -1.0f : 1.0f;
            float px = -dy / dist * side, py = dx / dist * side;
            float push = step * (0.6f - od) / 0.6f;
            float sx2 = nx + px * push, sy2 = ny + py * push;
            if (walkable(static_cast<int>(std::floor(sx2)), static_cast<int>(std::floor(sy2)), map,
                         fences, m.access)) {
              nx = sx2;
              ny = sy2;
            }
          }
        m.x = nx;
        m.y = ny;
      }
      if (m.pathAt >= m.path.size()) {
        m.path.clear();
        m.pathAt = 0;
        m.moveBoost = 1;
        if (m.job == Job::Flee) {
          // There: the task's over; still having seen it (+0x24c, till a
          // look finds none), off again
          m.job = Job::None;
          m.anim = "idle";
          m.idleLeft = 0.5f;
          // (else the chooser's task 1, the zoo routine: "Monitoring zoo")
          m.dutyId = t.kind == Kind::Guide ? kGuideMonitoring : t.kind == Kind::Maint ? kMaintMonitoring : kMonitoring;
          if (m.sawThreat)
            flee(m, map, fences);
        } else if (m.job == Job::Visit) {
          visitStep(m, map, fences);
        } else if (m.job == Job::Tour) {
          // There: with a group, the talk; without, the trip's over
          if (m.followers.empty()) {
            m.job = Job::None;
            m.anim = "idle";
            m.idleLeft = 0.5f;
            m.dutyId = kGuideMonitoring;
            m.dutyArg.clear();
          } else {
            ZT_STEP("0x58ce7e", "There with a group: the talk (cTourGuideBonus)", {"followers", static_cast<int>(m.followers.size())},
                    {"cTourGuideBonus", t.tourBonus}, {"cInformGuestTime", t.informTime});
            m.speaking = true;
            // The talk, bSpeak (tour.ai: fPlaySet(bSpeak1), fPlaySet(bSpeak2),
            // each fPlayTime(speak / speak2, cInformGuestTime)): fPlayTime
            // queues whole plays of its animation until cInformGuestTime * 1000
            // ms is covered (zoo.exe 0x424e7f -> 0x424ee7); measured:
            // 2026-11-15, the original's guides stay in bSpeak (task 0x77)
            // 6.8-6.9 s a talk, 20 talks timed - speak (40 frames at 83 ms)
            // played twice, 6.64 s; speak2 never comes (speak + speak2 would be
            // 7.57 s at the least)
            {
              const float once = std::max(0.1f, animSeconds(m, "speak"));
              m.speakLeft = once * std::max(1.0f, std::ceil(t.informTime / once));
            }
            m.dutyId = kSpeaking;
            m.dutyArg.clear();
            // (facing the exhibit)
            if (const Fences::Exhibit *ex = fences.exhibit(m.visit)) {
              float cx = 0, cy = 0;
              for (auto [x, y] : ex->tiles) {
                cx += x + 0.5f;
                cy += y + 0.5f;
              }
              cx /= ex->tiles.size();
              cy /= ex->tiles.size();
              float d = std::hypot(cx - m.x, cy - m.y);
              if (d > 0.001f) {
                m.fx = (cx - m.x) / d;
                m.fy = (cy - m.y) / d;
              }
            }
            if (this->guests)
              this->guests->tourTalk(m.id, t.tourBonus);
          }
        } else if (m.job == Job::Dung) {
          // bBag: sweep twice, bagging it; a dinosaur's dung (dinopoo),
          // bBagDino: the vac twice, then idle (Job12_BagZoodoo's begin,
          // zoo.exe 0x50e211, by the pile's type against dinoPooType
          // 0x41044c)
          m.dutyId = kCleaningUp;
          const ZooItems::Item *pile = this->items ? this->items->item(m.jobItem) : nullptr;
          playSet(m, pile && ZooItems::dinoDung(*pile) ? "bbagdino" : "bbag", "sweep", 2);
        } else if (m.job == Job::Trash) {
          if (this->objects)
            if (PlacedObjects::Object *can = this->objects->byId(m.jobItem)) {
              float d = std::hypot(can->x - m.x, can->y - m.y);
              if (d > 0.001f) {
                m.fx = (can->x - m.x) / d;
                m.fy = (can->y - m.y) / d;
              }
            }
          m.dutyId = kEmptyingCan;
          playSet(m, "bempty", "empty", 2);
        } else if (m.job == Job::Litter) {
          // bSweep: sweep twice
          m.dutyId = kSweeping;
          playSet(m, "bsweep", "sweep", 2);
        } else if (m.job == Job::Fence || m.job == Job::Filter) {
          // Face the work, then fix it (bFix: fix twice)
          float wx, wy;
          if (m.job == Job::Fence) {
            wx = m.jobEdge.alongX ? m.jobEdge.x + 0.5f : static_cast<float>(m.jobEdge.x);
            wy = m.jobEdge.alongX ? static_cast<float>(m.jobEdge.y) : m.jobEdge.y + 0.5f;
          } else {
            const Fences::Filter *f = fences.filter(m.jobFilter);
            wx = f ? f->x + fences.filterType(f->file).footprintX * 0.5f : m.x;
            wy = f ? f->y + fences.filterType(f->file).footprintY * 0.5f : m.y;
          }
          float d = std::hypot(wx - m.x, wy - m.y);
          if (d > 0.001f) {
            m.fx = (wx - m.x) / d;
            m.fy = (wy - m.y) / d;
          }
          m.dutyId = m.job == Job::Fence ? kFixing : kServicing;
          playSet(m, "bfix", "fix", 2);
        } else {
          m.anim = "idle";
          m.animTime = 0;
          if (idleOriginal(m) && m.job == Job::None) {
            // (the original's rhythm, after a walk: see idleOriginal)
            m.idleLeft = 0;
            m.walksLeft = 0;
          } else if (m.walksLeft <= 0)
            m.idleLeft = kIdleSeconds;
          // ours: a walk about (walkAbout) over, only the exhibit it ends in
          // stays open to it, so it can walk out of that one again
          if (m.job == Job::None && m.access >= 0) {
            const int at = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
            const Fences::Exhibit *e = fences.exhibit(at);
            m.access = e && !e->tank ? at : -1;
          }
        }
      }
      continue;
    }
    // Standing
    if (m.idleLeft > 0) {
      m.idleLeft -= seconds;
      if (m.anim != "idle") {
        m.anim = "idle";
        m.animTime = 0;
      }
      continue;
    }
    if (idleOriginal(m)) {
      idleStep(m, map, fences);
      continue;
    }
    // Time to look for work (every few seconds), else walk about
    if (m.workCheck <= 0) {
      m.workCheck = 3.0f;
      bool found = findWork(m, map, fences);
      ZT_STEP("", "Looks for work (every 3 s)", {"found", found ? kJobNames[std::clamp(static_cast<int>(m.job), 0, 9)] : "nothing: wanders"},
              {"duty", this->rm ? dutyText(m) : std::string()});
      if (found)
        continue;
    }
    if (m.walksLeft <= 0)
      m.walksLeft = t.kind == Kind::Keeper || t.kind == Kind::Scientist ? 5 : 3;
    m.walksLeft--;
    wander(m, map, fences);
  }
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
// ours: the view direction it faces on screen
CompassDirection Staff::facing(const WorldRenderer &view, const Member &m) const {
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

// ours: the DRT base's art (used while its helicopter's out)
Animation *Staff::baseArt(const Member &m) const {
  const auto &t = this->typeList[m.type];
  const bool onPad = m.heliStage == HeliStage::Parked || m.heliStage == HeliStage::SpinUp || m.heliStage == HeliStage::SpinDown;
  return onPad && t.baseUsed ? t.baseUsed : t.base;
}

// ours: drawing and optional lighting share native playback, including the
// reversed spin-down and one-shot spin-up/tranquillizer sequences.
int Staff::baseFrame(const Member &m,Animation *a)const {
  if(!a)a=baseArt(m);return a?int(std::fmod(m.baseAnimTime*1000/std::max(1u,a->frameTimeMs()),std::max(1,a->frameCount()))):0;
}
// ours: the helicopter's frame
int Staff::helicopterFrame(const Member &m,Animation *a)const {
  if(!a)return 0;int f=int(m.animTime*1000/std::max(1u,a->frameTimeMs()));
  if(m.heliStage==HeliStage::SpinUp || m.heliStage==HeliStage::SpinDown || (m.heliStage==HeliStage::Fire&&m.anim=="fire"))f=std::min(f,std::max(0,a->frameCount()-1));
  if(m.heliStage==HeliStage::SpinDown)f=std::max(0,a->frameCount()-1-f);return f;
}
void Staff::collect(const WorldRenderer &view, const WorldMap &map,
                    std::vector<Fences::Drawable> &all,
                    std::vector<Fences::Drawable> *raised) const {
  std::vector<Fences::Drawable> &out = all;
  for (const Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    // In a tank's water: drawn with the tank (drawInTank), not here
    // A standing show specialist is collected with the platform itself.
    if(m.showTank>=0 && m.phase==2 && !m.climbing)continue;
    bool withTank = m.tank >= 0 && (m.water >= 2 || (m.climbing && m.climbInside));
    if (this->onlyTank != -2 ? (!withTank || m.tank != this->onlyTank) : withTank)
      continue;
    if (t.kind == Kind::Helicopter) {
      const float g = groundAt(map, m.x, m.y);
      float bx, by, bdepth;
      view.worldToScreenF(m.x, m.y, g, bx, by, bdepth);
      const bool lit = m.id == this->hovered;
      const SDL_Color tint{255, 255, 110, 255};
      const auto baseSide = view.screenSide(0, -1);
      float baseFrontDepth = bdepth;
      Animation *base = baseArt(m);
      if (base) {
        // Exactly as a large building: each screen strip sorts at the front
        // of its fixed world footprint, rather than floating over its neighbours.
        float xs[4], ds[4], ignored;
        const float hx = t.footX * 0.5f, hy = t.footY * 0.5f;
        const float px[4] = {m.x-hx,m.x+hx,m.x+hx,m.x-hx};
        const float py[4] = {m.y-hy,m.y-hy,m.y+hy,m.y+hy};
        int left=0,right=0,front=0;
        for(int k=0;k<4;k++) {
          view.worldToScreenF(px[k],py[k],g,xs[k],ignored,ds[k]);
          if(xs[k]<xs[left])left=k;if(xs[k]>xs[right])right=k;if(ds[k]>ds[front])front=k;
        }
        baseFrontDepth = ds[front];
        auto edgeDepth=[&](float x) {
          const int end=x<=xs[front]?left:right;
          const float width=xs[front]-xs[end];
          return std::abs(width)>0.001f?ds[end]+(x-xs[end])/width*(ds[front]-ds[end]):ds[front];
        };
        const float step=std::max(4.0f,view.getTileWidth()/4.0f);
        const int count=std::max(1,static_cast<int>(std::ceil((xs[right]-xs[left])/step)));
        for(int k=0;k<count;k++) {
          const float a=xs[left]+k*step,b=std::min(xs[right],a+step);
          Fences::Drawable strip{edgeDepth((a+b)*0.5f)-0.02f,bx,by,base,baseSide,tint,lit};
          strip.frame=baseFrame(m);strip.clip=true;strip.clipX0=k?a:-1e9f;strip.clipX1=k==count-1?1e9f:b;
          out.push_back(strip);
        }
      }
      if (Animation *heli = this->art(m, m.anim)) {
        const bool parked = m.heliStage == HeliStage::Parked;
        const float x = parked ? m.x+t.padX : m.heliX;
        const float y = parked ? m.y+t.padY : m.heliY;
        const float z = parked ? g+t.padZ : m.heliZ;
        float hx, hy, hdepth;
        view.worldToScreenF(x, y, z, hx, hy, hdepth);
        auto side = parked ? view.screenSide(std::sin(t.padFacing*3.14159265f/4),-std::cos(t.padFacing*3.14159265f/4)) : facing(view,m);
        // An aircraft on/above its roof must sort over every base strip.
        if (std::abs(x-m.x) <= t.footX*0.5f && std::abs(y-m.y) <= t.footY*0.5f && z >= g+t.padZ-0.01f)
          hdepth = std::max(hdepth, baseFrontDepth);
        Fences::Drawable d{hdepth+0.02f,hx,hy,heli,side,tint,lit};
        int frame=helicopterFrame(m,heli);
        d.frame=frame;out.push_back(d);
        if(m.id==selected && selectArrow)out.push_back({hdepth+0.03f,hx,hy-65,selectArrow,CompassDirection::N,tint,false});
      }
      continue;
    }
    Animation *a = this->art(m, m.anim);
    if (!a)
      continue;
    float ms = a->frameTimeMs() ? static_cast<float>(a->frameTimeMs()) : 100.0f;
    int frame = static_cast<int>(m.animTime * 1000.0f / ms);
    // (a climb plays once; climbing down, backwards - fFlyReverse)
    if (m.climbing) {
      int count = std::max(1, a->frameCount());
      frame = std::min(frame, count - 1);
      if (m.climbDown)
        frame = count - 1 - frame;
    } else if (m.reverse && m.playsLeft > 0) {
      // (fPlayReverse: the animation backwards)
      int count = std::max(1, a->frameCount());
      frame = count - 1 - std::min(frame, count - 1);
    }
    // At a tank's surface: the part under the water ("under" and its art's
    // name: undersurfidle, undersurfswim, undersurfpoo) drawn under it, the
    // rest on the surface pass; without one, all of it under
    if (this->onlyTank != -2) {
      Animation *under = nullptr;
      if (m.water == 2 && !m.climbing) {
        int sex = m.female && t.sexes[1] ? 1 : 0;
        auto it = t.anims[sex].find(inWater(m, m.anim));
        if (it != t.anims[sex].end()) {
          std::string key = "under" + it->second.substr(it->second.find_last_of("/\\") + 1);
          if (t.anims[sex].count(key))
            under = this->art(m, key);
        }
      }
      // (climbing the inside of the wall: over the water while above it -
      // down from the rim, up to it - under it below)
      bool above = false;
      if (m.climbing && this->tankDepth) {
        const float surface = groundAt(map, m.inX, m.inY) + this->tankDepth(m.tank);
        above = heightOf(m, map) > surface + 0.05f;
      }
      if (m.climbing) {
        if (above != this->onlySurface)
          continue;
      } else {
        if (this->onlySurface && !under)
          continue;
        if (under && !this->onlySurface)
          a = under;
      }
    }
    float sx, sy, depth, gsx, gsy;
    float z = heightOf(m, map);
    view.worldToScreenF(m.x, m.y, z, sx, sy, depth);
    // On a deck: just after the deck it stands on (sorted by its tile's
    // front corner)
    if (m.layer == 1) {
      int tx = static_cast<int>(std::floor(m.x)), ty = static_cast<int>(std::floor(m.y));
      view.worldToScreenF(tx + 0.5f, ty + 0.5f, 0.0f, gsx, gsy, depth);
      depth += 0.62f;
    }
    bool lit = m.id == this->hovered;
    Fences::Drawable d{depth, sx, sy, a, facing(view, m), SDL_Color{255, 255, 110, 255}, lit};
    d.frame = frame;
    if (m.layer == 1 && raised)
      raised->push_back(d);
    else
      out.push_back(d);
    // The selected one: the arrow over its head
    if (m.id == this->selected && this->selectArrow) {
      int w = 0, h = 0;
      a->queryTexture(CompassDirection::S, &w, &h);
      Fences::Drawable arrow{depth + 0.0001f, sx, sy - 36.0f, this->selectArrow,
                             CompassDirection::N, SDL_Color{255, 255, 255, 255}, false};
      out.push_back(arrow);
    }
  }
  // The one being hired: its likeness at the cursor, green where it can go
  if (this->previewType >= 0 && this->onlyTank == -2) {
    const Type &pt = this->typeList[this->previewType];
    Animation *ghost = pt.kind == Kind::Helicopter && pt.base ? pt.base : this->preview(this->previewType);
    if (Animation *a = ghost) {
      float sx, sy, depth;
      view.worldToScreenF(this->previewX, this->previewY,
                          groundAt(map, this->previewX, this->previewY), sx, sy, depth);
      SDL_Color tint = this->previewFit == Fences::Fit::Ok && !this->previewPoor ? SDL_Color{0, 255, 0, 255}
                                                           : SDL_Color{255, 60, 60, 255};
      const auto side = pt.kind == Kind::Helicopter ? view.screenSide(0, -1) : CompassDirection::SE;
      out.push_back({depth, sx, sy, a, side, tint, true});
    }
  }
}

void Staff::drawInTank(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map, int tank,
                       bool surface) {
  std::vector<Fences::Drawable> out;
  this->onlyTank = tank;
  this->onlySurface = surface;
  this->collect(view, map, out);
  this->onlyTank = -2;
  this->onlySurface = false;
  std::stable_sort(out.begin(), out.end(),
                   [](const Fences::Drawable &a, const Fences::Drawable &b) { return a.depth < b.depth; });
  for (Fences::Drawable &d : out)
    if (d.art)
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tinted ? &d.tint : nullptr, d.frame);
}

// ours: which one is drawn at a screen point
int Staff::pick(float px, float py, const WorldRenderer &view, const WorldMap &map) const {
  int found = -1;
  float best = 1e9f;
  for (const Member &m : this->list) {
    float sx, sy, depth;
    view.worldToScreenF(m.x, m.y, heightOf(m, map), sx, sy, depth);
    // A figure about 12 wide and 30 tall standing at its anchor; a DRT
    // base its pad
    bool heli = this->typeList[m.type].kind == Kind::Helicopter;
    if (heli) {
      const auto &t = this->typeList[m.type];
      float left, top, right, bottom;
      bool hit = t.base && t.base->anchoredBounds(sx, sy, view.screenSide(0,-1), left, top, right, bottom) &&
                 px>=left && px<=right && py>=top && py<=bottom;
      if (m.heliStage != HeliStage::Parked) {
        float hx, hy, hd; view.worldToScreenF(m.heliX,m.heliY,m.heliZ,hx,hy,hd);
        Animation *a = art(m,m.anim);
        const bool aircraftHit = a && a->anchoredBounds(hx,hy,facing(view,m),left,top,right,bottom) &&
                                 px>=left && px<=right && py>=top && py<=bottom;
        if (aircraftHit) { hit=true; sx=hx; sy=hy; }
      }
      if (hit) { float d=std::abs(px-sx)+std::abs(py-sy); if(d<best){best=d;found=m.id;} }
      continue;
    }
    float hw = heli ? 90.0f : 9.0f, top = heli ? 90.0f : 32.0f, bottom = heli ? 40.0f : 4.0f;
    if (px < sx - hw || px > sx + hw || py < sy - top || py > sy + bottom)
      continue;
    float d = std::fabs(px - sx) + std::fabs(py - (sy - 14));
    if (d < best) {
      best = d;
      found = m.id;
    }
  }
  return found;
}
