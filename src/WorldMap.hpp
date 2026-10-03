#ifndef WORLD_MAP_HPP
#define WORLD_MAP_HPP

#include <vector>
#include <cstdint>
#include "ZooReader.hpp"

// ============================================================================
// WORLD MAP - Original ZT1 Engine Architecture
// ============================================================================
// PURPOSE: Store map data (tiles, heights, terrain types)
// SEPARATION: Pure data storage, NO rendering logic
// RENDERING: Handled by WorldRenderer which reads this data
// ============================================================================

// Tile corners by world position. Tile (x, y) spans grid vertices x..x+1,
// y..y+1. Which one is on top of the screen depends on the view rotation.
enum TileCorner {
    CORNER_X0Y0 = 0, // vertex (x,   y)   - the stored tile height
    CORNER_X1Y0,     // vertex (x+1, y)
    CORNER_X1Y1,     // vertex (x+1, y+1)
    CORNER_X0Y1      // vertex (x,   y+1)
};

struct MapTile {
    int32_t height;          // Height of the top corner (signed; pits < 0)
    int32_t cornerHeight[4]; // Absolute height per TileCorner
    uint8_t terrainType;     // terrain/tiletex*.cfg "type" (0-17)
    uint8_t edgeBits;        // Cliff edge flags (byte 6)
    uint8_t substrate;       // Wall material for cliff faces

    // Future: Object placement data (buildings, scenery)
    uint16_t objectId;

    MapTile() : height(0), cornerHeight{0, 0, 0, 0}, terrainType(0),
                edgeBits(0), substrate(0), objectId(0) {}
};

class WorldMap {
public:
    WorldMap();
    ~WorldMap();

    // Load map from .zoo file via ZooReader
    bool loadFromZooReader(const ZooReader& reader);

    // Get tile at position (returns nullptr if out of bounds)
    const MapTile* getTile(int x, int y) const;
    MapTile* getTileMutable(int x, int y);

    // Map dimensions
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // Lowest/highest corner height on the map
    int getMinHeight() const { return minHeight; }
    int getMaxHeight() const { return maxHeight; }

    // Incremented on every successful load, so renderers can tell when
    // cached per-map data is stale
    uint32_t getGeneration() const { return generation; }

    // Get substrate material for wall rendering
    static uint8_t getSubstrateMaterial(uint8_t floorTerrainType);

private:
    int width;
    int height;
    int minHeight;
    int maxHeight;
    uint32_t generation = 0;

    // Tile grid (row-major: [y][x])
    std::vector<std::vector<MapTile>> tiles;
};

#endif // WORLD_MAP_HPP
