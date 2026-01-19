#ifndef SCENARIO_MANAGER_HPP
#define SCENARIO_MANAGER_HPP

#include <map>
#include <string>
#include <vector>

#include "IniReader.hpp"
#include "ResourceManager.hpp"

struct ScenarioInfo {
  std::string id;                   // Section name (e.g., "aa", "ab")
  uint32_t nameId = 0;              // String ID for name (e.g., 16022)
  std::string name;                 // Resolved name
  std::string scenarioPath;         // Path to .scn file
  std::string description;          // Scenario description
  std::vector<std::string> locks;   // Required unlocks
  std::vector<std::string> unlocks; // What this unlocks
  bool isLocked = false;            // Whether scenario is locked

  // Database-synced fields
  std::string iconPath;     // Path to icon (locked/unlocked/completed)
  std::string stateMessage; // Locked/Unlocked/Completed message
};

struct FreeformMap {
  std::string path;         // Path to .scn file (e.g., "freeform/ff01.scn")
  std::string name;         // Map name
  std::string description;  // Map description
  std::string size;         // Size category: "Small", "Medium", "Large"
  int startingCash = 50000; // Default starting cash
};

#include "ScenarioDatabase.hpp"

// ... (ScenarioInfo struct)

class ScenarioManager {
public:
  ScenarioManager(ResourceManager *resource_manager);
  ~ScenarioManager();

  // Load configs
  void loadScenarios();
  void loadFreeformMaps();

  // Getters
  const std::vector<ScenarioInfo> &getScenarios() const { return scenarios; }
  const std::vector<FreeformMap> &getFreeformMaps() const {
    return freeform_maps;
  }
  ScenarioDatabase *getDatabase() { return &database; }

  // Get specific scenario/map
  const ScenarioInfo *getScenario(int index) const;
  const FreeformMap *getFreeformMap(int index) const;

  // Load scenario details (description, objectives)
  std::string loadScenarioDescription(const std::string &scenarioPath);
  std::vector<std::string>
  loadScenarioObjectives(const std::string &scenarioPath);

  // Sync scenarios with database state (call after loadState)
  void syncWithDatabase();

  // Sort scenarios by difficulty
  void sortByDifficulty();

private:
  ResourceManager *resource_manager;
  ScenarioDatabase database;
  std::vector<ScenarioInfo> scenarios;
  std::vector<FreeformMap> freeform_maps;

  void parseScenarioConfig(IniReader *reader);
  void parseFreeformConfig(IniReader *reader);
  std::string loadTextFile(const std::string &path);
  std::string wrapText(const std::string &text, int charsPerLine);
};

#endif // SCENARIO_MANAGER_HPP
