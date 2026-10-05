#ifndef FENCES_HPP
#define FENCES_HPP

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "CompassDirection.hpp"

class Animation;
class ResourceManager;
class WorldMap;
class WorldRenderer;
class ZooReader;

// ============================================================================
// FENCES AND EXHIBITS
// ============================================================================
// Measured against the original:
// - Fences stand on tile edges. Dragging with a fence picked lays one along
//   the grid lines the cursor passes (a staircase where it runs across
//   them); each piece costs the fence's price, charged when the button is
//   let go.
// - A loop of fence that shuts off ground from the zoo makes an exhibit: a
//   "New Exhibit Name:" box (ui/newname.lyt) offers "Exhibit N" ("Tank N"
//   for tank walls; N counts every exhibit made), and on OK a gate (the
//   fence's g subtype, the same price) goes into the loop where it was
//   started. The Exhibit/Show List (ui/mulhab.lyt, the HUD's exhibit
//   button) lists them; one shows when it was constructed, its upkeep and
//   whether staff are assigned.
// - Tank walls (fences with [TankFence]: Marine Mania) make a tank: the
//   ground inside sinks 4 height units (tanks.cfg initialSink), the floor
//   becomes sand (tankTerrain) and the walls run from the floor to one unit
//   above the ground (initialHeight 5), built of stacked pieces (bottom,
//   middle, top; left/right ends and single columns differ), and it fills
//   with salt water. Bulldozing a piece asks first ("Deleting this wall
//   piece will drain the tank ..."), then drains it: the water and the tall
//   walls go, the hole stays.
// ============================================================================
struct FenceType {
  std::string key;  // registry key and the map's subclass: chainlnk, atltank
  std::string file; // fences/chainlnk.ai
  std::string name;
  int cost = 0, gateCost = 0;
  int height = 0;
  bool tank = false;    // [TankFence]
  bool seeThrough = false; // cSeeThrough: a glass wall (the tank seen through it)
  bool zooWall = false; // the zoo's own walls (not an exhibit's)
  // [f|g][flat, rising, falling]
  Animation *art[2][3] = {{nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr}};
  // Tank pieces (f only): [left end, middle, right end, single column] x
  // [bottom, middle, top], and the low ones for a one-unit wall
  Animation *tankArt[4][3] = {};
  Animation *tankLow[4] = {};
  // A tank's gate: the platform keepers stand on, its ladder into the water
  Animation *platform = nullptr, *ladder = nullptr;
  Animation *ladderOut = nullptr; // (its ladrout beside ladrin: the .ai names only ladrin)
  // Wear (f/Characteristics: cLife, cDecayedLife, cDecayDelta; tank walls
  // cIndestructible): worn down to cDecayedLife it shows its "det" art, at
  // nothing its "broke" art; [flat, rising, falling]
  int life = 10, decayedLife = 5, decayDelta = 25;
  bool indestructible = false;
  // What keeps animals in (cStrength: an animal stronger than it gets
  // through; cIsJumpable, cIsClimbable, cIsElectrified)
  int strength = 200;
  bool jumpable = false, climbable = false, electrified = false;
  Animation *det[3] = {}, *broke[3] = {};
};

class Fences {
  friend class SaveGame; // (saving and loading a game)
public:
  // An edge of the tile grid: along x at line y (horizontal) or along y at
  // line x (vertical)
  struct Edge {
    bool alongX = true;
    int x = 0, y = 0;
    bool operator<(const Edge &o) const {
      if (alongX != o.alongX)
        return alongX < o.alongX;
      if (y != o.y)
        return y < o.y;
      return x < o.x;
    }
    bool operator==(const Edge &o) const {
      return alongX == o.alongX && x == o.x && y == o.y;
    }
  };
  struct Piece {
    int type = -1;   // into types()
    bool gate = false;
    int facing = 4;  // 0 N, 2 E, 4 S, 6 W: the side of its owner tile
    int ownerX = 0, ownerY = 0;
    long order = 0;  // when it was placed (for the gate)
    int drag = 0;    // which drag laid it
    int tank = -1;   // the tank exhibit it walls, raised (-1: none)
    int tank2 = -1;  // a second tank, on its other side (a wall two share)
    float life = -1; // its wear left (cLife when new; -1: not set yet)
    float open = 0;  // a gate: how far open (its art's frames, 0 shut)
  };
  struct Exhibit {
    int id = 0;
    std::string name;
    std::set<std::pair<int, int>> tiles;
    bool tank = false;
    bool named = false;
    bool pocket = false; // shut off when the map was made: not an exhibit
    int constructedDay = 1, constructedMonth = 0, constructedYear = 1;
    // Tanks: the ground they sank from, and the water (0-1 full)
    int groundHeight = 0, floorHeight = 0;
    float water = 0;
    // Tanks: the walls' height over the floor in the original's subtiles
    // (tanks.cfg's units: half a height unit, 8 px - measured, a wall step
    // moves the rim 8 px), raised and lowered on Tank Adjustment; salt or
    // fresh water, and whether it is filling (else draining)
    int wallSub = 0;
    float wallTop() const { return this->floorHeight + this->wallSub * 0.5f; }
    // The water's height as shown: it follows the walls and base up a
    // unit a second (down at once); unset until first drawn
    float level = -1e9f;
    bool salt = true;
    bool filling = true;
    // How clean its water is (tanks.cfg: initialWaterPurity 100, a point
    // lost every waterPurityDecayTime; murky under 60, very under 20)
    float purity = 100;
    // Waves on the surface (zoo.exe 0x496382: twaterwv / frshwav, one on a
    // random tile every 40000 / tiles ms while the water's clean, gone
    // after 4 s): where, which way, how old (ms); the countdown to the next
    struct Wave {
      int x, y, facing;
      float age;
    };
    std::vector<Wave> waves;
    float waveIn = 0;
    float purityClock = 0;
    // DEV CONSOLE fake stats (no animals yet): how many animals it "has"
    // and what kind, for the staff to look after
    int testAnimals = 0;
    bool testDino = false, testCarnivore = false;
    // Its real animals (set each frame); with the fake ones, what the
    // staff look after
    int animals = 0;
    int animalCount() const { return testAnimals + animals; }
    // Its books (zoo.exe hab+0xfc..0x110, rolled over monthly at
    // 0x48430c): guests' donations, and upkeep (keepers' food piles at
    // their cPurchaseCost, vet visits at 20% of the animal's price): this
    // month, last month, all told
    double donationsNow = 0, donationsLast = 0, donationsTotal = 0;
    double upkeepNow = 0, upkeepLast = 0, upkeepTotal = 0;
    // Popularity (0x468732): guests' viewing time since it was built (game
    // seconds on the guests' clock; builtAt unset until first seen)
    double viewed = 0, builtAt = -1;
    // 0-100: 15 x the guests watching it at once, on average
    int popularity(double now) const {
      if (builtAt < 0 || now - builtAt <= 0)
        return 0;
      return static_cast<int>(std::clamp(viewed / (now - builtAt) * 15.0, 0.0, 100.0));
    }
  };

  void loadTypes(ResourceManager *rm);
  // The map's fences (its fences/<key>/<f|g> objects)
  void load(const ZooReader &reader, WorldMap &map);
  void clear();

  const std::vector<FenceType> &types() const { return this->typeList; }
  int typeIndex(const std::string &key) const;
  const Piece *at(const Edge &e) const;
  const std::map<Edge, Piece> &pieces() const { return this->edges; }

  // Where fence can go: inside the main zoo wall (the original: "Zoo
  // objects can only be placed inside the main zoo wall"), not on water.
  // Worked out from the map's zoo walls when it loads.
  bool insideZoo(int x, int y) const;
  // Laying fence: whether a piece can go on an edge, and putting it there
  bool canPlace(const Edge &e, const WorldMap &map) const;
  // Why a piece can or can't go somewhere: it can (drawn green), not there
  // at all: off the map, outside the zoo, on water (red), or something is
  // in the way: a fence already, a building (orange)
  enum class Fit { Ok, Outside, InTheWay };
  Fit fit(const Edge &e, const WorldMap &map) const;
  // The same for a tile (the one under the cursor)
  Fit tileFit(int x, int y, const WorldMap &map) const;
  // A piece that can't go because it's outside the main zoo wall (on land)
  bool outsideZooWall(const Edge &e, const WorldMap &map) const;
  void place(const Edge &e, int type, int drag, const WorldMap &map);
  // The tank a wall is drawn with: its own, or of two sharing it the one
  // it's the back wall of (from the other it's the front: one face)
  int drawnWith(const Edge &e, const Piece &p, const WorldMap &map, const WorldRenderer &view) const;
  // A piece a fence can be laid over, replacing it
  bool replaceable(const Piece &p, int type) const;
  // What laying a piece of this kind there costs (over the same kind not
  // yet worn: nothing)
  int layCost(const Edge &e, int type) const;
  // Taking a piece away (bulldozer); a tank it walled drains first
  void remove(const Edge &e, WorldMap &map);

  // After fence changes: ground newly shut off becomes an exhibit (returned,
  // waiting to be named); exhibits opened up are gone
  // A gate no exhibit is behind any more is a plain piece again (as the
  // original, when an exhibit is opened up)
  void dropOrphanGates();
  // Buy Habitat's gate button ("Click to manually choose where to place the
  // entrance along an exhibit."): a piece between an exhibit and the open
  // zoo becomes its gate (the old one a plain fence), free unless worn
  // (then the gate's price)
  bool canGate(const Edge &e) const;
  int gateCostOf(const Edge &e) const;
  void moveGate(const Edge &e);
  // Whether an animal inside an exhibit can get out over the piece on an
  // edge (zoo.exe 0x414a73): no piece, or a broken one; else not the zoo's
  // walls or a gate, not a live electric fence; a climber over a climbable
  // one, a jumper over a jumpable one, a stronger one through it (its
  // strength worn down a cDecayDelta for each life it has lost)
  bool passable(const Edge &e, bool climber, bool jumper, int bash, int crush) const;
  bool broken(const Edge &e) const;
  // Gates swing open as someone comes to them (their art's frames, about
  // 20 a second) and shut after
  void updateGates(float seconds, const std::function<bool(const Edge &)> &someoneNear);
  // (tests) a piece made another type, as new
  void setPieceType(const Edge &e, int type) {
    auto it = this->edges.find(e);
    if (it != this->edges.end() && type >= 0 && type < static_cast<int>(this->typeList.size())) {
      it->second.type = type;
      it->second.life = static_cast<float>(this->typeList[type].life);
    }
  }
  // Something standing across an edge (a building, rock or tree's
  // footprint): no fence there
  std::function<bool(const Edge &)> edgeBlocked;
  // The state of an exhibit's fence (zoo.exe 0x449920, the Exhibit/Show
  // List's icon): 2 a piece broken, 1 a piece worn to its cDecayedLife
  // (one that wears), else 0
  int condition(int exhibit) const;
  // Broken through (life 0): anything can pass it until it's repaired
  void breakPiece(const Edge &e);
  std::vector<int> updateExhibits(WorldMap &map, int day, int month, int year,
                                 bool loading = false);
  Exhibit *exhibit(int id);
  const Exhibit *exhibit(int id) const {
    return const_cast<Fences *>(this)->exhibit(id);
  }
  const std::vector<Exhibit> &exhibits() const { return this->exhibitList; }
  // Names a new exhibit and puts its gate in (the fence's g piece where the
  // loop was started); the gate's price
  int finishExhibit(int id, const std::string &name, WorldMap &map);
  int exhibitCount() const { return this->made; }
  // Drains a tank (its Drain button): the water runs out, the tank stays
  void drainExhibit(int id);
  // A tank drained fills again, with salt or fresh water (Fill Tank)
  void fillExhibit(int id, bool salt = true);
  // Tank Adjustment (tanks.cfg; prices measured in the original): a step
  // of the wall costs each wall piece's price / wallHeightPriceDivisor and
  // the water a tile deeper takes (2 x 2 Concrete Edge and Glass, salt:
  // 8 x $25 + 4 x $1.50 = $206); a step of the base $8 a tile ($32). The
  // walls stand at most
  // maximumTankHeight above the floor and no lower than the ground around;
  // the base (the whole tank) recesses into the ground or comes up out of
  // it, its floor staying below the ground. Filling: the water's price
  // (saltWater / freshWater per unit, tiles x depth).
  double wallStepCost(int id) const;
  int baseStepCost(int id) const;
  bool canAdjustWall(int id, int step) const;
  void adjustWall(int id, int step);
  bool canAdjustBase(int id, int step) const;
  void adjustBase(int id, int step);
  int fillCost(int id, bool salt) const;
  // The exhibit a tank wall piece belongs to (-1: none)
  int tankOf(const Edge &e) const;
  // The (named) exhibit a tile is in, or a piece walls (-1: none)
  int exhibitAt(int x, int y) const;
  int exhibitOf(const Edge &e) const;

  // Tanks fill over time, their water gets dirty, filters clean it; fences
  // wear
  void update(float seconds);
  // Fence wear: worn (down to its cDecayedLife, needing a maintenance
  // worker), mended back to new
  bool worn(const Edge &e) const;
  // What the bulldozer gives back for a piece (measured: 80% of its price,
  // a $70 chain-link piece $56; a tank's other walls nothing)
  int refundOf(const Edge &e) const;
  void repair(const Edge &e);
  float lifeOf(const Edge &e) const;
  void setLife(const Edge &e, float life);
  // A maintenance worker's service: the filter as good as new
  void serviceFilter(int index);

  // Tank filters (scenery/other/filter1.ai): a 2x2 machine on land beside
  // a completed tank, its hose over the wall; one to a tank. It cleans the
  // water every cFilterDelay seconds (cFilterCleanAmount points, fewer once
  // decayed) for cFilterUpkeep each time; its health runs down a point every
  // cDecayTime seconds: decayed at cDecayedHealth, stopped at 0.
  struct FilterType {
    std::string file;
    int cost = 200, footprintX = 2, footprintY = 2;
    int startHealth = 10, decayedHealth = 5;
    float decayTime = 200, filterDelay = 50;
    int upkeep = 50, clean = 10, decayedClean = 5;
    Animation *idle = nullptr, *decayed = nullptr, *off = nullptr;
  };
  struct Filter {
    int x = 0, y = 0; // the footprint's top-left tile
    int tank = -1;    // the exhibit it cleans
    float health = 10;
    float decayClock = 0, filterClock = 0;
    float dx = 0, dy = 0; // toward its tank (world)
    std::string name;     // "Filter 1" (cUseNumbersInName)
    int placedMonth = 0;  // the game month it went in (for its panel)
    double upkeepLast = 0, upkeepCurrent = 0, upkeepTotal = 0;
  };
  const FilterType &filterType() const { return this->filterKind; }
  const std::vector<Filter> &filters() const { return this->filterList; }
  // A filter with its top-left tile at (x, y): whether it can go there
  // (Ok; Outside: not land in the zoo, or no completed tank beside it
  // without a filter; InTheWay: a fence, a building, an exhibit or another
  // filter there), and which tank it would clean
  Fit filterFit(int x, int y, const WorldMap &map, int *tank = nullptr) const;
  bool placeFilter(int x, int y, const WorldMap &map);
  // The filter drawn nearest a logical screen point (-1: none near)
  int filterAt(float sx, float sy, const WorldRenderer &view, const WorldMap &map) const;
  void removeFilter(int index);
  Filter *filter(int index) {
    return index >= 0 && index < static_cast<int>(this->filterList.size())
               ? &this->filterList[index]
               : nullptr;
  }
  // The game's month count (filters note when they went in) and a new
  // month (this month's upkeep becomes last month's)
  void setMonth(int month) { this->monthNow = month; }
  int month() const { return this->monthNow; }
  void newMonth();
  // Upkeep billed since last asked (whole dollars)
  int takeUpkeep();
  // The filter being placed (x < 0: none)
  int previewFilterX = -1, previewFilterY = -1;
  Fit previewFilterFit = Fit::Ok;

  // The piece drawn nearest a logical screen point (the bulldozer picks by
  // what is drawn: a tank wall stands well above its sunken floor); false
  // when none is within reach
  bool pieceAt(float sx, float sy, const WorldRenderer &view, const WorldMap &map,
               Edge &found) const;

  // Drawing: each piece (and the preview) as a drawable for PlacedObjects
  struct Drawable {
    float depth, sx, sy;
    Animation *art;
    CompassDirection side;
    SDL_Color tint;
    bool tinted;
    int frame = -1; // which frame (art that plays: staff walking); -1 its own
    // Drawn by code instead of art (a walkway deck with its posts)
    std::function<void(SDL_Renderer *)> custom;
    // Half a fence piece: only the screen columns from clipX0 to clipX1
    // (clip: all of it)
    bool clip = false;
    float clipX0 = 0, clipX1 = 0;
    float clipY1 = 1e9f; // (and nothing under this row: a tank wall's piece
                         // under the ground in front of it)
  };
  void collect(const WorldRenderer &view, const WorldMap &map,
               std::vector<Drawable> &out) const;
  // Water surfaces of tanks without the water art (drawn with the terrain;
  // with the art a tank's inside is drawn in collect)
  void drawWater(SDL_Renderer *renderer, const WorldRenderer &view,
                 const WorldMap &map) const;
  // A tank's diver platform under window-logical pixels: its tank, or -1
  int platformAt(float px, float py, const WorldRenderer &view) const;
  // The platform the cursor's over (drawn yellow), the tank whose panel is
  // open (its arrow over the platform)
  int hoverPlatform = -1;
  // The bulldozer over a filter: drawn red
  int highlightFilter = -1;
  // Game time (ms) for the water's ripples along the walls
  float rippleClock = 0;
  // The exhibit gate under the cursor: lit, as an animal is
  Edge hoverGate{false, -1, -1};
  // Draws some tiles' ground again (the world's terrain), over a tank's
  // inside where the ground in front of it hides the pit
  std::function<void(SDL_Renderer *, const std::set<std::pair<int, int>> &)> redrawGround;
  int selectedTank = -1;
  Animation *selectArrow = nullptr;

  // The placement preview: edges and whether they can be built
  std::vector<std::pair<Edge, bool>> preview;
  int previewType = -1;
  bool deleting = false; // the preview is the bulldozer's piece (red)

private:
  std::vector<FenceType> typeList;
  std::map<Edge, Piece> edges;
  FilterType filterKind;
  std::vector<Filter> filterList;
  int filtersMade = 0, monthNow = 0;
  double upkeepOwed = 0;
  float filterGround(const Filter &f, const WorldMap &map) const;
  std::vector<Exhibit> exhibitList;
  int lastRemovedGateOf = -1, lastRemovedOwner = -1;
  long placed = 0;
  int made = 0; // exhibits ever made (for "Exhibit N")
  ResourceManager *rm = nullptr;
  Animation *water = nullptr;
  // The tank water's art (water/<salt|fresh><scum>/<part>), loaded as used
  mutable std::map<std::string, Animation *> waterArt;
  Animation *objectArt(const std::string &path) const; // (cached, as waterPart)
  Animation *waterPart(const std::string &set, const std::string &part) const;
  // A tank wall's pieces (see Fences.cpp), and which end of its run it is
  struct TankPiece {
    Animation *art;
    float height; // what it's anchored at (height units)
    bool glass; // a pane of glass (drawn see-through) rather than wall
    bool top;   // the top rail's piece
    bool ladder = false; // the gate's ladder (lit with the platform)
  };
  void tankPieces(const Edge &e, const Piece &p, const Exhibit &ex, const WorldRenderer &view,
                  bool front, std::vector<TankPiece> &out) const;
  std::vector<uint8_t> inside; // per tile: within the main zoo wall
  std::vector<uint8_t> building; // per tile: under a building (the entrance)
  int mapW = 0, mapH = 0;
  const WorldMap *mapRef = nullptr;
  WorldMap *mapEdit = nullptr; // the map tanks sink into

  void edgeGeometry(const Edge &e, const Piece &p, const WorldMap &map, int &ax,
                    int &ay, int &bx, int &by, int &h0, int &h1) const;
  static Piece ownerFor(const Edge &e, const WorldMap &map);
  void makeTank(Exhibit &ex, WorldMap &map);
  void drainTank(Exhibit &ex);
};

#endif // FENCES_HPP
