#include "SaveGame.hpp"
#include "Goals.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <vector>

#include "Research.hpp"
#include "World.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#undef near
#undef far
#endif

namespace {
constexpr uint32_t kMagic = 0x5A543153; // "S1TZ"
constexpr uint32_t kMapMagic = 0x504D3153; // "S1MP": a map design (saveMap)
constexpr uint32_t kVersion = 9; // 9: filter definition paths persist per placed filter. // (7: before tanks kept the ground they sank from; 1: before visitors and exhibit books; 2: research progress whole; 3: no shared tank walls; 4: tank walls in height units; 5: no view turn; 6: tank walls in half units)

// A little binary writer and reader: numbers as they are, strings with
// their length, vectors with their count
struct Out {
  std::vector<char> b;
  template <class T> void raw(const T &v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const char *p = reinterpret_cast<const char *>(&v);
    b.insert(b.end(), p, p + sizeof(T));
  }
  void str(const std::string &s) {
    raw(static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
  }
};
struct In {
  const std::vector<char> &b;
  size_t at = 0;
  bool bad = false;
  template <class T> T raw() {
    T v{};
    if (at + sizeof(T) > b.size()) {
      bad = true;
      return v;
    }
    std::memcpy(&v, b.data() + at, sizeof(T));
    at += sizeof(T);
    return v;
  }
  template <class T> void get(T &v) { v = raw<T>(); }
  template <class T, size_t N> void get(T (&v)[N]) {
    for (size_t k = 0; k < N; k++)
      v[k] = raw<T>();
  }
  std::string str() {
    uint32_t n = raw<uint32_t>();
    if (bad || at + n > b.size()) {
      bad = true;
      return {};
    }
    std::string s(b.data() + at, n);
    at += n;
    return s;
  }
};

void putEdge(Out &o, const Fences::Edge &e) {
  o.raw(e.alongX);
  o.raw(e.x);
  o.raw(e.y);
}
Fences::Edge getEdge(In &i) {
  Fences::Edge e;
  i.get(e.alongX);
  i.get(e.x);
  i.get(e.y);
  return e;
}
} // namespace

std::string SaveGame::folder() {
  std::string base = ".";
#ifdef _WIN32
  char docs[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
    base = docs;
#endif
  std::filesystem::path p = std::filesystem::path(base) / "zt1-engine" / "Saved Games";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p.string();
}

// The original's dialogs: "Save a zoo..." / "Load a zoo...", Zoo Tycoon
// Saved Game files, in the Saved Games folder
static std::string fileDialog(bool saving) {
#ifdef _WIN32
  char file[MAX_PATH] = {};
  std::string dir = SaveGame::folder();
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof(ofn);
  // (owned by the game's window: on top of it, the game waiting - with no
  // owner it could open behind the window, the game seeming to hang)
  ofn.hwndOwner = GetActiveWindow() ? GetActiveWindow() : GetForegroundWindow();
  // (loading: the original game's own saved zoos too, .zoo)
  ofn.lpstrFilter = saving ? "Zoo Tycoon Saved Game (*.zt1save)\0*.zt1save\0"
                           : "All saved zoos (*.zt1save, *.zoo)\0*.zt1save;*.zoo\0"
                             "Zoo Tycoon Saved Game (*.zt1save)\0*.zt1save\0"
                             "The original game's saved zoos (*.zoo)\0*.zoo\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrInitialDir = dir.c_str();
  ofn.lpstrDefExt = "zt1save";
  ofn.lpstrTitle = saving ? "Save a zoo..." : "Load a zoo...";
  ofn.Flags = OFN_NOCHANGEDIR | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  BOOL ok = saving ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
  return ok ? std::string(file) : std::string();
#else
  (void)saving;
  return "";
#endif
}

std::string SaveGame::askSavePath() { return fileDialog(true); }
std::string SaveGame::askLoadPath() { return fileDialog(false); }

// ----------------------------------------------------------------------------
// The original game's own saved zoos (.zoo, read by ZooReader in zoo.exe's
// order: loadZoo 0x453000, worldLoad 0x452e13)
// ----------------------------------------------------------------------------
bool SaveGame::isOriginal(const std::string &path) {
  char magic[4] = {};
  FILE *fp = std::fopen(path.c_str(), "rb");
  if (!fp)
    return false;
  bool ok = std::fread(magic, 1, 4, fp) == 4 && std::memcmp(magic, "TZFB", 4) == 0;
  std::fclose(fp);
  return ok;
}

bool SaveGame::loadOriginal(World &world, ZooSim &sim, UiGameScreen &hud) {
  if (!loadOriginalWorld(world))
    return false;
  const ZooReader &z = world.zooReader;
  // The books: the cash (gameMgr +0xc), the date (+0x1194), the admission,
  // the rating
  if (z.hasGame()) {
    sim.money = z.getCash();
    sim.zooRating = z.getRating();
    const uint16_t *d = z.getDate();
    if (sim.months.empty())
      sim.months.push_back({});
    sim.months.back().month = std::clamp(static_cast<int>(d[1]) - 1, 0, 11);
    sim.months.back().year = std::max(1, static_cast<int>(d[0]) - 2000);
    sim.day = std::max(0, static_cast<int>(d[3]) - 1);
    sim.dirty = true;
    UiGameScreen::ZooInfo info = hud.zooInfo();
    info.admission = z.getAdmission();
    info.cash = static_cast<long>(sim.money);
    hud.setZooInfo(info);
  }
  return true;
}

bool SaveGame::loadOriginalWorld(World &world) {
  const ZooReader &z = world.zooReader;
  if (!z.isExact())
    return false;
  WorldMap &map = world.worldMap;
  Fences &f = world.fences;
  // The habitats: the map path keeps fenced ground as pockets; a saved zoo's
  // are exhibits (habitatMgrLoad 0x4c7e6c names each by a tile in it). Its
  // tanks' ground is sunk already: the heights are kept as the save has them
  std::vector<std::array<int, 5>> heights;
  for (int y = 0; y < map.height; y++)
    for (int x = 0; x < map.width; x++) {
      const MapTile &t = map.tiles[y][x];
      heights.push_back({t.height, t.cornerHeight[0], t.cornerHeight[1], t.cornerHeight[2], t.cornerHeight[3]});
    }
  f.exhibitList.clear();
  f.made = 0;
  f.updateExhibits(map, 1, 0, 1, false);
  for (int y = 0, k = 0; y < map.height; y++)
    for (int x = 0; x < map.width; x++, k++) {
      MapTile &t = map.tiles[y][x];
      t.height = heights[k][0];
      for (int c = 0; c < 4; c++)
        t.cornerHeight[c] = heights[k][1 + c];
    }
  map.touch();
  int named = 0;
  for (Fences::Exhibit &ex : f.exhibitList) {
    const ZooReader::Habitat *hab = nullptr;
    for (const ZooReader::Habitat &h : z.getHabitats())
      if (ex.tiles.count({h.x, h.y}))
        hab = &h;
    if (!hab) {
      // (no habitat of the save's: ground the walls shut off)
      ex.pocket = true;
      ex.named = false;
      continue;
    }
    ex.name = hab->name;
    ex.named = true;
    named++;
    if (ex.tank) {
      // The floor as the save has it; the ground round it; its walls and
      // water (in half steps over the floor: 5 and 4 is a new tank's)
      int floor = INT32_MAX, ground = INT32_MIN;
      for (auto [x, y] : ex.tiles)
        if (const MapTile *t = map.getTile(x, y))
          for (int c = 0; c < 4; c++)
            floor = std::min(floor, static_cast<int>(t->cornerHeight[c]));
      for (auto [x, y] : ex.tiles)
        for (auto [dx, dy] : {std::make_pair(1, 0), std::make_pair(-1, 0), std::make_pair(0, 1), std::make_pair(0, -1)})
          if (!ex.tiles.count({x + dx, y + dy}))
            if (const MapTile *t = map.getTile(x + dx, y + dy))
              for (int c = 0; c < 4; c++)
                ground = std::max(ground, static_cast<int>(t->cornerHeight[c]));
      ex.floorHeight = floor;
      ex.groundHeight = ground > INT32_MIN ? ground : floor + 2;
      ex.wallSub = std::max(1, hab->tankHeight);
      ex.water = hab->tankHeight > 1 ? std::clamp(hab->waterLevel / static_cast<float>(hab->tankHeight - 1), 0.0f, 1.0f) : 0.0f;
      ex.salt = hab->salt;
      ex.purity = static_cast<float>(std::clamp(hab->purity, 0, 100));
      ex.filling = true;
      ex.level = -1e9f;
    }
  }
  // Food and dung down where they lie, with what's left of them
  ZooItems &items = world.items;
  int piles = 0;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.decoded || (o.className != "food" && o.subClass != "poo" && o.subClass != "dinopoo"))
      continue;
    float x = o.x / 64.0f, y = o.y / 64.0f;
    int ex = f.exhibitAt(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    bool dung = o.subClass == "poo" || o.subClass == "dinopoo";
    items.add(dung ? ZooItems::Kind::Dung : ZooItems::Kind::Food, ex, x, y);
    ZooItems::Item &it = items.list.back();
    if (!dung) {
      it.food = o.subClass;
      it.units = it.full = static_cast<float>(std::max(0, o.food));
    }
    piles++;
  }
  // The animals, as they were (ZTAnimal::load 0x455533: their status)
  Animals &an = world.animals;
  int animals = 0, staffCount = 0, missed = 0;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.hasAnimal)
      continue;
    int type = an.typeOfFile("animals/" + o.subClass + ".ai");
    int id = type >= 0 ? an.adopt(type, o.animal.female, o.x / 64.0f, o.y / 64.0f, map, f, true) : -1;
    if (id < 0) {
      SDL_Log("[Save] the original's %s \"%s\" couldn't be put back at %.1f, %.1f", o.subClass.c_str(), o.name.c_str(),
              o.x / 64.0f, o.y / 64.0f);
      missed++;
      continue;
    }
    for (Animals::Member &m : an.list)
      if (m.id == id) {
        if (!o.name.empty())
          m.name = o.name;
        m.happiness = static_cast<float>(std::clamp(o.animal.happiness, -100, 100));
        m.hunger = static_cast<float>(o.animal.hunger);
        m.health = static_cast<float>(o.animal.health);
        m.energy = static_cast<float>(o.animal.energy);
        m.food = 100 - m.hunger;
        m.boxed = o.animal.crated;
        // (loose: the save's escape - its flag, or standing in no exhibit;
        // still an egg only if the save says so)
        m.escaped = o.animal.escaped || m.exhibit < 0;
        if (o.animal.egg && an.typeList[m.type].laysEggs)
          an.becomeEgg(m);
      }
    animals++;
  }
  an.markDirty();
  // The staff, where they stood (ZTStaff 0x455370 and its kinds)
  Staff &st = world.staff;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.decoded || (o.className != "keeper" && o.className != "maint" && o.className != "tour" &&
                       o.className != "staff" && o.className != "helicopter"))
      continue;
    int type = st.typeOfFile("staff/" + o.subClass + ".ai");
    int id = type >= 0 ? st.hire(type, o.x / 64.0f, o.y / 64.0f, map, f) : -1;
    if (id < 0) {
      SDL_Log("[Save] the original's %s \"%s\" couldn't be put back at %.1f, %.1f", o.subClass.c_str(), o.name.c_str(),
              o.x / 64.0f, o.y / 64.0f);
      missed++;
      continue;
    }
    if (Staff::Member *m = st.member(id))
      if (!o.name.empty())
        m->name = o.name;
    staffCount++;
  }
  SDL_Log("[Save] the original's saved zoo: %d habitats named, %d piles, %d animals, %d staff (%d couldn't be put back; "
          "its guests aren't read yet)",
          named, piles, animals, staffCount, missed);
  return true;
}

// The world as it's been made - the ground, paths, walkways, fences and
// exhibits, objects - written and read: a saved zoo has it, and so does a
// map design (saveMap: a freeform map made in the ZT1 Lab, over one of the
// game's maps)
void SaveGame::putWorld(void *out, World &world) {
  Out &o = *static_cast<Out *>(out);
  // The ground
  WorldMap &map = world.worldMap;
  o.raw(map.width);
  o.raw(map.height);
  for (int y = 0; y < map.height; y++)
    for (int x = 0; x < map.width; x++) {
      const MapTile &t = map.tiles[y][x];
      o.raw(t.height);
      o.raw(t.cornerHeight);
      o.raw(t.terrainType);
      o.raw(t.edgeBits);
      o.raw(t.substrate);
    }
  o.raw(static_cast<uint32_t>(map.pathTypes.size()));
  for (const std::string &s : map.pathTypes)
    o.str(s);
  o.raw(static_cast<uint32_t>(map.pathTile.size()));
  for (int16_t v : map.pathTile)
    o.raw(v);
  // Walkways
  o.raw(static_cast<uint32_t>(world.walkways.decks.size()));
  for (const auto &[pos, d] : world.walkways.decks) {
    o.raw(pos.first);
    o.raw(pos.second);
    o.str(d.type);
    o.raw(d.h);
  }
  // Fences, exhibits, filters
  Fences &f = world.fences;
  o.raw(static_cast<uint32_t>(f.edges.size()));
  for (const auto &[e, p] : f.edges) {
    putEdge(o, e);
    o.str(f.typeList[p.type].key);
    o.raw(p.gate);
    o.raw(p.facing);
    o.raw(p.ownerX);
    o.raw(p.ownerY);
    o.raw(p.order);
    o.raw(p.drag);
    o.raw(p.tank);
    o.raw(p.life);
    o.raw(p.tank2);
  }
  o.raw(static_cast<uint32_t>(f.exhibitList.size()));
  for (const Fences::Exhibit &x : f.exhibitList) {
    o.raw(x.id);
    o.str(x.name);
    o.raw(x.named);
    o.raw(x.pocket);
    o.raw(x.tank);
    o.raw(x.constructedDay);
    o.raw(x.constructedMonth);
    o.raw(x.constructedYear);
    o.raw(x.groundHeight);
    o.raw(x.floorHeight);
    o.raw(x.water);
    o.raw(x.wallSub);
    o.raw(x.salt);
    o.raw(x.filling);
    o.raw(x.purity);
    o.raw(x.purityClock);
    for (double v : {x.donationsNow, x.donationsLast, x.donationsTotal, x.upkeepNow, x.upkeepLast,
                     x.upkeepTotal, x.viewed, x.builtAt})
      o.raw(v);
    o.raw(static_cast<uint32_t>(x.tiles.size()));
    for (auto [tx, ty] : x.tiles) {
      o.raw(tx);
      o.raw(ty);
    }
    o.raw(static_cast<uint32_t>(x.before.size()));
    for (const auto &[at, g] : x.before) {
      o.raw(at.first);
      o.raw(at.second);
      o.raw(g.height);
      for (int c = 0; c < 4; c++)
        o.raw(g.corner[c]);
      o.raw(g.terrain);
    }
  }
  o.raw(f.made);
  o.raw(f.placed);
  o.raw(f.filtersMade);
  o.raw(static_cast<uint32_t>(f.filterList.size()));
  for (const Fences::Filter &x : f.filterList) {
    o.raw(x.x);
    o.raw(x.y);
    o.raw(x.tank);
    o.raw(x.health);
    o.raw(x.decayClock);
    o.raw(x.filterClock);
    o.raw(x.dx);
    o.raw(x.dy);
    o.str(x.name);
    o.raw(x.placedMonth);
    o.raw(x.upkeepLast);
    o.raw(x.upkeepCurrent);
    o.raw(x.upkeepTotal);
    o.str(x.file);
  }
  // Objects (the map's and those bought)
  PlacedObjects &po = world.placedObjects;
  o.raw(static_cast<uint32_t>(po.list.size()));
  for (const PlacedObjects::Object &x : po.list) {
    o.str(x.className);
    o.str(x.subClass);
    o.str(x.typeName);
    o.str(x.name);
    o.raw(x.x);
    o.raw(x.y);
    o.raw(x.z);
    o.raw(x.facing);
    o.raw(x.fence);
    o.raw(x.bought);
    o.raw(x.id);
    o.str(x.label);
    o.raw(x.price);
    o.raw(x.income);
    o.raw(x.upkeep);
    o.raw(x.openedMonth);
    o.raw(x.visitorsNow);
    o.raw(x.visitorsLast);
    o.raw(x.visitorsTotal);
    o.raw(static_cast<uint32_t>(x.colours.size()));
    for (int c : x.colours)
      o.raw(c);
    o.raw(x.fill);
    o.raw(x.nameId);
    o.raw(x.program);
    o.raw(static_cast<uint32_t>(x.sold.size()));
    for (const auto &[k, n] : x.sold) {
      o.str(k);
      o.raw(n);
    }
  }
  o.raw(po.nextId);
  o.raw(static_cast<uint32_t>(po.counts.size()));
  for (const auto &[k, n] : po.counts) {
    o.str(k);
    o.raw(n);
  }
}

bool SaveGame::getWorld(void *in, World &world, uint32_t version) {
  In &i = *static_cast<In *>(in);
  // The ground
  WorldMap &map = world.worldMap;
  int w = i.raw<int>(), h = i.raw<int>();
  if (w != map.width || h != map.height)
    return false;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      MapTile &t = map.tiles[y][x];
      i.get(t.height);
      i.get(t.cornerHeight);
      i.get(t.terrainType);
      i.get(t.edgeBits);
      i.get(t.substrate);
    }
  map.pathTypes.resize(i.raw<uint32_t>());
  for (std::string &s : map.pathTypes)
    s = i.str();
  map.pathTile.resize(i.raw<uint32_t>());
  for (int16_t &v : map.pathTile)
    i.get(v);
  map.touch();
  world.walkways.decks.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    int x = i.raw<int>(), y = i.raw<int>();
    Walkways::Deck d;
    d.type = i.str();
    i.get(d.h);
    world.walkways.decks[{x, y}] = d;
  }
  // Fences
  Fences &f = world.fences;
  f.edges.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    Fences::Edge e = getEdge(i);
    Fences::Piece p;
    p.type = f.typeIndex(i.str());
    i.get(p.gate);
    i.get(p.facing);
    i.get(p.ownerX);
    i.get(p.ownerY);
    i.get(p.order);
    i.get(p.drag);
    i.get(p.tank);
    i.get(p.life);
    if (version >= 4)
      i.get(p.tank2);
    if (p.type >= 0)
      f.edges[e] = p;
  }
  f.exhibitList.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    Fences::Exhibit x;
    i.get(x.id);
    x.name = i.str();
    i.get(x.named);
    i.get(x.pocket);
    i.get(x.tank);
    i.get(x.constructedDay);
    i.get(x.constructedMonth);
    i.get(x.constructedYear);
    i.get(x.groundHeight);
    i.get(x.floorHeight);
    i.get(x.water);
    i.get(x.wallSub);
    if (version < 5)
      x.wallSub -= x.floorHeight; // (it was the walls' top in height units)
    if (version < 5)
      x.wallSub *= 2; // (to the half units of 5 and 6)
    if (version < 7 && x.tank)
      x.wallSub = std::max(1, (x.wallSub + 1) / 2); // (half units to the original's whole ones)
    i.get(x.salt);
    i.get(x.filling);
    i.get(x.purity);
    i.get(x.purityClock);
    if (version >= 2)
      for (double *v : {&x.donationsNow, &x.donationsLast, &x.donationsTotal, &x.upkeepNow, &x.upkeepLast,
                        &x.upkeepTotal, &x.viewed, &x.builtAt})
        i.get(*v);
    for (uint32_t t = i.raw<uint32_t>(); t > 0 && !i.bad; t--) {
      int tx = i.raw<int>(), ty = i.raw<int>();
      x.tiles.insert({tx, ty});
    }
    if (version >= 8)
      for (uint32_t t = i.raw<uint32_t>(); t > 0 && !i.bad; t--) {
        int tx = i.raw<int>(), ty = i.raw<int>();
        Fences::Exhibit::Ground &g = x.before[{tx, ty}];
        i.get(g.height);
        for (int c = 0; c < 4; c++)
          i.get(g.corner[c]);
        i.get(g.terrain);
      }
    f.exhibitList.push_back(x);
  }
  i.get(f.made);
  i.get(f.placed);
  i.get(f.filtersMade);
  f.filterList.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    Fences::Filter x;
    i.get(x.x);
    i.get(x.y);
    i.get(x.tank);
    i.get(x.health);
    i.get(x.decayClock);
    i.get(x.filterClock);
    i.get(x.dx);
    i.get(x.dy);
    x.name = i.str();
    i.get(x.placedMonth);
    i.get(x.upkeepLast);
    i.get(x.upkeepCurrent);
    i.get(x.upkeepTotal);
    if (version >= 9) x.file = i.str();
    f.filterList.push_back(x);
  }
  // Objects: the art found again for each
  PlacedObjects &po = world.placedObjects;
  std::vector<PlacedObjects::Object> old = po.list;
  po.list.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    PlacedObjects::Object x;
    x.className = i.str();
    x.subClass = i.str();
    x.typeName = i.str();
    po.resolveExt(x);
    x.name = i.str();
    i.get(x.x);
    i.get(x.y);
    i.get(x.z);
    i.get(x.facing);
    i.get(x.fence);
    i.get(x.bought);
    i.get(x.id);
    x.label = i.str();
    i.get(x.price);
    i.get(x.income);
    i.get(x.upkeep);
    i.get(x.openedMonth);
    if (version >= 2) {
      i.get(x.visitorsNow);
      i.get(x.visitorsLast);
      i.get(x.visitorsTotal);
      for (uint32_t n = i.raw<uint32_t>(); n > 0 && n < 8 && !i.bad; n--)
        x.colours.push_back(i.raw<int>());
      i.get(x.fill);
      if (version >= 3) {
        i.get(x.nameId);
        i.get(x.program);
      }
    }
    for (uint32_t s = i.raw<uint32_t>(); s > 0 && !i.bad; s--) {
      std::string k = i.str();
      x.sold[k] = i.raw<int>();
    }
    // (its art: the map's own objects as loaded, bought ones by their file)
    for (const PlacedObjects::Object &m : old)
      if (m.className == x.className && m.subClass == x.subClass && m.typeName == x.typeName) {
        x.art = m.art;
        x.slopeUp = m.slopeUp;
        x.slopeDown = m.slopeDown;
        break;
      }
    if (!x.art || x.bought)
      x.art = po.artOfFile(PlacedObjects::fileOf(x));
    // (painted: its colours' art)
    if (x.art && !x.colours.empty()) {
      std::vector<int> picks = x.colours;
      for (size_t c = 0; c < picks.size(); c++)
        po.setColour(x, static_cast<int>(c), picks[c]);
    }
    if (x.art && x.bought)
      po.loadLayers(x);
    if (x.art)
      po.list.push_back(x);
  }
  i.get(po.nextId);
  po.counts.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    std::string k = i.str();
    po.counts[k] = i.raw<int>();
  }
  return !i.bad;
}

bool SaveGame::save(const std::string &path, World &world, ZooSim &sim, UiGameScreen &hud,
                    const std::string &mapPath) {
  Out o;
  o.raw(kMagic);
  o.raw(kVersion);
  o.str(mapPath);
  // The books
  o.raw(sim.money);
  o.raw(sim.zooRating);
  o.raw(sim.day);
  o.raw(sim.dayCount);
  o.raw(static_cast<uint32_t>(sim.months.size()));
  for (const ZooSim::Month &m : sim.months) {
    o.raw(m.month);
    o.raw(m.year);
    o.raw(m.lines);
    o.raw(m.rating);
  }
  // The zoo's name and admission
  const UiGameScreen::ZooInfo &info = hud.zooInfo();
  o.str(info.name);
  o.raw(info.admission);
  o.raw(hud.marketingLevel);
  // Research
  auto &branches = Research::get().branches();
  o.raw(static_cast<uint32_t>(branches.size()));
  for (const ResearchBranch &b : branches) {
    o.raw(b.fundingLevel);
    o.raw(b.currentCategory);
    o.raw(b.currentProgram);
    o.raw(static_cast<uint32_t>(b.categories.size()));
    for (const ResearchCategory &c : b.categories) {
      o.raw(static_cast<uint32_t>(c.programs.size()));
      for (const ResearchProgram &p : c.programs) {
        o.raw(p.progress);
        o.raw(p.done);
      }
    }
  }
  putWorld(&o, world);
  // Food, dung, litter
  o.raw(static_cast<uint32_t>(world.items.list.size()));
  for (const ZooItems::Item &x : world.items.list) {
    o.raw(x.id);
    o.raw(x.kind);
    o.raw(x.exhibit);
    o.raw(x.x);
    o.raw(x.y);
    o.raw(x.units);
    o.raw(x.full);
    o.str(x.food);
  }
  o.raw(world.items.nextId);
  // Animals
  Animals &an = world.animals;
  o.raw(static_cast<uint32_t>(an.list.size()));
  for (const Animals::Member &m : an.list) {
    o.str(an.typeList[m.type].file);
    o.raw(m.id);
    o.raw(m.female);
    o.raw(m.baby);
    o.str(m.name);
    o.raw(m.x);
    o.raw(m.y);
    o.raw(m.happiness);
    o.raw(m.hunger);
    o.raw(m.health);
    o.raw(m.energy);
    o.raw(m.dirty);
    o.raw(m.sick);
    o.raw(m.escaped);
    o.raw(m.boxed);
    o.raw(m.lastAte);
    o.raw(m.lastSlept);
    o.raw(m.life);
    o.raw(m.ready);
    o.raw(m.grow);
  }
  o.raw(an.nextId);
  o.raw(static_cast<uint32_t>(an.counts.size()));
  for (const auto &[k, n] : an.counts) {
    o.str(an.typeList[k].file);
    o.raw(n);
  }
  o.raw(an.now);
  // Staff
  Staff &st = world.staff;
  o.raw(static_cast<uint32_t>(st.list.size()));
  for (const Staff::Member &m : st.list) {
    o.str(st.typeList[m.type].file);
    o.raw(m.id);
    o.raw(m.female);
    o.raw(m.hair);
    o.raw(m.skin);
    o.str(m.name);
    // (one in a tank, or climbing in or out, is kept on the bank by its gate)
    const bool wet = m.tank >= 0 || m.water != 0 || m.climbing;
    o.raw(wet ? m.outX : m.x);
    o.raw(wet ? m.outY : m.y);
    o.raw(m.duties);
    o.raw(static_cast<uint32_t>(m.exhibits.size()));
    for (int e : m.exhibits)
      o.raw(e);
  }
  o.raw(st.nextId);
  o.raw(static_cast<uint32_t>(st.counts.size()));
  for (const auto &[k, n] : st.counts) {
    o.str(st.typeList[k].file);
    o.raw(n);
  }
  // Guests
  Guests &gs = world.guests;
  o.raw(static_cast<uint32_t>(gs.list.size()));
  for (const Guests::Guest &g : gs.list) {
    o.raw(g.id);
    o.raw(g.type);
    o.raw(g.colours);
    o.raw(g.x);
    o.raw(g.y);
    o.raw(g.happiness);
    o.raw(g.hunger);
    o.raw(g.thirst);
    o.raw(g.bathroom);
    o.raw(g.tired);
    o.raw(g.favourite);
    o.raw(g.leaving);
    o.raw(g.trash);
    o.str(g.name);
    o.raw(g.arrived);
  }
  o.raw(gs.nextId);
  o.raw(gs.clock);
  o.raw(world.clock);
  // The view
  const Camera &cam = world.worldRenderer.getCamera();
  o.raw(cam.x);
  o.raw(cam.y);
  o.raw(cam.zoom);
  // The goals: each one's state, the awards received
  {
    std::vector<int> states;
    std::vector<int> awards;
    if (SaveGame::goals) {
      for (const Goals::Goal &g : SaveGame::goals->all())
        states.push_back(g.state);
      awards = SaveGame::goals->awards;
    }
    o.raw(static_cast<uint32_t>(states.size()));
    for (int st : states)
      o.raw(st);
    o.raw(static_cast<uint32_t>(awards.size()));
    for (int a : awards)
      o.raw(a);
  }
  // The way the view's turned (its camera above is for that turn)
  o.raw(world.worldRenderer.getRotation());

  FILE *fp = std::fopen(path.c_str(), "wb");
  if (!fp)
    return false;
  bool ok = std::fwrite(o.b.data(), 1, o.b.size(), fp) == o.b.size();
  std::fclose(fp);
  return ok;
}

static bool readAll(const std::string &path, std::vector<char> &out) {
  FILE *fp = std::fopen(path.c_str(), "rb");
  if (!fp)
    return false;
  std::fseek(fp, 0, SEEK_END);
  long n = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  out.resize(n > 0 ? static_cast<size_t>(n) : 0);
  bool ok = n > 0 && std::fread(out.data(), 1, out.size(), fp) == out.size();
  std::fclose(fp);
  return ok;
}

std::string SaveGame::mapOf(const std::string &path) {
  std::vector<char> b;
  if (!readAll(path, b))
    return "";
  In i{b};
  if (i.raw<uint32_t>() != kMagic || i.raw<uint32_t>() > kVersion)
    return "";
  return i.str();
}

bool SaveGame::load(const std::string &path, World &world, ZooSim &sim, UiGameScreen &hud) {
  std::vector<char> b;
  if (!readAll(path, b))
    return false;
  In i{b};
  if (i.raw<uint32_t>() != kMagic)
    return false;
  const uint32_t version = i.raw<uint32_t>();
  if (version < 1 || version > kVersion)
    return false;
  i.str(); // the map (already loaded)
  i.get(sim.money);
  i.get(sim.zooRating);
  i.get(sim.day);
  i.get(sim.dayCount);
  sim.months.resize(i.raw<uint32_t>());
  for (ZooSim::Month &m : sim.months) {
    i.get(m.month);
    i.get(m.year);
    i.get(m.lines);
    i.get(m.rating);
  }
  sim.dirty = true;
  UiGameScreen::ZooInfo info = hud.zooInfo();
  info.name = i.str();
  i.get(info.admission);
  info.cash = static_cast<long>(sim.money);
  hud.setZooInfo(info);
  i.get(hud.marketingLevel);
  auto &branches = Research::get().branches();
  uint32_t nb = i.raw<uint32_t>();
  for (uint32_t k = 0; k < nb && !i.bad; k++) {
    ResearchBranch dummy;
    ResearchBranch &br = k < branches.size() ? branches[k] : dummy;
    i.get(br.fundingLevel);
    i.get(br.currentCategory);
    i.get(br.currentProgram);
    uint32_t nc = i.raw<uint32_t>();
    for (uint32_t c = 0; c < nc && !i.bad; c++) {
      uint32_t np = i.raw<uint32_t>();
      for (uint32_t p = 0; p < np && !i.bad; p++) {
        float progress = version >= 3 ? i.raw<float>() : static_cast<float>(i.raw<int>());
        bool done = i.raw<bool>();
        if (c < br.categories.size() && p < br.categories[c].programs.size()) {
          br.categories[c].programs[p].progress = progress;
          br.categories[c].programs[p].done = done;
        }
      }
    }
  }
  if (!getWorld(&i, world, version))
    return false;
  Fences &f = world.fences;
  // Items
  world.items.list.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    ZooItems::Item x;
    i.get(x.id);
    i.get(x.kind);
    i.get(x.exhibit);
    i.get(x.x);
    i.get(x.y);
    i.get(x.units);
    i.get(x.full);
    x.food = i.str();
    world.items.list.push_back(x);
  }
  i.get(world.items.nextId);
  // Animals
  Animals &an = world.animals;
  an.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    Animals::Member m;
    m.type = an.typeOfFile(i.str());
    i.get(m.id);
    i.get(m.female);
    i.get(m.baby);
    m.name = i.str();
    i.get(m.x);
    i.get(m.y);
    i.get(m.happiness);
    i.get(m.hunger);
    i.get(m.health);
    i.get(m.energy);
    i.get(m.dirty);
    i.get(m.sick);
    i.get(m.escaped);
    i.get(m.boxed);
    i.get(m.lastAte);
    i.get(m.lastSlept);
    i.get(m.life);
    i.get(m.ready);
    i.get(m.grow);
    m.exhibit = f.exhibitAt(static_cast<int>(m.x), static_cast<int>(m.y));
    m.food = 100 - m.hunger;
    if (m.type >= 0)
      an.list.push_back(m);
  }
  i.get(an.nextId);
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    int k = an.typeOfFile(i.str());
    int c = i.raw<int>();
    if (k >= 0)
      an.counts[k] = c;
  }
  i.get(an.now);
  an.markDirty();
  // Staff
  Staff &st = world.staff;
  st.list.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    Staff::Member m;
    m.type = st.typeOfFile(i.str());
    i.get(m.id);
    i.get(m.female);
    i.get(m.hair);
    i.get(m.skin);
    m.name = i.str();
    i.get(m.x);
    i.get(m.y);
    i.get(m.duties);
    for (uint32_t e = i.raw<uint32_t>(); e > 0 && !i.bad; e--)
      m.exhibits.push_back(i.raw<int>());
    if (m.type >= 0)
      st.list.push_back(m);
  }
  i.get(st.nextId);
  st.counts.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    int k = st.typeOfFile(i.str());
    int c = i.raw<int>();
    if (k >= 0)
      st.counts[k] = c;
  }
  // Guests
  Guests &gs = world.guests;
  gs.list.clear();
  for (uint32_t n = i.raw<uint32_t>(); n > 0 && !i.bad; n--) {
    Guests::Guest g;
    i.get(g.id);
    i.get(g.type);
    i.get(g.colours);
    i.get(g.x);
    i.get(g.y);
    i.get(g.happiness);
    i.get(g.hunger);
    i.get(g.thirst);
    i.get(g.bathroom);
    i.get(g.tired);
    i.get(g.favourite);
    i.get(g.leaving);
    i.get(g.trash);
    g.name = i.str();
    i.get(g.arrived);
    g.playFor = 0.5f;
    if (g.type >= 0 && g.type < static_cast<int>(gs.typeList.size()))
      gs.list.push_back(g);
  }
  i.get(gs.nextId);
  i.get(gs.clock);
  i.get(world.clock);
  Camera &cam = world.worldRenderer.getCamera();
  i.get(cam.x);
  i.get(cam.y);
  i.get(cam.zoom);
  if (version >= 2) {
    std::vector<int> states, awards;
    for (uint32_t n = i.raw<uint32_t>(); n > 0 && n < 10000 && !i.bad; n--)
      states.push_back(i.raw<int>());
    for (uint32_t n = i.raw<uint32_t>(); n > 0 && n < 1000 && !i.bad; n--)
      awards.push_back(i.raw<int>());
    if (SaveGame::goals && !i.bad) {
      // (what the goals did, done again quietly; the awards as they were)
      SaveGame::goals->awards.clear();
      SaveGame::goals->restore(states);
      SaveGame::goals->awards = awards;
      hud.setAwards(awards);
    }
  }
  if (version >= 6 && !i.bad) {
    int turn = i.raw<int>();
    float cx = cam.x, cy = cam.y, cz = cam.zoom;
    if (!i.bad && turn >= 0 && turn < 4) {
      world.rotateView((turn - world.worldRenderer.getRotation() + 4) % 4);
      cam.x = cx;
      cam.y = cy;
      cam.zoom = cz;
    }
  }
  world.reindex();
  return !i.bad;
}

// A map design: a freeform map made in the ZT1 Lab (Test Zoo > Save as a
// map), over one of the game's maps - that map's .zoo, then the world as it
// was made (putWorld) and the view it starts on. A mod's .scn names it in
// [start] design=, and World::loadFreeform puts it over the map (the
// "custommaps" switch, Features.hpp)
bool SaveGame::saveMap(const std::string &path, World &world, const std::string &over) {
  Out o;
  o.raw(kMapMagic);
  o.raw(kVersion);
  o.str(over);
  putWorld(&o, world);
  const Camera &cam = world.worldRenderer.getCamera();
  o.raw(cam.x);
  o.raw(cam.y);
  o.raw(cam.zoom);
  o.raw(world.worldRenderer.getRotation());
  FILE *fp = std::fopen(path.c_str(), "wb");
  if (!fp)
    return false;
  bool ok = std::fwrite(o.b.data(), 1, o.b.size(), fp) == o.b.size();
  std::fclose(fp);
  return ok;
}

bool SaveGame::applyMap(const std::vector<char> &b, World &world) {
  In i{b};
  if (i.raw<uint32_t>() != kMapMagic)
    return false;
  const uint32_t version = i.raw<uint32_t>();
  if (version < 8 || version > kVersion)
    return false;
  i.str(); // the map it's over (already loaded)
  if (!getWorld(&i, world, version))
    return false;
  Camera &cam = world.worldRenderer.getCamera();
  int cx = 0, cy = 0;
  float cz = 1;
  i.get(cx);
  i.get(cy);
  i.get(cz);
  int turn = i.raw<int>();
  if (!i.bad && turn >= 0 && turn < 4) {
    world.rotateView((turn - world.worldRenderer.getRotation() + 4) % 4);
    cam.x = cx;
    cam.y = cy;
    cam.zoom = cz;
  }
  world.reindex();
  return !i.bad;
}

Goals *SaveGame::goals = nullptr;
