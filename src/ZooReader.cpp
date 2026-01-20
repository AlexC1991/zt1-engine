#include "ZooReader.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <zlib.h>

ZooReader::ZooReader() {}

ZooReader::~ZooReader() {}

void ZooReader::scanForDimensions(const uint8_t *ptr, size_t len) {
  const uint32_t *u32 = reinterpret_cast<const uint32_t *>(ptr);
  size_t count = (len > 128 ? 128 : len) / 4;

  for (size_t i = 0; i < count - 1; i++) {
    uint32_t v1 = u32[i];
    uint32_t v2 = u32[i + 1];

    // Standard Map Sizes: 75, 100, 128, 150, 125(Ancient)
    bool isMapDim = (v1 == 50 || v1 == 75 || v1 == 100 || v1 == 125 ||
                     v1 == 150 || v1 == 128);
    if (isMapDim && v1 == v2) {
      SDL_Log("ZooReader: Found plausible dimensions %ux%u at offset 0x%zX", v1,
              v2, i * 4);
      this->mapWidth = v1;
      this->mapHeight = v2;
      return;
    }
  }

  SDL_Log("ZooReader: Could not auto-detect square map dimensions.");
}

size_t ZooReader::findMapDataOffset(const uint8_t *data, size_t size, int w,
                                    int h) {
  size_t stride = w * 10;
  size_t expectedSize = stride * h;

  if (size < expectedSize + 100)
    return 0;

  // HEURISTIC: Scan for a block of valid tiles
  // Valid tile: TerrainID (byte 0) <= 18, Elevation (byte 1) <= 32

  // We scan up to the point where the map wouldn't fit
  size_t scanLimit = size - expectedSize;

  // Optimization: Skip likely header (first 100 bytes)
  for (size_t i = 100; i < scanLimit; i++) {
    bool match = true;

    // Check first 10 tiles of this potential map start
    for (int k = 0; k < 10; k++) {
      uint8_t t = data[i + k * 10];     // Terrain
      uint8_t e = data[i + k * 10 + 1]; // Elevation

      if (t > 20 || e > 32) {
        match = false;
        break;
      }
    }

    if (match) {
      // Candidate found. Let's check the start of the second row to filter
      // false positives
      size_t row2 = i + stride;
      bool row2Match = true;
      for (int k = 0; k < 10; k++) {
        uint8_t t = data[row2 + k * 10];
        uint8_t e = data[row2 + k * 10 + 1];
        if (t > 20 || e > 32) {
          row2Match = false;
          break;
        }
      }

      if (row2Match) {
        return i;
      }
    }
  }

  return 0;
}

bool ZooReader::load(const AssetBuffer &buffer) {
  if (buffer.size < 64) {
    SDL_Log("ZooReader: Data too small (%zu bytes)", buffer.size);
    return false;
  }

  // Header Reading
  uint32_t magic;
  if (!buffer.safeRead(0, &magic))
    return false;

  if (magic != 0x42465A54) { // TZFB in LE? Check bytes directly or assume LE
    const uint8_t *b = (const uint8_t *)buffer.data;
    if (b[0] != 'T' || b[1] != 'Z') { // fallback check
      SDL_Log("ZooReader: Invalid signature");
    }
  }

  // Read Version
  uint32_t version;
  if (!buffer.safeRead(4, &version))
    return false;

  // Read Dimensions (Heuristic or Fixed Offset)
  // We'll stick to scanning for now but use bounds checking
  this->scanForDimensions((const uint8_t *)buffer.data, buffer.size);

  if (mapWidth == 0 || mapHeight == 0) {
    mapWidth = 0;
    mapHeight = 0; // Reset
    // Fallback: Check offset 0xC (12)
    uint32_t w, h;
    if (buffer.safeRead(12, &w) && buffer.safeRead(16, &h)) {
      if (w == h && (w == 75 || w == 100)) {
        mapWidth = w;
        mapHeight = h;
      }
    }
  }

  if (mapWidth == 0) {
    SDL_Log("ZooReader: Failed to detect dimensions. Defaulting to 100x100 for "
            "safety, but data will be garbage.");
    mapWidth = 100;
    mapHeight = 100;
  }

  // Find Map Data
  size_t offset = findMapDataOffset((const uint8_t *)buffer.data, buffer.size,
                                    mapWidth, mapHeight);
  if (offset == 0) {
    SDL_Log("ZooReader: Could not find map data (tiles).");
    return false;
  }

  SDL_Log("ZooReader: Found map data at offset %zu (0x%zX)", offset, offset);

  // Read Tiles
  tiles.clear();
  tiles.resize(mapWidth * mapHeight);

  if (!buffer.checkBounds(offset, mapWidth * mapHeight * 10)) {
    SDL_Log("ZooReader: Map data exceeds buffer size!");
    return false;
  }

  const uint8_t *bytes = (const uint8_t *)buffer.data;
  for (int y = 0; y < mapHeight; y++) {
    for (int x = 0; x < mapWidth; x++) {
      size_t tileIdx = offset + (y * mapWidth + x) * 10;
      ZooTile &tile = tiles[y * mapWidth + x];
      tile.terrainId = bytes[tileIdx]; // safe due to checkBounds
      tile.elevation = bytes[tileIdx + 1];
      memcpy(tile.data, &bytes[tileIdx + 2], 8);
    }
  }
  SDL_Log("ZooReader: Loaded %zu tiles", tiles.size());

  // Objects
  size_t endOfTiles = offset + (mapWidth * mapHeight * 10);

  uint32_t objCount = 0;
  if (buffer.safeRead(endOfTiles, &objCount)) {
    SDL_Log("ZooReader: Object Count at 0x%zX: %u", endOfTiles, objCount);

    if (objCount > 100000) {
      SDL_Log("ZooReader: Object count unreasonably high (%u). Likely parsing "
              "error or garbage.",
              objCount);
      objCount = 0; // Skip
    }
    uint32_t possibleCount = 0;
    if (buffer.safeRead(endOfTiles, &possibleCount)) {
      SDL_Log("ZooReader: Object Count at 0x%zX: %u", endOfTiles,
              possibleCount);

      if (possibleCount > 0 && possibleCount < 100000) {
        size_t objStart = endOfTiles + 4;
        for (uint32_t i = 0; i < possibleCount; i++) {
          size_t current = objStart + (i * 16); // Stride 16 guess
          // Safety check for EACH object read
          if (!buffer.checkBounds(current, 16)) {
            SDL_Log("ZooReader: Unexpected end of buffer reading object %u", i);
            break;
          }

          uint32_t id;
          int32_t x, y;
          // safeRead with offset
          memcpy(&id, bytes + current, 4);
          memcpy(&x, bytes + current + 4, 4);
          memcpy(&y, bytes + current + 8, 4);

          ZooObject obj;
          obj.id = id;
          obj.x = x;
          obj.y = y;

          if (obj.x >= 0 && obj.x < mapWidth && obj.y >= 0 &&
              obj.y < mapHeight) {
            objects.push_back(obj);
          }
        }
      } else {
        SDL_Log("ZooReader: Object count unreasonably high (%u) or zero. "
                "Falling back to scan.",
                possibleCount);
        scanForObjects(buffer, endOfTiles);
      }
    } else {
      SDL_Log(
          "ZooReader: Buffer ends before object count. Falling back to scan.");
      scanForObjects(buffer, endOfTiles); // heuristics from map data end
    }
    SDL_Log("ZooReader: Parsed %zu valid objects.", objects.size());

    return true;
  }
  return false;
}

void ZooReader::scanForObjects(const AssetBuffer &buffer, size_t startOffset) {
  if (startOffset >= buffer.size)
    return;

  SDL_Log("ZooReader: Scanning for objects heuristically starting at %zu",
          startOffset);
  const uint8_t *ptr = (const uint8_t *)buffer.data;
  size_t len = buffer.size;

  // Scan 4-byte aligned
  for (size_t i = startOffset; i < len - 12; i += 4) {
    uint32_t id;
    int32_t x, y;

    // Use MemoryManager's safeRead to prevent buffer overflows
    // This allows the Memory Manager to log any out-of-bounds access attempts
    if (!buffer.safeRead(i, &id) || !buffer.safeRead(i + 4, &x) ||
        !buffer.safeRead(i + 8, &y)) {
      continue; // Should not happen given loop bounds, but safe is safe
    }

    // Check Validity
    bool validPos = (x >= 0 && x < mapWidth && y >= 0 && y < mapHeight);
    bool validId =
        (id >= 1000 &&
         id < 60000); // Objects usually have high IDs (e.g. 7057, 9200)

    if (validPos && validId) {
      // Heuristic: Check if this looks like part of a sequence?
      // Or just accept it.
      // Avoid adding duplicates if we are scanning overlapping
      ZooObject obj = {id, x, y};
      // SDL_Log("ZooReader: Scanned Candidate ID=%u at %d,%d", id, x, y);
      this->objects.push_back(obj);
    }
  }
}

void ZooReader::addObject(const ZooObject &obj) {
  this->objects.push_back(obj);
}

void ZooReader::validateObjects(int maxWidth, int maxHeight) {
  if (this->objects.empty())
    return;

  SDL_Log("[Memory Manager] Running Garbage Collector scan on %zu objects...",
          this->objects.size());

  // Use remove_if to filter garbage coordinates
  auto it = std::remove_if(this->objects.begin(), this->objects.end(),
                           [maxWidth, maxHeight](const ZooObject &obj) {
                             // Strict check: Must be effectively within the
                             // world + slight margin
                             if (obj.x < -10 || obj.x > maxWidth + 10)
                               return true;
                             if (obj.y < -10 || obj.y > maxHeight + 10)
                               return true;

                             // Check for suspicious huge IDs if needed
                             return false;
                           });

  if (it != this->objects.end()) {
    int removedCount = std::distance(it, this->objects.end());
    this->objects.erase(it, this->objects.end());
    SDL_Log(
        "[Memory Manager] GC: Cleaned up %d garbage objects (out of bounds). "
        "Remaining: %zu",
        removedCount, this->objects.size());
  } else {
    SDL_Log("[Memory Manager] GC: Scan complete. All %zu objects are verified "
            "safe.",
            this->objects.size());
  }
}
