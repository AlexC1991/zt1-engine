#include "ScenarioManager.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

// --- ENCODING FIX: Convert Windows-1252 to UTF-8 ---
static std::string cp1252_to_utf8(const std::string &str) {
  std::string out;
  for (unsigned char c : str) {
    if (c < 128) {
      out += c;
    } else {
      switch (c) {
      case 0x91:
        out += "\xE2\x80\x98";
        break; // Left Single Quote
      case 0x92:
        out += "\xE2\x80\x99";
        break; // Right Single Quote
      case 0x93:
        out += "\xE2\x80\x9C";
        break; // Left Double Quote
      case 0x94:
        out += "\xE2\x80\x9D";
        break; // Right Double Quote
      case 0x96:
        out += "\xE2\x80\x93";
        break; // En Dash
      case 0x97:
        out += "\xE2\x80\x94";
        break; // Em Dash
      case 0xA0:
        out += " ";
        break; // NBSP -> Space
      default:
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
        break;
      }
    }
  }
  return out;
}

static std::string trim(const std::string &str) {
  if (str.empty())
    return "";
  size_t first = str.find_first_not_of(" \t\r\n");
  if (std::string::npos == first)
    return "";
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first) + 1);
}

// FIX: Sanitize that PRESERVES blank lines (Double Newlines)
static std::string loadRawText(ResourceManager *rm, const std::string &path) {
  int size = 0;
  void *content = rm->getFileContent(path, &size);
  if (!content || size <= 0)
    return "";

  std::string text((char *)content, size);
  free(content);

  // 1. Normalize Line Endings (\r\n -> \n)
  std::string clean;
  for (char c : text) {
    if (c == '\r')
      continue; // Skip CR, keep LF
    clean += c;
  }

  // 2. Fix Encoding (Boxes)
  return cp1252_to_utf8(clean);
}

// --- STRING FORMATTER ---
static std::string formatGoalString(const std::string &raw, IniReader *ini,
                                    const std::string &section,
                                    ResourceManager *rm) {
  std::string result = raw;

  int val = ini->getInt(section, "value", 0);
  int arga = ini->getInt(section, "arga", 0);
  if (arga == 0)
    arga = ini->getInt(section, "targa", 0);

  // Replace %d (Numbers)
  size_t pos = result.find("%d");
  if (pos != std::string::npos)
    result.replace(pos, 2, std::to_string(val));

  pos = result.find("%d");
  if (pos != std::string::npos)
    result.replace(pos, 2, std::to_string(arga));

  // Replace %s OR %r (Strings/Resources)
  size_t posS = result.find("%s");
  size_t posR = result.find("%r");
  size_t targetPos = std::string::npos;

  if (posS != std::string::npos)
    targetPos = posS;
  if (posR != std::string::npos) {
    if (targetPos == std::string::npos || posR < targetPos)
      targetPos = posR;
  }

  if (targetPos != std::string::npos) {
    if (arga > 0) {
      std::string name = rm->getString(arga);
      name = cp1252_to_utf8(name);
      if (name.empty())
        name = std::to_string(arga);
      result.replace(targetPos, 2, name);
    }
  }
  return result;
}

// --- CLASS IMPLEMENTATION ---

ScenarioManager::ScenarioManager(ResourceManager *resource_manager)
    : resource_manager(resource_manager) {
  SDL_Log("ScenarioManager initialized");

  // Initialize the scenario database with all scenarios
  database.initialize();
  SDL_Log("ScenarioDatabase initialized with %d entries", database.getCount());
}

ScenarioManager::~ScenarioManager() {
  scenarios.clear();
  freeform_maps.clear();
}

void ScenarioManager::loadScenarios() {
  // Base Game
  SDL_Log("Loading scenarios from scenario.cfg...");
  IniReader *reader = resource_manager->getIniReader("scenario.cfg");
  if (reader) {
    parseScenarioConfig(reader);
    delete reader;
  }

  // Expansion 1: Dinosaur Digs
  if (Utils::getExpansion() >= Expansion::DINOSAUR_DIGS) {
    SDL_Log("Loading scenarios from scenario1.cfg (Dinosaur Digs)...");
    // Try multiple possible config names
    const char *dd_configs[] = {"scenarj.cfg",   "scenario1.cfg",
                                "scenario2.cfg", "xscenario1.cfg",
                                "xscenario.cfg", nullptr};
    for (int i = 0; dd_configs[i] != nullptr; i++) {
      reader = resource_manager->getIniReader(dd_configs[i]);
      if (reader) {
        SDL_Log("Found Dinosaur Digs config: %s", dd_configs[i]);
        parseScenarioConfig(reader);
        delete reader;
        break;
      }
    }
  }

  // Expansion 2: Marine Mania
  if (Utils::getExpansion() >= Expansion::MARINE_MANIA) {
    SDL_Log("Loading scenarios from scenario2.cfg (Marine Mania)...");
    // Try multiple possible config names
    const char *mm_configs[] = {
        "scenark.cfg",    "scenario2.cfg",  "scenario3.cfg", "scenario6.cfg",
        "xscenario2.cfg", "xscenario3.cfg", nullptr};
    for (int i = 0; mm_configs[i] != nullptr; i++) {
      reader = resource_manager->getIniReader(mm_configs[i]);
      if (reader) {
        SDL_Log("Found Marine Mania config: %s", mm_configs[i]);
        parseScenarioConfig(reader);
        delete reader;
        break;
      }
    }
  }

  SDL_Log("Loaded %zu scenarios total", scenarios.size());
}

void ScenarioManager::loadFreeformMaps() {
  // Base Game
  SDL_Log("Loading freeform maps from freeform.cfg...");
  IniReader *reader = resource_manager->getIniReader("freeform.cfg");
  if (reader) {
    parseFreeformConfig(reader);
    delete reader;
  }

  // Expansion 1: Dinosaur Digs
  if (Utils::getExpansion() >= Expansion::DINOSAUR_DIGS) {
    SDL_Log("Loading freeform maps from freeform1.cfg...");
    const char *dd_ff[] = {"freefo01.cfg", "freeform1.cfg", "freeform2.cfg",
                           "xfreeform1.cfg", nullptr};
    for (int i = 0; dd_ff[i] != nullptr; i++) {
      reader = resource_manager->getIniReader(dd_ff[i]);
      if (reader) {
        SDL_Log("Found Dinosaur Digs freeform: %s", dd_ff[i]);
        parseFreeformConfig(reader);
        delete reader;
        break;
      }
    }
  }

  // Expansion 2: Marine Mania
  if (Utils::getExpansion() >= Expansion::MARINE_MANIA) {
    SDL_Log("Loading freeform maps from freeform2.cfg...");
    const char *mm_ff[] = {"freefo02.cfg",  "freeform2.cfg",  "freeform3.cfg",
                           "freeform6.cfg", "xfreeform2.cfg", "xfreeform3.cfg",
                           nullptr};
    for (int i = 0; mm_ff[i] != nullptr; i++) {
      reader = resource_manager->getIniReader(mm_ff[i]);
      if (reader) {
        SDL_Log("Found Marine Mania freeform: %s", mm_ff[i]);
        parseFreeformConfig(reader);
        delete reader;
        break;
      }
    }
  }

  SDL_Log("Loaded %zu freeform maps", freeform_maps.size());
}

void ScenarioManager::parseScenarioConfig(IniReader *reader) {
  for (const std::string &section : reader->getSections()) {
    if (section.empty())
      continue;

    std::string scenarioPath = reader->get(section, "scenario");
    if (scenarioPath.empty())
      continue;

    ScenarioInfo info;
    info.id = section;
    info.scenarioPath = scenarioPath;

    info.nameId = reader->getUnsignedInt(section, "name", 0);
    if (info.nameId > 0) {
      info.name = cp1252_to_utf8(resource_manager->getString(info.nameId));
    }
    if (info.name.empty())
      info.name = "Scenario: " + section;

    info.locks = reader->getList(section, "locks");

    // Sync with database
    if (database.getScenario(info.id)) {
      info.isLocked = (database.getState(info.id) == ScenarioState::LOCKED);
      info.stateMessage = database.getStateMessage(info.id);
      info.iconPath = database.getStateIcon(info.id);
    } else {
      // Default behavior for scenarios not in database (e.g. custom ones)
      // Check legacy lock logic if needed, or assume unlocked
      // For now, if it has 'locks' in .cfg, we might want to respect that,
      // but our database is the new source of truth.
      // Let's assume unlocked if not in database, unless it's a known
      // scenario.
      info.isLocked = false;
      info.stateMessage = "";
      info.iconPath = "ui/scenario/iconp/iconp"; // Default unlocked
    }
    info.unlocks = reader->getList(section, "unlocks");
    scenarios.push_back(info);
  }
}

void ScenarioManager::parseFreeformConfig(IniReader *reader) {
  // First, load raw freeform.cfg to parse comments for size categories
  std::map<std::string, std::string> sizeCategories;

  int cfgSize = 0;
  void *cfgData = resource_manager->getFileContent("freeform.cfg", &cfgSize);
  if (cfgData && cfgSize > 0) {
    std::string cfgContent((char *)cfgData, cfgSize);
    free(cfgData);

    // Parse comments to find size categories
    std::string currentSize = "Medium"; // Default
    std::stringstream ss(cfgContent);
    std::string line;

    while (std::getline(ss, line)) {
      // Remove CR if present
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      // Check for size category comments
      if (!line.empty() && line[0] == ';') {
        std::string comment = line.substr(1);
        // Convert to uppercase for comparison
        std::string upper;
        for (char c : comment)
          upper += toupper(c);

        if (upper.find("SMALL") != std::string::npos) {
          currentSize = "Small";
        } else if (upper.find("MEDIUM") != std::string::npos) {
          currentSize = "Medium";
        } else if (upper.find("LARGE") != std::string::npos) {
          currentSize = "Large";
        }
        continue;
      }

      // Check for freeform= lines
      if (line.find("freeform=") != std::string::npos ||
          line.find("Freeform=") != std::string::npos) {
        size_t eqPos = line.find('=');
        if (eqPos != std::string::npos) {
          std::string path = trim(line.substr(eqPos + 1));
          // Convert to lowercase for lookup
          std::string lowerPath;
          for (char c : path)
            lowerPath += tolower(c);
          sizeCategories[lowerPath] = currentSize;
        }
      }
    }
  }

  // Now parse the maps using IniReader
  std::vector<std::string> mapPaths = reader->getList("freeform", "freeform");
  for (const std::string &path : mapPaths) {
    if (path.empty())
      continue;
    FreeformMap map;
    map.path = path;

    // Look up size category
    std::string lowerPath;
    for (char c : path)
      lowerPath += tolower(c);
    auto it = sizeCategories.find(lowerPath);
    if (it != sizeCategories.end()) {
      map.size = it->second;
    } else {
      map.size = "Medium"; // Default
    }

    size_t lastSlash = path.find_last_of('/');
    size_t lastDot = path.find_last_of('.');
    if (lastSlash != std::string::npos && lastDot != std::string::npos) {
      map.name = path.substr(lastSlash + 1, lastDot - lastSlash - 1);
    } else {
      map.name = path;
    }

    std::string txtPath = path;
    if (txtPath.size() > 4)
      txtPath = txtPath.substr(0, txtPath.size() - 4) + ".txt";

    // Load map name
    int size = 0;
    void *data = resource_manager->getFileContent(txtPath, &size);
    if (data) {
      std::string raw((char *)data, size);
      free(data);
      map.name = cp1252_to_utf8(raw);
      // Clean up newlines for the list name
      map.name.erase(std::remove(map.name.begin(), map.name.end(), '\r'),
                     map.name.end());
      map.name.erase(std::remove(map.name.begin(), map.name.end(), '\n'),
                     map.name.end());
    }
    if (map.name.empty())
      map.name = "Map: " + map.path;

    map.startingCash = 50000;
    map.description = "";
    freeform_maps.push_back(map);
  }
}

const ScenarioInfo *ScenarioManager::getScenario(int index) const {
  if (index >= 0 && index < (int)scenarios.size())
    return &scenarios[index];
  return nullptr;
}

const FreeformMap *ScenarioManager::getFreeformMap(int index) const {
  if (index >= 0 && index < (int)freeform_maps.size())
    return &freeform_maps[index];
  return nullptr;
}

// FIX: Just return the sanitized string. Don't wrap it here.
// Let UiText.cpp handle the wrapping dynamically.
std::string
ScenarioManager::loadScenarioDescription(const std::string &scenarioPath) {
  size_t lastSlash = scenarioPath.find_last_of('/');
  if (lastSlash == std::string::npos)
    return "No description available.";
  std::string folder = scenarioPath.substr(0, lastSlash + 1);

  // Priority: start.txt (Tutorials) > p01.txt > desc.txt
  std::vector<std::string> tryFiles = {folder + "start.txt", folder + "p01.txt",
                                       folder + "exstart.txt",
                                       folder + "desc.txt"};

  for (const std::string &file : tryFiles) {
    // Use loadRawText to get UTF-8 string with newlines intact (including
    // blank lines)
    std::string desc = loadRawText(this->resource_manager, file);
    if (!desc.empty())
      return desc;
  }
  return "No description available.";
}

std::vector<std::string>
ScenarioManager::loadScenarioObjectives(const std::string &scenarioPath) {
  // Check if we have hardcoded objectives in the database first
  for (const auto &scenario : database.getAllScenarios()) {
    // Check if paths match (simplistic check, but effective given current data)
    // Normalize paths to be sure
    std::string dbPath = scenario.scenarioPath;
    std::string reqPath = scenarioPath;

    // Simple substring check since paths might be relative/absolute
    // e.g. "scenario/scn01/scn01.scn"
    if (reqPath.find(dbPath) != std::string::npos ||
        dbPath.find(reqPath) != std::string::npos) {

      if (!scenario.useFileObjectives && !scenario.objectives.empty()) {
        return scenario.objectives;
      }
    }
  }

  std::vector<std::string> objectives;
  std::set<std::string> uniqueObjectives;

  IniReader *reader = resource_manager->getIniReader(scenarioPath);
  if (reader) {
    // First, collect all goals that are children of chain goals
    // These are tutorial steps and shouldn't be shown as main objectives
    std::set<std::string> childGoals;
    std::vector<std::string> goalKeys = reader->getList("goals", "goal");

    for (const std::string &sectionName : goalKeys) {
      // Check if this is a chain goal (rtype=2) that has child goals
      int rtype = reader->getInt(sectionName, "rtype", 0);
      if (rtype == 2) {
        // Get all child goals from this chain
        std::vector<std::string> children =
            reader->getList(sectionName, "child");
        for (const std::string &child : children) {
          childGoals.insert(child);
        }
      }
    }

    for (const std::string &sectionName : goalKeys) {
      // Skip if this goal is a child of a chain (tutorial step)
      if (childGoals.count(sectionName) > 0)
        continue;

      // Skip chain goals themselves (they don't have text, just children)
      int rtype = reader->getInt(sectionName, "rtype", 0);
      if (rtype == 2)
        continue;

      bool isHidden = reader->getInt(sectionName, "hidden", 0) == 1;
      if (isHidden)
        continue;

      int textId = reader->getInt(sectionName, "text", 0);

      if (textId > 0) {
        std::string objText = resource_manager->getString(textId);
        objText = cp1252_to_utf8(objText); // Fix boxes

        if (!objText.empty()) {
          objText =
              formatGoalString(objText, reader, sectionName, resource_manager);

          if (uniqueObjectives.find(objText) == uniqueObjectives.end()) {
            objectives.push_back(" - " + objText);
            uniqueObjectives.insert(objText);
          }
        }
      }
    }
    delete reader;
  }

  // Fallback: Comments (Tutorial 1)
  if (objectives.empty()) {
    std::string scnContent = loadRawText(this->resource_manager, scenarioPath);
    if (!scnContent.empty()) {
      std::stringstream ss(scnContent);
      std::string line;
      while (std::getline(ss, line)) {
        std::string clean = trim(line);
        if (!clean.empty() && clean[0] == ';') {
          std::string text = trim(clean.substr(1));
          if (text.empty() || text.find("$Id") != std::string::npos ||
              text.length() < 5)
            continue;

          if (isupper(text[0]) || text[0] == '"') {
            if (text.front() == '"' && text.back() == '"')
              text = text.substr(1, text.length() - 2);
            text = cp1252_to_utf8(text);

            if (uniqueObjectives.find(text) == uniqueObjectives.end()) {
              objectives.push_back(" - " + text);
              uniqueObjectives.insert(text);
            }
          }
        }
      }
    }
  }

  return objectives;
}

void ScenarioManager::syncWithDatabase() {
  SDL_Log("Syncing scenarios with database state...");

  for (auto &scenario : scenarios) {
    std::string lowerName = scenario.name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   ::tolower);

    // Determine expansion from name prefix
    bool isDinoDigs = (lowerName.find("dinosaur digs") != std::string::npos);
    bool isMarineMania = (lowerName.find("marine mania") != std::string::npos);

    // Determine difficulty from name
    bool isTutorial = (lowerName.find("tutorial") != std::string::npos);
    bool isBeginner = (lowerName.find("beginner") != std::string::npos);
    bool isIntermediate = (lowerName.find("intermediate") != std::string::npos);
    bool isAdvanced = (lowerName.find("advanced") != std::string::npos &&
                       lowerName.find("very advanced") == std::string::npos);
    bool isVeryAdvanced =
        (lowerName.find("very advanced") != std::string::npos);

    // Default: Tutorials and Beginners are unlocked, others locked
    if (isTutorial || isBeginner) {
      scenario.isLocked = false;
      scenario.stateMessage = "";
    } else {
      scenario.isLocked = true;

      // Set lock message based on expansion and difficulty
      if (isDinoDigs) {
        if (isIntermediate) {
          scenario.stateMessage =
              "You must complete all of the Dinosaur Digs beginner scenarios "
              "to unlock this scenario.";
        } else if (isAdvanced) {
          scenario.stateMessage =
              "You must complete all of the Dinosaur Digs intermediate "
              "scenarios to unlock this scenario.";
        } else {
          scenario.stateMessage =
              "You must complete all of the Dinosaur Digs advanced scenarios "
              "to unlock this scenario.";
        }
      } else if (isMarineMania) {
        // Special case: Super Zoo is always unlocked
        if (lowerName.find("super zoo") != std::string::npos) {
          scenario.isLocked = false;
          scenario.stateMessage = "";
        } else if (isIntermediate) {
          scenario.stateMessage =
              "You must complete all of the Marine Mania beginner scenarios "
              "to unlock this scenario.";
        } else if (isAdvanced) {
          scenario.stateMessage =
              "You must complete all of the Marine Mania intermediate "
              "scenarios to unlock this scenario.";
        } else {
          scenario.stateMessage =
              "You must complete all of the Marine Mania advanced scenarios "
              "to unlock this scenario.";
        }
      } else {
        // Base game
        if (isIntermediate) {
          scenario.stateMessage =
              "You must complete all of the Zoo Tycoon beginner scenarios to "
              "unlock this scenario.";
        } else if (isAdvanced) {
          scenario.stateMessage =
              "You must complete all of the Zoo Tycoon intermediate scenarios "
              "to unlock this scenario.";
        } else {
          scenario.stateMessage =
              "You must complete all of the Zoo Tycoon advanced scenarios to "
              "unlock this scenario.";
        }
      }
    }

    // Set icon based on lock state
    // iconf = locked (gold padlock), iconp = unlocked (playable)
    if (scenario.isLocked) {
      scenario.iconPath = "ui/scenario/iconf/iconf";
    } else {
      scenario.iconPath = "ui/scenario/iconp/iconp";
    }
  }

  SDL_Log("Synced %d scenarios with database", (int)scenarios.size());
}

static int getDifficultyOrder(const std::string &name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower.find("tutorial") != std::string::npos)
    return 0;
  if (lower.find("beginner") != std::string::npos)
    return 1;
  if (lower.find("intermediate") != std::string::npos)
    return 2;
  if (lower.find("very advanced") != std::string::npos)
    return 4;
  if (lower.find("advanced") != std::string::npos)
    return 3;
  return 5; // Unknown
}

void ScenarioManager::sortByDifficulty() {
  std::sort(scenarios.begin(), scenarios.end(),
            [](const ScenarioInfo &a, const ScenarioInfo &b) {
              int orderA = getDifficultyOrder(a.name);
              int orderB = getDifficultyOrder(b.name);
              if (orderA != orderB)
                return orderA < orderB;
              return a.name < b.name; // Alphabetical within same difficulty
            });

  SDL_Log("Sorted scenarios by difficulty");
}