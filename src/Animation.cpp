#include "Animation.hpp"
#include "AniFile.hpp"
#include <assert.h>

Animation::Animation(std::unordered_map<std::string, AnimationData *> *data)
    : current_frame(0), last_direction(CompassDirection::S),
      renderer_flip(SDL_FLIP_NONE), has_background(false),
      frame_time_in_ms(100), frame_start_time(0) {
  for (auto map_entry : *data) {
    this->loadSurfaces(map_entry.first, map_entry.second);
  }
}

// [PATCH] Move Semantics to prevent double-free
Animation::Animation(Animation &&other) noexcept { *this = std::move(other); }

Animation &Animation::operator=(Animation &&other) noexcept {
  if (this != &other) {
    // 1. Destroy current resources
    for (auto surface_list : this->surfaces) {
      for (SDL_Surface *surface : surface_list.second) {
        if (surface)
          SDL_FreeSurface(surface);
      }
      surface_list.second.clear();
    }
    this->surfaces.clear();

    for (auto texture_list : this->textures) {
      for (SDL_Texture *texture : texture_list.second) {
        if (texture)
          SDL_DestroyTexture(texture);
      }
      texture_list.second.clear();
    }
    this->textures.clear();

    // 2. Move Resources
    this->surfaces = std::move(other.surfaces);
    this->textures = std::move(other.textures);

    // 3. Move Properties
    this->current_frame = other.current_frame;
    this->last_direction = other.last_direction;
    this->renderer_flip = other.renderer_flip;
    this->frame_start_time = other.frame_start_time;
    this->frame_time_in_ms = other.frame_time_in_ms;
    this->has_background = other.has_background;

    // 4. Reset Other
    other.surfaces.clear();
    other.textures.clear();
  }
  return *this;
}

Animation::~Animation() {
  for (auto surface_list : this->surfaces) {
    for (SDL_Surface *surface : surface_list.second) {
      if (surface)
        SDL_FreeSurface(surface);
    }
    surface_list.second.clear();
  }
  for (auto texture_list : this->textures) {
    for (SDL_Texture *texture : texture_list.second) {
      if (texture)
        SDL_DestroyTexture(texture);
    }
    texture_list.second.clear();
  }
}

void Animation::draw(SDL_Renderer *renderer, int x, int y,
                     CompassDirection direction) {
  std::string direction_string =
      convertCompassDirectionToExistingAnimationString(direction,
                                                       this->textures);
  SDL_Rect rect = {x, y, 0, 0};

  if (!this->textures[direction_string].empty()) {
    size_t texCount = this->textures[direction_string].size();
    if (texCount == 0)
      return;

    if ((size_t)this->current_frame >= texCount)
      this->current_frame = 0;

    SDL_Texture *t = this->textures[direction_string][this->current_frame];

    // [DEBUG CRASH GUARD]
    if (t == nullptr)
      return;

    SDL_QueryTexture(t, NULL, NULL, &rect.w, &rect.h);
  } else {
    direction_string = convertCompassDirectionToExistingAnimationString(
        direction, this->surfaces);

    if (this->surfaces.find(direction_string) == this->surfaces.end() ||
        this->surfaces[direction_string].empty()) {
      return;
    }

    if ((size_t)this->current_frame >= this->surfaces[direction_string].size())
      this->current_frame = 0;

    SDL_Surface *s = this->surfaces[direction_string][this->current_frame];
    if (s == nullptr)
      return;

    rect.w = s->w;
    rect.h = s->h;
  }

  if (rect.w <= 0 || rect.h <= 0)
    return;
  this->draw(renderer, &rect, direction);
}

void Animation::draw(SDL_Renderer *renderer, SDL_Rect *dest_rect,
                     CompassDirection direction) {
  if (renderer == nullptr)
    return;

  std::string direction_string =
      convertCompassDirectionToExistingAnimationString(direction,
                                                       this->textures);

  // LAZY TEXTURE GENERATION
  static int globalDrawCount = 0;
  globalDrawCount++;
  bool debugThis = false; // (globalDrawCount >= 1000);

  if (debugThis)
    SDL_Log("Animation::draw(rect) [Count %d] Start. DirString: '%s'",
            globalDrawCount, direction_string.c_str());

  if (direction_string.empty()) {
    if (debugThis)
      SDL_Log(
          "Animation::draw(rect) [Count %d] Empty dir string. Converting...",
          globalDrawCount);
    direction_string = convertCompassDirectionToExistingAnimationString(
        direction, this->surfaces);

    if (debugThis)
      SDL_Log("Animation::draw(rect) [Count %d] New DirString: '%s'",
              globalDrawCount, direction_string.c_str());

    if (this->surfaces.find(direction_string) == this->surfaces.end()) {
      if (debugThis)
        SDL_Log("Animation::draw(rect) [Count %d] Surface not found for dir.",
                globalDrawCount);
      return;
    }

    this->textures[direction_string] = std::vector<SDL_Texture *>();
    if (!direction_string.empty()) {
      if (debugThis)
        SDL_Log("Animation::draw(rect) [Count %d] Creating textures from "
                "surfaces...",
                globalDrawCount);
      for (SDL_Surface *surface : this->surfaces[direction_string]) {
        if (!surface) {
          if (debugThis)
            SDL_Log("Animation::draw(rect) [Count %d] Warning: Null surface in "
                    "vector!",
                    globalDrawCount);
          this->textures[direction_string].push_back(nullptr);
          continue;
        }
        SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, surface);
        if (!t)
          SDL_Log("Warning: Failed to create texture: %s", SDL_GetError());

        this->textures[direction_string].push_back(t);
        SDL_FreeSurface(surface); // Free surface to save RAM
      }
      this->surfaces[direction_string].clear();
      if (debugThis)
        SDL_Log("Animation::draw(rect) [Count %d] Surfaces cleared.",
                globalDrawCount);
    } else {
      return;
    }
  }

  if (debugThis)
    SDL_Log("Animation::draw(rect) [Count %d] Texture check...",
            globalDrawCount);

  if (this->textures[direction_string].empty()) {
    if (debugThis)
      SDL_Log("Animation::draw(rect) [Count %d] No textures!", globalDrawCount);
    return;
  }

  size_t texCount = this->textures[direction_string].size();
  if (texCount == 0)
    return;

  // Animation Timing
  if (direction != this->last_direction) {
    this->last_direction = direction;
    this->current_frame = 0;
    this->frame_start_time = SDL_GetTicks();
  } else {
    if (this->frame_time_in_ms < SDL_GetTicks() - this->frame_start_time) {
      // [FROZEN] this->current_frame++;
      // if (static_cast<size_t>(this->current_frame) >= texCount) {
      //   this->current_frame = 0;
      // }
      this->frame_start_time = SDL_GetTicks();
    }
  }

  if (static_cast<size_t>(this->current_frame) >= texCount)
    this->current_frame = 0;

  SDL_Texture *texture = this->textures[direction_string][this->current_frame];

  if (debugThis)
    SDL_Log("Animation::draw(rect) [Count %d] RenderCopy. Tex: %p Frame: %d",
            globalDrawCount, texture, this->current_frame);

  if (texture) {
    SDL_RenderCopyEx(renderer, texture, NULL, dest_rect, 0, NULL,
                     this->renderer_flip);
  }
  if (debugThis)
    SDL_Log("Animation::draw(rect) [Count %d] Done.", globalDrawCount);
}

void Animation::queryTexture(CompassDirection direction, int *w, int *h) {
  std::string direction_string =
      convertCompassDirectionToExistingAnimationString(direction,
                                                       this->textures);
  if (!this->textures[direction_string].empty()) {
    if (this->current_frame < this->textures[direction_string].size()) {
      SDL_Texture *t = this->textures[direction_string][this->current_frame];
      if (t)
        SDL_QueryTexture(t, NULL, NULL, w, h);
    }
  } else {
    direction_string = convertCompassDirectionToExistingAnimationString(
        direction, this->surfaces);
    if (!this->surfaces[direction_string].empty()) {
      if (this->current_frame < this->surfaces[direction_string].size()) {
        SDL_Surface *s = this->surfaces[direction_string][this->current_frame];
        if (s) {
          if (w)
            *w = s->w;
          if (h)
            *h = s->h;
        }
      }
    }
  }
}

std::string
Animation::convertCompassDirectionToString(CompassDirection direction) {
  switch (direction) {
  case CompassDirection::N:
    return "N";
  case CompassDirection::NE:
    return "NE";
  case CompassDirection::NW:
    return "NW";
  case CompassDirection::S:
    return "S";
  case CompassDirection::SE:
    return "SE";
  case CompassDirection::SW:
    return "SW";
  case CompassDirection::E:
    return "E";
  case CompassDirection::W:
    return "W";
  case CompassDirection::G:
    return "G";
  case CompassDirection::H:
    return "H";
  default:
    return "N";
  }
}

template <typename T>
std::string Animation::convertCompassDirectionToExistingAnimationString(
    CompassDirection direction,
    std::unordered_map<std::string, T> &animation_map) {
  std::string direction_string = "";
  this->renderer_flip = SDL_FLIP_NONE;

#define TRY_DIR(d, flip)                                                       \
  if (animation_map.count(d)) {                                                \
    direction_string = d;                                                      \
    this->renderer_flip = flip;                                                \
    return d;                                                                  \
  }

  switch (direction) {
  case CompassDirection::N:
    TRY_DIR("N", SDL_FLIP_NONE);
    TRY_DIR("NE", SDL_FLIP_NONE);
    TRY_DIR("NW", SDL_FLIP_NONE);
    break;
  case CompassDirection::NE:
    TRY_DIR("NE", SDL_FLIP_NONE);
    TRY_DIR("NW", SDL_FLIP_HORIZONTAL);
    TRY_DIR("N", SDL_FLIP_NONE);
    break;
  case CompassDirection::NW:
    TRY_DIR("NW", SDL_FLIP_NONE);
    TRY_DIR("NE", SDL_FLIP_HORIZONTAL);
    TRY_DIR("N", SDL_FLIP_NONE);
    break;
  case CompassDirection::S:
    TRY_DIR("S", SDL_FLIP_NONE);
    TRY_DIR("SE", SDL_FLIP_NONE);
    TRY_DIR("SW", SDL_FLIP_NONE);
    TRY_DIR("N", SDL_FLIP_NONE);
    break;
  case CompassDirection::SE:
    TRY_DIR("SE", SDL_FLIP_NONE);
    TRY_DIR("SW", SDL_FLIP_HORIZONTAL);
    TRY_DIR("E", SDL_FLIP_NONE);
    TRY_DIR("S", SDL_FLIP_NONE);
    break;
  case CompassDirection::SW:
    TRY_DIR("SW", SDL_FLIP_NONE);
    TRY_DIR("SE", SDL_FLIP_HORIZONTAL);
    TRY_DIR("W", SDL_FLIP_NONE);
    TRY_DIR("S", SDL_FLIP_NONE);
    break;
  case CompassDirection::E:
    TRY_DIR("E", SDL_FLIP_NONE);
    TRY_DIR("W", SDL_FLIP_HORIZONTAL);
    TRY_DIR("SE", SDL_FLIP_NONE);
    TRY_DIR("NE", SDL_FLIP_NONE);
    break;
  case CompassDirection::W:
    TRY_DIR("W", SDL_FLIP_NONE);
    TRY_DIR("E", SDL_FLIP_HORIZONTAL);
    TRY_DIR("SW", SDL_FLIP_NONE);
    TRY_DIR("NW", SDL_FLIP_NONE);
    break;
  case CompassDirection::G:
    TRY_DIR("G", SDL_FLIP_NONE);
    TRY_DIR("N", SDL_FLIP_NONE);
    break;
  case CompassDirection::H:
    TRY_DIR("H", SDL_FLIP_NONE);
    TRY_DIR("N", SDL_FLIP_NONE);
    break;
  }
  return "";
}

static void calculateOffset(AnimationData *data, int16_t *offset_x,
                            int16_t *offset_y) {
  *offset_x = 0;
  *offset_y = 0;

  if (data == nullptr || data->frame_count == 0)
    return;

  if (data->has_background) {
    *offset_x = (data->width / 2) -
                (data->frames[data->frame_count].width / 2) +
                data->frames[data->frame_count].offset_x;
    *offset_y = (data->height / 2) -
                (data->frames[data->frame_count].height / 2) +
                data->frames[data->frame_count].offset_y;
    return;
  }

  *offset_x = (data->width / 2) - (data->frames[0].width / 2) +
              data->frames[0].offset_x;
  *offset_y = (data->height / 2) - (data->frames[0].height / 2) +
              data->frames[0].offset_y;
}

void Animation::loadSurfaces(std::string direction_string,
                             AnimationData *data) {
  if (data == nullptr || data->frame_count == 0)
    return;

  this->frame_time_in_ms = data->frame_time_in_ms;
  this->has_background = data->has_background;

  int16_t offset_x = 0;
  int16_t offset_y = 0;
  calculateOffset(data, &offset_x, &offset_y);

  this->surfaces[direction_string] = std::vector<SDL_Surface *>();

  int loop_limit = (int)data->frame_count + (int)data->has_background;

  for (int i = 0; i < loop_limit; i++) {
    SDL_Surface *new_surface = SDL_CreateRGBSurfaceWithFormat(
        0, data->width, data->height, 0, SDL_PIXELFORMAT_RGBA32);

    if (!new_surface) {
      this->surfaces[direction_string].push_back(nullptr);
      continue;
    }

    SDL_FillRect(new_surface, NULL, 0x00000000);
    this->surfaces[direction_string].push_back(new_surface);

    for (int y = 0; y < data->frames[i].height; y++) {
      int x = offset_x - data->frames[i].offset_x;
      if (!data->frames[i].lines)
        continue;

      for (int instruction = 0;
           instruction < data->frames[i].lines[y].instruction_count;
           instruction++) {

        if (!data->frames[i].lines[y].instructions)
          continue;

        x += data->frames[i].lines[y].instructions[instruction].offset;

        for (int p = 0;
             p < data->frames[i].lines[y].instructions[instruction].color_count;
             p++, x++) {

          if (x < 0 || x >= data->width || y < 0 || y >= data->height)
            continue;

          uint32_t color = 0xFFFF00FF;

          if (data->frames[i].is_shadow) {
            color = 0xFF000000;
          } else if (data->pallet && data->pallet->colors) {
            if (!data->frames[i].lines[y].instructions[instruction].colors)
              continue;
            uint8_t index =
                data->frames[i].lines[y].instructions[instruction].colors[p];
            color = data->pallet->colors[index];
            color |= 0xFF000000;
          }

          int pitch = new_surface->pitch / 4;
          uint32_t *pixels = (uint32_t *)new_surface->pixels;
          int y_final = y + offset_y - data->frames[i].offset_y;
          int x_final = x;

          if (y_final >= 0 && y_final < data->height && x_final >= 0 &&
              x_final < data->width) {
            pixels[y_final * pitch + x_final] = color;
          }
        }
      }
    }
  }

  // [CRITICAL FIX] Use AniFile::freeAnimationData because data has nested
  // allocations
  if (data) {
    AniFile::freeAnimationData(data);
  }
}

bool Animation::hasFrames(CompassDirection direction) {
  std::string dir_str = convertCompassDirectionToExistingAnimationString(
      direction, this->surfaces);
  if (!dir_str.empty())
    return true;
  dir_str = convertCompassDirectionToExistingAnimationString(direction,
                                                             this->textures);
  if (!dir_str.empty())
    return true;
  return false;
}
