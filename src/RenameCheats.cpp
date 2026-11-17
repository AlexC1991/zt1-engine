#include "RenameCheats.hpp"

#include "Ambient.hpp"
#include "Guests.hpp"
#include "LabTrace.hpp"
#include "PlacedObjects.hpp"
#include "World.hpp"

namespace {

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

// zoo.exe 0x44940b, a guest's names (exact, case and all - the string's
// bytes and length): those ported here; the rest (#235): "Russell C"
// 0x6074ed (every fence), "Steve Serafino" .. "Lou Catanzaro", "Dinoman",
// "Ron Propst" 0x607001 (a research category's programs' effects, ids
// 23201-23207, 23213, 23214), "Zeta Psi" 0x607434, "Akiyama" 0x6000d3,
// "Eggman" (0x63cfa1 set), "Marwan Jubran" 0x60d8e3, "Stinky" (+0x340 | 2),
// "rpro", "Boogyman" (a male type: +0x340 | 4, | 8), "Yellowman" (| 0x10,
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
