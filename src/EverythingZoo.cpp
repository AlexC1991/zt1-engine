// The everything zoo (ZT_EVERYTHING_ZOO=1 with --panel-shots <out> <map>):
// a freeform zoo holding every type the game's registry lists - each
// building, scenery item and path, an exhibit of every fence, a tank of
// every tank wall, every animal (adult, young, egg) in a home of its kind,
// every kind of staff, guests, every food, dung and litter, a filter, a
// show tank with its toys and a program - and the states saves have gone
// wrong on: worn and broken fences, salt and fresh water.
//
// It is generated from the registry each run, so a type added to the game
// (an update pack, an expansion) is in the test without anyone adding it.
// The zoo is saved as a shared .zoo, and its original part checked against
// the rules zoo.exe reads it by, each taken from the original's own data:
//   - a scenery type's subclass is the registry section that lists it, and
//     a [building] type is building/building (zoo.exe finds no other: a
//     building is refused as a corrupted save, scenery silently dropped);
//   - a fence or tank wall goes by its registry key (tankwal1, not tankwall);
//   - no fence is worn or broken without the art for it (zoo.exe crashes
//     drawing it, 0x416ee8);
//   - every stock thing placed is in the original part.
// <out>/everything-<map>.zoo is the save, everything-<map>.json what its
// original part holds by class/subclass/type (for the Lab to compare with
// what the original actually made of it), everything-<map>.txt the report.
#include "World.hpp"
#include "Shows.hpp"
#include "SaveGame.hpp"
#include "ZooSim.hpp"
#include "ZooReader.hpp"
#include "ItemCatalog.hpp"
#include "MemoryManager.hpp"
#include "ResourceManager.hpp"
#include "IniReader.hpp"
#include "ui/UiGameScreen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace {

std::string stemOf(const std::string &file) {
  size_t s = file.find_last_of('/'), d = file.find_last_of('.');
  s = s == std::string::npos ? 0 : s + 1;
  return file.substr(s, d == std::string::npos || d < s ? std::string::npos : d - s);
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  return s;
}

std::string jsonText(const std::string &s) {
  std::string o;
  for (char c : s)
    if (c == '"' || c == '\\') o += '\\', o += c;
    else if (static_cast<unsigned char>(c) >= 32) o += c;
  return o;
}

// Free tiles inside the zoo wall, taken a rectangle at a time
struct Lot {
  int w = 0, h = 0;
  std::vector<char> used;
  int cursorY = 0;
  bool take(int rw, int rh, int &ox, int &oy, int margin = 1) {
    for (int y = std::max(cursorY, margin); y + rh + margin <= h; ++y)
      for (int x = margin; x + rw + margin <= w; ++x) {
        bool free = true;
        for (int j = -margin; j < rh + margin && free; ++j)
          for (int i = -margin; i < rw + margin && free; ++i)
            if (used[(y + j) * w + x + i]) free = false;
        if (!free) continue;
        for (int j = 0; j < rh; ++j)
          for (int i = 0; i < rw; ++i) used[(y + j) * w + x + i] = 1;
        ox = x;
        oy = y;
        return true;
      }
    return false;
  }
};

// Tiles inside one exhibit or tank, packed the same way
struct Pen {
  int x = 0, y = 0, w = 0, h = 0, id = -1;
  std::vector<char> used;
  bool take(int rw, int rh, int &ox, int &oy) {
    if (used.empty()) used.assign(w * h, 0);
    for (int j = 0; j + rh <= h; ++j)
      for (int i = 0; i + rw <= w; ++i) {
        bool free = true;
        for (int b = 0; b < rh && free; ++b)
          for (int a = 0; a < rw && free; ++a)
            if (used[(j + b) * w + i + a]) free = false;
        if (!free) continue;
        for (int b = 0; b < rh; ++b)
          for (int a = 0; a < rw; ++a) used[(j + b) * w + i + a] = 1;
        ox = x + i;
        oy = y + j;
        return true;
      }
    return false;
  }
};

} // namespace

int runEverythingZoo(World &world, UiGameScreen &hud, ZooSim &sim, ResourceManager *rm,
                     const std::string &out, const std::string &mapPath) {
  // (ZT_EVERYTHING_PART=units: the animals, staff and guests; else the
  // items - each part a zoo of its own, so each has room to be laid out as
  // a player would: units spaced by their footprints, people on paths)
  const char *partEnv = std::getenv("ZT_EVERYTHING_PART");
  const bool units = partEnv && std::string(partEnv) == "units";
  const std::string stem = stemOf(mapPath) + (units ? "-units" : "-items");
  std::ofstream report(out + "/everything-" + stem + ".txt");
  int failures = 0;
  auto note = [&](const std::string &s) { report << s << "\n"; report.flush(); SDL_Log("[Everything] %s", s.c_str()); };
  auto fail = [&](const std::string &s) { ++failures; note("FAIL " + s); };

  auto &f = world.getFences();
  auto &map = world.getMap();
  auto &animals = world.getAnimals();
  auto &staff = world.getStaff();
  auto &shows = world.getShows();
  auto &guests = world.getGuests();
  auto &items = world.getItems();
  auto &objects = world.objectsMutable();
  const auto &catalog = ItemCatalog::get();
  // What the map itself holds (its own original archive): a record the
  // shipped map has is the original's own, whatever the original makes of it
  std::set<std::string> fromMap;
  // (the map's own records of a type the original has none of - it skips
  // them in its own map too: listed for the comparison, not failed)
  std::set<std::string> originalSkips;
  for (const auto &o : world.mapReader().getObjects())
    fromMap.insert(o.className + "/" + lower(o.subClass) + "/" + lower(o.typeName));
  shows.clear();
  animals.clear();
  staff.clear();
  const_cast<std::vector<Guests::Guest> &>(guests.guests()).clear();

  // The land: inside the zoo wall, clear of what the map put there (the
  // entrance and its path) by a few tiles
  Lot lot;
  lot.w = map.getWidth();
  lot.h = map.getHeight();
  lot.used.assign(lot.w * lot.h, 1);
  for (int y = 0; y < lot.h; ++y)
    for (int x = 0; x < lot.w; ++x)
      if (f.insideZoo(x, y) && map.getPathType(x, y) < 0) lot.used[y * lot.w + x] = 0;
  for (const auto &o : objects.objects())
    for (int j = -3; j <= 3; ++j)
      for (int i = -3; i <= 3; ++i) {
        int x = static_cast<int>(o.x) + i, y = static_cast<int>(o.y) + j;
        if (x >= 0 && y >= 0 && x < lot.w && y < lot.h) lot.used[y * lot.w + x] = 1;
      }

  // Where each placed stock thing should turn up in the original part
  std::map<std::string, int> placed;  // "class/sub/type" as the original names it
  std::vector<std::string> didNotFit;
  int drag = 10;
  auto enclose = [&](int x, int y, int w, int h, int type, const std::string &name) {
    const int base = drag;
    drag += 4;
    for (int i = x; i < x + w; ++i) {
      f.place({true, i, y}, type, base, map);
      f.place({true, i, y + h}, type, base + 1, map);
    }
    for (int j = y; j < y + h; ++j) {
      f.place({false, x, j}, type, base + 2, map);
      f.place({false, x + w, j}, type, base + 3, map);
    }
    for (int id : f.updateExhibits(map, 1, 0, 1)) f.finishExhibit(id, name, map);
    return f.exhibitAt(x, y);
  };
  auto pen = [&](int w, int h, int type, const std::string &name, Pen &p) {
    int x = 0, y = 0;
    if (!lot.take(w + 1, h + 1, x, y)) return false;
    p = Pen{};
    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.id = enclose(x, y, w, h, type, name);
    return p.id >= 0;
  };
  auto tank = [&](int w, int h, int type, bool salt, const std::string &name, Pen &p) {
    if (!pen(w, h, type, name, p)) return false;
    while (f.canAdjustWall(p.id, 1)) f.adjustWall(p.id, 1);
    f.fillExhibit(p.id, salt);
    return true;
  };
  auto typeOfKey = [&](const std::string &key) { return f.typeIndex(key); };

  // An item's footprint (half tiles) and where it lives, from its own file
  struct Info { int w = 1, h = 1; bool water = false, show = false; };
  auto infoOf = [&](const std::string &file) {
    Info info;
    if (IniReader *ai = rm->getIniReader(file)) {
      const std::string ints = "characteristics/integers";
      // (the room it takes: its placement footprint where that's larger -
      // the stage's 18 half tiles to its 10 - as a player can't put one
      // closer to anything)
      const int fx = std::max(ai->getInt(ints, "cfootprintx", 2), ai->getInt(ints, "cplacementfootprintx", 0));
      const int fy = std::max(ai->getInt(ints, "cfootprinty", 2), ai->getInt(ints, "cplacementfootprinty", 0));
      info.w = std::max(1, (fx + 1) / 2);
      info.h = std::max(1, (fy + 1) / 2);
      info.water = ai->getInt(ints, "cunderwater", 0) != 0 || ai->getInt(ints, "csurface", 0) != 0;
      info.show = ai->getInt(ints, "cshow", 0) != 0;
      delete ai;
    }
    return info;
  };

  // 1. A show (the items part): a home tank and a show tank sharing a wall (its portal),
  // two orcas, a marine specialist, the show's toys and a program
  Pen home, showTank;
  int sx = 0, sy = 0;
  if (!units && lot.take(32, 17, sx, sy)) {
    home = Pen{};
    home.x = sx; home.y = sy; home.w = 10; home.h = 16;
    showTank = Pen{};
    showTank.x = sx + 10; showTank.y = sy; showTank.w = 20; showTank.h = 16;
    home.id = enclose(home.x, home.y, 10, 16, typeOfKey("tankwal2"), "Everything home tank");
    showTank.id = enclose(showTank.x, showTank.y, 20, 16, typeOfKey("tankwal3"), "Everything show tank");
    for (int id : {home.id, showTank.id})
      if (id >= 0) {
        while (f.canAdjustWall(id, 1)) f.adjustWall(id, 1);
        f.fillExhibit(id, true);
      }
  }
  if (!units && (home.id < 0 || showTank.id < 0)) fail("the show tanks could not be built");

  // 2. Every tank wall, a tank each, salt and fresh in turn
  std::vector<Pen> tanks;
  {
    bool salt = true;
    for (const auto &t : f.types())
      if (t.tank) {
        Pen p;
        if (tank(4, 4, &t - f.types().data(), salt, "Everything " + t.key, p)) tanks.push_back(p);
        else didNotFit.push_back(t.file);
        salt = !salt;
      }
  }
  // 3. Every fence, an exhibit each; where it has the art, one piece worn
  // and one broken
  std::vector<Pen> fencePens;
  for (const auto &t : f.types()) {
    if (t.tank) continue;
    Pen p;
    if (!pen(3, 3, &t - f.types().data(), "Everything " + t.key, p)) { didNotFit.push_back(t.file); continue; }
    fencePens.push_back(p);
    if (t.det[0] && !t.indestructible) f.setLife({true, p.x, p.y}, static_cast<float>(t.decayedLife));
    if (t.broke[0] && !t.indestructible) f.setLife({true, p.x + 1, p.y}, 0.0f);
  }

  // Underwater scenery's tanks (the items part) and the marine animals'
  // (the units part), made as they fill
  std::vector<Pen> seas;
  auto sea = [&]() -> Pen * {
    Pen p;
    if (!tank(16, 16, typeOfKey("tankwall") >= 0 ? typeOfKey("tankwall") : typeOfKey("tankwal2"), true,
              "Everything sea " + std::to_string(seas.size() + 1), p))
      return nullptr;
    seas.push_back(p);
    return &seas.back();
  };
  const int landFence = typeOfKey("chainlnk") >= 0 ? typeOfKey("chainlnk") : 0;
  // A strip of path for staff and guests to stand on, as they do in play
  std::vector<std::pair<int, int>> walk;
  auto pathStrip = [&](int n) {
    int x = 0, y = 0;
    while (n > 0) {
      const int len = std::min(n, 30);
      if (!lot.take(len, 1, x, y)) break;
      for (int i = 0; i < len; ++i) {
        map.setPath(x + i, y, "path");
        walk.push_back({x + i, y});
      }
      n -= len;
    }
  };

  if (units) {
    // 4. Every animal - male, female and young, a dinosaur's egg too - in
    // an exhibit (or tank) of its own species, as a player keeps them:
    // together, the predators eat the rest before the original's census
    // (the census found whole species gone). Each given room for its
    // footprint (cFootprintX/Y, half tiles) and a tile beside.
    const int dinoFence = typeOfKey("dbar") >= 0 ? typeOfKey("dbar") : landFence;
    const int tankWall = typeOfKey("tankwall") >= 0 ? typeOfKey("tankwall") : typeOfKey("tankwal2");
    int adopted = 0;
    for (const auto &item : catalog.all()) {
      if (item.registrySection != "animals") continue;
      const int type = animals.typeOfFile(item.file);
      if (type < 0) { fail("no animal type for " + item.file); continue; }
      const bool water = animals.types()[type].underwater, dino = animals.types()[type].dinosaur;
      const int cell = std::max(1, (std::max(animals.types()[type].footX, animals.types()[type].footY) + 1) / 2) + 1;
      const int side = std::max(3, cell * 2);
      Pen home;
      const std::string name = "Everything " + stemOf(item.file);
      const bool built = water ? tank(side, side, tankWall, true, name, home)
                               : pen(side, side, dino ? dinoFence : landFence, name, home);
      if (!built) { didNotFit.push_back(item.file); continue; }
      for (int k = 0; k < (dino ? 4 : 3); ++k) {
        int x = 0, y = 0;
        if (!home.take(cell, cell, x, y)) { didNotFit.push_back(item.file); break; }
        const int id = animals.adopt(type, k == 1, x + cell / 2.0f, y + cell / 2.0f, map, f, true);
        if (id < 0) { fail("could not adopt " + item.file); break; }
        if (auto *m = animals.member(id)) {
          if (k == 2) m->baby = true;
          if (k == 3) m->egg = true;
        }
        ++adopted;
      }
    }
    note("animals adopted: " + std::to_string(adopted));
    // 5. Every kind of staff, two each, and guests: on paths
    pathStrip(static_cast<int>(staff.types().size()) * 2 + 30);
    size_t at = 0;
    int hired = 0;
    for (size_t t = 0; t < staff.types().size(); ++t)
      for (int k = 0; k < 2 && at < walk.size(); ++k, ++at)
        hired += staff.hire(static_cast<int>(t), walk[at].first + 0.5f, walk[at].second + 0.5f, map, f) >= 0;
    auto &people = const_cast<std::vector<Guests::Guest> &>(guests.guests());
    // (guests are let in at an entrance: the flattened test map has none
    // worked out yet, so the path's start)
    if (!guests.hasEntrance() && at < walk.size()) guests.setEntrance(walk[at].first, walk[at].second);
    for (int k = 0; k < 30 && at < walk.size(); ++k, ++at) {
      guests.admit(0);
      if (people.empty()) break;
      auto &g = people.back();
      g.x = walk[at].first + 0.5f;
      g.y = walk[at].second + 0.5f;
      g.state = Guests::State::Walking;
    }
    note("staff hired: " + std::to_string(hired) + ", guests: " + std::to_string(people.size()));
  } else {
    // 4. Every building and scenery item: on land, underwater ones in the
    // sea, show toys in the show tank
    sea();
    int sceneryPlaced = 0;
    // (ZT_EVERYTHING_NOSCENERY=1: none - to tell what they change)
    for (const auto &item : catalog.all()) {
      if (std::getenv("ZT_EVERYTHING_NOSCENERY")) break;
      if (item.file.rfind("scenery/", 0) != 0) continue;
      // (ZT_EVERYTHING_SCENERY=<from>-<to>: only those of the list, by
      // number - to find one that changes what the original keeps)
      static int sceneryIndex = 0;
      const int thisIndex = sceneryIndex++;
      if (const char *range = std::getenv("ZT_EVERYTHING_SCENERY")) {
        int lo = 0, hi = 1 << 30;
        std::sscanf(range, "%d-%d", &lo, &hi);
        if (thisIndex < lo || thisIndex >= hi) continue;
      }
      // (the filter is placed as one, below; food is put down as food)
      if (item.registrySection == "filter1" || item.registrySection == "rubble" || item.registrySection == "food") continue;
      // (a zoo entrance's pieces are the map's, made in the map editor, not
      // bought - a second entrance inside the zoo has the original drop the
      // animals and staff: census-verified, fgate/fgate1/fgate2)
      if (item.members.count("zoogate") && !std::getenv("ZT_EVERYTHING_GATES")) continue;
      const Info info = infoOf(item.file);
      int x = 0, y = 0;
      bool room = false;
      if (info.show && showTank.id >= 0) room = showTank.take(info.w, info.h, x, y);
      else if (info.water) {
        room = !seas.empty() && seas.back().take(info.w, info.h, x, y);
        if (!room && sea()) room = seas.back().take(info.w, info.h, x, y);
      } else
        room = lot.take(info.w, info.h, x, y);
      if (!room) { didNotFit.push_back(item.file); continue; }
      if (!objects.add(item.file, x + info.w / 2.0f, y + info.h / 2.0f, 0)) {
        fail("not placed: " + item.file);
        continue;
      }
      ++sceneryPlaced;
      const bool building = item.registrySection == "building";
      placed[(building ? "building/building/" : "objects/" + item.registrySection + "/") + lower(item.registryKey)]++;
    }
    note("buildings and scenery placed: " + std::to_string(sceneryPlaced));
    // 5. Every path, four tiles each
    for (const auto &item : catalog.all()) {
      if (item.registrySection != "paths") continue;
      int x = 0, y = 0;
      if (!lot.take(4, 1, x, y)) { didNotFit.push_back(item.file); continue; }
      for (int i = 0; i < 4; ++i) map.setPath(x + i, y, stemOf(item.file));
    }
    // 6. Every food, dung and litter (food and dung in fences' exhibits); a
    // filter on a tank
    size_t feedPen = 0;
    for (const auto &item : catalog.all()) {
      if (item.registrySection != "food") continue;
      int x = 0, y = 0;
      bool put = false;
      while (!put && feedPen < fencePens.size()) {
        if (fencePens[feedPen].take(1, 1, x, y)) put = true;
        else ++feedPen;
      }
      if (put) items.addFood(fencePens[feedPen].id, x + 0.5f, y + 0.5f, 1, lower(item.registryKey));
      else didNotFit.push_back(item.file);
    }
    for (int k = 0; k < 3; ++k) {
      int x = 0, y = 0;
      if (fencePens.size() > feedPen + 1 && fencePens[feedPen + 1].take(1, 1, x, y))
        items.add(ZooItems::Kind::Dung, fencePens[feedPen + 1].id, x + 0.5f, y + 0.5f);
      if (lot.take(1, 1, x, y)) items.add(ZooItems::Kind::Litter, -1, x + 0.5f, y + 0.5f);
    }
    if (!tanks.empty()) {
      bool filter = false;
      for (const auto &item : catalog.all())
        if (item.registrySection == "filter1" && f.selectFilter(item.file)) {
          const Pen &t = tanks.front();
          for (int i = 0; i < t.w && !filter; ++i) filter = f.placeFilter(t.x + i, t.y - 1, map);
          break;
        }
      if (!filter) note("no tank filter placed");
    }
    // 7. The show: two orcas, a marine specialist on a path, a program
    if (home.id >= 0 && showTank.id >= 0) {
      int x = 0, y = 0;
      home.take(3, 3, x, y);
      animals.adopt(animals.typeOfFile("animals/orca.ai"), false, x + 1.5f, y + 1.5f, map, f, true);
      home.take(3, 3, x, y);
      animals.adopt(animals.typeOfFile("animals/orca.ai"), true, x + 1.5f, y + 1.5f, map, f, true);
      pathStrip(1);
      if (!walk.empty())
        for (size_t t = 0; t < staff.types().size(); ++t)
          if (staff.types()[t].file == "staff/trainer.ai")
            staff.hire(static_cast<int>(t), walk.back().first + 0.5f, walk.back().second + 0.5f, map, f);
      world.reindex();
      // (ZT_EVERYTHING_NOSHOW=1: the same tanks, orcas and trainer, no
      // program - to tell what the program changes in the original)
      if (!std::getenv("ZT_EVERYTHING_NOSHOW")) {
        shows.selectSpecies(showTank.id, "animals/orca.ai");
        int tricks = 0;
        for (const char *trick : {"finswim", "spit", "ballplay", "beach"}) tricks += shows.add(showTank.id, trick);
        note("show program tricks: " + std::to_string(tricks));
      }
    }
  }
  note("reindexing");
  world.reindex();
  for (const auto &file : didNotFit) note("DID NOT FIT " + file);
  note("saving");

  // Save it, shared, and read its original part back
  std::filesystem::create_directories(out);
  const std::string path = out + "/everything-" + stem + ".zoo";
  bool saved = false;
  try {
    saved = SaveGame::save(path, world, sim, hud, mapPath);
  } catch (const std::exception &e) {
    fail(std::string("the save threw: ") + e.what());
  }
  if (!saved) {
    fail("save: " + SaveGame::lastError());
    report << "\n" << failures << " failures\n";
    return 1;
  }
  note("saved: reading its original part back");
  std::vector<char> bytes;
  {
    std::ifstream in(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), {});
  }
  size_t originalSize = bytes.size();
  if (bytes.size() > 36 && std::memcmp(bytes.data() + bytes.size() - 36, "ZT1EXTRA", 8) == 0) {
    uint64_t offset = 0;
    std::memcpy(&offset, bytes.data() + bytes.size() - 36 + 12, 8);
    originalSize = static_cast<size_t>(offset);
  } else
    fail("the save has no engine footer: not a shared zoo");
  void *data = std::malloc(originalSize);
  std::memcpy(data, bytes.data(), originalSize);
  ZooReader z;
  if (!z.load(MemoryManager::get().wrap(data, originalSize)) || !z.hasGame()) {
    fail("the original part does not read back completely");
    report << "\n" << failures << " failures\n";
    return 1;
  }

  // The rules, from the original's own registry
  // (the whole registry, as zoo.exe reads it: every top-level .cfg's
  // "key = file.ai" lines by section - not the buy catalog, which leaves
  // out what can't be bought, the dung and litter it lists too)
  std::map<std::string, std::set<std::string>> sectionsOf; // registry key -> its sections
  {
    std::vector<std::string> configs;
    for (const std::string &name : rm->listResources("", ".cfg"))
      if (name.find('/') == std::string::npos) configs.push_back(name);
    for (const auto &[name, text] : rm->readResources(configs)) {
      std::istringstream lines(text);
      std::string line, section;
      while (std::getline(lines, line)) {
        const size_t open = line.find('['), close = line.find(']');
        if (open != std::string::npos && close != std::string::npos && close > open &&
            line.find_first_not_of(" \t") == open) {
          section = lower(line.substr(open + 1, close - open - 1));
          continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty() || section.find('/') != std::string::npos) continue;
        std::string key = line.substr(0, eq), file = line.substr(eq + 1);
        auto trim = [](std::string &s) {
          s.erase(0, s.find_first_not_of(" \t\r"));
          s.erase(s.find_last_not_of(" \t\r") + 1);
        };
        trim(key);
        trim(file);
        file = lower(file);
        if (key.empty() || file.size() < 4 || file.compare(file.size() - 3, 3, ".ai") != 0) continue;
        sectionsOf[lower(key)].insert(section);
      }
    }
  }
  for (const auto &item : catalog.all()) sectionsOf[lower(item.registryKey)].insert(item.registrySection);
  // Every entity's id its own: the original keeps its units by id
  {
    std::map<uint32_t, std::vector<std::string>> byId;
    for (const auto &o : z.getObjects())
      if (o.decoded && o.id) byId[o.id].push_back(o.className + "/" + o.subClass + "/" + o.typeName);
    int shared = 0;
    for (const auto &[id, who] : byId)
      if (who.size() > 1 && shared++ < 20) {
        std::string list;
        for (const auto &w : who) list += (list.empty() ? "" : ", ") + w;
        fail("id " + std::to_string(id) + " shared by " + list);
      }
    if (shared > 20) fail(std::to_string(shared) + " ids shared in all");
  }
  std::map<std::string, int> holds;
  for (const auto &o : z.getObjects()) {
    const std::string c = o.className, s = lower(o.subClass), t = lower(o.typeName);
    holds[c + "/" + s + "/" + t]++;
    const std::string where = c + "/" + s + "/" + t;
    if (c == "objects" || c == "building") {
      // On the ground under it (as drawn, and as the original saves its own):
      // one at 0 over a sunk tank floor the original may drop
      const float gx = o.x / 64.0f, gy = o.y / 64.0f;
      if (const MapTile *tile = map.getTile(static_cast<int>(std::floor(gx)), static_cast<int>(std::floor(gy)))) {
        const float fx = gx - std::floor(gx), fy = gy - std::floor(gy);
        const float top = tile->cornerHeight[CORNER_X0Y0] * (1 - fx) + tile->cornerHeight[CORNER_X1Y0] * fx;
        const float bottom = tile->cornerHeight[CORNER_X0Y1] * (1 - fx) + tile->cornerHeight[CORNER_X1Y1] * fx;
        const int want = static_cast<int>(std::lround((top * (1 - fy) + bottom * fy) * 16));
        if (std::abs(o.z - want) > 8 && !fromMap.count(where))
          fail(where + ": saved at height " + std::to_string(o.z) + ", the ground there is " + std::to_string(want));
      }
      const auto found = sectionsOf.find(t);
      if (found == sectionsOf.end()) {
        // (lg_sandy's jeep1: scenery.cfg lists it, but ZUPDATE's scenery.cfg,
        // which the original reads instead, does not - it skips the map's
        // own record too, so passing it on changes nothing)
        if (fromMap.count(where)) {
          note("kept from the map, the original skips it too: " + where);
          originalSkips.insert(where);
        }
        else fail(where + ": no type of that name in the original's registry");
      }
      else if (c == "building" && (s != "building" || !found->second.count("building")))
        fail(where + ": a building must be a [building] type, saved as building/building");
      else if (c == "objects" && !found->second.count(s))
        fail(where + ": its subclass must be the registry section that lists it (" + *found->second.begin() + ")");
    } else if (c == "fences" || c == "tankwall") {
      const int type = f.typeIndex(s);
      if (type < 0) { fail(where + ": no fence of that name"); continue; }
      const auto &ft = f.types()[type];
      if (ft.savedKey != s) fail(where + ": the original names it " + ft.savedKey);
      if (t == "g" && !ft.hasGate) fail(where + ": a gate of a fence with no gate (the original drops it)");
      if (o.hasCondition && o.life == 0 && !ft.broke[0]) fail(where + ": broken, and it has no broken art");
      if (o.hasCondition && o.life > 0 && o.life <= static_cast<uint32_t>(ft.decayedLife) && !ft.det[0] && !ft.indestructible)
        fail(where + ": worn, and it has no worn art");
    } else if ((c == "animals" || c == "keeper" || c == "maint" || c == "tour") && (!units || s == "orca" || s == "trainer")) {
      // (the items part's few units - the show's: where each was saved, and
      // whose ground that is, for telling why the original might not keep one)
      const int ex = f.exhibitAt(static_cast<int>(std::floor(o.x / 64.0f)), static_cast<int>(std::floor(o.y / 64.0f)));
      const auto *e = f.exhibit(ex);
      note("unit " + where + " at " + std::to_string(o.x / 64.0f) + "," + std::to_string(o.y / 64.0f) + " z " +
           std::to_string(o.z) + " id " + std::to_string(o.id) + " in " +
           (e ? e->name + (e->tank ? " (tank, water " + std::to_string(e->water) + ")" : "") : std::string("no exhibit")) +
           " path " + std::to_string(map.getPathType(static_cast<int>(o.x / 64), static_cast<int>(o.y / 64))));
    }
    if (c == "animals") {
      if (!sectionsOf.count(s) || !sectionsOf[s].count("animals")) fail(where + ": no animal of that name");
    } else if (c == "paths") {
      if (!sectionsOf.count(t) || !sectionsOf[t].count("paths")) fail(where + ": no path of that name");
    } else if (c == "food") {
      if (!sectionsOf.count(t) || !sectionsOf[t].count("food")) fail(where + ": no food of that name");
    }
  }
  for (const auto &[key, n] : placed) {
    auto found = holds.find(key);
    if (found == holds.end()) fail("placed, but not in the original part: " + key);
    else if (found->second < n) fail("placed " + std::to_string(n) + ", " + std::to_string(found->second) + " in the original part: " + key);
  }

  // What the original part holds, for the Lab to compare with the original
  std::ofstream manifest(out + "/everything-" + stem + ".json");
  // (ZT_EVERYTHING_WRITER: make_release's hash of the save writer's sources,
  // so a verdict from the original can be matched to the code it judged)
  const char *writer = std::getenv("ZT_EVERYTHING_WRITER");
  manifest << "{\"writer\":\"" << jsonText(writer ? writer : "") << "\",\"map\":\"" << jsonText(mapPath) << "\",\"version\":" << z.getVersion()
           << ",\"width\":" << z.getMapWidth() << ",\"height\":" << z.getMapHeight() << ",\"records\":{";
  bool first = true;
  for (const auto &[key, n] : holds) {
    manifest << (first ? "" : ",") << "\"" << jsonText(key) << "\":" << n;
    first = false;
  }
  manifest << "},\"originalSkips\":[";
  first = true;
  for (const auto &key : originalSkips) {
    manifest << (first ? "" : ",") << "\"" << jsonText(key) << "\"";
    first = false;
  }
  manifest << "],\"didNotFit\":" << didNotFit.size() << ",\"failures\":" << failures << "}\n";
  note("original part: version " + std::to_string(z.getVersion()) + ", " + std::to_string(z.getMapWidth()) + "x" +
       std::to_string(z.getMapHeight()) + ", " + std::to_string(z.getObjects().size()) + " records, " +
       std::to_string(holds.size()) + " kinds");
  report << "\n" << failures << " failures, " << didNotFit.size() << " did not fit\n";
  SDL_Log("[Everything] %d failures, %zu did not fit: %s", failures, didNotFit.size(), path.c_str());
  return failures ? 1 : 0;
}
