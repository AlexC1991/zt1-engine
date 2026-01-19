#ifndef SCENARIO_DATABASE_HPP
#define SCENARIO_DATABASE_HPP

#include <string>
#include <unordered_map>
#include <vector>

// Scenario state enum
enum class ScenarioState { LOCKED, UNLOCKED, COMPLETED };

// Expansion type for categorizing scenarios
enum class ScenarioExpansion { BASE_GAME, DINOSAUR_DIGS, MARINE_MANIA };

// Entry for each scenario in the database
struct ScenarioEntry {
  std::string id;           // Unique identifier (e.g., "s001", "d001", "m001")
  std::string name;         // Display name
  std::string scenarioPath; // Path to .scn file
  ScenarioExpansion expansion; // Which expansion this belongs to
  std::string
      difficulty; // "Beginner", "Intermediate", "Advanced", "Very Advanced"

  // State
  ScenarioState state = ScenarioState::LOCKED;

  // Messages
  std::string lockedMessage;    // Message to display when locked
  std::string unlockedMessage;  // Message to display when unlocked
  std::string completedMessage; // Message to display when completed

  // Icons
  std::string lockedIconPath;    // Custom locked icon path
  std::string unlockedIconPath;  // Unlocked icon path
  std::string completedIconPath; // Completed icon path

  // Preview image
  std::string previewImagePath;   // Preview image for scenario selection
  std::string previewPalettePath; // Palette for ZT1 preview images

  // Objectives
  std::vector<std::string> objectives; // List of objectives to display
  bool useFileObjectives =
      false; // If true, load objectives from .scn file instead
};

class ScenarioDatabase {
public:
  ScenarioDatabase();
  ~ScenarioDatabase();

  // Initialize the database with all scenarios
  void initialize();

  // Get scenario by ID
  const ScenarioEntry *getScenario(const std::string &id) const;

  // Get scenario by index
  const ScenarioEntry *getScenarioByIndex(int index) const;

  // Get all scenarios
  const std::vector<ScenarioEntry> &getAllScenarios() const {
    return scenarios;
  }

  // Get scenarios by expansion
  std::vector<const ScenarioEntry *>
  getScenariosByExpansion(ScenarioExpansion expansion) const;

  // Get scenarios by difficulty
  std::vector<const ScenarioEntry *>
  getScenariosByDifficulty(const std::string &difficulty) const;

  // State management
  ScenarioState getState(const std::string &id) const;
  void setState(const std::string &id, ScenarioState state);

  // Check if scenario is unlocked (UNLOCKED or COMPLETED)
  bool isUnlocked(const std::string &id) const;

  // Check if scenario is completed
  bool isCompleted(const std::string &id) const;

  // Get the appropriate message for current state
  std::string getStateMessage(const std::string &id) const;

  // Get the appropriate icon for current state
  std::string getStateIcon(const std::string &id) const;

  // Get objectives for a scenario (returns empty if useFileObjectives is true)
  const std::vector<std::string> &getObjectives(const std::string &id) const;

  // Unlock a scenario
  void unlock(const std::string &id);

  // Mark a scenario as completed (also unlocks next scenarios)
  void complete(const std::string &id);

  // Get count
  int getCount() const { return (int)scenarios.size(); }

  // Save/Load state to file
  void saveState(const std::string &filename);
  bool loadState(const std::string &filename);

private:
  std::vector<ScenarioEntry> scenarios;
  std::unordered_map<std::string, int> idToIndex; // Fast lookup by ID

  // Initialize base game scenarios
  void initBaseGameScenarios();

  // Initialize Dinosaur Digs scenarios
  void initDinosaurDigsScenarios();

  // Initialize Marine Mania scenarios
  void initMarineManiaSenarios();

  // Get default lock message based on difficulty and expansion
  std::string getDefaultLockMessage(ScenarioExpansion expansion,
                                    const std::string &difficulty);

  // Add a scenario entry
  void addScenario(const ScenarioEntry &entry);
};

#endif // SCENARIO_DATABASE_HPP
