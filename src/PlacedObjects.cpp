#include "PlacedObjects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

#include "Animation.hpp"
#include "ResourceManager.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include "ZooReader.hpp"

PlacedObjects::~PlacedObjects() { this->clear(); }

void PlacedObjects::clear() {
  this->list.clear();
  for (auto &kv : this->art)
    delete kv.second;
  this->art.clear();
}

Animation *PlacedObjects::artFor(const std::string &path) {
  auto it = this->art.find(path);
  if (it != this->art.end())
    return it->second;
  Animation *a = this->rm && this->rm->hasResource(path + ".ani")
                     ? this->rm->getAnimation(path)
                     : nullptr;
  this->art[path] = a;
  return a;
}

void PlacedObjects::load(const ZooReader &reader, ResourceManager *rm) {
  this->clear();
  this->rm = rm;
  for (const ZooReader::ZooObject &obj : reader.getObjects()) {
    // Paths are drawn with the terrain, fences by Fences, ambient animals
    // not yet
    if (obj.className == "paths" || obj.className == "ambient" ||
        obj.className == "fences")
      continue;
    Object o;
    o.className = obj.className;
    o.subClass = obj.subClass;
    o.typeName = obj.typeName;
    o.name = obj.name;
    o.x = obj.x / 64.0f;
    o.y = obj.y / 64.0f;
    o.z = obj.z / 16.0f;
    // The payload's fifth word is the facing
    if (obj.payload.size() >= 20) {
      uint32_t f = 0;
      std::memcpy(&f, obj.payload.data() + 16, 4);
      o.facing = static_cast<int>(f & 7);
    }
    o.fence = obj.className == "fences";
    if (o.fence) {
      std::string base = "fences/" + obj.subClass + "/" + obj.typeName + "/";
      o.art = this->artFor(base + "idle/idle");
      o.slopeUp = this->artFor(base + "idle30p/idle30p");
      o.slopeDown = this->artFor(base + "idle30n/idle30n");
    } else {
      o.art = this->artFor("objects/" + obj.typeName + "/idle/idle");
    }
    if (o.art)
      this->list.push_back(o);
  }
  SDL_Log("[PlacedObjects] %zu objects with art", this->list.size());
}

// A tile's corner height (TileCorner order X0Y0, X1Y0, X1Y1, X0Y1)
static int cornerHeight(const WorldMap &map, int tx, int ty, int corner) {
  const MapTile *t = map.getTile(tx, ty);
  return t ? t->cornerHeight[corner] : 0;
}

void PlacedObjects::draw(SDL_Renderer *renderer, const WorldRenderer &view,
                         const WorldMap &map,
                         const std::vector<Fences::Drawable> &fences,
                         const std::function<bool(float x, float y)> &under) {
  struct Drawn {
    float depth, sx, sy;
    const Object *o;
    Animation *art;
    CompassDirection side;
    const SDL_Color *tint;
    int frame = -1;
    const std::function<void(SDL_Renderer *)> *custom = nullptr;
  };
  std::vector<Drawn> drawn;
  drawn.reserve(this->list.size());
  for (const Object &o : this->list) {
    // The facing as a world direction, then as the screen side it shows
    float angle = o.facing * 3.14159265f / 4.0f;
    float fx = std::sin(angle), fy = -std::cos(angle);
    CompassDirection side = view.screenSide(fx, fy);
    Animation *art = o.art;
    float x = o.x, y = o.y, z = o.z;
    if (o.fence) {
      // The tile side it stands on: its two corners, and their heights
      int tx = static_cast<int>(std::floor(o.x)), ty = static_cast<int>(std::floor(o.y));
      int c0, c1; // corners in world order along the edge
      int ax, ay, bx, by;
      switch (o.facing & 6) {
      case 0: c0 = CORNER_X0Y0; c1 = CORNER_X1Y0; ax = tx; ay = ty; bx = tx + 1; by = ty; break;
      case 4: c0 = CORNER_X0Y1; c1 = CORNER_X1Y1; ax = tx; ay = ty + 1; bx = tx + 1; by = ty + 1; break;
      case 2: c0 = CORNER_X1Y0; c1 = CORNER_X1Y1; ax = tx + 1; ay = ty; bx = tx + 1; by = ty + 1; break;
      default: c0 = CORNER_X0Y0; c1 = CORNER_X0Y1; ax = tx; ay = ty; bx = tx; by = ty + 1; break;
      }
      int h0 = cornerHeight(map, tx, ty, c0), h1 = cornerHeight(map, tx, ty, c1);
      x = (ax + bx) * 0.5f;
      y = (ay + by) * 0.5f;
      z = (h0 + h1) * 0.5f;
      if (h0 != h1) {
        // Sloped: idle30p where the edge rises going clockwise round its
        // tile on screen (NE and NW sides start at their left end, SE and
        // SW at their right), idle30n where it falls
        float s0x, s0y, s1x, s1y, d;
        view.worldToScreenF(static_cast<float>(ax), static_cast<float>(ay), 0, s0x, s0y, d);
        view.worldToScreenF(static_cast<float>(bx), static_cast<float>(by), 0, s1x, s1y, d);
        bool startLeft = side == CompassDirection::NE || side == CompassDirection::NW;
        bool firstIsLeft = s0x < s1x;
        int hStart = (firstIsLeft == startLeft) ? h0 : h1;
        int hEnd = (firstIsLeft == startLeft) ? h1 : h0;
        Animation *slope = hEnd > hStart ? o.slopeUp : o.slopeDown;
        if (slope)
          art = slope;
      }
    }
    // Standing on the ground as it is now (it sinks with a tank, follows
    // terraforming)
    if (!o.fence) {
      const MapTile *t = map.getTile(static_cast<int>(std::floor(x)),
                                     static_cast<int>(std::floor(y)));
      if (t) {
        float fx = x - std::floor(x), fy = y - std::floor(y);
        float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
        float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
        z = top * (1 - fy) + bottom * fy;
      }
    }
    float sx, sy, depth;
    view.worldToScreenF(x, y, z, sx, sy, depth);
    // Under a walkway deck: drawn before it (a deck sorts half a tile on)
    if (under && under(x, y))
      depth -= 1.0f;
    drawn.push_back({depth, sx, sy, &o, art, side, nullptr});
  }
  for (const Fences::Drawable &f : fences)
    drawn.push_back({f.depth, f.sx, f.sy, nullptr, f.art, f.side,
                     f.tinted ? &f.tint : nullptr, f.frame, f.custom ? &f.custom : nullptr});
  std::stable_sort(drawn.begin(), drawn.end(),
                   [](const Drawn &a, const Drawn &b) { return a.depth < b.depth; });

  for (const Drawn &d : drawn)
    if (d.custom)
      (*d.custom)(renderer);
    else if (d.art)
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tint, d.frame);
}
