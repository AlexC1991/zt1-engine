#ifndef RENAME_CHEATS_HPP
#define RENAME_CHEATS_HPP

#include <functional>
#include <set>
#include <string>
#include <vector>

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
  // Every type of a cNameID made available (typesSetAvailable 0x591325, with
  // 1), the buy panels refreshed
  std::function<void(int)> typeAvailable;
};
void setHooks(const Hooks &hooks);
const Hooks &hooks();
// zoo.exe 0x591426 (applyHolidayStart), its real-date part: when the PC's own
// date (isRealDate 0x412c22: the system clock, not the zoo's calendar) is 31
// October, the first time in a run - witches every 30 seconds (0x607a78,
// kind 5084), every guest's clothes 10 / 2 / 11 (0x60739a), every
// building's colours 7 / 5 (0x6072e4) and the Jack-O'-Lantern available;
// the later zoos of that run only get the pumpkin. Returns whether it came
// in at all (day and month of the clock, month 1 to 12)
bool halloweenStart(World &world, int day, int month);
// A guest renamed (its index in Guests::list); the name already set.
// Returns the cheat's address ("" none) - for the checks.
std::string guestRenamed(World &world, int guest, const std::string &name);
// A staff member renamed (its id); the name already set
std::string staffRenamed(World &world, int id, const std::string &name);
// An animal renamed (its id); the name already set
std::string animalRenamed(World &world, int id, const std::string &name);
// An exhibit named (zoo.exe dinoExhibitNameList 0x5aee66, from 0x5aedbd:
// the name box, kept with something to see in it): "wonderland",
// "Microsoft", "Blue Fang", "Xanadu", "Cretaceous Corral", "Kathmandu",
// "Copralite Corner", "Super Croc", compared exactly. Returns the cheat's
// address ("" none) - for the checks. Original behaviour, not an addition
// (#298)
std::string exhibitRenamed(World &world, const std::string &name);
// "Susan Kittleson" / "Chuck Frizelle" (zoo.exe 0x6392c1): from then on,
// till the game is closed, the marketing's bonus counted twice in the rating
// guests come by (guestArrivalCheck 0x424375 at 0x42443d)
bool marketingTwice();
void setMarketingTwice(bool on); // (ours: the checks put it back)
// The cheats' global flags (zoo.exe: set by 0x44940b, never cleared but
// Dr. Dolittle's; not saved - ours: the checks put them back)
struct Flags {
  bool marketingTwice = false; // 0x6392c1
  bool eggman = false;         // 0x63cfa1: eggs made hatch in 5 ticks
  bool dolittle = false;       // 0x63c2e3: the next bear breeding check passes
  bool scholl = false;         // 0x63c2e4: scientists move at twice the rate
  bool frankenstein = false;   // 0x64e83a: 1 heal in 10 makes a dinosaur
  bool rosalie = false;        // 0x654ad8: set by the tour guide's name, read by nothing
  bool deer = false;           // 0x654ad9: set by "Deer", read by nothing
  // The exhibit names' flags (dinoExhibitNameList 0x5aee66; never cleared, not
  // saved, loadConfig 0x591a1f stops short of them):
  bool wonderland = false; // 0x6392c0: "wonderland", 30 added to the arrival check's rating (0x42444d)
  bool microsoft = false;  // 0x63c54e: "Microsoft", a viewing donation doubled (guestDoneViewingExhibit 0x42eb0e)
  bool blueFang = false;   // 0x63aff0: "Blue Fang", what a guest pays at a building doubled (guestBuy 0x42d8ee)
};
const Flags &flags();
void setFlags(const Flags &f);
// Dr. Dolittle's flag used up (zoo.exe 0x5db9c7)
void dolittleUsed();
// The name table (zoo.exe 0x639040, filled by 0x514554 from every resource
// skeepr*.cfg; none ship with the game) and its names used once (0x641860,
// saved in a zoo's cheats section - cheatsSave 0x47a10c, cheatsLoad
// 0x594197: ZooReader::usedNames, and SaveGame's version 25)
void loadNameTable(ResourceManager *rm);
std::set<std::string> &usedNames();
// The name table's entry for a unit renamed (zoo.exe 0x44940b's end): its
// type's name (+0x98) and subtype (+0xa4) to match the entry's, its colour
// picks (the guest's +0xfc, nullptr none). Returns whether one was applied
bool applyNameTable(const std::string &name, const std::string &typeName, const std::string &subtype,
                    int *colours, int colourCount);
// A guest kind's type name (zoo.exe its type's +0x98) and subtype (+0xa4),
// for the name table, from its key in guests.cfg ("man", "woman", "boy",
// "girl")
std::string guestType(const std::string &kind);
std::string guestSubtype(const std::string &kind);
// A zoo being read in (zoo.exe theGame +0x441, loadingZoo: set at 0x453052,
// cleared at 0x4531ca): the name table isn't applied then (0x4494d1) - a
// unit's name read from the zoo, or a default name made as it's read in.
// ours: SaveGame sets it while it reads a zoo; Loading holds it for a scope
bool loading();
struct Loading {
  bool was;
  Loading();
  ~Loading();
};
// (ours: the checks) An entry put in the table, as a skeepr*.cfg section;
// an empty name empties it
void setNameEntry(const std::string &name, const std::string &type, const std::string &subtype, int bonusCash,
                  int c0, int c1, int c2, const std::vector<int> &research, bool oneTime);
} // namespace RenameCheats

#endif // RENAME_CHEATS_HPP
