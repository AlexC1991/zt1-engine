#ifndef RENAME_CHEATS_HPP
#define RENAME_CHEATS_HPP

#include <functional>
#include <string>

class World;
class ResourceManager;
struct ResearchProgram;

// The original's rename cheats (zoo.exe 0x44940b, run by a unit's setName,
// class slot 0x1c 0x449515): a guest, keeper, maintenance worker, tour guide
// or animal given one of the makers' names does something. Original
// behaviour, not an addition. Issue #235: what's ported, and what isn't yet.
namespace RenameCheats {
// What the cheats need outside the world (ours: main.cpp sets them)
struct Hooks {
  ResourceManager *rm = nullptr;
  // A research program's effect (researchApplyEffect 0x58fb83)
  std::function<void(const ResearchProgram &)> research;
  // Money onto the books: a donation (0x42ebbe), a removal refund (0x4f9329),
  // each into the cash too (0x40f018)
  std::function<void(double)> donation, removalRefund;
  // zoo.ini [scenario]: every scenario's token at 0 (0x48a80a)
  std::function<void()> unlockScenarios;
};
void setHooks(const Hooks &hooks);
const Hooks &hooks();
// A guest renamed (its index in Guests::list); the name already set.
// Returns the cheat's address ("" none) - for the checks.
std::string guestRenamed(World &world, int guest, const std::string &name);
// A staff member renamed (its id); the name already set
std::string staffRenamed(World &world, int id, const std::string &name);
// "Susan Kittleson" / "Chuck Frizelle" (zoo.exe 0x6392c1): from then on,
// till the game is closed, the marketing's bonus counted twice in the rating
// guests come by (guestArrivalCheck 0x424375 at 0x42443d)
bool marketingTwice();
void setMarketingTwice(bool on); // (ours: the checks put it back)
} // namespace RenameCheats

#endif // RENAME_CHEATS_HPP
