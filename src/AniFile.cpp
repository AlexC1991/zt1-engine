#include "AniFile.hpp"

#include <cstring>
#include <vector>

#include "Utils.hpp"
#include "ZtdFile.hpp"

// PATCHED: NULL safety checks
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

  return new Animation(animations);
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
  AnimationData *animation_data = new AnimationData;
  animation_data->frame_count = 0;
  animation_data->frames = nullptr;

  // --- HEADER PARSING ---
  // File format:
  //   - 4 bytes: timing/height value (not used - actual dimensions come from
  //   .ani file)
  //   - 4 bytes: palette string length
  //   - N bytes: palette path string (null terminated or not)
  //   - 4 bytes: unknown field (possibly frame count) - SKIP THIS
  //   - Then frame data starts...

  uint32_t timing_or_height =
      SDL_ReadLE32(rw); // Not used - dimensions come from .ani
  uint32_t str_len = SDL_ReadLE32(rw);

  // Skip Palette String
  SDL_RWseek(rw, str_len, RW_SEEK_CUR);

  // Skip the 4-byte field after palette (frame count or other metadata)
  // This field was incorrectly being read as "width" causing offset issues
  SDL_ReadLE32(rw); // Skip this field

  // Initialize with placeholder values - these get overwritten by the .ani file
  // values
  animation_data->width = 0;
  animation_data->height = 0;

  // --- DYNAMIC FRAME LOADING ---
  std::vector<AnimationFrameData> temp_frames;

  while (SDL_RWtell(rw) < file_size) {
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

    SDL_Log("AniFile: Frame header: size=%u, %dx%d, offset=(%d,%d)", frame.size,
            frame.width, frame.height, frame.offset_x, frame.offset_y);

    // 2. PIXEL DATA
    frame.lines =
        (AnimationLineData *)calloc(frame.height, sizeof(AnimationLineData));

    long frame_data_start = SDL_RWtell(rw);
    long frame_data_end = frame_data_start + frame.size;

    SDL_Log("AniFile: Pixel data starts at offset %ld (0x%lx), ends at %ld",
            frame_data_start, frame_data_start, frame_data_end);

    for (int y = 0; y < frame.height; y++) {
      if (SDL_RWtell(rw) >= frame_data_end)
        break;

      frame.lines[y].instruction_count = SDL_ReadU8(rw);

      if (frame.lines[y].instruction_count > 0) {
        frame.lines[y].instructions = (AnimationDrawInstruction *)calloc(
            frame.lines[y].instruction_count, sizeof(AnimationDrawInstruction));

        for (int x = 0; x < frame.lines[y].instruction_count; x++) {
          frame.lines[y].instructions[x].offset = SDL_ReadU8(rw);
          frame.lines[y].instructions[x].color_count = SDL_ReadU8(rw);

          if (frame.lines[y].instructions[x].color_count > 0) {
            frame.lines[y].instructions[x].colors = (uint8_t *)calloc(
                frame.lines[y].instructions[x].color_count, sizeof(uint8_t));
            SDL_RWread(rw, frame.lines[y].instructions[x].colors,
                       sizeof(uint8_t),
                       frame.lines[y].instructions[x].color_count);
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
