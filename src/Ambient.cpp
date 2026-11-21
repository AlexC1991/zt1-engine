#include "Ambient.hpp"
#include "Sound.hpp"

#include <algorithm>
#include <cmath>

#include "IniReader.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"

namespace {
// measured (2026-11-16, the running original, Basic Grass, two bluejays
// cSpeed 40 and two crows cSpeed 50 read at +0x114/+0x118/+0x11c over 21 s
// of the world's clock +0x204): a unit's place is in 64ths of a tile; a flyer
// goes cSpeed of them a second along each axis it moves on - straight 40 and
// 50 a second, on a diagonal 40 or 50 on both axes, not slowed to the
// diagonal; its height (+0x11c) stays 0 (drawn high all the same:
// Ambient::kFlightLevels)
constexpr float kUnitsPerTile = 64.0f;
} // namespace

bool Ambient::allows(int kind) const {
  if (kind < 0 || kind >= static_cast<int>(this->kinds.size()))
    return false;
  if (this->allowed.empty())
    return true;
  // (by cNameID. measured: ff02 lists 5050, bluejay and crow, and the
  // running original's spawn list (world +0x20c) held only bluejay and
  // crow - 5050, which bird.ai and hawk.ai share, finds no kind)
  const int id = this->kinds[kind].nameId;
  if (id == 5050)
    return false;
  return std::find(this->allowed.begin(), this->allowed.end(), id) != this->allowed.end();
}

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
    // (zoo.exe 0x4c2ec9: cSpeed type +0x108, cFrequency +0x10c, both 0
    // without; cSoundLoop +0x110 only when 1, cSoundName. ours: a cSpeed of
    // 50 without - every stock one has it)
    k.speed = static_cast<float>(ai->getInt(ints, "cspeed", 50));
    k.frequency = ai->getInt(ints, "cfrequency", 0);
    k.nameId = ai->getInt(ints, "cnameid", 0);
    k.blackShadow = ai->getInt(ints, "cforceshadowblack", 1) != 0;
    k.idle = anim(ai->get("animations", "idle"));
    k.loop = ai->getInt(ints, "csoundloop", 0) == 1;
    std::string scfg = ai->get("characteristics/strings", "csoundname");
    if (scfg.empty())
      scfg = ai->get(ints, "csoundname");
    if (!scfg.empty())
      if (IniReader *sc = rm->getIniReader(Utils::string_to_lower(scfg))) {
        std::string group = sc->get("ambientlevels", "group", "happy");
        k.chance = sc->getInt(group, "chance", 0);
        std::vector<std::string> sounds = sc->getList(group, "sound"), probs = sc->getList(group, "prob");
        for (size_t i = 0; i < sounds.size(); i++)
          k.calls.push_back({Utils::string_to_lower(sounds[i]), i < probs.size() ? std::atoi(probs[i].c_str()) : 100});
        delete sc;
      }
    k.shadow = ai->getInt(ints, "chasshadowimages", 0) ? anim(ai->get("animations", "shadowidle"))
                                                       : nullptr;
    delete ai;
    if (k.idle)
      this->kinds.push_back(k);
  }
  delete cfg;
  SDL_Log("[Ambient] %zu flyers", this->kinds.size());
}

// zoo.exe 0x4fc160 (ZTAmbientAnimal setup): a side at random (rand % 4) -
// the far y edge, the near y edge, the far x edge, the near x edge - a
// place along it at random, and its way (+0x12c, of eight) straight in or
// 45 degrees either side (rand % 3); its speed cSpeed (type +0x108 to unit
// +0x154): cSpeed 64ths of a tile a second on each axis it goes along, from
// a tile's corner (measured, above). Its flight ends (zoo.exe 0x434e9c,
// its move, class slot 0x44: the place in 64ths, its tile the place / 64
// rounded toward 0) once its tile is off the map - under 0 (a whole tile
// past the near edges: -64 or less) or the map's width or height or more -
// and it's taken away (slot 0xa8). Art with 4 views goes straight in only
// (rand % 3 still drawn, then 1): 0x412ac4 gets the type's art set
// (GXLLEAnimSet, its views +0x10, 0x1c bytes each; GXLLEAnimSet::attempt
// 0x40b967 makes one a view its .ani lists) - no stock flyer's art has 4
// (measured 2026-11-17: the bluejay type's art is animals/bluejay/m/idle/
// idle.ani, 5 views). ours: -1 picks a kind it may send at random, for tests
int Ambient::spawn(const WorldMap &map, int kind) {
  if (this->kinds.empty())
    return -1;
  if (kind < 0) {
    std::vector<int> any;
    for (size_t i = 0; i < this->kinds.size(); i++)
      if (this->kinds[i].frequency > 0 && this->allows(static_cast<int>(i)))
        any.push_back(static_cast<int>(i));
    if (any.empty())
      return -1;
    kind = any[std::uniform_int_distribution<size_t>(0, any.size() - 1)(this->rng)];
  }
  if (kind < 0 || kind >= static_cast<int>(this->kinds.size()))
    return -1;
  const int W = map.getWidth(), H = map.getHeight();
  if (W <= 0 || H <= 0)
    return -1;
  const int side = std::uniform_int_distribution<int>(0, 3)(this->rng);
  int turn = std::uniform_int_distribution<int>(0, 2)(this->rng);
  if (this->kinds[kind].idle && this->kinds[kind].idle->viewCount() == 4)
    turn = 1;
  // (the ways: 0 -y, 1 +x-y, 2 +x, 3 +x+y, 4 +y, 5 -x+y, 6 -x, 7 -x-y)
  static const int ways[4][3] = {{1, 0, 7}, {3, 4, 5}, {7, 6, 5}, {1, 2, 3}};
  static const int step[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
  Flyer f;
  f.kind = kind;
  int x = 0, y = 0;
  switch (side) {
  case 0: x = std::uniform_int_distribution<int>(0, W - 1)(this->rng); y = H - 1; break;
  case 1: x = std::uniform_int_distribution<int>(0, W - 1)(this->rng); y = 0; break;
  case 2: x = W - 1; y = std::uniform_int_distribution<int>(0, H - 1)(this->rng); break;
  default: x = 0; y = std::uniform_int_distribution<int>(0, H - 1)(this->rng); break;
  }
  f.x = static_cast<float>(x);
  f.y = static_cast<float>(y);
  const int way = ways[side][turn];
  const float speed = this->kinds[kind].speed / kUnitsPerTile;
  f.vx = step[way][0] * speed;
  f.vy = step[way][1] * speed;
  // (till its tile is off the map: the far edges at the map's size, the
  // near ones a whole tile past 0)
  auto until = [](float pos, float v, float edge) {
    return v > 0 ? (edge - pos) / v : v < 0 ? (pos + 1.0f) / -v : 1e9f;
  };
  f.life = std::min(until(f.x, f.vx, static_cast<float>(W)), until(f.y, f.vy, static_cast<float>(H)));
  if (f.life <= 0 || f.life > 1e8f)
    return -1;
  this->flyers.push_back(f);
  this->sentCount++;
  return static_cast<int>(this->flyers.size()) - 1;
}

// zoo.exe 0x4c6a3e (a zoo started or loaded): the sleigh's and the
// biplane's sounds stopped and let go, the flyers they follow forgotten,
// both once-a-day marks (0x63e256, 0x63e25c) cleared. ours: the flyers in
// flight gone, the spawn clock from 0
void Ambient::clear() {
  for (Flyer &f : this->flyers)
    this->ended(f);
  this->flyers.clear();
  this->clock = 0;
  this->sentCount = 0;
  for (Kind &k : this->kinds)
    k.last = 0;
  this->santaSent = this->biplaneSent = false;
}

// A flight over: its sounds stopped - its own (zoo.exe 0x41e1fd, from
// taking a unit out of the world 0x41e1bc: the sleigh's or the biplane's
// stopped, slot 0x60, and let go) and its call
void Ambient::ended(Flyer &f) {
  if (f.ownChannel >= 0)
    Sound::get().stop(f.ownChannel);
  f.ownChannel = -1;
  if (f.channel >= 0)
    Sound::get().stop(f.channel);
  f.channel = -1;
}

// One flyer of a kind (a cNameID) sent at once, with its own sound if it has
// one (0x4f7ebe's send, below; addition: also the themed celebrations'
// flyovers, Celebrations.hpp). false: none sent
bool Ambient::sendFlyer(const WorldMap &map, int nameId, const char *own, int atten) {
  const int kind = this->kindOf(nameId);
  if (kind < 0)
    return false;
  const int at = this->spawn(map, kind);
  if (at < 0)
    return false;
  if (!own)
    return true;
  Flyer &f = this->flyers[at];
  f.own = own;
  f.ownAtten = atten;
  float dx = 0, dy = 0;
  if (this->viewOffset && this->viewOffset(f.x, f.y, dx, dy))
    f.ownChannel = Sound::get().loopAt(f.own, dx, dy, f.ownAtten);
  return true;
}

// zoo.exe 0x4f7ebe, run for each guest that comes in at the gate (0x4f6e3c,
// after the admission's paid): on 25 December (0x4f5f7a: that day and
// month) Santa (5083), once that day (0x63e256, cleared on any other day);
// on 31 October the witch (5084) - one for every guest that comes in; on 15
// September the biplane (5082), once that day (0x63e25c). Each sent at once
// (ambientSendNow 0x5a58ff). Santa's and the biplane's own sound (a DX8Sound,
// slot 0x8): sounds/sleigh.wav, attenuation 0 more (slot 0x48), and
// sounds/biplane.wav, 1000 more; played looped from where it is (slot 0x34,
// 0x441797: not at all over 6000 off), then following it (0x435282, from
// the world's update; updatePosition 0x44153b: stopped for good over 9000)
void Ambient::guestArrived(const WorldMap &map) {
  auto send = [&](int nameId, const char *own, int atten) { this->sendFlyer(map, nameId, own, atten); };
  if (this->month == 11 && this->day == 25) {
    if (!this->santaSent) {
      this->santaSent = true;
      send(5083, "sounds/sleigh.wav", 0);
    }
  } else {
    this->santaSent = false;
  }
  if (this->month == 9 && this->day == 31)
    send(5084, nullptr, 0);
  // The dated items (0x4f7ebe's other half): each on its day, off again the
  // day after - a latch (0x63cb39, 0x63cb3a, 0x63cb38) says which is out
  if (this->setTypeAvailable) {
    auto dated = [&](bool today, bool &out, int nameId) {
      if (today && !out) {
        this->setTypeAvailable(nameId, true);
        out = true;
      } else if (!today && out) {
        this->setTypeAvailable(nameId, false);
        out = false;
      }
    };
    const bool christmas = this->month == 11 && this->day == 25;
    dated(christmas, this->snowmanOn, 7079);
    dated(christmas, this->treeOn, 7077);
    if (!Ambient::halloweenDone()) // (0x63cb37 set: left on from the start)
      dated(this->month == 9 && this->day == 31, this->pumpkinOn, 7078);
    // the Yellow Brick Path, once (0x63cb36): lions and tigers and bears
    if (this->bearsTogether && !this->yellowDone) {
      this->setTypeAvailable(9111, true);
      this->yellowDone = true;
    }
  }
  if (this->month == 8 && this->day == 15) {
    if (!this->biplaneSent) {
      this->biplaneSent = true;
      send(5082, "sounds/biplane.wav", 1000);
    }
  } else {
    this->biplaneSent = false;
  }
}

// zoo.exe 0x43494e (ZTAmbientAnimal's update, class slot 0x28): it moves on
// along its way (slots 0x10, 0x44) unless held. ours: a straight line at its
// speed, its calls as before
void Ambient::update(float seconds, const WorldMap &map) {
  for (Flyer &f : this->flyers) {
    f.x += f.vx * seconds;
    f.y += f.vy * seconds;
    f.age += seconds;
    // Its sound, heard from where it is
    const Kind &k = this->kinds[f.kind];
    float dx = 0, dy = 0;
    bool seen = this->viewOffset && this->viewOffset(f.x, f.y, dx, dy);
    if (k.loop && !k.calls.empty()) {
      if (f.channel < 0 && seen)
        f.channel = Sound::get().loopAt(k.calls[0].first, dx, dy, 0);
      else if (f.channel >= 0 && !(seen && Sound::get().place(f.channel, dx, dy, 0)))
        f.channel = -1;
    } else if (k.chance > 0 && !k.calls.empty() && seen) {
      f.soundClock += seconds;
      while (f.soundClock >= 1.0f) {
        f.soundClock -= 1.0f;
        if (std::uniform_int_distribution<int>(0, k.chance - 1)(this->rng) != 0)
          continue;
        int total = 0;
        for (auto &c : k.calls)
          total += c.second;
        int roll = std::uniform_int_distribution<int>(0, std::max(1, total) - 1)(this->rng);
        for (auto &c : k.calls)
          if ((roll -= c.second) < 0) {
            Sound::get().playAt(c.first, dx, dy);
            break;
          }
      }
    }
    // Its own sound following it (zoo.exe 0x435282 -> updatePosition
    // 0x44153b: stopped for good once over 9000 off)
    if (f.ownChannel >= 0) {
      float ox = 0, oy = 0;
      if (!(this->viewOffset && this->viewOffset(f.x, f.y, ox, oy) &&
            Sound::get().place(f.ownChannel, ox, oy, f.ownAtten)))
        f.ownChannel = -1;
    }
    if (f.age >= f.life)
      this->ended(f);
  }
  this->flyers.erase(std::remove_if(this->flyers.begin(), this->flyers.end(),
                                    [](const Flyer &f) { return f.age >= f.life; }),
                     this->flyers.end());
  // zoo.exe 0x435adf (from the world's update 0x435921, its clock +0x204
  // in whole seconds): each kind the scenario lists, with a cFrequency, one
  // when that many seconds have gone since its last - no cap
  this->clock += seconds;
  const double now = std::floor(this->clock);
  for (size_t i = 0; i < this->kinds.size(); i++) {
    Kind &k = this->kinds[i];
    if (k.frequency <= 0 || !this->allows(static_cast<int>(i)) || now - k.last < k.frequency)
      continue;
    k.last = now;
    this->spawn(map, static_cast<int>(i));
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
    view.worldToScreenF(f.x, f.y, this->groundAt(map, f.x, f.y) + kFlightLevels, sx, sy, d);
    k.idle->drawAnchored(renderer, sx, sy, this->facing(view, f), nullptr, frame);
  }
}

// zoo.exe ZTAmbientAnimal::load 0x450261: its speed (+0x154) and the step
// left over (+0x158), then the unit's own part - its place and its way
// (+0x12c, 0x434e9c's steps: 0 -y, 1 +x-y, 2 +x ... 7 -x-y). The step left
// over is how far into its next 64th it is, in thousandths (0x434e9c: it
// adds ms x cSpeed and takes a step a 1000 of it). ours: its flight till it's
// off the map as spawn reckons it; a speed of 0 (written by our own saves
// before version 21) its kind's cSpeed
bool Ambient::resume(const WorldMap &map, const std::string &key, float x, float y, int way, float speed,
                     int stepLeft) {
  const int kind = this->kindIndex(key);
  const int W = map.getWidth(), H = map.getHeight();
  if (kind < 0 || W <= 0 || H <= 0)
    return false;
  static const int step[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
  if (speed <= 0)
    speed = this->kinds[kind].speed;
  Flyer f;
  f.kind = kind;
  f.x = x;
  f.y = y;
  f.vx = step[way & 7][0] * speed / kUnitsPerTile;
  f.vy = step[way & 7][1] * speed / kUnitsPerTile;
  const float left = static_cast<float>(std::clamp(stepLeft, 0, 999)) / 1000.0f / kUnitsPerTile;
  f.x += step[way & 7][0] * left;
  f.y += step[way & 7][1] * left;
  auto until = [](float pos, float v, float edge) {
    return v > 0 ? (edge - pos) / v : v < 0 ? (pos + 1.0f) / -v : 1e9f;
  };
  f.life = std::min(until(f.x, f.vx, static_cast<float>(W)), until(f.y, f.vy, static_cast<float>(H)));
  if (f.life <= 0 || f.life > 1e8f)
    return false;
  this->flyers.push_back(f);
  return true;
}

// zoo.exe ZTAmbientAnimal::save 0x47a7fd: its place in whole 64ths - the
// steps it has taken (0x434e9c), counted from where it came in - and the
// step left over (+0x158), how far into the next one it is in thousandths.
// ours: from where it is, toward the way it goes
void Ambient::originalPlace(const Flyer &f, int32_t &x64, int32_t &y64, int32_t &stepLeft) {
  auto whole = [](float pos, float v, double &into) {
    const double p = static_cast<double>(pos) * kUnitsPerTile;
    const double w = v > 0 ? std::floor(p + 1e-4) : v < 0 ? std::ceil(p - 1e-4) : std::round(p);
    into = std::max(into, std::fabs(p - w));
    return static_cast<int32_t>(w);
  };
  double into = 0;
  x64 = whole(f.x, f.vx, into);
  y64 = whole(f.y, f.vy, into);
  stepLeft = std::clamp(static_cast<int32_t>(into * 1000.0), 0, 999);
}

// ours: the flyers in flight as the engine's part of a save keeps them
std::vector<Ambient::Saved> Ambient::saveFlyers() const {
  std::vector<Saved> out;
  for (const Flyer &f : this->flyers) {
    Saved s;
    s.key = this->kinds[f.kind].key;
    s.nameId = this->kinds[f.kind].nameId;
    s.speed = this->kinds[f.kind].speed;
    s.way = static_cast<int>((std::lround(std::atan2(f.vx, -f.vy) * 4 / 3.14159265358979) + 8) % 8);
    s.x = f.x; s.y = f.y; s.vx = f.vx; s.vy = f.vy; s.age = f.age; s.life = f.life;
    s.own = f.own; s.ownAtten = f.ownAtten;
    originalPlace(f, s.x64, s.y64, s.stepLeft);
    out.push_back(s);
  }
  return out;
}

// ours: back in flight, their own sounds started again from where they are
void Ambient::loadFlyers(const std::vector<Saved> &saved) {
  for (Flyer &f : this->flyers)
    this->ended(f);
  this->flyers.clear();
  for (const Saved &s : saved) {
    const int kind = this->kindIndex(s.key);
    if (kind < 0)
      continue;
    Flyer f;
    f.kind = kind;
    f.x = s.x; f.y = s.y; f.vx = s.vx; f.vy = s.vy; f.age = s.age; f.life = s.life;
    f.own = s.own; f.ownAtten = s.ownAtten;
    float dx = 0, dy = 0;
    if (!f.own.empty() && this->viewOffset && this->viewOffset(f.x, f.y, dx, dy))
      f.ownChannel = Sound::get().loopAt(f.own, dx, dy, f.ownAtten);
    this->flyers.push_back(f);
  }
}

std::vector<std::pair<std::string, double>> Ambient::lasts() const {
  std::vector<std::pair<std::string, double>> out;
  for (const Kind &k : this->kinds)
    out.push_back({k.key, k.last});
  return out;
}

// zoo.exe 0x59148e (from applyStartSettings 0x58ed1f, which a saved zoo's
// load runs again through resumeScript 0x58fb34): each listed kind added to
// the spawn list (world +0x20c) as {last 0, type}, and the world's clock
// (+0x204, whole seconds) carries on from the file's (worldSave 0x47704c
// writes it, worldLoad 0x452e13 reads it back), so each comes on the next
// update once the clock has passed its cFrequency. measured (2026-11-17,
// the running original, everything-ff02-units loaded by Continue Saved
// Game): the list held bluejay and crow, both last 559 with the clock at
// 564, one more of each flying than the file held
void Ambient::dueNow() {
  for (Kind &k : this->kinds)
    k.last = 0;
}

void Ambient::setLasts(const std::vector<std::pair<std::string, double>> &l) {
  for (const auto &[key, last] : l) {
    const int kind = this->kindIndex(key);
    if (kind >= 0)
      this->kinds[kind].last = last;
  }
}
