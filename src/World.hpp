#ifndef WORLD_HPP
#define WORLD_HPP

#include "ResourceManager.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include "SpriteDatabase.hpp"
#include "SpriteManager.hpp"
#include "EntityManager.hpp"
#include "ZooReader.hpp"
#include <SDL2/SDL.h>
#include <string>

// ============================================================================
// WORLD - Coordinator Class (Original ZT1 Engine Architecture)
// ============================================================================
// PURPOSE: Coordinate all world-related subsystems
// ARCHITECTURE:
//   - WorldMap: Pure data storage (tiles, terrain, elevation)
//   - WorldRenderer: Pure rendering (reads WorldMap, draws to screen)
//   - SpriteDatabase: Terrain sprite cache
//   - SpriteManager: Entity/object sprite cache
//   - EntityManager: Entity tracking and simulation
//   - ZooReader: Map file parser
// ============================================================================

class World {
public:
    World(ResourceManager* resourceManager);
    ~World();

    // Load a map from .zoo/.scn file
    // Return false (and keep the previous map) if the file fails to load
    bool loadScenario(const std::string& path);
    bool loadFreeform(const std::string& path);

    // Update world state (simulation)
    void update(const Uint8* state, float deltaTime);

    // Render world (isometric terrain + entities)
    void draw(SDL_Renderer* renderer);

    // Where on the window the map is shown, and how much bigger than 1:1.
    // Like the original, the in-game screen is its 800x600 screen scaled to
    // fit and centred (rect = that area, scale = the UI scale); with the
    // widescreen option it is the whole window at 1:1.
    void setView(const SDL_Rect& rect, float scale) {
        viewRect = rect;
        viewScale = scale > 0.0f ? scale : 1.0f;
    }

    // Access subsystems
    WorldMap& getMap() { return worldMap; }
    WorldRenderer& getRenderer() { return worldRenderer; }
    EntityManager& getEntityManager() { return entityManager; }
    Camera& getCamera() { return worldRenderer.getCamera(); }

    // In-game HUD hooks
    void drawMiniMap(SDL_Renderer* renderer, const SDL_Rect& box);
    void miniMapClick(float fx, float fy);   // fractions across/down the box
    void rotateView(int steps);              // keeps the same spot centred
    void zoomStep(int direction);            // +1 in, -1 out
    void setPaused(bool p) { paused = p; }
    bool isPaused() const { return paused; }

    // Camera control
    void setCameraPosition(int x, int y);
    void getCameraPosition(int& x, int& y) const;

private:
    ResourceManager* resourceManager;

    // Core subsystems (separated concerns)
    WorldMap worldMap;              // Pure data
    WorldRenderer worldRenderer;    // Pure rendering
    EntityManager entityManager;    // Entity tracking
    ZooReader zooReader;           // Map file parser

    // Input handling
    void handleCameraInput(const Uint8* state, float deltaTime);
    void handleDebugInput(const Uint8* state);

    bool paused = false;
    int outputW = 1280, outputH = 720; // the map's area on the window
    SDL_Rect viewRect = {0, 0, 0, 0};  // empty = the whole window
    float viewScale = 1.0f;

    // Minimap picture, rebuilt when the map, rotation or box size changes
    SDL_Texture* miniMapTexture = nullptr;
    uint32_t miniMapGeneration = 0;
    int miniMapRotation = -1, miniMapW = 0, miniMapH = 0;
    void rebuildMiniMap(SDL_Renderer* renderer, int w, int h);

    // Debug state
    static int keyTimer;
    static bool showDebugInfo;
};

#endif // WORLD_HPP
