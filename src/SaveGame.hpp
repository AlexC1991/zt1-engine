#ifndef SAVE_GAME_HPP
#define SAVE_GAME_HPP

#include <cstdint>
#include <string>
#include <vector>

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
  // Native Lab regression: compare a just-loaded original zoo with its
  // decoded records (run before advancing time; never in a player's zoo).
  static std::string checkOriginalImport(World &world);
  // A map design (a freeform map made in the ZT1 Lab): the world as made -
  // ground, paths, walkways, fences, exhibits, objects - and its view, over
  // the map whose .zoo is "over"; put over that map once it's loaded
  static bool saveMap(const std::string &path, World &world, const std::string &over);
  static bool applyMap(const std::vector<char> &data, World &world);
  // The Windows "Save a zoo..." / "Load a zoo..." dialogs (empty: cancelled)
  static std::string askSavePath();
  static std::string askLoadPath();

private:
  // (the world as made - in a saved zoo and a map design alike: their own
  // writer and reader, SaveGame.cpp's Out and In)
  static void putWorld(void *out, World &world);
  static bool getWorld(void *in, World &world, uint32_t version);
  static void restoreOriginalManagers(World &world, UiGameScreen &hud);
  static void restoreOriginalBooks(World &world, ZooSim &sim);
};

#endif // SAVE_GAME_HPP
