#include "Animation.hpp"
#include "AniFile.hpp"
#include "ArtScaler.hpp"
#include "RenderSettings.hpp"
#include <SDL2/SDL.h>
#include <assert.h>
#include <cstdio>
#include <cstdlib> // for abs

Animation::Animation(std::unordered_map<std::string, AnimationData *> *data)
    : current_frame(0), last_direction(CompassDirection::S),
      renderer_flip(SDL_FLIP_NONE), has_background(false),
      frame_time_in_ms(100), frame_start_time(0) {
  for (auto map_entry : *data) {
    this->loadSurfaces(map_entry.first, map_entry.second);
  }
}

Animation::Animation(SDL_Surface *single_frame)
    : current_frame(0), last_direction(CompassDirection::S),
      renderer_flip(SDL_FLIP_NONE), has_background(false),
      frame_time_in_ms(100), frame_start_time(0) {
  if (single_frame) {
    // Duplicate the surface so we own the memory
    SDL_Surface *copy = SDL_DuplicateSurface(single_frame);
    if (copy) {
      this->surfaces["N"].push_back(copy);
    }
  }
}

Animation::Animation(Animation &&other) noexcept { *this = std::move(other); }

Animation &Animation::operator=(Animation &&other) noexcept {
  if (this != &other) {
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
    for (auto &texture_list : this->textures_hi)
      for (SDL_Texture *texture : texture_list.second)
        if (texture)
          SDL_DestroyTexture(texture);
    this->textures_hi.clear();

    this->surfaces = std::move(other.surfaces);
    this->textures = std::move(other.textures);
    this->textures_hi = std::move(other.textures_hi);
    this->world_art = other.world_art;
    this->current_frame = other.current_frame;
    this->last_direction = other.last_direction;
    this->renderer_flip = other.renderer_flip;
    this->frame_start_time = other.frame_start_time;
    this->frame_time_in_ms = other.frame_time_in_ms;
    this->has_background = other.has_background;
    this->upscale = other.upscale;
    this->hd_dir = other.hd_dir;
    this->anchor_offsets = std::move(other.anchor_offsets);

    other.surfaces.clear();
    other.textures.clear();
    other.textures_hi.clear();
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
  for (auto &texture_list : this->textures_hi)
    for (SDL_Texture *texture : texture_list.second)
      if (texture)
        SDL_DestroyTexture(texture);
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
    if (t == nullptr)
      return;

    ArtScaler::querySize(t, &rect.w, &rect.h);
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

  // Map art switches to its upscaled frames while the map is zoomed in
  bool hi = this->world_art && RenderSettings::worldZoomedIn &&
            ArtScaler::worldFactor() > 1;
  std::vector<SDL_Texture *> *frames =
      this->frameTextures(renderer, direction, hi);
  if (frames == nullptr || frames->empty())
    return;
  size_t texCount = frames->size();

  if (direction != this->last_direction) {
    this->last_direction = direction;
    this->current_frame = 0;
    this->frame_start_time = SDL_GetTicks();
  } else {
    if (this->frame_time_in_ms < SDL_GetTicks() - this->frame_start_time) {
      this->frame_start_time = SDL_GetTicks();
    }
  }

  if (static_cast<size_t>(this->current_frame) >= texCount)
    this->current_frame = 0;

  SDL_Texture *texture = (*frames)[this->current_frame];

  if (texture) {
    RenderSettings::applyArtScaleMode(texture);
    SDL_RenderCopyEx(renderer, texture, NULL, dest_rect, 0, NULL,
                     this->renderer_flip);
  }
}

// Textures for one direction, made on first use. UI art is upscaled per the
// setting; map art is made as is, or upscaled for the zoomed-in map (hi),
// and keeps its source frames so it can make the other set later. HD pack
// frames replace any art that is drawn magnified.
std::vector<SDL_Texture *> *Animation::frameTextures(SDL_Renderer *renderer,
                                                     CompassDirection direction,
                                                     bool hi) {
  auto &set = hi ? this->textures_hi : this->textures;
  // The art's own frames for this direction (or button state) when it has
  // them - even if another one was made first - else the nearest it has
  std::string exact = convertCompassDirectionToString(direction);
  std::string direction_string;
  if (set.count(exact) && !set[exact].empty())
    return &set[exact];
  if (this->surfaces.count(exact) && !this->surfaces[exact].empty())
    direction_string = exact;
  else {
    direction_string =
        convertCompassDirectionToExistingAnimationString(direction, set);
    if (!direction_string.empty() && !set[direction_string].empty())
      return &set[direction_string];
    direction_string = convertCompassDirectionToExistingAnimationString(
        direction, this->surfaces);
  }
  auto source = this->surfaces.find(direction_string);
  if (direction_string.empty() || source == this->surfaces.end())
    return nullptr;

  std::vector<SDL_Texture *> &frames = set[direction_string];
  frames.clear();
  bool magnified = this->upscale || hi;
  int index = 0;
  for (SDL_Surface *surface : source->second) {
    int frame = index++;
    if (!surface) {
      frames.push_back(nullptr);
      continue;
    }
    SDL_Texture *t = nullptr;
    if (magnified && !this->hd_dir.empty()) {
      std::string hdPath =
          this->hd_dir + "/" + direction_string + "_" + std::to_string(frame);
      if (SDL_Surface *hd = ArtScaler::loadHdSurface(hdPath)) {
        t = ArtScaler::createHdTexture(renderer, hd, surface->w, surface->h,
                                       hdPath);
        SDL_FreeSurface(hd);
      }
    }
    if (!t) {
      if (hi)
        t = ArtScaler::createTexture(renderer, surface,
                                     ArtScaler::worldFactor());
      else if (this->upscale)
        t = ArtScaler::createTexture(renderer, surface);
      else
        t = SDL_CreateTextureFromSurface(renderer, surface);
    }
    if (!t)
      SDL_Log("Warning: Failed to create texture: %s", SDL_GetError());
    frames.push_back(t);
    if (!this->world_art)
      SDL_FreeSurface(surface);
  }
  if (!this->world_art)
    source->second.clear();
  return &frames;
}

void Animation::anchorOffset(CompassDirection direction, int *dx, int *dy) {
  std::string direction_string =
      convertCompassDirectionToExistingAnimationString(direction,
                                                       this->anchor_offsets);
  auto it = this->anchor_offsets.find(direction_string);
  SDL_Point p = it == this->anchor_offsets.end() ? SDL_Point{0, 0} : it->second;
  if (dx)
    *dx = p.x;
  if (dy)
    *dy = p.y;
}

void Animation::queryTexture(CompassDirection direction, int *w, int *h) {
  std::string direction_string =
      convertCompassDirectionToExistingAnimationString(direction,
                                                       this->textures);
  if (!this->textures[direction_string].empty()) {
    if ((size_t)this->current_frame < this->textures[direction_string].size()) {
      SDL_Texture *t = this->textures[direction_string][this->current_frame];
      if (t)
        ArtScaler::querySize(t, w, h);
    }
  } else {
    direction_string = convertCompassDirectionToExistingAnimationString(
        direction, this->surfaces);
    if (!this->surfaces[direction_string].empty()) {
      if ((size_t)this->current_frame <
          this->surfaces[direction_string].size()) {
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

// [FIX] CLAMP OFFSETS TO PREVENT SKY-HIGH SPRITES
static void calculateOffset(AnimationData *data, int16_t *offset_x,
                            int16_t *offset_y) {
  *offset_x = 0;
  *offset_y = 0;

  if (data == nullptr || data->frame_count == 0)
    return;

  int raw_x = 0;
  int raw_y = 0;

  if (data->has_background) {
    raw_x = (data->width / 2) - (data->frames[data->frame_count].width / 2) +
            data->frames[data->frame_count].offset_x;
    raw_y = (data->height / 2) - (data->frames[data->frame_count].height / 2) +
            data->frames[data->frame_count].offset_y;
  } else {
    raw_x = (data->width / 2) - (data->frames[0].width / 2) +
            data->frames[0].offset_x;
    raw_y = (data->height / 2) - (data->frames[0].height / 2) +
            data->frames[0].offset_y;
  }

  // CLAMP: Max 200 pixels up or down
  if (raw_y < -200)
    raw_y = -200;
  if (raw_y > 200)
    raw_y = 200;

  *offset_x = (int16_t)raw_x;
  *offset_y = (int16_t)raw_y;
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

  // Frame 0 lands in its surface at (offset_x - its offset_x, 0). Note the
  // frame header's fields: offset_x holds the y anchor, offset_y the x one.
  {
    const AnimationFrameData &f = data->frames[0];
    int placedX = offset_x - f.offset_x;
    this->anchor_offsets[direction_string] = {
        f.width / 2 - f.offset_y - placedX, f.height / 2 - f.offset_x};
  }

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

            // DEBUG: Log specific pixel colors for Terrain ID 2 (Sand) to catch
            // the Yellow Bug We check if this is likely the first
            // non-transparent pixel of the frame
            static bool loggedSand = false;
            if (!loggedSand && color != 0 && (color & 0xFFFFFF) != 0) {
              // We don't have Terrain ID easily accessible here, but we can
              // verify color range If color is Yellow (FFFF00), log it!
              if ((color & 0xFFFFFF) == 0xFFFF00 ||
                  (color & 0xFFFFFF) == 0x00FFFF) {
                SDL_Log(
                    "[Animation] DEBUG: FOUND YELLOW PIXEL! 0x%08X (Idx %d)",
                    color, index);
                loggedSand = true;
              }
              // Log Brown as well to see if it's working
              if ((color & 0xFFFFFF) == 0x3D6E9A ||
                  (color & 0xFFFFFF) == 0x9A6E3D) { // Check both endianness
                SDL_Log("[Animation] DEBUG: FOUND BROWN PIXEL! 0x%08X (Idx %d)",
                        color, index);
                loggedSand = true;
              }
            }
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
  if (data)
    AniFile::freeAnimationData(data);
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
