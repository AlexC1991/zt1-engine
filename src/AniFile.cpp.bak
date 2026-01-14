#include "AniFile.hpp"

#include <vector>
#include <cstring>

#include "ZtdFile.hpp"
#include "Utils.hpp"

Animation * AniFile::getAnimation(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &file_name) {
  IniReader * ini_reader = ZtdFile::getIniReader(ztd_file, file_name);

  int width = ini_reader->getInt("animation", "x1") - ini_reader->getInt("animation", "x0");
  int height = ini_reader->getInt("animation", "y1") - ini_reader->getInt("animation", "y0");

  std::unordered_map<std::string, AnimationData *> * animations = new std::unordered_map<std::string, AnimationData *>;
  std::string directory = AniFile::getAnimationDirectory(ini_reader);
  for (std::string direction : ini_reader->getList("animation", "animation")) {
    (*animations)[direction] = AniFile::loadAnimationData(pallet_manager, ztd_file, directory + "/" + direction);
    (*animations)[direction]->width = width;
    (*animations)[direction]->height = height;
  }

  return new Animation(animations);
}

std::string AniFile::getAnimationDirectory(IniReader * ini_reader) {
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

AnimationData * AniFile::loadAnimationData(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &directory) {
    SDL_RWops * rw = ZtdFile::getFile(ztd_file, directory);
    if (rw == NULL) return NULL;

    Sint64 file_size = SDL_RWsize(rw);
    AnimationData * animation_data = new AnimationData;
    animation_data->frame_count = 0;
    animation_data->frames = nullptr;

    // --- HEADER PARSING ---
    // The file starts directly with Height (4 bytes), then String Len (4 bytes)
    uint32_t total_height = SDL_ReadLE32(rw);
    uint32_t str_len = SDL_ReadLE32(rw);

    // Skip Palette String
    SDL_RWseek(rw, str_len, RW_SEEK_CUR);

    uint32_t total_width = SDL_ReadLE32(rw);
    
    animation_data->width = (uint16_t)total_width;
    animation_data->height = (uint16_t)total_height;

    // --- DYNAMIC FRAME LOADING ---
    std::vector<AnimationFrameData> temp_frames;

    while (SDL_RWtell(rw) < file_size) {
        AnimationFrameData frame;
        
        // 1. FRAME HEADER
        frame.size = SDL_ReadLE32(rw);
        
        // EOF / Garbage check
        if (frame.size == 0 || frame.size > 10000000) break;

        frame.height = SDL_ReadLE16(rw);
        frame.width = SDL_ReadLE16(rw);
        frame.offset_x = SDL_ReadLE16(rw);
        frame.offset_y = SDL_ReadLE16(rw);
        frame.mystery_bytes = SDL_ReadLE16(rw);
        frame.is_shadow = false;

        // 2. PIXEL DATA
        frame.lines = (AnimationLineData *) calloc(frame.height, sizeof(AnimationLineData));
        
        long frame_data_start = SDL_RWtell(rw);
        long frame_data_end = frame_data_start + frame.size;

        for(int y = 0; y < frame.height; y++) {
            if (SDL_RWtell(rw) >= frame_data_end) break;

            frame.lines[y].instruction_count = SDL_ReadU8(rw);
            
            if (frame.lines[y].instruction_count > 0) {
                frame.lines[y].instructions = (AnimationDrawInstruction *) calloc(frame.lines[y].instruction_count, sizeof(AnimationDrawInstruction));
                
                for(int x = 0; x < frame.lines[y].instruction_count; x++) {
                    frame.lines[y].instructions[x].offset = SDL_ReadU8(rw);
                    frame.lines[y].instructions[x].color_count = SDL_ReadU8(rw);
                    
                    if (frame.lines[y].instructions[x].color_count > 0) {
                        frame.lines[y].instructions[x].colors = (uint8_t *) calloc(frame.lines[y].instructions[x].color_count, sizeof(uint8_t));
                        SDL_RWread(rw, frame.lines[y].instructions[x].colors, sizeof(uint8_t), frame.lines[y].instructions[x].color_count);
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
        animation_data->frames = (AnimationFrameData *) calloc(animation_data->frame_count, sizeof(AnimationFrameData));
        for (size_t i = 0; i < temp_frames.size(); i++) {
            animation_data->frames[i] = temp_frames[i];
        }
    }

    SDL_RWclose(rw);
    return animation_data;
}
