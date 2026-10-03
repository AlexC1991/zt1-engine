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

  // One placed object (path, fence, foliage, building, guest, ...). Every
  // object in the file uses the same record:
  //   string class, string sub-class, string type   (u32 length + chars)
  //   u32 payload size, then the payload:
  //     u32 (0), u32 x, u32 y, i32 z, u32 (varies), u32 id,
  //     string display name, then type-specific data
  // x and y are in 64ths of a tile (tile centre = n*64 + 32); z is height
  // in 16ths of a height unit. Verified on all 90 shipped maps.
  struct ZooObject {
    std::string className; // e.g. "paths", "fences", "objects"
    std::string subClass;  // e.g. "paths", "zoowall", "foliage"
    std::string typeName;  // e.g. "path" -> paths/path.ai
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    uint32_t id = 0;
    std::string name;      // e.g. "Concrete Path"
    std::vector<uint8_t> payload;

    int tileX() const { return x >= 0 ? x / 64 : (x - 63) / 64; }
    int tileY() const { return y >= 0 ? y / 64 : (y - 63) / 64; }
  };

  const std::vector<ZooObject> &getObjects() const { return objects; }

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
  void readObjects(const uint8_t *data, size_t size, size_t offset);
};

#endif // ZOOREADER_HPP
