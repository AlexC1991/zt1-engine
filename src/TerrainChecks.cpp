// Terrain edits (#294, #292): the tiles' side codes after Terrain Height
// (zoo.exe 0x45d935 -> 0x45e1c2), and where Paint Terrain may make or drain
// a lake (terrainCanPaint 0x4f8f0b). Run from runParityChecks (the flat
// Basic Grass test zoo); everything it changes is put back
#include "World.hpp"
#include "IniReader.hpp"
#include "OriginalConstants.hpp"
#include "ResourceManager.hpp"
#include <sstream>
#include <string>
#include <vector>

std::string runTerrainChecks(World &world, ResourceManager *rm, int &passed, int &total) {
  auto &map = world.getMap();
  auto &f = world.getFences();
  auto &g = world.getGuests();
  auto &objects = world.objectsMutable();
  TerrainTool &tool = world.getTerrainTool();
  std::ostringstream out;
  auto check = [&](const std::string &name, bool ok) {
    ++total;
    passed += ok;
    out << (ok ? "PASS " : "FAIL ") << name << "\n";
  };
  // The map's world clock kept on a new game (measured: Basic Grass,
  // maps/default.zoo, holds 100.444 s; the original's world +0x204 read 110
  // a few seconds after Play)
  out << "     world clock " << world.getAmbient().worldClock() << " s\n";
  check("a new game keeps its map's world clock (Basic Grass: 100.444 s on)",
        world.getAmbient().worldClock() >= 100.444 - 0.001);
  // Room: 9 x 9 flat open tiles in the zoo
  int bx = -1, by = -1;
  for (int y = 3; y < map.getHeight() - 12 && bx < 0; ++y)
    for (int x = 3; x < map.getWidth() - 12 && bx < 0; ++x) {
      bool ok = true;
      for (int yy = y; yy < y + 9 && ok; ++yy)
        for (int xx = x; xx < x + 9 && ok; ++xx) {
          const MapTile *t = map.getTile(xx, yy);
          ok = t && f.exhibitAt(xx, yy) < 0 && f.insideZoo(xx, yy) && !world.getIndex().blocked(xx, yy) &&
               !map.isPath(xx, yy) && t->cornerHeight[0] == t->cornerHeight[1] &&
               t->cornerHeight[1] == t->cornerHeight[2] && t->cornerHeight[2] == t->cornerHeight[3] &&
               t->cornerHeight[0] == map.getTile(x, y)->cornerHeight[0] && t->terrainType == 0;
        }
      if (ok) {
        bx = x;
        by = y;
      }
    }
  out << "TERRAIN room at " << bx << "," << by << "\n";
  check("#294/#292: room for the terrain fixture", bx >= 0);
  if (bx < 0)
    return out.str();
  // (the room's ground as it was, put back at the end)
  std::vector<MapTile> was;
  for (int yy = by - 1; yy <= by + 9; ++yy)
    for (int xx = bx - 1; xx <= bx + 9; ++xx)
      was.push_back(*map.getTile(xx, yy));
  auto putBack = [&]() {
    size_t i = 0;
    for (int yy = by - 1; yy <= by + 9; ++yy)
      for (int xx = bx - 1; xx <= bx + 9; ++xx)
        *map.getTileMutable(xx, yy) = was[i++];
    map.touch();
  };
  auto code = [&](int x, int y, int side) { return (map.getTile(x, y)->edgeBits >> (6 - side)) & 3; };
  const int cx = bx + 4, cy = by + 4;
  for (int yy = by; yy < by + 9; ++yy)
    for (int xx = bx; xx < bx + 9; ++xx)
      map.getTileMutable(xx, yy)->edgeBits = 0;

  // --- #294: Terrain Height keeps the side codes (0x45d935 -> 0x45e1c2) ---
  // Cliffs and moats, a one-tile brush raised a unit: the tile stands a unit
  // above all four neighbours - its sides 3, theirs facing it 2
  world.setTerrainTool(true, false, 0, 1, static_cast<int>(TerrainTool::Mode::Cliffs));
  tool.press(cx, cy, 100, 100);
  tool.drag(cx, cy, 100, 84, 16);
  tool.release();
  const MapTile *raised = map.getTile(cx, cy);
  const bool up = raised->cornerHeight[0] == was[static_cast<size_t>((cy - by + 1) * 11 + (cx - bx + 1))].cornerHeight[0] + 1;
  out << "     cliff: sides " << code(cx, cy, 0) << code(cx, cy, 2) << code(cx, cy, 4) << code(cx, cy, 6)
      << ", neighbours " << code(cx, cy - 1, 4) << code(cx + 1, cy, 6) << code(cx, cy + 1, 0) << code(cx - 1, cy, 2) << "\n";
  check("#294: a cliff raised: the tile's four sides above (3)", up && code(cx, cy, 0) == Original::sideCodeCliffAbove &&
        code(cx, cy, 2) == Original::sideCodeCliffAbove && code(cx, cy, 4) == Original::sideCodeCliffAbove &&
        code(cx, cy, 6) == Original::sideCodeCliffAbove);
  check("#294: ... and its neighbours' sides facing it below (2)",
        code(cx, cy - 1, 4) == Original::sideCodeCliffBelow && code(cx + 1, cy, 6) == Original::sideCodeCliffBelow &&
            code(cx, cy + 1, 0) == Original::sideCodeCliffBelow && code(cx - 1, cy, 2) == Original::sideCodeCliffBelow);
  check("#294: ... the neighbours' other sides flat (0)", code(cx + 1, cy, 2) == 0 && code(cx + 1, cy, 0) == 0 &&
                                                         code(cx, cy - 1, 0) == 0);
  // Undo: the ground back, and its codes with it
  tool.undo();
  check("#294: undone: the codes flat again", code(cx, cy, 0) == 0 && code(cx, cy, 2) == 0 && code(cx - 1, cy, 2) == 0 &&
                                              code(cx, cy + 1, 0) == 0);
  // Hills and valleys: the ground round the raised tile slopes to it - the
  // raised tile joins its neighbours flat (their near corners rose with
  // it), and the slopes beside each other are sloped alike (1)
  world.setTerrainTool(true, false, 0, 1, static_cast<int>(TerrainTool::Mode::Hills));
  tool.press(cx, cy, 100, 100);
  tool.drag(cx, cy, 100, 84, 16);
  tool.release();
  out << "     hill: centre " << code(cx, cy, 0) << code(cx, cy, 2) << code(cx, cy, 4) << code(cx, cy, 6) << ", east "
      << code(cx + 1, cy, 0) << code(cx + 1, cy, 2) << code(cx + 1, cy, 4) << code(cx + 1, cy, 6) << "\n";
  check("#294: a hill: the raised tile's sides flat (0), the slopes round it joined", code(cx, cy, 2) == 0 &&
        code(cx + 1, cy, 6) == 0 && code(cx + 1, cy, 4) == Original::sideCodeSloped &&
        code(cx + 1, cy + 1, 0) == Original::sideCodeSloped && code(cx + 1, cy, 2) == 0);
  tool.undo();
  world.setTerrainTool(false, true, 0, 2, 0);
  // A tile's codes by hand: a neighbour a unit higher on one edge only (a
  // slope against a flat tile): the rises differ, the first corners equal -
  // 0x45e1c2 takes this tile as the higher (3), the other 2
  {
    MapTile *n = map.getTileMutable(cx + 1, cy);
    n->cornerHeight[CORNER_X0Y1] += 1; // (its -x edge rises along y)
    map.updateSideCodes(cx, cy);
    check("#294: rises unlike from the same corner: this side 3, the neighbour's 2",
          code(cx, cy, 2) == Original::sideCodeCliffAbove && code(cx + 1, cy, 6) == Original::sideCodeCliffBelow);
    n->cornerHeight[CORNER_X0Y1] -= 1;
    map.updateSideCodes(cx, cy);
  }

  // --- #292: where Paint Terrain may make or drain a lake (0x4f8f0b) ---
  const int lakeType = 9; // (FreshWater: tiletex's water 1)
  const int grass = 0;
  auto ai = [&](const std::string &file, const char *key) {
    int v = 0;
    if (IniReader *r = rm->getIniReader(file)) {
      v = r->getInt("characteristics/integers", key, 0);
      delete r;
    }
    return v == 1;
  };
  // An object of each kind, by the game's own .ai: one that can't swim, one
  // that can (not only), one that only swims
  std::string dry = "scenery/other/bench.ai", swims, only;
  for (const char *c : {"scenery/foliage/cypress.ai", "scenery/other/dock1.ai", "scenery/other/aqrock1.ai"})
    if (swims.empty() && ai(c, "cswims") && !ai(c, "conlyswims"))
      swims = c;
  for (const char *c : {"scenery/other/buoy.ai", "scenery/foliage/wtrlily.ai", "scenery/foliage/wtrreed.ai",
                        "scenery/other/jetty1.ai", "scenery/other/sailboat.ai", "scenery/other/mtrboat.ai"})
    if (only.empty() && ai(c, "conlyswims"))
      only = c;
  out << "     objects: dry " << dry << ", swims " << swims << ", only swims " << only << "\n";
  check("#292: the game has a bench, a swimming and an only-swimming object", !ai(dry, "cswims") && !swims.empty() &&
                                                                             !only.empty());
  auto terrainAt = [&](int x, int y) { return static_cast<int>(map.getTile(x, y)->terrainType); };
  auto setTerrain = [&](int x, int y, int type) { map.getTileMutable(x, y)->terrainType = static_cast<uint8_t>(type); };
  const int lx = bx + 1, ly = by + 1;
  check("#292: a lake onto open flat grass", tool.paintTile(lx, ly, lakeType) && tool.isWater(lx, ly));
  check("#292: ... and drained back", tool.paintTile(lx, ly, grass) && terrainAt(lx, ly) == grass);
  // A bench (no cSwims) on the tile: no lake under it
  auto withObject = [&](const std::string &file, int x, int y, auto &&body) {
    const size_t before = objects.objects().size();
    const bool added = objects.add(file, x + 0.5f, y + 0.5f, 0);
    world.reindex();
    body(added);
    if (objects.objects().size() > before)
      objects.remove(static_cast<int>(objects.objects().size()) - 1);
    world.reindex();
  };
  withObject(dry, lx, ly, [&](bool added) {
    check("#292: no lake under a bench (cSwims 0)", added && !tool.paintTile(lx, ly, lakeType) && terrainAt(lx, ly) == grass);
  });
  withObject(swims, lx, ly, [&](bool added) {
    check("#292: a lake under " + swims + " (cSwims 1)", added && tool.paintTile(lx, ly, lakeType));
    check("#292: ... and drained from under it (not cOnlySwims)", tool.paintTile(lx, ly, grass));
  });
  setTerrain(lx, ly, lakeType);
  withObject(only, lx, ly, [&](bool added) {
    check("#292: no draining under " + only + " (cOnlySwims 1)", added && !tool.paintTile(lx, ly, grass) &&
                                                                     tool.isWater(lx, ly));
  });
  setTerrain(lx, ly, grass);
  // A guest standing there: neither way
  g.clear();
  g.setEntrance(bx, by + 8);
  g.admit(0);
  if (!g.guests().empty()) {
    Guests::Guest &guest = g.mutableGuests().front();
    guest.x = lx + 0.5f;
    guest.y = ly + 0.5f;
    check("#292: no lake under a guest", !tool.paintTile(lx, ly, lakeType) && terrainAt(lx, ly) == grass);
    setTerrain(lx, ly, lakeType);
    check("#292: no draining under a guest", !tool.paintTile(lx, ly, grass) && tool.isWater(lx, ly));
    guest.x = bx + 7.5f;
    guest.y = by + 7.5f;
    check("#292: ... and with the guest gone, drained", tool.paintTile(lx, ly, grass));
  } else
    check("#292: a guest admitted for the fixture", false);
  g.clear();
  // A slope: no lake on it
  map.getTileMutable(lx, ly)->cornerHeight[CORNER_X1Y1] += 1;
  check("#292: no lake on a slope", !tool.paintTile(lx, ly, lakeType));
  putBack();
  world.reindex();
  return out.str();
}
