#include "ZooReader.hpp"
#include <SDL.h>
#include <cstring>
#include <zlib.h>

ZooReader::ZooReader() {
}

ZooReader::~ZooReader() {
}

void ZooReader::scanForDimensions(const uint8_t* ptr, size_t len) {
    const uint32_t* u32 = reinterpret_cast<const uint32_t*>(ptr);
    size_t count = (len > 128 ? 128 : len) / 4;

    for (size_t i = 0; i < count - 1; i++) {
        uint32_t v1 = u32[i];
        uint32_t v2 = u32[i+1];

        // Standard Map Sizes: 75, 100, 128, 150, 125(Ancient)
        bool isMapDim = (v1 == 50 || v1 == 75 || v1 == 100 || v1 == 125 || v1 == 150 || v1 == 128);
        if (isMapDim && v1 == v2) {
             SDL_Log("ZooReader: Found plausible dimensions %ux%u at offset 0x%zX", v1, v2, i*4);
             this->mapWidth = v1;
             this->mapHeight = v2;
             return;
        }
    }
    
    SDL_Log("ZooReader: Could not auto-detect square map dimensions.");
}

size_t ZooReader::findMapDataOffset(const uint8_t* data, size_t size, int w, int h) {
    size_t stride = w * 10;
    size_t expectedSize = stride * h;
    
    if (size < expectedSize + 100) return 0;
    
    // HEURISTIC: Scan for a block of valid tiles
    // Valid tile: TerrainID (byte 0) <= 18, Elevation (byte 1) <= 32
    
    // We scan up to the point where the map wouldn't fit
    size_t scanLimit = size - expectedSize;
    
    // Optimization: Skip likely header (first 100 bytes)
    for (size_t i = 100; i < scanLimit; i++) {
        bool match = true;
        
        // Check first 10 tiles of this potential map start
        for (int k = 0; k < 10; k++) {
            uint8_t t = data[i + k * 10];      // Terrain
            uint8_t e = data[i + k * 10 + 1];  // Elevation
            
            if (t > 20 || e > 32) {
                match = false;
                break;
            }
        }
        
        if (match) {
            // Candidate found. Let's check the start of the second row to filter false positives
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

bool ZooReader::load(const AssetBuffer& buffer) {
    if (buffer.size < 64) {
        SDL_Log("ZooReader: Data too small (%zu bytes)", buffer.size);
        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(buffer.data);
    
    // Check Signature 'TZFB' (0x42465A54 LE)
    if (bytes[0] != 'T' || bytes[1] != 'Z' || bytes[2] != 'F' || bytes[3] != 'B') {
        SDL_Log("ZooReader: Invalid signature");
        return false;
    }

    uint32_t version = *reinterpret_cast<const uint32_t*>(bytes + 4);
    
    // Read Base Terrain ID (Offset 32 / 0x20)
    // 0x02 = Grass/Standard?, 0x06 = Snow?
    if (buffer.size > 36) {
        this->baseTerrainId = *reinterpret_cast<const uint32_t*>(bytes + 32);
        this->mapType = *reinterpret_cast<const uint32_t*>(bytes + 20); // 0x14
        SDL_Log("ZooReader: Detected TZFB version %u (0x%X), BaseTerrainId: %u (0x%X), MapType: %u", version, version, this->baseTerrainId, this->baseTerrainId, this->mapType);
    } else {
        this->baseTerrainId = 0;
        this->mapType = 0;
        SDL_Log("ZooReader: Detected TZFB version %u (0x%X)", version, version);
    }

    // 1. Find Dimensions
    this->mapWidth = 0;
    this->mapHeight = 0;
    scanForDimensions(bytes, buffer.size);
    
    if (this->mapWidth == 0) {
        return false; // Failed to find dimensions
    }
    
    // 2. Find Tile Data
    size_t offset = findMapDataOffset(bytes, buffer.size, this->mapWidth, this->mapHeight);
    if (offset == 0) {
        SDL_Log("ZooReader: Could not locate map tile data!");
        // Keep existing dimensions but empty tiles? Or fail?
        // Let's create default tiles
        this->tiles.resize(mapWidth * mapHeight);
        memset(this->tiles.data(), 0, this->tiles.size() * sizeof(ZooTile));
        return true; 
    }
    
    SDL_Log("ZooReader: Found map data at offset %zu (0x%zX)", offset, offset);
    
    // 3. Read Tiles
    this->tiles.resize(mapWidth * mapHeight);
    
    // Direct copy? Or cast?
    // We can just iterate
    for (int y = 0; y < mapHeight; y++) {
        for (int x = 0; x < mapWidth; x++) {
            size_t tileIdx = offset + (y * mapWidth + x) * 10;
            ZooTile& tile = this->tiles[y * mapWidth + x];
            
            tile.terrainId = bytes[tileIdx];
            tile.elevation = bytes[tileIdx + 1];
            memcpy(tile.data, &bytes[tileIdx + 2], 8);
        }
    }
    SDL_Log("ZooReader: Loaded %zu tiles", this->tiles.size());

    return true;
}
