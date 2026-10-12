// The Lab's engine scripts (EngineScripts.hpp): the list, the switched-on
// ones run each tick, and the zoo handed to each
#include "EngineScripts.hpp"

#include "LabTrace.hpp"
#include "World.hpp"

namespace EngineScripts {

std::vector<Script> &all() {
  static std::vector<Script> scripts;
  return scripts;
}

std::set<std::string> &switchedOn() {
  static std::set<std::string> on;
  return on;
}

std::function<void(Zoo &)> fillPicked;
std::function<void(const std::string &)> log;

void Zoo::say(const std::string &text) const {
  ZT_STEP("", "A script says", {"script", script}, {"says", text});
  if (log)
    log("[" + script + "] " + text);
}

namespace {
Zoo zooOf(World &world, const std::string &name, float dt) {
  Zoo z{world, world.getAnimals(), world.getGuests(), world.getStaff(), world.getFences(), world.getMap(), world.getClock(), dt,
        nullptr, -1, -1, -1, -1, -1, name};
  if (fillPicked)
    fillPicked(z);
  return z;
}

void runScript(World &world, const Script &s, float dt) {
  ZT_WHO("script", 0);
  Zoo z = zooOf(world, s.name, dt);
  if (s.run) {
    s.run(z);
  } else if (s.each) {
    // (by id, not by reference into the list: a script may adopt or remove)
    std::vector<int> ids;
    for (const Animals::Member &m : z.animals.members())
      ids.push_back(m.id);
    for (int id : ids)
      if (Animals::Member *m = z.animals.member(id))
        s.each(z, *m);
  }
}
} // namespace

bool runOnce(World &world, const std::string &name) {
  for (const Script &s : all())
    if (s.name == name) {
      runScript(world, s, 0.0f);
      return true;
    }
  return false;
}

void tick(World &world, float dt) {
  if (switchedOn().empty())
    return;
  for (const Script &s : all())
    if (s.when != Once && switchedOn().count(s.name))
      runScript(world, s, dt);
}

} // namespace EngineScripts
