#include "AniFile.hpp"
#include <chrono>
#include <thread>

#include <cstring>
#include <vector>

#include "Utils.hpp"
#include "ZtdFile.hpp"

// PATCHED: NULL safety checks & Memory Allocation Fix
Animation *AniFile::getAnimation(PalletManager *pallet_manager,
                                 const std::string &ztd_file,
                                 const std::string &file_name) {
  // Safety check - if ztd_file is empty, we can't load anything
  if (ztd_file.empty()) {
    SDL_Log("Warning: Empty ZTD file path for animation: %s",
            file_name.c_str());
    return nullptr;
  }

  SDL_Log("AniFile::getAnimation: ztd='%s' file='%s'", ztd_file.c_str(),
          file_name.c_str());
  IniReader *ini_reader = ZtdFile::getIniReader(ztd_file, file_name);
  if (ini_reader == nullptr) {
    SDL_Log("Warning: Could not read ini for animation: %s", file_name.c_str());
    return nullptr;
  }

  int width = ini_reader->getInt("animation", "x1") -
              ini_reader->getInt("animation", "x0");
  int height = ini_reader->getInt("animation", "y1") -
               ini_reader->getInt("animation", "y0");

  std::unordered_map<std::string, AnimationData *> *animations =
      new std::unordered_map<std::string, AnimationData *>;
  std::string directory = AniFile::getAnimationDirectory(ini_reader);

  // [CLEANUP] Blacklist removed. All assets allowed.

  bool has_valid_animation = false;
  for (std::string direction : ini_reader->getList("animation", "animation")) {
    AnimationData *anim_data = AniFile::loadAnimationData(
        pallet_manager, ztd_file, directory + "/" + direction);
    if (anim_data != nullptr) {
      (*animations)[direction] = anim_data;
      (*animations)[direction]->width = width;
      (*animations)[direction]->height = height;
      has_valid_animation = true;
    } else {
      SDL_Log("Warning: Could not load animation direction %s from %s",
              direction.c_str(), directory.c_str());
    }
  }

  // If no animations loaded successfully, clean up and return nullptr
  if (!has_valid_animation) {
    delete animations;
    return nullptr;
  }

  Animation *anim = new Animation(animations);
  delete animations;
  return anim;
}

std::string AniFile::getAnimationDirectory(IniReader *ini_reader) {
  std::string directory = ini_reader->get("animation", "dir0");

  std::string dir1 = ini_reader->get("animation", "dir1");
  std::string dir2 = ini_reader->get("animation", "dir2");
  std::string dir3 = ini_reader->get("animation", "dir3");

  if (!dir1.empty()) {
    directory += "/";
    directory += dir1;
  }
  if (!dir2.empty()) {
    directory += "/";
    directory += dir2;
  }
  if (!dir3.empty()) {
    directory += "/";
    directory += dir3;
  }

  return directory;
}

AnimationData *AniFile::loadAnimationData(PalletManager *pallet_manager,
                                          const std::string &ztd_file,
                                          const std::string &directory) {
  // 1. Get File Content from ZTD
  int raw_size = 0;
  void *file_data = ZtdFile::getFileContent(ztd_file, directory, &raw_size);

  if (file_data == NULL)
    return NULL;

  // 2. Wrap in RWops so we can read it like a file
  SDL_RWops *rw = SDL_RWFromMem(file_data, raw_size);
  if (rw == NULL) {
    free(file_data);
    return NULL;
  }

  Sint64 file_size = raw_size;

  // [CRITICAL FIX] Use calloc() instead of new
  AnimationData *animation_data =
      (AnimationData *)calloc(1, sizeof(AnimationData));
  if (!animation_data) {
    SDL_RWclose(rw);
    free(file_data);
    return NULL;
  }

  animation_data->frame_count = 0;
  animation_data->frames = nullptr;
  animation_data->has_background = false;
  animation_data->frame_time_in_ms = 100;

  // --- HEADER PARSING ---
  uint32_t timing_or_height = SDL_ReadLE32(rw);
  uint32_t str_len;

  // Check for FATZ header (0x5A544146)
  if (timing_or_height == 0x5A544146) {
    // SDL_Log("AniFile: Detected FATZ header");
    SDL_ReadLE32(rw);           // Skip unknown 0x00000000
    SDL_ReadLE32(rw);           // Skip Timing (shifted)
    str_len = SDL_ReadLE32(rw); // Read Length (shifted)
    str_len >>= 8;              // Fix shift
    SDL_ReadU8(rw);             // Skip padding byte (0x00)
  } else {
    str_len = SDL_ReadLE32(rw);
  }

  // [SAFETY] Validate string length against file size
  if (SDL_RWtell(rw) + str_len > file_size) {
    SDL_Log("AniFile: CRITICAL - Palette string length exceeds file size!");
    SDL_RWclose(rw);
    free(file_data);
    free(animation_data);
    return NULL;
  }

  // Read Palette String
  char *pal_str = (char *)calloc(1, str_len + 1);
  SDL_RWread(rw, pal_str, 1, str_len);

  std::string palette_path = pal_str;
  free(pal_str);

  // Sanitize path (remove nulls if embedded)
  size_t null_pos = palette_path.find('\0');
  if (null_pos != std::string::npos) {
    palette_path.resize(null_pos);
  }

  // SDL_Log("AniFile: Loading palette '%s' for animation",
  // palette_path.c_str());
  animation_data->pallet = pallet_manager->getPallet(palette_path);

  // Skip the 4-byte field after palette
  if (SDL_RWtell(rw) + 4 <= file_size) {
    SDL_ReadLE32(rw); // Skip this field
  }

  animation_data->width = 0;
  animation_data->height = 0;

  // --- DYNAMIC FRAME LOADING ---
  std::vector<AnimationFrameData> temp_frames;

  while (SDL_RWtell(rw) < file_size) {
    // [SAFETY] Check if we have enough bytes for a frame header (14 bytes)
    if (SDL_RWtell(rw) + 14 > file_size) {
      break;
    }

    AnimationFrameData frame;

    // 1. FRAME HEADER
    frame.size = SDL_ReadLE32(rw);

    // EOF / Garbage check
    if (frame.size == 0 || frame.size > 10000000)
      break;

    frame.height = SDL_ReadLE16(rw);
    frame.width = SDL_ReadLE16(rw);
    frame.offset_x = SDL_ReadLE16(rw);
    frame.offset_y = SDL_ReadLE16(rw);
    frame.mystery_bytes = SDL_ReadLE16(rw);
    frame.is_shadow = false;

    // 2. PIXEL DATA
    frame.lines =
        (AnimationLineData *)calloc(frame.height, sizeof(AnimationLineData));

    if (!frame.lines && frame.height > 0) {
      break;
    }

    long frame_data_start = SDL_RWtell(rw);
    long frame_data_end = frame_data_start + frame.size;

    // [SAFETY] Clamp end to actual file size (Truncation Fix)
    if (frame_data_end > file_size) {
      frame_data_end = file_size;
    }

    for (int y = 0; y < frame.height; y++) {
      // [SAFETY] Global bounds check
      if (SDL_RWtell(rw) >= frame_data_end)
        break;
      if (SDL_RWtell(rw) >= file_size)
        break;

      frame.lines[y].instruction_count = SDL_ReadU8(rw);

      if (frame.lines[y].instruction_count > 0) {
        frame.lines[y].instructions = (AnimationDrawInstruction *)calloc(
            frame.lines[y].instruction_count, sizeof(AnimationDrawInstruction));

        if (!frame.lines[y].instructions)
          continue;

        for (int x = 0; x < frame.lines[y].instruction_count; x++) {

          if (SDL_RWtell(rw) + 2 > frame_data_end) {
            break;
          }

          frame.lines[y].instructions[x].offset = SDL_ReadU8(rw);
          frame.lines[y].instructions[x].color_count = SDL_ReadU8(rw);

          if (frame.lines[y].instructions[x].color_count > 0) {

            if (SDL_RWtell(rw) + frame.lines[y].instructions[x].color_count >
                frame_data_end) {
              frame.lines[y].instructions[x].color_count = 0;
              break;
            }

            frame.lines[y].instructions[x].colors = (uint8_t *)calloc(
                frame.lines[y].instructions[x].color_count, sizeof(uint8_t));

            if (frame.lines[y].instructions[x].colors) {
              SDL_RWread(rw, frame.lines[y].instructions[x].colors,
                         sizeof(uint8_t),
                         frame.lines[y].instructions[x].color_count);
            }
          }
        }
      }
    }

    temp_frames.push_back(frame);

    // Align to next frame
    SDL_RWseek(rw, frame_data_end, RW_SEEK_SET);
  }

  // --- CONVERT VECTOR TO ARRAY ---
  animation_data->frame_count = (uint32_t)temp_frames.size();
  if (animation_data->frame_count > 0) {
    animation_data->frames = (AnimationFrameData *)calloc(
        animation_data->frame_count, sizeof(AnimationFrameData));
    for (size_t i = 0; i < temp_frames.size(); i++) {
      animation_data->frames[i] = temp_frames[i];
    }
  }

  SDL_RWclose(rw);
  free(file_data); // Free the raw memory
  return animation_data;
}

void AniFile::freeAnimationData(AnimationData *data) {
  if (!data)
    return;

  if (data->frames) {
    for (uint32_t i = 0; i < data->frame_count; i++) {
      if (data->frames[i].lines) {
        for (int y = 0; y < data->frames[i].height; y++) {
          if (data->frames[i].lines[y].instructions) {
            for (int x = 0; x < data->frames[i].lines[y].instruction_count;
                 x++) {
              if (data->frames[i].lines[y].instructions[x].colors) {
                free(data->frames[i].lines[y].instructions[x].colors);
              }
            }
            free(data->frames[i].lines[y].instructions);
          }
        }
        free(data->frames[i].lines);
      }
    }
    free(data->frames);
  }
  free(data);
}
