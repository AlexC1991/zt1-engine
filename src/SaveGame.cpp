#include "SaveGame.hpp"
#include "Goals.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>
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
constexpr uint32_t kVersion = 12; // 12: imported identity, facing, inventory and status/check timers. // 11: retain tranquilized animals across save/load. // 9: filter definition paths persist per placed filter. // (7: before tanks kept the ground they sank from; 1: before visitors and exhibit books; 2: research progress whole; 3: no shared tank walls; 4: tank walls in height units; 5: no view turn; 6: tank walls in half units)

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
                             "The original game's saved zoos (*.zoo)\0*.zoo\0"
                             "All files (*.*)\0*.*\0";
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

// Filters need the saved zoo's live tank exhibits, not the temporary
// map pockets produced by Fences::load. Run this after habitat restoration.
static int restoreOriginalFilters(const ZooReader &reader, Fences &fences, const WorldMap &map) {
  int missed = 0;
  const std::string previous = fences.filterType().file;
  for (const auto &obj : reader.getObjects()) {
    if (!obj.decoded || obj.className != "filter") continue;
    bool installed = false;
    for (const auto &file : {
           "scenery/" + obj.subClass + "/" + obj.typeName + ".ai",
           "scenery/other/" + obj.subClass + ".ai",
           "scenery/other/" + obj.typeName + ".ai"})
      if (fences.selectFilter(file)) { installed = true; break; }
    if (!installed) {
      SDL_Log("[Save] original filter %s/%s is not installed", obj.subClass.c_str(), obj.typeName.c_str());
      ++missed;
      continue;
    }
    const auto &kind = fences.filterType();
    int x = static_cast<int>(std::floor(obj.x / 64.0f - kind.footprintX * 0.5f + 0.001f));
    int y = static_cast<int>(std::floor(obj.y / 64.0f - kind.footprintY * 0.5f + 0.001f));
    if (fences.placeFilter(x, y, map)) {
      auto *filter = fences.filter(static_cast<int>(fences.filters().size()) - 1);
      if (obj.filterHealth >= 0)
        filter->health = static_cast<float>(std::clamp(obj.filterHealth, 0, kind.startHealth));
      if (!obj.name.empty()) filter->name = obj.name;
    } else {
      SDL_Log("[Save] original filter %s could not be restored at %d,%d", obj.name.c_str(), x, y);
      ++missed;
    }
  }
  fences.selectFilter(previous);
  return missed;
}

// Original dates are FILETIMEs in the zoo calendar, not wall-clock play time.
static double originalElapsed(const ZooReader &z, uint64_t then) {
  const auto *d = z.getDate();
  const int year = d[0], month = d[1], day = d[3];
  if (!then || year < 1601 || year > 9999 || month < 1 || month > 12 ||
      day < 1 || day > ZooSim::daysInMonth(month - 1, year - 2000)) return 0;
  auto leaps = [](int y) { return y / 4 - y / 100 + y / 400; };
  int64_t days = int64_t(year - 1601) * 365 + leaps(year - 1) - leaps(1600);
  for (int m = 1; m < month; ++m) {
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    days += lengths[m - 1] + (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)));
  }
  const uint64_t seconds = uint64_t(days + day - 1) * 86400 + d[4] * 3600 + d[5] * 60 + d[6];
  const uint64_t now = seconds * 10000000ULL + uint64_t(d[7]) * 10000;
  return then <= now ? double(now - then) / 10000000.0 / 86400.0 * ZooSim::kSecondsPerDay : 0;
}
static void originalFacing(uint32_t direction, float &x, float &y) {
  const float angle = (direction & 7) * 3.14159265358979323846f / 4;
  x = std::sin(angle); y = -std::cos(angle);
}

void SaveGame::restoreOriginalManagers(World &world, UiGameScreen &hud) {
  const auto &z = world.zooReader;
  if (!z.hasGame()) return;
  Research &research = Research::get();
  research.restart();
  // researchMgr::load (0x593c3c) matches the config's name IDs. The
  // kind-2 payload is the bit representation of a float, not an integer.
  for (const auto &record : z.getResearch())
    for (auto &branch : research.branches()) {
      if (record.kind == 0 && branch.nameId == record.id)
        branch.fundingLevel = std::clamp(static_cast<int>(record.value), 0,
                                       std::max(0, static_cast<int>(branch.funding.size()) - 1));
      for (size_t c = 0; c < branch.categories.size(); ++c) {
        auto &category = branch.categories[c];
        if (record.kind == 1 && category.nameId == record.id)
          branch.enabled[c] = record.value != 0;
        if (record.kind == 2)
          for (auto &program : category.programs)
            if (program.nameId == record.id) {
              float progress = 0;
              std::memcpy(&progress, &record.value, sizeof progress);
              if (std::isfinite(progress) && progress >= 0) {
                program.progress = progress;
                program.done = progress >= program.cost;
              }
            }
      }
    }
  for (auto &branch : research.branches()) research.pick(branch);
  hud.marketingLevel = std::clamp(z.getMarketing(), 0,
                                std::max(0, static_cast<int>(hud.marketing.size()) - 1));
  // awardsLoad stores the same target IDs used by the award goal action.
  std::vector<int> awards = z.getAwards();
  std::sort(awards.begin(), awards.end());
  awards.erase(std::unique(awards.begin(), awards.end()), awards.end());
  if (goals) goals->awards = awards;
  hud.setAwards(awards);
}

void SaveGame::restoreOriginalBooks(World &world, ZooSim &sim) {
  const auto &z = world.zooReader;
  if (!z.hasGame()) return;
  const auto &books = z.getBooks();
  const int month = std::clamp(static_cast<int>(z.getDate()[1]) - 1, 0, 11);
  const int year = std::clamp(static_cast<int>(z.getDate()[0]) - 2000, 1, 9999);
  sim.months.clear();
  sim.importedAnnual = books.yearly; sim.importedTotals = books.total;
  // The original retains a rolling 12 months, in January..December slots.
  // Its finance panel (0x52f820, tables at 0x635a90) combines the rows below.
  const std::vector<std::vector<int>> rows = {
      {4}, {5}, {8,20}, {6,7}, {30}, {9,10},
      {3}, {2}, {0,1,28}, {11,12,13}, {19}, {29}};
  for (int ago = std::min(12, (year - 1) * 12 + month + 1) - 1; ago >= 0; --ago) {
    const int serial = (year - 1) * 12 + month - ago;
    ZooSim::Month entry; entry.year = serial / 12 + 1; entry.month = serial % 12;
    for (size_t line = 0; line < rows.size(); ++line)
      for (int row : rows[line])
        if (row < static_cast<int>(books.monthly.size())) {
          const float value = books.monthly[row][entry.month];
          if (std::isfinite(value))
            entry.lines[line] += line >= ZooSim::Construction ? -value : value;
        }
    // Row 14 is the net total, already represented by these income/expense
    // rows; adding it would count every transaction twice.
    entry.rating = sim.zooRating;
    sim.months.push_back(entry);
  }
  const auto *d = z.getDate();
  sim.day = std::clamp(static_cast<int>(d[3]) - 1, 0, ZooSim::daysInMonth(month, year) - 1) +
            (d[4] * 3600 + d[5] * 60 + d[6]) / 86400.0;
  // Compute elapsed calendar days for monthly unlocks without replaying
  // charges, admissions or notifications from the original zoo.
  sim.dayCount = 0;
  for (int y = 1; y < year; ++y) for (int m = 0; m < 12; ++m) sim.dayCount += ZooSim::daysInMonth(m, y);
  for (int m = 0; m < month; ++m) sim.dayCount += ZooSim::daysInMonth(m, year);
  sim.dayCount += static_cast<long>(sim.day);
  sim.dirty = true;
}

bool SaveGame::loadOriginal(World &world, ZooSim &sim, UiGameScreen &hud) {
  if (!loadOriginalWorld(world))
    return false;
  const ZooReader &z = world.zooReader;
  restoreOriginalManagers(world, hud);
  // The books: the cash (gameMgr +0xc), the date (+0x1194), the admission,
  // the rating
  if (z.hasGame()) {
    sim.money = z.getCash();
    sim.zooRating = z.getRating();
    restoreOriginalBooks(world, sim);
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
  const int filterMisses = restoreOriginalFilters(z, f, map);
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
  int animals = 0, staffCount = 0, missed = filterMisses;
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
        if (o.id > 0 && o.id < static_cast<uint32_t>(INT32_MAX)) {
          m.id = static_cast<int>(o.id); an.nextId = std::max(an.nextId, m.id + 1);
        }
        originalFacing(o.direction, m.fx, m.fy);
        m.baby = o.typeName == "y";
        m.dirty = static_cast<float>(o.animal.dung);
        m.sick = o.animal.sick; m.sleeping = o.animal.sleeping;
        m.dying = o.animal.dying;
        if (o.animal.hasLife) m.life = o.animal.life;
        m.grow = o.animal.grow; m.ready = o.animal.ready;
        m.hungerCheck = o.animal.checks[0]; m.healthCheck = o.animal.checks[1];
        m.energyCheck = o.animal.checks[2]; m.otherCheck = o.animal.checks[3];
        m.captivityCheck = o.animal.checks[5]; m.habitatCheck = o.animal.checks[6];
        m.boredCheck = o.animal.checks[7]; m.socialCheck = o.animal.checks[8];
        m.reproductionCheck = o.animal.checks[9];
        m.preyCheck = m.huntCheck = o.animal.chaseCheck;
        m.lastAte = an.now - originalElapsed(z, o.animal.lastAte);
        m.lastSlept = an.now - originalElapsed(z, o.animal.lastSlept);
        m.rampaging = o.animal.rampaging; m.rampageTicks = o.animal.rampageTicks;
        m.happiness = static_cast<float>(std::clamp(o.animal.happiness, -100, 100));
        m.hunger = static_cast<float>(o.animal.hunger);
        m.health = static_cast<float>(o.animal.health);
        m.energy = static_cast<float>(o.animal.energy);
        m.food = 100 - m.hunger;
        m.boxed = o.animal.crated;
        // (loose: the save's escape - its flag, or standing in no exhibit;
        // still an egg only if the save says so)
        m.escaped = o.animal.escaped || m.exhibit < 0;
        if (o.animal.egg && an.typeList[m.type].laysEggs) {
          an.becomeEgg(m); m.hatchIn = o.animal.hatchIn;
          m.health = static_cast<float>(o.animal.health); m.grow = o.animal.grow;
        }
        if (m.dying) {
          m.stack.clear(); m.path.clear(); m.stepping = false;
          m.dieAfter = an.set(m, "bdie") ? 1 : 0;
          if (m.dieAfter) an.pushSet(m, "bdie");
        }
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
    if (Staff::Member *m = st.member(id)) {
      if (!o.name.empty()) m->name = o.name;
      if (o.id > 0 && o.id < static_cast<uint32_t>(INT32_MAX)) {
        m->id = static_cast<int>(o.id); st.nextId = std::max(st.nextId, m->id + 1);
      }
      originalFacing(o.direction, m->fx, m->fy);
      m->female = o.typeName == "f";
      if (!o.colours.empty()) m->hair = o.colours[0];
      if (o.colours.size() > 1) m->skin = o.colours[1];
      if (o.hasStaff) {
        std::copy(o.staff.duties.begin(), o.staff.duties.end(), m->duties);
        m->workCheck = static_cast<float>(o.staff.work);
        m->crowdClock = static_cast<float>(o.staff.crowd);
        for (auto [x, y] : o.staff.exhibits) {
          const int ex = f.exhibitAt(x, y);
          if (ex >= 0 && std::find(m->exhibits.begin(), m->exhibits.end(), ex) == m->exhibits.end())
            m->exhibits.push_back(ex);
        }
      }
    }
    staffCount++;
  }
  // Original guest records were decoded but discarded. Restore visitors
  // without calling admit(): they have already paid their entrance fee.
  // Original task/route pointers belong to zoo.exe; let our AI choose a
  // fresh task after a short stand, as for native save restoration.
  Guests &gs = world.guests;
  gs.clear();
  int guestCount = 0;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.decoded || !o.hasGuest)
      continue;
    int type = -1;
    for (size_t k = 0; k < gs.typeList.size(); ++k)
      if (gs.typeList[k].key == o.typeName || gs.typeList[k].key == o.subClass)
        type = static_cast<int>(k);
    if (type < 0 || o.id >= static_cast<uint32_t>(INT32_MAX) ||
        !map.getTile(o.tileX(), o.tileY())) {
      SDL_Log("[Save] original guest %s/%s id %u could not be restored", o.subClass.c_str(), o.typeName.c_str(), o.id);
      missed++;
      continue;
    }
    Guests::Guest g;
    g.id = static_cast<int>(o.id);
    g.type = type;
    g.name = o.name.empty() ? "Guest " + std::to_string(g.id) : o.name;
    g.x = o.x / 64.0f;
    g.y = o.y / 64.0f;
    g.speed = gs.typeList[type].speed;
    g.arrived = gs.clock - originalElapsed(z, o.guest.arrived);
    originalFacing(o.direction, g.fx, g.fy);
    g.hungerCheck = o.guest.checks[0]; g.thirstCheck = o.guest.checks[1];
    g.bathroomCheck = o.guest.checks[2]; g.leaveCheck = o.guest.checks[3];
    g.souvenirCheck = o.guest.checks[4]; g.energyCheck = o.guest.checks[5];
    g.trashCheck = o.guest.checks[6]; g.viewCheck = o.guest.checks[8];
    g.chaseCheck = o.guest.chaseCheck;
    g.leaving = o.guest.leaving; g.sawEscape = o.guest.afraid; g.sick = o.guest.sick;
    std::copy(o.guest.items.begin(), o.guest.items.end(), g.inventory);
    for (size_t k = 0; k < an.typeList.size(); ++k)
      if (an.typeList[k].nameId == o.guest.favourite) g.favourite = static_cast<int>(k);
    for (uint32_t seen : o.guest.seen)
      if (world.placedObjects.byId(static_cast<int>(seen))) g.visited.insert(static_cast<int>(seen));
    g.happiness = static_cast<float>(std::clamp(o.guest.happiness, -100, 100));
    g.hunger = static_cast<float>(std::clamp(o.guest.hunger, 0, 100));
    g.thirst = static_cast<float>(std::clamp(o.guest.thirst, 0, 100));
    g.tired = static_cast<float>(std::clamp(o.guest.energy, 0, 100));
    g.bathroom = static_cast<float>(std::clamp(o.guest.bathroom, 0, 100));
    for (size_t k = 0; k < 4 && k < o.colours.size(); ++k)
      g.colours[k] = o.colours[k];
    g.playFor = 0.5f;
    gs.nextId = std::max(gs.nextId, g.id + 1);
    gs.list.push_back(std::move(g));
    guestCount++;
  }
  world.reindex();
  SDL_Log("[Save] the original's saved zoo: %d habitats named, %d piles, %d animals, %d staff, %d guests (%d couldn't be put back)",
          named, piles, animals, staffCount, guestCount, missed);
  return true;
}

std::string SaveGame::checkOriginalImport(World &world) {
  const ZooReader &z = world.zooReader;
  if (!z.isExact())
    return "Load an original saved zoo in the Test Zoo first.";
  int passed = 0, total = 0;
  std::string out;
  auto check = [&](const std::string &name, bool ok) {
    ++total;
    if (ok) ++passed;
    out += std::string(ok ? "PASS " : "FAIL ") + name + "\n";
  };
  const Guests &gs = world.guests;
  int expected = 0, maxId = 0;
  std::set<int> ids;
  for (const auto &o : z.getObjects()) {
    if (!o.decoded || !o.hasGuest) continue;
    ++expected;
    const auto *g = gs.guest(static_cast<int>(o.id));
    check("guest " + o.name + " restored with original id", g != nullptr);
    if (!g) continue;
    maxId = std::max(maxId, g->id);
    ids.insert(g->id);
    check(o.name + " kind and name", gs.typeList[g->type].key == (o.typeName.empty() ? o.subClass : o.typeName) && g->name == o.name);
    check(o.name + " position", g->x == o.x / 64.0f && g->y == o.y / 64.0f);
    check(o.name + " five status values",
          g->happiness == std::clamp(o.guest.happiness, -100, 100) &&
          g->hunger == std::clamp(o.guest.hunger, 0, 100) &&
          g->thirst == std::clamp(o.guest.thirst, 0, 100) &&
          g->tired == std::clamp(o.guest.energy, 0, 100) &&
          g->bathroom == std::clamp(o.guest.bathroom, 0, 100));
    bool colours = true;
    for (size_t k = 0; k < 4 && k < o.colours.size(); ++k)
      colours = colours && g->colours[k] == o.colours[k];
    check(o.name + " saved clothing colours", colours);
    check(o.name + " inventory and leaving state", std::equal(o.guest.items.begin(), o.guest.items.end(), g->inventory) &&
          g->leaving == o.guest.leaving && g->sick == o.guest.sick);
    check(o.name + " original arrival date", std::abs((gs.clock - g->arrived) - originalElapsed(z, o.guest.arrived)) < 0.0001);
    check(o.name + " saved need/check timers", g->hungerCheck == o.guest.checks[0] &&
          g->thirstCheck == o.guest.checks[1] && g->bathroomCheck == o.guest.checks[2] &&
          g->leaveCheck == o.guest.checks[3] && g->viewCheck == o.guest.checks[8]);
    if (o.guest.favourite)
      check(o.name + " favourite species", g->favourite >= 0 && gs.typeList.size() > 0 &&
            world.animals.typeList[g->favourite].nameId == o.guest.favourite);
  }
  check("every original guest restored exactly once",
        static_cast<int>(gs.list.size()) == expected && static_cast<int>(ids.size()) == expected);
  check("future guest ids cannot collide", gs.nextId > maxId);
  check("restored guests are not charged admission again", gs.income == 0 && gs.arrivals == 0);
  for (const auto &o : z.getObjects()) {
    if (!o.decoded || !o.hasAnimal) continue;
    const Animals::Member *found = nullptr;
    for (const auto &m : world.animals.members())
      if (m.name == o.name) found = &m;
    check("animal " + o.name + " age category",
          found && found->baby == (o.typeName == "y"));
    check("animal " + o.name + " original identity/status", found && found->id == static_cast<int>(o.id) &&
          found->sick == o.animal.sick && found->rampaging == o.animal.rampaging &&
          found->dirty == o.animal.dung && found->grow == o.animal.grow && found->ready == o.animal.ready);
    if (o.animal.hasLife) check("animal " + o.name + " remaining lifetime", found && found->life == o.animal.life);
    if (o.animal.egg) check("animal " + o.name + " hatch timer", found && found->egg && found->hatchIn == o.animal.hatchIn);
  }
  // A saved damaged fence must not become new, and a saved pump must
  // become serviceable equipment rather than a decorative sprite.
  ZooReader fixture = z;
  auto &records = const_cast<std::vector<ZooReader::ZooObject> &>(fixture.getObjects());
  records.erase(std::remove_if(records.begin(), records.end(),
                [](const auto &o) { return o.className == "filter"; }), records.end());
  for (auto &o : records) if (o.hasCondition) o.life = 1;
  WorldMap ground;
  ground.makeBlank(z.getMapWidth(), z.getMapHeight(), 0);
  Fences imported;
  imported.loadTypes(world.resourceManager);
  imported.load(fixture, ground);
  imported.exhibitList.clear();
  imported.made = 0;
  imported.updateExhibits(ground, 1, 0, 1, false);
  auto markHabitats = [&](Fences &fn) {
    for (auto &ex : fn.exhibitList) {
      bool saved = false;
      for (const auto &habitat : z.getHabitats())
        saved = saved || ex.tiles.count({habitat.x, habitat.y}) != 0;
      ex.pocket = !saved; // ordinary zoo grounds are not an exhibit
      ex.named = saved;
    }
  };
  markHabitats(imported);
  bool condition = true;
  int fencesChecked = 0;
  for (const auto &[edge, piece] : imported.pieces()) {
    condition = condition && piece.life == 1;
    ++fencesChecked;
  }
  check("saved fence wear is retained", fencesChecked > 0 && condition);
  int pumpX = -1, pumpY = -1;
  imported.selectFilter("scenery/other/filter1.ai");
  for (int y = 0; y < ground.height && pumpX < 0; ++y)
    for (int x = 0; x < ground.width && pumpX < 0; ++x)
      if (imported.filterFit(x, y, ground) == Fences::Fit::Ok) pumpX = x, pumpY = y;
  check("filter import fixture has a valid tank-side tile", pumpX >= 0);

  if (pumpX >= 0) {
    ZooReader::ZooObject pump;
    pump.className = "filter"; pump.subClass = "other"; pump.typeName = "filter1";
    pump.decoded = true; pump.filterHealth = 1; pump.name = "Imported worn filter";
    pump.x = pumpX * 64 + 32; pump.y = pumpY * 64 + 32;
    records.push_back(pump);
    WorldMap pumpGround;
    pumpGround.makeBlank(z.getMapWidth(), z.getMapHeight(), 0);
    imported.load(fixture, pumpGround);
    imported.exhibitList.clear();
    imported.made = 0;
    imported.updateExhibits(pumpGround, 1, 0, 1, false);
    markHabitats(imported);
    restoreOriginalFilters(fixture, imported, pumpGround);
    check("original filter becomes live equipment", imported.filters().size() == 1);
    if (!imported.filters().empty()) {
      const auto &filter = imported.filters().front();
      check("filter centre converts to correct grid tile", filter.x == pumpX && filter.y == pumpY);
      check("filter health and name retained", filter.health == 1 && filter.name == pump.name);
      if (auto *tank = imported.exhibit(filter.tank)) tank->water = 1;
      check("imported worn filter has decayed running state",
            imported.filterState(filter) == Fences::FilterState::Decayed);
    }
  }
  // Native world snapshots must load over a different-size stock/blank
  // bootstrap, without needing the original zoo file or its old dimensions.
  Out snapshot;
  putWorld(&snapshot, world);
  World bootstrap(world.resourceManager);
  const bool blank = bootstrap.loadBlank(32, 32);
  In snapshotIn{snapshot.b};
  const bool restored = blank && getWorld(&snapshotIn, bootstrap, kVersion);
  check("native world snapshot loads over a different-size bootstrap", restored);
  if (restored) {
    check("native snapshot keeps original dimensions", bootstrap.worldMap.width == world.worldMap.width && bootstrap.worldMap.height == world.worldMap.height);
    check("native snapshot keeps objects and boundary pieces", bootstrap.placedObjects.objects().size() == world.placedObjects.objects().size() && bootstrap.fences.pieces().size() == world.fences.pieces().size());
    check("native snapshot keeps inside/outside zoo boundaries", bootstrap.fences.inside == world.fences.inside);
  }

  // Exercise the real full-file v12 writer/reader, including non-default
  // values that the short marine fixtures may not happen to contain.
  if (restored) {
    UiGameScreen testHud(world.resourceManager, "", "");
    ZooSim testBooks; testBooks.start(12345, 73);
    restoreOriginalBooks(world, testBooks);
    const auto savedMonths = testBooks.months;
    const auto savedAnnual = testBooks.importedAnnual;
    const auto savedTotals = testBooks.importedTotals;
    check("original monthly history ends at its saved calendar month",
          !z.hasGame() || (!savedMonths.empty() && savedMonths.back().month == z.getDate()[1]-1 &&
                          savedMonths.back().year == z.getDate()[0]-2000));
    if (z.hasGame() && z.getBooks().monthly.size() >= 31) {
      const int month = z.getDate()[1]-1;
      const auto &rows = z.getBooks().monthly;
      const auto &lines = savedMonths.back().lines;
      check("original finance income rows match the original panel",
            lines[ZooSim::Admissions] == rows[4][month] &&
            lines[ZooSim::AdmissionsIncome] == rows[5][month] &&
            lines[ZooSim::Concessions] == double(rows[6][month])+rows[7][month] &&
            lines[ZooSim::ShowIncome] == rows[30][month]);
      check("original finance expenses use the original panel signs",
            lines[ZooSim::Construction] == -double(rows[3][month]) &&
            lines[ZooSim::AnimalPurchase] == -double(rows[2][month]) &&
            lines[ZooSim::Wages] == -double(rows[11][month])-rows[12][month]-rows[13][month] &&
            lines[ZooSim::Research] == -double(rows[19][month]) &&
            lines[ZooSim::Marketing] == -double(rows[29][month]));
    }
    const auto previousResearch = Research::get().branches();
    const auto previousAwards = goals ? goals->awards : std::vector<int>{};
    restoreOriginalManagers(world, testHud);
    for (const auto &record : z.getResearch()) {
      bool found = false, matches = true;
      for (const auto &branch : Research::get().branches()) {
        if (record.kind == 0 && branch.nameId == record.id) {
          found = true;
          matches = matches && branch.fundingLevel == std::clamp(static_cast<int>(record.value), 0,
                                   std::max(0, static_cast<int>(branch.funding.size()) - 1));
        }
        for (size_t ci = 0; ci < branch.categories.size(); ++ci) {
          const auto &category = branch.categories[ci];
          if (record.kind == 1 && category.nameId == record.id) {
            found = true; matches = matches && branch.enabled[ci] == (record.value != 0);
          }
          for (const auto &program : category.programs)
            if (record.kind == 2 && program.nameId == record.id) {
              float progress = 0; std::memcpy(&progress, &record.value, sizeof progress);
              found = true;
              matches = matches && program.progress == progress && program.done == (progress >= program.cost);
            }
        }
      }
      check("research record " + std::to_string(record.kind) + "/" + std::to_string(record.id),
            found && matches);
    }
    // Deliberately exercise disabled categories and partial progress even in
    // a fixture whose original manager was left at its defaults.
    for (auto &branch : Research::get().branches()) {
      if (!branch.enabled.empty()) branch.enabled[0] = false;
      branch.fundingLevel = std::min(1, std::max(0, static_cast<int>(branch.funding.size()) - 1));
      for (auto &category : branch.categories)
        for (auto &program : category.programs)
          if (program.cost > 1 && !program.done) { program.progress = 0.5f; break; }
    }
    const auto savedResearch = Research::get().branches();
    const auto temp = std::filesystem::temp_directory_path() /
        ("zt1-native-import-check-" + std::to_string(SDL_GetTicks64()) + ".zt1save");
    bool written = save(temp.string(), world, testBooks, testHud, "freeform/ff01.scn");
    bool loaded = written && load(temp.string(), bootstrap, testBooks, testHud);
    check("full native v12 file round-trip", loaded);
    if (loaded) {
      bool history = savedMonths.size() == testBooks.months.size();
      for (size_t m = 0; m < savedMonths.size() && history; ++m)
        history = savedMonths[m].month == testBooks.months[m].month &&
                  savedMonths[m].year == testBooks.months[m].year && savedMonths[m].lines == testBooks.months[m].lines;
      check("native save retains imported monthly and annual books",
            history && savedAnnual == testBooks.importedAnnual && savedTotals == testBooks.importedTotals);
      bool researchEqual = savedResearch.size() == Research::get().branches().size();
      for (size_t b = 0; b < savedResearch.size() && researchEqual; ++b) {
        const auto &before = savedResearch[b], &after = Research::get().branches()[b];
        researchEqual = before.enabled == after.enabled && before.fundingLevel == after.fundingLevel &&
                        before.currentCategory == after.currentCategory && before.currentProgram == after.currentProgram;
        for (size_t c = 0; c < before.categories.size() && researchEqual; ++c)
          for (size_t p = 0; p < before.categories[c].programs.size(); ++p)
            researchEqual = researchEqual &&
                before.categories[c].programs[p].progress == after.categories[c].programs[p].progress &&
                before.categories[c].programs[p].done == after.categories[c].programs[p].done;
      }
      check("native research funding, categories and progress", researchEqual);
      for (const auto &g : world.guests.list) {
        const auto *copy = bootstrap.guests.guest(g.id);
        check("native guest state " + std::to_string(g.id), copy &&
              copy->arrived == g.arrived && copy->favourite == g.favourite &&
              copy->leaving == g.leaving && copy->sick == g.sick &&
              std::equal(std::begin(g.inventory), std::end(g.inventory), copy->inventory) &&
              copy->hungerCheck == g.hungerCheck && copy->visited == g.visited);
      }
      for (const auto &a : world.animals.list) {
        const auto *copy = bootstrap.animals.member(a.id);
        check("native animal state " + std::to_string(a.id), copy && copy->life == a.life &&
              copy->grow == a.grow && copy->ready == a.ready && copy->egg == a.egg &&
              copy->hatchIn == a.hatchIn && copy->sick == a.sick &&
              copy->rampaging == a.rampaging && copy->reproductionCheck == a.reproductionCheck);
      }
      for (const auto &s : world.staff.list) {
        const auto *copy = bootstrap.staff.member(s.id);
        check("native staff state " + std::to_string(s.id), copy && copy->female == s.female &&
              copy->hair == s.hair && copy->skin == s.skin && copy->exhibits == s.exhibits &&
              std::equal(std::begin(s.duties), std::end(s.duties), copy->duties) &&
              copy->workCheck == s.workCheck);
      }
      std::set<int> objectIds;
      bool ids = true;
      for (const auto &object : world.placedObjects.objects())
        ids = ids && object.id > 0 && objectIds.insert(object.id).second &&
              bootstrap.placedObjects.byId(object.id);
      check("original object identities remain unique through native save", ids);
    }
    Research::get().branches() = previousResearch;
    if (goals) goals->awards = previousAwards;
    std::error_code ec; if (written) std::filesystem::remove(temp, ec);
  }
  ZooReader probe = z;
  AssetBuffer invalid;
  check("empty file is rejected", !probe.load(invalid));
  check("failed reload clears previous exact/game/entity state",
        !probe.isExact() && !probe.hasGame() && probe.getObjects().empty() && probe.getHabitats().empty());
  out += std::to_string(passed) + "/" + std::to_string(total) + " original-save checks passed\n";
  return out;
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
  if (i.bad || w < 1 || h < 1 || w > 512 || h > 512)
    return false;
  if (w != map.width || h != map.height)
    map.makeBlank(w, h, 0);
  map.minHeight = INT32_MAX;
  map.maxHeight = INT32_MIN;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      MapTile &t = map.tiles[y][x];
      i.get(t.height);
      i.get(t.cornerHeight);
      i.get(t.terrainType);
      i.get(t.edgeBits);
      i.get(t.substrate);
      for (int corner : t.cornerHeight) {
        map.minHeight = std::min(map.minHeight, corner);
        map.maxHeight = std::max(map.maxHeight, corner);
      }
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
  f.mapW = w;
  f.mapH = h;
  f.mapRef = &map;
  f.mapEdit = &map;
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
  if (!i.bad) world.refindInside();
  return !i.bad;
}

bool SaveGame::save(const std::string &path, World &world, ZooSim &sim, UiGameScreen &hud,
                    const std::string &mapPath) {
  Out o;
  o.raw(kMagic);
  o.raw(kVersion);
  // The world snapshot below contains all terrain, paths, boundaries and
  // placed objects. A converted original save must not depend on the user's
  // original .zoo still existing at the same absolute file path.
  o.str(isOriginal(mapPath) ? "freeform/ff01.scn" : mapPath);
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
  o.raw(static_cast<uint32_t>(sim.importedAnnual.size()));
  for (const auto &row : sim.importedAnnual) {
    o.raw(static_cast<uint32_t>(row.size()));
    for (float value : row) o.raw(value);
  }
  o.raw(static_cast<uint32_t>(sim.importedTotals.size()));
  for (float value : sim.importedTotals) o.raw(value);
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
    for (size_t ci = 0; ci < b.categories.size(); ++ci) {
      const ResearchCategory &c = b.categories[ci];
      o.raw(ci >= b.enabled.size() || b.enabled[ci]);
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
    o.raw(m.tranquilised);
    o.raw(m.lastAte);
    o.raw(m.lastSlept);
    o.raw(m.life);
    o.raw(m.ready);
    o.raw(m.grow);
    o.raw(m.fx); o.raw(m.fy); o.raw(m.egg); o.raw(m.hatchIn);
    o.raw(m.rampaging); o.raw(m.rampageTicks); o.raw(m.dying); o.raw(m.dieAfter); o.raw(m.sleeping);
    for (int value : {m.hungerCheck, m.healthCheck, m.energyCheck, m.otherCheck, m.captivityCheck,
                      m.habitatCheck, m.boredCheck, m.socialCheck, m.reproductionCheck,
                      m.preyCheck, m.huntCheck}) o.raw(value);
    o.raw(m.swimZ); o.raw(m.swimTo);
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
    o.raw(m.fx); o.raw(m.fy); o.raw(m.workCheck); o.raw(m.crowdClock);
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
    o.raw(g.fx); o.raw(g.fy); o.raw(g.inventory); o.raw(g.sick); o.raw(g.sawEscape); o.raw(g.souvenir);
    for (int value : {g.hungerCheck, g.thirstCheck, g.bathroomCheck, g.leaveCheck, g.souvenirCheck,
                      g.energyCheck, g.trashCheck, g.viewCheck, g.chaseCheck}) o.raw(value);
    o.raw(static_cast<uint32_t>(g.visited.size()));
    for (int value : g.visited) o.raw(value);
    o.raw(static_cast<uint32_t>(g.thoughts.size()));
    for (const auto &thought : g.thoughts) o.str(thought);
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
  o.str(world.shows.snapshot());

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
  sim.importedAnnual.clear(); sim.importedTotals.clear();
  if (version >= 12) {
    const auto nr = i.raw<uint32_t>();
    if (nr > 1000) return false;
    sim.importedAnnual.resize(nr);
    for (auto &row : sim.importedAnnual) {
      const auto nc = i.raw<uint32_t>();
      if (nc > 1000) return false;
      row.resize(nc);
      for (auto &value : row) i.get(value);
    }
    const auto nt = i.raw<uint32_t>();
    if (nt > 1000) return false;
    sim.importedTotals.resize(nt);
    for (auto &value : sim.importedTotals) i.get(value);
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
      const bool enabled = version >= 12 ? i.raw<bool>() : true;
      if (c < br.enabled.size()) br.enabled[c] = enabled;
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
    if (version >= 11) i.get(m.tranquilised);
    i.get(m.lastAte);
    i.get(m.lastSlept);
    i.get(m.life);
    i.get(m.ready);
    i.get(m.grow);
    if (version >= 12) {
      i.get(m.fx); i.get(m.fy); i.get(m.egg); i.get(m.hatchIn);
      i.get(m.rampaging); i.get(m.rampageTicks); i.get(m.dying); i.get(m.dieAfter); i.get(m.sleeping);
      for (int *value : {&m.hungerCheck, &m.healthCheck, &m.energyCheck, &m.otherCheck, &m.captivityCheck,
                         &m.habitatCheck, &m.boredCheck, &m.socialCheck, &m.reproductionCheck,
                         &m.preyCheck, &m.huntCheck}) i.get(*value);
      i.get(m.swimZ); i.get(m.swimTo);
    }
    m.exhibit = f.exhibitAt(static_cast<int>(m.x), static_cast<int>(m.y));
    m.food = 100 - m.hunger;
    if (m.type >= 0) {
      if (m.egg) an.play(m, "egg_idle", Animals::Member::Mode::Loop, 3600.0f);
      else if (m.dying && m.dieAfter && an.set(m, "bdie")) an.pushSet(m, "bdie");
      an.list.push_back(m);
    }
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
    if (version >= 12) { i.get(m.fx); i.get(m.fy); i.get(m.workCheck); i.get(m.crowdClock); }
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
    if (version >= 12) {
      i.get(g.fx); i.get(g.fy); i.get(g.inventory); i.get(g.sick); i.get(g.sawEscape); i.get(g.souvenir);
      for (int *value : {&g.hungerCheck, &g.thirstCheck, &g.bathroomCheck, &g.leaveCheck, &g.souvenirCheck,
                         &g.energyCheck, &g.trashCheck, &g.viewCheck, &g.chaseCheck}) i.get(*value);
      uint32_t nv = i.raw<uint32_t>(); if (nv > 100000) return false;
      while (nv-- && !i.bad) g.visited.insert(i.raw<int>());
      uint32_t nt = i.raw<uint32_t>(); if (nt > 10) return false;
      while (nt-- && !i.bad) g.thoughts.push_back(i.str());
    }
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
  if(version>=10 && !i.bad) {auto shows=i.str();if(!i.bad && !world.shows.restore(shows))i.bad=true;}
  else {world.shows.clear();world.shows.refresh();}
  if (!i.bad) {
    gs.entranceX = gs.entranceY = -1;
    world.findEntrance();
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
  o.str(world.shows.snapshot(false));
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
  if(version>=10 && !i.bad) {auto shows=i.str();if(!i.bad && !world.shows.restore(shows))i.bad=true;}
  else {world.shows.clear();world.shows.refresh();}
  world.reindex();
  return !i.bad;
}

Goals *SaveGame::goals = nullptr;
