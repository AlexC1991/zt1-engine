#include "World.hpp"
#include "Shows.hpp"
#include "SaveGame.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"
#include <cstdlib>
#include <sstream>

// ours: ZT_JOIN_ZOO=<file.zoo> (ZT_FLAT_LAB=1, sm_aqua) - two joined tanks to
// watch a keeper in the original (#297, #306): a home tank (orcas) and a show
// tank joined to it by a portal, the show tank's water dirty, one marine
// specialist assigned to neither; saved as a shared .zoo the original loads
// too. A measuring aid: the original's habitatNextNeedingVisit 0x49e8fa
std::string buildJoinedTankZoo(World &world, ZooSim &sim, UiGameScreen &hud, const std::string &mapPath,
                               const std::string &file) {
  auto &f = world.getFences();
  auto &map = world.getMap();
  auto &a = world.getAnimals();
  auto &st = world.getStaff();
  auto &shows = world.getShows();
  shows.clear();
  a.clear();
  st.clear();
  int drag = 10;
  auto rectangle = [&](int x, int y, int w, int h, int type) {
    int base = drag;
    drag += 4;
    for (int i = x; i < x + w; i++) {
      f.place({true, i, y}, type, base, map);
      f.place({true, i, y + h}, type, base + 1, map);
    }
    for (int j = y; j < y + h; j++) {
      f.place({false, x, j}, type, base + 2, map);
      f.place({false, x + w, j}, type, base + 3, map);
    }
    for (int id : f.updateExhibits(map, 1, 0, 1))
      f.finishExhibit(id, "Join test " + std::to_string(id), map);
    return f.exhibitAt(x, y);
  };
  const int tank = rectangle(42, 24, 10, 10, f.typeIndex("tankwal3"));
  const int home = rectangle(28, 24, 14, 14, f.typeIndex("tankwal2"));
  if (tank < 0 || home < 0)
    return "the tanks didn't form";
  for (int id : {tank, home})
    while (f.exhibit(id)->wallTop() < f.exhibit(home)->floorHeight + 10 && f.canAdjustWall(id, 1))
      f.adjustWall(id, 1);
  for (int k = 0; k < 100; ++k)
    world.update(SDL_GetKeyboardState(nullptr), 0.05f);
  shows.refresh();
  const int one = a.adopt(a.typeOfFile("animals/orca.ai"), false, 34.5f, 31.5f, map, f);
  const int two = a.adopt(a.typeOfFile("animals/orca.ai"), false, 36.5f, 31.5f, map, f);
  if (one < 0 || two < 0)
    return "no orcas";
  for (int k = 0; k < 40; ++k)
    world.update(SDL_GetKeyboardState(nullptr), 0.05f);
  // ZT_JOIN_SHOWANIMAL=1: an animal in the show tank too, so both tanks have work for the sim
  if (std::getenv("ZT_JOIN_SHOWANIMAL"))
    if (a.adopt(a.typeOfFile("animals/orca.ai"), false, 46.5f, 29.5f, map, f) < 0)
      return "no third orca";
  // (the show tank's water dirty: a tank needing cleaning is work for a specialist)
  if (Fences::Exhibit *ex = f.exhibit(tank))
    ex->purity = 20.0f;
  const int specialist = st.hire(st.typeOfFile("staff/trainer.ai"), 54.5f, 24.5f, map, f);
  if (specialist < 0)
    return "no specialist hired";
  // ZT_JOIN_TWO=1: both tanks dirty and a second specialist, so one finishes while the other is still
  // at the other tank (the chooser's pass rules: a keeper already there)
  if (std::getenv("ZT_JOIN_TWO")) {
    if (Fences::Exhibit *ex = f.exhibit(home))
      ex->purity = 20.0f;
    if (st.hire(st.typeOfFile("staff/trainer.ai"), 54.5f, 26.5f, map, f) < 0)
      return "no second specialist hired";
  }
  world.reindex();
  if (!SaveGame::save(file, world, sim, hud, mapPath))
    return "save failed: " + SaveGame::lastError();
  // ZT_JOIN_SIM=1: 300 s of the zoo, the specialist's job / exhibit / status logged on each change
  // (ours, to compare with the original's task sequence: Visit -> Visit with no monitoring between)
  if (std::getenv("ZT_JOIN_SIM")) {
    int lastJob = -1, lastVisit = -2, lastDuty = -1;
    // The check: the first visit to the show tank (dirty, no animal - the
    // original's first pick), then on to the home tank with no idle between
    // (zoo.exe 0x49ebe7 -> habitatNextNeedingVisit 0x49e8fa, measured)
    int firstVisit = -1, secondVisit = -1;
    bool idleBetween = false, idleSeen = false;
    for (int k = 0; k < 6000; ++k) {
      world.update(SDL_GetKeyboardState(nullptr), 0.05f);
      if (const Staff::Member *m = st.member(specialist))
        if (static_cast<int>(m->job) != lastJob || m->visit != lastVisit || m->dutyId != lastDuty) {
          lastJob = static_cast<int>(m->job);
          lastVisit = m->visit;
          lastDuty = m->dutyId;
          SDL_Log("[JoinSim] %.2f s job %d visit %d status %d pos %.1f,%.1f tank %d", k * 0.05, lastJob, lastVisit, lastDuty,
                  m->x, m->y, m->tank);
          if (m->job == Staff::Job::None && firstVisit >= 0 && secondVisit < 0)
            idleSeen = true;
          if (m->job == Staff::Job::Visit) {
            if (firstVisit < 0)
              firstVisit = m->visit;
            else if (secondVisit < 0 && m->visit != firstVisit) {
              secondVisit = m->visit;
              idleBetween = idleSeen;
            }
          }
        }
    }
    const bool ok = firstVisit == tank && secondVisit == home && !idleBetween;
    SDL_Log("[JoinSim] %s the specialist's first visit %d (show tank %d), then %d (home tank %d), idle between: %d",
            ok ? "PASS" : "FAIL", firstVisit, tank, secondVisit, home, idleBetween ? 1 : 0);
    if (!ok)
      return "the hop check failed";
  }
  std::ostringstream r;
  r << "ok show tank #" << tank << " home #" << home << ", orcas " << one << "," << two << ", specialist " << specialist;
  return r.str();
}
