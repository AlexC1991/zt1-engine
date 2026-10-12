// Where is it: Says where the picked animal is and what it's doing
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
void whereIsIt(Zoo &zoo) {
  // The animal picked in the Test Zoo (click one first)
  Animals::Member *a = zoo.animal;
  if (!a) {
    zoo.say("Pick an animal in the Test Zoo first");
    return;
  }
  const Animals::Type &type = zoo.animals.types()[a->type];
  zoo.say(a->name + " (" + type.file + ") at " + num(a->x) + ", " + num(a->y) + ": " + a->anim + ", hunger " + num(a->hunger, 0));
}
} // namespace

ZT_SCRIPT("Where is it", "Says where the picked animal is and what it's doing", EngineScripts::Once, whereIsIt);
