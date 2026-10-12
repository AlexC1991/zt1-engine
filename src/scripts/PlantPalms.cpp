// Plant palms: Plants a palm on every other free tile of the picked animal's exhibit
//
// A Lab script (Write Code > New script): engine code, built in with the
// rest, run on the Test Zoo from Write Code - once, each time you press ▶ Build & run.
// Set up for you, in zoo:
//   zoo.world                 the whole world (World.hpp)
//   zoo.animals               animals.members(), animals.member(id), animals.types()
//   zoo.guests                guests.guests(), guests.guest(id)
//   zoo.staff                 staff.members(), staff.member(id)
//   zoo.fences                fences.exhibits(), fences.exhibit(id)
//   zoo.map                   map.getTile(x, y)
//   zoo.clock, zoo.dt         seconds of play so far; seconds this tick (0: once)
//   zoo.animal                the animal picked in the Test Zoo (nullptr: none)
//   zoo.guest, zoo.staffMember, zoo.exhibit   ids picked (-1: none)
//   zoo.tileX, zoo.tileY      the tile picked, or under the mouse (-1: none)
//   zoo.say("...")            a line in the engine log, and a step in Steps
//   num(x), num(x, 2)         a number as words (23.5, not 23.500000)
// Copying zoo.exe? Cite it, and the Lab shows it in our engine:
//   ZT_STEP("0x<its address>", "What it does", {"name", value});
#include "../EngineScripts.hpp"
#include "../LabTrace.hpp"
#include "../World.hpp"

#include <cmath>
#include <string>

namespace {
void plantPalms(Zoo &zoo) {
  // The animal picked in the Test Zoo (click one first)
  Animals::Member *a = zoo.animal;
  if (!a) {
    zoo.say("Pick an animal in the Test Zoo first");
    return;
  }
  // Its exhibit's tiles, in a checkerboard: a palm on each one nothing
  // stands on (and the animal isn't on)
  PlacedObjects &objects = zoo.world.objectsMutable();
  const WorldMap &map = zoo.map;
  int planted = 0;
  for (int y = 0; y < map.getHeight(); y++)
    for (int x = 0; x < map.getWidth(); x++) {
      if ((x + y) % 2 || zoo.fences.exhibitAt(x, y) != a->exhibit)
        continue;
      if (static_cast<int>(a->x) == x && static_cast<int>(a->y) == y)
        continue;
      bool taken = false;
      for (const PlacedObjects::Object &o : objects.objects())
        taken = taken || (!o.fence && std::fabs(o.x - (x + 0.5f)) < 0.6f && std::fabs(o.y - (y + 0.5f)) < 0.6f);
      if (!taken && objects.add("scenery/foliage/palm.ai", x + 0.5f, y + 0.5f, 0))
        planted++;
    }
  zoo.world.reindex();
  zoo.say("Planted " + std::to_string(planted) + " palms in " + a->name + "'s exhibit");
}
} // namespace

ZT_SCRIPT("Plant palms", "Plants a palm on every other free tile of the picked animal's exhibit", EngineScripts::Once, plantPalms);
