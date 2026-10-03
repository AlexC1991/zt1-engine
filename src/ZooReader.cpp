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
  scanForObjects(buffer, tileDataOffset + tiles.size() * TILE_STRIDE);
  return true;
}

void ZooReader::scanForObjects(const AssetBuffer &buffer, size_t startOffset) {
    if (startOffset >= buffer.size) {
        SDL_Log("[ZooReader] No object data (offset beyond buffer)");
        return;
    }

    SDL_Log("[ZooReader] === SCANNING OBJECTS ===");
    SDL_Log("[ZooReader]   Start offset: 0x%zX (%zu)", startOffset, startOffset);
    SDL_Log("[ZooReader]   Remaining bytes: %zu", buffer.size - startOffset);

    size_t len = buffer.size;
    int objectsFound = 0;
    int objectsRejected = 0;

    for (size_t i = startOffset; i < len - 12; i += 4) {
        uint32_t id;
        int32_t x, y;

        if (!buffer.safeRead(i, &id)) continue;

        // Valid object IDs are typically in range 1000-65000
        if (id > 1000 && id < 65000) {
            if (buffer.safeRead(i + 4, &x) && buffer.safeRead(i + 8, &y)) {
                if (x >= 0 && x < mapWidth && y >= 0 && y < mapHeight) {
                    ZooObject obj = {id, x, y};
                    this->objects.push_back(obj);
                    objectsFound++;

                    // Log first few objects for debugging
                    if (objectsFound <= 5) {
                        SDL_Log("[ZooReader]   Object #%d: ID=%u at (%d,%d)", objectsFound, id, x, y);
                    }

                    i += 8; // Skip past this object (will add 4 more in loop)
                } else {
                    objectsRejected++;
                }
            }
        }
    }

    SDL_Log("[ZooReader]   Objects found: %d", objectsFound);
    SDL_Log("[ZooReader]   Objects rejected (out of bounds): %d", objectsRejected);
}

void ZooReader::addObject(const ZooObject &obj) {
    this->objects.push_back(obj);
}

void ZooReader::validateObjects(int maxWidth, int maxHeight) {
    size_t before = objects.size();
    auto it = std::remove_if(objects.begin(), objects.end(),
        [maxWidth, maxHeight](const ZooObject& o) {
            return o.x < 0 || o.y < 0 || o.x >= maxWidth || o.y >= maxHeight;
        });
    objects.erase(it, objects.end());

    if (objects.size() != before) {
        SDL_Log("[ZooReader] Validated objects: %zu -> %zu", before, objects.size());
    }
}
