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
// Header (100 bytes):
//   0x00-0x03: Magic number
//   0x04-0x07: Version
//   0x08-0x0B: Unknown
//   0x0C-0x0F: Map Width (uint32)
//   0x10-0x13: Map Height (uint32)
//   0x14-0x1F: Unknown
//   0x20-0x23: Base Terrain ID (uint32)
//   0x24-0x27: Map Type (uint32) - 47745 = Excavation
//   0x28-0x63: Reserved
//
// Tile Data (10 bytes per tile, row-major order):
//   Byte 0: Terrain ID (0-255)
//   Byte 1: Elevation (bits 0-4 = height 0-31, bits 5-7 = flags)
//   Byte 2: Tile flags (walkable, buildable, etc.)
//   Byte 3: Water depth / special
//   Bytes 4-9: Entity data / reserved
//
// Object Data (12 bytes per object, after tile data):
//   Bytes 0-3: Object Type ID (uint32)
//   Bytes 4-7: X coordinate (int32)
//   Bytes 8-11: Y coordinate (int32)
// ============================================================================

ZooReader::ZooReader() {}
ZooReader::~ZooReader() {}

void ZooReader::scanForDimensions(const uint8_t *ptr, size_t len) {
    const uint32_t *u32 = reinterpret_cast<const uint32_t *>(ptr);
    size_t count = (len > 128 ? 128 : len) / 4;

    // Standard ZT1 map dimensions
    const uint32_t validDims[] = {75, 100, 125, 150, 128};

    for (size_t i = 0; i < count - 1; i++) {
        uint32_t v1 = u32[i];
        uint32_t v2 = u32[i + 1];

        // Check if this looks like a dimension pair (same width/height = square map)
        bool isValidDim = false;
        for (auto dim : validDims) {
            if (v1 == dim) { isValidDim = true; break; }
        }

        if (isValidDim && v1 == v2) {
            this->mapWidth = v1;
            this->mapHeight = v2;
            SDL_Log("[ZooReader] Found dimensions via scan: %dx%d at offset %zu", v1, v2, i * 4);
            return;
        }
    }
}

bool ZooReader::load(const AssetBuffer &buffer) {
    SDL_Log("[ZooReader] ========================================");
    SDL_Log("[ZooReader] Loading map file (%zu bytes)", buffer.size);
    SDL_Log("[ZooReader] ========================================");

    if (buffer.size < 64) {
        SDL_Log("[ZooReader] ERROR: Buffer too small (< 64 bytes)");
        return false;
    }

    // --- HEADER PARSING ---
    uint32_t magic, version;
    if (!buffer.safeRead(0, &magic) || !buffer.safeRead(4, &version)) {
        SDL_Log("[ZooReader] ERROR: Failed to read header magic/version");
        return false;
    }
    SDL_Log("[ZooReader] Header: Magic=0x%08X Version=%u", magic, version);

    // Try to read dimensions from standard header location
    uint32_t w, h;
    if (buffer.safeRead(12, &w) && buffer.safeRead(16, &h) && w == h && w > 0 && w <= 200) {
        mapWidth = w;
        mapHeight = h;
        SDL_Log("[ZooReader] Dimensions from header: %dx%d", mapWidth, mapHeight);
    } else {
        SDL_Log("[ZooReader] Standard dimension read failed, scanning...");
        this->scanForDimensions((const uint8_t *)buffer.data, buffer.size);
    }

    // Fallback to default
    if (mapWidth == 0 || mapHeight == 0) {
        mapWidth = 100;
        mapHeight = 100;
        SDL_Log("[ZooReader] Using default dimensions: 100x100");
    }

    // Read map metadata
    if (!buffer.safeRead(0x20, &baseTerrainId)) baseTerrainId = 0;
    if (!buffer.safeRead(0x24, &mapType)) mapType = 0;

    SDL_Log("[ZooReader] === MAP METADATA ===");
    SDL_Log("[ZooReader]   Dimensions: %d x %d = %d tiles", mapWidth, mapHeight, mapWidth * mapHeight);
    SDL_Log("[ZooReader]   Base Terrain ID: %u", baseTerrainId);
    SDL_Log("[ZooReader]   Map Type: %u (Excavation=47745)", mapType);

    // --- TILE DATA PARSING ---
    tiles.clear();
    tiles.resize(mapWidth * mapHeight);

    const size_t HEADER_SIZE = 100;
    const size_t TILE_STRIDE = 10;
    const uint8_t *raw_data = (const uint8_t *)buffer.data;

    size_t requiredSize = HEADER_SIZE + (mapWidth * mapHeight * TILE_STRIDE);
    if (buffer.size < requiredSize) {
        SDL_Log("[ZooReader] ERROR: Buffer too small for tile data!");
        SDL_Log("[ZooReader]   Required: %zu bytes, Got: %zu bytes", requiredSize, buffer.size);
        return false;
    }

    // Statistics for debug
    int minElev = 999, maxElev = 0;
    int terrainCounts[256] = {0};
    int elevCounts[32] = {0};
    int flaggedTiles = 0;

    SDL_Log("[ZooReader] === PARSING TILES ===");
    for (int y = 0; y < mapHeight; y++) {
        for (int x = 0; x < mapWidth; x++) {
            size_t idx = HEADER_SIZE + (y * mapWidth + x) * TILE_STRIDE;

            // Read tile data
            uint8_t terrainId = raw_data[idx + 0];
            uint8_t elevByte  = raw_data[idx + 1];
            uint8_t tileFlags = raw_data[idx + 2];
            uint8_t waterData = raw_data[idx + 3];

            // Extract elevation (low 5 bits) and flags (high 3 bits)
            uint8_t elevation = elevByte & 0x1F;  // Bits 0-4: height 0-31
            uint8_t elevFlags = elevByte >> 5;     // Bits 5-7: elevation flags

            ZooTile &tile = tiles[y * mapWidth + x];
            tile.terrainId = terrainId;
            tile.elevation = elevation;
            memcpy(tile.data, &raw_data[idx + 2], 8);

            // Statistics
            if (elevation < minElev) minElev = elevation;
            if (elevation > maxElev) maxElev = elevation;
            terrainCounts[terrainId]++;
            if (elevation < 32) elevCounts[elevation]++;
            if (tileFlags != 0 || elevFlags != 0) flaggedTiles++;
        }
    }

    SDL_Log("[ZooReader] === TILE STATISTICS ===");
    SDL_Log("[ZooReader]   Total tiles parsed: %d", mapWidth * mapHeight);
    SDL_Log("[ZooReader]   Elevation range: %d to %d", minElev, maxElev);
    SDL_Log("[ZooReader]   Tiles with flags: %d", flaggedTiles);

    // Log elevation distribution
    SDL_Log("[ZooReader] === ELEVATION DISTRIBUTION ===");
    for (int e = minElev; e <= maxElev && e < 32; e++) {
        if (elevCounts[e] > 0) {
            int percent = (elevCounts[e] * 100) / (mapWidth * mapHeight);
            SDL_Log("[ZooReader]   Elev %2d: %5d tiles (%2d%%)", e, elevCounts[e], percent);
        }
    }

    // Log terrain distribution (only non-zero)
    SDL_Log("[ZooReader] === TERRAIN DISTRIBUTION ===");
    for (int t = 0; t < 256; t++) {
        if (terrainCounts[t] > 0) {
            SDL_Log("[ZooReader]   Terrain %3d: %5d tiles", t, terrainCounts[t]);
        }
    }

    // Sample some edge tiles for debugging
    SDL_Log("[ZooReader] === SAMPLE TILES ===");
    int sampleCoords[][2] = {{0,0}, {mapWidth-1,0}, {0,mapHeight-1}, {mapWidth-1,mapHeight-1}, {mapWidth/2,mapHeight/2}};
    const char* sampleNames[] = {"Top-Left", "Top-Right", "Bot-Left", "Bot-Right", "Center"};
    for (int i = 0; i < 5; i++) {
        int sx = sampleCoords[i][0], sy = sampleCoords[i][1];
        const ZooTile* t = getTile(sx, sy);
        if (t) {
            size_t idx = HEADER_SIZE + (sy * mapWidth + sx) * TILE_STRIDE;
            SDL_Log("[ZooReader]   %s (%d,%d): terrain=%d elev=%d rawBytes=[%02X %02X %02X %02X]",
                sampleNames[i], sx, sy, t->terrainId, t->elevation,
                raw_data[idx], raw_data[idx+1], raw_data[idx+2], raw_data[idx+3]);
        }
    }

    // --- OBJECT DATA PARSING ---
    size_t objStart = HEADER_SIZE + (mapWidth * mapHeight * TILE_STRIDE);
    scanForObjects(buffer, objStart);

    SDL_Log("[ZooReader] ========================================");
    SDL_Log("[ZooReader] Map load complete!");
    SDL_Log("[ZooReader] ========================================");

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
