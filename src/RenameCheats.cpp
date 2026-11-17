#include "RenameCheats.hpp"

#include "Ambient.hpp"
#include "Animals.hpp"
#include "Fences.hpp"
#include "IniReader.hpp"
#include "Guests.hpp"
#include "LabTrace.hpp"
#include "OriginalConstants.hpp"
#include "PlacedObjects.hpp"
#include "Research.hpp"
#include "ResourceManager.hpp"
#include "Staff.hpp"
#include "World.hpp"

#include <map>
#include <set>

namespace {

RenameCheats::Hooks g_hooks;

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

// zoo.exe 0x60d694 ("The Rock", a maintenance man): every thing on the
// lists of what satisfies a need (0x639060: [Satisfies] -> its things;
// 0x41239d), each once, whose type is cRubbleable (+0x59: 1 unless its .ai
// says otherwise - the entity type's ctor 0x4be045, sceneryTypeLoad
// 0x4ba5b0) turned to rubble (its class slot 0x124: toRubble 0x611bd6 /
// objectToRubble 0x47c2ac) - World::rubbleObjects
int theRock(World &world) {
  static std::map<std::string, bool> rubbleable; // file -> cRubbleable
  std::vector<int> ids;
  for (const PlacedObjects::Object &o : world.getObjects().objects()) {
    if (o.fence || o.className == "fences")
      continue;
    const std::string file = PlacedObjects::fileOf(o);
    if (!world.getGuests().satisfiesAny(file))
      continue;
    auto it = rubbleable.find(file);
    if (it == rubbleable.end()) {
      bool on = true;
      if (g_hooks.rm)
        if (IniReader *ai = g_hooks.rm->getIniReader(file)) {
          on = ai->getInt("characteristics/integers", "crubbleable", 1) == 1;
          delete ai;
        }
      it = rubbleable.emplace(file, on).first;
    }
    if (it->second)
      ids.push_back(o.id);
  }
  return world.rubbleObjects(ids);
}

// zoo.exe 0x6075ea ("Deer", a grizzly - cNameID 0x1398 - renamed): if the
// zoo has the grizzly's type (world slot 0xc(0x1398)), every type of
// "animals" named "grizzly" (world slot 0x14: its subtypes, the young's
// too) gets cBashStrength (+0x3b4) 501; then every grizzly's exhibit (not
// the zoo's ground, +0x2c) is to look for a way out (+0x30 = 1: its next
// animalFindEscapeFence 0x446088 - ours: an animal looks every time,
// Animals::breakOut). Returns the types changed
int theDeer(World &world) {
  Animals &animals = world.getAnimals();
  int changed = 0;
  for (Animals::Type &t : animals.mutableTypes())
    if (t.key == "grizzly") {
      t.bash = Original::Cheat::deerBashStrength;
      t.young.bash = Original::Cheat::deerBashStrength;
      changed++;
    }
  for (const Animals::Member &m : animals.members())
    if (m.exhibit >= 0 && animals.types()[m.type].nameId == Original::Cheat::grizzlyNameId)
      animals.markDirty(m.exhibit);
  return changed;
}

// zoo.exe 0x607757 ("Comet", a reindeer - cNameID 0x14b4 - renamed): every
// animal whose type's cExpansionID (+0x54, a byte) is 1 - Dinosaur Digs' -
// listed, then each sold with nothing back (the flag 0x63b954 set around
// sellObject 0x50a432(it, 1)), the view told first (zooView 0x4017e5)
int theComet(World &world) {
  std::vector<int> ids;
  for (const Animals::Member &m : world.getAnimals().members())
    if ((world.getAnimals().types()[m.type].expansionId & 0xff) == Original::Cheat::cometExpansionId)
      ids.push_back(m.id);
  for (int id : ids)
    world.sellAnimal(id);
  return static_cast<int>(ids.size());
}

// The name table (zoo.exe 0x639040; 0x514554 fills it): a name, the type
// and subtype it's for, the bonus, the colours, the research it applies,
// and whether once only
struct NameEntry {
  std::string type, subtype;
  int bonusCash = 0, colour[3] = {0, 0, 0};
  std::vector<int> research;
  bool oneTime = true;
};
std::map<std::string, NameEntry> g_nameTable;
std::set<std::string> g_usedNames; // (zoo.exe 0x641860)
RenameCheats::Flags g_flags;

} // namespace

void RenameCheats::setHooks(const Hooks &hooks) { g_hooks = hooks; }
const RenameCheats::Hooks &RenameCheats::hooks() { return g_hooks; }

// zoo.exe 0x44940b, a guest's names (exact, case and all - the string's
// bytes and length, 0x40da1d)
static std::string guestCheat(World &world, int guest, const std::string &name) {
  Guests &guests = world.getGuests();
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
  Guests::Guest &g = guests.mutableGuests()[guest];
  const bool man = g.type >= 0 && g.type < static_cast<int>(guests.types().size()) &&
                   !guests.types()[g.type].female && !guests.types()[g.type].child;
  // "Eggman" (0x5cb02f: any guest): 0x63cfa1 set - eggs made from then on
  // hatch in 5 ticks (animalBecomeEgg 0x5a5283)
  if (name == "Eggman") {
    g_flags.eggman = true;
    ZT_STEP("0x63cfa1", "Rename cheat: eggs hatch in 5 ticks");
    return "0x63cfa1";
  }
  // The guest's own flags (+0x340, a guest's - its type isKindOf guests):
  // "Stinky" any guest; "rpro", "Boogyman", "Yellowman", "Redman" a man
  // (type +0x98 'm'). What they do, each tick: Guests::cheatTick (ZTGuest
  // slot 0x28 0x43a62c). "Yellowman" / "Redman" his own shirt too
  // (0x619215 9, 0x61922d 1)
  struct Flag {
    const char *name;
    int bit;
    bool menOnly;
  };
  static const Flag kFlags[] = {{"Stinky", Original::Cheat::stinky, false},
                                {"rpro", Original::Cheat::rpro, true},
                                {"Boogyman", Original::Cheat::boogyman, true},
                                {"Yellowman", Original::Cheat::yellowman, true},
                                {"Redman", Original::Cheat::redman, true}};
  for (const Flag &f : kFlags)
    if (name == f.name && (!f.menOnly || man)) {
      g.cheatFlags |= f.bit;
      if (f.bit == Original::Cheat::yellowman)
        g.colours[0] = Original::Cheat::yellowShirt;
      if (f.bit == Original::Cheat::redman)
        g.colours[0] = Original::Cheat::redShirt;
      ZT_STEP("0x43a62c", "Rename cheat: a guest's flag (+0x340)", {"name", name}, {"flag", f.bit});
      return "0x43a62c";
    }
  return "";
}

std::string RenameCheats::guestRenamed(World &world, int guest, const std::string &name) {
  Guests &guests = world.getGuests();
  if (guest < 0 || guest >= static_cast<int>(guests.mutableGuests().size()))
    return "";
  const std::string cheat = guestCheat(world, guest, name);
  // (then the name table, whatever matched - 0x4494b5; ours: a guest's
  // subtype unmeasured, its kind's name for both)
  Guests::Guest &g = guests.mutableGuests()[guest];
  const std::string kind = g.type >= 0 && g.type < static_cast<int>(guests.types().size()) ? guests.types()[g.type].key : "";
  applyNameTable(name, kind, kind, g.colours, 4);
  return cheat;
}

// zoo.exe 0x44940b, a staff member's names (the type +0xa4, "m" or "f",
// its sex). A tour guide (class 0x638770): a woman "Rosalie" - 0x654ad8
// set, which nothing reads. A maintenance worker (0x638780), a man:
// "George W" 0x607174, "Bob V" 0x6070ef, "The Rock" 0x60d694. A keeper
// (0x638760, the scientists and marine specialists too): a man "Dr.
// Dolittle" (0x63c2e3: the next breeding check of a grizzly or black bear
// passes - Animals' 0x5da846, 0x5db99f); a dinosaur scientist (type 0x254f)
// "Dr. Scholl" (0x63c2e4: scientists move at twice the rate, setMoveSpeed
// 0x41678b - and his own rate now x 2 through it, so x 4 till his next
// move); a woman "Susan Kittleson", a man "Chuck Frizelle" (0x6392c1); a
// scientist "Dr. Frankenstein" (0x64e83a: Job4_HealAnimal::atTarget
// 0x4702e8)
bool RenameCheats::marketingTwice() { return g_flags.marketingTwice; }
void RenameCheats::setMarketingTwice(bool on) { g_flags.marketingTwice = on; }
const RenameCheats::Flags &RenameCheats::flags() { return g_flags; }
void RenameCheats::setFlags(const Flags &f) { g_flags = f; }
void RenameCheats::dolittleUsed() { g_flags.dolittle = false; }

static std::string staffCheat(World &world, Staff::Member &m, const Staff::Type &t, const std::string &name) {
  if (t.kind == Staff::Kind::Guide) {
    if (m.female && name == "Rosalie") {
      g_flags.rosalie = true;
      ZT_STEP("0x654ad8", "Rename cheat: a flag nothing reads", {"name", name});
      return "0x654ad8";
    }
    return "";
  }
  if (t.kind == Staff::Kind::Maint) {
    if (m.female)
      return "";
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
    if (name == "The Rock") {
      const int wrecked = theRock(world);
      ZT_STEP("0x60d694", "Rename cheat: everything that satisfies a need to rubble", {"wrecked", wrecked});
      return "0x60d694";
    }
    return "";
  }
  const bool keeper = t.kind == Staff::Kind::Keeper || t.kind == Staff::Kind::Scientist || t.kind == Staff::Kind::Trainer;
  if (!keeper)
    return "";
  if (!m.female && name == "Dr. Dolittle") {
    g_flags.dolittle = true;
    ZT_STEP("0x63c2e3", "Rename cheat: the next bear breeding check passes");
    return "0x63c2e3";
  }
  const bool scientist = t.nameId == Original::Cheat::scientistNameId;
  if (scientist && name == "Dr. Scholl") {
    g_flags.scholl = true;
    m.moveBoost = Original::Cheat::schollRateFactor;
    ZT_STEP("0x63c2e4", "Rename cheat: scientists move at twice the rate");
    return "0x63c2e4";
  }
  if (m.female ? name == "Susan Kittleson" : name == "Chuck Frizelle") {
    g_flags.marketingTwice = true;
    ZT_STEP("0x6392c1", "Rename cheat: the marketing counted twice for arrivals", {"name", name});
    return "0x6392c1";
  }
  if (scientist && name == "Dr. Frankenstein") {
    g_flags.frankenstein = true;
    ZT_STEP("0x64e83a", "Rename cheat: 1 heal in 10 makes a dinosaur");
    return "0x64e83a";
  }
  return "";
}

std::string RenameCheats::staffRenamed(World &world, int id, const std::string &name) {
  Staff &staff = world.getStaff();
  Staff::Member *m = staff.member(id);
  if (!m || m->type < 0 || m->type >= static_cast<int>(staff.types().size()))
    return "";
  const Staff::Type &t = staff.types()[m->type];
  const std::string cheat = staffCheat(world, *m, t, name);
  // (then the name table - 0x4494b5; a staff member has no colour picks)
  applyNameTable(name, t.key, m->female ? "f" : "m", nullptr, 0);
  return cheat;
}

// zoo.exe 0x44940b, an animal's names (an animal's - isKindOf animals - by
// its type's cNameID): a grizzly (0x1398) "Deer" - 0x654ad9 set (read by
// nothing) and 0x6075ea; a reindeer (0x14b4) "Comet" - 0x607757
std::string RenameCheats::animalRenamed(World &world, int id, const std::string &name) {
  Animals &animals = world.getAnimals();
  const Animals::Member *m = animals.member(id);
  if (!m || m->type < 0 || m->type >= static_cast<int>(animals.types().size()))
    return "";
  const Animals::Type &t = animals.types()[m->type];
  const std::string key = t.key, subtype = m->female ? "f" : "m";
  std::string cheat;
  if (t.nameId == Original::Cheat::grizzlyNameId && name == "Deer") {
    g_flags.deer = true;
    const int types = theDeer(world);
    ZT_STEP("0x6075ea", "Rename cheat: grizzlies break out (cBashStrength 501)", {"types", types});
    cheat = "0x6075ea";
  } else if (t.nameId == Original::Cheat::reindeerNameId && name == "Comet") {
    const int sold = theComet(world);
    ZT_STEP("0x607757", "Rename cheat: every Dinosaur Digs animal sold for nothing", {"sold", sold});
    cheat = "0x607757";
  }
  // (then the name table - 0x4494b5; an animal has no colour picks)
  applyNameTable(name, key, subtype, nullptr, 0);
  return cheat;
}

// zoo.exe 0x514554: every resource skeepr*.cfg (0x4b41f2), each of its
// sections a name (kept as written: the original's names are exact; the
// first section of a name makes its entry, later ones add to it): type,
// subtype, addresearch (a list, added to), bonuscash, color0-2,
// oneTimeBonus (1 unless it says otherwise). ours: a number it doesn't set
// is 0 (the original leaves it unset)
void RenameCheats::loadNameTable(ResourceManager *rm) {
  g_nameTable.clear();
  if (!rm)
    return;
  for (const std::string &file : rm->listResources("skeepr", ".cfg")) {
    int size = 0;
    void *data = rm->getFileBytes(file, &size);
    if (!data)
      continue;
    std::string text(static_cast<const char *>(data), static_cast<size_t>(size));
    free(data);
    IniReader ini(const_cast<char *>(text.data()), text.size());
    size_t at = 0;
    while (at < text.size()) {
      size_t nl = text.find('\n', at);
      std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      at = nl == std::string::npos ? text.size() : nl + 1;
      const size_t open = line.find('['), close = line.find(']');
      if (open == std::string::npos || close == std::string::npos || close < open ||
          line.find_first_not_of(" \t") != open)
        continue;
      const std::string name = line.substr(open + 1, close - open - 1);
      NameEntry &e = g_nameTable[name];
      e.type = ini.get(name, "type", e.type);
      e.subtype = ini.get(name, "subtype", e.subtype);
      for (const std::string &r : ini.getList(name, "addresearch"))
        e.research.push_back(std::atoi(r.c_str()));
      e.bonusCash = ini.getInt(name, "bonuscash", e.bonusCash);
      for (int i = 0; i < 3; i++)
        e.colour[i] = ini.getInt(name, "color" + std::to_string(i), e.colour[i]);
      e.oneTime = ini.getInt(name, "onetimebonus", 1) == 1;
    }
  }
}

std::set<std::string> &RenameCheats::usedNames() { return g_usedNames; }

void RenameCheats::setNameEntry(const std::string &name, const std::string &type, const std::string &subtype,
                                int bonusCash, int c0, int c1, int c2, const std::vector<int> &research,
                                bool oneTime) {
  if (name.empty()) {
    g_nameTable.clear();
    return;
  }
  NameEntry &e = g_nameTable[name];
  e.type = type;
  e.subtype = subtype;
  e.bonusCash = bonusCash;
  e.colour[0] = c0;
  e.colour[1] = c1;
  e.colour[2] = c2;
  e.research = research;
  e.oneTime = oneTime;
}

// zoo.exe 0x44940b's end (0x4494b5, not while a zoo loads - 0x638148
// +0x441): the name looked up (0x449560); once only (+0x50) and already
// used (0x641860), nothing. Its type, if given, must be the unit's type's
// (+0x98), and then its subtype, if the type is given and the subtype too,
// the unit's (+0xa4) - the original asks the type's length both times
// (0x5cb67b), so a subtype alone is never asked. Then the bonus booked as
// a donation (0x6089e8: booksAddDonation 0x42ebbe, the cash 0x40f018), its
// three colour picks (0x60875c: each the unit has), each research program
// named (0x5944b6, by its name id) applied - not done (0x58fb83) - with a
// research manager (0x639010), and the name noted as used (0x607ab7)
bool RenameCheats::applyNameTable(const std::string &name, const std::string &typeName, const std::string &subtype,
                                  int *colours, int colourCount) {
  auto it = g_nameTable.find(name);
  if (it == g_nameTable.end())
    return false;
  const NameEntry &e = it->second;
  if (e.oneTime && g_usedNames.count(name))
    return false;
  if (!e.type.empty() && e.type != typeName)
    return false;
  if (!e.type.empty() && !e.subtype.empty() && e.subtype != subtype)
    return false;
  if (g_hooks.donation)
    g_hooks.donation(static_cast<double>(static_cast<float>(e.bonusCash)));
  for (int i = 0; i < 3; i++)
    if (colours && i < colourCount)
      colours[i] = e.colour[i] & 0xff;
  for (int id : e.research) {
    ResearchProgram *found = nullptr;
    for (ResearchBranch &b : Research::get().branches())
      for (ResearchCategory &c : b.categories)
        for (ResearchProgram &p : c.programs)
          if (!found && p.nameId == id)
            found = &p;
    if (found) {
      found->cheatApplied++;
      if (g_hooks.research)
        g_hooks.research(*found);
    }
  }
  g_usedNames.insert(name);
  ZT_STEP("0x449560", "Rename: the name table's entry", {"name", name}, {"bonuscash", e.bonusCash});
  return true;
}
