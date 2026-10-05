#ifndef TERRAIN_TOOL_HPP
#define TERRAIN_TOOL_HPP

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class ResourceManager;
class WorldMap;
class Fences;
struct MapTile;

// Buy Habitat's terraform tabs (zoo.exe's terrain tool, read in its
// disassembly; the cost model checked against the running original):
// - a square brush of 1 to 5 tiles (even sizes reach right and down from
//   the cursor's tile), kept inside the map;
// - Terrain Types paints the brush's tiles as the cursor enters a tile
//   (zoo ground only, not tanks; water only onto flat open ground);
// - Terrain Height drags up or down: every 16 px (8 zoomed out, 32 in) the
//   brush's lowest corners rise a unit (highest fall), to -12..12; "hills
//   and valleys" slopes the ground round it a unit a tile, "cliffs and
//   moats" moves the brush alone; the levelling tools bring the brush to
//   the height of the tile first clicked;
// - nothing is paid until Accept: every tile changed since the tab opened
//   costs 8 a unit its highest-moved corner moved, plus its new terrain's
//   cost if that changed (painting a tile back costs nothing); Undo puts it
//   all back; changing tab or closing the panel accepts it if the zoo can
//   pay, else undoes it ("There are not enough funds ...").
class TerrainTool {
public:
  enum class Mode { Hills, Cliffs, LevelHills, LevelCliffs };
  void load(ResourceManager *rm);
  void setMap(WorldMap *map, const Fences *fences) {
    this->map = map;
    this->fences = fences;
  }
  // Tiles with objects bigger than a quarter tile move as flat blocks
  std::function<bool(int x, int y)> hasBigObject;

  // The tool picked (a tab of the terraform page) or not; picking it
  // starts a fresh account
  void setActive(bool on);
  bool isActive() const { return this->active; }
  bool painting = true; // Terrain Types (else Terrain Height)
  int terrainType = 0;
  int size = 2;
  Mode mode = Mode::Hills;

  // The cursor over a tile (tx, ty), at window-logical pixels px, py
  void hover(int tx, int ty);
  void press(int tx, int ty, float px, float py);
  void drag(int tx, int ty, float px, float py, float pixelsPerUnit);
  void release() { this->held = false; }
  bool isHeld() const { return this->held; }

  // What it would cost now (negative: more than the zoo has), one brush
  // stroke here, and the brush's tiles
  float cost() const;
  float strokeCost() const { return this->hoverCost; }
  void brushRect(int &x0, int &y0, int &x1, int &y1) const {
    x0 = this->bx0, y0 = this->by0, x1 = this->bx1, y1 = this->by1;
  }
  bool hasBrush() const { return this->bx1 > this->bx0; }
  // Accept: what to charge (the account starts again); Undo: everything
  // back as it was
  int accept();
  void undo();
  bool changed() const { return !this->baseline.empty() && this->cost() != 0; }
  // The last change's sound (terrpnt, terrup, terrdown, terrflat), taken
  std::string takeSound() {
    std::string s = this->sound;
    this->sound.clear();
    return s;
  }

private:
  struct TypeInfo {
    float cost = 0;
    int water = 0;
  };
  std::map<int, TypeInfo> types;
  WorldMap *map = nullptr;
  const Fences *fences = nullptr;
  bool active = false, held = false;
  int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; // the brush (end-exclusive)
  int pickX = -1, pickY = -1;
  float lastX = 0, lastY = 0;
  int levelHeight = 0;
  float hoverCost = 0;
  std::string sound;
  struct Saved {
    int corner[4];
    int terrain;
    int height;
  };
  std::map<std::pair<int, int>, Saved> baseline;
  std::map<std::pair<int, int>, int> flags; // corners moved this step (a bit each)

  void setBrush(int tx, int ty);
  bool inBrush(int x, int y) const { return x >= bx0 && x < bx1 && y >= by0 && y < by1; }
  bool owned(int x, int y) const;
  bool tank(int x, int y) const;
  bool water(const MapTile &t) const;
  bool slopable(int x, int y, const MapTile &t) const;
  bool canPaint(int x, int y, int type) const;
  void snapshot(int x, int y);
  void paintAt(int tx, int ty, bool force);
  enum class Step { Raise, Lower, LevelUp, LevelDown };
  bool heightStep(int target, bool contig, int kind); // kind: 0 raise, 1 lower, 2 level
  void moveTile(int x, int y, int target, bool exact, bool raise);
  void smooth(int x, int y, bool raise);
  void follow(int x, int y, int corner, int h, bool raise, int depth);
  void touch();
};

#endif // TERRAIN_TOOL_HPP
