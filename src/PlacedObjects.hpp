#ifndef PLACED_OBJECTS_HPP
#define PLACED_OBJECTS_HPP

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL2/SDL.h>

#include "Fences.hpp"

class Animation;
class ResourceManager;
class WorldMap;
class WorldRenderer;
class ZooReader;

// ============================================================================
// PLACED OBJECTS: what stands on the map - the entrance, fences, rocks,
// trees, buildings - as the map file places them
// ============================================================================
// Every object in the map file has a class, subclass and type (e.g.
// fences/zoowall/f, objects/other/srock1, building/building/fgate), a
// position in 64ths of a tile, a height in 16ths of a height unit and a
// facing (0 N, 2 E, 4 S, 6 W: the world's -y, +x, +y, -x).
//
// Fences stand on tile edges: the facing says which side of their tile
// (a fence at y = 2879 facing 4 is the south edge of tile row 44; at
// x = 1152 facing 6 the west edge of column 18). Their art is
// fences/<subclass>/<type>/idle, one frame per screen side of the tile
// (NE, SE, SW, NW), and idle30p / idle30n where the edge slopes.
// Everything else is objects/<type>/idle, one frame per facing as seen.
// Paths are drawn with the terrain; ambient animals (birds) aren't drawn
// yet.
// ============================================================================
class PlacedObjects {
  friend class SaveGame; // (saving and loading a game)
public:
  struct Object {
    std::string className, subClass, typeName, name;
    float x = 0, y = 0; // tiles
    float z = 0;        // height units
    int facing = 0;     // 0-7, 0 = north (-y), clockwise
    bool fence = false;
    bool bought = false; // put down in this game (not the map's)
    // A building bought: its name ("Snack Machine 1"), price (stands),
    // takings and upkeep so far, what it sold, the month it opened
    int id = 0;
    std::string label;
    float price = -1;
    double income = 0, upkeep = 0;
    std::map<std::string, int> sold;
    int openedMonth = 0;
    // Visitors (guests who went in): this month, last month, all told
    int visitorsNow = 0, visitorsLast = 0, visitorsTotal = 0;
    // Its colours (cIsColorReplaced buildings): the palette picked for
    // each part ([colorrep] replace=), -1 its defaultpal
    std::vector<int> colours;
    Animation *art = nullptr;
    Animation *slopeUp = nullptr, *slopeDown = nullptr; // fences
    // Stands (drink, hot dog, ice cream): the body behind the counter
    // (objects/<type>/bg), idle drawn over it; "used" in place of idle while
    // a guest is at it (set by the guests each tick)
    Animation *bg = nullptr, *used = nullptr;
    bool inUse = false;
    // Trash cans: the rubbish in it (a guest's piece +1, taken while 24 or
    // less: 25 at most, cCapacity isn't used - zoo.exe 0x42d181), shown
    // half full over 12 (its "full" art never shows in the original:
    // 0x412513); emptied by maintenance workers
    int fill = 0;
    Animation *half = nullptr;
    // A bought building's cNameID, and an animal house's program (an index
    // into its collection, cheapest first: zoo.exe inst+0x188)
    int nameId = 0, program = 0;
  };

  // The HUD's Hide Foliage / Hide Buildings toggles: not drawn, not
  // picked (the zoo's entrance stays, as the original)
  bool hideFoliage = false, hideBuildings = false;
  bool hiddenNow(const Object &o) const {
    if (this->hideFoliage && o.subClass == "foliage")
      return true;
    if (!this->hideBuildings || o.subClass != "building")
      return false;
    const std::string &t = o.typeName;
    return !(t.size() >= 4 && t.compare(t.size() - 4, 4, "gate") == 0);
  }

  ~PlacedObjects();
  void load(const ZooReader &reader, ResourceManager *rm);
  void clear();
  // Draws every object over the terrain, back to front
  // 'under': whether a world point is under something raised (a walkway
  // deck): what stands there draws first, beneath it
  void draw(SDL_Renderer *renderer, const WorldRenderer &view,
            const WorldMap &map,
            const std::vector<Fences::Drawable> &fences = {},
            const std::function<bool(float x, float y)> &under = nullptr);

  const std::vector<Object> &objects() const { return this->list; }
  // The bulldozer: the scenery under window-logical pixels (in front
  // first), drawn red; taken away
  int pick(float px, float py, const WorldRenderer &view, const WorldMap &map) const;
  int highlight = -1;
  void remove(int index);
  // One taken away put back as it was (Undo)
  void restore(const Object &o) { this->list.push_back(o); }
  // A bought object put down (its catalogue file: scenery/<subclass>/<type>.ai)
  // at a point, facing 0 N, 2 E, 4 S, 6 W; false when it has no art
  bool add(const std::string &file, float x, float y, int facing);
  // The last one put down (to name it and so on)
  Object *last() { return this->list.empty() ? nullptr : &this->list.back(); }
  Object *byId(int id) {
    for (Object &o : this->list)
      if (o.id == id)
        return &o;
    return nullptr;
  }
  int indexOf(int id) const {
    for (size_t i = 0; i < this->list.size(); i++)
      if (this->list[i].id == id)
        return static_cast<int>(i);
    return -1;
  }
  std::vector<Object> &all() { return this->list; }
  // The art an object of a catalogue file stands as
  Animation *artOfFile(const std::string &file);
  // A building's colour parts (its .ai [colorrep]: replace= cr_partN from
  // building.ai, title=, defaultpal=; [cr_color] fullpal, ncolors): each
  // part's 16- or 8-colour palettes and their swatches. Null: it can't be
  // painted.
  struct ColourPart {
    int title = 0;
    int fallback = 0;           // its defaultpal's place in the list
    int size = 16;              // colours it replaces
    std::vector<std::string> pals;
    std::vector<SDL_Color> swatches;
  };
  struct Colouring {
    std::string fullPal;
    int fixed = 232;            // the full palette's own colours (the parts follow)
    std::vector<ColourPart> parts;
  };
  const Colouring *colouringOf(const std::string &file);
  // A part's colour picked (-1 its default): the building drawn in it
  void setColour(Object &o, int part, int choice);
  // The palette a part shows (its pick, else its default)
  int colourOf(const Object &o, int part);
  // Its catalogue file (scenery/<subclass>/<type>.ai)
  static std::string fileOf(const Object &o) { return "scenery/" + o.subClass + "/" + o.typeName + ".ai"; }

private:
  int nextId = 1;
  std::map<std::string, int> counts; // per type: how many were ever bought
  std::vector<Object> list;
  ResourceManager *rm = nullptr;
  // Art shared by objects of a type (owned here)
  std::unordered_map<std::string, Animation *> art;
  Animation *artFor(const std::string &path);
  std::map<std::string, Colouring *> colourings; // per file (null: none)
  // Footprints (cFootprintX / Y, half-tiles) by file
  std::map<std::string, std::pair<int, int>> footprints;
  std::pair<int, int> footprintOf(const Object &o);
  // (each colour scheme's art, by file and picks)
  std::unordered_map<std::string, Animation *> paintedArt;
  Animation *paintedArtOf(const Object &o, const std::string &layer = "idle");
public:
  // An object's other layers (bg, used) as its colours make them
  void loadLayers(Object &o);
private:
};

#endif // PLACED_OBJECTS_HPP
