#include "RenameCheats.hpp"

#include "Ambient.hpp"
#include "Fences.hpp"
#include "Guests.hpp"
#include "LabTrace.hpp"
#include "PlacedObjects.hpp"
#include "Research.hpp"
#include "ResourceManager.hpp"
#include "Staff.hpp"
#include "World.hpp"

#include <map>
#include <set>

namespace {

RenameCheats::Hooks g_hooks;
bool g_marketingTwice = false; // (zoo.exe 0x6392c1: set, never cleared)

// zoo.exe 0x607001 (id): every research category named id (the research
// manager 0x639010's branches +0xc, each one's categories +0x38, its name
// +0xc), each of its programs' effect applied (researchApplyEffect 0x58fb83)
// without its being marked done - researched later, it applies again.
// Returns the programs applied
int researchCategory(int id) {
  int applied = 0;
  for (ResearchBranch &b : Research::get().branches())
    for (ResearchCategory &c : b.categories) {
      if (c.nameId != id)
        continue;
      for (ResearchProgram &p : c.programs) {
        p.cheatApplied++;
        if (g_hooks.research)
          g_hooks.research(p);
        applied++;
      }
    }
  return applied;
}

// zoo.exe 0x607434 ("Zeta Psi"): every man (a guest type neither female,
// +0x320, nor a child, +0x324): his shirt (+0xfc[0]) 5 and his second
// colour 6; unless he's going into, in or coming out of a building
// (0x4108f7: task 0x68-0x6a), stopped (slot 0x198), smell-sick (+0x336)
// and a new task chosen (slot 0x38: chooseTask's smell-sick roll, cSickChance)
int theFraternity(World &world) {
  Guests &guests = world.getGuests();
  int men = 0;
  for (Guests::Guest &g : guests.mutableGuests()) {
    if (g.type < 0 || g.type >= static_cast<int>(guests.types().size()))
      continue;
    const Guests::Type &t = guests.types()[g.type];
    if (t.female || t.child)
      continue;
    men++;
    g.colours[0] = 5;
    g.colours[1] = 6;
    if (g.building >= 0)
      continue;
    g.path.clear();
    g.pathAt = 0;
    g.smelled = true;
    g.recheck = true;
  }
  return men;
}

// zoo.exe 0x6074ed ("Russell C"): every fence that can break (0x4857b9: not
// the zoo's wall or gate, not indestructible) broken (0x4858a4: life and
// strength 0, its break sound) - Fences::breakPiece
int breakFences(World &world) {
  Fences &fences = world.getFences();
  std::vector<Fences::Edge> edges;
  for (const auto &[e, p] : fences.pieces())
    if (fences.lifeOf(e) != 0)
      edges.push_back(e);
  int broken = 0;
  for (const Fences::Edge &e : edges) {
    fences.breakPiece(e);
    broken += fences.lifeOf(e) == 0 ? 1 : 0;
  }
  return broken;
}

// zoo.exe 0x6070ef ("Bob V", a maintenance man): every fence broken (+0x164)
// or worn to its cDecayedLife (+0x160 <= its type's +0x170) made as new
// (fenceStartCondition 0x5b391c)
int mendFences(World &world) {
  Fences &fences = world.getFences();
  std::vector<Fences::Edge> edges;
  for (const auto &[e, p] : fences.pieces())
    if (fences.worn(e))
      edges.push_back(e);
  for (const Fences::Edge &e : edges)
    fences.repair(e);
  return static_cast<int>(edges.size());
}

// zoo.exe 0x607174 ("George W", a maintenance man): every object whose type
// is cFoliage (sceneryTypeLoad 0x4ba5b0, +0x12a) sold with nothing back (the
// flag 0x63b954 set around sellObject 0x50a432(o, 1)); then $300
// (0x43960000) booked as money back for removals (bookRemovalRefund
// 0x4f9329) and into the cash (0x40f018) - once, trees or none
int clearFoliage(World &world) {
  static std::map<std::string, bool> foliage; // file -> cFoliage
  PlacedObjects &objects = world.objectsMutable();
  int removed = 0;
  for (int i = static_cast<int>(objects.objects().size()) - 1; i >= 0; i--) {
    const std::string file = PlacedObjects::fileOf(objects.objects()[i]);
    if (!g_hooks.rm)
      break;
    auto it = foliage.find(file);
    if (it == foliage.end()) {
      bool on = false;
      if (IniReader *ai = g_hooks.rm->getIniReader(file)) {
        on = ai->getInt("characteristics/integers", "cfoliage", 0) == 1;
        delete ai;
      }
      it = foliage.emplace(file, on).first;
    }
    if (!it->second)
      continue;
    objects.remove(i);
    removed++;
  }
  if (removed > 0) {
    world.reindex();
    world.getAnimals().markDirty();
  }
  if (g_hooks.removalRefund)
    g_hooks.removalRefund(300.0);
  return removed;
}

// zoo.exe 0x60d8e3 ("Marwan Jubran"): every object whose type is one of the
// cars - cNameID 0x17fa jeep1, 0x17fb sedan1, 0x17fc semi1, 0x17fe suv1,
// 0x1801 van1, 0x1817 labjeep - turned to rubble (its slot 0x124:
// objectToRubble 0x47c2ac); if any was, $5,500 (0x45abe000) given as a
// donation (0x42ebbe, 0x40f018)
int wreckCars(World &world) {
  static const std::set<int> kCars = {0x17fa, 0x17fb, 0x17fc, 0x17fe, 0x1801, 0x1817};
  std::vector<int> ids;
  for (const PlacedObjects::Object &o : world.getObjects().objects())
    if (kCars.count(world.nameIdOf(o)))
      ids.push_back(o.id);
  const int wrecked = world.rubbleObjects(ids);
  if (wrecked > 0 && g_hooks.donation)
    g_hooks.donation(5500.0);
  return wrecked;
}

// zoo.exe 0x607552 ("Alfred H"): every guest attacked (+0x33b - from its
// next behaviour set it leaves the zoo, running: Guests::decide); then 15
// times, for the crow (cNameID 5052) and the bluejay (5053): the kind put on
// the zoo's spawn list (0x59148e, no check for one already there - so they
// keep coming, as the scenario's own birds do) and one made there and then
// (world slot 0xc's type, its slot 0x24(0); into the world, slot 0x18(1)):
// 30 birds
int theBirds(World &world) {
  Guests &guests = world.getGuests();
  for (Guests::Guest &g : guests.mutableGuests())
    g.attacked = true;
  Ambient &ambient = world.getAmbient();
  int sent = 0;
  for (int round = 0; round < 15; round++)
    for (int nameId : {5052, 5053}) {
      ambient.allow(nameId);
      if (ambient.spawn(world.getMap(), ambient.kindOf(nameId)) >= 0)
        sent++;
    }
  return sent;
}

// zoo.exe 0x60739a (a, b, c): every guest's clothes - its first colour pick
// (+0xfc[0], the shirt) a, its second (+0xfc[1]) b for a man or boy, c for
// a woman or girl (+0x320); and 0x6072e4 (a, b): every building whose kind
// is colour-replaced (cIsColorReplaced, type +0x5a) its first part a, and
// its second b if it has more than one part (type +0x28, 12 bytes a part)
void recolour(World &world, int shirt, int male, int female, int part0, int part1) {
  Guests &guests = world.getGuests();
  for (Guests::Guest &g : guests.mutableGuests()) {
    if (g.type < 0 || g.type >= static_cast<int>(guests.types().size()))
      continue;
    g.colours[0] = shirt;
    g.colours[1] = guests.types()[g.type].female ? female : male;
  }
  PlacedObjects &objects = world.objectsMutable();
  for (PlacedObjects::Object &o : objects.all()) {
    if (o.subClass != "building")
      continue;
    const PlacedObjects::Colouring *c = objects.colouringOf(PlacedObjects::fileOf(o));
    if (!c || c->parts.empty())
      continue;
    objects.setColour(o, 0, part0);
    if (c->parts.size() > 1)
      objects.setColour(o, 1, part1);
  }
}

} // namespace

void RenameCheats::setHooks(const Hooks &hooks) { g_hooks = hooks; }
const RenameCheats::Hooks &RenameCheats::hooks() { return g_hooks; }

// zoo.exe 0x44940b, a guest's names (exact, case and all - the string's
// bytes and length): those ported here; the rest (#235): "Eggman"
// (0x63cfa1 set), "Stinky" (+0x340 | 2), "rpro",
// "Boogyman" (a male type: +0x340 | 4, | 8), "Yellowman" (| 0x10,
// 0x619215), "Redman" (| 0x20, 0x61922d); then the name list (0x449560)
std::string RenameCheats::guestRenamed(World &world, int guest, const std::string &name) {
  Guests &guests = world.getGuests();
  if (guest < 0 || guest >= static_cast<int>(guests.mutableGuests().size()))
    return "";
  if (name == "Alfred H") {
    const int birds = theBirds(world);
    ZT_STEP("0x607552", "Rename cheat: every guest attacked, the birds", {"birds", birds});
    return "0x607552";
  }
  if (name == "Zeta Psi") {
    const int men = theFraternity(world);
    ZT_STEP("0x607434", "Rename cheat: every man's clothes, smell-sick", {"men", men});
    return "0x607434";
  }
  if (name == "Russell C") {
    const int broken = breakFences(world);
    ZT_STEP("0x6074ed", "Rename cheat: every fence broken", {"broken", broken});
    return "0x6074ed";
  }
  // (0x5aa1-0x5aa7, 0x5aad, 0x5aae: the research categories' name ids)
  static const std::pair<const char *, int> kResearch[] = {
      {"Steve Serafino", 23201}, {"Charlie Peterson", 23202}, {"John Wheeler", 23203},
      {"Andrew Binder", 23204},  {"Hank Howie", 23205},       {"Adam Levesque", 23206},
      {"Lou Catanzaro", 23207},  {"Dinoman", 23213},          {"Ron Propst", 23214},
  };
  for (const auto &[who, category] : kResearch)
    if (name == who) {
      const int applied = researchCategory(category);
      ZT_STEP("0x607001", "Rename cheat: a research category's effects, not done", {"category", category},
              {"programs", applied});
      return "0x607001";
    }
  if (name == "Akiyama") {
    // (zoo.exe 0x6000d3: every scenario of the scenario manager 0x638ff8,
    // its token in zoo.ini [scenario] written 0 by 0x48a80a - every one
    // unlocked)
    if (g_hooks.unlockScenarios)
      g_hooks.unlockScenarios();
    ZT_STEP("0x6000d3", "Rename cheat: every scenario unlocked");
    return "0x6000d3";
  }
  if (name == "Marwan Jubran") {
    const int wrecked = wreckCars(world);
    ZT_STEP("0x60d8e3", "Rename cheat: the cars wrecked, a donation", {"cars", wrecked});
    return "0x60d8e3";
  }
  // (Mr. Pink .. Mr. Blonde: 0x60739a's three, then 0x6072e4's two)
  struct Colours {
    const char *name;
    int shirt, male, female, part0, part1;
  };
  static const Colours kColours[] = {
      {"Mr. Pink", 8, 0, 0, 0x15, 0x10},  {"Mr. Orange", 10, 1, 0, 7, 0xb}, {"Mr. Brown", 0xe, 5, 0, 0xe, 0x13},
      {"Mr. White", 0, 6, 1, 0x17, 4},    {"Mr. Blue", 0xf, 0, 3, 0, 0x16}, {"Mr. Blonde", 9, 1, 4, 0xf, 0xc},
  };
  for (const Colours &c : kColours)
    if (name == c.name) {
      recolour(world, c.shirt, c.male, c.female, c.part0, c.part1);
      ZT_STEP("0x60739a", "Rename cheat: every guest's clothes and building's colours", {"name", name});
      return "0x60739a";
    }
  return "";
}

// zoo.exe 0x44940b, a staff member's names: a maintenance worker (class
// 0x638780) whose type's sex (+0xa4) is "m": "George W" 0x607174, "Bob V"
// 0x6070ef; a keeper (0x638760, the scientists and marine specialists
// too): a woman "Susan Kittleson", a man "Chuck Frizelle" (0x6392c1) -
// ported; "The Rock" 0x60d694, the keepers' "Dr. Dolittle" 0x63c2e3 (the
// next birth check of a cNameID 0x1398 or 0x1399 animal passes, then the
// flag is cleared: 0x431117, 0x421a16), "Dr. Scholl" 0x63c2e4, "Dr.
// Frankenstein" 0x64e83a, not yet (#235). The tour guide's "Rosalie"
// (0x638770, a woman: 0x654ad8 set) - nothing reads that flag
bool RenameCheats::marketingTwice() { return g_marketingTwice; }
void RenameCheats::setMarketingTwice(bool on) { g_marketingTwice = on; }

std::string RenameCheats::staffRenamed(World &world, int id, const std::string &name) {
  Staff &staff = world.getStaff();
  const Staff::Member *m = staff.member(id);
  if (!m || m->type < 0 || m->type >= static_cast<int>(staff.types().size()))
    return "";
  const Staff::Type &t = staff.types()[m->type];
  const bool keeper = t.kind == Staff::Kind::Keeper || t.kind == Staff::Kind::Scientist || t.kind == Staff::Kind::Trainer;
  if (keeper && (m->female ? name == "Susan Kittleson" : name == "Chuck Frizelle")) {
    g_marketingTwice = true;
    ZT_STEP("0x6392c1", "Rename cheat: the marketing counted twice for arrivals", {"name", name});
    return "0x6392c1";
  }
  if (t.kind == Staff::Kind::Maint && !m->female) {
    if (name == "George W") {
      const int removed = clearFoliage(world);
      ZT_STEP("0x607174", "Rename cheat: every tree and plant gone, $300", {"removed", removed});
      return "0x607174";
    }
    if (name == "Bob V") {
      const int mended = mendFences(world);
      ZT_STEP("0x6070ef", "Rename cheat: every worn fence as new", {"mended", mended});
      return "0x6070ef";
    }
  }
  return "";
}
