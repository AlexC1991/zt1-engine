#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "SDL_ttf.h"

#include "AniFile.hpp"
#include "Animation.hpp"
#include "Config.hpp"
#include "FontManager.hpp"
#include "IniReader.hpp"
#include "Pallet.hpp"
#include "PalletManager.hpp"
#include "PeFile.hpp"
#include "ZtdFile.hpp"

class ResourceManager {
public:
  ResourceManager(Config *config);
  ~ResourceManager();

  void load_all(std::atomic<float> *progress, std::atomic<bool> *is_done);

  void *getFileContent(const std::string &file_name, int *size);
  SDL_Texture *getTexture(SDL_Renderer *renderer, const std::string &file_name);

  // NEW: ZT1 raw preview decode (N + .pal)
  SDL_Texture *getZt1Texture(
    SDL_Renderer *renderer,
    const std::string &raw_name,
    const std::string &pal_name
  );

  SDL_Cursor *getCursor(uint32_t cursor_id);
  Mix_Music *getMusic(const std::string &file_name);
  IniReader *getIniReader(const std::string &file_name);
  Animation *getAnimation(const std::string &file_name);
  SDL_Texture *getLoadTexture(SDL_Renderer *renderer);
  SDL_Texture *getStringTexture(
    SDL_Renderer *renderer,
    const int font,
    const std::string &string,
    SDL_Color color
  );
  std::string getString(uint32_t string_id);

  bool hasResource(const std::string &resource_name_raw);

private:
  std::unordered_map<std::string, std::string> resource_map;
  std::unordered_map<uint32_t, std::string> string_map;
  std::unordered_map<std::string, Animation *> animation_map;
  std::unordered_map<std::string, Pallet *> pallet_map;

  // Cache ZT1 preview textures to avoid repeated decode/scans when switching
  // selections/menus. Ownership stays with ResourceManager.
  std::unordered_map<std::string, SDL_Texture *> zt1_preview_texture_cache;

  bool resource_map_loaded = false;

  std::string getResourceLocation(const std::string &resoure_name);

  void load_resource_map(std::atomic<float> *progress, float progress_goal);
  void load_string_map(std::atomic<float> *progress, float progress_goal);
  void load_animation_map(std::atomic<float> *progress, float progress_goal);
  void load_pallet_map(std::atomic<float> *progress, float progress_goal);

  Mix_Music *intro_music = nullptr;

  Config *config;
  FontManager font_manager;
  PalletManager pallet_manager;

  std::string findActualResourceKey(const std::string &base_name);
  bool isDirectory(const std::string &path);
};

#endif // RESOURCE_MANAGER_HPP