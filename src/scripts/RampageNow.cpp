// Rampage now: Makes the picked animal unhappy and starts its rampage now, to test what a rampage does
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
void rampageNow(Zoo &zoo) {
  // The animal picked in the Test Zoo (click one first)
  Animals::Member *a = zoo.animal;
  if (!a) {
    zoo.say("Pick an animal in the Test Zoo first");
    return;
  }
  // Unhappy (at or under its cRampageThreshold, else the next check ends
  // it) and rampaging, as zoo.exe 0x438514 sets it - with no chance roll
  a->happiness = 0;
  // (and wide awake: a tired one picks bSleep first, and a sleep ends a
  // rampage - zoo.exe 0x4375d3)
  a->energy = 0;
  a->rampaging = true;
  a->rampageTicks = 0;
  // (what it's doing now ends, so it picks its rampage set straight away -
  // the original waits for it to finish: this is to test, not to copy)
  a->stack.clear();
  a->path.clear();
  a->pathAt = 0;
  a->stepping = false;
  zoo.say(a->name + " rampages, from " + num(a->x) + ", " + num(a->y));
}
} // namespace

ZT_SCRIPT("Rampage now", "Makes the picked animal unhappy and starts its rampage now, to test what a rampage does", EngineScripts::Once, rampageNow);
