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
#include "Animals.hpp"
#include "Guests.hpp"
#include "TerrainTool.hpp"
#include "ZooItems.hpp"
#include "WorldIndex.hpp"
#include "Walkways.hpp"
#include "ZooReader.hpp"
#include <SDL2/SDL.h>
#include <map>
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
  friend class SaveGame; // (saving and loading a game)
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
    PlacedObjects &objectsMutable() { return placedObjects; }
    // The fence being laid (a type index into getFences().types(); -1: none)
    void setFenceTool(int type);
    int getFenceTool() const { return fenceTool; }
    void setBulldozer(bool on) {
      bulldozer = on;
      if (!on) {
        highlight = {false, -1, -1};
        bulldozeTip.clear();
        bulldozeDeck = bulldozePath = {-1, -1};
        fences.highlightFilter = -1;
        placedObjects.highlight = -1;
      }
    }
    bool isBulldozing() const { return bulldozer; }
    bool isBulldozer() const { return bulldozer; }
    // The mouse over the map (window pixels). Down/up return what was done:
    struct ToolResult {
        int cost = 0;                  // money spent (fence laid)
        std::vector<int> newExhibits;  // exhibits made (to be named)
        bool askDrainTank = false;     // a tank wall clicked: ask first
        bool askEscape = false;        // an exhibit's wall with animals: ask first
        std::string askMerge;          // a wall between two exhibits: "merge A with B?"
        int exhibit = -1;              // no tool: the exhibit clicked
        bool outsideZoo = false;       // fence tried outside the zoo wall
        int messageId = 0;             // a message to show (lang string)
        int filter = -1;               // no tool: the tank filter clicked
        int wage = 0;                  // staff hired: the first month's pay
        int refund = 0;                // bulldozed: money back (Recycling)
        int staff = -1;                // a staff member clicked (or hired)
        int animal = -1;               // an animal clicked
        int guest = -1;                // a guest clicked
        int building = -1;             // a building clicked (its id)
        int animalCost = 0;            // an animal adopted: its price
    };
    // Where a world grid vertex is on the window (pixels), on the ground
    bool vertexToWindow(int vx, int vy, int &x, int &y);
    // Where a point at a height is on the window (pixels)
    void pointToWindow(float wx, float wy, float h, int &x, int &y);
    // A walkway being laid: stopped (true if there was one)
    bool cancelLine();
    bool isLayingLine() const { return line.active; }
    // A tank's walls being dragged up or down (no tool, as the original)
    bool isAdjustingTank() const { return wallDrag.tank >= 0; }
    // What the cursor's over with no tool: a tank wall's hint, a diver
    // platform's tank
    const std::string &hoverTip() const { return hoverTipText; }
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
    // The terraform tabs' tool (picked while a tab of the page is up)
    TerrainTool &getTerrainTool() { return terrain; }
    // Its settings from the page, each frame
    void setTerrainTool(bool active, bool painting, int type, int size, int mode);
    // The gate button: clicking an exhibit's wall makes it the gate
    void setGateTool(bool on) {
      gateTool = on;
      if (on)
        bulldozer = false;
    }
    bool getGateTool() const { return gateTool; }
    // Placing a bought object (shelters, toys, buildings, scenery, foliage,
    // rocks: its catalogue file; empty: none), facing as the buy panel's
    // icon shows it (0 SE, 1 SW, 2 NW, 3 NE)
    void setObjectTool(const std::string &file, int cost, int facing);
    const std::string &getObjectTool() const { return objectTool; }
    // Where it can go: inside the zoo wall, its footprint on clear ground
    // (not water or path, nothing standing there, no fence through it)
    Fences::Fit objectFit(const std::string &file, float x, float y, int facing);
    // Its footprint in half tiles (cFootprintX, cFootprintY)
    std::pair<int, int> footprint(const std::string &file) { return footprintOf(file); }
    // Adopting animals (a type of getAnimals().types(), male or female;
    // -1: none)
    void setAnimalTool(int type, bool female);
    int getAnimalTool() const { return animalTool; }
    Animals &getAnimals() { return animals; }
    Guests &getGuests() { return guests; }
    // The zoo's rating and admission (adult), for who comes
    void setEconomy(int rating, double admission) {
      zooRating = rating;
      zooAdmission = admission;
    }
    // Animal Information's Move: it follows the cursor until a click puts
    // it down in an exhibit
    void pickUpAnimal(int id);
    // Sold: gone, its exhibit's other animals reacting
    void sellAnimal(int id);
    void centreOnAnimal(int id);
    ZooItems &getItems() { return items; }
    WorldIndex &getIndex() { return index; }
    // What stands where changed (a filter put down, a base built)
    void reindex();
    // (debug) routes, blocked tiles and goals drawn over the map
    bool debugPaths = false;
    // (debug) each bought building's footprint outlined
    bool debugFootprints = false;
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
    // Paths and walkways laid in a mode too (Bend, the other bend,
    // Straight; Tab cycles while a path is picked)
    void cyclePathMode(int step);
    FenceMode getPathMode() const { return pathMode; }
    void cycleFenceMode(int step);
    FenceMode getFenceMode() const { return fenceMode; }
    static const char *fenceModeName(FenceMode m);
    // Deletes the tank wall asked about (after the player agreed)
    // (what the clicked piece gives back)
    int confirmDrain();
    // Takes away the exhibit wall asked about ("Deleting this fence piece
    // will allow any animals in this exhibit to escape."): its refund
    int confirmFenceRemoval();
    // Undo (zoo.exe 0x4de527, the Single_Undo button 1075): one level -
    // everything done since the last click on the map, undone newest
    // first, with exactly the money it took or gave back
    struct UndoMoney {
      int construction = 0, animals = 0, wages = 0, recycling = 0;
    };
    bool canUndo() const { return !this->undoSteps.empty(); }
    UndoMoney undo();
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

    // The mouse at the window's edge scrolls the map (off for the
    // screenshot tests, where the mouse is wherever it happens to be)
    bool edgeScroll = true;
    static constexpr float kEdgeScrollPixels = 32.0f; // a step, 30 a second (zoo.ini mouseScrollX/Y)

    // Camera control
    void setCameraPosition(int x, int y);
    void getCameraPosition(int& x, int& y) const;

private:
    struct UndoStep {
      enum class Kind { Object, Animal, Staff, Path, Fence, RemovedObject, RemovedPath, RemovedFence } kind;
      int id = -1, x = 0, y = 0;
      std::string pathType;          // the tile's path before (Path, RemovedPath)
      Fences::Edge edge;
      int fenceType = -1;            // (RemovedFence; Fence: the kind it replaced)
      float fenceLife = -1;          // (Fence: the replaced one's wear)
      PlacedObjects::Object object;  // (RemovedObject)
      int money = 0;
    };
    std::vector<UndoStep> undoSteps;
    bool undoFresh = false; // a click on the map: the next thing done starts a new list
    void recordUndo(UndoStep step) {
      if (this->undoFresh) {
        this->undoSteps.clear();
        this->undoFresh = false;
      }
      this->undoSteps.push_back(std::move(step));
    }
    std::string pathTypeAt(int x, int y) const;
    ResourceManager* resourceManager;

    // Core subsystems (separated concerns)
    WorldMap worldMap;              // Pure data
    WorldRenderer worldRenderer;    // Pure rendering
    EntityManager entityManager;    // Entity tracking
    PlacedObjects placedObjects;    // The map's entrance, scenery
    Ambient ambient;                // Birds flying over
    Staff staff;                    // Keepers, maintenance workers, ...
    Animals animals;                // The zoo's animals
    bool gateTool = false;
    Guests guests;                  // The zoo's visitors
    int zooRating = 50;
    double zooAdmission = 22.0;
    void findEntrance();
    // The world's ambience (the scenario's worldConfig) and its random
    // bird calls (worldsnd.cfg)
    int ambienceChannel = -1;
    float birdClock = 0;
    struct BirdCall {
      std::string file;
      int prob = 0;
    };
    int birdChance = 0;
    std::vector<BirdCall> birdCalls;
    void startAmbience(const std::string &scenario);
    // Buildings' [AmbientSound] loops (fountains ...): heard near the view
    // (attenuation and distance under 2200), one started a second at most
    std::map<int, int> buildingLoops; // object index -> channel
    std::map<std::string, std::pair<std::string, int>> ambientOf; // file -> sound, attenuation
    float buildingSoundClock = 0;
    void updateBuildingSounds(float seconds);
    void exhibitsNear(int x0, int y0, int x1, int y1, std::vector<int> &out) const;
    TerrainTool terrain;            // Terraforming
    void drawBrush(SDL_Renderer *renderer);
    std::string objectTool;
    int objectCost = 0, objectFacing = 0;
    // The object at the cursor: where (snapped), its facing, and whether it
    // can go there
    bool objectGhost = false;
    float ghostX = 0, ghostY = 0;
    int ghostFacing = 0;
    Fences::Fit ghostFit = Fences::Fit::Ok;
    std::map<std::string, std::pair<int, int>> footprints; // half tiles
    std::pair<int, int> footprintOf(const std::string &file);
    int fencePreviewPrice() const; // the fence drag's pieces, priced
    int worldFacing(int iconFacing) const;
    void snapObject(const std::string &file, int facing, float &x, float &y);
    int animalTool = -1;
    bool animalFemale = false;
    double clock = 0;               // seconds of play (not paused)
    ZooItems items;                 // Food, dung, litter lying about
    WorldIndex index;               // Where everything is
    float reindexIn = 0;
    int staffTool = -1, assigning = -1, tracked = -1;
    std::string pathTool;
    int pathCost = 0;
    int buildHeight = 0;
    // A walkway being laid: straight along x or y from its start tile to
    // the tile nearest the cursor as drawn (at its height, not the ground
    // under it). Click, then click again to build it (and carry on from
    // its end), or drag and let go; Esc or a right click stops.
    struct WalkLine {
      bool active = false;   // started
      bool pressed = false;  // the button's down
      bool clicked = false;  // started with a click: the next click builds
      bool moved = false;    // dragged off its start
      bool fromDeck = false; // starting on a deck
      int sx = 0, sy = 0;    // start tile
      int ex = 0, ey = 0;    // end tile
      int pressX = 0, pressY = 0; // where the button went down (window)
      int firstAxis = -1;         // the way it first went: 0 x, 1 y
    } line;
    // A path drag that meets a cliff, a raised block or a walkway: planned
    // as one, with stairs and raised pieces where the ground can't carry
    // it (smartPlan: deckPreview holds the plan)
    bool smartPlan = false;
    FenceMode pathMode = FenceMode::Bend;
    std::pair<int, int> pathPress = {-1, -1}; // a ground drag's first tile
    int pathFirstAxis = -1;                   // the way it started: 0 x, 1 y
    // The tiles from one to another in the path mode: along one axis to the
    // corner, then the other (first along 'firstAxis', the way it started)
    std::vector<std::pair<int, int>> modeTiles(int sx, int sy, int ex, int ey, int firstAxis) const;
    void layPathDrag(int tx, int ty);
    // The path tool over a walkway: that deck lit (it starts from there)
    std::pair<int, int> hoverDeck = {-1, -1};
    std::vector<SDL_Vertex> deckOverlay; // (drawn after the decks)
    void planSmartPath();
    void aimLine(int x, int y);
    void startLine(int tx, int ty, bool fromDeck);
    int lineStartLevel(int dir) const;
    int buildLine();
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
    // Zoomed out: the map drawn 1:1 and halved down to the view
    static constexpr int kShrinkLevels = 4;
    struct ShrinkLayer {
      SDL_Texture *tex = nullptr;
      int w = 0, h = 0;
    } shrinkLayers[kShrinkLevels];
    bool shrinkTarget(SDL_Renderer *renderer, int level, int w, int h);
    // The bulldozer over a walkway deck or a path tile: drawn red
    std::pair<int, int> bulldozeDeck = {-1, -1}, bulldozePath = {-1, -1};
    std::string hoverTipText;
    // A tank wall pressed and dragged: its tank, where the press was, the
    // steps it has gone up (down negative)
    struct {
      int tank = -1;
      int pressY = 0;
      int steps = 0;
    } wallDrag;
    int wallDragCost() const;
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
    Fences::Edge pendingFence = {false, -1, -1};
    // A fence piece taken away: the exhibits worked out again, gates no
    // exhibit is behind made plain, the animals told
    int removeFence(const Fences::Edge &e);
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
