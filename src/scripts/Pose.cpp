// Pose: Holds the picked animal at a spot, facing one way, playing one animation over and over - to look at how it's drawn
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
// Where it stands (tiles), the way it faces (world: +y is south-east on
// screen at the first turn of the view) and what it plays - change these
const float kX = 22.4f, kY = 42.7f;
const float kFaceX = -1.0f, kFaceY = 0.0f;
const char *kAnim = "hunt_walk";

void pose(Zoo &zoo) {
  // The animal picked - or, while a tile's picked, the first animal, posed
  // on that tile's middle (so a click on the map moves it, no build)
  static float atX = kX, atY = kY;
  Animals::Member *a = zoo.animal;
  if (!a && !zoo.animals.members().empty())
    a = zoo.animals.member(zoo.animals.members().front().id);
  if (!a)
    return;
  if (!zoo.animal && zoo.tileX >= 0 && (atX != zoo.tileX + 0.5f || atY != zoo.tileY + 0.5f)) {
    atX = zoo.tileX + 0.5f;
    atY = zoo.tileY + 0.5f;
    zoo.say(a->name + " posed on tile " + num(zoo.tileX) + ", " + num(zoo.tileY));
  }
  // Held there: no route, no behaviour of its own, its needs left alone
  a->stack.clear();
  a->path.clear();
  a->pathAt = 0;
  a->stepping = true;
  a->x = atX;
  a->y = atY;
  a->fx = kFaceX;
  a->fy = kFaceY;
  if (a->anim != kAnim) {
    a->anim = kAnim;
    a->animTime = 0;
    zoo.say(a->name + " posed at " + num(atX) + ", " + num(atY) + " playing " + kAnim);
  }
  a->mode = Animals::Member::Mode::Loop;
  a->playFor = 1e9f;
}
} // namespace

ZT_SCRIPT("Pose", "Holds the picked animal at a spot, facing one way, playing one animation over and over - to look at how it's drawn", EngineScripts::EveryTick, pose);
