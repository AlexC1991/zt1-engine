#ifndef WORLD_HPP
#define WORLD_HPP

#include "ResourceManager.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include "SpriteDatabase.hpp"
#include "SpriteManager.hpp"
#include "EntityManager.hpp"
#include "PlacedObjects.hpp"
#include "Ambient.hpp"
#include "Staff.hpp"
#include "ZooItems.hpp"
#include "WorldIndex.hpp"
#include "Walkways.hpp"
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
    // The fence piece drawn at a window point
    bool pickFence(int x, int y, Fences::Edge &e);
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

    // Fences and exhibits
    Fences& getFences() { return fences; }
    const PlacedObjects& getObjects() const { return placedObjects; }
    // The fence being laid (a type index into getFences().types(); -1: none)
    void setFenceTool(int type);
    int getFenceTool() const { return fenceTool; }
    void setBulldozer(bool on) { bulldozer = on; if (!on) { highlight = {}; bulldozeTip.clear(); } }
    bool isBulldozing() const { return bulldozer; }
    bool isBulldozer() const { return bulldozer; }
    // The mouse over the map (window pixels). Down/up return what was done:
    struct ToolResult {
        int cost = 0;                  // money spent (fence laid)
        std::vector<int> newExhibits;  // exhibits made (to be named)
        bool askDrainTank = false;     // a tank wall clicked: ask first
        int exhibit = -1;              // no tool: the exhibit clicked
        bool outsideZoo = false;       // fence tried outside the zoo wall
        int messageId = 0;             // a message to show (lang string)
        int filter = -1;               // no tool: the tank filter clicked
        int wage = 0;                  // staff hired: the first month's pay
        int refund = 0;                // bulldozed: money back (Recycling)
        int staff = -1;                // a staff member clicked (or hired)
    };
    // Where a world grid vertex is on the window (pixels), on the ground
    bool vertexToWindow(int vx, int vy, int &x, int &y);
    // Where a point at a height is on the window (pixels)
    void pointToWindow(float wx, float wy, float h, int &x, int &y);
    void mouseMove(int x, int y);
    ToolResult mouseDown(int x, int y);
    ToolResult mouseUp(int x, int y);
    void cancelTool();
    // The cursor left the map (over the HUD): no piece or price shown
    void hoverOff();
    // Placing a tank filter (picked in the fence list)
    void setFilterTool(bool on);
    bool getFilterTool() const { return filterTool; }
    // Laying paths (a type: "path", "dirtpath"; empty: none) at a price a
    // tile
    void setPathTool(const std::string &type, int cost);
    const std::string &getPathTool() const { return pathTool; }
    // Where a path tile can go: zoo ground (not water, not an exhibit, not
    // under a rock or building), flat or a ramp, no path of that type
    bool canLayPath(int x, int y) const { return pathFit(x, y) == Fences::Fit::Ok; }
    // Green: it can go; orange: a path's there already, or something stands
    // there; red: not zoo ground, water, an exhibit or too steep
    Fences::Fit pathFit(int x, int y) const;
    bool isDraggingPath() const { return pathDragging; }
    // The path types a walkway can be built of: solid built ones (not
    // dirt, sand, trails, ice, grass mats, rock or stepping stones)
    static bool raisable(const std::string &type);
    // (elevated paths) the height a path is laid at above the ground where
    // the drag starts, 0 = on the ground
    void adjustBuildHeight(int by);
    int getBuildHeight() const { return buildHeight; }
    Walkways &getWalkways() { return walkways; }
    // Hiring staff (a type of getStaff().types(); -1: none)
    void setStaffTool(int type);
    int getStaffTool() const { return staffTool; }
    Staff &getStaff() { return staff; }
    ZooItems &getItems() { return items; }
    WorldIndex &getIndex() { return index; }
    // What stands where changed (a filter put down, a base built)
    void reindex();
    // (debug) routes, blocked tiles and goals drawn over the map
    bool debugPaths = false;
    void drawPathDebug(SDL_Renderer *renderer);
    // Staff Information's Move (the member follows the cursor until a
    // click puts it down) and Assign (the next exhibit clicked)
    void pickUpStaff(int id);
    void assignStaff(int id) { assigning = id; }
    bool isAssigning() const { return assigning >= 0; }
    // The camera follows a staff member (-1: none)
    void trackStaff(int id) { tracked = id; }
    void centreOnStaff(int id);
    // The view centred on a point of the ground (at its height)
    void centreOn(float x, float y);
    // The ground at the middle of the view (tiles)
    bool viewCentre(float &x, float &y) {
      return this->pick(this->viewRect.x + this->viewRect.w / 2,
                        this->viewRect.y + this->viewRect.h / 2, x, y);
    }
    // How a fence drag lays out from the press to the cursor (Tab cycles,
    // Shift+Tab back, like Satisfactory's belt build modes): a line that
    // bends once (first along the way you started pulling), the same bent
    // the other way, straight only, or the whole rectangle
    enum class FenceMode { Bend, BendOther, Straight, Box };
    void cycleFenceMode(int step);
    FenceMode getFenceMode() const { return fenceMode; }
    static const char *fenceModeName(FenceMode m);
    // Deletes the tank wall asked about (after the player agreed)
    // (what the clicked piece gives back)
    int confirmDrain();
    // The bulldozer over something: its name (for the tooltip)
    const std::string &bulldozeName() const { return bulldozeTip; }
    // What the cursor's tool would cost there, and where (for its label)
    int hoverCost() const { return hoverPrice; }
    // While dragging the original shows the price as money going out
    bool isDraggingFence() const { return dragging && dragMoved; }
    // The current date, for exhibits made
    void setDate(int day, int month, int year) {
      date = {day, month, year};
      ambient.setDate(day, month);
    }
    Ambient &getAmbient() { return ambient; }

    // Camera control
    void setCameraPosition(int x, int y);
    void getCameraPosition(int& x, int& y) const;

private:
    ResourceManager* resourceManager;

    // Core subsystems (separated concerns)
    WorldMap worldMap;              // Pure data
    WorldRenderer worldRenderer;    // Pure rendering
    EntityManager entityManager;    // Entity tracking
    PlacedObjects placedObjects;    // The map's entrance, scenery
    Ambient ambient;                // Birds flying over
    Staff staff;                    // Keepers, maintenance workers, ...
    ZooItems items;                 // Food, dung, litter lying about
    WorldIndex index;               // Where everything is
    float reindexIn = 0;
    int staffTool = -1, assigning = -1, tracked = -1;
    std::string pathTool;
    int pathCost = 0;
    int buildHeight = 0;
    bool raisedDrag = false; // a drag making walkways (raised, or from a deck)
    Walkways walkways;
    // A raised drag's tiles: each a deck (its corner heights) or a ground
    // path tile, and whether it can be built
    struct DeckPreview {
      int x, y;
      int h[4];
      bool ground;
      bool ok;
    };
    std::vector<DeckPreview> deckPreview;
    void layDeckPreview();
    void collectWalkways(std::vector<Fences::Drawable> &out);
    int deckAt(int sx, int sy, int &tx, int &ty); // a deck under window pixels
    bool pathDragging = false;
    std::vector<std::pair<std::pair<int, int>, Fences::Fit>> pathPreview;
    std::pair<int, int> pathLast = {-1, -1};
    void addPathTile(int x, int y);
    std::string bulldozeTip;
    Fences fences;                  // Fences, exhibits and tanks
    int fenceTool = -1;
    bool filterTool = false;
    bool bulldozer = false;
    bool dragging = false;
    int dragCount = 0;
    int dragVertexX = 0, dragVertexY = 0;
    int pressVertexX = 0, pressVertexY = 0;
    bool dragMoved = false;
    FenceMode fenceMode = FenceMode::Bend;
    int firstAxis = -1; // the way the drag started: 0 along x, 1 along y
    // (fencemodes off) the grid points the cursor has gone through, as the
    // original's fence follows the mouse
    std::vector<std::pair<int, int>> followPath;
    void layDrag(int vx, int vy);
    // The tile under the cursor, outlined while laying fence (green: fence
    // can go there; red: outside the zoo or water)
    int hoverTileX = -1, hoverTileY = -1;
    Fences::Fit hoverTileFit = Fences::Fit::Outside;
    void drawToolOverlay(SDL_Renderer *renderer);
    Fences::Edge pressEdge = {false, -1, -1}; // the piece under the press
    int hoverPrice = -1; // -1: none shown
    Fences::Edge highlight = {false, -1, -1};
    Fences::Edge pendingDrain = {false, -1, -1};
    struct { int day, month, year; } date = {1, 0, 1};
    // Window pixels to the ground (world tiles); the nearest grid vertex and
    // edge
    bool pick(int x, int y, float &wx, float &wy);
    Fences::Edge nearestEdge(float wx, float wy) const;
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
