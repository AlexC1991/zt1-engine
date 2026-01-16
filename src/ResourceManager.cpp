#include "ResourceManager.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
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

  while (!path.empty() && path[0] == '/') path = path.substr(1);
  while (!path.empty() && path.back() == '/') path.pop_back();

  return path;
}

static std::string fixDoubleName(const std::string &input) {
  std::string path = normalizePath(input);

  size_t last = path.find_last_of('/');
  if (last != std::string::npos) {
    std::string file = path.substr(last + 1);
    std::string parent = path.substr(0, last);

    size_t parent_last = parent.find_last_of('/');
    std::string parent_name =
      (parent_last != std::string::npos) ? parent.substr(parent_last + 1)
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

std::string ResourceManager::getResourceLocation(
  const std::string &resource_name_raw
) {
  std::string base_name = fixDoubleName(resource_name_raw);

  std::vector<std::string> extensions = {
    "",
    ".ini",
    ".lyt",
    ".uca",
    ".ucb",
    ".ai",
    ".txt",
    ".ani",
    ".tga",
    ".bmp",
    ".png",
    ".pal",
    ".wav",
  };

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

  bool suppress = (base_name.find("textbck") != std::string::npos) ||
    (base_name.find("bkgnd") != std::string::npos) ||
    (base_name.find("backdrop") != std::string::npos);

  if (!suppress) {
    SDL_Log("Resource not found: %s", base_name.c_str());
  }

  return "";
}

std::string ResourceManager::findActualResourceKey(const std::string &base_name) {
  std::vector<std::string> extensions = {
    "",
    ".ini",
    ".lyt",
    ".uca",
    ".ucb",
    ".ai",
    ".txt",
    ".ani",
    ".tga",
    ".bmp",
    ".png",
    ".pal",
    ".wav",
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
  if (!resource_map_loaded) return false;

  std::string base_name = fixDoubleName(resource_name_raw);

  std::vector<std::string> extensions = {
    "",
    ".ini",
    ".lyt",
    ".uca",
    ".ucb",
    ".ai",
    ".txt",
    ".ani",
    ".tga",
    ".bmp",
    ".png",
    ".pal",
    ".wav",
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

void ResourceManager::load_resource_map(
  std::atomic<float> *progress,
  float progress_goal
) {
  if (resource_map_loaded) return;
  SDL_Log("Loading resource map...");

  std::vector<std::string> resource_paths = config->getResourcePaths();
  float step = (progress_goal - *progress) / (float)resource_paths.size();

  for (std::string path : resource_paths) {
    path = Utils::fixPath(path);
    if (path.empty()) continue;

    try {
      for (std::filesystem::directory_entry archive :
           std::filesystem::directory_iterator(path)) {
        if (Utils::getFileExtension(archive.path().string()) != "ZTD") continue;

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

    *progress = (*progress + step < progress_goal) ? *progress + step
                                                   : progress_goal;
  }

  resource_map_loaded = true;
  SDL_Log(
    "Loading resource map done. Total files indexed: %zu",
    resource_map.size()
  );
}

void ResourceManager::load_string_map(std::atomic<float> *progress, float progress_goal) {
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
  float step =
    lang_dlls.empty() ? 0 : (progress_goal - *progress) / (float)lang_dlls.size();

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

    *progress = (*progress + step < progress_goal) ? *progress + step
                                                   : progress_goal;
  }
}

void ResourceManager::load_pallet_map(std::atomic<float> *progress, float progress_goal) {
  for (auto file : resource_map) {
    if (Utils::getFileExtension(file.first) == "PAL") {
      pallet_manager.addPalletFileToMap(file.first, file.second);
    }
  }
  pallet_manager.loadPalletMap(progress, progress_goal);
}

void ResourceManager::load_all(std::atomic<float> *progress, std::atomic<bool> *is_done) {
  load_resource_map(progress, 33.0f);
  load_string_map(progress, 66.0f);
  load_pallet_map(progress, 100.0f);

  if (intro_music == nullptr && config->getPlayMenuMusic()) {
    intro_music = getMusic(config->getMenuMusic());
    if (intro_music) Mix_PlayMusic(intro_music, -1);
  }

  *is_done = true;
}

void *ResourceManager::getFileContent(const std::string &name_raw, int *size) {
  std::string name = fixDoubleName(name_raw);
  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);
  if (loc.empty()) return nullptr;
  return ZtdFile::getFileContent(loc, actual_key, size);
}

SDL_Texture *ResourceManager::getTexture(SDL_Renderer *r, const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);
  if (loc.empty()) return nullptr;

  SDL_Surface *s = ZtdFile::getImageSurface(loc, actual_key);
  if (!s) return nullptr;

  SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
  SDL_FreeSurface(s);
  return t;
}

static SDL_Surface *decodeZt1NToSurface(
  const uint8_t *data,
  int size,
  const Pallet *pal
) {
  if (data == nullptr || size < 64 || pal == nullptr) return nullptr;

  // TWEAK THIS IF NEEDED:
  // Earlier correct-looking headers were around 46-50; offset 9 was a false positive.
  static const int MIN_HEADER_OFFSET = 32;

  auto readU32 = [&](int off) -> uint32_t {
    return (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
      ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
  };

  auto inferDims = [&](int ptr0, int end, bool mode, int *out_w, int *out_h) -> bool {
    int inferred_w = 0;
    int inferred_h = 0;
    int ptr = ptr0;

    while (ptr < end) {
      uint8_t cmd_count = data[ptr++];

      if (cmd_count >= 0xF0) {
        inferred_h++;
        continue;
      }

      int x = 0;
      for (int cmd = 0; cmd < (int)cmd_count; cmd++) {
        if (ptr + 1 >= end) {
          ptr = end;
          break;
        }

        uint8_t a = data[ptr++];
        uint8_t b = data[ptr++];

        uint8_t skip = mode ? b : a;
        uint8_t run = mode ? a : b;

        x += (int)skip;

        if (ptr + (int)run > end) {
          ptr = end;
          break;
        }

        x += (int)run;
        ptr += (int)run;

        if (x > inferred_w) inferred_w = x;
      }

      inferred_h++;

      if (inferred_w > 8192 || inferred_h > 8192) return false;
    }

    *out_w = inferred_w;
    *out_h = inferred_h;
    return inferred_w > 0 && inferred_h > 0;
  };

  auto render = [&](int ptr0, int end, bool mode, int w, int h) -> SDL_Surface * {
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(
      0,
      w,
      h,
      32,
      SDL_PIXELFORMAT_RGBA32
    );
    if (!surf) return nullptr;

    SDL_LockSurface(surf);
    auto *pixels = (uint32_t *)surf->pixels;

    int ptr = ptr0;
    int y = 0;

    while (ptr < end && y < h) {
      uint8_t cmd_count = data[ptr++];

      if (cmd_count >= 0xF0) {
        y++;
        continue;
      }

      int x = 0;
      for (int cmd = 0; cmd < (int)cmd_count; cmd++) {
        if (ptr + 1 >= end) {
          ptr = end;
          break;
        }

        uint8_t a = data[ptr++];
        uint8_t b = data[ptr++];

        uint8_t skip = mode ? b : a;
        uint8_t run = mode ? a : b;

        x += (int)skip;

        for (int i = 0; i < (int)run; i++) {
          if (ptr >= end) break;

          uint8_t idx = data[ptr++];

          if (x >= 0 && x < w && y >= 0 && y < h) {
            uint32_t c = pal->colors[idx];
            uint8_t r = (uint8_t)((c >> 0) & 0xFF);
            uint8_t g = (uint8_t)((c >> 8) & 0xFF);
            uint8_t b2 = (uint8_t)((c >> 16) & 0xFF);
            pixels[y * w + x] = SDL_MapRGBA(surf->format, r, g, b2, 255);
          }

          x++;
        }
      }

      y++;
    }

    SDL_UnlockSurface(surf);
    return surf;
  };

  auto decodeAt = [&](int header_pos) -> SDL_Surface * {
    if (header_pos < 0 || header_pos + 14 > size) return nullptr;

    uint32_t rle_size = readU32(header_pos + 0);
    int rle_pos = header_pos + 14;
    if (rle_size < 16 || rle_pos + (int)rle_size > size) return nullptr;

    int ptr0 = rle_pos;
    int end = rle_pos + (int)rle_size;

    int w0 = 0, h0 = 0;
    int w1 = 0, h1 = 0;

    if (!inferDims(ptr0, end, false, &w0, &h0)) return nullptr;
    if (!inferDims(ptr0, end, true, &w1, &h1)) {
      return render(ptr0, end, false, w0, h0);
    }

    bool use_mode1 = (w1 > w0);
    int w = use_mode1 ? w1 : w0;
    int h = use_mode1 ? h1 : h0;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return nullptr;

    SDL_Log(
      "ZT1 preview: inferred dims mode0=%dx%d mode1=%dx%d using=%s",
      w0,
      h0,
      w1,
      h1,
      use_mode1 ? "mode1(run,skip)" : "mode0(skip,run)"
    );

    return render(ptr0, end, use_mode1, w, h);
  };

  int scan_limit = size;
  if (scan_limit > 8192) scan_limit = 8192;

  for (int pos = MIN_HEADER_OFFSET; pos + 14 < scan_limit; pos++) {
    uint32_t rle_size = readU32(pos + 0);
    if (rle_size < 16 || rle_size > (uint32_t)size) continue;
    if (pos + 14 + (int)rle_size > size) continue;

    int rle_pos = pos + 14;
    uint8_t first_cmd = data[rle_pos];
    if (first_cmd == 0) continue;

    SDL_Surface *s = decodeAt(pos);
    if (s) {
      SDL_Log("ZT1 preview: decoded using header at offset %d", pos);
      SDL_Log("ZT1 preview: inferred surface %dx%d", s->w, s->h);
      return s;
    }
  }

  SDL_Log("ZT1 preview: could not find a valid frame header by scanning");
  return nullptr;
}

SDL_Texture *ResourceManager::getZt1Texture(
  SDL_Renderer *renderer,
  const std::string &raw_name,
  const std::string &pal_name
) {
  if (renderer == nullptr) return nullptr;

  std::string raw = fixDoubleName(raw_name);
  std::string palPath = fixDoubleName(pal_name);

  SDL_Log("ZT1 preview: raw=%s pal=%s", raw.c_str(), palPath.c_str());

  std::string raw_loc = getResourceLocation(raw);
  if (raw_loc.empty()) return nullptr;

  std::string pal_loc = getResourceLocation(palPath);
  if (pal_loc.empty()) return nullptr;

  Pallet *pal = pallet_manager.getPallet(palPath);
  if (pal == nullptr) {
    SDL_Log("Preview palette missing/unloaded: %s", palPath.c_str());
    return nullptr;
  }

  int raw_size = 0;
  void *raw_bytes = ZtdFile::getFileContent(raw_loc, raw, &raw_size);
  if (raw_bytes == nullptr || raw_size <= 0) return nullptr;

  SDL_Log("ZT1 preview raw bytes: %d", raw_size);

  SDL_Surface *s = decodeZt1NToSurface((const uint8_t *)raw_bytes, raw_size, pal);
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
  if (loc.empty()) return nullptr;
  return ZtdFile::getMusic(loc, actual_key);
}

IniReader *ResourceManager::getIniReader(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);

  if (isDirectory(name)) return new IniReader((void *)"", 0);

  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);

  if (loc.empty() || actual_key.back() == '/') return new IniReader((void *)"", 0);

  return ZtdFile::getIniReader(loc, actual_key);
}

Animation *ResourceManager::getAnimation(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string loc = getResourceLocation(name);

  if (!loc.empty()) {
    std::string actual_key = findActualResourceKey(name);
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, actual_key);
    if (a) return a;
  }

  std::string name_ani = name + ".ani";
  loc = getResourceLocation(name_ani);
  if (!loc.empty()) {
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, name_ani);
    if (a) return a;
  }

  std::string dir_ani =
    name + "/" + name.substr(name.find_last_of('/') + 1) + ".ani";
  loc = getResourceLocation(dir_ani);
  if (!loc.empty()) {
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, dir_ani);
    if (a) return a;
  }

  return nullptr;
}

SDL_Cursor *ResourceManager::getCursor(uint32_t id) {
  try {
    PeFile pe(config->getResDllName());
    SDL_Surface *s = pe.getCursor(id);
    if (!s) return nullptr;
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
    uint32_t id = (Utils::getExpansion() == Expansion::ALL)
      ? 505
      : (Utils::getExpansion() == Expansion::MARINE_MANIA ? 504 : 502);
    PeFile pe(Utils::getExpansionLangDllPath(Utils::getExpansion()));
    SDL_Surface *s = pe.getLoadScreenSurface(id);
    if (!s) return nullptr;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
  } catch (...) {
    return nullptr;
  }
}

SDL_Texture *ResourceManager::getStringTexture(
  SDL_Renderer *r,
  const int f,
  const std::string &s,
  SDL_Color c
) {
  return font_manager.getStringTexture(r, f, s, c);
}

std::string ResourceManager::getString(uint32_t id) {
  if (string_map.count(id)) return string_map[id];
  return "";
}