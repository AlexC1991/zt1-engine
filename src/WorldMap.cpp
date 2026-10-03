#include "WorldMap.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <climits>

// ============================================================================
// WORLD MAP IMPLEMENTATION
// ============================================================================
// Pure data storage following original ZT1 architecture
// Map tiles stored in memory exactly as they appear in .zoo files
// Rendering is handled separately by WorldRenderer
// ============================================================================

WorldMap::WorldMap() : width(0), height(0), minHeight(0), maxHeight(0) {}

WorldMap::~WorldMap() { tiles.clear(); }

bool WorldMap::loadFromZooReader(const ZooReader &reader) {
  width = reader.getMapWidth();
  height = reader.getMapHeight();
  tiles.clear();

  if (width == 0 || height == 0) {
    SDL_Log("[WorldMap] ERROR: Invalid map dimensions %dx%d", width, height);
    return false;
  }

  tiles.assign(height, std::vector<MapTile>(width));
  minHeight = INT32_MAX;
  maxHeight = INT32_MIN;
  int terrainCounts[18] = {0};

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const ZooReader::ZooTile *src = reader.getTile(x, y);
      if (!src)
        continue;

      MapTile &tile = tiles[y][x];
      tile.height = src->height;
      tile.terrainType = src->terrain;
      tile.edgeBits = src->edgeBits;
      tile.substrate = getSubstrateMaterial(tile.terrainType);

      // Byte 4 holds a 2-bit raise per corner: bits 0-1 (x,y), 2-3 (x,y+1),
      // 4-5 (x+1,y+1), 6-7 (x+1,y). The stored height is vertex (x,y), so
      // every corner is height + raise[c] - raise[x0y0]. This makes shared
      // corners agree exactly across all hills; where neighbours still
      // disagree, the map has a cliff (byte 6 flags those edges).
      static const TileCorner bitOrder[4] = {CORNER_X0Y0, CORNER_X0Y1,
                                             CORNER_X1Y1, CORNER_X1Y0};
      int raise[4];
      for (int k = 0; k < 4; k++)
        raise[bitOrder[k]] = (src->cornerBits >> (2 * k)) & 0x3;
      for (int c = 0; c < 4; c++) {
        int h = tile.height + raise[c] - raise[CORNER_X0Y0];
        tile.cornerHeight[c] = h;
        minHeight = std::min(minHeight, h);
        maxHeight = std::max(maxHeight, h);
      }

      if (tile.terrainType < 18)
        terrainCounts[tile.terrainType]++;
    }
  }

  const char *terrainNames[18] = {
      "Grass",     "Savannah",   "Sand",      "Dirt",      "ForestFloor",
      "BrownRock", "GrayRock",   "Gravel",    "Snow",      "FreshWater",
      "SaltWater", "Deciduous",  "Waterfall", "Conifer",   "Concrete",
      "Asphalt",   "Trampled",   "Gunnite"};

  generation++;
  SDL_Log("[WorldMap] Loaded %dx%d, heights %d..%d", width, height, minHeight,
          maxHeight);
  for (int i = 0; i < 18; i++) {
    if (terrainCounts[i] > 0)
      SDL_Log("[WorldMap]   Type %2d (%s): %d tiles", i, terrainNames[i],
              terrainCounts[i]);
  }
  return true;
}

const MapTile *WorldMap::getTile(int x, int y) const {
  if (x < 0 || x >= width || y < 0 || y >= height) {
    return nullptr;
  }
  return &tiles[y][x];
}

MapTile *WorldMap::getTileMutable(int x, int y) {
  if (x < 0 || x >= width || y < 0 || y >= height) {
    return nullptr;
  }
  return &tiles[y][x];
}

// ============================================================================
// SUBSTRATE MATERIAL SELECTION
// ============================================================================
// Determines what material to use for cliff walls based on surface terrain
// Follows original ZT1 logic

uint8_t WorldMap::getSubstrateMaterial(uint8_t floorTerrainType) {
  // The original draws every cliff face with the concrete texture,
  // whatever the ground above or below is (measured on under.zoo and
  // sm_cclif.zoo: wall colours are a constant multiple of ccrete.tga)
  (void)floorTerrainType;
  return 14;
}
