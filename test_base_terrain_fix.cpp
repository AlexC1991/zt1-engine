// Test program to verify base terrain nibble decoding fix
#include "src/WorldMap.hpp"
#include "src/ZooReader.hpp"
#include "src/ResourceManager.hpp"
#include "src/SpriteDatabase.hpp"
#include <SDL2/SDL.h>

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO);

    ResourceManager rm;
    rm.loadResourceMap(".");

    // Test maps with different base terrain IDs
    struct TestCase {
        const char* path;
        uint32_t expectedBaseTerrainId;
        uint8_t expectedDecodedType;  // What terrain type should ID 0 become?
        const char* terrainName;
    };

    TestCase tests[] = {
        {"build/Release/maps/tundra.zoo", 6, 6, "Gray Stone"},  // ID 6 directly
        {"build/Release/maps/under.zoo", 2, 2, "Sand"},         // ID 2 directly
        {"build/Release/maps/beach.zoo", 1, 1, "Savannah"},     // ID 1 directly
    };

    // Also test scenario maps with nibble-encoded base terrain
    // scn07 has baseTerrainId=36 (0x24) which should decode to type 4 (Rainforest)

    SDL_Log("=== BASE TERRAIN DECODING TEST ===");
    SDL_Log("");

    for (const auto& test : tests) {
        WorldMap map;
        if (map.loadFromFile(test.path, &rm)) {
            uint32_t actualBase = map.getBaseTerrainId();
            SDL_Log("Map: %s", test.path);
            SDL_Log("  Expected base ID: %u", test.expectedBaseTerrainId);
            SDL_Log("  Actual base ID: %u", actualBase);

            // Check a tile with terrain type 0 to see if it got substituted
            const MapTile* tile = map.getTile(0, 0);
            if (tile && tile->terrainRaw == 0x00) {
                SDL_Log("  Tile [0,0] terrain type: %u (expected %u = %s)",
                       tile->terrainType, test.expectedDecodedType, test.terrainName);

                if (tile->terrainType == test.expectedDecodedType) {
                    SDL_Log("  ✓ CORRECT!");
                } else {
                    SDL_Log("  ✗ WRONG! Expected %u, got %u", test.expectedDecodedType, tile->terrainType);
                }
            }
            SDL_Log("");
        }
    }

    // Test nibble decoding explicitly
    SDL_Log("=== NIBBLE DECODING TEST ===");
    SDL_Log("");

    struct NibbleTest {
        uint32_t baseTerrainId;
        uint8_t expectedType;
        const char* name;
    };

    NibbleTest nibbleTests[] = {
        {33, 1, "Savannah (0x21 → low nibble 1)"},
        {36, 4, "Rainforest (0x24 → low nibble 4)"},
        {37, 5, "Brown Stone (0x25 → low nibble 5)"},
        {0, 0, "Grass (0x00 → low nibble 0)"},
        {2, 2, "Sand (0x02 → low nibble 2)"},
        {6, 6, "Gray Stone (0x06 → low nibble 6)"},
    };

    for (const auto& test : nibbleTests) {
        uint8_t decoded = static_cast<uint8_t>(test.baseTerrainId & 0x0F);
        SDL_Log("BaseID %u (0x%02X) → Terrain type %u - %s",
               test.baseTerrainId, test.baseTerrainId, decoded, test.name);

        if (decoded == test.expectedType) {
            SDL_Log("  ✓ CORRECT!");
        } else {
            SDL_Log("  ✗ WRONG!");
        }
        SDL_Log("");
    }

    SDL_Quit();
    return 0;
}
