#include "AniFile.hpp"
#include <chrono>
#include <thread>

#include <algorithm>
#include <cstring>
#include <memory>

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

  ZT_TRACE("AniFile::getAnimation: ztd='%s' file='%s'", ztd_file.c_str(),
          file_name.c_str());

  std::unique_ptr<IniReader> ini_reader(ZtdFile::getIniReader(ztd_file, file_name));
  if (ini_reader == nullptr) {
    ZT_TRACE("Warning: Could not read ini for animation: %s", file_name.c_str());
    return nullptr;
  }

  int width = ini_reader->getInt("animation", "x1") -
              ini_reader->getInt("animation", "x0");
  int height = ini_reader->getInt("animation", "y1") -
               ini_reader->getInt("animation", "y0");

  std::unordered_map<std::string, AnimationData *> *animations =
      new std::unordered_map<std::string, AnimationData *>;
  // Animation::loadSurfaces consumes and frees each direction's source data.
  // Keep only the container here; freeing its pointers again corrupts the heap.
  std::unique_ptr<std::unordered_map<std::string, AnimationData *>> decoded(animations);
  std::string directory = AniFile::getAnimationDirectory(ini_reader.get());

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
      ZT_TRACE("Warning: Could not load animation direction %s from %s",
              direction.c_str(), directory.c_str());
    }
  }

  // If no animations loaded successfully, clean up and return nullptr
  if (!has_valid_animation) return nullptr;
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

// ours: the file read from memory as SDL's memory stream reads it (a value
// running past the end: the bytes there and zeros after, the place moved
// past them; a seek kept inside the file). Through SDL it was a call for
// every byte read: most of the time spent reading the art
namespace {
struct MemReader {
  const uint8_t *base;
  Sint64 size, here;
  size_t read(void *to, size_t unit, size_t count) {
    if (!unit || !count)
      return 0;
    const size_t n = static_cast<size_t>(std::min<Sint64>(static_cast<Sint64>(unit * count), size - here));
    std::memcpy(to, base + here, n);
    here += static_cast<Sint64>(n);
    return n / unit;
  }
  Uint32 le32() {
    Uint32 v = 0;
    read(&v, 1, 4);
    return SDL_Swap32LE(v);
  }
  Uint16 le16() {
    Uint16 v = 0;
    read(&v, 1, 2);
    return SDL_Swap16LE(v);
  }
  Uint8 u8() {
    Uint8 v = 0;
    read(&v, 1, 1);
    return v;
  }
  void seek(Sint64 at) { here = std::clamp<Sint64>(at, 0, size); }
};
} // namespace

AnimationData *AniFile::loadAnimationData(PalletManager *pallet_manager,
                                          const std::string &ztd_file,
                                          const std::string &directory) {
  // 1. Get File Content from ZTD
  int raw_size = 0;
  void *file_data = ZtdFile::getFileContent(ztd_file, directory, &raw_size);

  if (file_data == NULL)
    return NULL;

  // 2. Read it from memory (MemReader)
  MemReader reader{static_cast<const uint8_t *>(file_data), raw_size, 0};
  MemReader *rw = &reader;

  Sint64 file_size = raw_size;

  // [CRITICAL FIX] Use calloc() instead of new
  AnimationData *animation_data =
      (AnimationData *)calloc(1, sizeof(AnimationData));
  if (!animation_data) {
    free(file_data);
    return NULL;
  }

  animation_data->frame_count = 0;
  animation_data->frames = nullptr;
  animation_data->has_background = false;
  animation_data->frame_time_in_ms = 100;

  // --- HEADER PARSING ---
  uint32_t timing_or_height = rw->le32();
  uint32_t str_len;

  // Check for FATZ header (0x5A544146)
  if (timing_or_height == 0x5A544146) {
    // SDL_Log("AniFile: Detected FATZ header");
    rw->le32();           // Skip unknown 0x00000000
    // The byte after it: 1 when the art has a background frame (its last:
    // a building's whole picture, the others drawn over it), then the
    // timing (shifted)
    uint32_t flagAndTiming = rw->le32();
    animation_data->has_background = (flagAndTiming & 0xFF) != 0;
    if ((flagAndTiming >> 8) > 0 && (flagAndTiming >> 8) < 10000)
      animation_data->frame_time_in_ms = flagAndTiming >> 8;
    str_len = rw->le32(); // Read Length (shifted)
    str_len >>= 8;              // Fix shift
    rw->u8();             // Skip padding byte (0x00)
  } else {
    // (the first word is the art's own frame time, ms: the electric fence's
    // sparks 83 - it was played at 100 for everything)
    if (timing_or_height > 0 && timing_or_height < 10000)
      animation_data->frame_time_in_ms = timing_or_height;
    str_len = rw->le32();
  }

  // [SAFETY] Validate string length against file size
  if (rw->here + str_len > file_size) {
    SDL_Log("AniFile: CRITICAL - Palette string length exceeds file size!");
    free(file_data);
    free(animation_data);
    return NULL;
  }

  // Read Palette String
  char *pal_str = (char *)calloc(1, str_len + 1);
  rw->read(pal_str, 1, str_len);

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
  if (rw->here + 4 <= file_size) {
    rw->le32(); // Skip this field
  }

  animation_data->width = 0;
  animation_data->height = 0;

  // --- DYNAMIC FRAME LOADING ---
  std::vector<AnimationFrameData> temp_frames;

  while (rw->here < file_size) {
    // [SAFETY] Check if we have enough bytes for a frame header (14 bytes)
    if (rw->here + 14 > file_size) {
      break;
    }

    AnimationFrameData frame;

    // 1. FRAME HEADER
    frame.size = rw->le32();

    // EOF / Garbage check
    if (frame.size == 0 || frame.size > 10000000)
      break;
    // The size counts from here: this header (10 bytes) and the pixels (a
    // seagull's six flapping frames read end to end this way)
    long frame_header_start = rw->here;

    frame.height = rw->le16();
    frame.width = rw->le16();
    frame.offset_x = rw->le16();
    frame.offset_y = rw->le16();
    frame.mystery_bytes = rw->le16();
    // The height's top bit: a shadow frame (Marine Mania's "shadow" + art,
    // on a tank's floor). Its lines' runs are (skip, length) with no colours:
    // every pixel of a run is shadow. Read as a height, it was 32 thousand
    // lines of nonsense, and the art never finished loading
    frame.is_shadow = (frame.height & 0x8000) != 0;
    frame.height &= 0x7FFF;

    // 2. PIXEL DATA
    frame.lines =
        (AnimationLineData *)calloc(frame.height, sizeof(AnimationLineData));

    if (!frame.lines && frame.height > 0) {
      break;
    }

    long frame_data_end = frame_header_start + frame.size;

    // [SAFETY] Clamp end to actual file size (Truncation Fix)
    if (frame_data_end > file_size) {
      frame_data_end = file_size;
    }

    for (int y = 0; y < frame.height; y++) {
      // [SAFETY] Global bounds check
      if (rw->here >= frame_data_end)
        break;
      if (rw->here >= file_size)
        break;

      frame.lines[y].instruction_count = rw->u8();

      if (frame.lines[y].instruction_count > 0) {
        frame.lines[y].instructions = (AnimationDrawInstruction *)calloc(
            frame.lines[y].instruction_count, sizeof(AnimationDrawInstruction));

        if (!frame.lines[y].instructions)
          continue;

        for (int x = 0; x < frame.lines[y].instruction_count; x++) {

          if (rw->here + 2 > frame_data_end) {
            break;
          }

          frame.lines[y].instructions[x].offset = rw->u8();
          frame.lines[y].instructions[x].color_count = rw->u8();

          // (a shadow run has no colours: its length is all)
          if (frame.lines[y].instructions[x].color_count > 0 && !frame.is_shadow) {

            if (rw->here + frame.lines[y].instructions[x].color_count >
                frame_data_end) {
              frame.lines[y].instructions[x].color_count = 0;
              break;
            }

            frame.lines[y].instructions[x].colors = (uint8_t *)calloc(
                frame.lines[y].instructions[x].color_count, sizeof(uint8_t));

            if (frame.lines[y].instructions[x].colors) {
              rw->read(frame.lines[y].instructions[x].colors, sizeof(uint8_t), frame.lines[y].instructions[x].color_count);
            }
          }
        }
      }
    }

    temp_frames.push_back(frame);

    // Align to next frame
    rw->seek(frame_data_end);
  }

  // --- CONVERT VECTOR TO ARRAY ---
  // (a background frame is kept after the others, not counted as one)
  if (animation_data->has_background && temp_frames.size() < 2)
    animation_data->has_background = false;
  animation_data->frame_count = (uint32_t)temp_frames.size() - (animation_data->has_background ? 1 : 0);
  if (!temp_frames.empty()) {
    animation_data->frames = (AnimationFrameData *)calloc(
        temp_frames.size(), sizeof(AnimationFrameData));
    for (size_t i = 0; i < temp_frames.size(); i++) {
      animation_data->frames[i] = temp_frames[i];
    }
  }

  free(file_data); // Free the raw memory
  return animation_data;
}

void AniFile::freeAnimationData(AnimationData *data) {
  if (!data)
    return;

  if (data->frames) {
    for (uint32_t i = 0; i < data->frame_count + (data->has_background ? 1u : 0u); i++) {
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
