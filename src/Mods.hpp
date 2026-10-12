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
namespace Mods {
struct Mod {
  std::string folder;   // its folder's name (the key)
  std::string path;     // the folder itself
  std::string name, author, version, about;
  std::vector<std::string> replaces; // the game files it changes
  // Its art for the Mod Menu: a frame it changes, with its own palette and
  // the original's (mod.json "preview", "palette", "originalPalette")
  std::string preview, palette, originalPalette;
  bool enabled = false;
};
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
