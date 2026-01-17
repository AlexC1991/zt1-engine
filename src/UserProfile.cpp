#include "UserProfile.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(dir) _mkdir(dir)
#else
#define MKDIR(dir) mkdir(dir, 0777)
#endif

UserProfile::UserProfile(const std::string& saveFile) : filePath(saveFile) {
    load();
}

UserProfile::~UserProfile() {
    save();
}

// Helper to auto-unlock beginners/tutorials if save is missing
std::string UserProfile::determineDifficulty(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("tutorial") != std::string::npos) return "Tutorial";
    if (lower.find("advanced") != std::string::npos) return "Advanced";
    if (lower.find("intermediate") != std::string::npos) return "Intermediate";
    return "Beginner";
}

void UserProfile::initializeDefaults(const std::vector<std::string>& allScenarioNames) {
    if (!scenarioStates.empty()) return; // Don't overwrite if we loaded data

    for (const auto& name : allScenarioNames) {
        std::string diff = determineDifficulty(name);

        if (diff == "Tutorial" || diff == "Beginner") {
            scenarioStates[name] = (int)ScenarioStatus::UNLOCKED;
        } else {
            scenarioStates[name] = (int)ScenarioStatus::LOCKED;
        }
    }
    // Auto-save these defaults immediately
    save();
}

ScenarioStatus UserProfile::getScenarioStatus(const std::string& name) {
    if (scenarioStates.find(name) != scenarioStates.end()) {
        return (ScenarioStatus)scenarioStates[name];
    }
    return ScenarioStatus::LOCKED;
}

void UserProfile::setScenarioStatus(const std::string& name, ScenarioStatus status) {
    scenarioStates[name] = (int)status;
    save();
}

bool UserProfile::isScenarioUnlocked(const std::string& name) {
    ScenarioStatus s = getScenarioStatus(name);
    return s == ScenarioStatus::UNLOCKED || s == ScenarioStatus::COMPLETED;
}

// --- JSON PARSER (Custom implementation) ---
void UserProfile::load() {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cout << "[PROFILE] No save found at " << filePath << ", starting fresh." << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Simple parser for "Name": Value
        size_t colonPos = line.find(':');
        size_t quoteStart = line.find('"');
        size_t quoteEnd = line.rfind('"');

        if (colonPos != std::string::npos && quoteStart != std::string::npos && quoteEnd > quoteStart) {
            std::string key = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            std::string valStr = line.substr(colonPos + 1);

            // Remove comma if present
            size_t comma = valStr.find(',');
            if (comma != std::string::npos) valStr = valStr.substr(0, comma);

            try {
                int val = std::stoi(valStr);
                scenarioStates[key] = val;
            } catch (...) {}
        }
    }
    std::cout << "[PROFILE] Loaded " << scenarioStates.size() << " scenario states." << std::endl;
}

void UserProfile::save() {
    // Attempt to make directory if needed (rudimentary check)
    // We assume the folder exists based on your setup_save.py script

    std::ofstream file(filePath);
    if (!file.is_open()) return;

    file << "{\n";
    file << "  \"scenarios\": {\n";

    auto it = scenarioStates.begin();
    while (it != scenarioStates.end()) {
        file << "    \"" << it->first << "\": " << it->second;
        auto next = it; ++next;
        if (next != scenarioStates.end()) file << ",";
        file << "\n";
        it++;
    }

    file << "  }\n";
    file << "}";
}