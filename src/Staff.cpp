#include "Staff.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

#include "IniReader.hpp"
#include "Pathfinder.hpp"
#include "Features.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"

namespace {
constexpr float kIdleSeconds = 2.0f;   // fPlayTime(idle,2)
constexpr int kWanderRadius = 5;       // tiles, for a walk about
// Lang strings: what they're doing
constexpr int kMonitoring = 10304, kGuideMonitoring = 10402, kMaintMonitoring = 10506;
constexpr int kGoingToFix = 10509, kFixing = 10510;
constexpr int kGoingToFilter = 10515, kServicing = 10516;
constexpr int kWaitingAtBase = 10316;
constexpr int kPlacingFood = 10300, kRaking = 10302, kGoingTo = 10303, kEntering = 10307,
              kExiting = 10306, kCleaningTank = 10327;
constexpr int kGoingToSweep = 10500, kSweeping = 10501;
constexpr float kCleanTankPct = 30.0f; // cCleanTankPct

std::string stripAni(std::string p) {
  p = Utils::string_to_lower(p);
  size_t dot = p.rfind(".ani");
  return dot == std::string::npos ? p : p.substr(0, dot);
}
} // namespace

// ----------------------------------------------------------------------------
// Types
// ----------------------------------------------------------------------------
void Staff::loadTypes(ResourceManager *rm) {
  this->rm = rm;
  if (!this->typeList.empty() || !rm)
    return;
  if (rm->hasResource("ui/select/selsmall/selsmall.ani"))
    this->selectArrow = rm->getAnimation("ui/select/selsmall/selsmall");
  for (const std::string &cfgName : {"staff.cfg", "staff1.cfg", "staff2.cfg"}) {
    IniReader *cfg = rm->getIniReader(cfgName);
    if (!cfg)
      continue;
    for (const auto &kv : cfg->getSection("staff")) {
      IniReader *ai = rm->getIniReader(kv.second);
      if (!ai)
        continue;
      Type t;
      t.file = Utils::string_to_lower(kv.second);
      t.key = kv.first;
      std::string type = Utils::string_to_lower(ai->get("global", "type", kv.first));
      t.kind = type == "maint"        ? Kind::Maint
               : type == "tour"       ? Kind::Guide
               : type == "scient"     ? Kind::Scientist
               : type == "trainer"    ? Kind::Trainer
               : type == "helicopter" ? Kind::Helicopter
                                      : Kind::Keeper;
      std::string ints = t.kind == Kind::Helicopter ? "characteristics/integers"
                                                    : "m/characteristics/integers";
      t.nameId = ai->getInt(ints, "cnameid", 0);
      t.salary = ai->getInt(ints, "cpurchasecost", ai->getInt(ints, "cmonthlycost", 0));
      t.dutiesText = ai->getInt(ints, "cdutiestextid", 0);
      // The subtypes it comes in
      std::vector<std::string> subs;
      for (const auto &s : cfg->getSection(kv.first + "/subtypes"))
        subs.push_back(Utils::string_to_lower(s.first));
      t.sexes[0] = subs.empty() || std::find(subs.begin(), subs.end(), "m") != subs.end();
      t.sexes[1] = std::find(subs.begin(), subs.end(), "f") != subs.end();
      // Its animations: "walk = walk" (staff/<type>/<sub>/walk/walk) or a
      // whole path
      for (int sex = 0; sex < 2; sex++) {
        const char *sub = sex == 0 ? "m" : "f";
        std::string section = t.kind == Kind::Helicopter ? "animations"
                                                         : std::string(sub) + "/animations";
        for (const auto &a : ai->getSection(section)) {
          std::string v = Utils::string_to_lower(a.second);
          if (v.empty())
            continue;
          std::string path = v.find('/') != std::string::npos
                                 ? stripAni(v)
                                 : "staff/" + type + "/" + sub + "/" + v + "/" + v;
          t.anims[sex][Utils::string_to_lower(a.first)] = path;
        }
        std::string strs = std::string(sub) + "/characteristics/strings";
        t.listImage[sex] = Utils::string_to_lower(ai->get(strs, "clistimagename"));
        t.infoImage[sex] = Utils::string_to_lower(ai->get(strs, "cinfoimagename"));
      }
      if (t.listImage[1].empty())
        t.listImage[1] = t.listImage[0];
      // Men and women: whichever has its walking art (the subtype lists,
      // bare "m" / "f" lines, aren't key = value)
      for (int sex = 0; sex < 2; sex++) {
        auto w = t.anims[sex].find(t.kind == Kind::Helicopter ? "idle" : "walk");
        t.sexes[sex] = w != t.anims[sex].end() && rm->hasResource(w->second + ".ani");
      }
      if ((!t.sexes[0] && !t.sexes[1]) || t.kind == Kind::Helicopter) {
        t.sexes[0] = true;
        t.sexes[1] = false;
      }
      // Colour replacement: hair and skin palettes swapped in
      t.fullPal = Utils::string_to_lower(ai->get("cr_color", "fullpal"));
      t.hair = ai->getList("cr_hair", "pal");
      t.skin = ai->getList("cr_skin", "pal");
      delete ai;
      if (t.kind == Kind::Helicopter) {
        t.baseFile = "scenery/building/helibase.ai";
        if (IniReader *b = rm->getIniReader(t.baseFile)) {
          t.footX = std::max(1, (b->getInt("characteristics/integers", "cfootprintx", 10) + 1) / 2);
          t.footY = std::max(1, (b->getInt("characteristics/integers", "cfootprinty", 8) + 1) / 2);
          delete b;
        }
        if (rm->hasResource("objects/helibase/idle/idle.ani"))
          t.base = rm->getAnimation("objects/helibase/idle/idle");
      }
      this->typeList.push_back(t);
    }
    delete cfg;
  }
  SDL_Log("[Staff] %zu staff types", this->typeList.size());
}

int Staff::typeOfFile(const std::string &file) const {
  std::string f = Utils::string_to_lower(file);
  for (size_t i = 0; i < this->typeList.size(); i++)
    if (this->typeList[i].file == f || (!this->typeList[i].baseFile.empty() &&
                                        this->typeList[i].baseFile == f))
      return static_cast<int>(i);
  return -1;
}

void Staff::clear() {
  this->list.clear();
  this->counts.clear();
  this->selected = this->hovered = this->carried = -1;
}

Staff::Member *Staff::member(int id) {
  for (Member &m : this->list)
    if (m.id == id)
      return &m;
  return nullptr;
}

const Staff::Member *Staff::member(int id) const {
  return const_cast<Staff *>(this)->member(id);
}

int Staff::monthlyWages() const {
  int total = 0;
  for (const Member &m : this->list)
    total += this->typeList[m.type].salary;
  return total;
}

// ----------------------------------------------------------------------------
// Art: each member's animations in its own colours
// ----------------------------------------------------------------------------
Animation *Staff::art(const Member &m, const std::string &name) const {
  const Type &t = this->typeList[m.type];
  int sex = m.female && t.sexes[1] ? 1 : 0;
  auto it = t.anims[sex].find(name);
  if (it == t.anims[sex].end()) {
    it = t.anims[sex].find("idle");
    if (it == t.anims[sex].end())
      return nullptr;
  }
  const std::string &path = it->second;
  bool recolor = !t.fullPal.empty() && !t.hair.empty() && !t.skin.empty();
  std::string key = path + (recolor ? "#" + std::to_string(m.hair) + "_" + std::to_string(m.skin) : "");
  auto c = this->artCache.find(key);
  if (c != this->artCache.end())
    return c->second;
  Animation *a = nullptr;
  if (this->rm && this->rm->hasResource(path + ".ani")) {
    if (recolor) {
      // The full palette with this member's hair (entries 224-239) and skin
      // (240-255) from their 16-colour palettes (whose entry 0 is unused)
      PalletManager *pm = this->rm->getPalletManager();
      std::string full = t.fullPal, hair = t.hair[m.hair % t.hair.size()],
                  skin = t.skin[m.skin % t.skin.size()];
      Pallet *base = pm->getPallet(full);
      Pallet *hp = pm->getPallet(hair);
      Pallet *sp = pm->getPallet(skin);
      if (base) {
        Pallet variant = *base;
        for (int i = 0; i < 16; i++) {
          if (hp && hp->color_count > static_cast<uint32_t>(i + 1))
            variant.colors[224 + i] = hp->colors[i + 1];
          if (sp && sp->color_count > static_cast<uint32_t>(i + 1))
            variant.colors[240 + i] = sp->colors[i + 1];
        }
        std::string name = full + "#" + std::to_string(m.hair) + "_" + std::to_string(m.skin);
        pm->addPallet(name, variant);
        PalletManager::setOverride(full, name);
      }
      a = this->rm->getAnimation(path);
      PalletManager::clearOverrides();
    } else {
      a = this->rm->getAnimation(path);
    }
  }
  this->artCache[key] = a;
  return a;
}

Animation *Staff::preview(int type) const {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return nullptr;
  Member m;
  m.type = type;
  return this->art(m, "idle");
}

float Staff::animSeconds(const Member &m, const std::string &anim) const {
  Animation *a = this->art(m, anim);
  if (!a)
    return 1.0f;
  float ms = a->frameTimeMs() ? static_cast<float>(a->frameTimeMs()) : 100.0f;
  return std::max(1, a->frameCount()) * ms / 1000.0f;
}

// ----------------------------------------------------------------------------
// Ground: where staff can walk
// ----------------------------------------------------------------------------
float Staff::groundAt(const WorldMap &map, float x, float y) const {
  const MapTile *t = map.getTile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  if (!t)
    return 0;
  float fx = x - std::floor(x), fy = y - std::floor(y);
  float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
  float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
  return top * (1 - fy) + bottom * fy;
}

// Zoo ground: inside the main wall, not water, not in an exhibit, not
// under a building or a tank filter
bool Staff::walkable(int x, int y, const WorldMap &map, const Fences &fences, int access) const {
  // Rocks, trees, buildings, filters, bases stand in the way
  if (this->index && this->index->blocked(x, y))
    return false;
  int ex = fences.exhibitAt(x, y);
  if (ex >= 0) {
    // In an exhibit only on a visit to it
    if (ex != access)
      return false;
    const MapTile *t = map.getTile(x, y);
    return t && t->terrainType != 9 && t->terrainType != 10;
  }
  if (fences.tileFit(x, y, map) != Fences::Fit::Ok)
    return false;
  const Fences::FilterType &k = fences.filterType();
  for (const Fences::Filter &f : fences.filters())
    if (x >= f.x && x < f.x + k.footprintX && y >= f.y && y < f.y + k.footprintY)
      return false;
  // DRT bases
  for (const Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    if (t.kind != Kind::Helicopter)
      continue;
    int x0 = static_cast<int>(std::floor(m.x - t.footX / 2.0f));
    int y0 = static_cast<int>(std::floor(m.y - t.footY / 2.0f));
    if (x >= x0 && x < x0 + t.footX && y >= y0 && y < y0 + t.footY)
      return false;
  }
  return true;
}

// From a tile to its neighbour: no fence on the edge between, no cliff
bool Staff::canStep(int x, int y, int nx, int ny, const WorldMap &map, const Fences &fences,
                    int access) const {
  Fences::Edge e = nx != x ? Fences::Edge{false, std::max(x, nx), y}
                           : Fences::Edge{true, x, std::max(y, ny)};
  // Fences stop them; an exhibit's gate lets in its keeper on a visit
  if (const Fences::Piece *p = fences.at(e))
    if (!(p->gate && access >= 0 && fences.exhibitOf(e) == access))
      return false;
  const MapTile *a = map.getTile(x, y), *b = map.getTile(nx, ny);
  if (!a || !b)
    return false;
  // The corners they share must meet
  int a0, a1, b0, b1;
  if (nx > x) {
    a0 = a->cornerHeight[CORNER_X1Y0]; a1 = a->cornerHeight[CORNER_X1Y1];
    b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X0Y1];
  } else if (nx < x) {
    a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X0Y1];
    b0 = b->cornerHeight[CORNER_X1Y0]; b1 = b->cornerHeight[CORNER_X1Y1];
  } else if (ny > y) {
    a0 = a->cornerHeight[CORNER_X0Y1]; a1 = a->cornerHeight[CORNER_X1Y1];
    b0 = b->cornerHeight[CORNER_X0Y0]; b1 = b->cornerHeight[CORNER_X1Y0];
  } else {
    a0 = a->cornerHeight[CORNER_X0Y0]; a1 = a->cornerHeight[CORNER_X1Y0];
    b0 = b->cornerHeight[CORNER_X0Y1]; b1 = b->cornerHeight[CORNER_X1Y1];
  }
  // Where the shared corners disagree at all the map has a cliff there
  // (the renderer draws a wall): nobody walks up or down it
  return a0 == b0 && a1 == b1;
}

Fences::Fit Staff::canPlace(float x, float y, const WorldMap &map, const Fences &fences) const {
  int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  if (!walkable(tx, ty, map, fences))
    return Fences::Fit::Outside;
  return Fences::Fit::Ok;
}

Fences::Fit Staff::canPlaceType(int type, float x, float y, const WorldMap &map,
                                const Fences &fences) const {
  if (type >= 0 && type < static_cast<int>(this->typeList.size()) &&
      this->typeList[type].kind == Kind::Helicopter) {
    // The base's whole footprint on clear zoo ground
    const Type &t = this->typeList[type];
    int x0 = static_cast<int>(std::floor(x - t.footX / 2.0f));
    int y0 = static_cast<int>(std::floor(y - t.footY / 2.0f));
    for (int ty = y0; ty < y0 + t.footY; ty++)
      for (int tx = x0; tx < x0 + t.footX; tx++)
        if (!walkable(tx, ty, map, fences))
          return Fences::Fit::Outside;
    for (const Member &m : this->list)
      if (m.x >= x0 && m.x < x0 + t.footX && m.y >= y0 && m.y < y0 + t.footY)
        return Fences::Fit::InTheWay;
    return Fences::Fit::Ok;
  }
  return this->canPlaceMember(x, y, map, fences);
}

Fences::Fit Staff::canPlaceMember(float x, float y, const WorldMap &map,
                                  const Fences &fences) const {
  int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
  if (!walkable(tx, ty, map, fences))
    return Fences::Fit::Outside;
  // Not on top of another
  for (const Member &m : this->list)
    if (std::fabs(m.x - x) < 0.35f && std::fabs(m.y - y) < 0.35f)
      return Fences::Fit::Outside;
  return Fences::Fit::Ok;
}

// A* over the tiles (8 ways, no corner cutting) that keeps to paths:
// open ground costs kOffPath times a path tile, so a route takes the path
// network to the point nearest its goal and leaves it only for the last
// stretch. Straightened only within a run of path or a run of open ground
// (never cutting a corner off a path).
constexpr float kOffPath = 6.0f;
bool Staff::findPath(Member &m, int gx, int gy, const WorldMap &map, const Fences &fences,
                     float endX, float endY) {
  if (Features::elevatedPaths && this->walkways && !this->walkways->all().empty())
    return findLayeredPath(m, gx, gy, map, fences, endX, endY);
  m.pathLayers.clear();
  m.layer = 0;
  // Inside an exhibit it may always walk out of it (through its gate)
  int access = m.access;
  if (access < 0)
    access = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
  auto pass = [&](int x, int y) { return walkable(x, y, map, fences, access); };
  auto step = [&](int x, int y, int nx, int ny) {
    return canStep(x, y, nx, ny, map, fences, access);
  };
  // (staffpaths off: every tile alike, as the original)
  auto onPathTile = [&](int x, int y) { return !Features::staffPaths || map.isPath(x, y); };
  auto cost = [&](int x, int y) { return onPathTile(x, y) ? 1.0f : kOffPath; };
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  std::vector<std::pair<int, int>> tiles;
  if (!Pathfinder::find(map.getWidth(), map.getHeight(), sx, sy, gx, gy, pass, step, tiles, 40000,
                        cost)) {
    if (this->logRoutes)
      SDL_Log("[Path] %s: %d,%d -> %d,%d  NO ROUTE  (%s)", m.name.c_str(), sx, sy, gx, gy,
              dutyText(m).c_str());
    return false;
  }
  // Waypoints at tile middles (a little loose off the paths), in runs of
  // path / open ground, each run straightened on its own kind of tile
  std::uniform_real_distribution<float> jitter(0.4f, 0.6f);
  std::vector<std::pair<float, float>> path;
  std::vector<std::pair<float, float>> run{{m.x, m.y}};
  bool runOnPath = onPathTile(sx, sy);
  auto flush = [&] {
    bool onPath = runOnPath;
    auto same = [&](int x, int y) { return pass(x, y) && onPathTile(x, y) == onPath; };
    if (Features::smoothRoutes)
      Pathfinder::smooth(run, same, step);
    for (size_t k = 1; k < run.size(); k++)
      path.push_back(run[k]);
  };
  for (size_t i = 1; i < tiles.size(); i++) {
    auto [x, y] = tiles[i];
    bool onPath = onPathTile(x, y);
    std::pair<float, float> p = i + 1 == tiles.size()
                                    ? std::make_pair(endX, endY)
                                    : onPath ? std::make_pair(x + 0.5f, y + 0.5f)
                                             : std::make_pair(x + jitter(this->rng), y + jitter(this->rng));
    if (onPath != runOnPath) {
      flush();
      run = {run.back()};
      runOnPath = onPath;
    }
    run.push_back(p);
  }
  flush();
  if (path.empty())
    path.push_back({endX, endY}); // (already on the goal tile)
  m.path = path;
  m.pathAt = 0;
  m.routeTiles = tiles;
  m.goalX = gx;
  m.goalY = gy;
  if (this->logRoutes) {
    int onPath = 0;
    for (auto [x, y] : tiles)
      onPath += map.isPath(x, y) ? 1 : 0;
    SDL_Log("[Path] %s: %d,%d -> %d,%d  %zu tiles (%d on paths), %zu legs  (%s)", m.name.c_str(), sx,
            sy, gx, gy, tiles.size(), onPath, m.path.size(), dutyText(m).c_str());
  }
  return true;
}

float Staff::heightOf(const Member &m, const WorldMap &map) const {
  if (m.layer == 1 && this->walkways) {
    int tx = static_cast<int>(std::floor(m.x)), ty = static_cast<int>(std::floor(m.y));
    if (this->walkways->at(tx, ty))
      return this->walkways->heightAt(tx, ty, m.x - tx, m.y - ty);
  }
  return groundAt(map, m.x, m.y);
}

// With walkways about: A* over two layers, the ground and the decks above
// it. Onto a deck where a stair's foot meets the ground at the same
// heights; deck to deck where their edges join; a deck crosses a fence
// only well above it. Decks count as path.
bool Staff::findLayeredPath(Member &m, int gx, int gy, const WorldMap &map, const Fences &fences,
                            float endX, float endY) {
  const int W = map.getWidth(), H = map.getHeight(), N = W * H;
  int access = m.access;
  if (access < 0)
    access = fences.exhibitAt(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y)));
  auto pass = [&](int x, int y) { return walkable(x, y, map, fences, access); };
  auto stepOk = [&](int x, int y, int nx, int ny) {
    return canStep(x, y, nx, ny, map, fences, access);
  };
  auto onPathTile = [&](int x, int y) { return !Features::staffPaths || map.isPath(x, y); };
  const Walkways &ww = *this->walkways;
  auto groundEdge = [&](int x, int y, int dir, int &a, int &b) {
    const MapTile *t = map.getTile(x, y);
    if (!t)
      return false;
    int c0, c1;
    Walkways::edgeCorners(dir, c0, c1);
    a = t->cornerHeight[c0];
    b = t->cornerHeight[c1];
    return true;
  };
  auto next = [&](int node, std::vector<std::pair<int, float>> &out) {
    int layer = node / N, idx = node % N, x = idx % W, y = idx / W;
    if (layer == 0) {
      // Ground: as ever, eight ways
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          if (!dx && !dy)
            continue;
          int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= W || ny >= H)
            continue;
          bool goal = nx == gx && ny == gy;
          if (!goal && !pass(nx, ny))
            continue;
          float c = 1.0f;
          if (dx && dy) {
            if (!pass(nx, y) || !pass(x, ny) || !stepOk(x, y, nx, y) || !stepOk(nx, y, nx, ny) ||
                !stepOk(x, y, x, ny) || !stepOk(x, ny, nx, ny))
              continue;
            c = 1.41421356f;
          } else if (!stepOk(x, y, nx, ny)) {
            continue;
          }
          out.push_back({ny * W + nx, c * (onPathTile(nx, ny) ? 1.0f : kOffPath)});
        }
      // Up a stair whose foot meets this tile
      for (int dir = 0; dir < 4; dir++) {
        int dx, dy;
        Walkways::step(dir, dx, dy);
        int nx = x + dx, ny = y + dy, a, b, p, q;
        if (!ww.edgeHeights(nx, ny, (dir + 2) & 3, a, b) || !groundEdge(x, y, dir, p, q))
          continue;
        if (a == p && b == q && !fences.at(dir & 1 ? Fences::Edge{false, std::max(x, nx), y}
                                                    : Fences::Edge{true, x, std::max(y, ny)}))
          out.push_back({N + ny * W + nx, 1.0f});
      }
    } else {
      // On a deck: along joined decks, or off a stair's foot
      for (int dir = 0; dir < 4; dir++) {
        int dx, dy;
        Walkways::step(dir, dx, dy);
        int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H)
          continue;
        Fences::Edge e = dir & 1 ? Fences::Edge{false, std::max(x, nx), y}
                                 : Fences::Edge{true, x, std::max(y, ny)};
        int a, b;
        ww.edgeHeights(x, y, dir, a, b);
        if (ww.joins(x, y, dir)) {
          // Over a fence only well above it
          int p, q;
          groundEdge(x, y, dir, p, q);
          if (!fences.at(e) || std::min(a, b) - std::max(p, q) >= 3)
            out.push_back({N + ny * W + nx, 1.0f});
          continue;
        }
        int p, q;
        if (!ww.at(nx, ny) && groundEdge(nx, ny, (dir + 2) & 3, p, q) && a == p && b == q &&
            !fences.at(e) && (pass(nx, ny) || (nx == gx && ny == gy)))
          out.push_back({ny * W + nx, onPathTile(nx, ny) ? 1.0f : kOffPath});
      }
    }
  };
  auto guess = [&](int node) {
    int idx = node % N, x = idx % W, y = idx / W;
    float dx = static_cast<float>(std::abs(x - gx)), dy = static_cast<float>(std::abs(y - gy));
    return std::max(dx, dy) + 0.41421356f * std::min(dx, dy);
  };
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  if (sx < 0 || sy < 0 || sx >= W || sy >= H)
    return false;
  bool startOnDeck = m.layer == 1 && ww.at(sx, sy);
  int start = (startOnDeck ? N : 0) + sy * W + sx, goal = gy * W + gx;
  std::vector<int> nodes;
  if (!Pathfinder::findGraph(2 * N, start, goal, next, guess, nodes)) {
    if (this->logRoutes)
      SDL_Log("[Path] %s: %d,%d -> %d,%d  NO ROUTE  (%s)", m.name.c_str(), sx, sy, gx, gy,
              dutyText(m).c_str());
    return false;
  }
  // Waypoints at the tile middles, each with its layer; ground runs
  // straightened as before, deck runs left as laid
  m.path.clear();
  m.pathLayers.clear();
  m.routeTiles.clear();
  std::uniform_real_distribution<float> jitter(0.4f, 0.6f);
  int decks = 0;
  for (size_t i = 0; i < nodes.size(); i++) {
    int layer = nodes[i] / N, idx = nodes[i] % N, x = idx % W, y = idx / W;
    m.routeTiles.push_back({x, y});
    decks += layer;
    if (i == 0)
      continue;
    bool last = i + 1 == nodes.size();
    float px = last ? endX : (layer || map.isPath(x, y) ? x + 0.5f : x + jitter(this->rng));
    float py = last ? endY : (layer || map.isPath(x, y) ? y + 0.5f : y + jitter(this->rng));
    m.path.push_back({px, py});
    m.pathLayers.push_back(layer);
  }
  if (m.path.empty()) {
    m.path.push_back({endX, endY});
    m.pathLayers.push_back(0);
  }
  m.pathAt = 0;
  m.goalX = gx;
  m.goalY = gy;
  if (this->logRoutes)
    SDL_Log("[Path] %s: %d,%d -> %d,%d  %zu tiles (%d on walkways), %zu points  (%s)", m.name.c_str(),
            sx, sy, gx, gy, nodes.size(), decks, m.path.size(), dutyText(m).c_str());
  return true;
}

void Staff::addToIndex(WorldIndex &index) const {
  for (const Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    if (t.kind == Kind::Helicopter) {
      // Its base stands in the way
      int x0 = static_cast<int>(std::floor(m.x - t.footX / 2.0f));
      int y0 = static_cast<int>(std::floor(m.y - t.footY / 2.0f));
      index.addBlock(x0, y0, t.footX, t.footY, WorldIndex::What::Base);
      continue;
    }
    index.addMover(WorldIndex::Kind::Staff, m.id, m.x, m.y);
  }
}

// fWalk, kept to the paths: on a path, off along it to another bit of
// path a few tiles away; off one, to the nearest path; no path near, it
// stands where it is (nobody roams open ground with nothing there)
void Staff::wander(Member &m, const WorldMap &map, const Fences &fences) {
  const int W = map.getWidth(), H = map.getHeight();
  int sx = static_cast<int>(std::floor(m.x)), sy = static_cast<int>(std::floor(m.y));
  // (staffpaths off: all ground counts as path, so they wander it freely)
  auto pathLike = [&](int x, int y) { return !Features::staffPaths || map.isPath(x, y); };
  bool onPath = pathLike(sx, sy);
  std::vector<std::pair<int, int>> reach;
  std::vector<char> seen(static_cast<size_t>(W) * H, 0);
  std::queue<std::pair<int, int>> q;
  q.push({sx, sy});
  if (sx >= 0 && sy >= 0 && sx < W && sy < H)
    seen[sy * W + sx] = 1;
  const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  const int radius = !Features::staffPaths ? kWanderRadius : onPath ? kWanderRadius + 3 : 15;
  while (!q.empty()) {
    auto [x, y] = q.front();
    q.pop();
    if (onPath && std::abs(x - sx) + std::abs(y - sy) >= 2)
      reach.push_back({x, y});
    if (!onPath && pathLike(x, y)) {
      reach = {{x, y}}; // the nearest path
      break;
    }
    for (auto &dd : d) {
      int nx = x + dd[0], ny = y + dd[1];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H || seen[ny * W + nx])
        continue;
      if (std::abs(nx - sx) > radius || std::abs(ny - sy) > radius)
        continue;
      if (onPath && !pathLike(nx, ny))
        continue;
      if (!walkable(nx, ny, map, fences, m.access) ||
          !canStep(x, y, nx, ny, map, fences, m.access))
        continue;
      seen[ny * W + nx] = 1;
      q.push({nx, ny});
    }
  }
  if (reach.empty()) {
    m.idleLeft = kIdleSeconds * 2;
    m.walksLeft = 0;
    return;
  }
  auto [gx, gy] = reach[std::uniform_int_distribution<size_t>(0, reach.size() - 1)(this->rng)];
  findPath(m, gx, gy, map, fences, gx + 0.5f, gy + 0.5f);
}

void Staff::play(Member &m, const std::string &anim, int times) {
  m.anim = anim;
  m.animTime = 0;
  m.playsLeft = times;
  m.path.clear();
}

// fMaintRoutine: a worn fence or a run-down tank filter it's assigned to,
// the nearest it can reach
bool Staff::findWork(Member &m, const WorldMap &map, Fences &fences) {
  const Type &t = this->typeList[m.type];
  if (t.kind == Kind::Keeper || t.kind == Kind::Scientist || t.kind == Kind::Trainer)
    return keeperWork(m, map, fences);
  if (t.kind != Kind::Maint)
    return false;
  struct Spot {
    float dist;
    Job job;
    Fences::Edge edge;
    int filter;
    int tx, ty;
  };
  std::vector<Spot> spots;
  auto near = [&](int tx, int ty) {
    return std::hypot(tx + 0.5f - m.x, ty + 0.5f - m.y);
  };
  // Fences: a tile beside the piece it can stand on
  if (m.duties[2])
    for (const auto &kv : fences.pieces()) {
      if (!fences.worn(kv.first))
        continue;
      bool taken = false;
      for (const Member &o : this->list)
        taken = taken || (o.id != m.id && o.job == Job::Fence && o.jobEdge == kv.first);
      if (taken)
        continue;
      const Fences::Edge &e = kv.first;
      int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
      for (auto [tx, ty] : {std::make_pair(ax, ay), std::make_pair(e.x, e.y)})
        if (walkable(tx, ty, map, fences))
          spots.push_back({near(tx, ty), Job::Fence, e, -1, tx, ty});
    }
  // Tank filters: a tile beside its footprint
  if (m.duties[3]) {
    const Fences::FilterType &k = fences.filterType();
    for (size_t i = 0; i < fences.filters().size(); i++) {
      const Fences::Filter &f = fences.filters()[i];
      if (f.health > k.decayedHealth)
        continue;
      bool taken = false;
      for (const Member &o : this->list)
        taken = taken || (o.id != m.id && o.job == Job::Filter && o.jobFilter == static_cast<int>(i));
      if (taken)
        continue;
      for (int ty = f.y - 1; ty <= f.y + k.footprintY; ty++)
        for (int tx = f.x - 1; tx <= f.x + k.footprintX; tx++) {
          bool edge = tx == f.x - 1 || ty == f.y - 1 || tx == f.x + k.footprintX || ty == f.y + k.footprintY;
          bool corner = (tx == f.x - 1 || tx == f.x + k.footprintX) && (ty == f.y - 1 || ty == f.y + k.footprintY);
          if (edge && !corner && walkable(tx, ty, map, fences))
            spots.push_back({near(tx, ty), Job::Filter, {false, -1, -1}, static_cast<int>(i), tx, ty});
        }
    }
  }
  // Litter on the grounds (Sweep and clean zoo)
  if (m.duties[1] && this->items)
    for (int id : this->items->ofKind(ZooItems::Kind::Litter, -1)) {
      const ZooItems::Item *it = this->items->item(id);
      bool taken = false;
      for (const Member &o : this->list)
        taken = taken || (o.id != m.id && o.job == Job::Litter && o.jobItem == id);
      int tx = static_cast<int>(std::floor(it->x)), ty = static_cast<int>(std::floor(it->y));
      if (!taken && walkable(tx, ty, map, fences)) {
        Spot s{near(tx, ty), Job::Litter, {false, -1, -1}, id, tx, ty};
        spots.push_back(s);
      }
    }
  std::sort(spots.begin(), spots.end(), [](const Spot &a, const Spot &b) { return a.dist < b.dist; });
  for (const Spot &s : spots) {
    if (s.job == Job::Litter) {
      const ZooItems::Item *it = this->items->item(s.filter);
      if (it && findPath(m, s.tx, s.ty, map, fences, it->x, it->y)) {
        m.job = Job::Litter;
        m.jobItem = s.filter;
        m.dutyId = kGoingToSweep;
        return true;
      }
      continue;
    }
    // Stand at the tile's side nearest the work
    float ex = s.tx + 0.5f, ey = s.ty + 0.5f;
    if (s.job == Job::Fence) {
      float mx = s.edge.alongX ? s.edge.x + 0.5f : static_cast<float>(s.edge.x);
      float my = s.edge.alongX ? static_cast<float>(s.edge.y) : s.edge.y + 0.5f;
      ex += (mx - ex) * 0.5f;
      ey += (my - ey) * 0.5f;
    }
    if (findPath(m, s.tx, s.ty, map, fences, ex, ey)) {
      m.job = s.job;
      m.jobEdge = s.edge;
      m.jobFilter = s.filter;
      m.dutyId = s.job == Job::Fence ? kGoingToFix : kGoingToFilter;
      return true;
    }
  }
  return false;
}

// ----------------------------------------------------------------------------
// Keepers, scientists, marine specialists (fKeeperRoutine): their assigned
// exhibits in turn. An exhibit needs a visit when its food is running low
// or there's dung to rake (a tank: food, or its water is dirty). Keepers
// walk to its gate ("Going to Exhibit 1"), in ("Entering Exhibit 1"), put
// food down ("Placing food", feedh or feedc three times), rake each pile
// ("Raking up poo", clean four times) and come out ("Exiting exhibit").
// Marine specialists work from the gate's platform: food in, then the
// water cleaned (cCleanTankPct a go, "Cleaning the tank").
// ----------------------------------------------------------------------------
void Staff::face(Member &m, float x, float y) {
  float d = std::hypot(x - m.x, y - m.y);
  if (d > 0.001f) {
    m.fx = (x - m.x) / d;
    m.fy = (y - m.y) / d;
  }
}

bool Staff::needsVisit(const Member &m, int exhibit, const Fences &fences) const {
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!ex || ex->testAnimals <= 0 || !this->items)
    return false;
  bool hungry = this->items->foodIn(exhibit) < ex->testAnimals * ZooItems::kFoodPerAnimal * 0.5f;
  if (ex->tank)
    return hungry || (ex->water > 0 && ex->purity < 90.0f);
  (void)m;
  return hungry || !this->items->ofKind(ZooItems::Kind::Dung, exhibit).empty();
}

bool Staff::keeperWork(Member &m, const WorldMap &map, Fences &fences) {
  // Exhibits gone (a fence taken out) leave the schedule
  m.exhibits.erase(std::remove_if(m.exhibits.begin(), m.exhibits.end(),
                                  [&](int ex) { return !fences.exhibit(ex); }),
                   m.exhibits.end());
  for (int ex : m.exhibits) {
    if (!needsVisit(m, ex, fences))
      continue;
    // Not one another keeper is on
    bool taken = false;
    for (const Member &o : this->list)
      taken = taken || (o.id != m.id && o.job == Job::Visit && o.visit == ex);
    if (taken)
      continue;
    // Its gate: the tile outside it and the one in
    for (const auto &kv : fences.pieces()) {
      if (!kv.second.gate || fences.exhibitOf(kv.first) != ex)
        continue;
      const Fences::Edge &e = kv.first;
      int ax = e.alongX ? e.x : e.x - 1, ay = e.alongX ? e.y - 1 : e.y;
      int bx = e.x, by = e.y;
      bool aIn = fences.exhibitAt(ax, ay) == ex;
      int ox = aIn ? bx : ax, oy = aIn ? by : ay, ix = aIn ? ax : bx, iy = aIn ? ay : by;
      if (!walkable(ox, oy, map, fences))
        continue;
      m.outX = ox + 0.5f;
      m.outY = oy + 0.5f;
      m.inX = ix + 0.5f;
      m.inY = iy + 0.5f;
      m.access = -1;
      m.dutyId = kGoingTo;
      m.dutyArg = fences.exhibit(ex)->name;
      if (!findPath(m, ox, oy, map, fences, m.outX, m.outY)) {
        m.dutyId = kMonitoring;
        m.dutyArg.clear();
        continue;
      }
      m.job = Job::Visit;
      m.visit = ex;
      m.phase = 0;
      return true;
    }
  }
  return false;
}

// The next step of a visit, when the last walk or animation is done
void Staff::visitStep(Member &m, const WorldMap &map, Fences &fences) {
  const Type &t = this->typeList[m.type];
  Fences::Exhibit *ex = fences.exhibit(m.visit);
  auto done = [&] {
    m.job = Job::None;
    m.visit = -1;
    m.access = -1;
    m.anim = "idle";
    m.idleLeft = 0.5f;
    m.dutyId = kMonitoring;
    m.dutyArg.clear();
  };
  if (!ex || !this->items) {
    done();
    return;
  }
  bool hungry = this->items->foodIn(m.visit) < ex->testAnimals * ZooItems::kFoodPerAnimal * 0.5f;
  std::string food = ex->tank ? "fish"
                     : ex->testDino ? (ex->testCarnivore ? "dinocarn" : "dinogras")
                                    : (ex->testCarnivore ? "carnchow" : "herbchow");
  // A tank, from the platform
  if (ex->tank) {
    switch (m.phase) {
    case 0: // at the gate
      face(m, m.inX, m.inY);
      if (hungry) {
        m.phase = 1;
        m.dutyId = kPlacingFood;
        play(m, "throw", 1);
        return;
      }
      [[fallthrough]];
    case 1: // (food thrown in)
      if (m.phase == 1) {
        this->items->addFood(m.visit, m.inX, m.inY, ex->testAnimals, food);
      }
      if (ex->water > 0 && ex->purity < 100.0f) {
        m.phase = 2;
        m.dutyId = kCleaningTank;
        play(m, "feed", 2);
        return;
      }
      done();
      return;
    case 2: // a sweep of the water
      ex->purity = std::min(100.0f, ex->purity + kCleanTankPct);
      if (ex->purity < 100.0f) {
        play(m, "feed", 2);
        return;
      }
      done();
      return;
    }
    done();
    return;
  }
  switch (m.phase) {
  case 0: // outside the gate: in through it
    m.access = m.visit;
    m.phase = 1;
    m.dutyId = kEntering;
    if (!findPath(m, static_cast<int>(m.inX), static_cast<int>(m.inY), map, fences, m.inX, m.inY))
      done();
    return;
  case 1: // just inside: food, if it's low
    if (hungry) {
      m.phase = 2;
      m.dutyId = kPlacingFood;
      m.dutyArg.clear();
      face(m, m.inX + (m.inX - m.outX), m.inY + (m.inY - m.outY));
      play(m, ex->testCarnivore ? "feedc" : "feedh", 3);
      return;
    }
    m.phase = 3;
    visitStep(m, map, fences);
    return;
  case 2: // food down, a step further in
    this->items->addFood(m.visit, m.inX + (m.inX - m.outX) * 0.6f, m.inY + (m.inY - m.outY) * 0.6f,
                         ex->testAnimals, food);
    m.phase = 3;
    visitStep(m, map, fences);
    return;
  case 3: { // dung to rake: the nearest pile
    m.jobItem = -1;
    float best = 1e9f;
    for (int id : this->items->ofKind(ZooItems::Kind::Dung, m.visit)) {
      const ZooItems::Item *it = this->items->item(id);
      float d = std::hypot(it->x - m.x, it->y - m.y);
      if (d < best) {
        best = d;
        m.jobItem = id;
      }
    }
    if (m.jobItem >= 0) {
      const ZooItems::Item *it = this->items->item(m.jobItem);
      m.phase = 4;
      m.dutyId = kRaking;
      if (findPath(m, static_cast<int>(std::floor(it->x)), static_cast<int>(std::floor(it->y)), map,
                   fences, it->x + 0.2f, it->y + 0.2f))
        return;
      this->items->remove(m.jobItem); // (can't reach it: leave it be)
      m.phase = 3;
      visitStep(m, map, fences);
      return;
    }
    // Out again
    m.phase = 6;
    m.dutyId = kExiting;
    if (!findPath(m, static_cast<int>(m.outX), static_cast<int>(m.outY), map, fences, m.outX,
                  m.outY))
      done();
    return;
  }
  case 4: // at the pile: rake it (bClean: clean four times)
    if (const ZooItems::Item *it = this->items->item(m.jobItem))
      face(m, it->x, it->y);
    m.phase = 5;
    play(m, t.kind == Kind::Scientist ? "clean" : "clean", 4);
    return;
  case 5: // raked
    this->items->remove(m.jobItem);
    m.jobItem = -1;
    m.phase = 3;
    visitStep(m, map, fences);
    return;
  default: // out
    done();
    return;
  }
}

int Staff::assign(int id, int exhibit, const Fences &fences) {
  Member *m = this->member(id);
  const Fences::Exhibit *ex = fences.exhibit(exhibit);
  if (!m || !ex)
    return 0;
  Kind k = this->typeList[m->type].kind;
  if (k == Kind::Trainer) {
    if (!ex->tank)
      return 10328; // "Marine specialists can only be assigned to tank exhibits with marine animals."
  } else if (k == Kind::Scientist) {
    if (ex->tank || (ex->testAnimals > 0 && !ex->testDino))
      return 10324; // "Scientists can only be assigned to land exhibits with dinosaurs."
  } else if (k == Kind::Keeper) {
    if (ex->tank || (ex->testAnimals > 0 && ex->testDino))
      return 10320; // "Zookeepers can only be assigned to land exhibits with zoo animals."
  } else {
    return 0;
  }
  if (ex->testAnimals <= 0)
    return 10329; // "Zoo staff cannot be assigned to empty exhibits."
  if (std::find(m->exhibits.begin(), m->exhibits.end(), exhibit) == m->exhibits.end())
    m->exhibits.push_back(exhibit);
  m->workCheck = 0;
  return 0;
}

bool Staff::walkTo(int id, int x, int y, const WorldMap &map, const Fences &fences) {
  Member *m = this->member(id);
  if (!m)
    return false;
  m->job = Job::None;
  m->playsLeft = 0;
  m->walksLeft = 0;
  m->workCheck = 30.0f; // (not straight off to other work)
  return findPath(*m, x, y, map, fences, x + 0.5f, y + 0.5f);
}

std::string Staff::dutyText(const Member &m) const {
  std::string s = this->rm ? this->rm->getString(m.dutyId) : "";
  size_t at = s.find("%s");
  if (at != std::string::npos)
    s.replace(at, 2, m.dutyArg);
  return s;
}

// ----------------------------------------------------------------------------
// Hiring
// ----------------------------------------------------------------------------
int Staff::hire(int type, float x, float y, const WorldMap &map, const Fences &fences) {
  if (type < 0 || type >= static_cast<int>(this->typeList.size()))
    return -1;
  if (canPlaceType(type, x, y, map, fences) != Fences::Fit::Ok)
    return -1;
  const Type &t = this->typeList[type];
  Member m;
  m.id = this->nextId++;
  m.type = type;
  m.female = t.sexes[1] && (!t.sexes[0] || std::uniform_int_distribution<int>(0, 1)(this->rng) == 1);
  if (!t.hair.empty())
    m.hair = std::uniform_int_distribution<int>(0, static_cast<int>(t.hair.size()) - 1)(this->rng);
  if (!t.skin.empty())
    m.skin = std::uniform_int_distribution<int>(0, static_cast<int>(t.skin.size()) - 1)(this->rng);
  int n = ++this->counts[type];
  m.name = (this->rm ? this->rm->getString(t.nameId) : t.key) + " " + std::to_string(n);
  m.x = x;
  m.y = y;
  m.fx = 0;
  m.fy = 1;
  m.idleLeft = 1.0f;
  m.walksLeft = 0;
  m.dutyId = t.kind == Kind::Helicopter ? kWaitingAtBase
             : t.kind == Kind::Guide    ? kGuideMonitoring
             : t.kind == Kind::Maint    ? kMaintMonitoring
                                        : kMonitoring;
  // Maintenance workers start on all their duties; keepers on none
  this->list.push_back(m);
  return m.id;
}

void Staff::fire(int id) {
  this->list.erase(std::remove_if(this->list.begin(), this->list.end(),
                                  [&](const Member &m) { return m.id == id; }),
                   this->list.end());
  if (this->selected == id)
    this->selected = -1;
  if (this->carried == id)
    this->carried = -1;
}

void Staff::moveTo(int id, float x, float y) {
  if (Member *m = this->member(id)) {
    m->x = x;
    m->y = y;
    m->path.clear();
    m->job = Job::None;
    m->visit = m->access = -1;
    m->playsLeft = 0;
    m->layer = 0;
    m->pathLayers.clear();
    m->anim = "idle";
    m->idleLeft = kIdleSeconds;
  }
}

// ----------------------------------------------------------------------------
// Each one's day: walk about, stand a while, go to work when there is some
// ----------------------------------------------------------------------------
void Staff::update(float seconds, const WorldMap &map, Fences &fences) {
  for (Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    m.animTime += seconds;
    m.workCheck -= seconds;
    if (m.id == this->carried)
      continue;
    if (t.kind == Kind::Helicopter) {
      m.anim = "idle";
      continue;
    }
    // Playing a work animation
    if (m.playsLeft > 0) {
      float len = animSeconds(m, m.anim);
      if (m.animTime >= len) {
        m.animTime -= len;
        if (--m.playsLeft == 0) {
          // The work's done
          if (m.job == Job::Visit) {
            visitStep(m, map, fences);
            continue;
          }
          if (m.job == Job::Fence)
            fences.repair(m.jobEdge);
          else if (m.job == Job::Filter)
            fences.serviceFilter(m.jobFilter);
          else if (m.job == Job::Litter && this->items)
            this->items->remove(m.jobItem);
          m.job = Job::None;
          m.anim = "idle";
          m.idleLeft = 0.5f;
          m.dutyId = t.kind == Kind::Maint ? kMaintMonitoring : kMonitoring;
        }
      }
      continue;
    }
    // Walking a path
    if (m.pathAt < m.path.size()) {
      auto [tx, ty] = m.path[m.pathAt];
      float dx = tx - m.x, dy = ty - m.y, dist = std::hypot(dx, dy);
      float step = t.speed * seconds;
      if (m.anim != "walk") {
        m.anim = "walk";
        m.animTime = 0;
      }
      if (dist > 0.0001f) {
        m.fx = dx / dist;
        m.fy = dy / dist;
      }
      if (m.pathAt < m.pathLayers.size())
        m.layer = m.pathLayers[m.pathAt];
      if (dist <= step) {
        m.x = tx;
        m.y = ty;
        m.pathAt++;
      } else {
        float nx = m.x + dx / dist * step, ny = m.y + dy / dist * step;
        // Stepping round anyone close in front (sideways, if there's room)
        if (this->index)
          for (const WorldIndex::Mover &o : this->index->near(m.x, m.y, 0.6f)) {
            if (o.id == m.id || o.kind != WorldIndex::Kind::Staff)
              continue;
            float ox = o.x - m.x, oy = o.y - m.y, od = std::hypot(ox, oy);
            if (od < 0.001f || (ox * dx + oy * dy) / (od * dist) < 0.3f)
              continue;
            // Their side of the line: go the other way
            float side = (dx * oy - dy * ox) > 0 ? -1.0f : 1.0f;
            float px = -dy / dist * side, py = dx / dist * side;
            float push = step * (0.6f - od) / 0.6f;
            float sx2 = nx + px * push, sy2 = ny + py * push;
            if (walkable(static_cast<int>(std::floor(sx2)), static_cast<int>(std::floor(sy2)), map,
                         fences, m.access)) {
              nx = sx2;
              ny = sy2;
            }
          }
        m.x = nx;
        m.y = ny;
      }
      if (m.pathAt >= m.path.size()) {
        m.path.clear();
        m.pathAt = 0;
        if (m.job == Job::Visit) {
          visitStep(m, map, fences);
        } else if (m.job == Job::Litter) {
          // bSweep: sweep twice
          m.dutyId = kSweeping;
          play(m, "sweep", 2);
        } else if (m.job == Job::Fence || m.job == Job::Filter) {
          // Face the work, then fix it (bFix: fix twice)
          float wx, wy;
          if (m.job == Job::Fence) {
            wx = m.jobEdge.alongX ? m.jobEdge.x + 0.5f : static_cast<float>(m.jobEdge.x);
            wy = m.jobEdge.alongX ? static_cast<float>(m.jobEdge.y) : m.jobEdge.y + 0.5f;
          } else {
            const Fences::Filter *f = fences.filter(m.jobFilter);
            wx = f ? f->x + fences.filterType().footprintX * 0.5f : m.x;
            wy = f ? f->y + fences.filterType().footprintY * 0.5f : m.y;
          }
          float d = std::hypot(wx - m.x, wy - m.y);
          if (d > 0.001f) {
            m.fx = (wx - m.x) / d;
            m.fy = (wy - m.y) / d;
          }
          m.dutyId = m.job == Job::Fence ? kFixing : kServicing;
          play(m, "fix", 2);
        } else {
          m.anim = "idle";
          m.animTime = 0;
          if (m.walksLeft <= 0)
            m.idleLeft = kIdleSeconds;
        }
      }
      continue;
    }
    // Standing
    if (m.idleLeft > 0) {
      m.idleLeft -= seconds;
      if (m.anim != "idle") {
        m.anim = "idle";
        m.animTime = 0;
      }
      continue;
    }
    // Time to look for work (every few seconds), else walk about
    if (m.workCheck <= 0) {
      m.workCheck = 3.0f;
      if (findWork(m, map, fences))
        continue;
    }
    if (m.walksLeft <= 0)
      m.walksLeft = t.kind == Kind::Keeper || t.kind == Kind::Scientist ? 5 : 3;
    m.walksLeft--;
    wander(m, map, fences);
  }
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
CompassDirection Staff::facing(const WorldRenderer &view, const Member &m) const {
  float x0, y0, x1, y1, d;
  view.worldToScreenF(m.x, m.y, 0, x0, y0, d);
  view.worldToScreenF(m.x + m.fx, m.y + m.fy, 0, x1, y1, d);
  float angle = std::atan2(x1 - x0, -(y1 - y0));
  int octant = static_cast<int>(std::lround(angle / (3.14159265f / 4.0f)));
  octant = (octant % 8 + 8) % 8;
  static const CompassDirection dirs[8] = {
      CompassDirection::N, CompassDirection::NE, CompassDirection::E, CompassDirection::SE,
      CompassDirection::S, CompassDirection::SW, CompassDirection::W, CompassDirection::NW};
  return dirs[octant];
}

void Staff::collect(const WorldRenderer &view, const WorldMap &map,
                    std::vector<Fences::Drawable> &all,
                    std::vector<Fences::Drawable> *raised) const {
  std::vector<Fences::Drawable> &out = all;
  for (const Member &m : this->list) {
    const Type &t = this->typeList[m.type];
    if (t.kind == Kind::Helicopter) {
      // Its base, and the helicopter in the base's slot (slotpos -64, 0,
      // slotposz 80: a tile along x, five units up)
      float bx, by, bdepth;
      float g = groundAt(map, m.x, m.y);
      view.worldToScreenF(m.x, m.y, g, bx, by, bdepth);
      bool lit = m.id == this->hovered;
      SDL_Color tint{255, 255, 110, 255};
      if (t.base)
        out.push_back({bdepth, bx, by, t.base, CompassDirection::SE, tint, lit});
      if (Animation *heli = this->art(m, "idle")) {
        float hx, hy, hdepth;
        view.worldToScreenF(m.x - 1.0f, m.y, g + 5.0f, hx, hy, hdepth);
        out.push_back({bdepth + 0.01f, hx, hy, heli, CompassDirection::SE, tint, lit});
      }
      continue;
    }
    Animation *a = this->art(m, m.anim);
    if (!a)
      continue;
    float ms = a->frameTimeMs() ? static_cast<float>(a->frameTimeMs()) : 100.0f;
    int frame = static_cast<int>(m.animTime * 1000.0f / ms);
    float sx, sy, depth, gsx, gsy;
    float z = heightOf(m, map);
    view.worldToScreenF(m.x, m.y, z, sx, sy, depth);
    // On a deck: just after the deck it stands on (sorted by its tile's
    // front corner)
    if (m.layer == 1) {
      int tx = static_cast<int>(std::floor(m.x)), ty = static_cast<int>(std::floor(m.y));
      view.worldToScreenF(tx + 0.5f, ty + 0.5f, 0.0f, gsx, gsy, depth);
      depth += 0.62f;
    }
    bool lit = m.id == this->hovered;
    Fences::Drawable d{depth, sx, sy, a, facing(view, m), SDL_Color{255, 255, 110, 255}, lit};
    d.frame = frame;
    if (m.layer == 1 && raised)
      raised->push_back(d);
    else
      out.push_back(d);
    // The selected one: the arrow over its head
    if (m.id == this->selected && this->selectArrow) {
      int w = 0, h = 0;
      a->queryTexture(CompassDirection::S, &w, &h);
      Fences::Drawable arrow{depth + 0.0001f, sx, sy - 36.0f, this->selectArrow,
                             CompassDirection::N, SDL_Color{255, 255, 255, 255}, false};
      out.push_back(arrow);
    }
  }
  // The one being hired: its likeness at the cursor, green where it can go
  if (this->previewType >= 0) {
    const Type &pt = this->typeList[this->previewType];
    Animation *ghost = pt.kind == Kind::Helicopter && pt.base ? pt.base : this->preview(this->previewType);
    if (Animation *a = ghost) {
      float sx, sy, depth;
      view.worldToScreenF(this->previewX, this->previewY,
                          groundAt(map, this->previewX, this->previewY), sx, sy, depth);
      SDL_Color tint = this->previewFit == Fences::Fit::Ok ? SDL_Color{0, 255, 0, 255}
                                                           : SDL_Color{255, 60, 60, 255};
      out.push_back({depth, sx, sy, a, CompassDirection::SE, tint, true});
    }
  }
}

int Staff::pick(float px, float py, const WorldRenderer &view, const WorldMap &map) const {
  int found = -1;
  float best = 1e9f;
  for (const Member &m : this->list) {
    float sx, sy, depth;
    view.worldToScreenF(m.x, m.y, heightOf(m, map), sx, sy, depth);
    // A figure about 12 wide and 30 tall standing at its anchor; a DRT
    // base its pad
    bool heli = this->typeList[m.type].kind == Kind::Helicopter;
    float hw = heli ? 90.0f : 9.0f, top = heli ? 90.0f : 32.0f, bottom = heli ? 40.0f : 4.0f;
    if (px < sx - hw || px > sx + hw || py < sy - top || py > sy + bottom)
      continue;
    float d = std::fabs(px - sx) + std::fabs(py - (sy - 14));
    if (d < best) {
      best = d;
      found = m.id;
    }
  }
  return found;
}
