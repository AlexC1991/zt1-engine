#include "ResourceManager.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Expansion.hpp"
#include "Utils.hpp"
#include "ZtdFile.hpp"

ResourceManager::ResourceManager(Config *config) : config(config) {}

ResourceManager::~ResourceManager() {
  Mix_HaltMusic();
  if (this->intro_music != nullptr) {
    Mix_FreeMusic(this->intro_music);
  }
}

static std::string normalizePath(const std::string &input) {
  std::string path = Utils::string_to_lower(input);
  std::replace(path.begin(), path.end(), '\\', '/');

  while (!path.empty() && path[0] == '/')
    path = path.substr(1);
  while (!path.empty() && path.back() == '/')
    path.pop_back();

  return path;
}

static std::string fixDoubleName(const std::string &input) {
  std::string path = normalizePath(input);

  size_t last = path.find_last_of('/');
  if (last != std::string::npos) {
    std::string file = path.substr(last + 1);
    std::string parent = path.substr(0, last);

    size_t parent_last = parent.find_last_of('/');
    std::string parent_name = (parent_last != std::string::npos)
                                  ? parent.substr(parent_last + 1)
                                  : parent;

    if (parent_name == file) {
      return parent;
    }
  }
  return path;
}

bool ResourceManager::isDirectory(const std::string &path) {
  std::string with_slash = path + "/";
  return resource_map.count(with_slash) > 0;
}

std::string
ResourceManager::getResourceLocation(const std::string &resource_name_raw) {
  std::string base_name = fixDoubleName(resource_name_raw);

  std::vector<std::string> extensions = {
      "",     ".ini", ".lyt", ".uca", ".ucb", ".ai",  ".txt",
      ".ani", ".tga", ".bmp", ".png", ".pal", ".wav",
  };

  // [PATCH] Allow loose files to override ZTD content
  for (const auto &ext : extensions) {
    std::string try_path = base_name + ext;
    if (std::filesystem::exists(try_path)) {
      SDL_Log("ResourceManager: Loading loose file override: %s",
              try_path.c_str());
      return try_path;
    }
  }

  for (const auto &ext : extensions) {
    std::string try_name = base_name + ext;
    if (this->resource_map.count(try_name)) {
      return this->resource_map[try_name];
    }
  }

  std::string with_slash = base_name + "/";
  if (this->resource_map.count(with_slash)) {
    return this->resource_map[with_slash];
  }

  bool suppress = (base_name.find("bkgnd") != std::string::npos) ||
                  (base_name.find("backdrop") != std::string::npos);

  if (!suppress) {
    SDL_Log("Resource not found: %s", base_name.c_str());
  }

  return "";
}

std::string
ResourceManager::findActualResourceKey(const std::string &base_name) {
  std::vector<std::string> extensions = {
      "",     ".ini", ".lyt", ".uca", ".ucb", ".ai",  ".txt",
      ".ani", ".tga", ".bmp", ".png", ".pal", ".wav",
  };

  for (const auto &ext : extensions) {
    std::string try_name = base_name + ext;
    if (this->resource_map.count(try_name)) {
      return try_name;
    }
  }
  return base_name;
}

bool ResourceManager::hasResource(const std::string &resource_name_raw) {
  if (!resource_map_loaded)
    return false;

  std::string base_name = fixDoubleName(resource_name_raw);

  std::vector<std::string> extensions = {
      "",     ".ini", ".lyt", ".uca", ".ucb", ".ai",  ".txt",
      ".ani", ".tga", ".bmp", ".png", ".pal", ".wav",
  };

  for (const auto &ext : extensions) {
    std::string try_name = base_name + ext;
    if (this->resource_map.count(try_name)) {
      return true;
    }
  }

  std::string with_slash = base_name + "/";
  if (this->resource_map.count(with_slash)) {
    return true;
  }

  return false;
}

void ResourceManager::load_resource_map(std::atomic<float> *progress,
                                        float progress_goal) {
  if (resource_map_loaded)
    return;
  SDL_Log("Loading resource map...");

  std::vector<std::string> resource_paths = config->getResourcePaths();
  float step = (progress_goal - *progress) / (float)resource_paths.size();

  for (std::string path : resource_paths) {
    path = Utils::fixPath(path);
    if (path.empty())
      continue;

    try {
      for (std::filesystem::directory_entry archive :
           std::filesystem::directory_iterator(path)) {
        if (Utils::getFileExtension(archive.path().string()) != "ZTD")
          continue;

        for (std::string file_raw :
             ZtdFile::getFileList(archive.path().string())) {
          std::string file = normalizePath(file_raw);
          if (resource_map.count(file) == 0) {
            resource_map[file] = archive.path().string();
          }
        }
      }
    } catch (std::exception &e) {
      SDL_Log("Warning: Could not scan path %s: %s", path.c_str(), e.what());
    }

    *progress =
        (*progress + step < progress_goal) ? *progress + step : progress_goal;
  }

  resource_map_loaded = true;
  SDL_Log("Loading resource map done. Total files indexed: %zu",
          resource_map.size());
}

void ResourceManager::load_string_map(std::atomic<float> *progress,
                                      float progress_goal) {
  std::vector<std::string> lang_dlls;
  try {
    for (std::filesystem::directory_entry lang_dll :
         std::filesystem::directory_iterator(Utils::getExecutableDirectory())) {
      std::string current = lang_dll.path().filename().string();
      if (Utils::string_to_lower(current).starts_with("lang") &&
          Utils::getFileExtension(current) == "DLL") {
        lang_dlls.push_back(lang_dll.path().string());
      }
    }
  } catch (...) {
  }

  std::sort(lang_dlls.begin(), lang_dlls.end());
  float step = lang_dlls.empty()
                   ? 0
                   : (progress_goal - *progress) / (float)lang_dlls.size();

  for (std::string dll : lang_dlls) {
    SDL_Log("Loading strings from %s", dll.c_str());
    try {
      PeFile pe(dll);
      for (uint32_t id : pe.getStringIds()) {
        std::string s = pe.getString(id);
        if (!s.empty())
          string_map[id] = s;
      }
    } catch (...) {
      SDL_Log("Warning: Could not load strings from %s", dll.c_str());
    }

    *progress =
        (*progress + step < progress_goal) ? *progress + step : progress_goal;
  }
}

void ResourceManager::load_pallet_map(std::atomic<float> *progress,
                                      float progress_goal) {
  for (auto file : resource_map) {
    if (Utils::getFileExtension(file.first) == "PAL") {
      pallet_manager.addPalletFileToMap(file.first, file.second);
    }
  }
  pallet_manager.loadPalletMap(progress, progress_goal);
}

void ResourceManager::load_all(std::atomic<float> *progress,
                               std::atomic<bool> *is_done) {
  load_resource_map(progress, 33.0f);
  load_string_map(progress, 66.0f);
  load_pallet_map(progress, 100.0f);

  if (intro_music == nullptr && config->getPlayMenuMusic()) {
    intro_music = getMusic(config->getMenuMusic());
    if (intro_music)
      Mix_PlayMusic(intro_music, -1);
  }

  *is_done = true;
}

void *ResourceManager::getFileContent(const std::string &name_raw, int *size) {
  std::string name = fixDoubleName(name_raw);
  std::string loc = getResourceLocation(name);
  if (loc.empty())
    return nullptr;

  // [PATCH] Handle loose files (non-ZTD)
  if (loc.find(".ztd") == std::string::npos &&
      loc.find(".ZTD") == std::string::npos) {
    FILE *f = fopen(loc.c_str(), "rb");
    if (f) {
      fseek(f, 0, SEEK_END);
      long fsize = ftell(f);
      fseek(f, 0, SEEK_SET);

      void *buffer =
          calloc(1, fsize + 1); // +1 for null safety if treated as string
      fread(buffer, 1, fsize, f);
      fclose(f);

      if (size)
        *size = (int)fsize;
      return buffer;
    }
    return nullptr;
  }

  std::string actual_key = findActualResourceKey(name);
  return ZtdFile::getFileContent(loc, actual_key, size);
}

SDL_Texture *ResourceManager::getTexture(SDL_Renderer *r,
                                         const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);
  if (loc.empty())
    return nullptr;

  SDL_Surface *s = ZtdFile::getImageSurface(loc, actual_key);
  if (!s)
    return nullptr;

  SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
  SDL_FreeSurface(s);
  return t;
}

// ----------------------------------------------------------------------------
// ZT1 RAW PREVIEW DECODER
// ----------------------------------------------------------------------------
static SDL_Surface *decodeZt1NToSurface(const uint8_t *data, int size,
                                        const Pallet *pal) {
  if (data == nullptr || size < 64 || pal == nullptr)
    return nullptr;

  auto readU16 = [&](int off) -> uint16_t {
    if (off + 2 > size)
      return 0;
    return (uint16_t)data[off] | ((uint16_t)data[off + 1] << 8);
  };

  auto readU32 = [&](int off) -> uint32_t {
    if (off + 4 > size)
      return 0;
    return (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
           ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
  };

  // 1. FATZ HEADER CHECK
  bool startsWithFATZ = (size >= 4 && data[0] == 'F' && data[1] == 'A' &&
                         data[2] == 'T' && data[3] == 'Z');

  if (!startsWithFATZ) {
    SDL_Log("ZT1 Decoder: Missing FATZ header");
    return nullptr;
  }

  // 2. NAVIGATE VARIABLE FATZ HEADER
  uint8_t str_len = data[13];
  int offset_after_string = 17 + str_len;
  if (offset_after_string >= size)
    return nullptr;
  if (data[offset_after_string] == 0)
    offset_after_string++;
  int rle_header_start = offset_after_string + 4;
  if (rle_header_start + 16 >= size)
    return nullptr;

  // 3. HYBRID HEADER PARSING
  uint32_t rle_size = readU32(rle_header_start + 0);
  int width = 0;
  int height = 0;
  int data_start_offset = 0;

  uint32_t w4 = readU32(rle_header_start + 4);
  uint32_t h4 = readU32(rle_header_start + 8);
  uint16_t w2 = readU16(rle_header_start + 4);
  uint16_t h2 = readU16(rle_header_start + 6);

  bool use_4byte = false;
  bool w4_valid = (w4 > 0 && w4 < 2048);
  bool h4_valid = (h4 > 0 && h4 < 2048);

  if (w4_valid && h4_valid) {
    use_4byte = true;
    width = (int)w4;
    height = (int)h4;
    data_start_offset = rle_header_start + 24;
  } else {
    width = (int)w2;
    height = (int)h2;
    data_start_offset = rle_header_start + 14;
  }

  // --- FIX: DETECT SWAPPED DIMENSIONS ---
  // Some ZT1 files have width/height swapped in their headers.
  // Log raw values for debugging
  SDL_Log("ZT1 Decoder: Raw dimensions from header: %dx%d (w2=%d h2=%d, w4=%d "
          "h4=%d)",
          width, height, w2, h2, w4, h4);

  // Auto-swap if dimensions look wrong (height > width by a large margin
  // suggests swap) Most ZT1 preview images are landscape (wider than tall)
  if (height > width && height > 200) {
    SDL_Log("ZT1 Decoder: Auto-fixing swapped dimensions: %dx%d -> %dx%d",
            width, height, height, width);
    std::swap(width, height);
  }

  if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    SDL_Log("ZT1 Decoder: Invalid dimensions detected (%dx%d)", width, height);
    return nullptr;
  }

  SDL_Log("ZT1 Decoder: Final dimensions: %dx%d using %s-byte header", width,
          height, use_4byte ? "4" : "2");

  // 4. DECODE RLE PIXELS
  SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                     SDL_PIXELFORMAT_RGBA32);
  if (!surf)
    return nullptr;
  SDL_FillRect(surf, nullptr, SDL_MapRGBA(surf->format, 0, 0, 0, 0));

  uint32_t *pixels = (uint32_t *)surf->pixels;
  int ptr = data_start_offset;

  for (int y = 0; y < height; y++) {
    if (ptr >= size)
      break;

    uint8_t cmd_count = data[ptr++];
    int x = 0;
    for (int c = 0; c < cmd_count; c++) {
      if (ptr + 2 > size)
        break;
      uint8_t skip = data[ptr++];
      uint8_t run = data[ptr++];
      x += skip;
      for (int i = 0; i < run; i++) {
        if (ptr >= size)
          break;
        uint8_t idx = data[ptr++];
        if (x >= 0 && x < width) {
          if (idx != 0) {
            uint32_t color = pal->colors[idx];
            uint8_t r = (color >> 0) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = (color >> 16) & 0xFF;
            if (!(r == 255 && g == 0 && b == 255)) {
              pixels[y * width + x] = SDL_MapRGBA(surf->format, r, g, b, 255);
            }
          }
        }
        x++;
      }
    }
  }

  return surf;
}

SDL_Texture *ResourceManager::getZt1Texture(SDL_Renderer *renderer,
                                            const std::string &raw_name,
                                            const std::string &pal_name) {
  if (renderer == nullptr)
    return nullptr;

  std::string raw = fixDoubleName(raw_name);
  std::string palPath = fixDoubleName(pal_name);

  std::string raw_loc = getResourceLocation(raw);
  if (raw_loc.empty()) {
    raw_loc = getResourceLocation(raw + "/n");
    if (raw_loc.empty())
      return nullptr;
    raw = raw + "/n";
  }

  std::string pal_loc = getResourceLocation(palPath);
  if (pal_loc.empty()) {
    palPath = "ui/palette/color256.pal";
    pal_loc = getResourceLocation(palPath);
  }

  Pallet *pal = pallet_manager.getPallet(palPath);
  if (pal == nullptr) {
    SDL_Log("Preview palette missing/unloaded: %s", palPath.c_str());
    return nullptr;
  }

  int raw_size = 0;
  void *raw_bytes = ZtdFile::getFileContent(raw_loc, raw, &raw_size);
  if (raw_bytes == nullptr || raw_size <= 0)
    return nullptr;

  SDL_Surface *s =
      decodeZt1NToSurface((const uint8_t *)raw_bytes, raw_size, pal);
  free(raw_bytes);

  if (!s) {
    SDL_Log("Failed to decode ZT1 raw image: %s", raw.c_str());
    return nullptr;
  }

  SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
  SDL_FreeSurface(s);
  return t;
}

Mix_Music *ResourceManager::getMusic(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);
  if (loc.empty())
    return nullptr;
  return ZtdFile::getMusic(loc, actual_key);
}

IniReader *ResourceManager::getIniReader(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);

  if (isDirectory(name))
    return new IniReader((void *)"", 0);

  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);

  if (loc.empty() || actual_key.back() == '/')
    return new IniReader((void *)"", 0);

  // [PATCH] Handle loose files for IniReader
  if (loc.find(".ztd") == std::string::npos &&
      loc.find(".ZTD") == std::string::npos) {
    return new IniReader(loc);
  }

  return ZtdFile::getIniReader(loc, actual_key);
}

Pallet *ResourceManager::getPallet(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string loc = getResourceLocation(name);

  // If loc is empty, maybe try to load directly from name?
  // PalletManager takes loc as "ztd path" or "directory" I think?
  // Check PalletManager::getPallet impl.
  // Actually PalletManager::getPallet(string) takes the key (file name in map)
  // But ResourceManager manages the map.

  // Wait, PalletManager has its OWN map.
  // pallet_manager.getPallet(string) does map lookup.

  return pallet_manager.getPallet(name);
}

Animation *ResourceManager::getAnimation(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string loc = getResourceLocation(name);

  SDL_Log("getAnimation: name_raw='%s' -> name='%s' loc='%s'", name_raw.c_str(),
          name.c_str(), loc.c_str());

  if (!loc.empty()) {
    std::string actual_key = findActualResourceKey(name);
    SDL_Log("getAnimation: trying actual_key='%s'", actual_key.c_str());
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, actual_key);
    if (a)
      return a;
  }

  std::string name_ani = name + ".ani";
  loc = getResourceLocation(name_ani);
  SDL_Log("getAnimation: trying name_ani='%s' loc='%s'", name_ani.c_str(),
          loc.c_str());
  if (!loc.empty()) {
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, name_ani);
    if (a)
      return a;
  }

  std::string dir_ani =
      name + "/" + name.substr(name.find_last_of('/') + 1) + ".ani";
  loc = getResourceLocation(dir_ani);
  SDL_Log("getAnimation: trying dir_ani='%s' loc='%s'", dir_ani.c_str(),
          loc.c_str());
  if (!loc.empty()) {
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, dir_ani);
    if (a)
      return a;
  }

  return nullptr;
}

SDL_Cursor *ResourceManager::getCursor(uint32_t id) {
  try {
    PeFile pe(config->getResDllName());
    SDL_Surface *s = pe.getCursor(id);
    if (!s)
      return nullptr;
    SDL_Cursor *c = SDL_CreateColorCursor(s, 0, 0);
    SDL_FreeSurface(s);
    return c;
  } catch (...) {
    return nullptr;
  }
}

void ResourceManager::load_animation_map(std::atomic<float> *, float) {}

SDL_Texture *ResourceManager::getLoadTexture(SDL_Renderer *r) {
  try {
    uint32_t id =
        (Utils::getExpansion() == Expansion::ALL)
            ? 505
            : (Utils::getExpansion() == Expansion::MARINE_MANIA ? 504 : 502);
    PeFile pe(Utils::getExpansionLangDllPath(Utils::getExpansion()));
    SDL_Surface *s = pe.getLoadScreenSurface(id);
    if (!s)
      return nullptr;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
  } catch (...) {
    return nullptr;
  }
}

SDL_Texture *ResourceManager::getStringTexture(SDL_Renderer *r, const int f,
                                               const std::string &s,
                                               SDL_Color c) {
  return font_manager.getStringTexture(r, f, s, c);
}

std::string ResourceManager::getString(uint32_t id) {
  if (string_map.count(id))
    return string_map[id];
  return "";
}