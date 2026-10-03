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

  SDL_Log("[ZooReader] Loading map file (%zu bytes)", buffer.size);

  uint32_t magic = 0;
  if (!buffer.safeRead(0, &magic) || !buffer.safeRead(4, &version) ||
      memcmp(&magic, "TZFB", 4) != 0) {
    SDL_Log("[ZooReader] ERROR: Not a TZFB map file");
    return false;
  }

  const size_t dimsOffset = version >= 82 ? 0x10 : 0x0C;
  uint32_t w = 0, h = 0;
  if (!buffer.safeRead(dimsOffset, &w) || !buffer.safeRead(dimsOffset + 4, &h) ||
      w == 0 || h == 0 || w > 512 || h > 512) {
    SDL_Log("[ZooReader] ERROR: Bad dimensions %ux%u (version %u)", w, h,
            version);
    return false;
  }
  mapWidth = static_cast<int>(w);
  mapHeight = static_cast<int>(h);

  int32_t camX = mapWidth / 2, camY = mapHeight / 2;
  buffer.safeRead(dimsOffset + 8, &camX);
  buffer.safeRead(dimsOffset + 12, &camY);
  startCameraX = std::clamp(static_cast<int>(camX), 0, mapWidth - 1);
  startCameraY = std::clamp(static_cast<int>(camY), 0, mapHeight - 1);

  const uint8_t *raw = static_cast<const uint8_t *>(buffer.data);
  tileDataOffset =
      findTileDataOffset(raw, buffer.size, dimsOffset + 8, mapWidth, mapHeight);
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

  // --- OBJECT DATA PARSING ---
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
