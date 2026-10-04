#include <cstdlib>
#include "Fences.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <queue>

#include "Animation.hpp"
#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include "ZooReader.hpp"

namespace {
// tanks.cfg
constexpr int kTankSink = 4;       // initialSink
constexpr int kTankHeight = 5;     // initialHeight
constexpr int kTankTerrain = 2;    // tankTerrain (sand)
constexpr float kFillSeconds = 8.0f;
constexpr int kTankMaxHeight = 20;     // maximumTankHeight
constexpr int kWallPriceDivisor = 5;   // wallHeightPriceDivisor
constexpr int kBaseStepPerTile = 8;    // measured: a 2 x 2 tank's base step $32
constexpr double kSaltWaterPrice = 1.5;  // saltWater
constexpr double kFreshWaterPrice = 1.0; // freshWater
constexpr float kPurityDecaySeconds = 30.0f; // waterPurityDecayTime 30000 ms

std::string fileStem(const std::string &file) {
  size_t slash = file.find_last_of('/'), dot = file.find_last_of('.');
  return file.substr(slash + 1, dot - slash - 1);
}
} // namespace

// ----------------------------------------------------------------------------
// Fence types: every fence and tank wall in the registry
// ----------------------------------------------------------------------------
void Fences::loadTypes(ResourceManager *rm) {
  this->rm = rm;
  if (!this->typeList.empty())
    return;
  std::map<std::string, Animation *> loaded;
  auto anim = [&](const std::string &path) -> Animation * {
    if (path.empty())
      return nullptr;
    auto it = loaded.find(path);
    if (it != loaded.end())
      return it->second;
    Animation *a = rm->hasResource(path + ".ani") ? rm->getAnimation(path) : nullptr;
    loaded[path] = a;
    return a;
  };
  ItemCatalog &catalog = ItemCatalog::get();
  catalog.load(rm);
  for (const CatalogItem &item : catalog.all()) {
    if (item.file.rfind("fences/", 0) != 0)
      continue;
    IniReader *ai = rm->getIniReader(item.file);
    if (!ai)
      continue;
    FenceType t;
    t.file = item.file;
    t.key = fileStem(item.file);
    t.name = item.name;
    t.cost = item.cost;
    t.height = item.height;
    t.gateCost = ai->getInt("g/characteristics/integers", "cpurchasecost", t.cost);
    t.tank = item.registrySection == "tankwall";
    t.zooWall = item.members.count("zoowall") > 0;
    // An animation value is a path (fences/tank1/g/gate/gate.ani) or a
    // name under fences/<key>/<sub>/
    auto artOf = [&](const std::string &sub, const std::string &key) -> Animation * {
      std::string v = ai->get(sub + "/animations", key);
      if (v.empty())
        v = ai->get("animations", key);
      if (v.empty())
        return nullptr;
      v = Utils::string_to_lower(v);
      if (v.find('/') != std::string::npos) {
        size_t dot = v.rfind(".ani");
        return anim(dot == std::string::npos ? v : v.substr(0, dot));
      }
      return anim("fences/" + t.key + "/" + sub + "/" + v + "/" + v);
    };
    const char *subs[2] = {"f", "g"};
    for (int s = 0; s < 2; s++) {
      t.art[s][0] = artOf(subs[s], "idle");
      t.art[s][1] = artOf(subs[s], "idle30p");
      t.art[s][2] = artOf(subs[s], "idle30n");
    }
    if (!t.art[1][0]) // no gate art: the fence's own
      for (int i = 0; i < 3; i++)
        t.art[1][i] = t.art[0][i];
    if (t.tank) {
      const char *keys[4][3] = {{"blidle", "mlidle", "tlidle"},
                                {"bmidle", "mmidle", "tmidle"},
                                {"bridle", "mridle", "tridle"},
                                {"blridle", "mlridle", "tlridle"}};
      for (int c = 0; c < 4; c++)
        for (int l = 0; l < 3; l++)
          t.tankArt[c][l] = artOf("f", keys[c][l]);
      t.tankLow[0] = artOf("f", "tblidle");
      t.tankLow[1] = artOf("f", "tbmidle");
      t.tankLow[2] = artOf("f", "tbridle");
      t.tankLow[3] = t.tankLow[1];
      t.platform = artOf("g", "platform");
      t.ladder = artOf("g", "ladder");
    }
    const std::string fints = "f/characteristics/integers";
    t.life = ai->getInt(fints, "clife", 10);
    t.decayedLife = ai->getInt(fints, "cdecayedlife", 5);
    t.decayDelta = ai->getInt(fints, "cdecaydelta", 25);
    t.indestructible = ai->getInt(fints, "cindestructible", 0) != 0 || t.zooWall || t.tank;
    t.det[0] = artOf("f", "det");
    t.det[1] = artOf("f", "det30p");
    t.det[2] = artOf("f", "det30n");
    t.broke[0] = artOf("f", "broke");
    t.broke[1] = artOf("f", "broke30p");
    t.broke[2] = artOf("f", "broke30n");
    delete ai;
    if (t.art[0][0])
      this->typeList.push_back(t);
  }
  this->water = nullptr;
  // The tank filter, listed with the fences
  for (const CatalogItem &item : catalog.all()) {
    if (item.file != "scenery/other/filter1.ai")
      continue;
    IniReader *ai = rm->getIniReader(item.file);
    if (!ai)
      break;
    FilterType &f = this->filterKind;
    const std::string ints = "characteristics/integers";
    f.file = item.file;
    f.cost = item.cost;
    // (in half tiles: the filter's 2 x 2 is a tile)
    f.footprintX = std::max(1, (ai->getInt(ints, "cfootprintx", 2) + 1) / 2);
    f.footprintY = std::max(1, (ai->getInt(ints, "cfootprinty", 2) + 1) / 2);
    f.startHealth = ai->getInt(ints, "cstartinghealth", 10);
    f.decayedHealth = ai->getInt(ints, "cdecayedhealth", 5);
    f.decayTime = static_cast<float>(ai->getInt(ints, "cdecaytime", 200));
    f.filterDelay = static_cast<float>(ai->getInt(ints, "cfilterdelay", 50));
    f.upkeep = ai->getInt(ints, "cfilterupkeep", 50);
    f.clean = ai->getInt(ints, "cfiltercleanamount", 10);
    f.decayedClean = ai->getInt(ints, "cfilterdecayedcleanamount", 5);
    auto art = [&](const std::string &key) -> Animation * {
      std::string v = Utils::string_to_lower(ai->get("animations", key));
      size_t dot = v.rfind(".ani");
      return v.empty() ? nullptr : anim(dot == std::string::npos ? v : v.substr(0, dot));
    };
    f.idle = art("idle");
    f.decayed = art("decayed");
    f.off = art("off");
    delete ai;
    break;
  }
  SDL_Log("[Fences] %zu fence types", this->typeList.size());
}

int Fences::typeIndex(const std::string &key) const {
  for (size_t i = 0; i < this->typeList.size(); i++)
    if (this->typeList[i].key == key)
      return static_cast<int>(i);
  return -1;
}

void Fences::clear() {
  this->edges.clear();
  this->exhibitList.clear();
  this->filterList.clear();
  this->upkeepOwed = 0;
  this->preview.clear();
  this->placed = 0;
  this->made = 0;
}

// ----------------------------------------------------------------------------
// The map's fences
// ----------------------------------------------------------------------------
void Fences::load(const ZooReader &reader, WorldMap &map) {
  this->clear();
  for (const ZooReader::ZooObject &obj : reader.getObjects()) {
    if (obj.className != "fences")
      continue;
    int type = this->typeIndex(obj.subClass);
    if (type < 0)
      continue;
    int facing = 4;
    if (obj.payload.size() >= 20) {
      uint32_t f = 0;
      std::memcpy(&f, obj.payload.data() + 16, 4);
      facing = static_cast<int>(f & 6);
    }
    Edge e;
    Piece p;
    p.type = type;
    p.gate = obj.typeName == "g";
    p.facing = facing;
    p.ownerX = obj.x >= 0 ? obj.x / 64 : (obj.x - 63) / 64;
    p.ownerY = obj.y >= 0 ? obj.y / 64 : (obj.y - 63) / 64;
    if (facing == 0 || facing == 4) {
      e.alongX = true;
      e.x = p.ownerX;
      e.y = static_cast<int>(std::lround(obj.y / 64.0));
    } else {
      e.alongX = false;
      e.x = static_cast<int>(std::lround(obj.x / 64.0));
      e.y = p.ownerY;
    }
    p.order = this->placed++;
    this->edges[e] = p;
  }
  // Ground the map already shuts off (pockets behind the zoo's walls) isn't
  // an exhibit: remembered as it is
  this->updateExhibits(map, 1, 0, 1, true);
  int insideCount = 0;
  (void)insideCount;

  // Inside the main zoo wall: everything the map's edge can't reach without
  // crossing a zoo wall (fences of subclass zoowall)
  this->mapRef = &map;
  this->mapEdit = &map;
  this->mapW = map.getWidth();
  this->mapH = map.getHeight();
  this->inside.assign(static_cast<size_t>(mapW) * mapH, 1);
  // The entrance is a building in the wall's gap: its footprint (from
  // scenery/other/<type>.ai, turned with its facing) closes the gap
  std::vector<uint8_t> gate(static_cast<size_t>(mapW) * mapH, 0);
  for (const ZooReader::ZooObject &obj : reader.getObjects()) {
    const std::string &t = obj.typeName;
    if (obj.className != "building" || t.size() < 4 || t.compare(t.size() - 4, 4, "gate") != 0)
      continue;
    int fx = 6, fy = 2;
    if (IniReader *ai = this->rm ? this->rm->getIniReader("scenery/other/" + t + ".ai") : nullptr) {
      fx = ai->getInt("characteristics/integers", "cfootprintx", fx);
      fy = ai->getInt("characteristics/integers", "cfootprinty", fy);
      delete ai;
    }
    // Footprints are in half tiles (a bathroom's 2 x 2 is a tile)
    fx = (fx + 1) / 2;
    fy = (fy + 1) / 2;
    uint32_t facing = 0;
    if (obj.payload.size() >= 20)
      std::memcpy(&facing, obj.payload.data() + 16, 4);
    if ((facing & 6) == 2 || (facing & 6) == 6)
      std::swap(fx, fy);
    float cx = obj.x / 64.0f, cy = obj.y / 64.0f;
    for (int y = static_cast<int>(std::floor(cy - fy / 2.0f)); y < cy + fy / 2.0f; y++)
      for (int x = static_cast<int>(std::floor(cx - fx / 2.0f)); x < cx + fx / 2.0f; x++)
        if (x >= 0 && y >= 0 && x < mapW && y < mapH)
          gate[y * mapW + x] = 1;
  }
  this->building = gate;
  std::queue<std::pair<int, int>> q;
  for (int y = 0; y < mapH; y++)
    for (int x = 0; x < mapW; x++)
      if (x == 0 || y == 0 || x == mapW - 1 || y == mapH - 1) {
        this->inside[y * mapW + x] = 0;
        q.push({x, y});
      }
  auto zooWall = [&](const Edge &e) {
    const Piece *p = this->at(e);
    return p && this->typeList[p->type].zooWall;
  };
  while (!q.empty()) {
    auto [x, y] = q.front();
    q.pop();
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &dd : d) {
      int nx = x + dd[0], ny = y + dd[1];
      if (nx < 0 || ny < 0 || nx >= mapW || ny >= mapH || !this->inside[ny * mapW + nx] ||
          gate[ny * mapW + nx])
        continue;
      Edge e;
      if (nx != x) {
        e.alongX = false;
        e.x = std::max(x, nx);
        e.y = y;
      } else {
        e.alongX = true;
        e.x = x;
        e.y = std::max(y, ny);
      }
      if (zooWall(e))
        continue;
      this->inside[ny * mapW + nx] = 0;
      q.push({nx, ny});
    }
  }
  for (uint8_t v : this->inside)
    insideCount += v;
  SDL_Log("[Fences] %d of %d tiles inside the zoo wall", insideCount, mapW * mapH);
  SDL_Log("[Fences] %zu fence pieces from the map", this->edges.size());
}

const Fences::Piece *Fences::at(const Edge &e) const {
  auto it = this->edges.find(e);
  return it == this->edges.end() ? nullptr : &it->second;
}

// New pieces stand on the side of the tile above a horizontal edge (facing
// south) or right of a vertical one (facing west), as the map's do
Fences::Piece Fences::ownerFor(const Edge &e, const WorldMap &map) {
  Piece p;
  if (e.alongX) {
    p.ownerX = e.x;
    p.ownerY = e.y > 0 ? e.y - 1 : e.y;
    p.facing = e.y > 0 ? 4 : 0;
  } else {
    p.ownerY = e.y;
    p.ownerX = e.x < map.getWidth() ? e.x : e.x - 1;
    p.facing = e.x < map.getWidth() ? 6 : 2;
  }
  return p;
}

bool Fences::insideZoo(int x, int y) const {
  if (x < 0 || y < 0 || x >= mapW || y >= mapH || this->inside.empty())
    return false;
  return this->inside[y * mapW + x] != 0;
}

bool Fences::outsideZooWall(const Edge &e, const WorldMap &map) const {
  int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
  for (auto [tx, ty] : {std::make_pair(ax, ay), std::make_pair(e.x, e.y)}) {
    const MapTile *t = map.getTile(tx, ty);
    if (t && t->terrainType != 9 && t->terrainType != 10 && !this->insideZoo(tx, ty))
      return true;
  }
  return false;
}

bool Fences::canPlace(const Edge &e, const WorldMap &map) const {
  return this->fit(e, map) == Fit::Ok;
}

Fences::Fit Fences::tileFit(int x, int y, const WorldMap &map) const {
  const MapTile *t = map.getTile(x, y);
  if (!t || t->terrainType == 9 || t->terrainType == 10)
    return Fit::Outside;
  if (x >= 0 && y >= 0 && x < this->mapW && y < this->mapH &&
      !this->building.empty() && this->building[y * this->mapW + x])
    return Fit::InTheWay;
  if (!this->insideZoo(x, y))
    return Fit::Outside;
  return Fit::Ok;
}

Fences::Fit Fences::fit(const Edge &e, const WorldMap &map) const {
  const int W = map.getWidth(), H = map.getHeight();
  if (e.alongX) {
    if (e.x < 0 || e.x >= W || e.y < 1 || e.y > H - 1)
      return Fit::Outside;
  } else {
    if (e.y < 0 || e.y >= H || e.x < 1 || e.x > W - 1)
      return Fit::Outside;
  }
  // A fence there already
  if (this->edges.find(e) != this->edges.end())
    return Fit::InTheWay;
  // The tiles either side: inside the zoo, not water, no building
  int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
  Fit worst = Fit::Ok;
  for (auto [tx, ty] : {std::make_pair(ax, ay), std::make_pair(e.x, e.y)}) {
    Fit f = this->tileFit(tx, ty, map);
    if (f == Fit::Outside)
      return f;
    if (f == Fit::InTheWay)
      worst = f;
  }
  return worst;
}

void Fences::place(const Edge &e, int type, int drag, const WorldMap &map) {
  if (!this->canPlace(e, map))
    return;
  Piece p = ownerFor(e, map);
  p.type = type;
  p.life = static_cast<float>(this->typeList[type].life);
  p.order = this->placed++;
  p.drag = drag;
  this->edges[e] = p;
}

int Fences::exhibitAt(int x, int y) const {
  for (const Exhibit &ex : this->exhibitList)
    if (ex.named && ex.tiles.count({x, y}))
      return ex.id;
  return -1;
}

int Fences::exhibitOf(const Edge &e) const {
  // The tiles either side of it
  int a = e.alongX ? this->exhibitAt(e.x, e.y - 1) : this->exhibitAt(e.x - 1, e.y);
  return a >= 0 ? a : this->exhibitAt(e.x, e.y);
}

int Fences::tankOf(const Edge &e) const {
  const Piece *p = this->at(e);
  return p ? p->tank : -1;
}

static std::vector<Fences::Edge> boundaryOf(const Fences::Exhibit &ex);

void Fences::remove(const Edge &e, WorldMap &map) {
  auto it = this->edges.find(e);
  if (it == this->edges.end())
    return;
  // A tank's wall piece takes the whole tank with it (as the original:
  // every wall piece and the gate's platform go, the water too); the hole
  // it was sunk into stays
  int tank = it->second.tank;
  if (tank >= 0) {
    if (Exhibit *ex = this->exhibit(tank)) {
      this->drainTank(*ex);
      for (const Edge &w : boundaryOf(*ex)) {
        auto p = this->edges.find(w);
        if (p != this->edges.end() && (p->second.tank == tank || p->second.tank < 0) &&
            this->typeList[p->second.type].tank)
          this->edges.erase(p);
      }
    }
  }
  this->edges.erase(e);
  (void)map;
}

Fences::Exhibit *Fences::exhibit(int id) {
  for (Exhibit &x : this->exhibitList)
    if (x.id == id)
      return &x;
  return nullptr;
}

// ----------------------------------------------------------------------------
// Exhibits: ground fences shut off from the map's edge
// ----------------------------------------------------------------------------
std::vector<int> Fences::updateExhibits(WorldMap &map, int day, int month, int year,
                                        bool loading) {
  const int W = map.getWidth(), H = map.getHeight();
  std::vector<int> region(static_cast<size_t>(W) * H, -1);
  auto blocked = [&](int x, int y, int nx, int ny) {
    Edge e;
    if (nx != x) {
      e.alongX = false;
      e.x = std::max(x, nx);
      e.y = y;
    } else {
      e.alongX = true;
      e.x = x;
      e.y = std::max(y, ny);
    }
    return this->edges.count(e) > 0;
  };
  std::vector<std::set<std::pair<int, int>>> regions;
  std::vector<bool> open; // reaches the map's edge
  for (int sy = 0; sy < H; sy++)
    for (int sx = 0; sx < W; sx++) {
      if (region[sy * W + sx] >= 0)
        continue;
      int id = static_cast<int>(regions.size());
      regions.emplace_back();
      open.push_back(false);
      std::queue<std::pair<int, int>> q;
      q.push({sx, sy});
      region[sy * W + sx] = id;
      while (!q.empty()) {
        auto [x, y] = q.front();
        q.pop();
        regions[id].insert({x, y});
        if (x == 0 || y == 0 || x == W - 1 || y == H - 1)
          open[id] = true;
        const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto &dd : d) {
          int nx = x + dd[0], ny = y + dd[1];
          if (nx < 0 || ny < 0 || nx >= W || ny >= H || region[ny * W + nx] >= 0)
            continue;
          if (blocked(x, y, nx, ny))
            continue;
          region[ny * W + nx] = id;
          q.push({nx, ny});
        }
      }
    }

  // Exhibits whose ground is no longer shut off as it was are gone
  std::vector<int> created;
  std::vector<bool> taken(regions.size(), false);
  for (auto it = this->exhibitList.begin(); it != this->exhibitList.end();) {
    bool still = false;
    if (!it->tiles.empty()) {
      auto first = *it->tiles.begin();
      int id = region[first.second * W + first.first];
      still = !open[id] && regions[id] == it->tiles;
      if (still)
        taken[id] = true;
    }
    if (!still) {
      if (it->tank)
        this->drainTank(*it);
      it = this->exhibitList.erase(it);
    } else {
      ++it;
    }
  }
  // Newly shut off ground
  for (size_t id = 0; id < regions.size(); id++) {
    if (open[id] || taken[id])
      continue;
    Exhibit ex;
    ex.tiles = regions[id];
    if (loading) {
      ex.id = -static_cast<int>(id) - 1;
      ex.pocket = true;
      this->exhibitList.push_back(ex);
      continue;
    }
    ex.id = ++this->made;
    ex.constructedDay = day;
    ex.constructedMonth = month;
    ex.constructedYear = year;
    // A tank when every piece round it is a tank wall
    bool tank = true, any = false;
    for (auto [x, y] : ex.tiles) {
      const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto &dd : d) {
        int nx = x + dd[0], ny = y + dd[1];
        if (ex.tiles.count({nx, ny}))
          continue;
        Edge e;
        if (nx != x) {
          e.alongX = false;
          e.x = std::max(x, nx);
          e.y = y;
        } else {
          e.alongX = true;
          e.x = x;
          e.y = std::max(y, ny);
        }
        const Piece *p = this->at(e);
        if (!p)
          continue;
        any = true;
        if (!this->typeList[p->type].tank)
          tank = false;
      }
    }
    ex.tank = tank && any;
    this->exhibitList.push_back(ex);
    if (ex.tank)
      this->makeTank(this->exhibitList.back(), map);
    created.push_back(ex.id);
  }
  return created;
}

// The edges round an exhibit
static std::vector<Fences::Edge> boundaryOf(const Fences::Exhibit &ex) {
  std::vector<Fences::Edge> out;
  for (auto [x, y] : ex.tiles) {
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &dd : d) {
      int nx = x + dd[0], ny = y + dd[1];
      if (ex.tiles.count({nx, ny}))
        continue;
      Fences::Edge e;
      if (nx != x) {
        e.alongX = false;
        e.x = std::max(x, nx);
        e.y = y;
      } else {
        e.alongX = true;
        e.x = x;
        e.y = std::max(y, ny);
      }
      out.push_back(e);
    }
  }
  return out;
}

int Fences::finishExhibit(int id, const std::string &name, WorldMap &map) {
  (void)map;
  Exhibit *ex = this->exhibit(id);
  if (!ex)
    return 0;
  ex->name = name;
  ex->named = true;
  // The gate: in the middle of the first-laid stretch of the loop
  std::vector<std::pair<Edge, Piece *>> round;
  for (const Edge &e : boundaryOf(*ex)) {
    auto it = this->edges.find(e);
    if (it != this->edges.end())
      round.push_back({e, &it->second});
  }
  if (round.empty())
    return 0;
  int firstDrag = round.front().second->drag;
  for (auto &r : round)
    firstDrag = std::min(firstDrag, r.second->drag);
  std::vector<std::pair<Edge, Piece *>> stretch;
  for (auto &r : round)
    if (r.second->drag == firstDrag)
      stretch.push_back(r);
  std::sort(stretch.begin(), stretch.end(),
            [](const auto &a, const auto &b) { return a.second->order < b.second->order; });
  Piece *gate = stretch[stretch.size() / 2].second;
  if (gate->gate)
    return 0;
  gate->gate = true;
  return this->typeList[gate->type].gateCost;
}

// ----------------------------------------------------------------------------
// Tanks
// ----------------------------------------------------------------------------
void Fences::makeTank(Exhibit &ex, WorldMap &map) {
  int ground = INT32_MIN;
  for (auto [x, y] : ex.tiles)
    if (const MapTile *t = map.getTile(x, y))
      for (int c = 0; c < 4; c++)
        ground = std::max(ground, static_cast<int>(t->cornerHeight[c]));
  ex.groundHeight = ground;
  ex.floorHeight = ground - kTankSink;
  for (auto [x, y] : ex.tiles)
    if (MapTile *t = map.getTileMutable(x, y)) {
      for (int c = 0; c < 4; c++)
        t->cornerHeight[c] = ex.floorHeight;
      t->height = ex.floorHeight;
      t->terrainType = kTankTerrain;
    }
  map.touch();
  SDL_Log("[Fences] tank %d: %zu tiles sunk from %d to %d", ex.id, ex.tiles.size(),
          ex.groundHeight, ex.floorHeight);
  for (const Edge &e : boundaryOf(ex)) {
    auto it = this->edges.find(e);
    if (it != this->edges.end())
      it->second.tank = ex.id;
  }
  ex.water = 0;
  ex.wallTop = ex.floorHeight + kTankHeight; // a unit above the ground
  ex.salt = true;                            // initialSalinity 100
  ex.filling = true;                         // initialFillState 1
  ex.water = 1;                              // full at once, as the original
}

// The water and the tall walls go; the hole stays
void Fences::drainTank(Exhibit &ex) {
  ex.water = 0;
  ex.tank = false;
  for (auto &kv : this->edges)
    if (kv.second.tank == ex.id)
      kv.second.tank = -1;
}

// Draining and filling happen at once, paused or not (as the original)
void Fences::drainExhibit(int id) {
  if (Exhibit *ex = this->exhibit(id)) {
    ex->filling = false;
    ex->water = 0;
  }
}

void Fences::fillExhibit(int id, bool salt) {
  Exhibit *ex = this->exhibit(id);
  if (!ex || ex->groundHeight == ex->floorHeight)
    return;
  ex->tank = true;
  ex->salt = salt;
  ex->filling = true;
  ex->water = 1;
  ex->purity = 100;
  if (ex->wallTop <= ex->floorHeight)
    ex->wallTop = ex->floorHeight + kTankHeight;
  for (const Edge &e : boundaryOf(*ex)) {
    auto it = this->edges.find(e);
    if (it != this->edges.end() && this->typeList[it->second.type].tank)
      it->second.tank = ex->id;
  }
}

double Fences::wallStepCost(int id) const {
  const Exhibit *ex = this->exhibit(id);
  if (!ex)
    return 0;
  double walls = 0;
  for (const auto &kv : this->edges)
    if (kv.second.tank == id)
      walls += static_cast<double>(this->typeList[kv.second.type].cost) / kWallPriceDivisor;
  // (the water only while it has some: drained, the original shows $200)
  double water = ex->filling ? static_cast<double>(ex->tiles.size()) *
                                   (ex->salt ? kSaltWaterPrice : kFreshWaterPrice)
                             : 0.0;
  return walls + water;
}

int Fences::baseStepCost(int id) const {
  const Exhibit *ex = this->exhibit(id);
  return ex ? static_cast<int>(ex->tiles.size()) * kBaseStepPerTile : 0;
}

bool Fences::canAdjustWall(int id, int step) const {
  const Exhibit *ex = this->exhibit(id);
  if (!ex || !ex->tank)
    return false;
  int top = ex->wallTop + step;
  return top - ex->floorHeight <= kTankMaxHeight && top >= ex->groundHeight &&
         top - ex->floorHeight >= 2;
}

void Fences::adjustWall(int id, int step) {
  if (!this->canAdjustWall(id, step))
    return;
  this->exhibit(id)->wallTop += step;
}

bool Fences::canAdjustBase(int id, int step) const {
  const Exhibit *ex = this->exhibit(id);
  if (!ex || !ex->tank || !this->mapEdit)
    return false;
  int floor = ex->floorHeight + step, top = ex->wallTop + step;
  return floor < ex->groundHeight && top >= ex->groundHeight;
}

void Fences::adjustBase(int id, int step) {
  if (!this->canAdjustBase(id, step))
    return;
  Exhibit *ex = this->exhibit(id);
  ex->floorHeight += step;
  ex->wallTop += step;
  for (auto [x, y] : ex->tiles)
    if (MapTile *t = this->mapEdit->getTileMutable(x, y)) {
      for (int c = 0; c < 4; c++)
        t->cornerHeight[c] = ex->floorHeight;
      t->height = ex->floorHeight;
    }
  this->mapEdit->touch();
}

int Fences::fillCost(int id, bool salt) const {
  const Exhibit *ex = this->exhibit(id);
  if (!ex)
    return 0;
  // The tank's whole height (measured: a 2 x 2 tank 5 deep fills for
  // $30 salt, $20 fresh)
  int depth = std::max(0, ex->wallTop - ex->floorHeight);
  double units = static_cast<double>(ex->tiles.size()) * depth;
  return static_cast<int>(std::lround(units * (salt ? kSaltWaterPrice : kFreshWaterPrice)));
}

void Fences::update(float seconds) {
  // Wear: a point every cDecayDelta game days (12 s a day)
  for (auto &kv : this->edges) {
    Piece &p = kv.second;
    const FenceType &t = this->typeList[p.type];
    if (p.life < 0)
      p.life = static_cast<float>(t.life);
    if (t.indestructible || p.tank >= 0 || t.decayDelta <= 0)
      continue;
    p.life = std::max(0.0f, p.life - seconds / (12.0f * t.decayDelta));
  }
  for (Exhibit &ex : this->exhibitList) {
    if (!ex.tank)
      continue;
    if (ex.filling && ex.water < 1.0f)
      ex.water = std::min(1.0f, ex.water + seconds / kFillSeconds);
    else if (!ex.filling && ex.water > 0.0f)
      ex.water = std::max(0.0f, ex.water - seconds / kFillSeconds);
    // Standing water gets dirty; fresh water poured in is clean
    if (ex.water <= 0.0f) {
      ex.purity = 100;
      ex.purityClock = 0;
      continue;
    }
    ex.purityClock += seconds;
    while (ex.purityClock >= kPurityDecaySeconds) {
      ex.purityClock -= kPurityDecaySeconds;
      ex.purity = std::max(0.0f, ex.purity - 1.0f);
    }
  }
  const FilterType &k = this->filterKind;
  for (Filter &f : this->filterList) {
    f.decayClock += seconds;
    while (f.decayClock >= k.decayTime) {
      f.decayClock -= k.decayTime;
      f.health = std::max(0.0f, f.health - 1.0f);
    }
    Exhibit *ex = this->exhibit(f.tank);
    if (f.health <= 0 || !ex || !ex->tank || ex->water <= 0.0f)
      continue; // broken, or nothing to clean
    f.filterClock += seconds;
    while (f.filterClock >= k.filterDelay) {
      f.filterClock -= k.filterDelay;
      float amount = static_cast<float>(f.health > k.decayedHealth ? k.clean : k.decayedClean);
      ex->purity = std::min(100.0f, ex->purity + amount);
      this->upkeepOwed += k.upkeep;
      f.upkeepCurrent += k.upkeep;
      f.upkeepTotal += k.upkeep;
    }
  }
}

// ----------------------------------------------------------------------------
// Fence wear. A fence loses a point of its cLife every cDecayDelta days (an
// assumption, not yet measured against the original); worn to its
// cDecayedLife it shows its damaged art and a maintenance worker comes.
// ----------------------------------------------------------------------------
int Fences::refundOf(const Edge &e) const {
  const Piece *p = this->at(e);
  if (!p)
    return 0;
  const FenceType &t = this->typeList[p->type];
  return static_cast<int>(std::lround((p->gate ? t.gateCost : t.cost) * 0.8));
}

bool Fences::worn(const Edge &e) const {
  const Piece *p = this->at(e);
  if (!p || p->tank >= 0)
    return false;
  const FenceType &t = this->typeList[p->type];
  return !t.indestructible && p->life >= 0 && p->life <= t.decayedLife;
}

void Fences::repair(const Edge &e) {
  auto it = this->edges.find(e);
  if (it != this->edges.end())
    it->second.life = static_cast<float>(this->typeList[it->second.type].life);
}

float Fences::lifeOf(const Edge &e) const {
  const Piece *p = this->at(e);
  return p ? p->life : -1;
}

void Fences::setLife(const Edge &e, float life) {
  auto it = this->edges.find(e);
  if (it != this->edges.end())
    it->second.life = life;
}

void Fences::serviceFilter(int index) {
  if (Filter *f = this->filter(index)) {
    f->health = static_cast<float>(this->filterKind.startHealth);
    f->decayClock = 0;
  }
}

int Fences::takeUpkeep() {
  int whole = static_cast<int>(this->upkeepOwed);
  this->upkeepOwed -= whole;
  return whole;
}

// ----------------------------------------------------------------------------
// Tank filters
// ----------------------------------------------------------------------------
Fences::Fit Fences::filterFit(int x, int y, const WorldMap &map, int *tank) const {
  const FilterType &k = this->filterKind;
  if (k.file.empty())
    return Fit::Outside;
  auto inside = [&](int tx, int ty) {
    return tx >= x && tx < x + k.footprintX && ty >= y && ty < y + k.footprintY;
  };
  Fit worst = Fit::Ok;
  for (int ty = y; ty < y + k.footprintY; ty++)
    for (int tx = x; tx < x + k.footprintX; tx++) {
      Fit f = this->tileFit(tx, ty, map);
      if (f == Fit::Outside)
        return f;
      if (f == Fit::InTheWay || this->exhibitAt(tx, ty) >= 0)
        worst = Fit::InTheWay;
      // No fence across it
      if (inside(tx + 1, ty) && this->edges.count({false, tx + 1, ty}))
        worst = Fit::InTheWay;
      if (inside(tx, ty + 1) && this->edges.count({true, tx, ty + 1}))
        worst = Fit::InTheWay;
      // No other filter on it
      for (const Filter &o : this->filterList)
        if (tx >= o.x && tx < o.x + k.footprintX && ty >= o.y && ty < o.y + k.footprintY)
          worst = Fit::InTheWay;
    }
  // Beside a completed tank (across one of its walls) that has no filter
  int found = -1;
  for (int ty = y; ty < y + k.footprintY && found < 0; ty++)
    for (int tx = x; tx < x + k.footprintX && found < 0; tx++) {
      const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto &dd : d) {
        int nx = tx + dd[0], ny = ty + dd[1];
        if (inside(nx, ny))
          continue;
        int id = this->exhibitAt(nx, ny);
        const Exhibit *ex = id >= 0 ? this->exhibit(id) : nullptr;
        if (!ex || !ex->tank)
          continue;
        Edge wall = dd[0] != 0 ? Edge{false, std::max(tx, nx), ty} : Edge{true, tx, std::max(ty, ny)};
        const Piece *p = this->at(wall);
        if (!p || p->tank != id)
          continue;
        bool taken = false;
        for (const Filter &o : this->filterList)
          taken = taken || o.tank == id;
        if (!taken) {
          found = id;
          break;
        }
      }
    }
  if (tank)
    *tank = found;
  if (found < 0)
    return Fit::Outside;
  return worst;
}

bool Fences::placeFilter(int x, int y, const WorldMap &map) {
  int tank = -1;
  if (this->filterFit(x, y, map, &tank) != Fit::Ok)
    return false;
  const FilterType &k = this->filterKind;
  Filter f;
  f.x = x;
  f.y = y;
  f.tank = tank;
  f.health = static_cast<float>(k.startHealth);
  f.placedMonth = this->monthNow;
  f.name = "Filter " + std::to_string(++this->filtersMade);
  // Toward the tank: from the footprint's middle to the tank's
  if (const Exhibit *ex = this->exhibit(tank)) {
    float cx = 0, cy = 0;
    for (auto [tx, ty] : ex->tiles) {
      cx += tx + 0.5f;
      cy += ty + 0.5f;
    }
    cx /= ex->tiles.size();
    cy /= ex->tiles.size();
    float mx = x + k.footprintX * 0.5f, my = y + k.footprintY * 0.5f;
    if (std::fabs(cx - mx) >= std::fabs(cy - my))
      f.dx = cx > mx ? 1.0f : -1.0f;
    else
      f.dy = cy > my ? 1.0f : -1.0f;
  }
  this->filterList.push_back(f);
  return true;
}

float Fences::filterGround(const Filter &f, const WorldMap &map) const {
  int h = 0;
  for (int ty = f.y; ty < f.y + this->filterKind.footprintY; ty++)
    for (int tx = f.x; tx < f.x + this->filterKind.footprintX; tx++)
      if (const MapTile *t = map.getTile(tx, ty))
        for (int c = 0; c < 4; c++)
          h = std::max(h, static_cast<int>(t->cornerHeight[c]));
  return static_cast<float>(h);
}

int Fences::filterAt(float px, float py, const WorldRenderer &view, const WorldMap &map) const {
  float best = 30.0f * 30.0f;
  int found = -1;
  for (size_t i = 0; i < this->filterList.size(); i++) {
    const Filter &f = this->filterList[i];
    float sx, sy, d;
    view.worldToScreenF(f.x + this->filterKind.footprintX * 0.5f,
                        f.y + this->filterKind.footprintY * 0.5f, this->filterGround(f, map) + 1.0f,
                        sx, sy, d);
    float dist = (sx - px) * (sx - px) + (sy - py) * (sy - py);
    if (dist < best) {
      best = dist;
      found = static_cast<int>(i);
    }
  }
  return found;
}

void Fences::newMonth() {
  for (Filter &f : this->filterList) {
    f.upkeepLast = f.upkeepCurrent;
    f.upkeepCurrent = 0;
  }
}

void Fences::removeFilter(int index) {
  if (index >= 0 && index < static_cast<int>(this->filterList.size()))
    this->filterList.erase(this->filterList.begin() + index);
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
// Where a piece stands: its edge's ends (world vertices) and their heights,
// the higher side's where the edge is a cliff (a fence stands on top)
void Fences::edgeGeometry(const Edge &e, const Piece &p, const WorldMap &map, int &ax,
                          int &ay, int &bx, int &by, int &h0, int &h1) const {
  (void)p;
  if (e.alongX) {
    ax = e.x; ay = e.y; bx = e.x + 1; by = e.y;
  } else {
    ax = e.x; ay = e.y; bx = e.x; by = e.y + 1;
  }
  // The tiles on either side, and their corners at the edge's ends
  auto corner = [&](int tx, int ty, int vx, int vy, int &h) {
    const MapTile *t = map.getTile(tx, ty);
    if (!t)
      return;
    int c = (vx > tx ? 1 : 0) + (vy > ty ? 2 : 0); // x1? y1?
    static const int index[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X0Y1, CORNER_X1Y1};
    h = std::max(h, static_cast<int>(t->cornerHeight[index[c]]));
  };
  h0 = h1 = INT32_MIN;
  int t1x, t1y, t2x, t2y;
  if (e.alongX) {
    t1x = e.x; t1y = e.y - 1; t2x = e.x; t2y = e.y;
  } else {
    t1x = e.x - 1; t1y = e.y; t2x = e.x; t2y = e.y;
  }
  corner(t1x, t1y, ax, ay, h0);
  corner(t2x, t2y, ax, ay, h0);
  corner(t1x, t1y, bx, by, h1);
  corner(t2x, t2y, bx, by, h1);
  if (h0 == INT32_MIN)
    h0 = 0;
  if (h1 == INT32_MIN)
    h1 = 0;
}

bool Fences::pieceAt(float px, float py, const WorldRenderer &view, const WorldMap &map,
                     Edge &found) const {
  float best = 20.0f * 20.0f; // within 20 logical pixels of its middle
  bool any = false;
  for (const auto &kv : this->edges) {
    int ax, ay, bx, by, h0, h1;
    this->edgeGeometry(kv.first, kv.second, map, ax, ay, bx, by, h0, h1);
    float h = (h0 + h1) * 0.5f + 1.0f; // about half way up a fence
    if (kv.second.tank >= 0)
      for (const Exhibit &x : this->exhibitList)
        if (x.id == kv.second.tank)
          h = x.wallTop - 0.5f;
    float sx, sy, d;
    view.worldToScreenF((ax + bx) * 0.5f, (ay + by) * 0.5f, h, sx, sy, d);
    float dist = (sx - px) * (sx - px) + (sy - py) * (sy - py);
    if (dist < best) {
      best = dist;
      found = kv.first;
      any = true;
    }
  }
  return any;
}

void Fences::collect(const WorldRenderer &view, const WorldMap &map,
                     std::vector<Drawable> &out) const {
  auto add = [&](const Edge &e, const Piece &p, int type, bool tinted, SDL_Color tint) {
    const FenceType &t = this->typeList[type];
    int ax, ay, bx, by, h0, h1;
    this->edgeGeometry(e, p, map, ax, ay, bx, by, h0, h1);
    float angle = p.facing * 3.14159265f / 4.0f;
    CompassDirection side = view.screenSide(std::sin(angle), -std::cos(angle));
    float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;

    // A tank's walls: stacked from its floor to a unit above the ground;
    // its gate is wall too, with the platform and ladder on it
    if (p.tank >= 0) {
      const Exhibit *ex = nullptr;
      for (const Exhibit &x : this->exhibitList)
        if (x.id == p.tank)
          ex = &x;
      if (ex && t.tankArt[1][0]) {
        // Which end it is: collinear tank neighbours to its screen left and
        // right
        Edge prev = e, next = e;
        if (e.alongX) { prev.x--; next.x++; } else { prev.y--; next.y++; }
        auto isTankWall = [&](const Edge &n) {
          const Piece *q = this->at(n);
          return q && q->tank == p.tank;
        };
        float s0x, s0y, s1x, s1y, d;
        view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), 0, s0x, s0y, d);
        view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), 0, s1x, s1y, d);
        bool prevLeft = s0x < s1x; // prev joins the edge at (ax, ay)
        bool left = isTankWall(prevLeft ? prev : next);
        bool right = isTankWall(prevLeft ? next : prev);
        int column = left && right ? 1 : right ? 0 : left ? 2 : 3;
        int top = ex->wallTop;
        int levels = top - ex->floorHeight;
        // The ground outside hides the wall below it: the original shows
        // the rows from a unit under the ground up (the cap and the glass
        // under it, as made; more as the wall is raised or the tank comes
        // up out of the ground)
        for (int k = std::max(0, ex->groundHeight - 1 - ex->floorHeight); k < levels; k++) {
          int level = k == 0 ? 0 : k == levels - 1 ? 2 : 1;
          Animation *a = t.tankArt[column][level];
          if (!a)
            a = t.tankArt[1][level];
          if (!a)
            continue;
          float sx, sy, depth;
          view.worldToScreenF(mx, my, static_cast<float>(ex->floorHeight + k), sx, sy, depth);
          out.push_back({depth + k * 0.001f, sx, sy, a, side, tint, tinted});
        }
        if (p.gate) {
          // Facing into the tank: toward the tile on its side of the edge
          int ix = e.alongX ? e.x : e.x - 1, iy = e.alongX ? e.y - 1 : e.y;
          bool firstInside = ex->tiles.count({ix, iy}) > 0;
          float dx = e.alongX ? 0.0f : (firstInside ? -1.0f : 1.0f);
          float dy = e.alongX ? (firstInside ? -1.0f : 1.0f) : 0.0f;
          // (its art is named a quarter turn round from the way it faces:
          // NE, SE, SW, NW in turn)
          CompassDirection in = static_cast<CompassDirection>(
              (static_cast<int>(view.screenSide(dx, dy)) + 1) % 4);
          float sx, sy, depth;
          // At the water, a level under the rim (its rails reach up to it)
          view.worldToScreenF(mx, my, static_cast<float>(top - 2), sx, sy, depth);
          if (t.ladder)
            out.push_back({depth + levels * 0.001f + 0.0005f, sx, sy, t.ladder, in, tint, tinted});
          if (t.platform)
            out.push_back({depth + levels * 0.001f + 0.001f, sx, sy, t.platform, in, tint, tinted});
        }
        return;
      }
    }

    // Worn: its damaged or broken art (fences, not gates)
    Animation *const *set = t.art[p.gate ? 1 : 0];
    if (!p.gate && !t.indestructible && p.life >= 0 && !tinted) {
      if (p.life <= 0.0f && t.broke[0])
        set = t.broke;
      else if (p.life <= t.decayedLife && t.det[0])
        set = t.det;
    }
    Animation *art = set[0];
    if (h0 != h1) {
      float s0x, s0y, s1x, s1y, d;
      view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), 0, s0x, s0y, d);
      view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), 0, s1x, s1y, d);
      bool startLeft = side == CompassDirection::NE || side == CompassDirection::NW;
      bool firstIsLeft = s0x < s1x;
      int hStart = (firstIsLeft == startLeft) ? h0 : h1;
      int hEnd = (firstIsLeft == startLeft) ? h1 : h0;
      Animation *slope = set[hEnd > hStart ? 1 : 2];
      if (slope)
        art = slope;
    }
    if (!art)
      return;
    float sx, sy, depth;
    view.worldToScreenF(mx, my, (h0 + h1) * 0.5f, sx, sy, depth);
    out.push_back({depth, sx, sy, art, side, tint, tinted});
  };

  for (const auto &kv : this->edges)
    add(kv.first, kv.second, kv.second.type, false, SDL_Color{255, 255, 255, 255});
  // Filters: running, decayed, or stopped; their hose toward the tank (the
  // art is named a quarter turn round, as the tank gate's)
  const FilterType &fk = this->filterKind;
  auto addFilter = [&](const Filter &f, Animation *a, bool tinted, SDL_Color tint) {
    if (!a)
      return;
    CompassDirection d = static_cast<CompassDirection>(
        (static_cast<int>(view.screenSide(f.dx, f.dy)) + 1) % 4);
    float sx, sy, depth;
    view.worldToScreenF(f.x + fk.footprintX * 0.5f, f.y + fk.footprintY * 0.5f,
                        this->filterGround(f, map), sx, sy, depth);
    out.push_back({depth, sx, sy, a, d, tint, tinted});
  };
  for (const Filter &f : this->filterList) {
    Animation *a = f.health <= 0                ? fk.off
                   : f.health <= fk.decayedHealth ? fk.decayed
                                                  : fk.idle;
    addFilter(f, a ? a : fk.idle, false, SDL_Color{255, 255, 255, 255});
  }
  if (this->previewFilterX >= 0) {
    Filter f;
    f.x = this->previewFilterX;
    f.y = this->previewFilterY;
    int tank = -1;
    this->filterFit(f.x, f.y, map, &tank);
    if (const Exhibit *ex = this->exhibit(tank)) {
      float cx = 0, cy = 0;
      for (auto [tx, ty] : ex->tiles) {
        cx += tx + 0.5f;
        cy += ty + 0.5f;
      }
      cx /= ex->tiles.size();
      cy /= ex->tiles.size();
      float mx = f.x + fk.footprintX * 0.5f, my = f.y + fk.footprintY * 0.5f;
      if (std::fabs(cx - mx) >= std::fabs(cy - my))
        f.dx = cx > mx ? 1.0f : -1.0f;
      else
        f.dy = cy > my ? 1.0f : -1.0f;
    } else {
      f.dx = 1.0f;
    }
    SDL_Color tint = this->previewFilterFit == Fit::Ok       ? SDL_Color{0, 255, 0, 255}
                     : this->previewFilterFit == Fit::Outside ? SDL_Color{255, 60, 60, 255}
                                                              : SDL_Color{255, 170, 45, 255};
    addFilter(f, fk.idle, true, tint);
  }
  if (this->previewType >= 0)
    for (const auto &pv : this->preview) {
      Piece p = ownerFor(pv.first, map);
      p.type = this->previewType;
      add(pv.first, p, this->previewType, true,
          pv.second ? SDL_Color{0, 255, 0, 255}
          : this->deleting || this->fit(pv.first, map) == Fit::Outside
              ? SDL_Color{255, 60, 60, 255}
              : SDL_Color{255, 170, 45, 255});
    }
}

Animation *Fences::waterPart(const std::string &set, const std::string &part) const {
  std::string path = "water/" + set + "/" + part + "/" + part;
  auto it = this->waterArt.find(path);
  if (it != this->waterArt.end())
    return it->second;
  Animation *a = this->rm && this->rm->hasResource(path + ".ani") ? this->rm->getAnimation(path)
                                                                  : nullptr;
  this->waterArt[path] = a;
  return a;
}

// The original's tank water (XPACK2 water/): each tile's surface ("top")
// at the water level and, on the tank's front edges, its sides seen through
// the glass ("front" down-left, "right" down-right), a height unit each,
// from a unit under the ground up (as the walls). The art set is the
// water's: salt or fresh, clean or with light / heavy / extreme scum
// (tanks.cfg murkyWaterPurity 60, veryMurky 20, extremelyMurky 1), the
// scummy surfaces and sides varying tile to tile.
void Fences::drawWater(SDL_Renderer *renderer, const WorldRenderer &view,
                       const WorldMap &map) const {
  (void)map;
  for (const Exhibit &ex : this->exhibitList) {
    if (!ex.tank || ex.water <= 0)
      continue;
    // Up to a unit below the walls' top (tanks.cfg waterOffset -1)
    float level = ex.floorHeight + ex.water * (ex.wallTop - 1 - ex.floorHeight);
    std::string set = ex.salt ? "salt" : "fresh";
    if (ex.purity < 1)
      set += "es";
    else if (ex.purity < 20)
      set += "hs";
    else if (ex.purity < 60)
      set += "ls";
    const bool scum = set.size() > (ex.salt ? 4u : 5u);
    Animation *top = this->waterPart(set, "top");
    if (!top) {
      // No art (Marine Mania missing): plain water
      std::vector<SDL_Vertex> v;
      std::vector<int> idx;
      SDL_Color c = {40, 110, 190, 150};
      for (auto [x, y] : ex.tiles) {
        float px[4], py[4], d;
        const float cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
        for (int i = 0; i < 4; i++)
          view.worldToScreenF(x + cx[i], y + cy[i], level, px[i], py[i], d);
        int base = static_cast<int>(v.size());
        for (int i = 0; i < 4; i++)
          v.push_back({{px[i], py[i]}, c, {0, 0}});
        for (int i : {0, 1, 2, 0, 2, 3})
          idx.push_back(base + i);
      }
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_RenderGeometry(renderer, nullptr, v.data(), static_cast<int>(v.size()), idx.data(),
                         static_cast<int>(idx.size()));
      continue;
    }
    // Back to front
    struct TileAt {
      int x, y;
      float depth;
    };
    std::vector<TileAt> tiles;
    for (auto [x, y] : ex.tiles) {
      float sx, sy, d;
      view.worldToScreenF(x + 0.5f, y + 0.5f, level, sx, sy, d);
      tiles.push_back({x, y, d});
    }
    std::sort(tiles.begin(), tiles.end(),
              [](const TileAt &p, const TileAt &q) { return p.depth < q.depth; });
    auto variant = [&](const std::string &part, const std::string &scumPart, int x, int y) {
      if (!scum)
        return this->waterPart(set, part);
      unsigned h = static_cast<unsigned>(x * 73856093) ^ static_cast<unsigned>(y * 19349663);
      int n = h % 4;
      Animation *a = n == 0 ? nullptr : this->waterPart(set, scumPart + std::to_string(n));
      return a ? a : this->waterPart(set, part);
    };
    const int lowest = std::max(ex.floorHeight, ex.groundHeight - 1);
    for (const TileAt &t : tiles) {
      // Its sides on the front edges (neighbours outside the tank)
      const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto &dd : d) {
        if (ex.tiles.count({t.x + dd[0], t.y + dd[1]}))
          continue;
        CompassDirection side = view.screenSide(static_cast<float>(dd[0]), static_cast<float>(dd[1]));
        bool left = side == CompassDirection::SW, right = side == CompassDirection::SE;
        if (!left && !right)
          continue;
        Animation *face = variant(left ? "front" : "right", left ? "fscum" : "rscum", t.x, t.y);
        if (!face)
          continue;
        // The edge's middle
        float mx = t.x + 0.5f + dd[0] * 0.5f, my = t.y + 0.5f + dd[1] * 0.5f;
        for (float h = static_cast<float>(lowest); h < level - 0.01f; h += 1.0f) {
          float sx, sy, dep;
          view.worldToScreenF(mx, my, std::min(h + 1.0f, level), sx, sy, dep);
          face->drawAnchored(renderer, sx, sy, CompassDirection::N, nullptr);
        }
      }
      Animation *surface = variant("top", "tscum", t.x, t.y);
      float sx, sy, dep;
      view.worldToScreenF(t.x + 0.5f, t.y + 0.5f, level, sx, sy, dep);
      if (surface)
        surface->drawAnchored(renderer, sx, sy, CompassDirection::N, nullptr);
    }
  }
}
