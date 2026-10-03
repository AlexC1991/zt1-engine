#include "ResourceManager.hpp"

#include "SDL_image.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "ArtScaler.hpp"
#include "Expansion.hpp"
#include "Utils.hpp"
// NOTE: PngLoader removed - using original game assets only
#include "ZtdFile.hpp"

ResourceManager::ResourceManager(Config *config) : config(config) {
  // Layout fonts are string-table ids (face name, point size)
  font_manager.setStringLookup(
      [this](uint32_t id) { return this->getString(id); });
}

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

  // [PATCH] Allow loose files to override ZTD content (files only, not
  // directories)
  // [DIAGNOSTIC] Trace every path attempt
  for (const auto &ext : extensions) {
    std::string try_path = base_name + ext;
    // Log what we are checking (Only log failures if you want less spam, but
    // user asked for tracing) SDL_Log("[TRACE] Checking: %s",
    // try_path.c_str());

    if (std::filesystem::exists(try_path) &&
        std::filesystem::is_regular_file(try_path)) {
      // // SDL_Log("[SUCCESS] Found Loose File: %s", try_path.c_str());
      return try_path;
    }
  }

  for (const auto &ext : extensions) {
    std::string try_name = base_name + ext;
    if (this->resource_map.count(try_name)) {
      SDL_Log("[SUCCESS] Found in ZTD Map: %s -> %s", try_name.c_str(),
              this->resource_map[try_name].c_str());
      return this->resource_map[try_name];
    }
  }

  std::string with_slash = base_name + "/";
  if (this->resource_map.count(with_slash)) {
    return this->resource_map[with_slash];
  }

  bool suppress = (base_name.find("bkgnd") != std::string::npos) ||
                  (base_name.find("backdrop") != std::string::npos) ||
                  (base_name.find("_N") != std::string::npos) ||
                  (base_name.find("_H") != std::string::npos) ||
                  (base_name.find("_S") != std::string::npos) ||
                  (base_name.find("_G") != std::string::npos) ||
                  (base_name.find("_n") != std::string::npos) ||
                  (base_name.find("_h") != std::string::npos) ||
                  (base_name.find("_s") != std::string::npos) ||
                  (base_name.find("_g") != std::string::npos);

  if (!suppress) {
    // Log terrain resources specifically for debugging
    if (base_name.find("terrain/") != std::string::npos) {
      SDL_Log("[WARNING] Terrain resource not found: %s", base_name.c_str());
    }
    // SDL_Log("Resource not found: %s", base_name.c_str()); // [PATCH] Silenced
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

  // [PATCH] Also check for loose files on disk (like getResourceLocation does)
  for (const auto &ext : extensions) {
    std::string try_path = base_name + ext;
    if (std::filesystem::exists(try_path) &&
        std::filesystem::is_regular_file(try_path)) {
      return true;
    }
  }

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

  // [PATCH] Auto-include expansion directories (check both cases)
  for (const auto &xpack : {"xpack1", "XPACK1", "xpack2", "XPACK2"}) {
    if (std::filesystem::exists(xpack))
      resource_paths.push_back(xpack);
  }
  float step = (progress_goal - *progress) / (float)resource_paths.size();

  int pathIndex = -1;
  for (std::string path : resource_paths) {
    pathIndex++;
    path = Utils::fixPath(path);
    if (path.empty())
      continue;

    try {
      for (std::filesystem::directory_entry archive :
           std::filesystem::directory_iterator(path)) {
        std::string ext = Utils::getFileExtension(archive.path().string());
        if (ext != "ZTD" && ext != "ZIP")
          continue;

        for (std::string file_raw :
             ZtdFile::getFileList(archive.path().string())) {
          std::string file = normalizePath(file_raw);
          if (resource_map.count(file) == 0) {
            resource_map[file] = archive.path().string();
          }
          archive_priority.emplace(archive.path().string(), pathIndex);
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

  // [DEBUG] List terrain sprite entries
  SDL_Log("=== DEBUG: Terrain Sprite Entries ===");
  int terrain_count = 0;
  for (auto const &[key, val] : resource_map) {
    if (key.find("terrain/ic") != std::string::npos) {
      SDL_Log("  %s -> %s", key.c_str(), val.c_str());
      terrain_count++;
      if (terrain_count > 50) {
        SDL_Log("  ... (showing first 50 terrain entries)");
        break;
      }
    }
  }
  SDL_Log("=== Total terrain/* entries: %d ===", terrain_count);

  // --- [DIAGNOSTIC] ZTD MAP SCANNER ---
  SDL_Log("========================================");
  SDL_Log("      INTERNAL MAP ARCHIVE SCAN");
  SDL_Log("========================================");
  int mapCount = 0;
  for (auto const &[key, val] : resource_map) {
    if (key.length() > 4 && key.substr(key.length() - 4) == ".zoo") {
      int size = 0;
      // Read header from ZTD
      void *data = ZtdFile::getFileContent(val, key, &size);
      if (data && size > 0x28) {
        uint8_t *b = (uint8_t *)data;
        uint32_t baseId = 0;
        uint32_t mapType = 0;
        memcpy(&baseId, b + 0x20, 4);
        memcpy(&mapType, b + 0x24, 4);

        SDL_Log("MAP DETECTED: %-20s | BaseID: %-2u | Type: %-6u", key.c_str(),
                baseId, mapType);
        mapCount++;
        free(data);
      }
    }
  }
  SDL_Log("Total Maps Found in ZTDs: %d", mapCount);
  SDL_Log("========================================");
  // ------------------------------------
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

// Texture for magnified art: an HD pack's replacement when there is one,
// otherwise the art upscaled per the setting
static SDL_Texture *magnifiedTexture(SDL_Renderer *r, SDL_Surface *s,
                                     const std::string &name, int factor = 0,
                                     bool tileable = false) {
  std::string stem = name;
  size_t slash = stem.find_last_of('/'), dot = stem.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    stem = stem.substr(0, dot);
  if (SDL_Surface *hd = ArtScaler::loadHdSurface(stem)) {
    SDL_Texture *t = ArtScaler::createHdTexture(r, hd, s->w, s->h, stem);
    SDL_FreeSurface(hd);
    if (t)
      return t;
  }
  return ArtScaler::createTexture(r, s, factor, tileable);
}

SDL_Texture *ResourceManager::getTexture(SDL_Renderer *r,
                                         const std::string &name_raw,
                                         bool magnified, int factor,
                                         bool tileable) {
  std::string name = fixDoubleName(name_raw);
  std::string actual_key = findActualResourceKey(name);
  std::string loc = getResourceLocation(name);
  if (loc.empty())
    return nullptr;

  SDL_Surface *s = nullptr;

  // [PATCH] Handle loose files (non-ZTD) - load directly with SDL_image
  if (loc.find(".ztd") == std::string::npos &&
      loc.find(".ZTD") == std::string::npos) {
    s = IMG_Load(loc.c_str());
    if (!s) {
      SDL_Log("ResourceManager: Failed to load loose image %s: %s", loc.c_str(),
              IMG_GetError());
      return nullptr;
    }
  } else {
    s = ZtdFile::getImageSurface(loc, actual_key);
  }

  if (!s)
    return nullptr;

  SDL_Texture *t = magnified ? magnifiedTexture(r, s, name, factor, tileable)
                             : SDL_CreateTextureFromSurface(r, s);
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

// ZT1 sprite frame file (optionally prefixed by "FATZ" + 5 bytes):
//   u32 frame time (ms), u32 palette name length, palette name,
//   u32 frame count, then per frame:
//     u32 byte size, u16 height, u16 width, i16 y anchor, i16 x anchor,
//     u16 unknown, then per row: u8 run count, (u8 skip, u8 length,
//     length palette indices) per run
// Only the first frame is decoded.
SDL_Texture *ResourceManager::getZt1FrameTexture(SDL_Renderer *renderer,
                                                 const std::string &file_name,
                                                 int *anchorX, int *anchorY,
                                                 bool magnified) {
  if (renderer == nullptr)
    return nullptr;
  std::string loc = getResourceLocation(file_name);
  if (loc.empty())
    return nullptr;
  int size = 0;
  uint8_t *data = (uint8_t *)ZtdFile::getFileContent(
      loc, findActualResourceKey(file_name), &size);
  if (data == nullptr)
    return nullptr;

  SDL_Texture *texture = nullptr;
  auto u16 = [&](int o) { return o + 2 <= size ? data[o] | (data[o + 1] << 8) : 0; };
  auto u32 = [&](int o) {
    return o + 4 <= size ? (uint32_t)(data[o] | (data[o + 1] << 8) |
                                      (data[o + 2] << 16) | (data[o + 3] << 24))
                         : 0u;
  };

  int base = (size >= 9 && memcmp(data, "FATZ", 4) == 0) ? 9 : 0;
  int pos = base + 4;
  uint32_t palLen = u32(pos);
  pos += 4;
  if (palLen > 0 && palLen < 256 && pos + (int)palLen + 18 <= size) {
    std::string palName((const char *)data + pos, palLen);
    palName = palName.c_str(); // drop trailing NUL
    pos += palLen;
    uint32_t frames = u32(pos);
    pos += 4;
    Pallet *pal = pallet_manager.getPallet(palName);
    int height = u16(pos + 4), width = u16(pos + 6);
    if (anchorY)
      *anchorY = (int16_t)u16(pos + 8);
    if (anchorX)
      *anchorX = (int16_t)u16(pos + 10);
    pos += 4 + 10; // frame size, then the 10-byte frame header
    if (frames > 0 && pal && width > 0 && height > 0 && width < 4096 &&
        height < 4096) {
      SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(
          0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
      if (surf) {
        SDL_FillRect(surf, nullptr, SDL_MapRGBA(surf->format, 0, 0, 0, 0));
        uint32_t *pixels = (uint32_t *)surf->pixels;
        int pitch = surf->pitch / 4;
        for (int y = 0; y < height && pos < size; y++) {
          int runs = data[pos++];
          int x = 0;
          for (int r = 0; r < runs && pos + 2 <= size; r++) {
            x += data[pos++];
            int len = data[pos++];
            for (int i = 0; i < len && pos < size; i++, x++) {
              uint32_t c = pal->colors[data[pos++]];
              if (x >= 0 && x < width)
                pixels[y * pitch + x] = SDL_MapRGBA(
                    surf->format, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, 255);
            }
          }
        }
        if (magnified) {
          // The same file naming as the HD pack exporter: frame 0
          std::string hdPath = file_name + "_0";
          if (SDL_Surface *hd = ArtScaler::loadHdSurface(hdPath)) {
            texture = ArtScaler::createHdTexture(renderer, hd, width, height,
                                                 hdPath);
            SDL_FreeSurface(hd);
          }
          if (!texture)
            texture = ArtScaler::createTexture(renderer, surf,
                                               ArtScaler::worldFactor());
        } else {
          texture = SDL_CreateTextureFromSurface(renderer, surf);
        }
        if (texture)
          SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(surf);
      }
    }
  }
  free(data);
  return texture;
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

  SDL_Texture *t = magnifiedTexture(renderer, s, raw);
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

// UI animations are drawn magnified: they get art upscaling. Everything
// else is map art, upscaled only for the zoomed-in map. Either way frames
// can come from an HD pack in hd/<folder of the .ani>/
static Animation *withArtOptions(Animation *a, const std::string &name,
                                 const std::string &ani_key) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  size_t slash = ani_key.find_last_of('/');
  std::string dir = slash == std::string::npos ? "" : ani_key.substr(0, slash);
  if (lower.rfind("ui/", 0) == 0)
    a->setArtOptions(true, dir);
  else
    a->setWorldArt(dir);
  return a;
}

Animation *ResourceManager::getAnimation(const std::string &name_raw) {
  std::string name = fixDoubleName(name_raw);
  std::string loc = getResourceLocation(name);

  // [DEBUG] Detailed animation loading trace
  bool is_terrain = (name_raw.find("terrain/") != std::string::npos);
  if (is_terrain) {
    SDL_Log("[TERRAIN] getAnimation: name_raw='%s' -> fixed='%s' loc='%s'",
            name_raw.c_str(), name.c_str(), loc.c_str());
  }

  // NOTE: pc-sync override system disabled - use original game assets only

  if (!loc.empty()) {
    std::string actual_key = findActualResourceKey(name);

    SDL_Log("getAnimation: trying actual_key='%s'", actual_key.c_str());
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, actual_key);
    if (a)
      return withArtOptions(a, name_raw, actual_key);
  }

  std::string name_ani = name + ".ani";
  loc = getResourceLocation(name_ani);
  if (is_terrain) {
    SDL_Log("[TERRAIN] trying name_ani='%s' loc='%s'", name_ani.c_str(),
            loc.c_str());
  }
  if (!loc.empty()) {
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, name_ani);
    if (a) {
      if (is_terrain)
        SDL_Log("[TERRAIN] SUCCESS with name_ani!");
      return withArtOptions(a, name_raw, name_ani);
    }
  }

  std::string dir_ani =
      name + "/" + name.substr(name.find_last_of('/') + 1) + ".ani";
  loc = getResourceLocation(dir_ani);
  if (is_terrain) {
    SDL_Log("[TERRAIN] trying dir_ani='%s' loc='%s'", dir_ani.c_str(),
            loc.c_str());
  }
  if (!loc.empty()) {
    Animation *a = AniFile::getAnimation(&pallet_manager, loc, dir_ani);
    if (a) {
      if (is_terrain)
        SDL_Log("[TERRAIN] SUCCESS with dir_ani!");
      return withArtOptions(a, name_raw, dir_ani);
    } else {
      if (is_terrain)
        SDL_Log(
            "[TERRAIN] AniFile::getAnimation returned nullptr for dir_ani!");
    }
  }

  if (is_terrain) {
    SDL_Log("[TERRAIN] FAILED to load animation for '%s'", name_raw.c_str());
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
    SDL_Texture *t = ArtScaler::createTexture(r, s);
    SDL_FreeSurface(s);
    return t;
  } catch (...) {
    return nullptr;
  }
}

SDL_Texture *ResourceManager::getStringTexture(SDL_Renderer *r, const int f,
                                               const std::string &s,
                                               SDL_Color c, int fontSize) {
  return font_manager.getStringTexture(r, f, s, c, fontSize);
}

int ResourceManager::getFontLineHeight(int font, int fontSize) {
  return font_manager.getLineHeight(font, fontSize);
}

std::vector<std::string>
ResourceManager::listResources(const std::string &prefix,
                               const std::string &suffix) {
  std::vector<std::string> names;
  for (const auto &entry : resource_map) {
    const std::string &n = entry.first;
    if (n.size() >= prefix.size() + suffix.size() &&
        n.compare(0, prefix.size(), prefix) == 0 &&
        n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)
      names.push_back(n);
  }
  std::sort(names.begin(), names.end());
  return names;
}

int ResourceManager::getResourcePriority(const std::string &name) {
  auto it = resource_map.find(name);
  if (it == resource_map.end())
    return 1 << 20;
  auto p = archive_priority.find(it->second);
  return p == archive_priority.end() ? 1 << 20 : p->second;
}

std::unordered_map<std::string, std::string>
ResourceManager::readResources(const std::vector<std::string> &names) {
  std::unordered_map<std::string, std::set<std::string>> byArchive;
  for (const std::string &n : names) {
    auto it = resource_map.find(n);
    if (it != resource_map.end())
      byArchive[it->second].insert(n);
  }
  std::unordered_map<std::string, std::string> contents;
  for (const auto &archive : byArchive)
    ZtdFile::readFiles(archive.first, archive.second,
                       [&](const std::string &name, const char *data, int size) {
                         contents[name] = std::string(data, data + size);
                       });
  return contents;
}

std::string ResourceManager::getString(uint32_t id) {
  if (string_map.count(id))
    return string_map[id];
  return "";
}