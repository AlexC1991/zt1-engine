#include "ScenarioManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <sstream>
#include <vector>
#include <set>

// --- ENCODING FIX: Convert Windows-1252 to UTF-8 ---
static std::string cp1252_to_utf8(const std::string& str) {
    std::string out;
    for (unsigned char c : str) {
        if (c < 128) {
            out += c;
        } else {
            switch(c) {
                case 0x91: out += "\xE2\x80\x98"; break; // Left Single Quote
                case 0x92: out += "\xE2\x80\x99"; break; // Right Single Quote
                case 0x93: out += "\xE2\x80\x9C"; break; // Left Double Quote
                case 0x94: out += "\xE2\x80\x9D"; break; // Right Double Quote
                case 0x96: out += "\xE2\x80\x93"; break; // En Dash
                case 0x97: out += "\xE2\x80\x94"; break; // Em Dash
                case 0xA0: out += " "; break;            // NBSP -> Space
                default:
                    out += (char)(0xC0 | (c >> 6));
                    out += (char)(0x80 | (c & 0x3F));
                    break;
            }
        }
    }
    return out;
}

static std::string trim(const std::string& str) {
    if (str.empty()) return "";
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first) + 1);
}

// FIX: Sanitize that PRESERVES blank lines (Double Newlines)
static std::string loadRawText(ResourceManager* rm, const std::string& path) {
    int size = 0;
    void* content = rm->getFileContent(path, &size);
    if (!content || size <= 0) return "";
    
    std::string text((char*)content, size);
    free(content);
    
    // 1. Normalize Line Endings (\r\n -> \n)
    std::string clean;
    for(char c : text) {
        if(c == '\r') continue; // Skip CR, keep LF
        clean += c;
    }
    
    // 2. Fix Encoding (Boxes)
    return cp1252_to_utf8(clean);
}

// --- STRING FORMATTER ---
static std::string formatGoalString(const std::string& raw, IniReader* ini, const std::string& section, ResourceManager* rm) {
    std::string result = raw;
    
    int val = ini->getInt(section, "value", 0);
    int arga = ini->getInt(section, "arga", 0);
    if (arga == 0) arga = ini->getInt(section, "targa", 0); 
    
    // Replace %d (Numbers)
    size_t pos = result.find("%d");
    if (pos != std::string::npos) result.replace(pos, 2, std::to_string(val));
    
    pos = result.find("%d");
    if (pos != std::string::npos) result.replace(pos, 2, std::to_string(arga));

    // Replace %s OR %r (Strings/Resources)
    size_t posS = result.find("%s");
    size_t posR = result.find("%r"); 
    size_t targetPos = std::string::npos;
    
    if (posS != std::string::npos) targetPos = posS;
    if (posR != std::string::npos) {
        if (targetPos == std::string::npos || posR < targetPos) targetPos = posR;
    }

    if (targetPos != std::string::npos) {
        if (arga > 0) {
            std::string name = rm->getString(arga);
            name = cp1252_to_utf8(name); 
            if (name.empty()) name = std::to_string(arga); 
            result.replace(targetPos, 2, name);
        }
    }
    return result;
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
        
        std::string scenarioPath = reader->get(section, "scenario");
        if (scenarioPath.empty()) continue;
        
        ScenarioInfo info;
        info.id = section;
        info.scenarioPath = scenarioPath;
        
        info.nameId = reader->getUnsignedInt(section, "name", 0);
        if (info.nameId > 0) {
            info.name = cp1252_to_utf8(resource_manager->getString(info.nameId));
        }
        if (info.name.empty()) info.name = "Scenario: " + section;
        
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
        if (txtPath.size() > 4) txtPath = txtPath.substr(0, txtPath.size() - 4) + ".txt";
        
        // Load map name
        int size=0;
        void* data = resource_manager->getFileContent(txtPath, &size);
        if(data) {
            std::string raw((char*)data, size);
            free(data);
            map.name = cp1252_to_utf8(raw);
            // Clean up newlines for the list name
            map.name.erase(std::remove(map.name.begin(), map.name.end(), '\r'), map.name.end());
            map.name.erase(std::remove(map.name.begin(), map.name.end(), '\n'), map.name.end());
        }
        if(map.name.empty()) map.name = "Map: " + map.path;
        
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

// FIX: Just return the sanitized string. Don't wrap it here.
// Let UiText.cpp handle the wrapping dynamically.
std::string ScenarioManager::loadScenarioDescription(const std::string& scenarioPath) {
    size_t lastSlash = scenarioPath.find_last_of('/');
    if (lastSlash == std::string::npos) return "No description available.";
    std::string folder = scenarioPath.substr(0, lastSlash + 1);
    
    // Priority: start.txt (Tutorials) > p01.txt > desc.txt
    std::vector<std::string> tryFiles = {
        folder + "start.txt", 
        folder + "p01.txt", 
        folder + "exstart.txt", 
        folder + "desc.txt"
    };
    
    for (const std::string& file : tryFiles) {
        // Use loadRawText to get UTF-8 string with newlines intact (including blank lines)
        std::string desc = loadRawText(this->resource_manager, file);
        if (!desc.empty()) return desc; 
    }
    return "No description available.";
}

std::vector<std::string> ScenarioManager::loadScenarioObjectives(const std::string& scenarioPath) {
    std::vector<std::string> objectives;
    std::set<std::string> uniqueObjectives; 

    IniReader* reader = resource_manager->getIniReader(scenarioPath);
    if (reader) {
        std::vector<std::string> goalKeys = reader->getList("goals", "goal");
        for (const std::string& sectionName : goalKeys) {
            
            bool isHidden = reader->getInt(sectionName, "hidden", 0) == 1;
            if (isHidden) continue;

            int textId = reader->getInt(sectionName, "text", 0);
            
            if (textId > 0) {
                std::string objText = resource_manager->getString(textId);
                objText = cp1252_to_utf8(objText); // Fix boxes
                
                if (!objText.empty()) {
                    objText = formatGoalString(objText, reader, sectionName, resource_manager);
                    
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
                    if (text.empty() || text.find("$Id") != std::string::npos || text.length() < 5) continue;
                    
                    if (isupper(text[0]) || text[0] == '"') {
                         if (text.front() == '"' && text.back() == '"') text = text.substr(1, text.length() - 2);
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