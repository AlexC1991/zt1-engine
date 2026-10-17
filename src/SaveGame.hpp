#ifndef SAVE_GAME_HPP
#define SAVE_GAME_HPP

#include <string>

class World;
class ZooSim;
class UiGameScreen;

// A saved zoo: the map it was started on, then everything that's changed
// since (the ground, fences and exhibits, what's been built, the animals,
// staff and guests, the books, research), so loading starts that map
// afresh and puts it all back. That's our own file format; the original's
// own saved zoos (.zoo) are read too (loadOriginal), not written.
class Goals;

class SaveGame {
public:
  // The game's goals (states and awards kept with the zoo)
  static Goals *goals;
  // Where saved zoos go (Documents/zt1-engine/Saved Games), made if need be
  static std::string folder();
  static bool save(const std::string &path, World &world, ZooSim &sim, UiGameScreen &hud,
                   const std::string &mapPath);
  // The map a save was started on (empty: not a save)
  static std::string mapOf(const std::string &path);
  // After World::loadFreeform(mapOf(path)) and the HUD's set up
  static bool load(const std::string &path, World &world, ZooSim &sim, UiGameScreen &hud);
  // The original game's own saved zoo (a .zoo: "TZFB")
  static bool isOriginal(const std::string &path);
  // After World::loadFreeform(path) - the save is its own map - and the
  // HUD's set up: the rest of what it holds put back (its tanks and
  // habitats, the food and dung down, the animals and staff, the cash, date,
  // admission and rating). False: it didn't read in the original's order.
  static bool loadOriginal(World &world, ZooSim &sim, UiGameScreen &hud);
  // (its world alone - habitats, food and dung, animals, staff - for the
  // ZT1 Lab's test zoo, which keeps no books)
  static bool loadOriginalWorld(World &world);
  // The Windows "Save a zoo..." / "Load a zoo..." dialogs (empty: cancelled)
  static std::string askSavePath();
  static std::string askLoadPath();
};

#endif // SAVE_GAME_HPP
