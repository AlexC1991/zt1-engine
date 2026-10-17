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

    // Read exactly by the original's loaders (decoded: the whole body read
    // to its stored size - see ZooReader.cpp); else only the fields above,
    // from their fixed places
    bool decoded = false;
    uint32_t direction = 0;          // 0-7 (loadUnitCommon +0x12c)
    int32_t food = 0;                // scenery's food counter (food, poo)
    std::vector<uint8_t> colours;    // colour choices, from version 67
    bool hasCondition = false;       // fences and tank walls, from 55
    uint32_t strength = 0, life = 0; // (life under 1: broken)
    bool portal = false;             // a tank wall that's a show portal
    int movementMode = 0;            // units: 3 inside a tank
    struct Animal {
      int32_t happiness = 0, hunger = 0, health = 0, energy = 0;
      bool female = false;
      int32_t dung = 0;
      bool keeperVisited = false, dying = false, mourning = false, crated = false, thirsty = false,
           rampaging = false, egg = false, escaped = false;
    };
    bool hasAnimal = false;
    Animal animal;

    int tileX() const { return x >= 0 ? x / 64 : (x - 63) / 64; }
    int tileY() const { return y >= 0 ? y / 64 : (y - 63) / 64; }
  };

  const std::vector<ZooObject> &getObjects() const { return objects; }

  // ---- The whole file, read in the original's order (exact: every section
  // read to its end - else only the tiles and objects, found by the search)
  bool isExact() const { return exact; }
  // A habitat's record (zoo.exe habitatMgrLoad 0x4c7e6c)
  struct Habitat {
    int x = 0, y = 0;             // its first tile
    std::string name;
    int gateX = -1, gateY = -1;   // its gate's tile (-1: none)
    int gateSide = -1;            // map direction 0, 2, 4, 6 (-1: none)
    uint32_t stats[9] = {};
    bool tank = false, show = false;
    int tankHeight = 0, waterLevel = 0; // steps above the tank's floor
    bool salt = true;
    int purity = 100;
  };
  const std::vector<Habitat> &getHabitats() const { return habitats; }
  uint32_t getLanguage() const { return language; }
  int getEntranceX() const { return startCameraX; }
  int getEntranceY() const { return startCameraY; }
  uint32_t getNextUnitId() const { return nextUnitId; }
  // The game manager's (zoo.exe gameMgrLoad 0x5948c6): saved games have
  // these; a map's are its starting ones
  bool hasGame() const { return game; }
  float getCash() const { return cash; }
  float getAdmission() const { return admission; }
  uint32_t getRating() const { return rating; }
  const uint16_t *getDate() const { return date; } // SYSTEMTIME: year, month, weekday, day...
  // The view the zoo was saved with: zoom, rotation, scroll x, scroll y,
  // the terraform brush's size
  const int32_t *getView() const { return view; }
  // How many entity records decoded to exactly their size, of how many
  int decodedCount() const { return decoded; }
  // Where the exact read stopped, when it did ("" when it didn't)
  const std::string &problem() const { return why; }

private:
  bool exact = false, game = false;
  uint32_t language = 0, nextUnitId = 0, rating = 0;
  std::vector<Habitat> habitats;
  float cash = 0, admission = 49.0f;
  uint16_t date[8] = {};
  int32_t view[5] = {};
  int decoded = 0;
  std::string why;
  bool readExact(const uint8_t *data, size_t size);
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
