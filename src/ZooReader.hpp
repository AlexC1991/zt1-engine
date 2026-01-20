#ifndef ZOOREADER_HPP
#define ZOOREADER_HPP

#include <vector>
#include <string>
#include <cstdint>
#include "MemoryManager.hpp"
#include <SDL.h>

class ZooReader {
public:
    struct ZooTile {
        uint8_t terrainId;
        uint8_t elevation;
        uint8_t data[8]; // Padding or flags
    };

    ZooReader();
    ~ZooReader();

    bool load(const AssetBuffer& buffer);
    
    int getMapWidth() const { return mapWidth; }
    int getMapHeight() const { return mapHeight; }
    
    // Base terrain ID from header (determined by reverse engineering)
    uint32_t getBaseTerrainId() const { return baseTerrainId; }
    uint32_t getMapType() const { return mapType; }

    const ZooTile* getTile(int x, int y) const {
        if (x < 0 || x >= mapWidth || y < 0 || y >= mapHeight) return nullptr;
        if (tiles.empty()) return nullptr;
        return &tiles[y * mapWidth + x];
    }

private:
    int mapWidth = 0;
    int mapHeight = 0;
    uint32_t baseTerrainId = 0;
    uint32_t mapType = 0;
    
    std::vector<ZooTile> tiles;
    
    // Helper to find dimensions heuristically
    void scanForDimensions(const uint8_t* ptr, size_t len);
    size_t findMapDataOffset(const uint8_t* data, size_t size, int w, int h);
};

#endif // ZOOREADER_HPP
