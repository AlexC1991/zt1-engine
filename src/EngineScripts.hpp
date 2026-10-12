#ifndef ENGINESCRIPTS_HPP
#define ENGINESCRIPTS_HPP

// Scripts written in the ZT1 Lab (Write Code > New script): engine code like
// any other - built in, in src/scripts/ - with the zoo's parts set up and
// handed over, so a script starts with what it needs instead of finding it.
// Each is registered by its name and when it runs:
//
//   void myScript(Zoo &zoo) { ... }
//   ZT_SCRIPT("My script", "what it does", EngineScripts::Once, myScript);
//
//   Once       - run from the Lab (Write Code > Build & run), on the Test Zoo
//   EveryTick  - each tick of the zoo, while it's switched on in the Lab
//   EachAnimal - each tick, once for every animal (ZT_SCRIPT_EACH_ANIMAL)
//
// Nothing runs unless the Lab switches it on: a script is a way to try a
// piece of game code on the live zoo, then move it where it belongs.

#include "Animals.hpp"
#include "Fences.hpp"
#include "Guests.hpp"
#include "Staff.hpp"
#include "WorldMap.hpp"

#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

class World;

namespace EngineScripts {

// What a script is handed: the zoo's parts, the time, and what's picked in
// the Test Zoo
struct Zoo {
  World &world;
  Animals &animals;  // animals.members(), animals.member(id), animals.types()
  Guests &guests;    // guests.guests(), guests.guest(id)
  Staff &staff;      // staff.members(), staff.member(id)
  Fences &fences;    // fences.exhibits(), fences.exhibit(id)
  WorldMap &map;     // map.getTile(x, y)
  double clock;      // seconds of play so far
  float dt;          // seconds this tick (0 for a Once script)
  // What's picked in the Test Zoo (nullptr / -1: nothing of that kind)
  Animals::Member *animal;
  int guest, staffMember, exhibit;
  int tileX, tileY;   // the tile picked or last clicked (-1: none)
  std::string script; // this script's name
  // A line in the Lab's log and a step in Steps
  void say(const std::string &text) const;
};

// A number as words for say(): num(23.5) is "23.5", num(0.333, 2) "0.33"
inline std::string num(double v, int places = 1) {
  char b[48];
  snprintf(b, sizeof b, "%.*f", places, v);
  std::string s = b;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0')
      s.pop_back();
    if (s.back() == '.')
      s.pop_back();
  }
  return s;
}

enum When { Once, EveryTick, EachAnimal };

struct Script {
  std::string name, about;
  When when = Once;
  void (*run)(Zoo &) = nullptr;
  void (*each)(Zoo &, Animals::Member &) = nullptr; // EachAnimal
};

std::vector<Script> &all();
std::set<std::string> &switchedOn();

// The Lab's hooks: what's picked, and where a script's words go
extern std::function<void(Zoo &)> fillPicked;
extern std::function<void(const std::string &)> log;

// A script run now (by name); false when there's none by that name
bool runOnce(World &world, const std::string &name);
// The switched-on scripts, each tick of the zoo (World::update)
void tick(World &world, float dt);

struct Add {
  Add(const char *name, const char *about, When when, void (*run)(Zoo &)) { all().push_back({name, about, when, run, nullptr}); }
  Add(const char *name, const char *about, void (*each)(Zoo &, Animals::Member &)) {
    all().push_back({name, about, EachAnimal, nullptr, each});
  }
};

} // namespace EngineScripts

using EngineScripts::Zoo;
using EngineScripts::num;

#define ZT_SCRIPT_CAT2(a, b) a##b
#define ZT_SCRIPT_CAT(a, b) ZT_SCRIPT_CAT2(a, b)
#define ZT_SCRIPT(name, about, when, fn) static EngineScripts::Add ZT_SCRIPT_CAT(zt_script_, __LINE__)(name, about, when, fn)
#define ZT_SCRIPT_EACH_ANIMAL(name, about, fn) static EngineScripts::Add ZT_SCRIPT_CAT(zt_script_, __LINE__)(name, about, fn)

#endif // ENGINESCRIPTS_HPP
