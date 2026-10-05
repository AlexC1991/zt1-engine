#include "PlacedObjects.hpp"
#include "PalletManager.hpp"
#include "IniReader.hpp"

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

Animation *PlacedObjects::artOfFile(const std::string &file) {
  size_t slash = file.find_last_of('/'), dot = file.find_last_of('.');
  if (slash == std::string::npos)
    return nullptr;
  std::string type = file.substr(slash + 1, dot == std::string::npos ? std::string::npos : dot - slash - 1);
  return this->artFor("objects/" + type + "/idle/idle");
}

const PlacedObjects::Colouring *PlacedObjects::colouringOf(const std::string &file) {
  auto it = this->colourings.find(file);
  if (it != this->colourings.end())
    return it->second;
  Colouring *c = nullptr;
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(file)) {
      if (ai->getInt("characteristics/integers", "ciscolorreplaced", 0)) {
        std::vector<std::string> parts = ai->getList("colorrep", "replace");
        std::vector<std::string> titles = ai->getList("colorrep", "title");
        std::vector<std::string> defaults = ai->getList("colorrep", "defaultpal");
        std::string colour = ai->get("colorrep", "color");
        IniReader *shared = this->rm->getIniReader("building.ai");
        if (!parts.empty() && !colour.empty() && shared) {
          c = new Colouring();
          c->fullPal = ai->get(colour, "fullpal");
          c->fixed = ai->getInt(colour, "ncolors", 232);
          for (size_t p = 0; p < parts.size(); p++) {
            ColourPart part;
            part.title = p < titles.size() ? std::atoi(titles[p].c_str()) : 0;
            part.size = shared->getInt(parts[p], "ncolors", 16);
            part.pals = shared->getList(parts[p], "pal");
            for (const std::string &ui : shared->getList(parts[p], "ui_info")) {
              std::vector<std::string> fc = shared->getList(ui, "forecolor");
              SDL_Color s = {0, 0, 0, 255};
              if (fc.size() >= 3)
                s = {static_cast<Uint8>(std::atoi(fc[0].c_str())), static_cast<Uint8>(std::atoi(fc[1].c_str())),
                     static_cast<Uint8>(std::atoi(fc[2].c_str())), 255};
              part.swatches.push_back(s);
            }
            part.swatches.resize(part.pals.size(), SDL_Color{0, 0, 0, 255});
            for (size_t i = 0; i < part.pals.size() && p < defaults.size(); i++)
              if (part.pals[i] == defaults[p])
                part.fallback = static_cast<int>(i);
            c->parts.push_back(part);
          }
        }
        delete shared;
      }
      delete ai;
    }
  this->colourings[file] = c;
  return c;
}

int PlacedObjects::colourOf(const Object &o, int part) {
  const Colouring *c = this->colouringOf(fileOf(o));
  if (!c || part < 0 || part >= static_cast<int>(c->parts.size()))
    return -1;
  int pick = part < static_cast<int>(o.colours.size()) ? o.colours[part] : -1;
  return pick >= 0 && pick < static_cast<int>(c->parts[part].pals.size()) ? pick : c->parts[part].fallback;
}

// The full palette with each part's colours swapped in (after its own
// ncolors: 16 for the first part, 8 for the second), as guests' clothes
Animation *PlacedObjects::paintedArtOf(const Object &o, const std::string &layer) {
  std::string path = "objects/" + o.typeName + "/" + layer + "/" + layer;
  const Colouring *c = this->colouringOf(fileOf(o));
  if (!c || !this->rm)
    return this->rm && this->rm->hasResource(path + ".ani") ? this->artFor(path) : (layer == "idle" ? o.art : nullptr);
  std::string key = path;
  for (size_t p = 0; p < c->parts.size(); p++)
    key += "#" + std::to_string(this->colourOf(o, static_cast<int>(p)));
  auto it = this->paintedArt.find(key);
  if (it != this->paintedArt.end())
    return it->second;
  Animation *a = nullptr;
  PalletManager *pm = this->rm->getPalletManager();
  std::string full = c->fullPal;
  Pallet *base = full.empty() ? nullptr : pm->getPallet(full);
  if (base && this->rm->hasResource(path + ".ani")) {
    Pallet variant = *base;
    int at = c->fixed;
    for (size_t p = 0; p < c->parts.size(); p++) {
      const ColourPart &part = c->parts[p];
      int pick = this->colourOf(o, static_cast<int>(p));
      std::string name = pick >= 0 ? part.pals[pick] : std::string();
      Pallet *pal = name.empty() ? nullptr : pm->getPallet(name);
      for (int i = 0; pal && i < part.size; i++)
        if (pal->color_count > static_cast<uint32_t>(i + 1) && variant.color_count > static_cast<uint32_t>(at + i))
          variant.colors[at + i] = pal->colors[i + 1];
      at += part.size;
    }
    std::string name2 = full + "#" + key;
    pm->addPallet(name2, variant);
    PalletManager::setOverride(full, name2);
    a = this->rm->getAnimation(path);
    PalletManager::clearOverrides();
  }
  if (!a && layer == "idle")
    a = o.art;
  this->paintedArt[key] = a;
  return a;
}

void PlacedObjects::loadLayers(Object &o) {
  // (a trash can's half-full art)
  if (o.typeName == "trshcan") {
    std::string path = "objects/" + o.typeName + "/half/half";
    o.half = this->rm && this->rm->hasResource(path + ".ani") ? this->artFor(path) : nullptr;
  }
  if (o.subClass != "building")
    return;
  // (a building that can be painted starts in its default colours)
  if (this->colouringOf(fileOf(o)))
    o.art = this->paintedArtOf(o, "idle");
  o.bg = this->paintedArtOf(o, "bg");
  o.used = this->paintedArtOf(o, "used");
}


void PlacedObjects::setColour(Object &o, int part, int choice) {
  const Colouring *c = this->colouringOf(fileOf(o));
  if (!c || part < 0 || part >= static_cast<int>(c->parts.size()))
    return;
  if (static_cast<int>(o.colours.size()) < static_cast<int>(c->parts.size()))
    o.colours.resize(c->parts.size(), -1);
  o.colours[part] = choice;
  o.art = this->paintedArtOf(o);
  this->loadLayers(o);
}

bool PlacedObjects::add(const std::string &file, float x, float y, int facing) {
  // scenery/<subclass>/<type>.ai
  size_t a = file.find('/'), b = file.find('/', a + 1), dot = file.find_last_of('.');
  if (a == std::string::npos || b == std::string::npos)
    return false;
  Object o;
  o.subClass = file.substr(a + 1, b - a - 1);
  o.typeName = file.substr(b + 1, dot == std::string::npos ? std::string::npos : dot - b - 1);
  // (as the map file names them: rocks and foliage are "objects")
  o.className = o.subClass == "building" ? "building" : "objects";
  o.bought = true;
  o.x = x;
  o.y = y;
  o.facing = facing & 7;
  o.art = this->artOfFile(file);
  if (!o.art)
    return false;
  o.id = this->nextId++;
  this->loadLayers(o);
  // Buildings are numbered by type ("Snack Machine 1"; cUseNumbersInName 0
  // and scenery go without)
  if (o.subClass == "building" && this->rm)
    if (IniReader *ai = this->rm->getIniReader(file)) {
      const std::string ints = "characteristics/integers";
      int nameId = ai->getInt(ints, "cnameid", 0);
      o.nameId = nameId;
      bool numbers = ai->getInt(ints, "cusenumbersinname", 1) != 0;
      std::string cost = ai->get("characteristics/floats", "cdefaultcost");
      o.price = cost.empty() ? -1.0f : static_cast<float>(std::atof(cost.c_str()));
      delete ai;
      o.label = this->rm->getString(nameId);
      if (numbers)
        o.label += " " + std::to_string(++this->counts[o.typeName]);
    }
  this->list.push_back(o);
  return true;
}

void PlacedObjects::remove(int index) {
  if (index < 0 || index >= static_cast<int>(this->list.size()))
    return;
  this->list.erase(this->list.begin() + index);
  this->highlight = -1;
}

int PlacedObjects::pick(float px, float py, const WorldRenderer &view, const WorldMap &map) const {
  int found = -1;
  float best = -1e30f;
  for (size_t i = 0; i < this->list.size(); i++) {
    const Object &o = this->list[i];
    // Scenery, and buildings bought (not the map's own, like the entrance)
    if (o.fence || (o.className != "objects" && !o.bought) || !o.art || this->hiddenNow(o))
      continue;
    float z = o.z;
    if (const MapTile *t = map.getTile(static_cast<int>(std::floor(o.x)), static_cast<int>(std::floor(o.y)))) {
      float fx = o.x - std::floor(o.x), fy = o.y - std::floor(o.y);
      float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
      float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
      z = top * (1 - fy) + bottom * fy;
    }
    float sx, sy, depth;
    view.worldToScreenF(o.x, o.y, z, sx, sy, depth);
    int w = 0, h = 0;
    float angle = o.facing * 3.14159265f / 4.0f;
    o.art->queryTexture(view.screenSide(std::sin(angle), -std::cos(angle)), &w, &h);
    if (w <= 0 || h <= 0)
      continue;
    // Its art stands over its anchor (about its foot)
    float hw = std::max(10.0f, w * 0.4f);
    if (px < sx - hw || px > sx + hw || py < sy - h * 0.9f || py > sy + std::max(6.0f, h * 0.12f))
      continue;
    if (depth > best) {
      best = depth;
      found = static_cast<int>(i);
    }
  }
  return found;
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

std::pair<int, int> PlacedObjects::footprintOf(const Object &o) {
  std::string file = fileOf(o);
  auto it = this->footprints.find(file);
  if (it != this->footprints.end())
    return it->second;
  std::pair<int, int> fp{0, 0};
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(file)) {
      fp.first = ai->getInt("characteristics/integers", "cfootprintx", 0);
      fp.second = ai->getInt("characteristics/integers", "cfootprinty", 0);
      delete ai;
    }
  this->footprints[file] = fp;
  return fp;
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
    // A strip of a big object: only the screen columns from clipX0 to
    // clipX1 drawn (clip: none)
    bool clip = false;
    float clipX0 = 0, clipX1 = 0;
    float clipY1 = 1e9f;
  };
  std::vector<Drawn> drawn;
  drawn.reserve(this->list.size());
  for (const Object &o : this->list) {
    if (this->hiddenNow(o))
      continue;
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
    static const SDL_Color red{255, 60, 50, 255};
    bool lit = static_cast<int>(&o - this->list.data()) == this->highlight;
    if (o.inUse && o.used)
      art = o.used;
    if (o.half && o.fill > 12)
      art = o.half;
    // A big one (more than a tile across): drawn in strips a quarter tile
    // wide, each sorted at the front of its footprint in that strip, so a
    // fence or animal behind part of it goes behind that part and one in
    // front goes in front (sorted as a whole at its middle, the fence along
    // its back went through it)
    std::pair<int, int> fp = o.fence ? std::pair<int, int>{0, 0} : this->footprintOf(o);
    if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
      std::swap(fp.first, fp.second);
    if (fp.first > 2 || fp.second > 2) {
      float hx = fp.first / 4.0f, hy = fp.second / 4.0f;
      float cxs[4], cds[4];
      const float px[4] = {x - hx, x + hx, x + hx, x - hx}, py[4] = {y - hy, y - hy, y + hy, y + hy};
      for (int k = 0; k < 4; k++) {
        float csy;
        view.worldToScreenF(px[k], py[k], z, cxs[k], csy, cds[k]);
      }
      int L = 0, R = 0, F = 0;
      for (int k = 1; k < 4; k++) {
        if (cxs[k] < cxs[L]) L = k;
        if (cxs[k] > cxs[R]) R = k;
        if (cds[k] > cds[F]) F = k;
      }
      // The footprint's front edge, seen from above: the most forward depth
      // at a screen x
      auto front = [&](float at) {
        if (at <= cxs[F]) {
          float w = cxs[F] - cxs[L];
          return w > 0.001f ? cds[L] + (at - cxs[L]) / w * (cds[F] - cds[L]) : cds[F];
        }
        float w = cxs[R] - cxs[F];
        return w > 0.001f ? cds[R] + (cxs[R] - at) / w * (cds[F] - cds[R]) : cds[F];
      };
      float step = std::max(4.0f, view.getTileWidth() / 4.0f);
      int strips = std::max(1, static_cast<int>(std::ceil((cxs[R] - cxs[L]) / step)));
      for (int k = 0; k < strips; k++) {
        float xa = cxs[L] + k * step, xb = std::min(cxs[R], xa + step);
        // (at its middle, as a fence piece's halves are: one along its front
        // goes in front of it all)
        float d = front((xa + xb) * 0.5f) - 0.02f;
        if (under && under(x, y))
          d -= 1.0f;
        Drawn strip{d, sx, sy, &o, art, side, lit ? &red : nullptr};
        strip.clip = true;
        // (the first and last reach out to the art's edges: eaves)
        strip.clipX0 = k == 0 ? -1e9f : xa;
        strip.clipX1 = k == strips - 1 ? 1e9f : xb;
        if (o.bg) {
          Drawn back = strip;
          back.art = o.bg;
          back.depth -= 0.001f;
          drawn.push_back(back);
        }
        drawn.push_back(strip);
      }
      continue;
    }
    if (o.bg)
      drawn.push_back({depth - 0.01f, sx, sy, &o, o.bg, side, lit ? &red : nullptr});
    drawn.push_back({depth, sx, sy, &o, art, side, lit ? &red : nullptr});
  }
  for (const Fences::Drawable &f : fences)
    drawn.push_back({f.depth, f.sx, f.sy, nullptr, f.art, f.side,
                     f.tinted ? &f.tint : nullptr, f.frame, f.custom ? &f.custom : nullptr,
                     f.clip, f.clipX0, f.clipX1, f.clipY1});
  std::stable_sort(drawn.begin(), drawn.end(),
                   [](const Drawn &a, const Drawn &b) { return a.depth < b.depth; });

  SDL_Rect oldClip;
  SDL_RenderGetClipRect(renderer, &oldClip);
  bool hadClip = SDL_RenderIsClipEnabled(renderer);
  float sxScale = 1, syScale = 1;
  SDL_RenderGetScale(renderer, &sxScale, &syScale);
  for (const Drawn &d : drawn) {
    if (d.clip) {
      // (the strip's columns, the whole height; within any clip there was)
      int x0 = d.clipX0 < -1e8f ? -100000 : static_cast<int>(std::floor(d.clipX0));
      int x1 = d.clipX1 > 1e8f ? 100000 : static_cast<int>(std::ceil(d.clipX1));
      int y1 = d.clipY1 > 1e8f ? 100000 : static_cast<int>(std::ceil(d.clipY1));
      SDL_Rect r = {x0, -100000, x1 - x0, y1 + 100000};
      if (hadClip) {
        SDL_Rect both;
        if (!SDL_IntersectRect(&r, &oldClip, &both))
          continue;
        r = both;
      }
      SDL_RenderSetClipRect(renderer, &r);
    }
    if (d.custom)
      (*d.custom)(renderer);
    else if (d.art)
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tint, d.frame);
    if (d.clip)
      SDL_RenderSetClipRect(renderer, hadClip ? &oldClip : nullptr);
  }
  (void)sxScale;
  (void)syScale;
}
