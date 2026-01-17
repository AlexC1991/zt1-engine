#include "ScenarioManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <sstream>
#include <vector>
#include <cstdio> // Required for snprintf

// --- HELPER FUNCTIONS ---

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first) + 1);
}

// Helper to load RAW text (preserves newlines for parsing)
static std::string loadRawText(ResourceManager* rm, const std::string& path) {
    int size = 0;
    void* content = rm->getFileContent(path, &size);
    if (!content || size <= 0) return "";
    
    std::string text((char*)content, size);
    free(content);
    
    // Clean up Windows CR characters
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
    for (const std::string& section : reader->getSections()) {
        if (section.empty()) continue;
        
        // Use "scenario" key as per your original file
        std::string scenarioPath = reader->get(section, "scenario");
        if (scenarioPath.empty()) continue;
        
        ScenarioInfo info;
        info.id = section;
        info.scenarioPath = scenarioPath;
        
        // Get name ID and resolve it
        info.nameId = reader->getUnsignedInt(section, "name", 0);
        if (info.nameId > 0) {
            info.name = resource_manager->getString(info.nameId);
        }
        if (info.name.empty()) {
            info.name = "Scenario: " + section;
        }
        
        // Get locks/unlocks
        info.locks = reader->getList(section, "locks");
        info.unlocks = reader->getList(section, "unlocks");
        info.isLocked = false;
        
        scenarios.push_back(info);
    }
}

void ScenarioManager::parseFreeformConfig(IniReader* reader) {
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

// --- Helper to fill %d and %s templates ---
static std::string formatObjectiveText(ResourceManager* rm, const std::string& fmt, int val, int arga) {
    char buffer[512];
    
    // Check what placeholders are present
    bool hasString = fmt.find("%s") != std::string::npos;
    int intCount = 0;
    size_t pos = 0;
    while ((pos = fmt.find("%d", pos)) != std::string::npos) { intCount++; pos += 2; }

    if (hasString && intCount == 1) {
        // Example: "Adopt 1 %s" -> val, string(arga)
        std::string s = rm->getString(arga);
        snprintf(buffer, sizeof(buffer), fmt.c_str(), val, s.c_str());
    } 
    else if (hasString) {
        // Example: "%s" -> string(arga)
        std::string s = rm->getString(arga);
        if (s.empty()) s = rm->getString(val);
        snprintf(buffer, sizeof(buffer), fmt.c_str(), s.c_str());
    }
    else if (intCount == 2) {
        // Example: "Rating of %d for %d exhibits" -> usually (arga, val) or (val, arga)
        // ZT1 heuristic: arga is often the quality/ID, value is quantity
        snprintf(buffer, sizeof(buffer), fmt.c_str(), arga, val);
    } 
    else if (intCount == 1) {
        // Example: "Have %d guests" -> val
        snprintf(buffer, sizeof(buffer), fmt.c_str(), val);
    } 
    else {
        // No formatting needed
        return fmt;
    }
    return std::string(buffer);
}

std::vector<std::string> ScenarioManager::loadScenarioObjectives(const std::string& scenarioPath) {
    std::vector<std::string> objectives;

    // STRATEGY 1: Parse [goals] section for 'text=' IDs (Standard Scenarios)
    IniReader* reader = resource_manager->getIniReader(scenarioPath);
    if (reader) {
        std::vector<std::string> goalKeys = reader->getList("goals", "goal");
        for (const std::string& sectionName : goalKeys) {
            
            // 1. Get the raw text ID
            int textId = reader->getInt(sectionName, "text", 0);
            
            if (textId > 0) {
                std::string objText = resource_manager->getString(textId);
                
                if (!objText.empty()) {
                    // 2. Get the parameters for formatting
                    int val = reader->getInt(sectionName, "value", 0);
                    int arga = reader->getInt(sectionName, "arga", 0);
                    
                    // 3. Format the string (replace %d with numbers)
                    std::string finalStr = formatObjectiveText(resource_manager, objText, val, arga);
                    objectives.push_back(finalStr);
                }
            }
        }
        delete reader;
    }

    // STRATEGY 2: Fallback to scanning comments (Tutorial 1 Style)
    if (objectives.empty()) {
        std::string scnContent = loadRawText(this->resource_manager, scenarioPath);
        if (!scnContent.empty()) {
            std::stringstream ss(scnContent);
            std::string line;
            
            while (std::getline(ss, line)) {
                std::string clean = trim(line);
                if (!clean.empty() && clean[0] == ';') {
                    std::string text = trim(clean.substr(1));
                    if (text.empty() || text.find("$Id") != std::string::npos || text.length() < 5) continue; 
                    
                    if (isupper(text[0]) || text[0] == '"') {
                         if (text.front() == '"' && text.back() == '"') {
                            text = text.substr(1, text.length() - 2);
                        }
                        objectives.push_back(text);
                    }
                }
            }
        }
    }

    return objectives;
}