#ifndef ITEM_CATALOG_HPP
#define ITEM_CATALOG_HPP

#include <map>
#include <set>
#include <string>
#include <vector>

class ResourceManager;

// ============================================================================
// ITEM CATALOG
// ============================================================================
// Everything the player can buy. Like the original:
//
// - The entity registry: the config files (animals.cfg, animal01.cfg ...
//   bldg*.cfg, fences*.cfg, paths*.cfg, scenery*.cfg, staff*.cfg, ...) list
//   every entity's .ai file. Base game, expansions and update packs each
//   add their own files; together they are one list (e.g. all 118 animals),
//   read in file name order (bldg.cfg, bldg06.cfg, ...), each section's
//   entries by key (as the original reads them: alphabetically). Downloaded
//   content (.uca/.ucb/.ucs files, e.g. the yeti) is added too and is always
//   available.
// - An item's [Member] lines are the categories it is listed under, which
//   are what the buy panels' tabs ask for (stringData=animals, shelters,
//   toys, structures, scenery, fence, paths, foliage, rocks, staff).
// - Freeform availability: every freeform map runs freeform/unlock.scn
//   (and the expansions' unlock01..unlock20.scn). Their DEFAULT_ITEMS goal
//   lists what a new zoo can buy; later goals unlock more after so many
//   months. Items are named by their cNameID.
// - Order: the registry's, stable-sorted (measured against every tab of
//   the original's buy panels) by habitat, continent, family, genus, then
//   price - which for buildings and scenery is just price; except staff,
//   by how often they work (others, like the DRT base, last), and fences:
//   habitat fences first, tallest then cheapest, then the zoo fences.
// ============================================================================
struct CatalogItem {
  std::string file;  // e.g. animals/elephant.ai
  std::string type;  // [Global] Type, e.g. elephant
  int registryIndex = 0;
  std::string registrySection; // e.g. animals, building, fences, tankwall
  std::set<std::string> members;
  std::string icon;  // animation shown in the buy panels (the male's)
  // [Icon]'s icons, one per facing (objects: SE, SW, NW, NE), which the buy
  // panels' rotate buttons step through; icons[0] == icon
  std::vector<std::string> icons;
  int nameId = 0;    // cNameID (lang DLL string, and the unlock lists' id)
  std::string name;
  int cost = 0;      // cPurchaseCost
  int habitatId = 0;  // cHabitat
  int locationId = 0; // cLocation (continent)
  int family = 0;     // cFamily
  int genus = 0;      // cGenus
  int workCheck = -1; // cWorkCheck (staff; -1 = none)
  int height = 0;     // cHeight (fences)
  bool showFence = false; // cIsShowFence
  int capacity = 0;   // cCapacity (shelters)
  std::string prefIcon; // cPrefIcon: what the animal likes (an object icon)
  int dutiesTextId = 0; // cDutiesTextID (staff: what they do)
  int unlockMonth = -1; // freeform: 0 = from the start, n = after n months
  // Its pack, for the content filter: 1 Dinosaur Digs ([Member] dinosaur),
  // 2 Marine Mania ([Member] aqua), else 0 Zoo Tycoon. (Not cExpansionID:
  // the original files the Loch Ness Monster, the dinosaur fences and
  // buildings, which have none, under Dinosaur Digs.)
  int expansion = 0;
};

class ItemCatalog {
public:
  static ItemCatalog &get();

  // Reads the registry and the entity files once (later calls do nothing)
  void load(ResourceManager *resource_manager);

  // The items listed under a category, in the original's order.
  // availableOnly: only what a new freeform zoo can buy.
  std::vector<const CatalogItem *> inCategory(const std::string &category,
                                              bool availableOnly = true) const;

  const CatalogItem *find(const std::string &file) const;
  // The game's month (January of year 1 = 1): freeform unlocks of "value
  // 6" are there from the 6th month (measured: the Tank Filter in June)
  void setMonth(int month) { this->month = month; }
  int getMonth() const { return this->month; }
  // Every item read (available or not)
  const std::vector<CatalogItem> &all() const { return this->items; }
  // By cNameID (what research programs and unlock lists name items by)
  const CatalogItem *findName(int nameId) const;

private:
  std::vector<CatalogItem> items;
  int month = 1;
  bool loaded = false;
};

#endif // ITEM_CATALOG_HPP
