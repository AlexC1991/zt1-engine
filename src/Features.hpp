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

inline std::vector<Switch> &all() {
  static std::vector<Switch> list = {
      {"allunlocked", "freeform items all available from the start (off: released month by month)", &allUnlocked},
      {"fencemodes", "fence drag modes, Tab to cycle (off: the fence follows the mouse)", &fenceModes},
      {"staffpaths", "staff take paths and idle on them (off: wander open ground)", &staffPaths},
      {"smoothroutes", "routes straightened where the way is clear (off: tile by tile)", &smoothRoutes},
      {"maptooltips", "tool hints shown over the map (off: only on buttons)", &mapTooltips},
      {"elevatedpaths", "raised walkways and stairs, PageUp/PageDown sets the height (off: none, as ZT1)", &elevatedPaths},
  };
  return list;
}
} // namespace Features

#endif // FEATURES_HPP
