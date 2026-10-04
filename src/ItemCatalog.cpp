#include "ItemCatalog.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>

#include <SDL2/SDL.h>

#include "ResourceManager.hpp"
#include "Utils.hpp"

ItemCatalog &ItemCatalog::get() {
  static ItemCatalog catalog;
  return catalog;
}

namespace {

// An entity (.ai) file: sections of key = value lines, plus bare lines
// (the [Member] list). Section and key names are matched lower case.
struct AiFile {
  std::map<std::string, std::map<std::string, std::string>> values;
  std::map<std::string, std::vector<std::string>> lines;
  // Every key = value of each section in file order, and the sections' order
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> entries;
  std::vector<std::string> order;

  explicit AiFile(const std::string &text) {
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
      size_t comment = line.find(';');
      if (comment != std::string::npos)
        line = line.substr(0, comment);
      line = trim(line);
      if (line.empty())
        continue;
      if (line.front() == '[' && line.back() == ']') {
        section = Utils::string_to_lower(line.substr(1, line.size() - 2));
        order.push_back(section);
        continue;
      }
      size_t eq = line.find('=');
      if (eq == std::string::npos) {
        lines[section].push_back(Utils::string_to_lower(line));
        continue;
      }
      std::string key = Utils::string_to_lower(trim(line.substr(0, eq)));
      values[section].emplace(key, trim(line.substr(eq + 1))); // first wins
      entries[section].push_back({key, trim(line.substr(eq + 1))});
    }
  }

  std::string get(const std::string &section, const std::string &key) const {
    auto s = values.find(section);
    if (s == values.end())
      return "";
    auto v = s->second.find(key);
    return v == s->second.end() ? "" : v->second;
  }

  static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
      return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
  }
};

} // namespace

void ItemCatalog::load(ResourceManager *rm) {
  // Loaded in the background while the menus show (see main); a game
  // started before it is done waits here
  static std::mutex loading;
  std::lock_guard<std::mutex> lock(loading);
  if (this->loaded || !rm)
    return;
  this->loaded = true;
  Uint32 start = SDL_GetTicks();

  // 1. The registry: every top-level config file that lists .ai files,
  //    base game first, then the expansions and update packs
  std::vector<std::string> configs; // listed in name order
  for (const std::string &name : rm->listResources("", ".cfg"))
    if (name.find('/') == std::string::npos)
      configs.push_back(name);
  std::unordered_map<std::string, std::string> configText = rm->readResources(configs);
  std::vector<std::string> files;
  std::map<std::string, std::string> fileSection;
  // An entity's subtypes can be given by the registry ([<key>/subtypes],
  // e.g. the tank walls' f and g) instead of its own [Global]; the first is
  // its default
  std::map<std::string, std::string> fileSubtype;
  std::set<std::string> seen;
  for (const std::string &cfg : configs) {
    AiFile registry(configText[cfg]);
    for (const std::string &section : registry.order) {
      auto entries = registry.entries[section];
      std::stable_sort(entries.begin(), entries.end(),
                       [](const auto &a, const auto &b) { return a.first < b.first; });
      for (const auto &kv : entries) {
        std::string path = Utils::string_to_lower(kv.second);
        if (path.size() > 3 && path.compare(path.size() - 3, 3, ".ai") == 0 &&
            seen.insert(path).second) {
          files.push_back(path);
          fileSection[path] = section;
          const auto &subs = registry.lines[kv.first + "/subtypes"];
          if (!subs.empty())
            fileSubtype[path] = subs.front();
        }
      }
    }
  }

  // Downloaded content (e.g. the yeti): .uca/.ucb/.ucs entity files
  // anywhere in the data (dlupdate's test/scenery/other/632db827.ucs too),
  // outside the registry, always available
  std::set<std::string> downloaded;
  for (const char *ext : {".uca", ".ucb", ".ucs"})
      for (const std::string &file : rm->listResources("", ext))
        if (seen.insert(file).second) {
          files.push_back(file);
          downloaded.insert(file);
        }

  // 2. Freeform availability: the unlock goals' item lists (by cNameID)
  std::map<int, int> unlockMonth;
  std::vector<std::string> unlocks = rm->listResources("freeform/unlock", ".scn");
  std::unordered_map<std::string, std::string> unlockText = rm->readResources(unlocks);
  for (const std::string &u : unlocks) {
    AiFile scn(unlockText[u]);
    for (const auto &goal : scn.entries["goals"]) {
      if (goal.first != "goal")
        continue;
      std::string g = Utils::string_to_lower(goal.second);
      int months = std::atoi(scn.get(g, "value").c_str());
      std::string list = Utils::string_to_lower(scn.get(g, "targa"));
      for (const auto &kv : scn.entries[list]) {
        if (kv.first != "id")
          continue;
        int id = std::atoi(kv.second.c_str());
        auto it = unlockMonth.find(id);
        if (it == unlockMonth.end() || months < it->second)
          unlockMonth[id] = months;
      }
    }
  }

  // 3. The entities themselves
  std::unordered_map<std::string, std::string> contents = rm->readResources(files);
  int index = 0;
  for (const std::string &file : files) {
    auto found = contents.find(file);
    if (found == contents.end())
      continue;
    AiFile ai(found->second);

    CatalogItem item;
    item.file = file;
    item.registryIndex = index++;
    item.registrySection = fileSection[file];
    item.type = ai.get("global", "type");
    for (const std::string &m : ai.lines["member"])
      item.members.insert(m);
    if (item.members.empty())
      continue;

    // Animals and staff have subtypes (m/f): the buy panel shows the male
    // (its Male button starts selected), else the default subtype
    std::string sub = Utils::string_to_lower(ai.get("global", "defaultsubtype"));
    if (sub.empty())
      sub = fileSubtype[file];
    std::vector<std::string> prefixes;
    if (!ai.get("m/icon", "icon").empty())
      prefixes.push_back("m/");
    if (!sub.empty())
      prefixes.push_back(sub + "/");
    prefixes.push_back("");
    auto value = [&](const std::string &section, const std::string &key) {
      for (const std::string &p : prefixes) {
        std::string v = ai.get(p + section, key);
        if (!v.empty())
          return v;
      }
      return std::string();
    };
    item.icon = value("icon", "icon");
    // Staff show their female icon (measured: every staff icon in the
    // original's Hire Staff panel is the f/ one); animals their male
    if (file.rfind("staff/", 0) == 0 && !ai.get("f/icon", "icon").empty())
      item.icon = ai.get("f/icon", "icon");
    std::vector<std::string> iconPrefixes = prefixes;
    if (file.rfind("staff/", 0) == 0)
      iconPrefixes.insert(iconPrefixes.begin(), "f/");
    for (const std::string &p : iconPrefixes) {
      auto section = ai.entries.find(p + "icon");
      if (section == ai.entries.end())
        continue;
      for (const auto &kv : section->second)
        if (kv.first == "icon")
          item.icons.push_back(kv.second);
      if (!item.icons.empty())
        break;
    }
    if (item.icons.empty())
      item.icons.push_back(item.icon);
    // Items whose icon art isn't in the game data are still listed, with
    // an empty cell (as the original lists the test entity "First
    // user-created entity" in Scenery)
    auto integer = [&](const std::string &key) {
      return std::atoi(value("characteristics/integers", key).c_str());
    };
    item.nameId = integer("cnameid");
    item.cost = integer("cpurchasecost");
    item.habitatId = integer("chabitat");
    item.locationId = integer("clocation");
    item.family = integer("cfamily");
    item.genus = integer("cgenus");
    std::string work = value("characteristics/integers", "cworkcheck");
    item.workCheck = work.empty() ? -1 : std::atoi(work.c_str());
    item.dutiesTextId = integer("cdutiestextid");
    item.expansion = item.members.count("dinosaur") ? 1
                     : item.members.count("aqua")   ? 2
                                                    : 0;
    item.height = integer("cheight");
    item.showFence = integer("cisshowfence") != 0;
    item.capacity = integer("ccapacity");
    item.prefIcon = value("characteristics/strings", "cpreficon");
    item.name = item.nameId ? rm->getString(item.nameId) : "";
    auto unlock = unlockMonth.find(item.nameId);
    item.unlockMonth = unlock != unlockMonth.end() ? unlock->second
                       : downloaded.count(file) ? 0
                                                : -1;
    this->items.push_back(item);
  }
  SDL_Log("[ItemCatalog] %zu items from %zu registry files, %zu unlock lists "
          "in %u ms",
          this->items.size(), configs.size(), unlocks.size(),
          SDL_GetTicks() - start);

  // ZT_DUMP_CATALOG=1: every buy tab's list, to compare with the original
  if (std::getenv("ZT_DUMP_CATALOG")) {
    for (const char *c : {"animals", "shelters", "toys", "showtoys",
                          "structures", "scenery", "fence", "paths", "foliage",
                          "rocks", "staff"}) {
      std::string line;
      for (const CatalogItem *i : this->inCategory(c))
        line += " " + i->file;
      SDL_Log("[CATALOG] %s:%s", c, line.c_str());
    }
  }
}

const CatalogItem *ItemCatalog::findName(int nameId) const {
  for (const CatalogItem &item : this->items)
    if (item.nameId == nameId)
      return &item;
  return nullptr;
}

std::vector<const CatalogItem *>
ItemCatalog::inCategory(const std::string &category, bool availableOnly) const {
  std::string c = Utils::string_to_lower(category);
  std::vector<const CatalogItem *> list;
  for (const CatalogItem &item : this->items)
    if (item.members.count(c) &&
        (!availableOnly || (item.unlockMonth >= 0 && item.unlockMonth <= this->month)))
      list.push_back(&item);

  // Registry order, then the category's own order
  std::stable_sort(list.begin(), list.end(),
                   [](const CatalogItem *a, const CatalogItem *b) {
                     return a->registryIndex < b->registryIndex;
                   });
  if (c == "staff") {
    auto work = [](const CatalogItem *i) {
      return i->workCheck < 0 ? 1 << 30 : i->workCheck;
    };
    std::stable_sort(list.begin(), list.end(),
                     [&](const CatalogItem *a, const CatalogItem *b) {
                       return work(a) < work(b);
                     });
  } else if (c == "toys") {
    // Toys by price alone (measured: the Sunken Log, the one toy with a
    // habitat, sits between the $600 Lion Rock and the $700 Jungle Gym)
    std::stable_sort(list.begin(), list.end(),
                     [](const CatalogItem *a, const CatalogItem *b) {
                       return a->cost < b->cost;
                     });
  } else if (c == "fence") {
    // Habitat fences (tank walls, dinosaur and chain-link fences ...) first,
    // tallest then cheapest; the zoo's own fences after, as listed
    // Things listed with the fences that aren't fences (the Tank Filter, a
    // building) come first (measured in June)
    auto key = [](const CatalogItem *i) {
      bool fence = i->file.rfind("fences/", 0) == 0;
      bool habitat = i->members.count("habitatfences") > 0;
      return std::make_tuple(fence ? 1 : 0, habitat ? 0 : 1, habitat ? -i->height : 0,
                             habitat ? i->cost : 0);
    };
    std::stable_sort(list.begin(), list.end(),
                     [&](const CatalogItem *a, const CatalogItem *b) {
                       return key(a) < key(b);
                     });
  } else {
    std::stable_sort(list.begin(), list.end(),
                     [](const CatalogItem *a, const CatalogItem *b) {
                       return std::tie(a->habitatId, a->locationId, a->family,
                                       a->genus, a->cost) <
                              std::tie(b->habitatId, b->locationId, b->family,
                                       b->genus, b->cost);
                     });
  }
  return list;
}

const CatalogItem *ItemCatalog::find(const std::string &file) const {
  for (const CatalogItem &item : this->items)
    if (item.file == file)
      return &item;
  return nullptr;
}
