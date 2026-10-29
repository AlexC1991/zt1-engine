#include "PlacedObjects.hpp"
#include "ItemCatalog.hpp"
#include "Utils.hpp"
#include "PalletManager.hpp"
#include "IniReader.hpp"
#include "Features.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <cstring>
#include <cstdlib>

#include "Animation.hpp"
#include "ResourceManager.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include "ZooReader.hpp"

namespace FrameProfile { extern bool active; void record(const char *name, double elapsed); }
namespace { bool cullWorldSprites = true; }
// Reference path for the Lab's pixel comparisons; normal drawing always enables it.
extern void worldTerrainCulling(bool enabled);
void worldSpriteCulling(bool enabled) { cullWorldSprites = enabled; worldTerrainCulling(enabled); }

PlacedObjects::~PlacedObjects() { this->clear(); }

void PlacedObjects::clear() {
  this->list.clear();
  this->nextId = 1;
  this->counts.clear();
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
  std::string path = "objects/" + type + "/idle/idle";
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(file)) {
      std::string own = Utils::string_to_lower(ai->get("animations", "idle"));
      if (own.empty() && Features::mods) own = Utils::string_to_lower(ai->get("animations", "empty"));
      delete ai;
      if (!own.empty()) {
        if (own.size() > 4 && own.compare(own.size() - 4, 4, ".ani") == 0) own.resize(own.size() - 4);
        path = own.find('/') == std::string::npos ? "objects/" + type + "/" + own + "/" + own : own;
      }
    }
  return this->artFor(path);
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
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(fileOf(o))) {
      std::string own = Utils::string_to_lower(ai->get("animations", layer));
      delete ai;
      if (!own.empty()) {
        if (own.size() > 4 && own.compare(own.size() - 4, 4, ".ani") == 0) own.resize(own.size() - 4);
        path = own.find('/') == std::string::npos ? "objects/" + o.typeName + "/" + own + "/" + own : own;
      }
    }
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
  o.trashCan = o.typeName == "trshcan";
  if (Features::mods && this->rm)
    if (IniReader *ai = this->rm->getIniReader(fileOf(o))) {
      o.trashCan = o.trashCan || (!ai->get("animations", "empty").empty() &&
                                  !ai->get("animations", "half").empty() && !ai->get("animations", "full").empty());
      delete ai;
    }
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(fileOf(o))) {
      const std::string ints = "characteristics/integers";
      o.onWater=ai->getInt(ints,"csurface",0)!=0;
      // (a thing that stands half in the water: its underidle is the part below
      // the surface, idle the part on it)
      if (ai->getInt(ints, "chasunderwatersection", 0) != 0 && ai->getInt(ints, "cunderwater", 0) != 0 &&
          ai->getInt(ints, "csurface", 0) != 0)
        o.underIdle = this->paintedArtOf(o, "underidle");
      o.randomUse = ai->getInt(ints, "crandomuse", 0) != 0;
      if (o.randomUse) {
        o.minUsePeriod = std::max(1, ai->getInt(ints, "cminuseperiod", 20));
        o.maxUsePeriod = std::max(o.minUsePeriod, static_cast<float>(ai->getInt(ints, "cmaxuseperiod", 60)));
        o.useAnimCount = std::max(1, ai->getInt(ints, "cuseanimcount", 1));
        if (!o.randomActive && o.randomWait <= 0)
          o.randomWait = std::uniform_real_distribution<float>(o.minUsePeriod, o.maxUsePeriod)(randomUseRng);
        o.used = this->paintedArtOf(o, "used");
      }
      delete ai;
    }
  if (o.trashCan) o.half = this->paintedArtOf(o, "half");
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

// A map's or save's object names only its type: its file is the .ai, or a
// downloaded entity's .ucs / .ucb / .uca when there's no .ai by that name
void PlacedObjects::resolveExt(Object &o) const {
  o.ext = ".ai";
  if (!this->rm || this->rm->hasResource(fileOf(o)))
    return;
  for (const char *e : {".ucs", ".ucb", ".uca"}) {
    o.ext = e;
    if (this->rm->hasResource(fileOf(o)))
      return;
  }
  o.ext = ".ai";
}

bool PlacedObjects::add(const std::string &file, float x, float y, int facing) {
  // scenery/<subclass>/<type>.ai
  size_t a = file.find('/'), b = file.find('/', a + 1), dot = file.find_last_of('.');
  if (a == std::string::npos || b == std::string::npos)
    return false;
  Object o;
  o.subClass = file.substr(a + 1, b - a - 1);
  o.typeName = file.substr(b + 1, dot == std::string::npos ? std::string::npos : dot - b - 1);
  if (dot != std::string::npos && dot > b)
    o.ext = file.substr(dot);
  // (as the map file names them: rocks and foliage are "objects"; the
  // entrance's pieces, under scenery/other/, "building")
  o.className = o.subClass == "building" || (this->isEntrance && this->isEntrance(o.typeName)) ? "building" : "objects";
  o.bought = true;
  o.x = x;
  o.y = y;
  o.facing = facing & 7;
  o.art = this->artOfFile(file);
  if (!o.art)
    return false;
  o.id = this->nextId++;
  this->loadLayers(o);
  // Its cNameID, which a show trick names as the object it needs (zoo.exe
  // loads the tricks' "building" and matches the object placed in the tank
  // by it): every object has one, not only the buildings - a show toy
  // (objects/orcaball: cNameID 6502) is scenery.
  if (this->rm)
    if (IniReader *ai = this->rm->getIniReader(file)) {
      const std::string ints = "characteristics/integers";
      int nameId = ai->getInt(ints, "cnameid", 0);
      o.nameId = nameId;
      // Buildings are numbered by type ("Snack Machine 1"; cUseNumbersInName 0
      // and scenery go without)
      if (o.subClass == "building") {
        bool numbers = ai->getInt(ints, "cusenumbersinname", 1) != 0;
        std::string cost = ai->get("characteristics/floats", "cdefaultcost");
        o.price = cost.empty() ? -1.0f : static_cast<float>(std::atof(cost.c_str()));
        const CatalogItem *item = ItemCatalog::get().find(file);
        o.label = item ? item->name : this->rm->getString(nameId);
        if (numbers)
          o.label += " " + std::to_string(++this->counts[o.typeName]);
      }
      delete ai;
    }
  this->list.push_back(o);
  return true;
}

void PlacedObjects::explode(Object &o) {
  o.explodeArt = nullptr;
  o.exploding = -1;
  if (!this->rm)
    return;
  std::string path;
  if (IniReader *ai = this->rm->getIniReader(fileOf(o))) {
    path = ai->get("animations", "explode");
    delete ai;
  }
  // (objects/brubble2/used/used.ani: the art's name without .ani)
  if (path.size() > 4 && path.compare(path.size() - 4, 4, ".ani") == 0)
    path.resize(path.size() - 4);
  if (path.empty() || !this->rm->hasResource(path + ".ani"))
    return;
  o.explodeArt = this->artFor(path);
  if (o.explodeArt)
    o.exploding = 0;
}

int PlacedObjects::animationFrame(const Object &o, const Animation *art) {
  if (!art || art->frameCount() <= 1) return 0;
  // Idle and used can point to the same cached animation (aviary, boat
  // shack, changing room). The active state, not pointer equality alone,
  // chooses the clock; otherwise those buildings freeze when unoccupied.
  const double time = art == o.used && o.randomActive ? o.randomTime
                    : art == o.used && o.inUse ? o.useTime : o.animTime;
  return static_cast<int>(std::fmod(time * 1000.0 / std::max(1u, art->frameTimeMs()), art->frameCount()));
}

void PlacedObjects::tick(float seconds) {
  if (seconds <= 0) return;
  for (Object &o : this->list) {
    o.animTime += seconds;
    o.useTime = o.inUse ? (o.wasInUse ? o.useTime + seconds : 0) : 0;
    o.wasInUse = o.inUse;
    o.randomStarted = false;
    if (o.randomUse && o.used) {
      double remaining = seconds;
      const double length = std::max(0.001, o.used->frameCount() * std::max(1u, o.used->frameTimeMs()) * 0.001 * o.useAnimCount);
      while (remaining > 0) {
        if (o.randomActive) {
          const double step = std::min(remaining, std::max(0.0, length - o.randomTime));
          o.randomTime += step; remaining -= step;
          if (o.randomTime >= length) {
            o.randomActive = false; o.randomTime = 0;
            o.randomWait = std::uniform_real_distribution<float>(o.minUsePeriod, o.maxUsePeriod)(randomUseRng);
          }
        } else {
          const double step = std::min(remaining, std::max(0.0, o.randomWait));
          o.randomWait -= step; remaining -= step;
          if (o.randomWait <= 0) { o.randomActive = true; o.randomTime = 0; o.randomStarted = true; }
        }
      }
    }
    if (o.exploding >= 0 && o.explodeArt) {
      o.exploding += seconds;
      if (o.exploding * 1000.0f >= o.explodeArt->frameCount() * o.explodeArt->frameTimeMs())
        o.exploding = -1;
    }
  }
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
    if(this->tankHeight)z=this->tankHeight(o,z);
    view.worldToScreenF(o.x, o.y, z, sx, sy, depth);
    const float angle=o.facing*3.14159265f/4.0f;
    const auto side=view.screenSide(std::sin(angle),-std::cos(angle));
    bool hit=false;
    for(Animation *layer:{displayArt(o),o.bg,o.underIdle})
      if(layer && layer->hitTestAnchored(px,py,sx,sy,side,animationFrame(o,layer)))hit=true;
    if(!hit)continue;
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
  // Keep original object identities: guests, shows and jobs refer to them.
  for (const auto &obj : reader.getObjects())
    if (obj.id > 0 && obj.id < static_cast<uint32_t>(INT32_MAX))
      this->nextId = std::max(this->nextId, static_cast<int>(obj.id) + 1);
  std::set<int> usedIds;
  for (const ZooReader::ZooObject &obj : reader.getObjects()) {
    // Paths are drawn with the terrain, fences by Fences, ambient animals
    // not yet
    if (obj.className == "paths" || obj.className == "ambient" ||
        obj.className == "fences")
      continue;
    // (a saved zoo's tank walls are Fences', its food and dung ZooItems', its
    // animals, guests and staff live: SaveGame::loadOriginal)
    static const std::set<std::string> live = {"tankwall", "filter", "food", "animals", "guests", "staff",
                                               "keeper",   "maint", "tour", "helicopter"};
    if (live.count(obj.className) || obj.typeName == "poo" || obj.typeName == "dinopoo" || obj.typeName == "trash")
      continue;
    Object o;
    o.className = obj.className;
    o.subClass = obj.subClass;
    o.typeName = obj.typeName;
    resolveExt(o);
    o.name = obj.name;
    o.label = obj.name;
    o.id = obj.id > 0 && obj.id < static_cast<uint32_t>(INT32_MAX) ? static_cast<int>(obj.id) : this->nextId++;
    if (!usedIds.insert(o.id).second) { o.id = this->nextId++; usedIds.insert(o.id); }
    o.x = obj.x / 64.0f;
    o.y = obj.y / 64.0f;
    o.z = obj.z / 16.0f;
    // Its direction (loadUnitCommon +0x12c; the payload's fifth word for
    // scenery, where a record wasn't decoded)
    if (obj.decoded) {
      o.facing = static_cast<int>(obj.direction & 7);
    } else if (obj.payload.size() >= 20) {
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
      o.art = this->artOfFile(fileOf(o));
      o.colours.assign(obj.colours.begin(), obj.colours.end());
      this->loadLayers(o);
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

void PlacedObjects::drawInTank(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map, int tank,
                               bool surface, float level, const std::function<bool(const std::string &)> &onSurface) {
  // Upper toy layers sort with platforms and front walls in draw().
  // Only their submerged halves belong in the water composite.
  if(surface)return;
  struct One {
    float depth, sx, sy;
    const Object *o;
    CompassDirection side;
  };
  std::vector<One> all;
  for (const Object &o : this->list) {
    if (o.fence || this->hiddenNow(o) || !this->tankAt || this->tankAt(o.x, o.y) != tank)
      continue;
    // A thing with both halves (a show tank's toys) is in BOTH passes: its
    // underidle below the water, its idle on the surface. Anything else is in
    // the one pass its kind belongs to.
    if (!o.underIdle && onSurface && onSurface(fileOf(o)) != surface)
      continue;
    float angle = o.facing * 3.14159265f / 4.0f;
    CompassDirection side = view.screenSide(std::sin(angle), -std::cos(angle));
    float z = o.z;
    if (const MapTile *t = map.getTile(static_cast<int>(std::floor(o.x)), static_cast<int>(std::floor(o.y))))
      z = static_cast<float>(t->cornerHeight[0]);
    // (half in the water: its body is at the surface, its underidle hung below
    // it - the tank's own water pass is drawn between the two)
    if (surface || o.underIdle)
      z = level;
    float sx, sy, depth;
    if(this->tankHeight)z=this->tankHeight(o,z);
    view.worldToScreenF(o.x, o.y, z, sx, sy, depth);
    all.push_back({depth, sx, sy, &o, side});
  }
  std::stable_sort(all.begin(), all.end(), [](const One &a, const One &b) { return a.depth < b.depth; });
  static const SDL_Color red{255, 60, 50, 255};
  for (const One &d : all) {
    // (the bulldozer over it: red, as everywhere)
    const SDL_Color *tint = static_cast<int>(d.o - this->list.data()) == this->highlight ? &red : nullptr;
    if (d.o->bg)
      d.o->bg->drawAnchored(renderer, d.sx, d.sy, d.side, tint, animationFrame(*d.o, d.o->bg));
    // (under the water, the underidle; on the surface, idle)
    Animation *art = d.o->underIdle && !surface ? d.o->underIdle : displayArt(*d.o);
    if (art)
      art->drawAnchored(renderer, d.sx, d.sy, d.side, tint, animationFrame(*d.o, art));
  }
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
  const bool profiling = FrameProfile::active;
  const Uint64 setupStart = profiling ? SDL_GetPerformanceCounter() : 0;
  double customMs = 0, spriteMs = 0, clipMs = 0;
  auto elapsed = [](Uint64 start) { return 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency(); };
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
    bool seamless = false; // (a tank wall's piece: drawLayered with its neighbours)
  };
  std::vector<Drawn> drawn;
  drawn.reserve(this->list.size());
  for (const Object &o : this->list) {
    if (this->hiddenNow(o))
      continue;
    // Floor scenery stays under the water. A toy's upper layer joins
    // the world depth sort so a nearby platform does not always cover it.
    const bool inTank=!o.fence && this->tankAt && this->tankAt(o.x,o.y)>=0;
    if(inTank && !o.onWater && !o.underIdle)continue;
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
    if(inTank && this->tankHeight)z=this->tankHeight(o,z);
    view.worldToScreenF(x, y, z, sx, sy, depth);
    // Under a walkway deck: drawn before it (a deck sorts half a tile on)
    if (under && under(x, y))
      depth -= 1.0f;
    static const SDL_Color red{255, 60, 50, 255};
    bool lit = static_cast<int>(&o - this->list.data()) == this->highlight;
    if ((o.inUse || o.randomActive) && o.used)
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
        Drawn strip{d, sx, sy, &o, art, side, lit ? &red : nullptr, animationFrame(o, art)};
        strip.clip = true;
        // (the first and last reach out to the art's edges: eaves)
        strip.clipX0 = k == 0 ? -1e9f : xa;
        strip.clipX1 = k == strips - 1 ? 1e9f : xb;
        if (o.bg) {
          Drawn back = strip;
          back.art = o.bg;
          back.frame = animationFrame(o, o.bg);
          back.depth -= 0.001f;
          drawn.push_back(back);
        }
        drawn.push_back(strip);
      }
      continue;
    }
    if (o.bg)
      drawn.push_back({depth - 0.01f, sx, sy, &o, o.bg, side, lit ? &red : nullptr, animationFrame(o, o.bg)});
    // (rubble just made: its explode, frame by frame from the first)
    if (o.exploding >= 0 && o.explodeArt && o.explodeArt->frameTimeMs() > 0) {
      int f = static_cast<int>(o.exploding * 1000.0f / o.explodeArt->frameTimeMs());
      drawn.push_back({depth, sx, sy, &o, o.explodeArt, side, lit ? &red : nullptr,
                       std::min(f, std::max(0, o.explodeArt->frameCount() - 1))});
      continue;
    }
    drawn.push_back({depth, sx, sy, &o, art, side, lit ? &red : nullptr, animationFrame(o, art)});
  }
  for (const Fences::Drawable &f : fences)
    drawn.push_back({f.depth, f.sx, f.sy, nullptr, f.art, f.side,
                     f.tinted ? &f.tint : nullptr, f.frame, f.custom ? &f.custom : nullptr,
                     f.clip, f.clipX0, f.clipX1, f.clipY1, f.seamless});
  // Do not sort or submit art that cannot touch the view. In particular,
  // off-screen fence halves used to change SDL's clip for every map edge,
  // flushing the renderer even though no pixels from those pieces were visible.
  // Keep custom passes unless their explicit clip is off-screen, and keep
  // unknown art bounds conservatively.
  // Use the art's full canvas, including tall/airborne frames and mirrored anchors.
  if (cullWorldSprites) {
    SDL_Rect viewport;
    SDL_RenderGetViewport(renderer, &viewport);
    const float width = static_cast<float>(viewport.w);
    const float height = static_cast<float>(viewport.h);
    constexpr float margin = 2.0f; // outward rounding at fractional zoom/odd view sizes
    drawn.erase(std::remove_if(drawn.begin(), drawn.end(), [&](const Drawn &d) {
      // A custom tank pass is still bounded by its strip's scissor. Skip an
      // entirely off-screen strip before invoking its water, contents and ground passes.
      if (d.clip && (d.clipX1 < -margin || d.clipX0 > width + margin || d.clipY1 < -margin))
        return true;
      if (d.custom || !d.art) return false;
      // An anchor inside the view can be kept without querying its canvas.
      if (d.sx >= -margin && d.sx <= width + margin && d.sy >= -margin && d.sy <= height + margin)
        return false;
      float left, top, right, bottom;
      if (!d.art->anchoredBounds(d.sx, d.sy, d.side, left, top, right, bottom)) return false;
      return right < -margin || left > width + margin || bottom < -margin || top > height + margin;
    }), drawn.end());
  }
  std::stable_sort(drawn.begin(), drawn.end(),
                   [](const Drawn &a, const Drawn &b) { return a.depth < b.depth; });

  // Clip rects set in the screen's own pixels (at scale 1), each edge
  // rounded once: set in logical pixels, SDL floors each strip's left edge
  // and width apart - at most zooms a column fell between two strips (the
  // ground showing through as a line), and rounding out by hand drew a
  // column twice (see-through water darker there)
  float sxScale = 1, syScale = 1;
  SDL_RenderGetScale(renderer, &sxScale, &syScale);
  SDL_RenderSetScale(renderer, 1.0f, 1.0f);
  SDL_Rect oldClip;
  SDL_RenderGetClipRect(renderer, &oldClip);
  SDL_RenderSetScale(renderer, sxScale, syScale);
  bool hadClip = SDL_RenderIsClipEnabled(renderer);
  if (profiling) FrameProfile::record("sprite preparation", elapsed(setupStart));
  auto setDeviceClip = [&](const SDL_Rect *r) {
    const Uint64 start = profiling ? SDL_GetPerformanceCounter() : 0;
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SDL_RenderSetClipRect(renderer, r);
    SDL_RenderSetScale(renderer, sxScale, syScale);
    if (profiling) clipMs += elapsed(start);
  };
  for (size_t i = 0; i < drawn.size(); i++) {
    const Drawn &d = drawn[i];
    // A run of a tank wall's pieces, one after another in the order: drawn
    // as one picture (Fences.hpp drawLayered)
    if (d.seamless && !d.clip && !d.custom && d.art) {
      size_t j = i;
      while (j < drawn.size() && drawn[j].seamless && !drawn[j].clip && !drawn[j].custom && drawn[j].art)
        j++;
      const Uint64 start = profiling ? SDL_GetPerformanceCounter() : 0;
      drawLayered(renderer, [&] {
        for (size_t k = i; k < j; k++)
          drawn[k].art->drawAnchored(renderer, drawn[k].sx, drawn[k].sy, drawn[k].side, drawn[k].tint, drawn[k].frame);
      });
      if (profiling) spriteMs += elapsed(start);
      i = j - 1;
      continue;
    }
    if (d.clip) {
      // (the strip's columns, the whole height; within any clip there was)
      int x0 = d.clipX0 < -1e8f ? -1000000 : static_cast<int>(std::lround(d.clipX0 * sxScale));
      int x1 = d.clipX1 > 1e8f ? 1000000 : static_cast<int>(std::lround(d.clipX1 * sxScale));
      int y1 = d.clipY1 > 1e8f ? 1000000 : static_cast<int>(std::lround(d.clipY1 * syScale));
      SDL_Rect r = {x0, -1000000, x1 - x0, y1 + 1000000};
      if (r.w <= 0)
        continue;
      if (hadClip) {
        SDL_Rect both;
        if (!SDL_IntersectRect(&r, &oldClip, &both))
          continue;
        r = both;
      }
      setDeviceClip(&r);
    }
    const Uint64 start = profiling ? SDL_GetPerformanceCounter() : 0;
    if (d.custom)
      (*d.custom)(renderer);
    else if (d.art)
      d.art->drawAnchored(renderer, d.sx, d.sy, d.side, d.tint, d.frame);
    if (profiling) (d.custom ? customMs : spriteMs) += elapsed(start);
    if (d.clip)
      setDeviceClip(hadClip ? &oldClip : nullptr);
  }
  if (profiling) {
    FrameProfile::record("tank composite", customMs);
    FrameProfile::record("sprite submission", spriteMs);
    FrameProfile::record("sprite clip changes", clipMs);
  }
}
