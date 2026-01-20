#ifndef ZOOREADER_HPP
#define ZOOREADER_HPP

#include "MemoryManager.hpp"
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>

class ZooReader {
public:
  struct ZooTile {
    uint8_t terrainId;
    uint8_t elevation;
    uint8_t data[8]; // Padding or flags
  };

  ZooReader();
  ~ZooReader();

  bool load(const AssetBuffer &buffer);

  int getMapWidth() const { return mapWidth; }
  int getMapHeight() const { return mapHeight; }

  // Base terrain ID from header (determined by reverse engineering)
  uint32_t getBaseTerrainId() const { return baseTerrainId; }
  uint32_t getMapType() const { return mapType; }

  const ZooTile *getTile(int x, int y) const {
    if (x < 0 || x >= mapWidth || y < 0 || y >= mapHeight)
      return nullptr;
    if (tiles.empty())
      return nullptr;
    return &tiles[y * mapWidth + x];
  }

  struct ZooObject {
    uint32_t id;
    int x;
    int y;
  };

  const std::vector<ZooObject> &getObjects() const { return objects; }

  // Debug helper to manually add objects
  // Debug helper to manually add objects
  void addObject(const ZooObject &obj);

  // Validate and remove garbage objects
  void validateObjects(int maxWidth, int maxHeight);

private:
  int mapWidth = 0;
  int mapHeight = 0;
  uint32_t baseTerrainId = 0;
  uint32_t mapType = 0;

  std::vector<ZooTile> tiles;
  std::vector<ZooObject> objects;

  // Helper to find dimensions heuristically
  void scanForDimensions(const uint8_t *ptr, size_t len);
  size_t findMapDataOffset(const uint8_t *data, size_t size, int w, int h);
  void scanForObjects(const AssetBuffer &buffer, size_t startOffset);

  // Validate and remove garbage objects
  // (Moved to Public)
};

#endif // ZOOREADER_HPP
