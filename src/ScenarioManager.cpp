#include "ScenarioManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <sstream> // Added for parsing
#include <vector>

// --- HELPER FUNCTIONS ---

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first) + 1);
}

static bool startsWithIgnoreCase(const std::string& str, const std::string& prefix) {
    if (str.size() < prefix.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), str.begin(), 
        [](char a, char b) { return tolower(a) == tolower(b); });
}

// Helper to load text WITHOUT wrapping (for parsing data like goals)
static std::string loadRawText(ResourceManager* rm, const std::string& path) {
    int size = 0;
    void* content = rm->getFileContent(path, &size);
    if (!content || size <= 0) return "";
    
    std::string text((char*)content, size);
    free(content);
    
    // Clean up CR
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

// --- CLASS IMPLEMENTATION ---

ScenarioManager::ScenarioManager(ResourceManager* resource_manager) 
    : resource_manager(resource_manager) {
    SDL_Log("ScenarioManager initialized");
}

ScenarioManager::~ScenarioManager() {
    scenarios.clear();
    freeform_maps.clear();
}

void ScenarioManager::loadScenarios() {
    SDL_Log("Loading scenarios from scenario.cfg...");
    IniReader* reader = resource_manager->getIniReader("scenario.cfg");
    if (!reader) {
        SDL_Log("ERROR: Could not load scenario.cfg");
        return;
    }
    parseScenarioConfig(reader);
    delete reader;
    SDL_Log("Loaded %zu scenarios", scenarios.size());
}

void ScenarioManager::loadFreeformMaps() {
    SDL_Log("Loading freeform maps from freeform.cfg...");
    IniReader* reader = resource_manager->getIniReader("freeform.cfg");
    if (!reader) {
        SDL_Log("ERROR: Could not load freeform.cfg");
        return;
    }
    parseFreeformConfig(reader);
    delete reader;
    SDL_Log("Loaded %zu freeform maps", freeform_maps.size());
}

void ScenarioManager::parseScenarioConfig(IniReader* reader) {
    // RESTORED ORIGINAL LOGIC
    for (const std::string& section : reader->getSections()) {
        if (section.empty()) continue;
        
        std::string scenarioPath = reader->get(section, "scenario");
        if (scenarioPath.empty()) continue;
        
        ScenarioInfo info;
        info.id = section;
        info.scenarioPath = scenarioPath;
        
        info.nameId = reader->getUnsignedInt(section, "name", 0);
        if (info.nameId > 0) {
            info.name = resource_manager->getString(info.nameId);
        }
        if (info.name.empty()) {
            info.name = "Scenario: " + section;
        }
        
        info.locks = reader->getList(section, "locks");
        info.unlocks = reader->getList(section, "unlocks");
        info.isLocked = false;
        
        scenarios.push_back(info);
    }
}

void ScenarioManager::parseFreeformConfig(IniReader* reader) {
    // RESTORED ORIGINAL LOGIC
    std::vector<std::string> mapPaths = reader->getList("freeform", "freeform");
    
    for (const std::string& path : mapPaths) {
        if (path.empty()) continue;
        
        FreeformMap map;
        map.path = path;
        
        size_t lastSlash = path.find_last_of('/');
        size_t lastDot = path.find_last_of('.');
        if (lastSlash != std::string::npos && lastDot != std::string::npos) {
            map.name = path.substr(lastSlash + 1, lastDot - lastSlash - 1);
        } else {
            map.name = path;
        }
        
        std::string txtPath = path;
        if (txtPath.size() > 4) {
            txtPath = txtPath.substr(0, txtPath.size() - 4) + ".txt";
        }
        std::string displayName = loadTextFile(txtPath);
        if (!displayName.empty()) {
            map.name = displayName;
        }
        
        map.startingCash = 50000;
        map.description = ""; 
        
        freeform_maps.push_back(map);
    }
}

const ScenarioInfo* ScenarioManager::getScenario(int index) const {
    if (index >= 0 && index < (int)scenarios.size()) return &scenarios[index];
    return nullptr;
}

const FreeformMap* ScenarioManager::getFreeformMap(int index) const {
    if (index >= 0 && index < (int)freeform_maps.size()) return &freeform_maps[index];
    return nullptr;
}

std::string ScenarioManager::wrapText(const std::string& text, int charsPerLine) {
    if (text.empty() || charsPerLine <= 0) return text;
    std::string result;
    std::string currentLine;
    std::string word;
    
    for (size_t i = 0; i <= text.length(); i++) {
        char c = (i < text.length()) ? text[i] : ' ';
        if (c == ' ' || c == '\n' || i == text.length()) {
            if (!word.empty()) {
                if (currentLine.empty()) {
                    currentLine = word;
                } else if ((int)(currentLine.length() + 1 + word.length()) <= charsPerLine) {
                    currentLine += " " + word;
                } else {
                    if (!result.empty()) result += "\n";
                    result += currentLine;
                    currentLine = word;
                }
                word.clear();
            }
            if (c == '\n') {
                if (!result.empty()) result += "\n";
                result += currentLine;
                currentLine.clear();
            }
        } else {
            word += c;
        }
    }
    if (!currentLine.empty()) {
        if (!result.empty()) result += "\n";
        result += currentLine;
    }
    return result;
}

std::string ScenarioManager::loadTextFile(const std::string& path) {
    int size = 0;
    void* content = resource_manager->getFileContent(path, &size);
    if (!content || size <= 0) return "";
    
    std::string text((char*)content, size);
    free(content);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    
    size_t start = text.find_first_not_of(" \t\n");
    size_t end = text.find_last_not_of(" \t\n");
    if (start != std::string::npos && end != std::string::npos) {
        text = text.substr(start, end - start + 1);
    }
    text = wrapText(text, 50);
    return text;
}

std::string ScenarioManager::loadScenarioDescription(const std::string& scenarioPath) {
    size_t lastSlash = scenarioPath.find_last_of('/');
    if (lastSlash == std::string::npos) return "No description available.";
    std::string folder = scenarioPath.substr(0, lastSlash + 1);
    
    std::vector<std::string> tryFiles = {
        folder + "start.txt",
        folder + "p01.txt",
        folder + "exstart.txt",
        folder + "desc.txt"
    };
    
    for (const std::string& file : tryFiles) {
        std::string desc = loadTextFile(file);
        if (!desc.empty()) {
            return desc;
        }
    }
    return "No description available.";
}

// UPDATED: Now actually parses goals!
std::vector<std::string> ScenarioManager::loadScenarioObjectives(const std::string& scenarioPath) {
    std::vector<std::string> objectives;
    
    size_t lastSlash = scenarioPath.find_last_of('/');
    std::string folder = (lastSlash == std::string::npos) ? "" : scenarioPath.substr(0, lastSlash + 1);
    
    std::vector<std::string> tryFiles = {
        folder + "goals.txt",
        folder + "start.txt",
        folder + "p01.txt"
    };

    bool foundAny = false;

    for (const std::string& file : tryFiles) {
        // Use loadRawText to avoid wrapping issues when parsing keys
        std::string content = loadRawText(this->resource_manager, file);
        if (content.empty()) continue;

        std::stringstream ss(content);
        std::string line;
        bool inGoalsSection = false;

        while (std::getline(ss, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';') continue;

            if (line.find("[Goals]") != std::string::npos) {
                inGoalsSection = true;
                continue;
            }
            if (line.find("[") == 0 && line.find("[Goals]") == std::string::npos) {
                inGoalsSection = false;
            }

            if (startsWithIgnoreCase(line, "goal=")) {
                objectives.push_back(line.substr(5));
                foundAny = true;
            }
            else if (inGoalsSection) {
                size_t eqPos = line.find('=');
                if (eqPos != std::string::npos) {
                    objectives.push_back(line.substr(eqPos + 1));
                    foundAny = true;
                }
            }
        }

        if (foundAny) break; 
    }

    return objectives;
}