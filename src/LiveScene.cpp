// ours: a check. ZT_LIVE_SCENE=<script>: a test zoo built from a short
// script, run, pictured and saved shared - the same scene then played in the
// original in the Lab (Play original), to compare the two side by side.
// One command a line ('#' starts a comment):
//   guests <n>                      n guests let in at the gate
//   run <seconds>                   the zoo run, unpaused
//   frames <name> <count> <secs>    run, a picture every secs (name-NNN)
//   capture <name> / centre <x> <y>
//   obj <file> <x> <y> <facing>     an object put down (no placing rules)
//   objnear <file> path|water <x> <y>   the first spot round x, y that the
//                                   placing rules allow, its door on a path
//                                   (and by water for "water")
//   animal <ai file> <x> <y> [f]    an animal put down (female with f)
//   staff <ai file> <x> <y>         a member of staff hired there
//   exhibit <fence key> <x> <y> <w> <h> <name>   a fence (or tank wall) loop
//   path <type> <x0> <y0> <x1> <y1> a rectangle of path
//   terrain <type> <x0> <y0> <x1> <y1>  ground painted (9 fresh water)
//   fire_staff                      every member of staff let go
//   date <month 1-12> <day>         the calendar run on to that day
//   log                             where every guest, animal and staff is
//   save <file.zoo>                 saved shared, for the original
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Animals.hpp"
#include "Fences.hpp"
#include "Guests.hpp"
#include "LabTrace.hpp"
#include "PlacedObjects.hpp"
#include "Staff.hpp"
#include "World.hpp"
#include "WorldMap.hpp"
#include "ZooSim.hpp"

int runLiveScene(World &world, ZooSim &sim, const std::string &script,
                 const std::function<void(const char *)> &capture,
                 const std::function<bool(const std::string &)> &save, const std::function<void(float)> &tick) {
  std::map<std::string, int> *tally = nullptr;
  std::ifstream in(script);
  if (!in) {
    SDL_Log("[LiveScene] no script %s", script.c_str());
    return 1;
  }
  static bool noKeys[SDL_SCANCODE_COUNT] = {};
  WorldMap &map = world.getMap();
  Fences &fences = world.getFences();
  Guests &guests = world.getGuests();
  Animals &animals = world.getAnimals();
  // (each step as the game's own frame does it: the world, the books and
  // calendar, and the money and news passed on - main's tick)
  auto step = [&](float seconds) {
    for (float t = 0; t < seconds; t += 0.1f)
      tick(0.1f);
  };
  step(0.5f); // (the entrance is found on the zoo's first update)
  int failures = 0, drag = 1000, lineNo = 0;
  std::string line;
  while (std::getline(in, line)) {
    lineNo++;
    if (const size_t hash = line.find('#'); hash != std::string::npos)
      line.resize(hash);
    std::istringstream words(line);
    std::string cmd;
    if (!(words >> cmd))
      continue;
    bool ok = true;
    if (cmd == "guests") {
      int n = 0;
      words >> n;
      for (int k = 0; k < n; k++)
        guests.admit(0);
    } else if (cmd == "run") {
      float s = 0;
      words >> s;
      step(s);
    } else if (cmd == "frames") {
      std::string name;
      int count = 0;
      float every = 1;
      words >> name >> count >> every;
      for (int k = 0; k < count; k++) {
        char n[160];
        std::snprintf(n, sizeof n, "%s-%03d", name.c_str(), k);
        capture(n);
        step(every);
      }
    } else if (cmd == "capture") {
      std::string name;
      words >> name;
      capture(name.c_str());
    } else if (cmd == "centre") {
      float x = 0, y = 0;
      words >> x >> y;
      world.centreOn(x, y);
    } else if (cmd == "obj") {
      std::string file;
      float x = 0, y = 0;
      int facing = 0;
      words >> file >> x >> y >> facing;
      ok = world.objectsMutable().add(file, x, y, facing);
      world.reindex();
      if (ok)
        for (auto [dx, dy] : guests.doorTiles(world.getObjects().objects().back())) {
          const MapTile *t = map.getTile(dx, dy);
          SDL_Log("[LiveScene] %s door at %d,%d: %s", file.c_str(), dx, dy,
                  map.isPath(dx, dy) ? "path" : t && (t->terrainType == 9 || t->terrainType == 10) ? "water" : "ground");
        }
    } else if (cmd == "objnear") {
      std::string file, near;
      int cx = 0, cy = 0;
      words >> file >> near >> cx >> cy;
      auto waterBy = [&](int x, int y) {
        for (int dy = -2; dy <= 2; dy++)
          for (int dx = -2; dx <= 2; dx++)
            if (const MapTile *t = map.getTile(x + dx, y + dy); t && (t->terrainType == 9 || t->terrainType == 10))
              return true;
        return false;
      };
      ok = false;
      for (int r = 0; r < 30 && !ok; r++)
        for (int dy = -r; dy <= r && !ok; dy++)
          for (int dx = -r; dx <= r && !ok; dx++) {
            if (std::max(std::abs(dx), std::abs(dy)) != r)
              continue;
            const int tx = cx + dx, ty = cy + dy;
            if ((near == "water" && !waterBy(tx, ty)) || fences.exhibitAt(tx, ty) >= 0)
              continue;
            for (float off : {0.5f, 1.0f})
              for (int facing : {0, 2, 4, 6}) {
                if (ok || world.objectFit(file, tx + off, ty + off, facing) != Fences::Fit::Ok ||
                    !world.objectsMutable().add(file, tx + off, ty + off, facing))
                  continue;
                const PlacedObjects::Object &o = world.getObjects().objects().back();
                auto doors = guests.doorTiles(o);
                bool onPath = doors.empty();
                for (auto [px, py] : doors)
                  onPath = onPath || map.isPath(px, py);
                if (onPath) {
                  ok = true;
                  SDL_Log("[LiveScene] %s at %.1f,%.1f facing %d", file.c_str(), tx + off, ty + off, facing);
                } else {
                  world.objectsMutable().remove(static_cast<int>(world.getObjects().objects().size()) - 1);
                }
              }
          }
      world.reindex();
    } else if (cmd == "animal") {
      std::string file, sex;
      float x = 0, y = 0;
      words >> file >> x >> y >> sex;
      const int type = animals.typeOfFile(file);
      ok = type >= 0 && animals.adopt(type, sex == "f", x, y, map, fences, true) >= 0;
    } else if (cmd == "staff") {
      std::string file;
      float x = 0, y = 0;
      words >> file >> x >> y;
      const int type = world.getStaff().typeOfFile(file);
      ok = type >= 0 && world.getStaff().hire(type, x, y, map, fences) >= 0;
    } else if (cmd == "exhibit") {
      std::string key, name;
      int x = 0, y = 0, w = 0, h = 0;
      words >> key >> x >> y >> w >> h;
      std::getline(words, name);
      name.erase(0, name.find_first_not_of(' '));
      const int type = fences.typeIndex(key);
      ok = type >= 0;
      if (ok) {
        for (int k = 0; k < w; k++) {
          fences.place({true, x + k, y}, type, drag, map);
          fences.place({true, x + k, y + h}, type, drag + 1, map);
        }
        for (int k = 0; k < h; k++) {
          fences.place({false, x, y + k}, type, drag + 2, map);
          fences.place({false, x + w, y + k}, type, drag + 3, map);
        }
        drag += 4;
        for (int id : fences.updateExhibits(map, 1, 0, 1))
          fences.finishExhibit(id, name.empty() ? "Exhibit" : name, map);
        world.reindex();
        ok = fences.exhibitAt(x, y) >= 0;
        // (a tank filled with salt water, as Fill Tank does)
        if (ok)
          if (const Fences::Exhibit *e = fences.exhibit(fences.exhibitAt(x, y)); e && e->tank)
            SDL_Log("[LiveScene] tank %d floor %d: filled %d", e->id, e->floorHeight,
                    static_cast<int>(fences.fillExhibit(e->id, true)));
      }
    } else if (cmd == "path") {
      std::string type;
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      words >> type >> x0 >> y0 >> x1 >> y1;
      for (int y = std::min(y0, y1); y <= std::max(y0, y1); y++)
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); x++)
          map.setPath(x, y, type);
      map.touch();
      world.reindex();
    } else if (cmd == "terrain") {
      int type = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      words >> type >> x0 >> y0 >> x1 >> y1;
      for (int y = std::min(y0, y1); y <= std::max(y0, y1); y++)
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); x++)
          if (MapTile *t = map.getTileMutable(x, y))
            t->terrainType = static_cast<uint8_t>(type);
      map.touch();
      world.reindex();
    } else if (cmd == "height") {
      int h = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      words >> h >> x0 >> y0 >> x1 >> y1;
      for (int y = std::min(y0, y1); y <= std::max(y0, y1); y++)
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); x++)
          if (MapTile *t = map.getTileMutable(x, y)) {
            t->height = h;
            for (int c = 0; c < 4; c++)
              t->cornerHeight[c] = h;
          }
      // (each edge's side code made again, as the terrain tools do: the
      // original goes by them)
      for (int y = std::min(y0, y1) - 1; y <= std::max(y0, y1) + 1; y++)
        for (int x = std::min(x0, x1) - 1; x <= std::max(x0, x1) + 1; x++)
          map.updateSideCodes(x, y);
      map.touch();
      world.reindex();
    } else if (cmd == "fire_staff") {
      std::vector<int> ids;
      for (const Staff::Member &m : world.getStaff().members())
        ids.push_back(m.id);
      for (int id : ids)
        world.getStaff().fire(id);
    } else if (cmd == "date") {
      int month = 1, day = 1;
      words >> month >> day;
      for (int k = 0; k < 400 && !(sim.month() + 1 == month && sim.dayOfMonth() == day); k++)
        sim.update(ZooSim::kSecondsPerDay);
      ok = sim.month() + 1 == month && sim.dayOfMonth() == day;
    } else if (cmd == "trace") {
      // (trace: every traced step from now counted by its zoo.exe address
      // and what it is; tally: the counts, most first)
      static std::map<std::string, int> *steps = new std::map<std::string, int>();
      steps->clear();
      LabTrace::on = true;
      LabTrace::sink = [](const LabTrace::Step &st) {
        if (!st.enter && st.what)
          (*steps)[std::string(st.zoo ? st.zoo : "") + " " + st.what]++;
      };
      tally = steps;
    } else if (cmd == "tally") {
      std::vector<std::pair<int, std::string>> rows;
      if (tally)
        for (const auto &[k, n] : *tally)
          rows.push_back({n, k});
      std::sort(rows.rbegin(), rows.rend());
      for (const auto &[n, k] : rows)
        SDL_Log("[LiveScene] step %6d  %s", n, k.c_str());
    } else if (cmd == "log") {
      for (const Guests::Guest &g : guests.guests())
        if (g.x > -999)
          SDL_Log("[LiveScene] guest %u at %.2f,%.2f state %d anim %s leaving %d happy %d need '%s' route %zu/%zu to %d,%d",
                  g.id, g.x, g.y, static_cast<int>(g.state), g.anim.c_str(), static_cast<int>(g.leaving),
                  static_cast<int>(g.happiness), g.need.c_str(), g.pathAt, g.path.size(),
                  g.path.empty() ? -1 : g.path.back().first, g.path.empty() ? -1 : g.path.back().second);
      double gSum = 0, aSum = 0;
      int gN = 0;
      for (const Guests::Guest &g : guests.guests())
        if (g.x > -999)
          gSum += g.happiness, gN++;
      for (const Animals::Member &m : animals.members()) {
        SDL_Log("[LiveScene] animal %d %s at %.2f,%.2f anim %s happy %d hunger %d health %d", m.id, m.name.c_str(), m.x,
                m.y, m.anim.c_str(), static_cast<int>(std::lround(m.happiness)), static_cast<int>(m.hunger),
                static_cast<int>(m.health));
        aSum += m.happiness;
      }
      for (const Fences::Exhibit &e : fences.exhibits())
        SDL_Log("[LiveScene] exhibit %d '%s' happiness %d suitability %d", e.id, e.name.c_str(), animals.exhibitHappiness(e.id),
                animals.exhibitSuitability(e.id));
      SDL_Log("[LiveScene] date %d/%d, guests %d happy %.1f on average, animals %zu happy %.1f on average", sim.month() + 1,
              sim.dayOfMonth(), gN, gN ? gSum / gN : 0.0, animals.members().size(),
              animals.members().empty() ? 0.0 : aSum / animals.members().size());
      for (const Staff::Member &m : world.getStaff().members())
        SDL_Log("[LiveScene] staff %d at %.2f,%.2f", m.id, m.x, m.y);
    } else if (cmd == "info") {
      int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, paths = 0;
      for (int y = 0; y < map.getHeight(); y++)
        for (int x = 0; x < map.getWidth(); x++) {
          if (fences.insideZoo(x, y))
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
          paths += map.isPath(x, y);
        }
      int ex = -1, ey = -1;
      guests.entrance(ex, ey);
      SDL_Log("[LiveScene] map %d x %d, zoo %d,%d .. %d,%d, entrance %d,%d, %d path tiles, %zu exhibits", map.getWidth(),
              map.getHeight(), x0, y0, x1, y1, ex, ey, paths, fences.exhibits().size());
      for (const Fences::Exhibit &e : fences.exhibits()) {
        SDL_Log("[LiveScene] exhibit %d '%s' tank %d tiles %zu floor %d water %.2f", e.id, e.name.c_str(),
                static_cast<int>(e.tank), e.tiles.size(), e.floorHeight, e.water);
        std::string walls;
        for (auto [x, y] : e.tiles)
          for (auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            if (e.tiles.count({x + dx, y + dy}))
              continue;
            const Fences::Edge edge = dx ? Fences::Edge{false, std::max(x, x + dx), y} : Fences::Edge{true, x, std::max(y, y + dy)};
            const Fences::Piece *p = fences.at(edge);
            if (!p || !fences.types()[p->type].tank)
              walls += " " + std::string(p ? fences.types()[p->type].key : "none") + "@" + std::to_string(edge.x) + "," +
                       std::to_string(edge.y) + (edge.alongX ? "x" : "y");
          }
        SDL_Log("[LiveScene]   walls:%s", walls.c_str());
      }
      for (int y = y0; y <= y1; y += 1) {
        std::string row;
        for (int x = x0; x <= x1; x++)
          row += !fences.insideZoo(x, y) ? ' ' : map.isPath(x, y) ? '#' : fences.exhibitAt(x, y) >= 0 ? 'E'
                 : map.getTile(x, y) && (map.getTile(x, y)->terrainType == 9 || map.getTile(x, y)->terrainType == 10) ? '~' : '.';
        SDL_Log("[LiveScene] row %3d %s", y, row.c_str());
      }
    } else if (cmd == "route") {
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      words >> x0 >> y0 >> x1 >> y1;
      std::vector<std::pair<int, int>> route;
      ok = guests.testRoute(x0, y0, x1, y1, route);
      SDL_Log("[LiveScene] guest route %d,%d to %d,%d: %d, %zu steps", x0, y0, x1, y1, static_cast<int>(ok), route.size());
      for (auto [tx, ty] : {std::pair{x0, y0}, std::pair{x1, y1}})
        if (const MapTile *t = map.getTile(tx, ty))
          SDL_Log("[LiveScene]   tile %d,%d height %d corners %d/%d/%d/%d path %d", tx, ty, t->height, t->cornerHeight[0],
                  t->cornerHeight[1], t->cornerHeight[2], t->cornerHeight[3], static_cast<int>(map.isPath(tx, ty)));
    } else if (cmd == "fences") {
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      words >> x0 >> y0 >> x1 >> y1;
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
          for (const Fences::Edge &e : {Fences::Edge{false, x, y}, Fences::Edge{true, x, y}})
            if (const Fences::Piece *p = fences.at(e))
              SDL_Log("[LiveScene] fence %s at %d,%d%s gate %d", fences.types()[p->type].key.c_str(), e.x, e.y,
                      e.alongX ? "x" : "y", static_cast<int>(p->gate));
    } else if (cmd == "save") {
      std::string file;
      words >> file;
      ok = save(file);
    } else {
      ok = false;
    }
    SDL_Log("[LiveScene] line %d: %s - %s", lineNo, line.c_str(), ok ? "ok" : "FAILED");
    failures += !ok;
  }
  SDL_Log("[LiveScene] done, %d failed", failures);
  return failures ? 1 : 0;
}
