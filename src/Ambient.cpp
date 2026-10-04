#include "Ambient.hpp"

#include <algorithm>
#include <cmath>

#include "IniReader.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"

namespace {
constexpr float kPixelsPerTile = 36.0f; // a tile's step on screen (32 x 16)
constexpr float kAltitude = 6.0f;       // height units above the ground
constexpr int kMostAtOnce = 3;
} // namespace

void Ambient::load(ResourceManager *rm) {
  if (!this->kinds.empty() || !rm)
    return;
  IniReader *cfg = rm->getIniReader("ambient.cfg");
  if (!cfg)
    return;
  auto anim = [&](std::string path) -> Animation * {
    path = Utils::string_to_lower(path);
    size_t dot = path.rfind(".ani");
    if (dot != std::string::npos)
      path = path.substr(0, dot);
    return !path.empty() && rm->hasResource(path + ".ani") ? rm->getAnimation(path) : nullptr;
  };
  for (const auto &kv : cfg->getSection("ambient")) {
    IniReader *ai = rm->getIniReader(kv.second);
    if (!ai)
      continue;
    Kind k;
    k.key = kv.first;
    const std::string ints = "characteristics/integers";
    k.speed = static_cast<float>(ai->getInt(ints, "cspeed", 50));
    k.frequency = ai->getInt(ints, "cfrequency", 0);
    k.blackShadow = ai->getInt(ints, "cforceshadowblack", 1) != 0;
    k.idle = anim(ai->get("animations", "idle"));
    k.shadow = ai->getInt(ints, "chasshadowimages", 0) ? anim(ai->get("animations", "shadowidle"))
                                                       : nullptr;
    delete ai;
    if (k.idle)
      this->kinds.push_back(k);
  }
  delete cfg;
  SDL_Log("[Ambient] %zu flyers", this->kinds.size());
}

void Ambient::spawn(const WorldMap &map, int kind) {
  if (this->kinds.empty())
    return;
  if (kind < 0) {
    int total = 0;
    for (const Kind &k : this->kinds)
      total += k.frequency;
    if (total <= 0)
      return;
    int pick = std::uniform_int_distribution<int>(0, total - 1)(this->rng);
    for (size_t i = 0; i < this->kinds.size(); i++) {
      pick -= this->kinds[i].frequency;
      if (pick < 0) {
        kind = static_cast<int>(i);
        break;
      }
    }
  }
  if (kind < 0 || kind >= static_cast<int>(this->kinds.size()))
    return;
  // From a point on one edge of the map to one on another
  const float W = static_cast<float>(map.getWidth()), H = static_cast<float>(map.getHeight());
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  auto edgePoint = [&](int side, float &x, float &y) {
    float t = u(this->rng);
    switch (side) {
    case 0: x = t * W; y = 0; break;
    case 1: x = W; y = t * H; break;
    case 2: x = t * W; y = H; break;
    default: x = 0; y = t * H; break;
    }
  };
  int from = std::uniform_int_distribution<int>(0, 3)(this->rng);
  int to = (from + std::uniform_int_distribution<int>(1, 3)(this->rng)) % 4;
  Flyer f;
  f.kind = kind;
  float tx, ty;
  edgePoint(from, f.x, f.y);
  edgePoint(to, tx, ty);
  float dx = tx - f.x, dy = ty - f.y, len = std::sqrt(dx * dx + dy * dy);
  if (len < 1.0f)
    return;
  float speed = this->kinds[kind].speed / kPixelsPerTile;
  f.vx = dx / len * speed;
  f.vy = dy / len * speed;
  f.life = len / speed;
  this->flyers.push_back(f);
}

void Ambient::update(float seconds, const WorldMap &map) {
  for (Flyer &f : this->flyers) {
    f.x += f.vx * seconds;
    f.y += f.vy * seconds;
    f.age += seconds;
  }
  this->flyers.erase(std::remove_if(this->flyers.begin(), this->flyers.end(),
                                    [](const Flyer &f) { return f.age >= f.life; }),
                     this->flyers.end());
  // The witch on Halloween, Santa on Christmas Eve and Day: once a day
  int special = -1;
  for (size_t i = 0; i < this->kinds.size(); i++) {
    if (this->kinds[i].key == "witch" && this->month == 9 && this->day == 31)
      special = static_cast<int>(i);
    if (this->kinds[i].key == "santa" && this->month == 11 &&
        (this->day == 24 || this->day == 25))
      special = static_cast<int>(i);
  }
  if (special >= 0 && !this->specialFlown) {
    this->spawn(map, special);
    this->specialFlown = true;
  } else if (special < 0) {
    this->specialFlown = false;
  }
  this->nextIn -= seconds;
  if (this->nextIn <= 0) {
    if (static_cast<int>(this->flyers.size()) < kMostAtOnce)
      this->spawn(map);
    this->nextIn = std::uniform_real_distribution<float>(6.0f, 20.0f)(this->rng);
  }
}

float Ambient::groundAt(const WorldMap &map, float x, float y) const {
  const MapTile *t = map.getTile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
  return t ? static_cast<float>(t->height) : 0.0f;
}

// Its art's facing: the way it goes on the screen, of eight
CompassDirection Ambient::facing(const WorldRenderer &view, const Flyer &f) const {
  float x0, y0, x1, y1, d;
  view.worldToScreenF(f.x, f.y, 0, x0, y0, d);
  view.worldToScreenF(f.x + f.vx, f.y + f.vy, 0, x1, y1, d);
  float angle = std::atan2(x1 - x0, -(y1 - y0)); // 0 up, clockwise
  int octant = static_cast<int>(std::lround(angle / (3.14159265f / 4.0f)));
  octant = (octant % 8 + 8) % 8;
  static const CompassDirection dirs[8] = {
      CompassDirection::N, CompassDirection::NE, CompassDirection::E, CompassDirection::SE,
      CompassDirection::S, CompassDirection::SW, CompassDirection::W, CompassDirection::NW};
  return dirs[octant];
}

void Ambient::drawShadows(SDL_Renderer *renderer, const WorldRenderer &view,
                          const WorldMap &map) const {
  for (const Flyer &f : this->flyers) {
    const Kind &k = this->kinds[f.kind];
    Animation *a = k.shadow ? k.shadow : nullptr;
    if (!a)
      continue;
    uint32_t ms = a->frameTimeMs() ? a->frameTimeMs() : 100;
    int frame = static_cast<int>(f.age * 1000.0f / ms);
    float sx, sy, d;
    view.worldToScreenF(f.x, f.y, this->groundAt(map, f.x, f.y), sx, sy, d);
    SDL_Color black = {0, 0, 0, 140};
    a->drawAnchored(renderer, sx, sy, this->facing(view, f), k.blackShadow ? &black : nullptr,
                    frame);
  }
}

void Ambient::drawFlyers(SDL_Renderer *renderer, const WorldRenderer &view,
                         const WorldMap &map) const {
  for (const Flyer &f : this->flyers) {
    const Kind &k = this->kinds[f.kind];
    uint32_t ms = k.idle->frameTimeMs() ? k.idle->frameTimeMs() : 100;
    int frame = static_cast<int>(f.age * 1000.0f / ms);
    float sx, sy, d;
    view.worldToScreenF(f.x, f.y, this->groundAt(map, f.x, f.y) + kAltitude, sx, sy, d);
    k.idle->drawAnchored(renderer, sx, sy, this->facing(view, f), nullptr, frame);
  }
}
