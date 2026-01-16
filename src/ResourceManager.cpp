#include "ResourceManager.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <SDL2/SDL.h>
#include "ZtdFile.hpp"
#include "Utils.hpp"
#include "FontManager.hpp"
#include "Expansion.hpp"

ResourceManager::ResourceManager(Config * config) : config(config) {}
ResourceManager::~ResourceManager() {
  Mix_HaltMusic();
  if (this->intro_music != nullptr){ Mix_FreeMusic(this->intro_music); }
}

// Helper to normalize paths: lowercase + forward slashes
std::string normalizePath(const std::string& input) {
    std::string path = Utils::string_to_lower(input);
    std::replace(path.begin(), path.end(), '\\', '/');
    
    // Remove leading/trailing slashes
    while (!path.empty() && path[0] == '/') path = path.substr(1);
    while (!path.empty() && path.back() == '/') path.pop_back();
    
    return path;
}

// Helper to fix the "Double Name" bug (e.g. "textbck/textbck" -> "textbck")
std::string fixDoubleName(const std::string& input) {
    std::string path = normalizePath(input);
    
    size_t last = path.find_last_of('/');
    if (last != std::string::npos) {
        std::string file = path.substr(last + 1);
        std::string parent = path.substr(0, last);
        
        size_t parent_last = parent.find_last_of('/');
        std::string parent_name = (parent_last != std::string::npos) ? 
                                   parent.substr(parent_last + 1) : parent;
        
        if (parent_name == file) {
            return parent;
        }
    }
    return path;
}

// Check if a path is a directory (ends with / in the resource map)
bool ResourceManager::isDirectory(const std::string& path) {
    std::string with_slash = path + "/";
    return resource_map.count(with_slash) > 0;
}

std::string ResourceManager::getResourceLocation(const std::string &resource_name_raw) {
  std::string base_name = fixDoubleName(resource_name_raw);

  std::vector<std::string> extensions = { "", ".ini", ".lyt", ".uca", ".ucb", ".ai", ".txt", ".ani", ".tga", ".bmp", ".png", ".pal", ".wav" };

  for (const auto& ext : extensions) {
      std::string try_name = base_name + ext;
      if (this->resource_map.count(try_name)) {
          return this->resource_map[try_name];
      }
  }

  std::string with_slash = base_name + "/";
  if (this->resource_map.count(with_slash)) {
      return this->resource_map[with_slash];
  }

  bool suppress = (base_name.find("textbck") != std::string::npos) ||
                  (base_name.find("bkgnd") != std::string::npos) ||
                  (base_name.find("backdrop") != std::string::npos);
  
  if (!suppress) {
      SDL_Log("Resource not found: %s", base_name.c_str());
  }
  
  return "";
}

std::string ResourceManager::findActualResourceKey(const std::string &base_name) {
  std::vector<std::string> extensions = { "", ".ini", ".lyt", ".uca", ".ucb", ".ai", ".txt", ".ani", ".tga", ".bmp", ".png", ".pal", ".wav" };
  
  for (const auto& ext : extensions) {
      std::string try_name = base_name + ext;
      if (this->resource_map.count(try_name)) {
          return try_name;
      }
  }
  return base_name;
}

void ResourceManager::load_resource_map(std::atomic<float> * progress, float progress_goal) {
  if (resource_map_loaded) return;
  SDL_Log("Loading resource map...");
  std::vector<std::string> resource_paths = config->getResourcePaths();
  float step = (progress_goal - *progress) / (float) resource_paths.size();
  
  for (std::string path : resource_paths) {
    path = Utils::fixPath(path);
    if (path.empty()) continue;
    
    try {
      for (std::filesystem::directory_entry archive : std::filesystem::directory_iterator(path)) {
        if (Utils::getFileExtension(archive.path().string()) != "ZTD") continue;
        
        for (std::string file_raw : ZtdFile::getFileList(archive.path().string())) {
          std::string file = normalizePath(file_raw);
          if (resource_map.count(file) == 0) {
            resource_map[file] = archive.path().string();
          }
        }
      }
    } catch (std::exception& e) {
      SDL_Log("Warning: Could not scan path %s: %s", path.c_str(), e.what());
    }
    *progress = (*progress + step < progress_goal) ? *progress + step : progress_goal;
  }
  
  resource_map_loaded = true;
  SDL_Log("Loading resource map done. Total files indexed: %zu", resource_map.size());
}

void ResourceManager::load_string_map(std::atomic<float> * progress, float progress_goal) {
  std::vector<std::string> lang_dlls;
  try {
    for (std::filesystem::directory_entry lang_dll : std::filesystem::directory_iterator(Utils::getExecutableDirectory())) {
      std::string current = lang_dll.path().filename().string();
      if (Utils::string_to_lower(current).starts_with("lang") && Utils::getFileExtension(current) == "DLL") 
          lang_dlls.push_back(lang_dll.path().string());
    }
  } catch (...) {}
  
  std::sort(lang_dlls.begin(), lang_dlls.end());
  float step = lang_dlls.empty() ? 0 : (progress_goal - *progress) / (float) lang_dlls.size();
  
  for (std::string dll : lang_dlls) {
    SDL_Log("Loading strings from %s", dll.c_str());
    try {
      PeFile pe(dll);
      for (uint32_t id : pe.getStringIds()) {
        std::string s = pe.getString(id);
        if (!s.empty()) string_map[id] = s;
      }
    } catch (...) {
      SDL_Log("Warning: Could not load strings from %s", dll.c_str());
    }
    *progress = (*progress + step < progress_goal) ? *progress + step : progress_goal;
  }
}

void ResourceManager::load_pallet_map(std::atomic<float> * progress, float progress_goal) {
  for (auto file : resource_map) {
    if(Utils::getFileExtension(file.first) == "PAL") {
      pallet_manager.addPalletFileToMap(file.first, file.second);
    }
  }
  pallet_manager.loadPalletMap(progress, progress_goal);
}

void ResourceManager::load_all(std::atomic<float> * progress, std::atomic<bool> * is_done) {
  load_resource_map(progress, 33.0f);
  load_string_map(progress, 66.0f);
  load_pallet_map(progress, 100.0f);
  if (intro_music == nullptr && config->getPlayMenuMusic()) {
      intro_music = getMusic(config->getMenuMusic());
      if (intro_music) Mix_PlayMusic(intro_music, -1);
  }
  *is_done = true;
}

void * ResourceManager::getFileContent(const std::string &name_raw, int *size) { 
    std::string name = fixDoubleName(name_raw);
    std::string actual_key = findActualResourceKey(name);
    std::string loc = getResourceLocation(name);
    if (loc.empty()) return nullptr;
    return ZtdFile::getFileContent(loc, actual_key, size);
}

SDL_Texture * ResourceManager::getTexture(SDL_Renderer * r, const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);
  if (loc.empty()) return nullptr;
  
  SDL_Surface * s = ZtdFile::getImageSurface(loc, actual_key);
  if (!s) return nullptr;
  SDL_Texture * t = SDL_CreateTextureFromSurface(r, s);
  SDL_FreeSurface(s);
  return t;
}

Mix_Music * ResourceManager::getMusic(const std::string &name_raw) { 
    std::string name = fixDoubleName(name_raw);
    std::string actual_key = findActualResourceKey(name);
    std::string loc = getResourceLocation(name);
    if (loc.empty()) return nullptr;
    return ZtdFile::getMusic(loc, actual_key); 
}

IniReader * ResourceManager::getIniReader(const std::string &name_raw) { 
    std::string name = fixDoubleName(name_raw);
    
    // [FIX] Check if directory, return empty reader if so
    // Using (void*)"" cast to fix C2665 error
    if (isDirectory(name)) return new IniReader((void*)"", 0);
    
    std::string actual_key = findActualResourceKey(name);
    std::string loc = getResourceLocation(name);
    
    if (loc.empty() || actual_key.back() == '/') return new IniReader((void*)"", 0);
    
    return ZtdFile::getIniReader(loc, actual_key); 
}

Animation *ResourceManager::getAnimation(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string loc = getResourceLocation(name);
  
  if (!loc.empty()) { 
      std::string actual_key = findActualResourceKey(name);
      Animation* a = AniFile::getAnimation(&pallet_manager, loc, actual_key); 
      if(a) return a; 
  }
  
  std::string name_ani = name + ".ani";
  loc = getResourceLocation(name_ani);
  if (!loc.empty()) { 
      Animation* a = AniFile::getAnimation(&pallet_manager, loc, name_ani); 
      if(a) return a; 
  }
  
  // Try looking inside if it's a directory
  std::string dir_ani = name + "/" + name.substr(name.find_last_of('/') + 1) + ".ani";
  loc = getResourceLocation(dir_ani);
  if (!loc.empty()) {
      Animation* a = AniFile::getAnimation(&pallet_manager, loc, dir_ani);
      if (a) return a;
  }
  return nullptr;
}

SDL_Cursor * ResourceManager::getCursor(uint32_t id) {
  try {
    PeFile pe(config->getResDllName());
    SDL_Surface * s = pe.getCursor(id);
    if (!s) return nullptr;
    SDL_Cursor * c = SDL_CreateColorCursor(s, 0, 0);
    SDL_FreeSurface(s);
    return c;
  } catch (...) { return nullptr; }
}

void ResourceManager::load_animation_map(std::atomic<float>*, float) {}

SDL_Texture * ResourceManager::getLoadTexture(SDL_Renderer *r) {
  try {
    uint32_t id = (Utils::getExpansion() == Expansion::ALL) ? 505 : 
                  (Utils::getExpansion() == Expansion::MARINE_MANIA ? 504 : 502);
    PeFile pe(Utils::getExpansionLangDllPath(Utils::getExpansion()));
    SDL_Surface * s = pe.getLoadScreenSurface(id);
    if (!s) return nullptr;
    SDL_Texture * t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
  } catch (...) { return nullptr; }
}

SDL_Texture *ResourceManager::getStringTexture(SDL_Renderer * r, const int f, const std::string &s, SDL_Color c) {
  return font_manager.getStringTexture(r, f, s, c);
}

std::string ResourceManager::getString(uint32_t id) { 
  if (string_map.count(id)) return string_map[id];
  return "";
}
