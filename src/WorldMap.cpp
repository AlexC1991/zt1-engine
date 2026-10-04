#include "WorldMap.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <climits>
#include <cstdlib>

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

  // Paths
  pathTypes.clear();
  pathTile.assign(static_cast<size_t>(width) * height, -1);
  for (const ZooReader::ZooObject &obj : reader.getObjects()) {
    if (obj.className != "paths")
      continue;
    int tx = obj.tileX(), ty = obj.tileY();
    if (tx < 0 || ty < 0 || tx >= width || ty >= height)
      continue;
    auto it = std::find(pathTypes.begin(), pathTypes.end(), obj.typeName);
    int index = static_cast<int>(it - pathTypes.begin());
    if (it == pathTypes.end())
      pathTypes.push_back(obj.typeName);
    pathTile[static_cast<size_t>(ty) * width + tx] = static_cast<int16_t>(index);
  }

  // ZT_DUMP_OBJECTS=1: every object the map places, with its payload
  if (std::getenv("ZT_DUMP_OBJECTS")) {
    for (const ZooReader::ZooObject &obj : reader.getObjects()) {
      std::string hex;
      for (size_t i = 0; i < obj.payload.size() && i < 96; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02x ", obj.payload[i]);
        hex += b;
      }
      SDL_Log("[OBJ] %s/%s/%s at %d,%d z %d id %u '%s' payload %zu: %s",
              obj.className.c_str(), obj.subClass.c_str(), obj.typeName.c_str(),
              obj.x, obj.y, obj.z, obj.id, obj.name.c_str(), obj.payload.size(),
              hex.c_str());
    }
  }

  // The zoo's name, and what is placed on the map
  zooName.clear();
  placed.clear();
  for (const ZooReader::ZooObject &obj : reader.getObjects()) {
    placed.push_back({obj.className, obj.subClass, obj.typeName});
    const std::string &t = obj.typeName;
    if (zooName.empty() && obj.className == "building" && t.size() > 4 &&
        t.compare(t.size() - 4, 4, "gate") == 0 && !obj.name.empty())
      zooName = obj.name;
  }

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

void WorldMap::setPath(int x, int y, const std::string &type) {
  if (x < 0 || y < 0 || x >= width || y >= height || pathTile.empty())
    return;
  int16_t index = -1;
  if (!type.empty()) {
    auto it = std::find(pathTypes.begin(), pathTypes.end(), type);
    index = static_cast<int16_t>(it - pathTypes.begin());
    if (it == pathTypes.end())
      pathTypes.push_back(type);
  }
  pathTile[static_cast<size_t>(y) * width + x] = index;
  touch();
}

bool WorldMap::pathShapeOk(int x, int y) const {
  const MapTile *t = getTile(x, y);
  if (!t)
    return false;
  const int *c = t->cornerHeight;
  int lo = *std::min_element(c, c + 4), hi = *std::max_element(c, c + 4);
  if (hi == lo)
    return true;
  if (hi - lo != 1)
    return false;
  // Two raised, side by side (not opposite corners)
  int raised = 0;
  for (int i = 0; i < 4; i++)
    raised += c[i] > lo ? 1 : 0;
  if (raised != 2)
    return false;
  return !((c[CORNER_X0Y0] > lo && c[CORNER_X1Y1] > lo) || (c[CORNER_X1Y0] > lo && c[CORNER_X0Y1] > lo));
}

void WorldMap::raiseVertex(int vx, int vy, int by) {
  // The four tiles round the vertex, each its corner there
  const int dx[4] = {0, -1, -1, 0}, dy[4] = {0, 0, -1, -1};
  const TileCorner corner[4] = {CORNER_X0Y0, CORNER_X1Y0, CORNER_X1Y1, CORNER_X0Y1};
  for (int i = 0; i < 4; i++) {
    int x = vx + dx[i], y = vy + dy[i];
    if (x < 0 || y < 0 || x >= width || y >= height)
      continue;
    MapTile &t = tiles[y][x];
    t.cornerHeight[corner[i]] += by;
    t.height = t.cornerHeight[CORNER_X0Y0];
    minHeight = std::min(minHeight, t.cornerHeight[corner[i]]);
    maxHeight = std::max(maxHeight, t.cornerHeight[corner[i]]);
  }
  touch();
}

int WorldMap::getPathType(int x, int y) const {
  if (x < 0 || y < 0 || x >= width || y >= height || pathTile.empty())
    return -1;
  return pathTile[static_cast<size_t>(y) * width + x];
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
