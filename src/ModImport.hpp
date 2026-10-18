#ifndef MOD_IMPORT_HPP
#define MOD_IMPORT_HPP

#include <string>
#include <vector>

// A mod added by the player, for their own game only (nothing is uploaded):
// the Mod Menu's "Add a mod...", and the Lab's import (zt1-engine
// --import-mod <path>). What it takes:
//   - an old Zoo Tycoon download - a .ztd, the 2001 game's own kind (a zip
//     of game files under their paths: animals/yeti/..., the yeti's .ai) -
//     made into a mod: its own folder with the .ztd and a mod.json saying
//     what it adds;
//   - a .zip: a mod (a mod.json at its top or one folder down) put in as it
//     is, or one or more old .ztds made into a mod;
//   - a mod's folder (its mod.json picked), copied in.
// Into the mods folder (Mods::folder), switched off. Only game data comes
// in: a file of any other kind (a program, a script) and the whole thing is
// turned down (allowed() says which).
namespace ModImport {

struct Result {
  bool ok = false;
  std::string folder;  // the mod's folder in the mods folder
  std::string name;
  std::string message; // what happened, in words
};

Result import(const std::string &path);

// Whether a mod may hold a file of this name (by its extension): game data
// only - the same list the mod repository's checks use
bool allowed(const std::string &fileName);

// What an old .ztd adds: its .ai files (animals/yeti/yeti.ai...)
std::vector<std::string> adds(const std::string &ztd);

} // namespace ModImport

#endif // MOD_IMPORT_HPP
