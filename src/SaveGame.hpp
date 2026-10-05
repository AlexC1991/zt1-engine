#ifndef SAVE_GAME_HPP
#define SAVE_GAME_HPP

#include <string>

class World;
class ZooSim;
class UiGameScreen;

// A saved zoo: the map it was started on, then everything that's changed
// since (the ground, fences and exhibits, what's been built, the animals,
// staff and guests, the books, research), so loading starts that map
// afresh and puts it all back. (Our own file format: the original's .zoo
// saves aren't read or written yet.)
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
  // The Windows "Save a zoo..." / "Load a zoo..." dialogs (empty: cancelled)
  static std::string askSavePath();
  static std::string askLoadPath();
};

#endif // SAVE_GAME_HPP
