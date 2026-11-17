// Picking up and putting down a guest (#227): Guest Information's
// PickupGuest (ZUPDATE1's infogst.lyt 3416), where a held guest may go and
// what it does once put down - zoo.exe infoPickUp_click 0x46af10 ->
// 0x46ab75, zooViewMouse 0x4ded90's guest branch and placementCheck
// 0x4f885e, placeObject 0x4d923c, guestEnterWater 0x46b6a3, endPlacing
// 0x443df7. Run from runParityChecks (the flat Basic Grass test zoo)
#include "World.hpp"
#include "Features.hpp"
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

std::string runGuestPickupChecks(World &world, ResourceManager *rm, int &passed, int &total) {
  (void)rm;
  auto &map = world.getMap();
  auto &f = world.getFences();
  auto &g = world.getGuests();
  auto &objects = world.objectsMutable();
  std::ostringstream out;
  auto check = [&](const std::string &name, bool ok) {
    ++total;
    passed += ok;
    out << (ok ? "PASS " : "FAIL ") << name << "\n";
  };
  // Room: 14 x 11 tiles in the zoo, no exhibit, no object (paths put back
  // as they were after)
  int bx = -1, by = -1;
  for (int y = 2; y < map.getHeight() - 13 && bx < 0; ++y)
    for (int x = 2; x < map.getWidth() - 16 && bx < 0; ++x) {
      bool ok = true;
      for (int yy = y - 1; yy <= y + 11 && ok; ++yy)
        for (int xx = x - 1; xx <= x + 14 && ok; ++xx)
          ok = f.exhibitAt(xx, yy) < 0 && f.insideZoo(xx, yy) && !world.getIndex().blocked(xx, yy);
      if (ok) {
        bx = x;
        by = y;
      }
    }
  out << "PICKUP room at " << bx << "," << by << " on a " << map.getWidth() << "x" << map.getHeight() << " map\n";
  check("#227: room for the guest pick-up fixture", bx >= 0);
  if (bx < 0)
    return out.str();
  const size_t objectsBefore = objects.objects().size();
  // A path along the top; a restroom; a lake tile; a steep tile; a 4x4 tank
  std::vector<std::pair<std::pair<int, int>, std::string>> pathsWere;
  for (int yy = by - 1; yy <= by + 11; ++yy)
    for (int xx = bx - 1; xx <= bx + 14; ++xx)
      if (map.isPath(xx, yy)) {
        const int t = map.getPathType(xx, yy);
        pathsWere.push_back({{xx, yy}, t >= 0 && t < static_cast<int>(map.getPathTypes().size()) ? map.getPathTypes()[t] : "path"});
        map.setPath(xx, yy, "");
      }
  for (int x = bx; x <= bx + 12; ++x)
    map.setPath(x, by, "path");
  const bool loo = objects.add("scenery/building/bathroom.ai", bx + 4.0f, by + 3.0f, 0);
  MapTile *lake = map.getTileMutable(bx + 8, by + 3);
  const auto lakeWas = lake ? lake->terrainType : 0;
  if (lake)
    lake->terrainType = 9;
  map.raiseVertex(bx + 11, by + 3, 2);
  int drag = 900;
  const int wall = f.typeIndex("atltank");
  for (int k = 0; k < 4; ++k) {
    f.place({true, bx + k, by + 6}, wall, drag, map);
    f.place({true, bx + k, by + 10}, wall, drag + 1, map);
    f.place({false, bx, by + 6 + k}, wall, drag + 2, map);
    f.place({false, bx + 4, by + 6 + k}, wall, drag + 3, map);
  }
  for (int id : f.updateExhibits(map, 1, 0, 1))
    f.finishExhibit(id, "Pick-up tank", map);
  const int tankId = f.exhibitAt(bx + 1, by + 7);
  Fences::Exhibit *tank = f.exhibit(tankId);
  if (tank)
    tank->water = 1;
  world.reindex();
  check("#227: the fixture's restroom and tank", loo && tank && tank->tank);
  g.clear();
  g.setEntrance(bx, by);
  g.admit(0);
  if (g.list.empty())
    return out.str();
  const int id = g.list.front().id;
  auto guest = [&]() { return const_cast<Guests::Guest *>(g.guest(id)); };
  {
    Guests::Guest &q = *guest();
    q.x = bx + 2.5f;
    q.y = by + 0.5f;
    q.state = Guests::State::Walking;
    q.ride = 8061;
    q.leaving = true;
  }
  // Picked up: held, off its ride, out of the world - nothing it does goes
  // on (0x46ab75)
  world.pickUpGuest(id);
  const float hx = guest()->x, hy = guest()->y;
  guest()->x = bx + 1.5f;
  guest()->y = by + 2.5f;
  const float tickBefore = guest()->tickMs, timeBefore = guest()->animTime;
  for (int k = 0; k < 40; ++k)
    g.update(0.1f, 0, 0);
  const bool held = g.carried == id && guest()->ride == 0 && guest()->tickMs == tickBefore &&
                    guest()->animTime == timeBefore && guest()->x == bx + 1.5f;
  check("#227: picked up (PickupGuest, 0x46ab75): held, off its ride, nothing it does goes on", held);
  // Where it may go (zooViewMouse 0x4ded90's guest branch, placementCheck)
  auto where = [&](float x, float y, int &message) { return g.canPutDown(x, y, message); };
  int mGrass = -1, mPath = -1, mLoo = -1, mSteep = -1, mLake = -1, mTank = -1, mShow = -1, mOut = -1;
  const bool grass = where(bx + 1.5f, by + 2.5f, mGrass);
  const bool path = where(bx + 6.5f, by + 0.5f, mPath);
  const bool overLoo = where(bx + 4.0f, by + 3.0f, mLoo);
  const bool steep = where(bx + 10.5f, by + 2.5f, mSteep);
  const bool onLake = where(bx + 8.5f, by + 3.5f, mLake);
  const bool inTank = where(bx + 1.5f, by + 7.5f, mTank);
  bool inShow = true;
  if (tank) {
    tank->show = true;
    inShow = where(bx + 1.5f, by + 7.5f, mShow);
    tank->show = false;
  }
  int ox = -1, oy = -1;
  for (int y = 0; y < map.getHeight() && ox < 0; ++y)
    for (int x = 0; x < map.getWidth() && ox < 0; ++x)
      if (!f.insideZoo(x, y)) {
        ox = x;
        oy = y;
      }
  const bool outside = ox >= 0 && where(ox + 0.5f, oy + 0.5f, mOut);
  out << "PICKUP may go: grass " << grass << ", path " << path << ", restroom " << overLoo << " (" << mLoo
      << "), steep " << steep << ", lake " << onLake << ", tank " << inTank << ", show tank " << inShow << " (" << mShow
      << "), outside the zoo " << outside << " (" << mOut << ")\n";
  check("#227: put down on open ground and paths, on the lake and in a tank (cUnderwater)",
        grass && path && onLake && inTank && mGrass == 0);
  check("#227: not over a building (10555, 0x293b), on a steep tile (tile +0x81), in a show tank (10561) or "
        "outside the zoo (10311)",
        !overLoo && mLoo == 10555 && !steep && !inShow && mShow == 10561 && ox >= 0 && !outside && mOut == 10311);
  // Given up (a right click): back where it was picked up (endPlacing
  // 0x443df7)
  const bool cancelled = world.cancelTool() && g.carried < 0 && guest()->x == hx && guest()->y == hy;
  check("#227: a right click while held puts it back where it was (endPlacing 0x443df7)", cancelled);
  // Put down on land: no longer held, selected, its needs looked at next
  // tick (+0x33e); still leaving
  world.pickUpGuest(id);
  guest()->hunger = guest()->thirst = guest()->tired = guest()->bathroom = 0;
  g.putDown(id, bx + 1.5f, by + 2.5f);
  const Guests::Guest &land = *guest();
  const bool onLand = g.carried < 0 && g.selected == id && land.recheck && land.state == Guests::State::Walking &&
                      land.leaving && !land.inWater && std::abs(land.x - (bx + 1.5f)) < 0.01f;
  check("#227: put down on land: selected, its needs looked at next tick (0x46b6a3's +0x33e)", onLand);
  // #234: put down hungry 3 tiles from the restroom (squared tile distance
  // 10): straight to it, as a restroom - the building's first guest kind,
  // not the need (slots 0x28c/0x290: guestOverAThreshold 0x58d5eb,
  // guestNearNeedBuilding 0x46b7d6, 0x618f29); 117 away (over 10 x 10):
  // its needs looked at next tick
  {
    const int looAt = static_cast<int>(objectsBefore);
    // (a path from the top one down to the restroom's door, 4,2)
    map.setPath(bx + 4, by + 1, "path");
    map.setPath(bx + 4, by + 2, "path");
    world.pickUpGuest(id);
    guest()->leaving = false;
    guest()->hunger = static_cast<float>(g.types()[static_cast<size_t>(guest()->type)].hungerThreshold);
    guest()->recheck = false; // (the last put-down's +0x33e, taken by a tick)
    g.putDown(id, bx + 1.5f, by + 2.5f);
    const Guests::Guest &near = *guest();
    const bool straight = !near.recheck && near.building == looAt && near.need == "bathroom" && !near.path.empty();
    out << "PICKUP in need by the restroom: building " << near.building << " (restroom " << looAt << "), goal "
        << near.need << ", needs next tick " << near.recheck << ", hunger " << near.hunger << "; doors";
    if (looAt < static_cast<int>(objects.objects().size()))
      for (const auto &[dx, dy] : g.doorTiles(objects.objects()[static_cast<size_t>(looAt)]))
        out << " " << dx - bx << "," << dy - by << (g.walkable(dx, dy) ? " walkable" : " not");
    out << "\n";
    world.pickUpGuest(id);
    g.putDown(id, bx + 13.5f, by + 9.5f);
    const Guests::Guest &far = *guest();
    const bool farOff = far.recheck && far.building < 0;
    check("#234: put down in need with a building within 10 tiles, straight to it as its first guest kind "
          "(0x46b7d6, 0x618f29); further off, its needs next tick",
          straight && farOff);
    guest()->hunger = 0;
    guest()->leaving = true;
    guest()->building = -1;
    guest()->need.clear();
    guest()->path.clear();
    guest()->pathAt = 0;
    map.setPath(bx + 4, by + 1, "");
    map.setPath(bx + 4, by + 2, "");
  }
  // On the lake: in the water, no longer leaving, making for land (task
  // 0x83), "Hey! I don't want to be in the water!"
  world.pickUpGuest(id);
  g.putDown(id, bx + 8.5f, by + 3.5f);
  const Guests::Guest &wet = *guest();
  const bool swims = wet.state == Guests::State::Water && wet.waterTask == Guests::kInWaterTask && wet.inWater &&
                     !wet.leaving && wet.lastThought == 10568;
  check("#227: put down on the lake: no longer leaving, making for land (task 0x83), 10568 (0x46b6a3)", swims);
  // In a tank: in the water (0x274(1)); its next tick, "Hey! I don't want
  // to be in the water!" and out (chooseTask 0x59cbb8: Task7f)
  guest()->leaving = true;
  world.pickUpGuest(id);
  g.putDown(id, bx + 1.5f, by + 7.5f);
  const bool wetFlag = guest()->inWater;
  g.tick(*guest());
  const Guests::Guest &dip = *guest();
  const bool outOfTank = wetFlag && dip.lastThought == 10568 && dip.state == Guests::State::Water &&
                         (dip.waterTask == Guests::kOutOfWaterTask || dip.waterTask == Guests::kInWaterTask) && dip.leaving;
  out << "PICKUP in a tank: in the water " << wetFlag << ", thought " << dip.lastThought << ", task 0x" << std::hex
      << dip.waterTask << std::dec << "\n";
  check("#227: put down in a tank: in the water, then 10568 and out (0x4d923c, 0x59cbb8)", outOfTank);
  // Staff Information's pick-up turns Assign off first (zoo.exe
  // infostf_PickupStaff_click 0x618553: uiTurnOff 3578)
  {
    auto &st = world.getStaff();
    const int keeper = st.hire(st.typeOfFile("staff/keeper.ai"), bx + 6.5f, by + 2.5f, map, f);
    bool assignOff = false;
    if (keeper >= 0) {
      world.assignStaff(keeper);
      const bool was = world.isAssigning();
      world.pickUpStaff(keeper);
      assignOff = was && !world.isAssigning() && st.carried == keeper;
      st.carried = -1;
    }
    check("#227: picking up a staff member turns Assign off (0x618553)", assignOff);
    st.clear();
  }
  // (the fixture taken up)
  g.clear();
  while (objects.objects().size() > objectsBefore)
    objects.remove(static_cast<int>(objects.objects().size()) - 1);
  for (int x = bx; x <= bx + 12; ++x)
    map.setPath(x, by, "");
  for (const auto &[at, type] : pathsWere)
    map.setPath(at.first, at.second, type);
  if (lake)
    lake->terrainType = lakeWas;
  map.raiseVertex(bx + 11, by + 3, -2);
  // (the tank taken down, its ground put back, so the checks after it find
  // the flat zoo they expect)
  {
    const bool groundWas = Features::tankGroundBack;
    Features::tankGroundBack = true;
    f.remove({true, bx, by + 6}, map);
    Features::tankGroundBack = groundWas;
    f.updateExhibits(map, 1, 0, 1);
    f.takeLostTanks();
  }
  world.reindex();
  check("#227: the fixture taken down (no exhibit left)", f.exhibits().empty());
  return out.str();
}
