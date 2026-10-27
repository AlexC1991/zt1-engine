#include "ZooReader.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <zlib.h>

// ============================================================================
// ZT1 MAP FILE FORMAT (.zoo)
// ============================================================================
// Verified against all shipped maps (base game, Dinosaur Digs, Marine Mania)
// by comparing decoded terrain with each map's own preview image.
//
// Header:
//   0x00: Magic "TZFB"
//   0x04: Format version (70/71 base, 82/83 Dinosaur Digs, 104-106 Marine Mania)
//   0x08: 1033 (locale id)
//   Version <  82: width @ 0x0C, height @ 0x10
//   Version >= 82: one extra uint32 @ 0x0C, width @ 0x10, height @ 0x14
//   Next two int32: tile the game's camera starts on (x, y)
//   Then a variable-length block (scenario exhibit list, per-version fields),
//   so the tile grid does NOT start at a fixed offset. Typical starts:
//   0x28 (base freeform), 0x2C (Dino Digs), 0x34 / 0x20D (Marine Mania),
//   anywhere up to ~0x300 for scenarios with named exhibits.
//
// Tile grid (width * height records, 10 bytes each, row-major [y][x];
// confirmed unmirrored by the painted pad numbers on lunar.zoo):
//   Bytes 0-3: Height of vertex (x, y), signed int32 (pits < 0)
//   Byte 4:    Slope code, 2-bit raise per corner; corner height is
//              height + raise[corner] - raise[(x, y)]
//                bits 0-1: corner (x,   y)
//                bits 2-3: corner (x,   y+1)
//                bits 4-5: corner (x+1, y+1)
//                bits 6-7: corner (x+1, y)
//              (confirmed: shared corners match exactly across every hill
//              on crater, highland, cratlake, lvalley, dinolrg)
//   Byte 5:    Terrain type, the "type" key in terrain/tiletex*.cfg (0-17)
//   Byte 6:    Cliff flags, 2 bits per edge. Set on both sides of every edge
//              where neighbouring corner heights disagree (cliffs, banks)
//   Bytes 7-9: Always 0
//
// Followed by the object section (count + length-prefixed type strings).
// ============================================================================

namespace {

const size_t TILE_STRIDE = 10;
const int MAX_TERRAIN_TYPE = 17; // ttGunnite (Marine Mania)
const int MAX_ABS_HEIGHT = 64;

int32_t readI32(const uint8_t *p) {
  int32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

bool isPlausibleTile(const uint8_t *p) {
  int32_t h = readI32(p);
  return h >= -MAX_ABS_HEIGHT && h <= MAX_ABS_HEIGHT &&
         p[5] <= MAX_TERRAIN_TYPE && p[7] == 0 && p[8] == 0 && p[9] == 0;
}

} // namespace

// ============================================================================
// THE WHOLE FILE, IN THE ORIGINAL'S ORDER
// ============================================================================
// zoo.exe reads a .zoo section by section (loadZoo 0x453000 -> worldLoad
// 0x452e13), each class's record by its own load (class slot 0x6c). Every
// part below was read in those loaders; RustyZoo's zoo.rs
// (github.com/RustyZoo/RustyZoo, its notes on the format) was the guide to
// what each field means. Nothing is searched for: a file that doesn't read to
// its end this way falls back to the old search (findTileDataOffset).
namespace {

// A bounded reader over the file's bytes; ok goes false on the first read
// past the end (or a string or count the original would refuse)
struct Cursor {
  const uint8_t *p;
  size_t left;
  uint32_t v; // the file's version
  bool ok = true;
  bool need(size_t n) {
    if (!ok || left < n)
      ok = false;
    return ok;
  }
  void skip(size_t n) {
    if (need(n))
      p += n, left -= n;
  }
  uint8_t u8() {
    if (!need(1))
      return 0;
    uint8_t x = *p;
    skip(1);
    return x;
  }
  uint16_t u16() {
    if (!need(2))
      return 0;
    uint16_t x;
    memcpy(&x, p, 2);
    skip(2);
    return x;
  }
  uint32_t u32() {
    if (!need(4))
      return 0;
    uint32_t x;
    memcpy(&x, p, 4);
    skip(4);
    return x;
  }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  float f32() {
    uint32_t b = u32();
    float f;
    memcpy(&f, &b, 4);
    return f;
  }
  // A string: a u32 length under 0x1000 (zoo.exe's readers refuse longer),
  // then the bytes
  std::string str() {
    uint32_t n = u32();
    if (n >= 0x1000 || !need(n)) {
      ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char *>(p), n);
    skip(n);
    return s;
  }
  // A count, checked against what's left (each item at least minSize bytes)
  uint32_t count(size_t minSize) {
    uint32_t n = u32();
    if (ok && minSize && n > left / minSize)
      ok = false;
    return ok ? n : 0;
  }
  void skipList(size_t size) { skip(static_cast<size_t>(count(size)) * size); }
  void skipStrings() {
    for (uint32_t i = 0, n = count(4); i < n && ok; i++)
      str();
  }
};

// A behaviour set's saved state (zoo.exe 0x44fb77, from version 63): each
// level's leading part, a nested set flag; then every level's trailing part
void behaviourSet(Cursor &r) {
  int levels = 1;
  for (;;) {
    r.str();
    r.skip(1);
    r.skipStrings();
    r.str();
    r.str();
    r.skipStrings();
    r.skip(3);
    if (!r.ok || r.u8() == 0)
      break;
    levels++;
  }
  for (int i = 0; i < levels && r.ok; i++) {
    r.skipStrings();
    r.skip(4);
  }
}

// The part every record ends with (zoo.exe loadUnitCommon 0x44f866):
// position, direction, id, name, from 35 a flag and a time, from 58 a flag,
// from 63 the behaviour set, from 67 the colour choices
void common(Cursor &r, ZooReader::ZooObject &o) {
  o.x = r.i32();
  o.y = r.i32();
  o.z = r.i32();
  o.direction = r.u32() % 8;
  o.id = r.u32();
  o.name = r.str();
  if (r.v <= 62)
    r.str(); // (the old behaviour set's name)
  r.skip(1 + 4);
  if (r.v >= 58)
    r.skip(1);
  if (r.v >= 63)
    behaviourSet(r);
  if (r.v >= 67) {
    uint32_t n = r.count(1);
    if (r.need(n)) {
      o.colours.assign(r.p, r.p + n);
      r.skip(n);
    }
  }
}

// Scenery (food, objects, paths): its food counter, then the common part
// (zoo.exe foodSceneryLoad 0x44f625)
void scenery(Cursor &r, ZooReader::ZooObject &o) {
  o.food = r.i32();
  common(r, o);
}

// A unit's movement (zoo.exe 0x456f0a, from 84): its mode (3 in a tank)
int movement(Cursor &r) {
  int first = r.i32();
  int kind = r.v >= 93 ? r.i32() : first;
  if (kind == 3)
    r.skip(r.v >= 96 ? 1 + 32 : 1 + 12);
  else if (kind > 3)
    r.ok = false;
  return first;
}

// The base unit (zoo.exe UnitBase::load 0x4547cc): its goal, target and path
// state, then the common part last
void baseUnit(Cursor &r, ZooReader::ZooObject &o) {
  const uint32_t v = r.v;
  r.skip(2 + 10);
  r.skip(16); // (the target block, 0x454aaf)
  r.skip(v <= 85 ? 16 : 24);
  r.skip(2);
  if (v >= 53)
    r.skip(8);
  if (v >= 72)
    r.skip(8);
  r.skip(4);
  r.skip(v <= 74 ? 8 : 12);
  r.skip(4);
  r.str(); // (0x454d93)
  r.skip(1);
  r.skipList(v <= 83 ? 8 : 12);
  if (v >= 46)
    r.skip(1);
  if (v >= 62)
    r.skip(1);
  if (v >= 84) {
    r.skip(4);
    o.movementMode = movement(r);
  }
  if (v >= 85)
    r.str();
  if (v >= 98)
    r.skip(2);
  common(r, o);
}

// The zoo unit (zoo.exe ZTUnit::load 0x454488), then the base unit
void zooUnit(Cursor &r, ZooReader::ZooObject &o, bool guest) {
  const uint32_t v = r.v;
  if (v <= 57)
    r.skip(1);
  r.skip(1 + 5);
  r.skip(v <= 90 ? 8 : 12);
  r.skip(8 + 8);
  r.skip(v <= 85 ? 16 : 12);
  r.skip(6);
  if (v >= 97 && !guest)
    for (int i = 0; i < 2 && r.ok; i++)
      if (r.u8() == 1)
        r.skip(2); // (two flags, each with a value only when exactly 1)
  if (v >= 98)
    r.skip(2);
  baseUnit(r, o);
}

// An animal's own part (zoo.exe ZTAnimal::load 0x455533): its status first
void animal(Cursor &r, ZooReader::ZooObject &o) {
  const uint32_t v = r.v;
  ZooReader::ZooObject::Animal &a = o.animal;
  a.happiness = r.i32(); // +0x2a8
  a.hunger = r.i32();
  a.health = r.i32();
  a.energy = r.i32();
  a.sick = r.u8() != 0;
  a.sleeping = r.u8() != 0;
  r.skip(3);
  r.skip(5 + 20);
  a.female = r.str().rfind('M', 0) != 0; // ("M..." is a male)
  r.str();
  for (auto &check : a.checks) check = r.i32();
  r.skip(4);
  if (v <= 53)
    r.skip(4);
  r.skip(4);
  a.chaseCheck = r.i32();
  a.dung = r.i32();
  a.keeperVisited = r.u8() != 0;
  r.skip(2);
  r.skip(4);
  r.skip(6);
  a.grow = r.i32(); a.ready = r.i32(); r.skip(4);
  a.lastAte = r.u32(); a.lastAte |= uint64_t(r.u32()) << 32;
  a.lastSlept = r.u32(); a.lastSlept |= uint64_t(r.u32()) << 32;
  r.skip(2);
  if (v >= 41)
    r.skip(8);
  if (v >= 43) {
    r.skip(4); a.life = r.i32(); a.hasLife = true; r.skip(4);
    a.dying = r.u8() != 0;
  }
  if (v >= 44)
    a.mourning = r.u8() != 0;
  if (v >= 46)
    r.skip(4);
  // The escape: its values only when the flag is exactly 1
  if (v >= 47 && (a.escaped = r.u32() == 1)) {
    r.skip(8 + 8 + 12);
    if (v >= 81)
      r.skip(4);
  }
  if (v >= 52) {
    r.skip(9);
    a.crated = r.u8() != 0;
  }
  if (v >= 54)
    a.thirsty = r.u8() != 0;
  if (v == 57 || v >= 61)
    r.skip(1);
  if (v >= 76)
    a.rampaging = r.u8() != 0;
  if (v >= 77) { r.skip(4); a.rampageTicks = r.i32(); }
  if (v >= 78)
    r.skip(1);
  if (v >= 79) {
    a.egg = r.u8() != 0;
    a.hatchIn = r.i32();
  }
  if (v >= 87)
    r.skip(8);
  if (v >= 101)
    r.skip(8);
  o.hasAnimal = true;
}

// A guest's own part (zoo.exe ZTGuest::load 0x455fa2)
void guest(Cursor &r, ZooReader::ZooObject &o) {
  const uint32_t v = r.v;
  o.guest.happiness = r.i32(); // ZTGuest +0x2a8
  o.guest.hunger = r.i32();    // +0x2b0
  o.guest.thirst = r.i32();    // +0x2b8
  o.guest.energy = r.i32();    // +0x2c0 (tiredness, not remaining stamina)
  o.guest.bathroom = r.i32();  // +0x2c8
  o.hasGuest = true;
  o.guest.sick = r.u8() != 0;
  r.skip(20);
  if (v <= 66)
    r.skip(4);
  r.skip(8);
  if (v <= 64)
    r.skip(6);
  for (auto &check : o.guest.checks) check = r.i32();
  for (uint32_t k = 0, n = r.count(4); k < n && r.ok; ++k) {
    uint32_t item = r.u32();
    if (k < o.guest.items.size()) o.guest.items[k] = item;
  }
  for (uint32_t k = 0, n = r.count(4); k < n && r.ok; ++k) o.guest.seen.push_back(r.u32());
  if (v <= 93) {
    r.str();
    r.str();
  } else {
    r.skip(4);
    r.str();
  }
  r.skipList(8);
  o.guest.chaseCheck = r.i32();
  o.guest.leaving = r.u8() != 0; o.guest.afraid = r.u8() != 0;
  r.skip(4 + 2);
  o.guest.arrived = r.u32(); o.guest.arrived |= uint64_t(r.u32()) << 32;
  o.guest.favourite = r.i32();
  r.skip(2 + 8);
  if (v >= 42)
    r.skip(2);
  if (v >= 54)
    r.skip(1);
  if (v >= 56)
    r.skip(1);
  if (v >= 65)
    o.guest.guide = r.u32();
}

// Staff's own part (zoo.exe ZTKeeper 0x455444, ZTMaintenanceWorker 0x4c60bc,
// ZTTourGuide 0x59b3a1, ZTHelicopter 0x46b1dd, then ZTStaff 0x455370)
void staff(Cursor &r, const std::string &cls, ZooReader::ZooObject &o) {
  auto &s = o.staff; o.hasStaff = true;
  const uint32_t v = r.v;
  if (cls == "keeper") {
    r.skip(4 + 16 + 1);
    if (v >= 52 && v <= 74)
      r.skip(4);
  } else if (cls == "maint") {
    r.skip(4);
    if (v >= 92) {
      r.skip(10);
      for (uint32_t k = 0, n = r.count(1); k < n && r.ok; ++k) {
        bool on = r.u8() != 0; if (k < s.duties.size()) s.duties[k] = on;
      }
    }
  } else if (cls == "tour") {
    r.skip(4);
    if (v >= 54)
      s.crowd = r.i32();
    if (v >= 54 && v <= 64)
      r.skip(4);
    if (v >= 65)
      for (uint32_t k = 0, n = r.count(4); k < n && r.ok; ++k) s.followers.push_back(r.u32());
    if (v >= 66)
      r.skip(1);
  } else if (cls == "helicopter" && v >= 75) {
    r.skip(12);
  }
  s.work = r.i32(); s.chase = r.i32();
  for (uint32_t k = 0, n = r.count(8); k < n && r.ok; ++k) {
    int32_t x = r.i32(), y = r.i32(); s.exhibits.emplace_back(x, y);
  }
  if (v >= 75) s.captured = r.u32();
}

// A building's own part (zoo.exe ZTBuilding::load 0x45184a)
void building(Cursor &r) {
  const uint32_t v = r.v;
  r.skip(16);
  r.skipList(8);
  r.skip(4 + 16 + 8);
  if (v >= 45 && v <= 47)
    r.skip(2);
  else if (v >= 48 && v <= 66)
    r.skipList(1);
  if (v >= 68)
    r.skip(4);
  if (v >= 74)
    r.skip(4);
  if (v >= 75)
    r.skipList(4);
}

// One record's body, by its class; false: a class the original's loaders
// here don't cover (the body is then skipped by its size)
bool body(Cursor &r, const std::string &cls, ZooReader::ZooObject &o) {
  const uint32_t v = r.v;
  if (cls == "objects" || cls == "food" || cls == "paths") {
    scenery(r, o);
  } else if (cls == "fences" || cls == "tankwall") {
    scenery(r, o); // (zoo.exe ZTFence::load 0x44ff2d, ZTTankWall 0x45ce3d)
    if (v >= 55) {
      o.hasCondition = true;
      o.strength = r.u32();
      o.life = r.u32();
      r.skip(8);
    }
    if (cls == "tankwall" && v >= 101) {
      o.portal = r.u8() != 0;
      r.skip(1);
    }
  } else if (cls == "ambient") {
    r.skip(8); // (zoo.exe ZTAmbientAnimal::load 0x450261)
    common(r, o);
  } else if (cls == "filter") {
    scenery(r, o); // (zoo.exe ZTTankFilter::load 0x45d468)
    o.filterHealth = r.i32(); // +0x15c: health, used by setArtForState 0x45d234
    if (v >= 95)
      r.skip(12 + 16 + 4);
  } else if (cls == "rubble") {
    scenery(r, o); // (zoo.exe ZTRubble::load 0x48e392)
    r.skipList(4);
    r.skip(4);
  } else if (cls == "building") {
    scenery(r, o);
    building(r);
  } else if (cls == "animals") {
    animal(r, o);
    zooUnit(r, o, false);
  } else if (cls == "guests") {
    guest(r, o);
    zooUnit(r, o, true);
  } else if (cls == "staff" || cls == "keeper" || cls == "maint" || cls == "tour" || cls == "helicopter") {
    staff(r, cls, o);
    zooUnit(r, o, false);
  } else {
    return false;
  }
  return true;
}

} // namespace

bool ZooReader::readExact(const uint8_t *data, size_t size) {
  if (size < 8)
    return false;
  Cursor r{data + 8, size - 8, version};
  const uint32_t v = version;
  auto fail = [&](const char *where) {
    why = where;
    return false;
  };
  // loadZoo 0x453000: versions 0x26 to 0x6a; the language from 69 (0 reads
  // as 0x409); the expansions from 82
  if (v < 0x26 || v > 0x6a)
    return fail("a version the original refuses");
  language = v >= 69 ? r.u32() : 0;
  if (language == 0)
    language = 0x409;
  if (v >= 82)
    r.u32();
  // habitatMgrLoad 0x4c7e6c: the map's size, the entrance, the habitats
  const uint32_t w = r.u32(), h = r.u32();
  const int32_t ex = r.i32(), ey = r.i32();
  if (!r.ok || w == 0 || h == 0 || w > 512 || h > 512)
    return fail("habitats: the map's size");
  std::vector<Habitat> hab;
  for (uint32_t i = 0, n = r.count(76); i < n && r.ok; i++) {
    Habitat b;
    b.x = r.i32();
    b.y = r.i32();
    b.name = r.str();
    b.gateX = r.i32();
    b.gateY = r.i32();
    b.gateSide = r.i32();
    for (uint32_t &s : b.stats)
      s = r.u32();
    r.skip(8 + 8); // (two dates)
    if (v >= 83)
      r.skip(4);
    b.tank = v >= 85 && r.u8() != 0;
    b.show = v >= 90 && r.u8() != 0;
    if (b.show) { // (its show, zoo.exe 0x46d779)
      r.skip(8);
      r.skipList(4);
      if (v <= 98)
        r.skipList(8);
      r.skipList(v >= 99 ? 6 + 2 + 28 + 8 : 6);
      if (v >= 97) {
        r.skipList(20);
        r.skip(6);
      }
      if (v >= 105)
        r.skip(12);
      if (v >= 106)
        r.skip(24);
      if (v >= 97) {
        r.skip(16 + 5 + 16);
        r.skipList(14);
      }
      if (v == 101)
        r.skip(1);
    }
    if (b.tank) {
      b.tankHeight = r.i32();
      b.waterLevel = r.i32();
      if (v >= 88)
        r.skip(1);
      if (v >= 100) {
        b.salt = r.u32() != 0;
        r.skip(4);
        b.purity = r.i32();
      }
    }
    hab.push_back(b);
  }
  if (v >= 64)
    r.skip(4); // (read into +0x68, then set to 1)
  // showsLoad 0x4c6f54, from 97 here (89-96: later)
  auto shows = [&] {
    if (v >= 89)
      for (uint32_t i = 0, n = r.count(14); i < n && r.ok; i++) {
        r.skip(10);
        for (uint32_t j = 0, m = r.count(53); j < m && r.ok; j++) {
          r.skip(12);
          for (int k = 0; k < 4; k++)
            r.str();
          r.skip(8 + 1 + 16);
          if (v >= 103) {
            r.skip(8);
            r.str();
            r.str();
          }
        }
      }
    if (v >= 97)
      r.skip(4);
  };
  if (v >= 97)
    shows();
  if (!r.ok)
    return fail("habitats and shows");
  // entitiesLoad 0x44f280: the next unit id, the tile grid, the entities
  const uint32_t nextId = r.u32();
  const size_t gridAt = r.p - data;
  if (!r.need(static_cast<size_t>(w) * h * TILE_STRIDE))
    return fail("tiles");
  r.skip(static_cast<size_t>(w) * h * TILE_STRIDE);
  std::vector<ZooObject> objs;
  int good = 0;
  for (uint32_t i = 0, n = r.count(v >= 70 ? 16 : 12); i < n && r.ok; i++) {
    ZooObject o;
    o.className = r.str();
    o.subClass = r.str();
    o.typeName = r.str();
    if (!r.ok)
      break;
    if (v >= 70) {
      // (each body's size, from 70: an unknown or misread one is skipped)
      uint32_t bodySize = r.u32();
      if (!r.need(bodySize))
        break;
      Cursor b{r.p, bodySize, v};
      ZooObject d = o;
      if (bodySize > 0 && body(b, o.className, d) && b.ok && b.left == 0) {
        d.decoded = true;
        o = d;
        good++;
      }
      o.payload.assign(r.p, r.p + bodySize);
      if (!o.decoded && bodySize >= 24) {
        // (as the map path always read them: scenery's fixed places)
        memcpy(&o.x, r.p + 4, 4);
        memcpy(&o.y, r.p + 8, 4);
        memcpy(&o.z, r.p + 12, 4);
        memcpy(&o.id, r.p + 20, 4);
        uint32_t nameLen = 0;
        if (bodySize >= 28) {
          memcpy(&nameLen, r.p + 24, 4);
          if (nameLen < 256 && 28 + nameLen <= bodySize)
            o.name.assign(reinterpret_cast<const char *>(r.p + 28), nameLen);
        }
      }
      r.skip(bodySize);
    } else {
      // (before 70 no sizes: the body has to be read to find the next)
      const uint8_t *start = r.p;
      if (!body(r, o.className, o) || !r.ok)
        return fail("an entity of an unknown class, in a file without body sizes");
      o.decoded = true;
      o.payload.assign(start, r.p);
      good++;
    }
    objs.push_back(std::move(o));
  }
  if (!r.ok)
    return fail("entities");
  // (read to here, the map's part is exact - a map needs no more; a later
  // section that won't read only means no saved game state)
  mapWidth = static_cast<int>(w);
  mapHeight = static_cast<int>(h);
  startCameraX = ex;
  startCameraY = ey;
  tileDataOffset = gridAt;
  habitats = hab;
  nextUnitId = nextId;
  objects = objs;
  decoded = good;
  exact = true;
  auto failLate = [&](const char *where) {
    why = std::string(where) + " (the map's part read exactly; no saved game state)";
    return true;
  };
  // gameMgrLoad 0x5948c6 and the managers after it: the view, the scenario,
  // the name counters, the zoo's status and books, the date, the cash
  // The view: its zoom, rotation, scroll and the terraform brush's size
  for (int32_t &vv : view)
    vv = r.i32();
  {
    uint32_t sz = r.u32();
    if (sz > 0 && v >= 51) {
      uint32_t name = r.u32();
      r.skip(name);
      uint32_t goals = r.u32();
      r.skip(goals);
      if (r.ok && (goals % 4 != 0 || sz != 8 + name + goals))
        return failLate("the scenario's block");
    } else {
      r.skip(sz);
    }
  }
  if (!r.ok)
    return failLate("the view and the scenario's block");
  for (uint32_t i = 0, n = r.count(8); i < n && r.ok; i++) {
    r.str();
    r.u32();
  }
  if (!r.ok)
    return failLate("the name counters");
  r.skip(4 + 12 + 1);
  const uint32_t zooRating = r.u32();
  r.skip(12 + 4);
  books.month = r.u32(); books.year = r.u32();
  const uint32_t rows = r.u32(), cols = r.u32();
  if (!r.ok || (static_cast<uint64_t>(12 + cols + 1) * 4 * rows) > r.left)
    return failLate("the zoo's books");
  books.monthly.resize(rows);
  for (auto &row : books.monthly) for (auto &value : row) value = r.f32();
  books.yearly.assign(rows, std::vector<float>(cols));
  for (auto &row : books.yearly) for (auto &value : row) value = r.f32();
  books.total.resize(rows);
  for (auto &value : books.total) value = r.f32();
  const float price = v >= 39 ? r.f32() : 49.0f;
  if (v >= 71)
    r.skip(8);
  uint16_t when[8];
  for (uint16_t &d : when)
    d = r.u16(); // (gameMgr +0x1194, a SYSTEMTIME)
  const float money = r.f32(); // (gameMgr +0xc)
  if (v >= 73)
    r.skip(4);
  if (!r.ok)
    return failLate("the date and the cash");
  r.skipList(20); // (the thoughts, thoughtMgr)
  r.skip(8);      // (the AI clock)
  if (!r.ok)
    return failLate("the thoughts");
  if (v >= 97) {  // (the event queue, eventQueueLoad 0x594d17)
    r.skipList(20);
    r.skipList(20);
    r.skip(1);
  }
  if (v >= 40) { // (the research, researchMgr)
    r.skip(4);
    for (int k = 0; r.ok && k < 100000; k++) {
      int kind = r.i32();
      if (kind < 0)
        break;
      ResearchRecord record; record.kind = kind; record.id = r.i32();
      if (kind == 0 || kind == 2)
        record.value = r.u32();
      else if (kind == 1)
        record.value = r.u8();
      else
        return failLate("a research record of an unknown kind");
      research.push_back(record);
    }
  }
  if (!r.ok)
    return failLate("the events and the research");
  if (v >= 59)
    for (uint32_t k = 0, n = r.count(4); k < n && r.ok; ++k) awards.push_back(r.i32());
  if (v >= 60)
    marketing = r.i32(); // funding level, ZTMarketingMgr::load 0x594129
  if (v >= 89 && v <= 96)
    shows();
  if (v >= 104)
    r.skipStrings(); // (the cheats, cheatsLoad 0x594197)
  if (v >= 58)
    r.skip(v <= 66 ? 12 : 8); // (the world clock)
  if (!r.ok)
    return failLate("the awards, marketing, cheats or the world clock");
  // Read to the end, every part where the original reads it: the saved
  // game's state too
  game = true;
  cash = money;
  admission = price;
  rating = zooRating;
  memcpy(date, when, sizeof date);
  return true;
}

ZooReader::ZooReader() {}
ZooReader::~ZooReader() {}

// The tile grid is the first run of width*height consecutive plausible
// 10-byte records. Misaligned starts 1-9 bytes early can also pass on
// perfectly flat maps (all-zero records), so take the last valid start
// within that first 10-byte window.
size_t ZooReader::findTileDataOffset(const uint8_t *data, size_t size,
                                     size_t searchFrom, int w, int h) {
  const size_t gridBytes = static_cast<size_t>(w) * h * TILE_STRIDE;
  if (size < searchFrom + gridBytes)
    return 0;
  const size_t lastStart = std::min(size - gridBytes, searchFrom + 0x10000);

  // run[i] = number of consecutive plausible records starting at byte i
  std::vector<uint32_t> run(lastStart + gridBytes + TILE_STRIDE + 1, 0);
  for (size_t i = lastStart + gridBytes; i-- > searchFrom;) {
    if (i + TILE_STRIDE <= size && isPlausibleTile(data + i))
      run[i] = run[i + TILE_STRIDE] + 1;
  }

  const uint32_t needed = static_cast<uint32_t>(w) * h;
  for (size_t s = searchFrom; s <= lastStart; s++) {
    if (run[s] < needed)
      continue;
    size_t best = s;
    for (size_t t = s + 1; t < s + TILE_STRIDE && t <= lastStart; t++) {
      if (run[t] >= needed)
        best = t;
    }
    return best;
  }
  return 0;
}

bool ZooReader::load(const AssetBuffer &buffer) {
  mapWidth = 0;
  mapHeight = 0;
  version = 0;
  startCameraX = startCameraY = 0;
  tileDataOffset = 0;
  tiles.clear();
  objects.clear();
  // A failed second load must not expose the previous zoo's saved state.
  exact = game = false;
  habitats.clear();
  decoded = 0;
  why.clear();
  language = nextUnitId = rating = 0;
  cash = 0;
  research.clear(); awards.clear(); marketing = 0; books = {};
  admission = 49.0f;
  std::fill(std::begin(date), std::end(date), 0);
  std::fill(std::begin(view), std::end(view), 0);

  SDL_Log("[ZooReader] Loading map file (%zu bytes)", buffer.size);

  uint32_t magic = 0;
  if (!buffer.safeRead(0, &magic) || !buffer.safeRead(4, &version) ||
      memcmp(&magic, "TZFB", 4) != 0) {
    SDL_Log("[ZooReader] ERROR: Not a TZFB map file");
    return false;
  }

  const uint8_t *raw = static_cast<const uint8_t *>(buffer.data);
  // First the whole file in the original's order; the search below only
  // when that doesn't read to the end
  exact = game = false;
  habitats.clear();
  decoded = 0;
  why.clear();
  if (readExact(raw, buffer.size)) {
    startCameraX = std::clamp(startCameraX, 0, mapWidth - 1);
    startCameraY = std::clamp(startCameraY, 0, mapHeight - 1);
    SDL_Log("[ZooReader]   Read in the original's order: %zu habitats, %zu entities (%d decoded)",
            habitats.size(), objects.size(), decoded);
  } else {
    SDL_Log("[ZooReader]   Not read in the original's order (%s): the old search instead", why.c_str());
    objects.clear();
  }
  const size_t dimsOffset = version >= 82 ? 0x10 : 0x0C;
  uint32_t w = 0, h = 0;
  if (!exact && (!buffer.safeRead(dimsOffset, &w) || !buffer.safeRead(dimsOffset + 4, &h) ||
                 w == 0 || h == 0 || w > 512 || h > 512)) {
    SDL_Log("[ZooReader] ERROR: Bad dimensions %ux%u (version %u)", w, h,
            version);
    return false;
  }
  if (!exact) {
    mapWidth = static_cast<int>(w);
    mapHeight = static_cast<int>(h);
    // (the entrance tile: the original's habitat section)
    int32_t camX = mapWidth / 2, camY = mapHeight / 2;
    buffer.safeRead(dimsOffset + 8, &camX);
    buffer.safeRead(dimsOffset + 12, &camY);
    startCameraX = std::clamp(static_cast<int>(camX), 0, mapWidth - 1);
    startCameraY = std::clamp(static_cast<int>(camY), 0, mapHeight - 1);
    tileDataOffset = findTileDataOffset(raw, buffer.size, dimsOffset + 8, mapWidth, mapHeight);
  }
  if (tileDataOffset == 0) {
    SDL_Log("[ZooReader] ERROR: Tile grid not found (version %u, %dx%d)",
            version, mapWidth, mapHeight);
    mapWidth = mapHeight = 0;
    return false;
  }

  SDL_Log("[ZooReader]   Version %u, %dx%d tiles, grid at 0x%zX", version,
          mapWidth, mapHeight, tileDataOffset);

  tiles.resize(static_cast<size_t>(mapWidth) * mapHeight);
  int terrainCounts[MAX_TERRAIN_TYPE + 1] = {0};
  int minHeight = MAX_ABS_HEIGHT, maxHeight = -MAX_ABS_HEIGHT;

  for (size_t i = 0; i < tiles.size(); i++) {
    const uint8_t *p = raw + tileDataOffset + i * TILE_STRIDE;
    ZooTile &tile = tiles[i];
    tile.height = readI32(p);
    tile.cornerBits = p[4];
    tile.terrain = p[5];
    tile.edgeBits = p[6];

    terrainCounts[tile.terrain]++;
    minHeight = std::min(minHeight, static_cast<int>(tile.height));
    maxHeight = std::max(maxHeight, static_cast<int>(tile.height));
  }

  SDL_Log("[ZooReader]   Height range: %d to %d", minHeight, maxHeight);
  for (int t = 0; t <= MAX_TERRAIN_TYPE; t++) {
    if (terrainCounts[t] > 0)
      SDL_Log("[ZooReader]   Terrain %2d: %5d tiles", t, terrainCounts[t]);
  }

  // --- OBJECT DATA PARSING --- (read already, exactly, when the file was)
  if (!exact)
    readObjects(raw, buffer.size, tileDataOffset + tiles.size() * TILE_STRIDE);
  return true;
}

void ZooReader::readObjects(const uint8_t *data, size_t size, size_t offset) {
  size_t pos = offset;
  auto u32 = [&](uint32_t &out) {
    if (pos + 4 > size)
      return false;
    memcpy(&out, data + pos, 4);
    pos += 4;
    return true;
  };
  auto str = [&](std::string &out) {
    uint32_t len = 0;
    if (!u32(len) || len > 1024 || pos + len > size)
      return false;
    out.assign((const char *)data + pos, len);
    pos += len;
    return true;
  };

  uint32_t count = 0;
  if (!u32(count) || count > 1000000) {
    SDL_Log("[ZooReader] No object list");
    return;
  }
  objects.reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    ZooObject obj;
    uint32_t payloadSize = 0;
    if (!str(obj.className) || !str(obj.subClass) || !str(obj.typeName) ||
        !u32(payloadSize) || pos + payloadSize > size) {
      SDL_Log("[ZooReader] Object list ended early at %u of %u", i, count);
      break;
    }
    obj.payload.assign(data + pos, data + pos + payloadSize);
    if (payloadSize >= 24) {
      memcpy(&obj.x, data + pos + 4, 4);
      memcpy(&obj.y, data + pos + 8, 4);
      memcpy(&obj.z, data + pos + 12, 4);
      memcpy(&obj.id, data + pos + 20, 4);
      uint32_t nameLen = 0;
      if (payloadSize >= 28) {
        memcpy(&nameLen, data + pos + 24, 4);
        if (nameLen < 256 && 28 + nameLen <= payloadSize)
          obj.name.assign((const char *)data + pos + 28, nameLen);
      }
    }
    pos += payloadSize;
    objects.push_back(std::move(obj));
  }
  SDL_Log("[ZooReader]   %zu objects", objects.size());
}
