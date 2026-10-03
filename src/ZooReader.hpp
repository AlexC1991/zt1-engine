#ifndef ZOOREADER_HPP
#define ZOOREADER_HPP

#include "MemoryManager.hpp"
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>

class ZooReader {
public:
  // One 10-byte tile record (see ZooReader.cpp for the full layout)
  struct ZooTile {
    int32_t height;     // Bytes 0-3: height of vertex (x, y) (signed)
    uint8_t cornerBits; // Byte 4: slope code, 2-bit raise per corner
    uint8_t terrain;    // Byte 5: terrain type (terrain/tiletex*.cfg "type")
    uint8_t edgeBits;   // Byte 6: cliff flags, 2 bits per edge
  };

  ZooReader();
  ~ZooReader();

  bool load(const AssetBuffer &buffer);

  int getMapWidth() const { return mapWidth; }
  int getMapHeight() const { return mapHeight; }

  uint32_t getVersion() const { return version; }

  // Tile the original game's camera starts on (header, after the size)
  int getStartCameraX() const { return startCameraX; }
  int getStartCameraY() const { return startCameraY; }

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
  uint32_t version = 0;
  int startCameraX = 0;
  int startCameraY = 0;
  size_t tileDataOffset = 0;

  std::vector<ZooTile> tiles;
  std::vector<ZooObject> objects;

  static size_t findTileDataOffset(const uint8_t *data, size_t size,
                                   size_t searchFrom, int w, int h);
  void scanForObjects(const AssetBuffer &buffer, size_t startOffset);

  // Validate and remove garbage objects
  // (Moved to Public)
};

#endif // ZOOREADER_HPP
