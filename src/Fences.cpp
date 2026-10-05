#include <cstdlib>
#include "Fences.hpp"
#include <random>
#include "Sound.hpp"

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
constexpr int kTankSink = 4;       // initialSink (subtiles: 2 height units)
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
  if (!this->selectArrow && rm->hasResource("ui/select/selsmall/selsmall.ani"))
    this->selectArrow = rm->getAnimation("ui/select/selsmall/selsmall");
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
      {
        std::string v = Utils::string_to_lower(ai->get("g/animations", "ladder"));
        size_t at = v.find("ladrin");
        if (at != std::string::npos) {
          v = v.substr(0, at) + "ladrout/ladrout";
          t.ladderOut = anim(v);
        }
      }
    }
    const std::string fints = "f/characteristics/integers";
    t.life = ai->getInt(fints, "clife", 10);
    t.decayedLife = ai->getInt(fints, "cdecayedlife", 5);
    t.decayDelta = ai->getInt(fints, "cdecaydelta", 25);
    t.strength = ai->getInt(fints, "cstrength", 200);
    t.jumpable = ai->getInt(fints, "cisjumpable", 0) != 0;
    t.climbable = ai->getInt(fints, "cisclimbable", 0) != 0;
    t.seeThrough = ai->getInt(fints, "cseethrough", 0) != 0;
    t.electrified = ai->getInt(fints, "ciselectrified", 0) != 0;
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
  // A fence there already: the fence tool's kind replaces it (zoo.exe
  // 0x486965: any kind, a gate staying a gate; not the zoo's wall, not a
  // tank's, not with a tank wall)
  if (auto have = this->edges.find(e); have != this->edges.end()) {
    const Piece &p = have->second;
    if (!replaceable(p, this->previewType))
      return Fit::InTheWay;
    return Fit::Ok;
  }
  // Something standing across it
  if (this->edgeBlocked && this->edgeBlocked(e))
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

bool Fences::replaceable(const Piece &p, int type) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()) || p.tank >= 0)
    return false;
  if (p.type >= 0 && p.type < static_cast<int>(this->typeList.size()) && this->typeList[p.type].zooWall)
    return false;
  return !this->typeList[type].zooWall && !this->typeList[type].tankArt[1][0];
}

void Fences::place(const Edge &e, int type, int drag, const WorldMap &map) {
  if (auto have = this->edges.find(e); have != this->edges.end()) {
    // Over one already: replaced by a new one of this kind (a gate stays a
    // gate)
    if (!replaceable(have->second, type))
      return;
    have->second.type = type;
    have->second.life = static_cast<float>(this->typeList[type].life);
    have->second.order = this->placed++;
    have->second.drag = drag;
    have->second.open = 0;
    return;
  }
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

void Fences::updateGates(float seconds, const std::function<bool(const Edge &)> &someoneNear) {
  for (auto &[e, p] : this->edges) {
    if (!p.gate || p.tank >= 0)
      continue;
    bool near = someoneNear(e);
    const float frames = 12.0f;
    p.open = near ? std::min(frames, p.open + 20.0f * seconds) : std::max(0.0f, p.open - 20.0f * seconds);
  }
}

bool Fences::broken(const Edge &e) const {
  const Piece *p = this->at(e);
  return p && p->life == 0.0f && !p->gate;
}

int Fences::condition(int exhibit) const {
  const Exhibit *ex = this->exhibit(exhibit);
  if (!ex)
    return 0;
  int worst = 0;
  for (auto [x, y] : ex->tiles) {
    const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &dd : d) {
      int nx = x + dd[0], ny = y + dd[1];
      if (ex->tiles.count({nx, ny}))
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
      if (!p || p->gate)
        continue;
      const FenceType &t = this->typeList[p->type];
      float life = p->life < 0 ? static_cast<float>(t.life) : p->life;
      if (life <= 0)
        return 2;
      if (!t.indestructible && life <= t.decayedLife)
        worst = 1;
    }
  }
  return worst;
}

bool Fences::passable(const Edge &e, bool climber, bool jumper, int bash, int crush) const {
  const Piece *p = this->at(e);
  if (!p)
    return true;
  const FenceType &t = this->typeList[p->type];
  if (t.zooWall || p->gate || p->tank >= 0)
    return false;
  float life = p->life < 0 ? static_cast<float>(t.life) : p->life;
  if (life <= 0)
    return true;
  if (t.electrified && life > t.decayedLife)
    return false;
  if (climber && t.climbable)
    return true;
  if (jumper && t.jumpable)
    return true;
  int strength = std::max(1, t.strength - t.decayDelta * static_cast<int>(t.life - life));
  if (bash > strength)
    return true;
  return crush > 0 && crush >= t.height;
}

void Fences::breakPiece(const Edge &e) {
  auto it = this->edges.find(e);
  if (it == this->edges.end())
    return;
  const FenceType &t = this->typeList[it->second.type];
  if (t.zooWall || t.indestructible || it->second.gate || it->second.tank >= 0)
    return;
  it->second.life = 0;
  Sound::get().play("sounds/brokfenc");
}

void Fences::dropOrphanGates() {
  // A gate that's no exhibit's, or between two exhibits, or a second gate
  // of an exhibit (joined): a plain fence again
  std::set<int> gated;
  for (auto &[e, p] : this->edges) {
    if (!p.gate || p.tank >= 0)
      continue;
    int a = e.alongX ? this->exhibitAt(e.x, e.y - 1) : this->exhibitAt(e.x - 1, e.y);
    int b = this->exhibitAt(e.x, e.y);
    int ex = a >= 0 ? a : b;
    if (ex < 0 || (a >= 0 && b >= 0) || !gated.insert(ex).second)
      p.gate = false;
  }
}

bool Fences::canGate(const Edge &e) const {
  const Piece *p = this->at(e);
  if (!p || p->gate || p->tank >= 0 || this->typeList[p->type].zooWall)
    return false;
  int a = e.alongX ? this->exhibitAt(e.x, e.y - 1) : this->exhibitAt(e.x - 1, e.y);
  int b = this->exhibitAt(e.x, e.y);
  return (a >= 0) != (b >= 0); // exactly one side an exhibit, the other open zoo
}

int Fences::gateCostOf(const Edge &e) const {
  const Piece *p = this->at(e);
  if (!p)
    return 0;
  const FenceType &t = this->typeList[p->type];
  float life = p->life < 0 ? static_cast<float>(t.life) : p->life;
  return life > t.decayedLife ? 0 : t.gateCost;
}

void Fences::moveGate(const Edge &e) {
  if (!this->canGate(e))
    return;
  int a = e.alongX ? this->exhibitAt(e.x, e.y - 1) : this->exhibitAt(e.x - 1, e.y);
  int ex = a >= 0 ? a : this->exhibitAt(e.x, e.y);
  for (auto &[o, p] : this->edges)
    if (p.gate && p.tank < 0 && this->exhibitOf(o) == ex)
      p.gate = false;
  auto it = this->edges.find(e);
  it->second.gate = true;
  if (it->second.life >= 0)
    it->second.life = static_cast<float>(this->typeList[it->second.type].life);
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
  // For the exhibits' rebuild: whose gate this was, and whose side it stood on
  this->lastRemovedGateOf = it->second.gate ? this->exhibitOf(e) : -1;
  {
    int ox = it->second.ownerX, oy = it->second.ownerY;
    this->lastRemovedOwner = this->exhibitAt(ox, oy);
  }
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
  // Exhibits reshaped (zoo.exe 0x50c3d8, 0x58a009): one split by a new
  // fence keeps itself (name, gate, keepers) on the side with its gate;
  // two joined by taking out the wall between: the one whose gate was in
  // that wall goes, else the one nested inside the other (reached only
  // through it), else the smaller; the survivor keeps its name and gate,
  // the other's gate becomes a plain fence again
  if (!loading) {
    this->lastRemovedGateOf = this->lastRemovedGateOf; // (set by remove)
    auto gateTile = [&](const Exhibit &ex, int &gx, int &gy) {
      for (const auto &[e, p] : this->edges)
        if (p.gate && p.tank < 0) {
          int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
          if (ex.tiles.count({ax, ay})) { gx = ax; gy = ay; return true; }
          if (ex.tiles.count({e.x, e.y})) { gx = e.x; gy = e.y; return true; }
        }
      return false;
    };
    // Which closed ground each old exhibit's tiles lie in
    std::map<int, std::vector<Exhibit *>> claims; // region -> exhibits overlapping it
    for (Exhibit &ex : this->exhibitList) {
      if (ex.tank || !ex.named || ex.tiles.empty())
        continue;
      std::set<int> seen;
      for (auto [x, y] : ex.tiles)
        if (x >= 0 && y >= 0 && x < W && y < H) {
          int id = region[y * W + x];
          if (!open[id] && seen.insert(id).second)
            claims[id].push_back(&ex);
        }
    }
    for (auto &[id, exs] : claims) {
      if (taken[id])
        continue;
      Exhibit *keep = nullptr;
      if (exs.size() == 1) {
        // One exhibit's ground (made bigger, or a part of it split off):
        // its gate's side keeps it (no gate: the part with most of it)
        Exhibit *ex = exs[0];
        int gx, gy;
        bool hasGate = gateTile(*ex, gx, gy);
        bool gateHere = hasGate && region[gy * W + gx] == id;
        bool gateElsewhere = false;
        if (hasGate && !gateHere) {
          int gid = region[gy * W + gx];
          gateElsewhere = !open[gid];
        }
        if (gateHere || !gateElsewhere) {
          // (no gate anywhere closed: the part with most of its tiles)
          if (!gateHere) {
            int mine = 0, best = 0;
            std::map<int, int> overlap;
            for (auto [x, y] : ex->tiles)
              overlap[region[y * W + x]]++;
            for (auto [rid, n] : overlap)
              if (!open[rid]) {
                best = std::max(best, n);
                if (rid == id) mine = n;
              }
            if (mine < best)
              continue;
          }
          keep = ex;
        }
      } else {
        // Joined: whose gate was in the wall that went? (now inside the
        // joined ground, not on its edge)
        std::vector<Exhibit *> alive = exs;
        if (this->lastRemovedGateOf >= 0)
          for (Exhibit *ex : exs)
            if (ex->id == this->lastRemovedGateOf && alive.size() > 1)
              alive.erase(std::find(alive.begin(), alive.end(), ex));
        // Else the larger (ties: the side the wall piece stood on)
        keep = alive[0];
        for (Exhibit *ex : alive)
          if (ex->tiles.size() > keep->tiles.size() ||
              (ex->tiles.size() == keep->tiles.size() && ex->id == this->lastRemovedOwner))
            keep = ex;
      }
      if (!keep)
        continue;
      keep->tiles = regions[id];
      taken[id] = true;
    }
  }
  this->lastRemovedGateOf = this->lastRemovedOwner = -1;
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
  // Already has one (a reshaped exhibit keeps its gate)
  for (const Edge &e : boundaryOf(*ex)) {
    auto it = this->edges.find(e);
    if (it != this->edges.end() && it->second.gate)
      return 0;
  }
  // The gate: in the middle of the first-laid stretch of the loop, on a
  // wall to the open zoo (never one shared with another exhibit)
  std::vector<std::pair<Edge, Piece *>> round;
  for (const Edge &e : boundaryOf(*ex)) {
    auto it = this->edges.find(e);
    if (it == this->edges.end())
      continue;
    int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
    bool aIn = ex->tiles.count({ax, ay}) > 0;
    int ox = aIn ? e.x : ax, oy = aIn ? e.y : ay;
    if (this->exhibitAt(ox, oy) >= 0 || this->typeList[it->second.type].zooWall)
      continue;
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
  ex.floorHeight = ground - kTankSink / 2;
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
    if (it == this->edges.end())
      continue;
    // (a wall of the tank next door: the two share it)
    if (it->second.tank >= 0 && it->second.tank != ex.id && this->exhibit(it->second.tank))
      it->second.tank2 = ex.id;
    else
      it->second.tank = ex.id;
  }
  ex.water = 0;
  ex.wallSub = kTankHeight; // a subtile above the ground
  ex.salt = true;                            // initialSalinity 100
  ex.filling = true;                         // initialFillState 1
  ex.water = 1;                              // full at once, as the original
  ex.level = -1e9f;
}

// The water and the tall walls go; the hole stays
void Fences::drainTank(Exhibit &ex) {
  ex.water = 0;
  ex.tank = false;
  for (auto &kv : this->edges) {
    if (kv.second.tank2 == ex.id)
      kv.second.tank2 = -1;
    if (kv.second.tank == ex.id) {
      kv.second.tank = kv.second.tank2;
      kv.second.tank2 = -1;
    }
  }
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
  ex->level = -1e9f;
  ex->purity = 100;
  if (ex->wallSub <= 0)
    ex->wallSub = kTankHeight;
  for (const Edge &e : boundaryOf(*ex)) {
    auto it = this->edges.find(e);
    if (it == this->edges.end() || !this->typeList[it->second.type].tank)
      continue;
    if (it->second.tank >= 0 && it->second.tank != ex->id && this->exhibit(it->second.tank))
      it->second.tank2 = ex->id;
    else if (it->second.tank2 != ex->id)
      it->second.tank = ex->id;
  }
}

double Fences::wallStepCost(int id) const {
  const Exhibit *ex = this->exhibit(id);
  if (!ex)
    return 0;
  double walls = 0;
  for (const auto &kv : this->edges)
    if (kv.second.tank == id || kv.second.tank2 == id)
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
  // (as the original: down to two subtiles over the floor - under the
  // ground round it - up to maximumTankHeight)
  int sub = ex->wallSub + step;
  return sub <= kTankMaxHeight && sub >= 2;
}

void Fences::adjustWall(int id, int step) {
  if (!this->canAdjustWall(id, step))
    return;
  this->exhibit(id)->wallSub += step;
  // (silent, as the original: no sound for the walls, filling or draining)
}

bool Fences::canAdjustBase(int id, int step) const {
  const Exhibit *ex = this->exhibit(id);
  if (!ex || !ex->tank || !this->mapEdit)
    return false;
  int floor = ex->floorHeight + step;
  return floor < ex->groundHeight;
}

void Fences::adjustBase(int id, int step) {
  if (!this->canAdjustBase(id, step))
    return;
  Exhibit *ex = this->exhibit(id);
  ex->floorHeight += step;
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
  int depth = std::max(0, ex->wallSub);
  double units = static_cast<double>(ex->tiles.size()) * depth;
  return static_cast<int>(std::lround(units * (salt ? kSaltWaterPrice : kFreshWaterPrice)));
}

void Fences::update(float seconds) {
  this->rippleClock += seconds * 1000.0f;
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
    // The water shown follows the walls and the base: up a unit a second
    // (as the original after the base is raised), down at once
    {
      float target = ex.floorHeight + ex.water * (ex.wallTop() - 0.5f - ex.floorHeight);
      if (ex.level < -1e8f || ex.level > target)
        ex.level = target;
      else
        ex.level = std::min(target, ex.level + seconds);
    }
    // Waves now and then, while the water's clean (murkyWaterPurity 60)
    for (Exhibit::Wave &w : ex.waves)
      w.age += seconds * 1000.0f;
    ex.waves.erase(std::remove_if(ex.waves.begin(), ex.waves.end(),
                                  [](const Exhibit::Wave &w) { return w.age >= 4000.0f; }),
                   ex.waves.end());
    if (ex.water > 0 && ex.purity >= 60 && !ex.tiles.empty()) {
      ex.waveIn -= seconds * 1000.0f;
      if (ex.waveIn <= 0) {
        ex.waveIn = 40000.0f / static_cast<float>(ex.tiles.size());
        static std::mt19937 waveRng(12345);
        auto it = ex.tiles.begin();
        std::advance(it, std::uniform_int_distribution<size_t>(0, ex.tiles.size() - 1)(waveRng));
        ex.waves.push_back({it->first, it->second, std::uniform_int_distribution<int>(0, 7)(waveRng), 0.0f});
      }
    } else {
      ex.waves.clear();
    }
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
  // (zoo.exe 0x4f945b: broken, nothing; worn, half of the 80%)
  float life = p->life < 0 ? static_cast<float>(t.life) : p->life;
  if (life <= 0)
    return 0;
  float rate = life <= t.decayedLife ? 0.4f : 0.8f;
  return static_cast<int>(std::lround((p->gate ? t.gateCost : t.cost) * rate));
}

int Fences::layCost(const Edge &e, int type) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return 0;
  // (zoo.exe 0x4e05f7: over one of the same kind not yet worn, free)
  if (const Piece *p = this->at(e))
    if (p->type == type) {
      float life = p->life < 0 ? static_cast<float>(this->typeList[type].life) : p->life;
      if (life > this->typeList[type].decayedLife)
        return 0;
    }
  return this->typeList[type].cost;
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
  for (Exhibit &ex : this->exhibitList) {
    ex.donationsLast = ex.donationsNow;
    ex.donationsNow = 0;
    ex.upkeepLast = ex.upkeepNow;
    ex.upkeepNow = 0;
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
  // A tank's wall: anywhere on its face, from the floor up to its top (a
  // raised one's middle was far from where it was looked for); of two in
  // the way, the nearer
  float nearest = -1e30f;
  for (const auto &kv : this->edges) {
    if (kv.second.tank < 0)
      continue;
    const Exhibit *x = this->exhibit(kv.second.tank);
    if (!x)
      continue;
    int ax, ay, bx, by, h0, h1;
    this->edgeGeometry(kv.first, kv.second, map, ax, ay, bx, by, h0, h1);
    // (a front one - its outside toward the viewer - only above the ground:
    // under it is the ground in front of the tank)
    const Edge &e = kv.first;
    int ox = e.alongX ? e.x : e.x - 1, oy = e.alongX ? e.y - 1 : e.y;
    if (x->tiles.count({ox, oy}))
      ox = e.x, oy = e.y;
    int ix = ox == e.x && oy == e.y ? (e.alongX ? e.x : e.x - 1) : e.x;
    int iy = ox == e.x && oy == e.y ? (e.alongX ? e.y - 1 : e.y) : e.y;
    float tx, ty, outD, inD;
    view.worldToScreenF(ox + 0.5f, oy + 0.5f, 0, tx, ty, outD);
    view.worldToScreenF(ix + 0.5f, iy + 0.5f, 0, tx, ty, inD);
    const float low = static_cast<float>(outD > inD ? x->groundHeight : x->floorHeight);
    float s0x, s0y, s1x, s1y, t0x, t0y, t1x, t1y, d0, d1;
    view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), low, s0x, s0y, d0);
    view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), low, s1x, s1y, d1);
    view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), x->wallTop(), t0x, t0y, d0);
    view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), x->wallTop(), t1x, t1y, d1);
    if (std::fabs(s1x - s0x) < 0.5f || px < std::min(s0x, s1x) || px > std::max(s0x, s1x))
      continue;
    float f = (px - s0x) / (s1x - s0x);
    float top = t0y + (t1y - t0y) * f - 4.0f, bottom = s0y + (s1y - s0y) * f;
    if (py < top || py > bottom)
      continue;
    float depth = (d0 + d1) * 0.5f;
    if (depth > nearest) {
      nearest = depth;
      found = kv.first;
      any = true;
    }
  }
  if (any)
    return true;
  for (const auto &kv : this->edges) {
    int ax, ay, bx, by, h0, h1;
    this->edgeGeometry(kv.first, kv.second, map, ax, ay, bx, by, h0, h1);
    float h = (h0 + h1) * 0.5f + 1.0f; // about half way up a fence
    if (kv.second.tank >= 0)
      for (const Exhibit &x : this->exhibitList)
        if (x.id == kv.second.tank)
          h = x.wallTop() - 0.25f;
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

// A tank wall's edge: the tank tile inside it, the way out, whether it's at
// the front (its outside toward the viewer, down-left or down-right: the
// tank is seen through it) and the ground just outside it
namespace {
struct TankSide {
  int ix = 0, iy = 0, dx = 0, dy = 0;
  bool front = false;
  int outside = 0;
};
} // namespace

static bool tankSide(const Fences::Exhibit &ex, const Fences::Edge &e, const WorldMap &map,
                     const WorldRenderer &view, TankSide &s) {
  int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
  bool aIn = ex.tiles.count({ax, ay}) > 0, bIn = ex.tiles.count({e.x, e.y}) > 0;
  if (aIn == bIn)
    return false;
  s.ix = aIn ? ax : e.x;
  s.iy = aIn ? ay : e.y;
  int ox = aIn ? e.x : ax, oy = aIn ? e.y : ay;
  s.dx = ox - s.ix;
  s.dy = oy - s.iy;
  CompassDirection side = view.screenSide(static_cast<float>(s.dx), static_cast<float>(s.dy));
  s.front = side == CompassDirection::SW || side == CompassDirection::SE;
  s.outside = ex.floorHeight;
  if (const MapTile *t = map.getTile(ox, oy)) {
    // The outside tile's corners along the edge
    int c0, c1;
    if (s.dx > 0) {
      c0 = CORNER_X0Y0; c1 = CORNER_X0Y1;
    } else if (s.dx < 0) {
      c0 = CORNER_X1Y0; c1 = CORNER_X1Y1;
    } else if (s.dy > 0) {
      c0 = CORNER_X0Y0; c1 = CORNER_X1Y0;
    } else {
      c0 = CORNER_X0Y1; c1 = CORNER_X1Y1;
    }
    s.outside = std::min(t->cornerHeight[c0], t->cornerHeight[c1]);
  }
  return true;
}

// The water's and glass's art sets for how clean a tank is (tanks.cfg
// murkyWaterPurity 60, veryMurky 20, extremelyMurky 1)
static std::string scumSuffix(float purity) {
  return purity < 1 ? "es" : purity < 20 ? "hs" : purity < 60 ? "ls" : "";
}

// How see-through the original draws a tank's water and glass
constexpr Uint8 kWaterAlpha = 120; // (the floor's sand shows through, as the original)
constexpr Uint8 kGlassAlpha = 96;

// A tank wall's pieces, bottom to top. A wall L units high has its bottom
// rail's piece at the floor and its top rail's two units under the top (that
// art reaches up to the rim, the posts' caps past it), the rows between
// glass only but for the posts; at two units high one piece has both rails
// ("low"). Its glass, a pane a unit (anchored at the unit's foot), from the
// floor to the top. The ends of a run of wall get the corner posts.
void Fences::tankPieces(const Edge &e, const Piece &p, const Exhibit &ex, const WorldRenderer &view,
                        bool front, std::vector<TankPiece> &out) const {
  const FenceType &t = this->typeList[p.type];
  int ax = e.x, ay = e.y, bx = e.alongX ? e.x + 1 : e.x, by = e.alongX ? e.y : e.y + 1;
  Edge prev = e, next = e;
  if (e.alongX) { prev.x--; next.x++; } else { prev.y--; next.y++; }
  auto isTankWall = [&](const Edge &n) {
    const Piece *q = this->at(n);
    return q && (q->tank == ex.id || q->tank2 == ex.id);
  };
  float s0x, s0y, s1x, s1y, d;
  view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), 0, s0x, s0y, d);
  view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), 0, s1x, s1y, d);
  bool prevLeft = s0x < s1x; // prev joins the edge at (ax, ay)
  bool left = isTankWall(prevLeft ? prev : next);
  bool right = isTankWall(prevLeft ? next : prev);
  // A back wall's art is drawn from inside the tank: its left and right
  // are the screen's the other way round
  if (!front)
    std::swap(left, right);
  int column = left && right ? 1 : right ? 0 : left ? 2 : 3;
  const int levels = ex.wallSub; // (subtiles: a piece every 8 px)
  const int rows = std::max(1, levels - 1);
  // (each a subtile under the one above it from the top: the top one's rail
  // reaches the walls' height - measured against the original at default,
  // flush, and one and two under the ground)
  // (the back walls' art, seen from inside, sits a subtile lower than the
  // front walls' seen from outside)
  const float drop = front ? 0.0f : 0.5f;
  for (int k = 0; k < rows; k++) {
    int level = rows == 1 ? -1 : k == 0 ? 0 : k == rows - 1 ? 2 : 1;
    Animation *a = level < 0 ? (t.tankLow[column] ? t.tankLow[column] : t.tankLow[1])
                             : t.tankArt[column][level];
    if (!a && level >= 0)
      a = t.tankArt[1][level];
    // The gate: the wall as everywhere, its ladder's rungs over it (as the
    // original: a back wall's on its inside face, a front wall's on its
    // outside face down to the ground)
    if (a)
      out.push_back({a, ex.floorHeight + k * 0.5f - drop, false, k == rows - 1});
    if (p.gate && t.ladder)
      out.push_back({t.ladder, ex.floorHeight + k * 0.5f - drop, false, k == rows - 1, true});
  }
  // The glass: "\" edges (facing down-left / up-right) and "/" ones
  bool falling = (s1y - s0y) * (s1x - s0x) > 0;
  Animation *glass = this->waterPart("glass" + scumSuffix(ex.purity),
                                     falling ? (front ? "fgrey" : "bgrey") : (front ? "rgrey" : "lgrey"));
  // (only as high as the water: the rail over it isn't tinted)
  float level = ex.level > -1e8f ? ex.level : ex.floorHeight + ex.water * (ex.wallTop() - 0.5f - ex.floorHeight);
  // (a pane a height unit tall, the top one down to the water's height -
  // a glass wall's up to its rail, as the original: over the water it
  // showed the sand floor through it, untinted)
  const float glassTop = t.seeThrough ? std::max(level, ex.wallTop() - 0.5f) : level;
  // (from the top down: an overlap under the ground, not a dark band)
  // (a solid wall's outside isn't tinted: only its inside, seen through
  // the water)
  if (glass && (t.seeThrough || !front))
    for (float h = glassTop - 1.0f; h > ex.floorHeight - 1.0f + 0.01f; h -= 1.0f)
      out.push_back({glass, h, true, false});
}

int Fences::drawnWith(const Edge &e, const Piece &p, const WorldMap &map, const WorldRenderer &view) const {
  if (p.tank2 < 0)
    return p.tank;
  for (int id : {p.tank, p.tank2}) {
    TankSide ts;
    const Exhibit *ex = this->exhibit(id);
    if (ex && tankSide(*ex, e, map, view, ts) && !ts.front)
      return id;
  }
  return p.tank;
}

// The way a tank wall's art faces: from where it is (in or out of the
// tank), not the way it happened to be dragged (one dragged the other way
// put its corner posts at the wrong ends)
static CompassDirection tankSide(const WorldRenderer &view, const TankSide &ts) {
  return view.screenSide(static_cast<float>(ts.dx), static_cast<float>(ts.dy));
}

static CompassDirection ladderSide(const WorldRenderer &view, const TankSide &ts) {
  return tankSide(view, ts);
}

int Fences::platformAt(float px, float py, const WorldRenderer &view) const {
  for (const auto &kv : this->edges) {
    const Piece &p = kv.second;
    if (!p.gate || p.tank < 0)
      continue;
    const Exhibit *ex = this->exhibit(p.tank);
    if (!ex || !this->typeList[p.type].platform)
      continue;
    const Edge &e = kv.first;
    float mx = e.alongX ? e.x + 0.5f : static_cast<float>(e.x);
    float my = e.alongX ? static_cast<float>(e.y) : e.y + 0.5f;
    TankSide ts;
    if (this->mapEdit && tankSide(*ex, e, *this->mapEdit, view, ts) && ts.front) {
      mx -= ts.dx * 0.5f;
      my -= ts.dy * 0.5f;
    }
    float sx, sy, d;
    view.worldToScreenF(mx, my, ex->wallTop() - 1.0f, sx, sy, d);
    // (the platform stands about 20 px over its anchor, the ladder under)
    if (std::fabs(px - sx) <= 16.0f && py >= sy - 34.0f && py <= sy + 6.0f)
      return ex->id;
  }
  return -1;
}

void Fences::collect(const WorldRenderer &view, const WorldMap &map,
                     std::vector<Drawable> &out) const {
  auto add = [&](const Edge &e, const Piece &p, int type, bool tinted, SDL_Color tint) {
    const FenceType &t = this->typeList[type];
    if (p.gate && !tinted && e == this->hoverGate) {
      tinted = true;
      tint = SDL_Color{255, 255, 110, 255};
    }
    int ax, ay, bx, by, h0, h1;
    this->edgeGeometry(e, p, map, ax, ay, bx, by, h0, h1);
    float angle = p.facing * 3.14159265f / 4.0f;
    CompassDirection side = view.screenSide(std::sin(angle), -std::cos(angle));
    float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;

    // A tank's walls: stacked from its floor to the top. A front one (the
    // tank seen through it) here, sorted with everything else, from the
    // ground outside it up (its top rail always); what's under that, and
    // the back ones, go with the tank's inside, under the ground in front
    // and the water. Its gate is wall too, the diver platform on it (the
    // ladder's with the inside).
    if (p.tank >= 0) {
      const Exhibit *ex = this->exhibit(this->drawnWith(e, p, map, view));
      TankSide ts;
      if (ex && t.tankArt[1][0] && tankSide(*ex, e, map, view, ts)) {
        const float top = ex->wallTop();
        // The ground line along its outside: what's under it isn't seen (as
        // the original: the ground in front hides the wall, the posts too)
        float g0x, g0y, g1x, g1y, gd;
        view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(ts.outside), g0x, g0y, gd);
        view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), static_cast<float>(ts.outside), g1x, g1y, gd);
        if (g0x > g1x) {
          std::swap(g0x, g1x);
          std::swap(g0y, g1y);
        }
        // (past its ends: at the tank's front corner the ground turns back
        // up along the other front wall - the corner post cut there too; at
        // a side corner it stays level: never lower than the end, or
        // slivers of the posts showed in the grass)
        auto groundY = [&](float x) {
          float slope = (g1y - g0y) / std::max(0.001f, g1x - g0x);
          if (x <= g0x)
            return std::min(g0y, g0y - slope * (x - g0x));
          if (x >= g1x)
            return std::min(g1y, g1y - slope * (x - g1x));
          return g0y + slope * (x - g0x);
        };
        // (drawn in narrow strips, each cut at the ground under it)
        // (only across the art's own width: across the whole wall, every
        // piece by the ground made thousands of strips - a big raised tank
        // slowed the game to 20 frames a second)
        // (only a piece that crosses the ground line is cut, in narrow
        // strips across its own width - across the whole wall, every piece
        // by the ground made thousands of them, a big raised tank slowing the
        // game to 20 frames a second; one wholly over the line is drawn
        // whole, one wholly under it not at all)
        auto pushCut = [&](const Drawable &d) {
          float L, T, R, B;
          if (!d.art || !d.art->anchoredBounds(d.sx, d.sy, d.side, L, T, R, B)) {
            out.push_back(d);
            return;
          }
          float lo = 1e9f, hi = -1e9f;
          for (float x : {L, R, std::clamp(g0x, L, R), std::clamp(g1x, L, R)}) {
            lo = std::min(lo, groundY(x));
            hi = std::max(hi, groundY(x));
          }
          if (B <= lo) {
            out.push_back(d);
            return;
          }
          if (T >= hi)
            return;
          const float step = 4.0f; // (the line moves 2 px in 4: cut within a pixel)
          for (float x = std::floor(L); x < R; x += step) {
            Drawable strip = d;
            strip.clip = true;
            strip.clipX0 = x;
            strip.clipX1 = x + step;
            strip.clipY1 = groundY(x + step * 0.5f);
            if (strip.clipY1 > T)
              out.push_back(strip);
          }
        };
        if (p.gate && t.platform) {
          // Facing into the tank (its art is named a quarter turn round from
          // the way it faces: NE, SE, SW, NW in turn)
          // (its art lines up with a wall facing its way: the wall's own)
          CompassDirection in = tankSide(view, ts);
          float sx, sy, depth;
          // With the top rail's piece, two units under the rim. On a front
          // wall, inside the tank behind its rail (as the original: only
          // the handles show over it); on a back one its art already sits
          // inside.
          float px = mx, py = my;
          const float sh = 0.0f, lf = 0.5f;
          float lift = 0.0f;
          // (checked in all four turns of the view: facing up-right, its NE
          // art puts the floor along the wall; the SE art lies inside)
          if (in == CompassDirection::NE)
            in = CompassDirection::SE;
          if (ts.front) {
            // On a front wall: just inside it, up at its rim, behind its
            // rail (as the original: its floor hidden, the handles over the
            // rim)
            px -= ts.dx * sh;
            py -= ts.dy * sh;
            lift = lf;
          }
          view.worldToScreenF(px, py, top - 1.0f + lift, sx, sy, depth);
          // (its handles over the ladder's rails: on a front wall they stood
          // 7 px along from them, the ladder not meeting them)
          if (ts.front) {
            const float nudge = 7.0f;
            sx += view.screenSide(static_cast<float>(ts.dx), static_cast<float>(ts.dy)) == CompassDirection::SW ? -nudge
                                                                                                                : nudge;
          }
          float wx, wy, wallDepth;
          view.worldToScreenF(mx, my, static_cast<float>(top - 2), wx, wy, wallDepth);
          bool lit = !tinted && this->hoverPlatform == ex->id;
          SDL_Color yellow{255, 255, 80, 255};
          Drawable plat{ts.front ? wallDepth - 0.05f : wallDepth + 0.1f, sx, sy, t.platform, in,
                        lit ? yellow : tint, tinted || lit};
          // (on a front wall down by the ground: the ground in front hides
          // it, only what's over the ground shows - the handles' tips)
          if (ts.front && top < ts.outside + 1.0f)
            pushCut(plat);
          else
            out.push_back(plat);
          if (!tinted && this->selectedTank == ex->id && this->selectArrow)
            out.push_back({wallDepth + 0.101f, sx, sy - 40.0f, this->selectArrow, CompassDirection::N,
                           SDL_Color{255, 255, 255, 255}, false});
        }
        if (!ts.front && !tinted)
          return;
        std::vector<TankPiece> pieces;
        this->tankPieces(e, p, *ex, view, ts.front, pieces);
        side = tankSide(view, ts);
        for (const TankPiece &tp : pieces) {
          // (one whose art doesn't reach over the ground outside: with the
          // inside only, hidden by the ground in front)
          if (tinted ? tp.glass : tp.height + 1.75f <= ts.outside)
            continue;
          float sx, sy, depth;
          view.worldToScreenF(mx, my, tp.height, sx, sy, depth);
          // (the wall from the floor up; then the water's tint over all of it,
          // then the ladder's rungs - in among the wall's pieces, the ones above
          // cut them into bands, moving as the water rose)
          float order = depth + (tp.glass ? 0.02f : tp.ladder ? 0.03f : 0.0f) +
                        (tp.height - ex->floorHeight) * ((tp.glass || tp.ladder) ? 0.0001f : 0.001f);
          bool litLadder = tp.ladder && !tinted && this->hoverPlatform == ex->id;
          Drawable d = tp.glass ? Drawable{order, sx, sy, tp.art, CompassDirection::N,
                                           SDL_Color{255, 255, 255, kGlassAlpha}, true}
                                : Drawable{order, sx, sy, tp.art, tp.ladder ? ladderSide(view, ts) : side,
                                           litLadder ? SDL_Color{255, 255, 80, 255} : tint, tinted || litLadder};
          if (tinted || tp.height >= ts.outside + 0.01f) {
            out.push_back(d);
            continue;
          }
          pushCut(d);
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
    Drawable d{depth, sx, sy, art, side, tint, tinted};
    // (a gate: as far open as it is)
    if (p.gate)
      d.frame = static_cast<int>(p.open);
    // In two halves, each sorted at its own middle: a building's strips
    // are a quarter tile wide, sorted the same way, so one standing against
    // the fence goes behind all of it (sorted as a whole at its middle,
    // every other strip of the wall came over the fence)
    float s0x, s0y, d0, s1x, s1y, d1;
    view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(h0), s0x, s0y, d0);
    view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), static_cast<float>(h1), s1x, s1y, d1);
    if (s0x > s1x) {
      std::swap(s0x, s1x);
      std::swap(d0, d1);
    }
    Drawable left = d, right = d;
    left.clip = right.clip = true;
    left.depth = (3 * d0 + d1) / 4;
    left.clipX0 = -1e9f;
    left.clipX1 = sx;
    right.depth = (d0 + 3 * d1) / 4;
    right.clipX0 = sx;
    right.clipX1 = 1e9f;
    out.push_back(left);
    out.push_back(right);
  };

  // Each tank's inside: its back walls from the floor up (glazed), the
  // diver's ladder down into it, then the water over them, see-through:
  // the surface at the water's height, its sides behind the front glass.
  // Sorted at the back of the tank.
  for (const Exhibit &ex : this->exhibitList) {
    if (!ex.tank)
      continue;
    std::string set = (ex.salt ? "salt" : "fresh") + scumSuffix(ex.purity);
    Animation *surfaceArt = this->waterPart(set, "top");
    if (!surfaceArt)
      continue; // (drawn plain in drawWater)
    struct Sprite {
      float depth, sx, sy;
      Animation *art;
      CompassDirection side;
      SDL_Color tint;
      bool tinted;
      int frame = -1;
    };
    std::vector<Sprite> back, water, frontLow;
    // Its edges (for the waterline and the outline at the ground)
    struct Rim {
      float ax, ay, bx, by;
      bool front;
      int outside;
    };
    std::vector<Rim> rims;
    float backDepth = 1e30f;
    for (const auto &kv : this->edges) {
      const Piece &p = kv.second;
      if (p.tank != ex.id && p.tank2 != ex.id)
        continue;
      // (a wall shared with the tank next door: drawn once, with the tank
      // it's the back wall of)
      if (this->drawnWith(kv.first, p, map, view) != ex.id)
        continue;
      const FenceType &t = this->typeList[p.type];
      TankSide ts;
      if (!t.tankArt[1][0] || !tankSide(ex, kv.first, map, view, ts))
        continue;
      const Edge &e = kv.first;
      float mx = e.alongX ? e.x + 0.5f : static_cast<float>(e.x);
      float my = e.alongX ? static_cast<float>(e.y) : e.y + 0.5f;
      float sx, sy, depth;
      view.worldToScreenF(mx, my, static_cast<float>(ex.floorHeight), sx, sy, depth);
      backDepth = std::min(backDepth, depth);
      rims.push_back({static_cast<float>(e.x), static_cast<float>(e.y),
                      e.alongX ? e.x + 1.0f : static_cast<float>(e.x),
                      e.alongX ? static_cast<float>(e.y) : e.y + 1.0f, ts.front, ts.outside});
      CompassDirection side = tankSide(view, ts);
      std::vector<TankPiece> pieces;
      this->tankPieces(e, p, ex, view, ts.front, pieces);
      for (const TankPiece &tp : pieces) {
        // (a front wall: drawn with everything else, cut at the ground; what's
        // under the ground isn't drawn at all, as the original - drawn here
        // under the ground in front, its top showed through a glass wall as
        // a band of concrete)
        if (ts.front)
          continue;
        float rx, ry, rd;
        view.worldToScreenF(mx, my, tp.height, rx, ry, rd);
        // (the wall from the floor up; then the water's tint over all of it,
          // then the ladder's rungs - in among the wall's pieces, the ones above
          // cut them into bands, moving as the water rose)
          float order = depth + (tp.glass ? 0.02f : tp.ladder ? 0.03f : 0.0f) +
                        (tp.height - ex.floorHeight) * ((tp.glass || tp.ladder) ? 0.0001f : 0.001f);
        Sprite sp = tp.glass ? Sprite{order, rx, ry, tp.art, CompassDirection::N,
                                      SDL_Color{255, 255, 255, kGlassAlpha}, true}
                             : Sprite{order, rx, ry, tp.art, tp.ladder ? ladderSide(view, ts) : side,
                                      SDL_Color{255, 255, 80, 255},
                                      tp.ladder && this->hoverPlatform == ex.id};
        (ts.front ? frontLow : back).push_back(sp);
      }
    }
    if (backDepth > 1e29f)
      continue;
    std::stable_sort(back.begin(), back.end(),
                     [](const Sprite &p, const Sprite &q) { return p.depth < q.depth; });
    // The water: up to a unit under the walls' top (tanks.cfg waterOffset
    // -1), as it's shown rising
    float level = ex.level > -1e8f ? ex.level
                                   : ex.floorHeight + ex.water * (ex.wallTop() - 0.5f - ex.floorHeight);
    if (ex.water > 0 && level > ex.floorHeight + 0.01f) {
      const bool scum = !scumSuffix(ex.purity).empty();
      auto variant = [&](const std::string &part, const std::string &scumPart, int x, int y) {
        if (!scum)
          return this->waterPart(set, part);
        unsigned h = static_cast<unsigned>(x * 73856093) ^ static_cast<unsigned>(y * 19349663);
        int n = h % 4;
        Animation *a = n == 0 ? nullptr : this->waterPart(set, scumPart + std::to_string(n));
        return a ? a : this->waterPart(set, part);
      };
      const SDL_Color see{255, 255, 255, kWaterAlpha};
      for (auto [x, y] : ex.tiles) {
        float cx, cy, cd;
        view.worldToScreenF(x + 0.5f, y + 0.5f, level, cx, cy, cd);
        // Its sides on the front edges, from a unit under the ground there
        const int dd[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto &o : dd) {
          if (ex.tiles.count({x + o[0], y + o[1]}))
            continue;
          CompassDirection side = view.screenSide(static_cast<float>(o[0]), static_cast<float>(o[1]));
          bool left = side == CompassDirection::SW, right = side == CompassDirection::SE;
          if (!left && !right)
            continue;
          Animation *face = variant(left ? "front" : "right", left ? "fscum" : "rscum", x, y);
          if (!face)
            continue;
          Edge e = o[0] ? Edge{false, x + (o[0] > 0 ? 1 : 0), y} : Edge{true, x, y + (o[1] > 0 ? 1 : 0)};
          (void)e;
          // A unit a piece, anchored at its foot, from the water's height
          // down (stacked from the floor up, the top one overlapped the one
          // under it, see-through twice: a dark band; two units apart they
          // left gaps of sand); what the ground hides is drawn over again
          float mx = x + 0.5f + o[0] * 0.5f, my = y + 0.5f + o[1] * 0.5f;
          for (float h = level - 1.0f; h > ex.floorHeight - 1.0f + 0.01f; h -= 1.0f) {
            float sx, sy, dep;
            view.worldToScreenF(mx, my, h, sx, sy, dep);
            water.push_back({cd + 0.25f, sx, sy, face, CompassDirection::N, see, true});
          }
        }
        if (Animation *surface = variant("top", "tscum", x, y))
          water.push_back({cd, cx, cy, surface, CompassDirection::N, see, true});
        // The ripple along each wall on the surface (tankripl: edgrip, 19
        // frames of 83 ms, each started 0-2 s in; clean water only)
        if (ex.purity >= 60)
          if (Animation *rip = this->objectArt("objects/edgrip/idle/idle"))
            for (auto &o : dd) {
              if (ex.tiles.count({x + o[0], y + o[1]}))
                continue;
              float ex2, ey2, ed;
              view.worldToScreenF(x + 0.5f + o[0] * 0.5f, y + 0.5f + o[1] * 0.5f, level, ex2, ey2, ed);
              unsigned h = static_cast<unsigned>(x * 92821 + y * 68917 + (o[0] + 2) * 131 + (o[1] + 2) * 17);
              int frame = static_cast<int>((this->rippleClock + static_cast<float>(h % 2000)) / 83.0f) % 19;
              Sprite sp{cd + 0.3f, ex2, ey2, rip, view.screenSide(static_cast<float>(o[0]), static_cast<float>(o[1])),
                        SDL_Color{255, 255, 255, 255}, false};
              sp.frame = frame;
              water.push_back(sp);
            }
      }
      // Waves: salt twaterwv (waterwv1, 125 ms a frame), fresh frshwav (83 ms)
      if (ex.purity >= 60)
        if (Animation *wv = this->objectArt(ex.salt ? "objects/waterwv1/idle/idle" : "objects/frshwav/idle/idle"))
          for (const Exhibit::Wave &w : ex.waves) {
            int frames = std::max(1, wv->frameCount());
            int frame = static_cast<int>(w.age / (ex.salt ? 125.0f : 83.0f));
            if (frame >= frames)
              continue; // (played once)
            float wx, wy, wd;
            view.worldToScreenF(w.x + 0.5f, w.y + 0.5f, level, wx, wy, wd);
            static const CompassDirection four[4] = {CompassDirection::NE, CompassDirection::SE, CompassDirection::SW,
                                                     CompassDirection::NW};
            Sprite sp{wd + 0.3f, wx, wy, wv, four[(w.facing / 2) % 4], SDL_Color{255, 255, 255, 255}, false};
            sp.frame = frame;
            water.push_back(sp);
          }
      std::stable_sort(water.begin(), water.end(),
                       [](const Sprite &p, const Sprite &q) { return p.depth < q.depth; });
    }
    // The ground in front of the tank (as far as the pit's depth reaches
    // down the screen), drawn again over the pit: it hides the walls, the
    // ladder and the water below its rim
    // (in front: below one of the tank's tiles in its screen column or the
    // next - not merely nearer than its back corner, which took in the
    // ground behind its back walls on a big tank and drew it over them)
    std::set<std::pair<int, int>> cover;
    {
      struct Spot {
        float sx, d;
      };
      std::vector<Spot> spots;
      for (auto [x, y] : ex.tiles) {
        float sx, sy, d;
        view.worldToScreenF(x + 0.5f, y + 0.5f, 0, sx, sy, d);
        spots.push_back({sx, d});
      }
      // (a little wider than a tile's half: the corner posts at the tank's
      // sides reach past its last column)
      const float column = view.getTileWidth() * 0.75f + 1.0f;
      const int reach = std::max(1, ex.groundHeight - ex.floorHeight) + 1;
      for (auto [x, y] : ex.tiles)
        for (int dy = -reach; dy <= reach; dy++)
          for (int dx = -reach; dx <= reach; dx++) {
            int nx = x + dx, ny = y + dy;
            if (ex.tiles.count({nx, ny}) || cover.count({nx, ny}) || !map.getTile(nx, ny))
              continue;
            float sx, sy, d;
            view.worldToScreenF(nx + 0.5f, ny + 0.5f, 0, sx, sy, d);
            for (const Spot &s : spots)
              if (std::fabs(s.sx - sx) <= column && d > s.d) {
                cover.insert({nx, ny});
                break;
              }
          }
    }
    Drawable inside{backDepth - 0.01f, 0, 0, nullptr, CompassDirection::N, SDL_Color{255, 255, 255, 255}, false};
    std::stable_sort(frontLow.begin(), frontLow.end(),
                     [](const Sprite &p, const Sprite &q) { return p.depth < q.depth; });
    // (the waterline is the ripples' art, with the water; here only the
    // thin line where the front glass meets the ground)
    const bool wet = ex.water > 0 && level > ex.floorHeight + 0.01f;
    const WorldRenderer *vp = &view;
    // In vertical strips across the screen, each sorted at the tank's back
    // wall in that column (as one piece at its back corner, a tank beside
    // it came wrongly in front of or behind its walls); each strip draws
    // only what reaches into it
    struct Strip {
      float x0, x1, depth;
      float y1 = 1e9f; // (nothing under this row)
    };
    std::vector<Strip> strips;
    {
      float minX = 1e30f, maxX = -1e30f, minY = 0, maxY = 0, minD = 0, maxD = 0;
      for (const Rim &m : rims)
        for (int k = 0; k < 2; k++) {
          float sx, sy, d;
          view.worldToScreenF(k ? m.bx : m.ax, k ? m.by : m.ay, static_cast<float>(ex.groundHeight), sx, sy, d);
          if (sx < minX) {
            minX = sx;
            minY = sy;
            minD = d;
          }
          if (sx > maxX) {
            maxX = sx;
            maxY = sy;
            maxD = d;
          }
        }
      // (a tile wide: narrower, every sprite drawn in more of them - a
      // frame took twice as long with four big tanks)
      const float step = view.getTileWidth();
      for (float x = minX; x < maxX - 0.5f; x += step) {
        float mid = std::min(x + step * 0.5f, maxX), depth = 1e30f;
        for (const Rim &m : rims) {
          if (m.front)
            continue;
          float ax, ay, da, bx, by, db;
          view.worldToScreenF(m.ax, m.ay, 0, ax, ay, da);
          view.worldToScreenF(m.bx, m.by, 0, bx, by, db);
          if (mid < std::min(ax, bx) - 0.5f || mid > std::max(ax, bx) + 0.5f || std::fabs(bx - ax) < 0.01f)
            continue;
          depth = std::min(depth, da + (db - da) * (mid - ax) / (bx - ax));
        }
        if (depth > 1e29f)
          depth = backDepth;
        strips.push_back({x, std::min(x + step, maxX), depth - 0.01f});
      }
      // (past its side corners, only what's over the ground there: its
      // corner posts above it - under it they're hidden, the tank not seen
      // through the ground; reaching out under it, specks of the posts
      // showed in the grass beside a sunk tank)
      if (!strips.empty()) {
        strips.insert(strips.begin(), {-1e9f, minX, minD - 0.01f, minY});
        strips.push_back({maxX, 1e9f, maxD - 0.01f, maxY});
      }
    }
    auto bounds = [](const Sprite &s, float &l, float &rr) {
      float t, b;
      if (!s.art->anchoredBounds(s.sx, s.sy, s.side, l, t, rr, b)) {
        l = s.sx - 64;
        rr = s.sx + 64;
      }
    };
    auto within = [&](const std::vector<Sprite> &all, const Strip &st) {
      std::vector<Sprite> some;
      for (const Sprite &sp : all) {
        float l, rr;
        bounds(sp, l, rr);
        if (rr > st.x0 && l < st.x1)
          some.push_back(sp);
      }
      return some;
    };
    for (const Strip &st : strips) {
      std::vector<Sprite> back1 = within(back, st), water1 = within(water, st), front1 = within(frontLow, st);
      std::set<std::pair<int, int>> cover1;
      for (auto [cx, cy] : cover) {
        float sx, sy, d;
        view.worldToScreenF(cx + 0.5f, cy + 0.5f, 0, sx, sy, d);
        if (sx + view.getTileWidth() * 0.5f > st.x0 && sx - view.getTileWidth() * 0.5f < st.x1)
          cover1.insert({cx, cy});
      }
      Drawable part = inside;
      part.depth = st.depth;
      part.clip = true;
      part.clipX0 = st.x0;
      part.clipX1 = st.x1;
      part.clipY1 = st.y1;
      part.custom = [this, back = back1, water = water1, frontLow = front1, cover = cover1, rims, wet, level,
                     vp](SDL_Renderer *r) {
      for (const Sprite &s : back)
        s.art->drawAnchored(r, s.sx, s.sy, s.side, s.tinted ? &s.tint : nullptr);
      for (const Sprite &s : water)
        s.art->drawAnchored(r, s.sx, s.sy, s.side, s.tinted ? &s.tint : nullptr, s.frame);
      (void)wet;
      (void)level;
      for (const Sprite &s : frontLow)
        s.art->drawAnchored(r, s.sx, s.sy, s.side, s.tinted ? &s.tint : nullptr);
      if (this->redrawGround)
        this->redrawGround(r, cover);
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(r, 176, 96, 40, 230);
      for (const Rim &m : rims) {
        if (!m.front)
          continue;
        float x0, y0, x1, y1, d;
        vp->worldToScreenF(m.ax, m.ay, static_cast<float>(m.outside), x0, y0, d);
        vp->worldToScreenF(m.bx, m.by, static_cast<float>(m.outside), x1, y1, d);
        SDL_RenderDrawLineF(r, x0, y0, x1, y1);
      }
      };
      out.push_back(part);
    }
  }

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
    bool red = this->highlightFilter == static_cast<int>(&f - this->filterList.data());
    addFilter(f, a ? a : fk.idle, red, red ? SDL_Color{255, 60, 60, 255} : SDL_Color{255, 255, 255, 255});
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

Animation *Fences::objectArt(const std::string &path) const {
  auto it = this->waterArt.find(path);
  if (it != this->waterArt.end())
    return it->second;
  Animation *a = this->rm && this->rm->hasResource(path + ".ani") ? this->rm->getAnimation(path) : nullptr;
  this->waterArt[path] = a;
  return a;
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
    float level = ex.floorHeight + ex.water * (ex.wallTop() - 0.5f - ex.floorHeight);
    std::string set = ex.salt ? "salt" : "fresh";
    if (ex.purity < 1)
      set += "es";
    else if (ex.purity < 20)
      set += "hs";
    else if (ex.purity < 60)
      set += "ls";
    const bool scum = set.size() > (ex.salt ? 4u : 5u);
    Animation *top = this->waterPart(set, "top");
    if (top)
      continue; // (with the art: the tank's inside, in collect)
    {
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
  }
}
