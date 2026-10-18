#ifndef FEATURES_HPP
#define FEATURES_HPP

#include <string>
#include <vector>

// Our additions and changes to the original game, each a switch: off is
// always the original's behaviour. Flipped in game from the dev console
// ("features", "set <name> on|off"); an options menu comes much later.
namespace Features {
struct Switch {
  const char *name;
  const char *about;
  bool *value;
};

inline bool allUnlocked = true;   // freeform: every item from day one (off: month by month)
inline bool fenceModes = true;    // fence drag modes, Tab cycles (off: follows the mouse)
inline bool staffPaths = true;    // staff keep to paths, idle on them (off: roam open ground)
inline bool smoothRoutes = true;  // walkers' routes straightened (off: tile to tile)
inline bool mapTooltips = true;   // tool hints over the map (off: buttons only)
inline bool elevatedPaths = true; // ZT2-style walkways and stairs (off: none, as ZT1)
inline bool pathModes = true;     // path drags bend once, Tab cycles (off: follows the mouse)
inline bool hotkeys = true;       // Esc / right-click put the bulldozer away, Ctrl+Z undoes (off: the button only)
inline bool mods = true;          // mods switched on in the mods folder are loaded (off: the original's files only)
inline bool modMenu = true;       // the main menu's Get New Zoo Tycoon Items opens the Mod Menu (off: as the original)
inline bool updateChecks = true;  // an installed official build looks for a newer release at start (off: never; Updater.hpp)
inline bool misnamedArt = true;   // art the original ships under a misspelt name is used (off: those animations stay missing)
inline bool optionsWindow = true; // Game Options as a window with every setting, also on the main menu (off: the original side panel)
inline bool tankGroundBack = true; // a tank taken away leaves the ground as it was (off: the sunken hole stays, as the original)
inline bool noFenceOnPath = true; // fences and tank walls can't be built across a path (off: they can, as the original)
inline bool modFilter = true;     // the buy panels' content filter has a Mods choice (off: All and the three packs, as the original)

inline std::vector<Switch> &all() {
  static std::vector<Switch> list = {
      {"allunlocked", "freeform items all available from the start (off: released month by month)", &allUnlocked},
      {"fencemodes", "fence drag modes, Tab to cycle (off: the fence follows the mouse)", &fenceModes},
      {"staffpaths", "staff take paths and idle on them (off: wander open ground)", &staffPaths},
      {"smoothroutes", "routes straightened where the way is clear (off: tile by tile)", &smoothRoutes},
      {"maptooltips", "tool hints shown over the map (off: only on buttons)", &mapTooltips},
      {"elevatedpaths", "raised walkways and stairs, the wheel sets the height (off: none, as ZT1)", &elevatedPaths},
      {"pathmodes", "path and walkway drags bend once, Tab to cycle (off: the path follows the mouse)", &pathModes},
      {"hotkeys", "Esc or right-click puts the bulldozer away, Ctrl+Z undoes, Q / E turn an object being placed, Alt+Enter or F11 switches full screen (off: their buttons only)", &hotkeys},
      {"mods", "mods switched on in the mods folder are loaded over the game's files (off: the original's files only)", &mods},
      {"modmenu", "the Mod Menu - your mods on or off, their folder - from Game Options' Mods & Updates (with the original options panel: the main menu's Get New Zoo Tycoon Items) (off: none, as the original, whose download service is gone)", &modMenu},
      {"updatechecks", "an installed official build looks for a newer release when it starts, and asks before updating (off: never looks)", &updateChecks},
      {"misnamedart", "Marine Mania art the game's files name wrongly (its own art, shipped under a misspelt name: the hammerhead's thrash, the marlin's, shadows) is found and shown (off: missing, as the original)", &misnamedArt},
      {"tankgroundback", "a tank taken away puts its ground back as it was before it sank - heights and terrain (off: the sunken hole stays, as the original)", &tankGroundBack},
      {"nofenceonpath", "fences and tank walls can't be built across a path (off: they can be, as the original - a tank then asks to clear the path before it fills)", &noFenceOnPath},
      {"modfilter", "the buy panels' content filter (All, Zoo Tycoon, Dinosaur Digs, Marine Mania) has a Mods choice too: only what your switched-on mods add or change (off: the original's four choices)", &modFilter},
      {"optionswindow", "Game Options opens as a window with every setting - graphics, sound, controls, gameplay - and the main menu has it too (off: the original's side panel, and no main menu entry)", &optionsWindow},
  };
  return list;
}
} // namespace Features

#endif // FEATURES_HPP
