// Zoo count: Says how many animals and guests, once a second
//
// A Lab script (Write Code > New script): engine code, built in with the
// rest, run on the Test Zoo from Write Code - each tick of the zoo while it’s switched on (its bar: On).
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
void zooCount(Zoo &zoo) {
  // Every tick is a lot of ticks: this one speaks once a second
  // (and a new zoo - its clock back at 0 - starts it again)
  static double last = -1;
  if (zoo.clock >= last && zoo.clock - last < 1.0)
    return;
  last = zoo.clock;
  int loose = 0;
  for (const Animals::Member &m : zoo.animals.members())
    loose += m.escaped ? 1 : 0;
  zoo.say(std::to_string(zoo.animals.members().size()) + " animals, " + std::to_string(loose) + " loose, " +
          std::to_string(zoo.guests.guests().size()) + " guests");
}
} // namespace

ZT_SCRIPT("Zoo count", "Says how many animals and guests, once a second", EngineScripts::EveryTick, zooCount);
