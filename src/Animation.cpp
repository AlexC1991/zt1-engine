#include "Animation.hpp"
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstring>
#include "AniFile.hpp"
#include "ArtScaler.hpp"
#include "HdArt.hpp"
#include "ShaderMods.hpp"
#include "RenderSettings.hpp"
#include <SDL2/SDL.h>
#include <assert.h>
#include <cstdio>
#include <cstdlib> // for abs

Animation::Animation(std::unordered_map<std::string, AnimationData *> *data)
    : current_frame(0), last_direction(CompassDirection::S),
      renderer_flip(SDL_FLIP_NONE), has_background(false),
      frame_time_in_ms(100), frame_start_time(0) {
  // (one view named for itself - the dust ball's "animation = dust" - is
  // its N: art the same from every side)
  static const char *compass[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW", "G", "H"};
  bool single = data->size() == 1 &&
                std::none_of(std::begin(compass), std::end(compass),
                             [&](const char *c) { return data->begin()->first == c; });
  for (auto map_entry : *data) {
    this->loadSurfaces(single ? std::string("N") : map_entry.first, map_entry.second);
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
    this->frame_count = other.frame_count;
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
  if (onRelease) onRelease();
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
  bool hi = this->usesHi();
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

bool Animation::drawAnchored(SDL_Renderer *renderer, float x, float y,
                             CompassDirection direction, const SDL_Color *tint,
                             int frame) {
  if (!renderer)
    return false;
  // (sets renderer_flip when a mirrored direction stands in)
  std::string key =
      convertCompassDirectionToExistingAnimationString(direction, this->anchor_points);
  if (key.empty())
    return false;
  SDL_RendererFlip flip = this->renderer_flip;
  bool hi = this->usesHi();
  std::vector<SDL_Texture *> *frames = this->frameTextures(renderer, direction, hi);
  if (!frames || frames->empty())
    return false;
  if (static_cast<size_t>(this->current_frame) >= frames->size())
    this->current_frame = 0;
  size_t index = frame >= 0 ? static_cast<size_t>(frame) % frames->size()
                            : static_cast<size_t>(this->current_frame);
  SDL_Texture *texture = (*frames)[index];
  if (!texture)
    return false;
  int w = 0, h = 0;
  ArtScaler::querySize(texture, &w, &h);
  SDL_Point a = this->anchor_points[key];
  float ax = flip == SDL_FLIP_HORIZONTAL ? static_cast<float>(w - a.x) : a.x;
  SDL_FRect dest = {x - ax, y - a.y, static_cast<float>(w), static_cast<float>(h)};
  if (RenderSettings::snapEdges) {
    // (each edge on the screen pixel its own position rounds to)
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(renderer, &sx, &sy);
    float l = SDL_roundf(dest.x * sx) / sx, t = SDL_roundf(dest.y * sy) / sy;
    float r = SDL_roundf((dest.x + dest.w) * sx) / sx, b = SDL_roundf((dest.y + dest.h) * sy) / sy;
    dest = {l, t, r - l, b - t};
  }
  RenderSettings::applyArtScaleMode(texture);
  if (tint) {
    SDL_SetTextureColorMod(texture, tint->r, tint->g, tint->b);
    SDL_SetTextureAlphaMod(texture, tint->a);
  }
  SDL_RenderCopyExF(renderer, texture, nullptr, &dest, 0, nullptr, flip);
  if (tint) {
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
  }
  return true;
}

bool Animation::drawMapped(SDL_Renderer *renderer, CompassDirection direction, int frame, const SDL_FPoint dst[4],
                           const SDL_FPoint uv[4], SDL_Color tint) {
  if (!renderer)
    return false;
  std::string key = convertCompassDirectionToExistingAnimationString(direction, this->anchor_points);
  if (key.empty())
    return false;
  bool hi = this->usesHi();
  std::vector<SDL_Texture *> *frames = this->frameTextures(renderer, direction, hi);
  if (!frames || frames->empty())
    return false;
  size_t index = frame >= 0 ? static_cast<size_t>(frame) % frames->size() : 0;
  SDL_Texture *texture = (*frames)[index];
  if (!texture)
    return false;
  RenderSettings::applyArtScaleMode(texture);
  SDL_Vertex v[4];
  for (int i = 0; i < 4; i++)
    v[i] = {dst[i], tint, uv[i]};
  const int idx[6] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, texture, v, 4, idx, 6);
  return true;
}

bool Animation::hitTestAnchored(float px,float py,float x,float y,CompassDirection direction,int frame) {
  const std::string key=convertCompassDirectionToExistingAnimationString(direction,this->surfaces);
  const bool mirrored=this->renderer_flip==SDL_FLIP_HORIZONTAL;
  auto frames=this->surfaces.find(key);auto anchor=this->anchor_points.find(key);
  if(frames==this->surfaces.end() || frames->second.empty() || anchor==this->anchor_points.end())return false;
  const int index=(frame>=0?frame:static_cast<int>(this->current_frame))%static_cast<int>(frames->second.size());
  SDL_Surface *source=frames->second[index];if(!source)return false;
  int sx=static_cast<int>(std::floor(px-x+(mirrored?source->w-anchor->second.x:anchor->second.x)));
  const int sy=static_cast<int>(std::floor(py-y+anchor->second.y));
  if(sx<0 || sy<0 || sx>=source->w || sy>=source->h)return false;
  if(mirrored)sx=source->w-1-sx;
  Uint32 pixel=0;std::memcpy(&pixel,static_cast<Uint8*>(source->pixels)+sy*source->pitch+sx*source->format->BytesPerPixel,source->format->BytesPerPixel);
  Uint8 r,g,b,a;SDL_GetRGBA(pixel,source->format,&r,&g,&b,&a);return a>=32;
}

bool Animation::anchoredBounds(float x, float y, CompassDirection direction, float &left, float &top,
                               float &right, float &bottom) {
  std::string key = convertCompassDirectionToExistingAnimationString(direction, this->anchor_points);
  if (key.empty())
    return false;
  SDL_RendererFlip flip = this->renderer_flip;
  SDL_Point a = this->anchor_points[key];
  int w = 0, h = 0;
  this->queryTexture(direction, &w, &h);
  if (w <= 0 || h <= 0)
    return false;
  float ax = flip == SDL_FLIP_HORIZONTAL ? static_cast<float>(w - a.x) : a.x;
  left = x - ax;
  top = y - a.y;
  right = left + w;
  bottom = top + h;
  return true;
}

// Textures for one direction, made on first use. UI art is upscaled per the
// setting; map art is made as is, or upscaled for the zoomed-in map (hi),
// and keeps its source frames so it can make the other set later. HD pack
// frames replace any art that is drawn magnified.
bool Animation::usesHi() const {
  // (zoomed in: its upscaled set, or an HD art mod's frames when one's on)
  return this->world_art && RenderSettings::worldZoomedIn &&
         (ArtScaler::worldFactor() > 1 || HdArt::active() || Shaders::artActive());
}

std::vector<SDL_Texture *> *Animation::frameTextures(SDL_Renderer *renderer,
                                                     CompassDirection direction,
                                                     bool hi) {
  // (an HD art mod switched on or off since they were made: made again)
  // (or a shader mod's art shader changed)
  if (hi && this->hi_generation != HdArt::generation() * 100000 + Shaders::generation()) {
    for (auto &kv : this->textures_hi)
      for (SDL_Texture *t : kv.second)
        if (t)
          SDL_DestroyTexture(t);
    this->textures_hi.clear();
    this->hi_generation = HdArt::generation() * 100000 + Shaders::generation();
  }
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
    // A shader mod's art shader at work (zoomed-in map art; not a tank's
    // water or the terrain): the frame at 4x - an HD art mod's, else the
    // original upscaled - through it (ShaderMods.hpp)
    if (hi && Shaders::artActive() && this->hd_dir.rfind("water/", 0) != 0 &&
        this->hd_dir.rfind("terrain/", 0) != 0) {
      std::string hdPath = this->hd_dir + "/" + direction_string + "_" + std::to_string(frame);
      SDL_Surface *big = nullptr;
      if (SDL_Surface *hd = ArtScaler::loadHdSurface(hdPath)) {
        if (!this->recoloured.empty())
          hd = HdArt::recolour(hd, surface, this->recoloured, direction_string, frame);
        int k = surface->w > 0 ? hd->w / surface->w : 0;
        if (k >= 2 && hd->w == surface->w * k && hd->h == surface->h * k)
          big = SDL_ConvertSurfaceFormat(hd, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(hd);
      }
      if (!big)
        big = ArtScaler::upscaleSurface(surface, 4);
      if (big) {
        // (what it is, for the shader: 1 a fence's thin lines, 2 an object,
        // 0 an animal or a person)
        const int kind = this->hd_dir.rfind("fences/", 0) == 0    ? 1
                         : this->hd_dir.rfind("objects/", 0) == 0 ? 2
                                                                  : 0;
        t = Shaders::applyArt(renderer, big, kind, surface->w, surface->h);
        SDL_FreeSurface(big);
      }
    }
    if (!t && magnified && !this->hd_dir.empty()) {
      std::string hdPath =
          this->hd_dir + "/" + direction_string + "_" + std::to_string(frame);
      if (SDL_Surface *hd = ArtScaler::loadHdSurface(hdPath)) {
        if (!this->recoloured.empty())
          hd = HdArt::recolour(hd, surface, this->recoloured, direction_string, frame);
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

  // (No clamp: the frame header's offset_y holds the x anchor, half the
  // width for centred art, so a cap of 200 shifted every frame wider than
  // 400 px up - the Zoo Status panel's art by 50 rows)
  *offset_x = (int16_t)raw_x;
  *offset_y = (int16_t)raw_y;
}

void Animation::loadSurfaces(std::string direction_string,
                             AnimationData *data) {
  if (data == nullptr || data->frame_count == 0)
    return;
  this->frame_time_in_ms = data->frame_time_in_ms;
  this->has_background = data->has_background;
  this->frame_count = std::max(this->frame_count, static_cast<int>(data->frame_count));
  // The .ani's box (x0..x1, y0..y1) is the surface's size - the game's own
  // art's box holds its frames. Art made with APE (an old fan download: RDingFT's
  // rock cliffs) can say a box far smaller than its frames (22 x 13 for
  // a 251 x 212 cliff), and each frame was clipped to almost nothing: grown,
  // round the same centre, to hold the biggest frame
  if (!data->has_background)
    for (uint32_t i = 0; i < data->frame_count; i++) {
      const AnimationFrameData &f = data->frames[i];
      int needW = f.width + 2 * std::abs(static_cast<int>(f.offset_y) - f.width / 2) + 2;
      int needH = f.height + 2 * std::abs(static_cast<int>(f.offset_x) - f.height / 2) + 2;
      if (data->width < needW && f.width > data->width)
        data->width = needW;
      if (data->height < needH && f.height > data->height)
        data->height = needH;
    }

  int16_t offset_x = 0;
  int16_t offset_y = 0;
  calculateOffset(data, &offset_x, &offset_y);

  this->surfaces[direction_string] = std::vector<SDL_Surface *>();

  // A building with a background frame (zoo.exe's FATZ flag): every frame
  // is the background with that frame drawn over it, each placed by its
  // anchor (the header's offset_y the x anchor, offset_x the y) in a
  // surface big enough for them all
  if (data->has_background) {
    const int count = static_cast<int>(data->frame_count);
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    // (the frame header's offset_x is its y anchor, offset_y its x, as in
    // art without a background: read the other way round, each building
    // sat off its footprint by a different amount in each view - the
    // restaurant's sign on the ground, the DRT gift shop 107 px out)
    for (int i = 0; i <= count; i++) {
      const AnimationFrameData &f = data->frames[i];
      int l = -f.offset_y, t = -f.offset_x;
      if (i == 0 || l < minX) minX = l;
      if (i == 0 || t < minY) minY = t;
      if (i == 0 || l + f.width > maxX) maxX = l + f.width;
      if (i == 0 || t + f.height > maxY) maxY = t + f.height;
    }
    const int W = std::max(1, maxX - minX), H = std::max(1, maxY - minY);
    const SDL_Point anchor = {-minX, -minY};
    this->anchor_points[direction_string] = anchor;
    this->anchor_offsets[direction_string] = {W / 2 - anchor.x, H / 2 - anchor.y};
    auto paint = [&](SDL_Surface *s, const AnimationFrameData &f) {
      if (!f.lines)
        return;
      const int left = anchor.x - f.offset_y, top = anchor.y - f.offset_x;
      uint32_t *pixels = static_cast<uint32_t *>(s->pixels);
      const int pitch = s->pitch / 4;
      for (int y = 0; y < f.height; y++) {
        int x = left;
        if (!f.lines[y].instructions)
          continue;
        for (int k = 0; k < f.lines[y].instruction_count; k++) {
          const auto &ins = f.lines[y].instructions[k];
          x += ins.offset;
          for (int p = 0; p < ins.color_count; p++, x++) {
            int yy = y + top;
            if (x < 0 || x >= W || yy < 0 || yy >= H)
              continue;
            uint32_t color = 0xFF000000;
            if (!f.is_shadow) {
              if (!data->pallet || !data->pallet->colors || !ins.colors)
                continue;
              color = data->pallet->colors[ins.colors[p]] | 0xFF000000;
            }
            pixels[yy * pitch + x] = color;
          }
        }
      }
    };
    for (int i = 0; i < count; i++) {
      SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 0, SDL_PIXELFORMAT_RGBA32);
      if (s) {
        SDL_FillRect(s, NULL, 0x00000000);
        paint(s, data->frames[count]);
        paint(s, data->frames[i]);
      }
      this->surfaces[direction_string].push_back(s);
    }
    AniFile::freeAnimationData(data);
    return;
  }

  // Keep the original placement, expanding the canvas in every direction
  // needed by later frames. Airborne flips can rise above frame 0 without
  // being taller than the declared box; growing its height alone cropped them.
  const AnimationFrameData &first = data->frames[0];
  int frameAnchorX=offset_x-first.offset_x+first.offset_y;
  int frameAnchorY=offset_y-first.offset_y+first.offset_x;
  int minX=0,minY=0,maxX=data->width,maxY=data->height;
  for(uint32_t i=0;i<data->frame_count;++i) {
    const auto &f=data->frames[i];
    const int x=frameAnchorX-f.offset_y,y=frameAnchorY-f.offset_x;
    minX=std::min(minX,x);minY=std::min(minY,y);
    maxX=std::max(maxX,x+f.width);maxY=std::max(maxY,y+f.height);
  }
  if(maxX-minX>65535 || maxY-minY>65535) {
    AniFile::freeAnimationData(data);return;
  }
  data->width=std::max(1,maxX-minX);data->height=std::max(1,maxY-minY);
  frameAnchorX-=minX;frameAnchorY-=minY;
  this->anchor_points[direction_string]={frameAnchorX,frameAnchorY};
  this->anchor_offsets[direction_string]={first.width/2-frameAnchorX,first.height/2-frameAnchorY};

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

    // Each frame placed by its own anchor (the header's offset_x is its y
    // anchor, offset_y its x anchor), lined up on frame 0's: frames whose
    // anchors differ (a zebra's walk: x anchor 11 to 15) no longer shift
    // about (they shook)
    const int anchorX = frameAnchorX;
    const int anchorY = frameAnchorY;
    // (art with a background frame, the UI's, kept as it was: matched)
    const int left = data->has_background ? offset_x - data->frames[i].offset_x
                                          : anchorX - data->frames[i].offset_y;
    const int top = data->has_background ? offset_y - data->frames[i].offset_y
                                         : anchorY - data->frames[i].offset_x;
    for (int y = 0; y < data->frames[i].height; y++) {
      int x = left;
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
          int y_final = y + top;
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
