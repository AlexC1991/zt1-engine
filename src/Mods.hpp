#ifndef MODS_HPP
#define MODS_HPP

#include <string>
#include <vector>

// Mods: folders of the game's own kind of files that take the place of the
// originals while they're switched on (the original's own way: a later
// patch archive's files win). One mod is a folder in the mods folder
// (mods/ by the game, or wherever Mods' folder says) with
//   mod.json   {"name", "author", "version", "about", "replaces": [...]}
//   *.ztd      the files, under their game paths (fences/dzbar/f/idle/se)
// Which are on, and the folder, are kept in mods.cfg by the game. With the
// "mods" switch off (Features), none is loaded: the original's files only.
//
// A mod's own settings (mod.json "settings"): each its own row on the mod's
// page in Game Options (Mods & Updates > its Settings button), kept in
// zoo.ini [Mod:<folder>] and handed to its shaders as uniforms by name
//   {"name": "grain", "label": "Grain", "about": "...",
//    "type": "slider" | "toggle" | "choice", "min": 0, "max": 1, "value": 0.5,
//    "choices": ["Soft", "Sharp"]}
// (a toggle's value true/false - 1 or 0; a choice's, one of its choices -
// its place in the list)
namespace Mods {
struct Setting {
  std::string name, label, about;
  std::string type = "slider"; // slider | toggle | choice
  float min = 0, max = 1, standard = 0, value = 0;
  std::vector<std::string> choices;
};
struct Mod {
  std::string folder;   // its folder's name (the key)
  std::string path;     // the folder itself
  std::string name, author, version, about;
  std::vector<std::string> replaces; // the game files it changes
  // Its art for the Mod Menu: a frame it changes, with its own palette and
  // the original's (mod.json "preview", "palette", "originalPalette")
  std::string preview, palette, originalPalette;
  std::vector<Setting> settings;
  bool enabled = false;
};
// One of a mod's settings changed (kept in zoo.ini [Mod:<folder>])
void setSetting(const std::string &folder, const std::string &name, float value);
// A mod's setting as it is now (its value, else its mod.json's, else fallback)
float setting(const std::string &folder, const std::string &name, float fallback = 0);
// The folder mods are looked for in (mods.cfg "folder=", else "mods")
std::string folder();
void setFolder(const std::string &path);
// Every mod found there, with whether it's on
std::vector<Mod> list();
void setEnabled(const std::string &folder, bool on);
// The folders of the mods switched on, for the resource search path (the
// first wins): none with the mods switch off
std::vector<std::string> enabledPaths();
} // namespace Mods

#endif // MODS_HPP
