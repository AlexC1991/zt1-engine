#ifndef USER_PROFILE_HPP
#define USER_PROFILE_HPP

#include <string>
#include <map>
#include <vector>

enum class ScenarioStatus {
    LOCKED = 0,
    UNLOCKED = 1,
    COMPLETED = 2
};

class UserProfile {
public:
    UserProfile(const std::string& saveFile);
    ~UserProfile();

    // Load/Save
    void load();
    void save();

    // Logic
    ScenarioStatus getScenarioStatus(const std::string& scenarioName);
    void setScenarioStatus(const std::string& scenarioName, ScenarioStatus status);
    bool isScenarioUnlocked(const std::string& scenarioName);

    // Setup defaults if file is missing
    void initializeDefaults(const std::vector<std::string>& allScenarioNames);

private:
    std::string filePath;
    std::map<std::string, int> scenarioStates;

    std::string determineDifficulty(const std::string& name);
};

#endif // USER_PROFILE_HPP