#include "World.hpp"
#include "CompassDirection.hpp"
#include "MemoryManager.hpp"
#include "MemoryTracker.hpp"
#include <algorithm>
#include <vector>

// ============================================================================
// ZT1 ENGINE REMAKE - WORLD RENDERING
// ============================================================================
// Isometric Projection Rules:
//   - Tile Dimensions: 64x32 pixels (2:1 ratio)
//   - Elevation Step: 16 pixels per unit (0-31 range)
//   - Screen X: (tileX - tileY) * 32
//   - Screen Y: (tileX + tileY) * 16 - (elevation * 16)
//
// Z-Sorting (Painter's Algorithm):
//   - Sort by isometric depth (x + y)
//   - Lower depth = draw first (further back)
//   - Walls drawn after floor to appear in front
// ============================================================================

float g_ZoomLevel = 1.0f;
int g_ElevationScale = 16;
int g_BaseHeightOffset = 0;

// Debug flags
static bool g_DebugOnce = true;

struct RenderTile {
    int x, y;
    int sortKey;
    int screenX, screenY;
    int elevation;
    int terrainId;
    int rawTerrainId;
};

World::World(ResourceManager *resourceManager) {
    ZT_MEMORY_CONTEXT(MemoryOwner::World);
    this->resourceManager = resourceManager;
    this->camX = 0;
    this->camY = 0;
    this->startX = 640;  // Screen center X (1280/2)
    this->startY = 360;  // Screen center Y (720/2)

    SpriteDatabase::get().init(resourceManager);
    SpriteDatabase::get().loadDefinitions();
}

World::~World() {}

void World::loadScenario(const std::string &path) {
    loadFreeform(path);
}

void World::loadFreeform(const std::string &path) {
    SDL_Log("[World] ========================================");
    SDL_Log("[World] Loading map: %s", path.c_str());
    SDL_Log("[World] ========================================");

    // Resolve path: .scn -> .zoo, freeform/ -> maps/
    std::string actualPath = path;
    if (actualPath.find(".scn") != std::string::npos) {
        actualPath = actualPath.substr(0, actualPath.find(".scn")) + ".zoo";
        if (actualPath.find("freeform/") != std::string::npos) {
            actualPath.replace(actualPath.find("freeform/"), 9, "maps/");
        }
    }
    SDL_Log("[World] Resolved path: %s", actualPath.c_str());

    int size = 0;
    void *raw_data = this->resourceManager->getFileContent(actualPath, &size);

    if (!raw_data) {
        SDL_Log("[World] ERROR: Failed to load file!");
        return;
    }

    SDL_Log("[World] File loaded: %d bytes", size);
    AssetBuffer buffer = MemoryManager::get().wrap(raw_data, static_cast<size_t>(size));

    if (!this->zooReader.load(buffer)) {
        SDL_Log("[World] ERROR: ZooReader failed to parse!");
        return;
    }

    // Map successfully loaded - setup world state
    int mapW = zooReader.getMapWidth();
    int mapH = zooReader.getMapHeight();

    entityManager.loadFromZooReader(zooReader);

    // Calculate elevation statistics for camera positioning
    g_ElevationScale = 16;
    g_BaseHeightOffset = 0;

    long totalElev = 0;
    int count = 0;
    int minElev = 999, maxElev = 0;

    for (int y = 0; y < mapH; y++) {
        for (int x = 0; x < mapW; x++) {
            const ZooReader::ZooTile* t = zooReader.getTile(x, y);
            if (t) {
                totalElev += t->elevation;
                count++;
                if (t->elevation < minElev) minElev = t->elevation;
                if (t->elevation > maxElev) maxElev = t->elevation;
            }
        }
    }

    int avgElev = (count > 0) ? (int)(totalElev / count) : 0;
    if (avgElev > 30) avgElev = 0;

    // Center camera on map, adjusted for average elevation
    int isoCenterY = mapW * 16;
    this->camX = 0;
    this->camY = -isoCenterY + (avgElev * g_ElevationScale);

    SDL_Log("[World] === WORLD SETUP ===");
    SDL_Log("[World]   Map Size: %d x %d tiles", mapW, mapH);
    SDL_Log("[World]   Elevation Range: %d to %d (avg: %d)", minElev, maxElev, avgElev);
    SDL_Log("[World]   Camera: (%d, %d)", camX, camY);
    SDL_Log("[World]   Map Type: %u", zooReader.getMapType());

    g_DebugOnce = true;
    SDL_Log("[World] ========================================");
}

void World::update(const Uint8 *state, float deltaTime) {
    // Camera movement (scaled by zoom)
    int scrollSpeed = (int)(25 * (1.0f / g_ZoomLevel));

    if (state[SDL_SCANCODE_LEFT] || state[SDL_SCANCODE_A]) this->camX += scrollSpeed;
    if (state[SDL_SCANCODE_RIGHT] || state[SDL_SCANCODE_D]) this->camX -= scrollSpeed;
    if (state[SDL_SCANCODE_UP] || state[SDL_SCANCODE_W]) this->camY += scrollSpeed;
    if (state[SDL_SCANCODE_DOWN] || state[SDL_SCANCODE_S]) this->camY -= scrollSpeed;

    // Debug controls (throttled)
    static int keyTimer = 0;
    keyTimer++;
    if (keyTimer > 5) {
        if (state[SDL_SCANCODE_EQUALS]) g_BaseHeightOffset += 16;
        if (state[SDL_SCANCODE_MINUS]) g_BaseHeightOffset -= 16;
        if (state[SDL_SCANCODE_2]) g_ElevationScale++;
        if (state[SDL_SCANCODE_1]) g_ElevationScale--;
        keyTimer = 0;
    }

    // Zoom controls
    if (state[SDL_SCANCODE_RIGHTBRACKET]) g_ZoomLevel += 0.01f;
    if (state[SDL_SCANCODE_LEFTBRACKET]) g_ZoomLevel -= 0.01f;
    if (g_ZoomLevel < 0.2f) g_ZoomLevel = 0.2f;
    if (g_ZoomLevel > 3.0f) g_ZoomLevel = 3.0f;

    entityManager.update(deltaTime);
}

// ============================================================================
// TERRAIN ID REMAPPING
// ============================================================================
// ZT1 Terrain Byte Encoding (discovered via map file analysis):
//
//   Byte format: [FFFF][TTTT] (high nibble = flags, low nibble = terrain)
//
//   Low Nibble (bits 0-3): Actual terrain type (0-15)
//     0 = Grass          4 = Rainforest      8 = Snow         12 = Waterfall
//     1 = Savannah       5 = Brown Stone     9 = Fresh Water  13 = Conifer Floor
//     2 = Sand           6 = Gray Stone     10 = Salt Water   14 = Concrete
//     3 = Dirt           7 = Gravel         11 = Deciduous    15 = Asphalt
//
//   High Nibble (bits 4-7): Terrain flags/modifiers
//     0x00 = Natural/unmodified terrain
//     0x10 = Modified variant 1 (edge blending?)
//     0x40 = Modified variant 2 (biome transition?)
//     0x50 = Modified variant 3
//     0x60 = Special markers (object placement, spawn points?)
//     0xF0 = Player-placed/painted terrain
//
//   Examples from map analysis:
//     0xF4 (244) -> terrain 4 (Rainforest) with flag 0xF0 (player-placed)
//     0x11 (17)  -> terrain 1 (Savannah) with flag 0x10 (modified)
//     0x44 (68)  -> terrain 4 (Rainforest) with flag 0x40 (transition)
//     0xFF (255) -> terrain 15 (Asphalt) with flag 0xF0 (player-placed)
//     0x55 (85)  -> terrain 5 (Brown Stone) with flag 0x50

int World::getRemappedTerrainId(int terrainId) const {
    // Extract terrain type from low nibble (bits 0-3)
    // This is the actual terrain regardless of any flags in the high nibble
    int actualTerrain = terrainId & 0x0F;

    // Valid terrain types are 0-15
    // The original engine only has 16 terrain textures (0-15)
    // ID 16 (Trampled) shares the Dirt texture, so clamp to 15
    if (actualTerrain > 15) {
        actualTerrain = 3;  // Fallback to Dirt
    }

    return actualTerrain;
}

// ============================================================================
// SUBSTRATE MATERIAL SELECTION
// ============================================================================
// Determines what texture to use for cliff walls based on surface material

static int getSubstrateMaterial(int floorId) {
    switch (floorId) {
        case 14:  // Concrete
        case 15:  // Asphalt
            return 3;  // Dirt walls
        case 8:   // Snow
            return 6;  // Gray Stone walls
        case 2:   // Sand
            return 2;  // Sand walls
        case 3:   // Dirt
            return 3;  // Dirt walls
        case 5:   // Brown Stone
        case 6:   // Gray Stone
            return floorId;  // Stone walls match surface
        default:
            return 3;  // Default: Dirt
    }
}

// ============================================================================
// COORDINATE CONVERSION
// ============================================================================

void World::tileToScreen(int tileX, int tileY, int elevation, int &screenX, int &screenY) const {
    // Isometric projection (2:1 ratio)
    int isoX = (tileX - tileY) * 32;
    int isoY = (tileX + tileY) * 16;

    // Apply camera offset and screen center
    screenX = isoX + camX + startX;
    screenY = isoY + camY + startY;

    // Apply elevation offset (higher = further up on screen)
    screenY -= (elevation * g_ElevationScale) + g_BaseHeightOffset;
}

static int getTileElev(const ZooReader& reader, int x, int y) {
    if (x < 0 || y < 0 || x >= reader.getMapWidth() || y >= reader.getMapHeight()) {
        return -999;  // Out of bounds marker
    }
    const ZooReader::ZooTile* t = reader.getTile(x, y);
    return t ? t->elevation : -999;
}

// ============================================================================
// TERRAIN RENDERING
// ============================================================================

void World::drawTerrain(SDL_Renderer *renderer) {
    int mapWidth = this->zooReader.getMapWidth();
    int mapHeight = this->zooReader.getMapHeight();
    if (mapWidth == 0 || mapHeight == 0) return;

    // Debug output on first frame
    if (g_DebugOnce) {
        SDL_Log("[Render] === FIRST FRAME ===");
        SDL_Log("[Render] Map: %dx%d, Camera: (%d,%d), Scale: %d, Zoom: %.2f",
            mapWidth, mapHeight, camX, camY, g_ElevationScale, g_ZoomLevel);
        SDL_Log("[Render] MapType: %u, BaseTerrainId: %u",
            zooReader.getMapType(), zooReader.getBaseTerrainId());

        // Log terrain ID remapping for debugging
        SDL_Log("[Render] === TERRAIN ID REMAPPING SAMPLE ===");
        int remapCounts[17] = {0};
        int unmappedIds[32] = {0};
        int unmappedCount = 0;

        for (int y = 0; y < mapHeight; y++) {
            for (int x = 0; x < mapWidth; x++) {
                const ZooReader::ZooTile *tile = this->zooReader.getTile(x, y);
                if (!tile) continue;
                int raw = tile->terrainId;
                int mapped = getRemappedTerrainId(raw);
                if (mapped >= 0 && mapped < 17) remapCounts[mapped]++;

                // Track unique unmapped high IDs for debugging
                if (raw > 16 && unmappedCount < 32) {
                    bool found = false;
                    for (int i = 0; i < unmappedCount; i++) {
                        if (unmappedIds[i] == raw) { found = true; break; }
                    }
                    if (!found) unmappedIds[unmappedCount++] = raw;
                }
            }
        }

        SDL_Log("[Render] Remapped terrain distribution:");
        const char* terrainNames[] = {
            "Grass", "Savannah", "Sand", "Dirt", "Rainforest", "BrownRock",
            "GrayRock", "Gravel", "Snow", "FreshWater", "SaltWater",
            "Deciduous", "Waterfall", "Conifer", "Concrete", "Asphalt", "Trampled"
        };
        for (int i = 0; i < 17; i++) {
            if (remapCounts[i] > 0) {
                SDL_Log("[Render]   ID %2d (%s): %d tiles", i, terrainNames[i], remapCounts[i]);
            }
        }

        if (unmappedCount > 0) {
            SDL_Log("[Render] Raw high IDs found (>16): ");
            for (int i = 0; i < unmappedCount && i < 16; i++) {
                int raw = unmappedIds[i];
                int mapped = getRemappedTerrainId(raw);
                SDL_Log("[Render]   Raw %3d (0x%02X) -> Mapped %d (%s)",
                    raw, raw, mapped, terrainNames[mapped]);
            }
        }
    }

    // Collect all tiles for sorting
    std::vector<RenderTile> drawList;
    drawList.reserve(mapWidth * mapHeight);

    for (int y = 0; y < mapHeight; y++) {
        for (int x = 0; x < mapWidth; x++) {
            const ZooReader::ZooTile *tile = this->zooReader.getTile(x, y);
            if (!tile) continue;

            int screenX, screenY;
            tileToScreen(x, y, tile->elevation, screenX, screenY);

            RenderTile rt;
            rt.x = x;
            rt.y = y;
            rt.sortKey = x + y;  // Isometric depth
            rt.screenX = screenX;
            rt.screenY = screenY;
            rt.elevation = tile->elevation;
            rt.rawTerrainId = tile->terrainId;
            rt.terrainId = getRemappedTerrainId(tile->terrainId);
            drawList.push_back(rt);
        }
    }

    // Sort back-to-front (painter's algorithm)
    std::sort(drawList.begin(), drawList.end(), [](const RenderTile& a, const RenderTile& b) {
        if (a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
        // Tiebreaker: lower elevation draws first
        return a.elevation < b.elevation;
    });

    // Render statistics
    int floorsDrawn = 0, wallsDrawn = 0;

    for (const auto& rt : drawList) {
        Animation *floorAnim = SpriteDatabase::get().getTerrainSprite(rt.terrainId);
        int wallId = getSubstrateMaterial(rt.terrainId);
        Animation *wallAnim = SpriteDatabase::get().getTerrainSprite(wallId);
        if (!wallAnim) wallAnim = SpriteDatabase::get().getTerrainSprite(3);  // Dirt fallback

        // --- Draw Floor Surface ---
        if (floorAnim) {
            floorAnim->draw(renderer, rt.screenX, rt.screenY, CompassDirection::N);
            floorsDrawn++;
        } else {
            // Fallback: colored rectangle
            SDL_Color c = {50, 150, 50, 255};  // Default green
            if (rt.terrainId == 2) c = {210, 180, 140, 255};       // Sand
            else if (rt.terrainId == 3) c = {139, 90, 43, 255};    // Dirt
            else if (rt.terrainId == 8) c = {240, 240, 240, 255};  // Snow
            else if (rt.terrainId == 9) c = {64, 164, 223, 255};   // Water
            else if (rt.terrainId == 15) c = {80, 80, 80, 255};    // Asphalt

            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
            SDL_Rect r = {rt.screenX + 16, rt.screenY + 8, 32, 16};
            SDL_RenderFillRect(renderer, &r);
        }

        // --- Draw Cliff Walls ---
        // Walls appear where this tile is higher than its neighbor
        int southElev = getTileElev(zooReader, rt.x, rt.y + 1);
        int eastElev = getTileElev(zooReader, rt.x + 1, rt.y);

        const int MAX_WALL_GAP = 15;

        // South wall (left face as viewed by player)
        if (southElev != -999 && rt.elevation > southElev) {
            int gap = std::min(rt.elevation - southElev, MAX_WALL_GAP);
            for (int h = 0; h < gap; h++) {
                int wallY = rt.screenY + 16 + (h * g_ElevationScale);
                if (wallAnim) {
                    wallAnim->draw(renderer, rt.screenX, wallY, CompassDirection::N);
                }
                wallsDrawn++;
            }
        }

        // East wall (right face as viewed by player)
        if (eastElev != -999 && rt.elevation > eastElev) {
            int gap = std::min(rt.elevation - eastElev, MAX_WALL_GAP);
            for (int h = 0; h < gap; h++) {
                int wallY = rt.screenY + 12 + (h * g_ElevationScale);
                if (wallAnim) {
                    wallAnim->draw(renderer, rt.screenX + 16, wallY, CompassDirection::N);
                }
                wallsDrawn++;
            }
        }
    }

    if (g_DebugOnce) {
        SDL_Log("[Render] Tiles: %zu, Floors: %d, Walls: %d",
            drawList.size(), floorsDrawn, wallsDrawn);
        SDL_Log("[Render] === END FIRST FRAME ===");
        g_DebugOnce = false;
    }
}

void World::drawEntities(SDL_Renderer *renderer) {
    entityManager.draw(renderer, camX, camY, startX, startY);
}

void World::draw(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderSetScale(renderer, g_ZoomLevel, g_ZoomLevel);
    drawTerrain(renderer);
    drawEntities(renderer);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
}
