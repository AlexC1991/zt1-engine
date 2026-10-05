#include "TerrainTool.hpp"

#include <algorithm>
#include <cmath>

#include "Fences.hpp"
#include "IniReader.hpp"
#include "ResourceManager.hpp"
#include "WorldMap.hpp"

namespace {
constexpr float kElevationCost = 8.0f; // [Map] elevationCost
constexpr int kMinHeight = -12, kMaxHeight = 12;
// A corner's vertex offset in its tile, and the corners beside it along
// the tile's edges and across it
const int kDx[4] = {0, 1, 1, 0}, kDy[4] = {0, 0, 1, 1};
const int kSide1[4] = {CORNER_X1Y0, CORNER_X0Y0, CORNER_X0Y1, CORNER_X1Y1};
const int kSide2[4] = {CORNER_X0Y1, CORNER_X1Y1, CORNER_X1Y0, CORNER_X0Y0};
const int kOpposite[4] = {CORNER_X1Y1, CORNER_X0Y1, CORNER_X0Y0, CORNER_X1Y0};
// The corner of the tile at (vx - dx, vy - dy) that is vertex (vx, vy)
int cornerAt(int dx, int dy) {
  return dy == 0 ? (dx == 0 ? CORNER_X0Y0 : CORNER_X1Y0) : (dx == 0 ? CORNER_X0Y1 : CORNER_X1Y1);
}
int lowest(const MapTile &t) { return *std::min_element(t.cornerHeight, t.cornerHeight + 4); }
int highest(const MapTile &t) { return *std::max_element(t.cornerHeight, t.cornerHeight + 4); }
} // namespace

void TerrainTool::load(ResourceManager *rm) {
  if (!rm || !this->types.empty())
    return;
  for (const std::string &cfg : rm->listResources("terrain/tiletex", ".cfg")) {
    IniReader *ini = rm->getIniReader(cfg);
    if (!ini)
      continue;
    for (const std::string &section : ini->getSections()) {
      int type = ini->getInt(section, "type", -1);
      if (type < 0)
        continue;
      TypeInfo t;
      t.cost = static_cast<float>(ini->getInt(section, "cost", 0));
      t.water = ini->getInt(section, "water", 0);
      this->types[type] = t;
    }
    delete ini;
  }
}

void TerrainTool::setActive(bool on) {
  if (on && !this->active) {
    this->baseline.clear();
    this->bx0 = this->bx1 = 0;
  }
  this->active = on;
  if (!on)
    this->held = false;
}

bool TerrainTool::owned(int x, int y) const {
  return this->map && this->map->getTile(x, y) && (!this->fences || this->fences->insideZoo(x, y));
}

bool TerrainTool::tank(int x, int y) const {
  if (!this->fences)
    return false;
  int ex = this->fences->exhibitAt(x, y);
  const Fences::Exhibit *e = ex >= 0 ? this->fences->exhibit(ex) : nullptr;
  return e && e->tank;
}

bool TerrainTool::water(const MapTile &t) const {
  auto it = this->types.find(t.terrainType);
  return it != this->types.end() ? it->second.water != 0 : (t.terrainType == 9 || t.terrainType == 10);
}

// Its corners can move on their own: not water, not a tank, nothing on it
// bigger than a quarter tile
bool TerrainTool::slopable(int x, int y, const MapTile &t) const {
  return !water(t) && !tank(x, y) && !(this->hasBigObject && this->hasBigObject(x, y));
}

void TerrainTool::setBrush(int tx, int ty) {
  if (!this->map)
    return;
  const int W = this->map->getWidth(), H = this->map->getHeight();
  int half = this->size / 2, lo = (this->size & 1) ? half : half - 1;
  this->bx0 = tx - lo;
  this->bx1 = tx + half + 1;
  this->by0 = ty - lo;
  this->by1 = ty + half + 1;
  // (moved inside the map, not cut off)
  if (this->bx0 < 0) {
    this->bx1 -= this->bx0;
    this->bx0 = 0;
  }
  if (this->bx1 > W) {
    this->bx0 -= this->bx1 - W;
    this->bx1 = W;
  }
  if (this->by0 < 0) {
    this->by1 -= this->by0;
    this->by0 = 0;
  }
  if (this->by1 > H) {
    this->by0 -= this->by1 - H;
    this->by1 = H;
  }
}

void TerrainTool::snapshot(int x, int y) {
  const MapTile *t = this->map ? this->map->getTile(x, y) : nullptr;
  if (!t || this->baseline.count({x, y}))
    return;
  Saved s;
  for (int c = 0; c < 4; c++)
    s.corner[c] = t->cornerHeight[c];
  s.terrain = t->terrainType;
  s.height = t->height;
  this->baseline[{x, y}] = s;
}

void TerrainTool::touch() {
  if (this->map)
    this->map->touch();
}

float TerrainTool::cost() const {
  float total = 0;
  for (const auto &[pos, o] : this->baseline) {
    const MapTile *t = this->map->getTile(pos.first, pos.second);
    if (!t)
      continue;
    int d = 0;
    for (int c = 0; c < 4; c++)
      d = std::max(d, std::abs(o.corner[c] - t->cornerHeight[c]));
    total += d * kElevationCost;
    if (o.terrain != t->terrainType) {
      auto it = this->types.find(t->terrainType);
      total += it != this->types.end() ? it->second.cost : 0;
    }
  }
  return total;
}

int TerrainTool::accept() {
  int c = static_cast<int>(this->cost());
  this->baseline.clear();
  return c;
}

void TerrainTool::undo() {
  for (const auto &[pos, o] : this->baseline)
    if (MapTile *t = this->map->getTileMutable(pos.first, pos.second)) {
      for (int c = 0; c < 4; c++)
        t->cornerHeight[c] = o.corner[c];
      t->terrainType = static_cast<uint8_t>(o.terrain);
      t->height = o.height;
    }
  this->baseline.clear();
  touch();
}

// ----------------------------------------------------------------------------
// Painting
// ----------------------------------------------------------------------------
bool TerrainTool::canPaint(int x, int y, int type) const {
  const MapTile *t = this->map->getTile(x, y);
  if (!t || !owned(x, y) || tank(x, y) || t->terrainType == type)
    return false;
  auto it = this->types.find(type);
  bool toWater = it != this->types.end() && it->second.water != 0;
  if (water(*t) == toWater)
    return true;
  // Water only onto flat open ground
  if (toWater)
    return lowest(*t) == highest(*t) && !(this->hasBigObject && this->hasBigObject(x, y)) &&
           !this->map->isPath(x, y);
  return true;
}

void TerrainTool::hover(int tx, int ty) {
  if (!this->active || this->held)
    return;
  setBrush(tx, ty);
  // One stroke's price here (the cursor's label)
  this->hoverCost = 0;
  if (this->painting)
    for (int y = this->by0; y < this->by1; y++)
      for (int x = this->bx0; x < this->bx1; x++)
        if (canPaint(x, y, this->terrainType)) {
          auto it = this->types.find(this->terrainType);
          this->hoverCost += it != this->types.end() ? it->second.cost : 0;
        }
}

void TerrainTool::paintAt(int tx, int ty, bool force) {
  if (!force && tx == this->pickX && ty == this->pickY)
    return;
  this->pickX = tx;
  this->pickY = ty;
  setBrush(tx, ty);
  bool changed = false;
  for (int y = this->by0; y < this->by1; y++)
    for (int x = this->bx0; x < this->bx1; x++)
      if (canPaint(x, y, this->terrainType)) {
        snapshot(x, y);
        this->map->getTileMutable(x, y)->terrainType = static_cast<uint8_t>(this->terrainType);
        changed = true;
      }
  if (changed) {
    this->sound = "sounds/terrpnt";
    touch();
  }
  this->hoverCost = 0;
}

void TerrainTool::press(int tx, int ty, float px, float py) {
  if (!this->active || !this->map->getTile(tx, ty))
    return;
  this->held = true;
  this->lastX = px;
  this->lastY = py;
  this->pickX = tx;
  this->pickY = ty;
  setBrush(tx, ty);
  if (this->painting) {
    paintAt(tx, ty, true);
    return;
  }
  // Levelling: to the height of the tile clicked, a step at once
  if (this->mode == Mode::LevelHills || this->mode == Mode::LevelCliffs) {
    this->levelHeight = this->map->getTile(tx, ty)->cornerHeight[CORNER_X0Y0];
    drag(tx, ty, px, py, 16.0f);
  }
}

void TerrainTool::drag(int tx, int ty, float px, float py, float pixelsPerUnit) {
  if (!this->active || !this->held)
    return;
  if (this->painting) {
    // Every tile the cursor passed over
    paintAt(tx, ty, false);
    this->lastX = px;
    this->lastY = py;
    return;
  }
  const bool level = this->mode == Mode::LevelHills || this->mode == Mode::LevelCliffs;
  const bool contig = this->mode == Mode::Hills || this->mode == Mode::LevelHills;
  float adx = std::fabs(px - this->lastX), ady = std::fabs(py - this->lastY);
  bool repick = level ? (adx >= 16 || ady >= 8) : (contig && adx >= 16);
  if (repick) {
    this->lastX = px;
    if (!level && ady > 32)
      this->lastY = py;
    this->pickX = tx;
    this->pickY = ty;
    setBrush(tx, ty);
  }
  const MapTile *picked = this->map->getTile(this->pickX, this->pickY);
  if (!picked)
    return;
  int h = picked->cornerHeight[CORNER_X0Y0];
  int n = 0, kind = 2, target = this->levelHeight;
  if (level) {
    n = 1;
    this->sound = "sounds/terrflat";
  } else {
    float step = pixelsPerUnit;
    auto steps = [&](float d) {
      int q = static_cast<int>(d / step);
      float r = d - q * step;
      return q + (r > step / 2 ? 1 : 0);
    };
    if (h < kMaxHeight && this->lastY - py >= step) {
      n = std::min(steps(this->lastY - py), kMaxHeight - h);
      kind = 0;
      this->sound = "sounds/terrup";
    } else if (h > kMinHeight && py - this->lastY >= step) {
      n = std::min(steps(py - this->lastY), h - kMinHeight);
      kind = 1;
      this->sound = "sounds/terrdown";
    } else {
      return;
    }
    this->lastY = py;
  }
  bool changed = false;
  for (int i = 0; i < n; i++)
    changed |= heightStep(target, contig, kind);
  this->flags.clear();
  if (changed)
    touch();
  else
    this->sound.clear();
}

// ----------------------------------------------------------------------------
// Height
// ----------------------------------------------------------------------------
bool TerrainTool::heightStep(int target, bool contig, int kind) {
  int mn = 1 << 30, mx = -(1 << 30);
  bool any = false;
  for (int y = this->by0; y < this->by1; y++)
    for (int x = this->bx0; x < this->bx1; x++)
      if (owned(x, y)) {
        const MapTile *t = this->map->getTile(x, y);
        mn = std::min(mn, lowest(*t));
        mx = std::max(mx, highest(*t));
        any = true;
      }
  if (!any)
    return false;
  Step mode;
  if (kind == 0) {
    mode = Step::Raise;
    target = mn + 1;
  } else if (kind == 1) {
    mode = Step::Lower;
    target = mx - 1;
  } else if (target > mn) {
    mode = contig ? Step::Raise : Step::LevelUp;
    if (contig)
      target = mn + 1;
  } else if (target < mx) {
    mode = contig ? Step::Lower : Step::LevelDown;
    if (contig)
      target = mx - 1;
  } else {
    return false;
  }
  if (target > kMaxHeight || target < kMinHeight)
    return false;
  const bool raise = mode == Step::Raise || mode == Step::LevelUp;
  for (int y = this->by0; y < this->by1; y++)
    for (int x = this->bx0; x < this->bx1; x++) {
      const MapTile *t = this->map->getTile(x, y);
      if (!t)
        continue;
      switch (mode) {
      case Step::Raise:
        if (lowest(*t) < target) {
          snapshot(x, y);
          moveTile(x, y, lowest(*t) + 1, false, true);
        }
        break;
      case Step::Lower:
        if (highest(*t) > target) {
          snapshot(x, y);
          moveTile(x, y, highest(*t) - 1, false, false);
        }
        break;
      case Step::LevelUp:
        if (lowest(*t) < target) {
          snapshot(x, y);
          moveTile(x, y, target, true, true);
        }
        break;
      case Step::LevelDown:
        if (highest(*t) > target) {
          snapshot(x, y);
          moveTile(x, y, target, true, false);
        }
        break;
      }
    }
  if (contig) {
    for (int y = this->by0; y < this->by1; y++)
      for (int x = this->bx0; x < this->bx1; x++)
        smooth(x, y, raise);
  } else {
    // (cliffs: the ground round it stays; it's in the account for its
    // new cliff faces)
    for (int y = this->by0 - 1; y <= this->by1; y++)
      for (int x = this->bx0 - 1; x <= this->bx1; x++)
        snapshot(x, y);
  }
  return true;
}

void TerrainTool::moveTile(int x, int y, int target, bool exact, bool raise) {
  if (!owned(x, y))
    return;
  MapTile *t = this->map->getTileMutable(x, y);
  int &moved = this->flags[{x, y}];
  if (slopable(x, y, *t)) {
    for (int c = 0; c < 4; c++) {
      int &h = t->cornerHeight[c];
      bool move = exact ? h != target : (raise ? target - h == 1 : h - target == 1);
      if (move) {
        h = target;
        moved |= 1 << c;
      }
    }
  } else if (raise ? highest(*t) < target : lowest(*t) > target) {
    // A flat block: all its corners together
    int base = t->cornerHeight[CORNER_X0Y0];
    int to = exact ? target : base + (raise ? 1 : -1);
    for (int c = 0; c < 4; c++)
      t->cornerHeight[c] += to - base;
    moved = 0xF;
  }
  t->height = t->cornerHeight[CORNER_X0Y0];
}

// "Hills and valleys": the ground round the brush follows its moved
// corners down (or up) a unit a tile
void TerrainTool::smooth(int x, int y, bool raise) {
  const MapTile *t = this->map->getTile(x, y);
  auto f = this->flags.find({x, y});
  if (!t || f == this->flags.end())
    return;
  for (int c = 0; c < 4; c++) {
    if (!(f->second & (1 << c)))
      continue;
    int vx = x + kDx[c], vy = y + kDy[c];
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++) {
        int nx = vx - dx, ny = vy - dy;
        if ((nx == x && ny == y) || inBrush(nx, ny))
          continue;
        follow(nx, ny, cornerAt(dx, dy), t->cornerHeight[c], raise, 0);
      }
  }
}

void TerrainTool::follow(int x, int y, int k, int h, bool raise, int depth) {
  if (depth > 64 || !owned(x, y))
    return;
  MapTile *n = this->map->getTileMutable(x, y);
  if (water(*n) && !(this->hasBigObject && this->hasBigObject(x, y)))
    return; // (water's edge stays: a cliff)
  int d = n->cornerHeight[k] - h;
  if (!((raise && d == -1) || (!raise && d == 1)))
    return;
  snapshot(x, y);
  int delta = h - n->cornerHeight[k];
  bool moveAll = !slopable(x, y, *n);
  bool moved[4] = {};
  for (int c = 0; c < 4; c++) {
    int gap = std::abs(h - n->cornerHeight[c]);
    bool move = moveAll || c == k || ((c == kSide1[k] || c == kSide2[k]) && gap > 1) ||
                (c == kOpposite[k] && gap > 2);
    if (move) {
      n->cornerHeight[c] += delta;
      moved[c] = true;
    }
  }
  n->height = n->cornerHeight[CORNER_X0Y0];
  // Its moved corners pull their neighbours along in turn
  for (int c = 0; c < 4; c++) {
    if (!moved[c])
      continue;
    int vx = x + kDx[c], vy = y + kDy[c];
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++) {
        int nx = vx - dx, ny = vy - dy;
        if ((nx == x && ny == y) || inBrush(nx, ny))
          continue;
        follow(nx, ny, cornerAt(dx, dy), n->cornerHeight[c], raise, depth + 1);
      }
  }
}
