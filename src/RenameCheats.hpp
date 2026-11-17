#ifndef RENAME_CHEATS_HPP
#define RENAME_CHEATS_HPP

#include <string>

class World;

// The original's rename cheats (zoo.exe 0x44940b, run by a unit's setName,
// class slot 0x1c 0x449515): a guest, keeper, maintenance worker, tour guide
// or animal given one of the makers' names does something. Original
// behaviour, not an addition. Issue #235: what's ported, and what isn't yet.
namespace RenameCheats {
// A guest renamed (its index in Guests::list); the name already set.
// Returns the cheat's address ("" none) - for the checks.
std::string guestRenamed(World &world, int guest, const std::string &name);
} // namespace RenameCheats

#endif // RENAME_CHEATS_HPP
