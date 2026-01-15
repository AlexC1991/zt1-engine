#include "ScenarioManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>

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
    
    // Load scenario.cfg from config.ztd
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
    
    // Load freeform.cfg from config.ztd
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
    // scenario.cfg format:
    // [aa]
    // name=16022
    // scenario=scenario/scn01a/scn01a.scn
    // unlocks=aa
    // locks=ba
    
    for (const std::string& section : reader->getSections()) {
        // Skip empty sections
        if (section.empty()) continue;
        
        // Get the scenario path
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
            // Fallback to section name
            info.name = "Scenario: " + section;
        }
        
        // Get locks/unlocks (these can be lists)
        info.locks = reader->getList(section, "locks");
        info.unlocks = reader->getList(section, "unlocks");
        
        // For now, mark as unlocked (proper unlock tracking would need save data)
        info.isLocked = false;
        
        scenarios.push_back(info);
        SDL_Log("  Found scenario: %s -> %s", info.name.c_str(), info.scenarioPath.c_str());
    }
}

void ScenarioManager::parseFreeformConfig(IniReader* reader) {
    // freeform.cfg format:
    // [freeform]
    // freeform=freeform/ff01.scn
    // freeform=freeform/deathmtn.scn
    // ...
    
    std::vector<std::string> mapPaths = reader->getList("freeform", "freeform");
    
    for (const std::string& path : mapPaths) {
        if (path.empty()) continue;
        
        FreeformMap map;
        map.path = path;
        
        // Extract name from path (e.g., "freeform/ff01.scn" -> "ff01")
        size_t lastSlash = path.find_last_of('/');
        size_t lastDot = path.find_last_of('.');
        if (lastSlash != std::string::npos && lastDot != std::string::npos) {
            map.name = path.substr(lastSlash + 1, lastDot - lastSlash - 1);
        } else {
            map.name = path;
        }
        
        // Try to load the map's .txt file for a better name
        // Format: freeform/ff01.txt contains the display name
        std::string txtPath = path;
        if (txtPath.size() > 4) {
            txtPath = txtPath.substr(0, txtPath.size() - 4) + ".txt";
        }
        std::string displayName = loadTextFile(txtPath);
        if (!displayName.empty()) {
            map.name = displayName;
        }
        
        // Default values
        map.startingCash = 50000;
        map.description = ""; // Could load from .scn file
        
        freeform_maps.push_back(map);
        SDL_Log("  Found freeform map: %s -> %s", map.name.c_str(), map.path.c_str());
    }
}

const ScenarioInfo* ScenarioManager::getScenario(int index) const {
    if (index >= 0 && index < (int)scenarios.size()) {
        return &scenarios[index];
    }
    return nullptr;
}

const FreeformMap* ScenarioManager::getFreeformMap(int index) const {
    if (index >= 0 && index < (int)freeform_maps.size()) {
        return &freeform_maps[index];
    }
    return nullptr;
}

// Simple word wrap - inserts newlines at word boundaries
std::string ScenarioManager::wrapText(const std::string& text, int charsPerLine) {
    if (text.empty() || charsPerLine <= 0) return text;
    
    std::string result;
    std::string currentLine;
    std::string word;
    
    for (size_t i = 0; i <= text.length(); i++) {
        char c = (i < text.length()) ? text[i] : ' ';
        
        if (c == ' ' || c == '\n' || i == text.length()) {
            // End of word
            if (!word.empty()) {
                if (currentLine.empty()) {
                    currentLine = word;
                } else if ((int)(currentLine.length() + 1 + word.length()) <= charsPerLine) {
                    currentLine += " " + word;
                } else {
                    // Line is full, start new line
                    if (!result.empty()) result += "\n";
                    result += currentLine;
                    currentLine = word;
                }
                word.clear();
            }
            
            // Handle explicit newlines
            if (c == '\n') {
                if (!result.empty()) result += "\n";
                result += currentLine;
                currentLine.clear();
            }
        } else {
            word += c;
        }
    }
    
    // Add last line
    if (!currentLine.empty()) {
        if (!result.empty()) result += "\n";
        result += currentLine;
    }
    
    return result;
}

std::string ScenarioManager::loadTextFile(const std::string& path) {
    int size = 0;
    void* content = resource_manager->getFileContent(path, &size);
    if (!content || size <= 0) {
        return "";
    }
    
    std::string text((char*)content, size);
    free(content);
    
    // Clean up the text (remove \r, trim whitespace)
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    
    // Trim leading/trailing whitespace
    size_t start = text.find_first_not_of(" \t\n");
    size_t end = text.find_last_not_of(" \t\n");
    if (start != std::string::npos && end != std::string::npos) {
        text = text.substr(start, end - start + 1);
    }
    
    // Word wrap to ~50 chars per line for description boxes
    text = wrapText(text, 50);
    
    return text;
}

std::string ScenarioManager::loadScenarioDescription(const std::string& scenarioPath) {
    // Scenario description is in files like:
    // scenario/scn05/start.txt (most common)
    // scenario/scn01/p01.txt (some tutorials)
    
    // Extract folder from path (e.g., "scenario/scn05/scn05.scn" -> "scenario/scn05/")
    size_t lastSlash = scenarioPath.find_last_of('/');
    if (lastSlash == std::string::npos) return "No description available.";
    
    std::string folder = scenarioPath.substr(0, lastSlash + 1);
    
    // Try different possible description files in order of likelihood
    std::vector<std::string> tryFiles = {
        folder + "start.txt",      // Most common
        folder + "p01.txt",        // Tutorial style
        folder + "exstart.txt",    // Example/expansion
        folder + "desc.txt",       // Generic
    };
    
    for (const std::string& file : tryFiles) {
        std::string desc = loadTextFile(file);
        if (!desc.empty()) {
            SDL_Log("Loaded description from: %s", file.c_str());
            return desc;
        }
    }
    
    return "No description available.";
}

std::vector<std::string> ScenarioManager::loadScenarioObjectives(const std::string& scenarioPath) {
    std::vector<std::string> objectives;
    
    // Load the .scn file and parse objectives
    // This is a simplified version - real implementation would parse the .scn format
    
    // For now, return placeholder
    objectives.push_back("Complete the scenario objectives");
    
    return objectives;
}
