#include "Animation.hpp"
#include <algorithm>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <iterator>
#include <cmath>
#include <cstring>
#include "AniFile.hpp"
#include "ArtScaler.hpp"
#include "HdArt.hpp"
#include "ShaderMods.hpp"
#include "RenderSettings.hpp"
#include "Features.hpp"
#include <SDL3/SDL.h>
#include <assert.h>
#include <cstdio>
#include <cstdlib> // for abs

// addition: a shadow-free copy may be magnified or replaced by HD art. Use
// the original palette mask, so upscaling cannot bring the old shadow back.
// addition: retain source palette indices as compact runs. RGB black may
// also be genuine pigment; cUsesRealShadows/cHasShadowImages selects that
// policy for species with separate shadow artwork in their original .ai.
static std::vector<uint64_t> compressShadow(const std::vector<Uint8> &mask) {
  std::vector<uint64_t> runs;
  for(size_t i=0;i<mask.size();) {
    if(!mask[i]){++i;continue;}const size_t start=i;while(i<mask.size() && mask[i])++i;
    runs.push_back(uint64_t(start)<<32 | uint32_t(i-start));
  }
  return runs;
}
namespace {
void liveAdd(const Animation *a); // (memoryStats)
void liveRemove(const Animation *a);
} // namespace

static void removePaletteShadow(SDL_Surface *target,const SDL_Surface *original,const std::vector<Uint8> &mask) {
  if(!target || !original || mask.empty() || SDL_BYTESPERPIXEL(target->format)!=4)return;
  for(int y=0;y<target->h;++y)for(int x=0;x<target->w;++x) {
    const int ox=x*original->w/target->w,oy=y*original->h/target->h;
    if(mask[size_t(oy)*original->w+ox])reinterpret_cast<Uint32*>(static_cast<Uint8*>(target->pixels)+y*target->pitch)[x]=0;
  }
}

Animation::Animation(std::unordered_map<std::string, AnimationData *> *data)
    : current_frame(0), last_direction(CompassDirection::S),
      renderer_flip(SDL_FLIP_NONE), has_background(false),
      frame_time_in_ms(100), frame_start_time(0) {
  liveAdd(this);
  // (one view named for itself - the dust ball's "animation = dust" - is
  // its N: art the same from every side)
  static const char *compass[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW", "G", "H"};
  bool single = data->size() == 1 &&
                std::none_of(std::begin(compass), std::end(compass),
                             [&](const char *c) { return data->begin()->first == c; });
  for (auto map_entry : *data) {
    this->loadSurfaces(single ? std::string("N") : map_entry.first, map_entry.second);
  }
  packAll();
}

Animation::Animation(SDL_Surface *single_frame)
    : current_frame(0), last_direction(CompassDirection::S),
      renderer_flip(SDL_FLIP_NONE), has_background(false),
      frame_time_in_ms(100), frame_start_time(0) {
  liveAdd(this);
  if (single_frame) {
    // Duplicate the surface so we own the memory
    SDL_Surface *copy = SDL_DuplicateSurface(single_frame);
    if (copy) {
      this->surfaces["N"].push_back(copy);
    }
  }
  packAll();
}

namespace {
// ours: the animations holding zoomed-in sets, their total size, and the
// frame count (trimZoomedIn)
std::unordered_set<Animation *> g_hiHolders;
size_t g_hiBytes = 0;
uint32_t g_hiFrame = 1;
bool g_hiGradual = true;
double g_hiSpentMs = 0; // (making zoomed-in sets, this frame)
// ours: the HD level for the zoom (the window's scale in it): 2x up to a
// little past 2, 4x from a little further (between, as it was, so it
// doesn't flick to and fro), and a count moved on when it changes
int g_hdLevel = 2;
uint32_t g_levelGen = 1;
std::atomic<uint64_t> g_nextLoaderId{1};
// (1.5 GB of pixels: a busy zoo's view zoomed in with HD art needs well
// under it; ZT_HD_BUDGET_MB sets another, for checks)
size_t hiBudget() {
  static const size_t budget = [] {
    const char *mb = std::getenv("ZT_HD_BUDGET_MB");
    return (mb && std::atoi(mb) > 0 ? size_t(std::atoi(mb)) : size_t(1536)) << 20;
  }();
  return budget;
}
} // namespace

uint64_t Animation::nextLoaderId() { return g_nextLoaderId++; }

namespace {
// ours: every animation alive (memoryStats); one may be made off the main
// thread (the background preload)
std::mutex g_liveMutex;
std::unordered_set<const Animation *> g_live;
void liveAdd(const Animation *a) {
  std::lock_guard<std::mutex> lock(g_liveMutex);
  g_live.insert(a);
}
void liveRemove(const Animation *a) {
  std::lock_guard<std::mutex> lock(g_liveMutex);
  g_live.erase(a);
}
size_t textureBytes(const std::unordered_map<std::string, std::vector<SDL_Texture *>> &set) {
  size_t n = 0;
  for (const auto &[key, frames] : set)
    for (SDL_Texture *t : frames)
      if (t) {
        int w = 0, h = 0;
        n += ArtScaler::textureBytes(t);
      }
  return n;
}
} // namespace

Animation::Animation() { liveAdd(this); }

std::string Animation::memoryStats() {
  std::lock_guard<std::mutex> lock(g_liveMutex);
  size_t surfaces = 0, frames = 0, normal = 0, lit = 0, hi = 0;
  for (const Animation *a : g_live) {
    for (const auto &[key, list] : a->surfaces)
      for (const SDL_Surface *s : list)
        if (s) {
          surfaces += size_t(s->pitch) * s->h;
          frames++;
        }
    normal += textureBytes(a->textures);
    lit += textureBytes(a->textures_lit);
    hi += textureBytes(a->textures_hi) + textureBytes(a->textures_lit_hi);
  }
  size_t packedBytes = 0, packedFrames = 0;
  for (const Animation *a : g_live)
    for (const auto &[key, pd] : a->packed) {
      packedBytes += pd.colours.size() * 4;
      for (const PackedFrame &f : pd.frames) {
        packedBytes += f.data.capacity();
        packedFrames += f.present ? 1 : 0;
      }
    }
  char out[320];
  std::snprintf(out, sizeof out, "%zu animations; source frames packed %zu (%zu MB), unpacked %zu (%zu MB); textures: normal %zu MB, lit %zu MB, zoomed-in %zu MB",
                g_live.size(), packedFrames, packedBytes >> 20, frames, surfaces >> 20, normal >> 20, lit >> 20, hi >> 20);
  return out;
}

void Animation::releaseHi() {
  HdArt::cancelFrames(loaderId); // (its frames on their way: not wanted now)
  hiLevel.clear();
  for (auto *set : {&textures_hi, &textures_lit_hi}) {
    for (auto &kv : *set)
      for (SDL_Texture *t : kv.second)
        if (t)
          SDL_DestroyTexture(t);
    set->clear();
  }
  g_hiBytes -= std::min(g_hiBytes, hi_bytes);
  hi_bytes = 0;
  g_hiHolders.erase(this);
  ++textureGen;
}

void Animation::setGradualZoomedIn(bool on) { g_hiGradual = on; }

void Animation::releaseAllTextures() {
  std::vector<Animation *> all;
  {
    std::lock_guard<std::mutex> lock(g_liveMutex);
    for (const Animation *a : g_live)
      all.push_back(const_cast<Animation *>(a));
  }
  for (Animation *a : all) {
    for (auto &kv : a->textures)
      for (SDL_Texture *t : kv.second)
        if (t)
          SDL_DestroyTexture(t);
    a->textures.clear();
    a->releaseHi();
    a->releaseLighting();
  }
}

void Animation::trimZoomedIn() {
  ++g_hiFrame;
  g_hiSpentMs = 0;
  HdArt::loaderTick();
  trimSources();
  const float zoom = RenderSettings::lastWorldZoom;
  const int level = zoom >= 2.4f ? 4 : zoom <= 2.1f ? 2 : g_hdLevel;
  if (level != g_hdLevel) {
    g_hdLevel = level;
    ++g_levelGen;
  }
  if (g_hiBytes <= hiBudget())
    return;
  // (oldest first, down to three quarters of the budget, so it isn't
  // trimming again every frame)
  std::vector<Animation *> idle;
  for (Animation *a : g_hiHolders)
    if (a->hi_drawn + 1 < g_hiFrame)
      idle.push_back(a);
  std::sort(idle.begin(), idle.end(), [](const Animation *a, const Animation *b) { return a->hi_drawn < b->hi_drawn; });
  for (Animation *a : idle) {
    if (g_hiBytes <= hiBudget() / 4 * 3)
      break;
    a->releaseHi();
  }
}

size_t Animation::zoomedInBytes() { return g_hiBytes; }

namespace {
// ours: the animations holding unpacked source frames, and their size; the
// budget for them (512 MB: what's on the screen and near it; ZT_SOURCE_
// BUDGET_MB sets another, for checks); and who's told when one's let go
std::unordered_set<const Animation *> g_srcHolders;
size_t g_srcBytes = 0;
size_t sourceBudget() {
  static const size_t budget = [] {
    const char *mb = std::getenv("ZT_SOURCE_BUDGET_MB");
    return (mb && std::atoi(mb) > 0 ? size_t(std::atoi(mb)) : size_t(512)) << 20;
  }();
  return budget;
}
std::function<void(SDL_Surface *)> g_frameFreed;
const void *g_frameFreedOwner = nullptr;
void put16(std::vector<uint8_t> &out, unsigned v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>(v >> 8));
}
unsigned get16(const uint8_t *&p) {
  const unsigned v = p[0] | (p[1] << 8);
  p += 2;
  return v;
}
} // namespace

void Animation::setFrameFreedHook(const void *owner, std::function<void(SDL_Surface *)> hook) {
  if (!hook && owner != g_frameFreedOwner)
    return; // (another's since)
  g_frameFreed = std::move(hook);
  g_frameFreedOwner = g_frameFreed ? owner : nullptr;
}

// Each direction's frames packed, its own unpacked ones freed (made again
// when asked for). A row: its runs' count, then each run's clear gap and
// length, and its colours (a byte each, the table's indices, or four).
// Only RGBA32 frames are packed (the .ani's own); any other kept as it is.
// ours: a direction's colours (at most 256, never 0 - clear) to their index:
// a small fixed table, remembering the last colour asked for (runs of one
// colour are common). A general hash map here, asked twice a pixel, was most
// of an animation's loading time - a hitch whenever an animal first did
// something (Animals::art)
namespace {
struct ColourTable {
  uint32_t key[1024] = {};
  uint8_t value[1024] = {};
  uint32_t lastKey = 0;
  int lastValue = -1, size = 0;
  int find(uint32_t k) {
    if (k == lastKey && lastValue >= 0)
      return lastValue;
    for (unsigned h = (k * 2654435761u) >> 22;; h = (h + 1) & 1023) {
      if (!key[h])
        return -1;
      if (key[h] == k) {
        lastKey = k;
        lastValue = value[h];
        return lastValue;
      }
    }
  }
  void add(uint32_t k, uint8_t v) {
    unsigned h = (k * 2654435761u) >> 22;
    while (key[h])
      h = (h + 1) & 1023;
    key[h] = k;
    value[h] = v;
    ++size;
    lastKey = k;
    lastValue = v;
  }
};
} // namespace

void Animation::packAll() {
  for (auto &[direction, frames] : this->surfaces) {
    if (this->packed.count(direction))
      continue; // (packed as it loaded: loadSurfaces)
    PackedDirection pd;
    bool packable = !frames.empty();
    ColourTable index;
    for (SDL_Surface *s : frames) {
      if (!s)
        continue;
      if (s->format != SDL_PIXELFORMAT_RGBA32 || s->w > 65535 || SDL_MUSTLOCK(s)) {
        packable = false;
        break;
      }
      for (int y = 0; y < s->h && pd.indexed; y++) {
        const uint32_t *row = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(s->pixels) + size_t(y) * s->pitch);
        for (int x = 0; x < s->w; x++)
          if (row[x] && index.find(row[x]) < 0) {
            if (index.size == 256) {
              pd.indexed = false;
              break;
            }
            index.add(row[x], static_cast<uint8_t>(index.size));
            pd.colours.push_back(row[x]);
          }
      }
    }
    if (!packable)
      continue;
    if (!pd.indexed)
      pd.colours.clear();
    for (SDL_Surface *s : frames) {
      PackedFrame f;
      if (s) {
        f.present = true;
        f.w = s->w;
        f.h = s->h;
        for (int y = 0; y < s->h; y++) {
          const uint32_t *row = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(s->pixels) + size_t(y) * s->pitch);
          const size_t countAt = f.data.size();
          put16(f.data, 0);
          unsigned runs = 0;
          for (int x = 0; x < s->w;) {
            const int gapFrom = x;
            while (x < s->w && !row[x])
              x++;
            if (x == s->w)
              break;
            const int runFrom = x;
            while (x < s->w && row[x])
              x++;
            put16(f.data, static_cast<unsigned>(runFrom - gapFrom));
            put16(f.data, static_cast<unsigned>(x - runFrom));
            for (int k = runFrom; k < x; k++) {
              if (pd.indexed) {
                f.data.push_back(static_cast<uint8_t>(index.find(row[k])));
              } else {
                const uint32_t v = row[k];
                const uint8_t *b = reinterpret_cast<const uint8_t *>(&v);
                f.data.insert(f.data.end(), b, b + 4);
              }
            }
            runs++;
          }
          f.data[countAt] = static_cast<uint8_t>(runs & 0xFF);
          f.data[countAt + 1] = static_cast<uint8_t>(runs >> 8);
        }
        f.data.shrink_to_fit();
      }
      pd.frames.push_back(std::move(f));
    }
    for (SDL_Surface *&s : frames)
      if (s) {
        SDL_DestroySurface(s);
        s = nullptr;
      }
    this->packed[direction] = std::move(pd);
  }
}

SDL_Surface *Animation::sourceFrame(const std::string &direction, size_t frame) const {
  auto slots = this->surfaces.find(direction);
  if (slots == this->surfaces.end() || frame >= slots->second.size())
    return nullptr;
  this->src_used = g_hiFrame;
  if (SDL_Surface *s = slots->second[frame])
    return s;
  auto pd = this->packed.find(direction);
  if (pd == this->packed.end() || frame >= pd->second.frames.size() || !pd->second.frames[frame].present)
    return nullptr;
  const PackedFrame &f = pd->second.frames[frame];
  SDL_Surface *s = SDL_CreateSurface(f.w, f.h, SDL_PIXELFORMAT_RGBA32);
  if (!s)
    return nullptr;
  SDL_FillSurfaceRect(s, nullptr, 0);
  const uint8_t *p = f.data.data();
  for (int y = 0; y < f.h; y++) {
    uint32_t *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(s->pixels) + size_t(y) * s->pitch);
    const unsigned runs = get16(p);
    int x = 0;
    for (unsigned r = 0; r < runs; r++) {
      x += static_cast<int>(get16(p));
      const unsigned length = get16(p);
      for (unsigned k = 0; k < length; k++, x++) {
        if (pd->second.indexed) {
          row[x] = pd->second.colours[*p++];
        } else {
          std::memcpy(&row[x], p, 4);
          p += 4;
        }
      }
    }
  }
  slots->second[frame] = s;
  const size_t bytes = size_t(s->pitch) * s->h;
  this->src_bytes += bytes;
  g_srcBytes += bytes;
  g_srcHolders.insert(this);
  return s;
}

bool Animation::sourceSize(const std::string &direction, size_t frame, int &w, int &h) const {
  auto slots = this->surfaces.find(direction);
  if (slots == this->surfaces.end() || frame >= slots->second.size())
    return false;
  if (const SDL_Surface *s = slots->second[frame]) {
    w = s->w;
    h = s->h;
    return true;
  }
  auto pd = this->packed.find(direction);
  if (pd == this->packed.end() || frame >= pd->second.frames.size() || !pd->second.frames[frame].present)
    return false;
  w = pd->second.frames[frame].w;
  h = pd->second.frames[frame].h;
  return true;
}

// Its unpacked frames that are packed too, freed (the others - not
// packable - kept: they're the only copy)
void Animation::releaseSources() const {
  for (auto &[direction, frames] : this->surfaces) {
    auto pd = this->packed.find(direction);
    if (pd == this->packed.end())
      continue;
    for (size_t i = 0; i < frames.size(); i++)
      if (frames[i] && i < pd->second.frames.size() && pd->second.frames[i].present) {
        if (g_frameFreed)
          g_frameFreed(frames[i]);
        SDL_DestroySurface(frames[i]);
        frames[i] = nullptr;
      }
  }
  g_srcBytes -= std::min(g_srcBytes, this->src_bytes);
  this->src_bytes = 0;
  g_srcHolders.erase(this);
}

// A direction's frames gone for good, unpacked and packed (UI art, once
// its textures are made: as before, it doesn't keep them)
void Animation::dropSources(const std::string &direction) {
  auto slots = this->surfaces.find(direction);
  if (slots == this->surfaces.end())
    return;
  for (SDL_Surface *s : slots->second)
    if (s) {
      const size_t bytes = std::min(this->src_bytes, size_t(s->pitch) * s->h);
      if (this->packed.count(direction)) {
        this->src_bytes -= bytes;
        g_srcBytes -= std::min(g_srcBytes, bytes);
      }
      if (g_frameFreed)
        g_frameFreed(s);
      SDL_DestroySurface(s);
    }
  slots->second.clear();
  this->packed.erase(direction);
}

void Animation::trimSources() {
  if (g_srcBytes <= sourceBudget())
    return;
  // (longest unused first, down to three quarters; none asked for in the
  // last frame - something may be holding it)
  std::vector<const Animation *> idle;
  for (const Animation *a : g_srcHolders)
    if (a->src_used + 1 < g_hiFrame)
      idle.push_back(a);
  std::sort(idle.begin(), idle.end(), [](const Animation *a, const Animation *b) { return a->src_used < b->src_used; });
  for (const Animation *a : idle) {
    if (g_srcBytes <= sourceBudget() / 4 * 3)
      break;
    a->releaseSources();
  }
}


Animation::Animation(Animation &&other) noexcept {
  liveAdd(this);
  *this = std::move(other);
}

Animation &Animation::operator=(Animation &&other) noexcept {
  if (this != &other) {
    releaseHi();
    releaseLighting();
    releaseSources();
    for (AnchorMemo &m : this->anchorMemo)
      m = AnchorMemo{};
    textures_lit=std::move(other.textures_lit);textures_lit_hi=std::move(other.textures_lit_hi);
    emissions=std::move(other.emissions);paletteShadows=other.paletteShadows;blackIsBody=other.blackIsBody;shadowRuns=std::move(other.shadowRuns);
    other.textures_lit.clear();other.textures_lit_hi.clear();other.emissions.clear();
    for (auto surface_list : this->surfaces) {
      for (SDL_Surface *surface : surface_list.second) {
        if (surface)
          SDL_DestroySurface(surface);
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
    // (its zoomed-in sets came too: counted as this one's now, with their
    // levels and the frames on their way to them)
    this->hi_drawn = other.hi_drawn;
    std::swap(this->loaderId, other.loaderId);
    this->hiLevel = std::move(other.hiLevel);
    other.hiLevel.clear();
    if (other.hi_bytes) {
      g_hiHolders.erase(&other);
      g_hiHolders.insert(this);
      this->hi_bytes = other.hi_bytes;
      other.hi_bytes = 0;
    }
    // (and its packed frames, with their unpacked ones' count)
    this->packed = std::move(other.packed);
    other.packed.clear();
    this->src_used = other.src_used;
    if (other.src_bytes) {
      g_srcHolders.erase(&other);
      g_srcHolders.insert(this);
      this->src_bytes = other.src_bytes;
      other.src_bytes = 0;
    }
  }
  return *this;
}

Animation::~Animation() {
  liveRemove(this);
  releaseSources();
  releaseHi();
  releaseLighting();
  if (onRelease) onRelease();
  for (auto surface_list : this->surfaces) {
    for (SDL_Surface *surface : surface_list.second) {
      if (surface)
        SDL_DestroySurface(surface);
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
    if (t)
      ArtScaler::querySize(t, &rect.w, &rect.h);
    else if (!this->sourceSize(direction_string, this->current_frame, rect.w, rect.h))
      return; // (one not made yet - makeLazyFrame - measured from its source)
  } else {
    direction_string = convertCompassDirectionToExistingAnimationString(
        direction, this->surfaces);

    if (this->surfaces.find(direction_string) == this->surfaces.end() ||
        this->surfaces[direction_string].empty()) {
      return;
    }

    if ((size_t)this->current_frame >= this->surfaces[direction_string].size())
      this->current_frame = 0;

    if (!this->sourceSize(direction_string, this->current_frame, rect.w, rect.h))
      return;
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
      this->frameTextures(renderer, direction, hi, this->current_frame);
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
    // (a texture made up to whole blocks: only the art's part of it)
    SDL_Rect content;
    const bool padded = ArtScaler::contentRect(texture, &content);
    ZT_RenderCopyEx(renderer, texture, padded ? &content : NULL, dest_rect, 0, NULL,
                     this->renderer_flip);
  }
}

// ours: convertCompassDirectionToExistingAnimationString over anchor_points
// and the point it finds, once per direction (nullptr: none - renderer_flip
// left as it leaves it)
const Animation::AnchorMemo *Animation::anchorFor(CompassDirection direction) {
  const int di = static_cast<int>(direction);
  if (di < 0 || di >= 10) {
    std::string key = convertCompassDirectionToExistingAnimationString(direction, this->anchor_points);
    if (key.empty())
      return nullptr;
    static AnchorMemo once;
    once = {1, this->renderer_flip, this->anchor_points[key]};
    return &once;
  }
  AnchorMemo &m = this->anchorMemo[di];
  if (!m.state) {
    std::string key = convertCompassDirectionToExistingAnimationString(direction, this->anchor_points);
    m = key.empty() ? AnchorMemo{-1, SDL_FLIP_NONE, {}} : AnchorMemo{1, this->renderer_flip, this->anchor_points[key]};
  }
  this->renderer_flip = m.flip;
  return m.state > 0 ? &m : nullptr;
}

bool Animation::drawAnchored(SDL_Renderer *renderer, float x, float y,
                             CompassDirection direction, const SDL_Color *tint,
                             int frame) {
  if (!renderer)
    return false;
  // (sets renderer_flip when a mirrored direction stands in)
  const AnchorMemo *anchor = this->anchorFor(direction);
  if (!anchor)
    return false;
  SDL_FlipMode flip = this->renderer_flip;
  bool hi = this->usesHi();
  std::vector<SDL_Texture *> *frames = this->frameTextures(renderer, direction, hi, frame >= 0 ? frame : this->current_frame);
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
  SDL_Point a = anchor->anchor;
  float ax = flip == SDL_FLIP_HORIZONTAL ? static_cast<float>(w - a.x) : a.x;
  SDL_FRect dest = {x - ax, y - a.y, static_cast<float>(w), static_cast<float>(h)};
  if (RenderSettings::snapEdges) {
    // (each edge on the screen pixel its own position rounds to)
    float sx = 1.0f, sy = 1.0f;
    SDL_GetRenderScale(renderer, &sx, &sy);
    float l = SDL_roundf(dest.x * sx) / sx, t = SDL_roundf(dest.y * sy) / sy;
    float r = SDL_roundf((dest.x + dest.w) * sx) / sx, b = SDL_roundf((dest.y + dest.h) * sy) / sy;
    dest = {l, t, r - l, b - t};
  }
  RenderSettings::applyArtScaleMode(texture);
  if (tint) {
    SDL_SetTextureColorMod(texture, tint->r, tint->g, tint->b);
    SDL_SetTextureAlphaMod(texture, tint->a);
  }
  SDL_Rect content;
  const bool padded = ArtScaler::contentRect(texture, &content);
  ZT_RenderCopyExF(renderer, texture, padded ? &content : nullptr, &dest, 0, nullptr, flip);
  if (tint) {
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
  }
  if(RenderSettings::relightWorld && Features::dayNightLighting && RenderSettings::fluorescence)
    RenderSettings::fluorescence(renderer,this,x,y,direction,int(index));
  return true;
}

Animation::LightingFrame Animation::lightingFrame(CompassDirection side,int frame) {
  const auto key=convertCompassDirectionToExistingAnimationString(side,surfaces);
  const auto found=surfaces.find(key);
  if(found==surfaces.end() || found->second.empty())return {};
    const size_t index=std::max(0,frame)%found->second.size();
  const auto masks=shadowRuns.find(key);const std::vector<uint64_t> *mask=!blackIsBody && masks!=shadowRuns.end() && index<masks->second.size()?&masks->second[index]:nullptr;
  return {sourceFrame(key,index),anchor_points[key],renderer_flip==SDL_FLIP_HORIZONTAL,mask};
}

bool Animation::drawMapped(SDL_Renderer *renderer, CompassDirection direction, int frame, const SDL_FPoint dst[4],
                           const SDL_FPoint uv[4], SDL_Color tint) {
  return drawMapped(renderer,direction,frame,dst,uv,tint,false);
}

std::string Animation::lightingDirection(CompassDirection side) {
  return convertCompassDirectionToExistingAnimationString(side,surfaces);
}

bool Animation::drawMapped(SDL_Renderer *renderer, CompassDirection direction, int frame, const SDL_FPoint dst[4],
                           const SDL_FPoint uv[4], SDL_Color tint, bool anchoredMirror) {
  if (!renderer)
    return false;
  if (!this->anchorFor(direction))
    return false;
  bool hi = this->usesHi();
  std::vector<SDL_Texture *> *frames = this->frameTextures(renderer, direction, hi, frame >= 0 ? frame : 0);
  if (!frames || frames->empty())
    return false;
  size_t index = frame >= 0 ? static_cast<size_t>(frame) % frames->size() : 0;
  SDL_Texture *texture = (*frames)[index];
  if (!texture)
    return false;
  RenderSettings::applyArtScaleMode(texture);
  ZT_Vertex v[4];
  for (int i = 0; i < 4; i++)
    v[i] = {dst[i], tint, uv[i]};
  // addition: silhouette shadows follow the same missing-direction mirror as
  // drawAnchored. Existing mapped water art keeps its own supplied UVs.
  if (anchoredMirror && this->renderer_flip == SDL_FLIP_HORIZONTAL)
    for (auto &vertex : v) vertex.tex_coord.x = 1.0f - vertex.tex_coord.x;
  float um = 1.0f, vm = 1.0f; // (a texture made up to whole blocks: its art's part)
  if (ArtScaler::contentUV(texture, &um, &vm))
    for (auto &vertex : v) {
      vertex.tex_coord.x *= um;
      vertex.tex_coord.y *= vm;
    }
  const int idx[6] = {0, 1, 2, 0, 2, 3};
  ZT_RenderGeometry(renderer, texture, v, 4, idx, 6);
  return true;
}

bool Animation::drawCastShadow(SDL_Renderer *renderer, CompassDirection direction, int frame,
                               const std::function<SDL_FPoint(float across, float up)> &place, Uint8 alpha,
                               Uint8 tipAlpha) {
  if (!renderer)
    return false;
  const AnchorMemo *anchor = this->anchorFor(direction);
  if (!anchor)
    return false;
  const SDL_FlipMode flip = this->renderer_flip;
  std::vector<SDL_Texture *> *frames = this->frameTextures(renderer, direction, this->usesHi(), frame >= 0 ? frame : 0);
  if (!frames || frames->empty())
    return false;
  SDL_Texture *texture = (*frames)[frame >= 0 ? static_cast<size_t>(frame) % frames->size() : 0];
  if (!texture)
    return false;
  int w = 0, h = 0;
  ArtScaler::querySize(texture, &w, &h);
  const float ax = flip == SDL_FLIP_HORIZONTAL ? static_cast<float>(w - anchor->anchor.x) : anchor->anchor.x;
  const float left = -ax, top = static_cast<float>(anchor->anchor.y), right = left + w, bottom = top - h;
  auto cast = [&](float across, float up) { return place(across, up); };
  const SDL_Color black{0, 0, 0, alpha}, tip{0, 0, 0, tipAlpha};
  float um = 1.0f, vm = 1.0f; // (a texture made up to whole blocks: its art's part)
  ArtScaler::contentUV(texture, &um, &vm);
  const float u0 = flip == SDL_FLIP_HORIZONTAL ? um : 0.0f, u1 = um - u0;
  ZT_Vertex v[4] = {{cast(left, top), tip, {u0, 0}}, {cast(right, top), tip, {u1, 0}},
                     {cast(right, bottom), black, {u1, vm}}, {cast(left, bottom), black, {u0, vm}}};
  const int idx[6] = {0, 1, 2, 0, 2, 3};
  RenderSettings::applyArtScaleMode(texture);
  return ZT_RenderGeometry(renderer, texture, v, 4, idx, 6) == 0;
}

bool Animation::hitTestAnchored(float px,float py,float x,float y,CompassDirection direction,int frame) {
  const std::string key=convertCompassDirectionToExistingAnimationString(direction,this->surfaces);
  const bool mirrored=this->renderer_flip==SDL_FLIP_HORIZONTAL;
  auto frames=this->surfaces.find(key);auto anchor=this->anchor_points.find(key);
  if(frames==this->surfaces.end() || frames->second.empty() || anchor==this->anchor_points.end())return false;
  const int index=(frame>=0?frame:static_cast<int>(this->current_frame))%static_cast<int>(frames->second.size());
  SDL_Surface *source=sourceFrame(key,static_cast<size_t>(index));if(!source)return false;
  int sx=static_cast<int>(std::floor(px-x+(mirrored?source->w-anchor->second.x:anchor->second.x)));
  const int sy=static_cast<int>(std::floor(py-y+anchor->second.y));
  if(sx<0 || sy<0 || sx>=source->w || sy>=source->h)return false;
  if(mirrored)sx=source->w-1-sx;
  Uint32 pixel=0;std::memcpy(&pixel,static_cast<Uint8*>(source->pixels)+sy*source->pitch+sx*SDL_BYTESPERPIXEL(source->format),SDL_BYTESPERPIXEL(source->format));
  Uint8 r,g,b,a;ZT_GetRGBA(pixel,source->format,&r,&g,&b,&a);
  if(Features::dayNightLighting && world_art && !blackIsBody && paletteShadows) {
    auto masks=shadowRuns.find(key);if(masks!=shadowRuns.end() && size_t(index)<masks->second.size()) {
      const uint32_t at=uint32_t(sy*source->w+sx);
      for(uint64_t run:masks->second[index]){const uint32_t start=uint32_t(run>>32),count=uint32_t(run);if(start>at)break;if(at-start<count)return false;}
    }
  }
  return a>=32;
}

bool Animation::anchoredBounds(float x, float y, CompassDirection direction, float &left, float &top,
                               float &right, float &bottom) {
  const AnchorMemo *anchor = this->anchorFor(direction);
  if (!anchor)
    return false;
  SDL_FlipMode flip = this->renderer_flip;
  SDL_Point a = anchor->anchor;
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

// ours: a plain map-art frame (not magnified, not HD) made the first time
// it's drawn, as the whole direction's were before (Animation::frameTextures)
void Animation::makeLazyFrame(SDL_Renderer *renderer, std::vector<SDL_Texture *> &frames, const std::string &direction,
                              bool relight, int wanted) {
  if (wanted < 0 || frames.empty())
    return;
  const size_t index = static_cast<size_t>(wanted) % frames.size();
  if (frames[index])
    return;
  SDL_Surface *surface = this->sourceFrame(direction, index);
  if (!surface)
    return;
  SDL_Surface *clean = nullptr;
  if (relight) {
    std::vector<Uint8> shadowMask(size_t(surface->w) * surface->h, 0);
    auto masks = shadowRuns.find(direction);
    if (masks != shadowRuns.end() && index < masks->second.size())
      for (uint64_t run : masks->second[index]) {
        const size_t start = size_t(run >> 32), count = uint32_t(run);
        if (start + count <= shadowMask.size())
          std::fill_n(shadowMask.begin() + start, count, Uint8(1));
      }
    clean = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    if (clean)
      removePaletteShadow(clean, surface, shadowMask);
  }
  SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, clean ? clean : surface);
  if (!t)
    SDL_Log("Warning: Failed to create texture: %s", SDL_GetError());
  frames[index] = t;
  if (clean)
    SDL_DestroySurface(clean);
}

std::vector<SDL_Texture *> *Animation::frameTextures(SDL_Renderer *renderer,
                                                     CompassDirection direction,
                                                     bool hi, int wanted) {
  // (an HD art mod switched on or off since they were made: made again)
  // (or a shader mod's art shader changed)
  if (hi && this->hi_generation != HdArt::generation() * 100000 + Shaders::generation()) {
    this->releaseHi();
    this->hi_generation = HdArt::generation() * 100000 + Shaders::generation();
  }
  if (hi)
    this->hi_drawn = g_hiFrame;
  const bool relight=this->world_art && !blackIsBody && RenderSettings::relightWorld && Features::dayNightLighting && paletteShadows;
  auto &set = relight ? (hi?textures_lit_hi:textures_lit) : (hi?this->textures_hi:this->textures);
  const bool lazy = !hi && !this->upscale && this->world_art;
  // ours: HD frames at the zoom's level (a shader mod's art shader takes
  // them at 4x, as it always has)
  const bool hdBacked = hi && !this->hd_dir.empty() && HdArt::active();
  const int want = Shaders::artActive() ? 4 : g_hdLevel;
  auto lvKey = [&](const std::string &d) { return (relight ? "L" : "") + d; };
  // ours: frames used as they are (not recoloured, relit or through an art
  // shader) encoded for the graphics card by the loader too, as the HD art
  // mod's setting says (HdArt::requestFrames)
  const int encodeHow = hdBacked && !relight && this->recoloured.empty() && !Shaders::artActive() ? HdArt::compression() : 0;
  auto pathsFor = [&](const std::string &d) {
    std::vector<std::string> paths;
    auto it = this->surfaces.find(d);
    for (size_t f = 0; it != this->surfaces.end() && f < it->second.size(); f++)
      paths.push_back(this->hd_dir + "/" + d + "_" + std::to_string(f));
    return paths;
  };
  // (what the lookups below found last time, while no set has changed)
  const int di = static_cast<int>(direction);
  FramesMemo *memo = di >= 0 && di < 10 ? &this->framesMemo[(relight ? 2 : 0) + (hi ? 1 : 0)][di] : nullptr;
  if (memo && memo->gen == this->textureGen && memo->frames && (!hdBacked || memo->levelGen == g_levelGen)) {
    if (memo->setsFlip)
      this->renderer_flip = memo->flip;
    if (lazy)
      this->makeLazyFrame(renderer, *memo->frames, memo->key, relight, wanted);
    return memo->frames;
  }
  // A set found: as it is; made at another level, the new level asked for
  // and this one drawn until it comes (then this one's freed and the new
  // one made: null)
  auto useFound = [&](const std::string &d, bool mirrored) -> std::vector<SDL_Texture *> * {
    auto level = this->hiLevel.find(lvKey(d));
    const bool stale = hdBacked && level != this->hiLevel.end() && level->second != 0 && level->second != want;
    if (!stale) {
      if (memo)
        *memo = {this->textureGen, mirrored, mirrored ? this->renderer_flip : SDL_FLIP_NONE, &set[d], g_levelGen, d};
      if (lazy)
        this->makeLazyFrame(renderer, set[d], d, relight, wanted);
      return &set[d];
    }
    if (g_hiGradual && (g_hiSpentMs >= 30.0 || !HdArt::framesReady(this->loaderId, lvKey(d), want))) {
      HdArt::requestFrames(this->loaderId, lvKey(d), want, pathsFor(d), encodeHow);
      return &set[d];
    }
    for (SDL_Texture *t : set[d])
      if (t) {
        const size_t bytes = std::min(this->hi_bytes, ArtScaler::textureBytes(t));
        this->hi_bytes -= bytes;
        g_hiBytes -= std::min(g_hiBytes, bytes);
        SDL_DestroyTexture(t);
      }
    set.erase(d);
    this->hiLevel.erase(level);
    ++this->textureGen;
    return nullptr;
  };
  // The art's own frames for this direction (or button state) when it has
  // them - even if another one was made first - else the nearest it has
  std::string exact = convertCompassDirectionToString(direction);
  std::string direction_string;
  if (set.count(exact) && !set[exact].empty()) {
    if (std::vector<SDL_Texture *> *found = useFound(exact, false))
      return found;
    direction_string = exact;
  } else if (this->surfaces.count(exact) && !this->surfaces[exact].empty())
    direction_string = exact;
  else {
    direction_string =
        convertCompassDirectionToExistingAnimationString(direction, set);
    if (!direction_string.empty() && !set[direction_string].empty()) {
      if (std::vector<SDL_Texture *> *found = useFound(direction_string, true))
        return found;
    } else
      direction_string = convertCompassDirectionToExistingAnimationString(
          direction, this->surfaces);
  }
  auto source = this->surfaces.find(direction_string);
  if (direction_string.empty() || source == this->surfaces.end())
    return nullptr;
  // (this frame's time for making zoomed-in sets used up: the normal frames
  // for now, the zoomed-in ones made in a later frame - setGradualZoomedIn)
  if (hi && g_hiGradual && g_hiSpentMs >= 30.0)
    return this->frameTextures(renderer, direction, false, wanted);
  const Uint64 madeFrom = SDL_GetPerformanceCounter();
  bool magnified = this->upscale || hi;
  // ours: an HD art mod's frames for the whole direction, at the level -
  // from the background loader in play (asked for, and the normal frames
  // drawn until they come), read at once on all cores in the checks
  // (HdArt::loadMany) - then made into textures below
  std::vector<SDL_Surface *> hdFrames;
  int builtLevel = 0;
  if (magnified && !this->hd_dir.empty() && HdArt::active()) {
    builtLevel = hi ? want : 4;
    if (hi && g_hiGradual) {
      if (!HdArt::takeFrames(this->loaderId, lvKey(direction_string), builtLevel, hdFrames)) {
        HdArt::requestFrames(this->loaderId, lvKey(direction_string), builtLevel, pathsFor(direction_string), encodeHow);
        return this->frameTextures(renderer, direction, false, wanted);
      }
    } else {
      hdFrames = HdArt::loadMany(pathsFor(direction_string), builtLevel);
    }
  }
  const bool anyHd = std::any_of(hdFrames.begin(), hdFrames.end(), [](SDL_Surface *s) { return s != nullptr; });

  std::vector<SDL_Texture *> &frames = set[direction_string];
  frames.clear();
  ++this->textureGen;
  std::vector<bool> hdTaken(hdFrames.size(), false);
  auto takeHd = [&](int frame, const std::string &path) -> SDL_Surface * {
    if (frame >= 0 && static_cast<size_t>(frame) < hdFrames.size() && !hdTaken[frame]) {
      SDL_Surface *hd = hdFrames[frame]; // (null: not in the mod)
      hdFrames[frame] = nullptr;
      hdTaken[frame] = true;
      return hd;
    }
    // (asked again: read again, as before, at the level)
    SDL_Surface *again = ArtScaler::loadHdSurface(path);
    if (again && builtLevel == 2) {
      SDL_Surface *half = HdArt::halve(again);
      SDL_DestroySurface(again);
      again = half;
    }
    return again;
  };
  const size_t frameCount = source->second.size();
  const bool oneNow = lazy && !magnified && wanted >= 0 && frameCount > 1;
  for (size_t index = 0; index < frameCount; index++) {
    if (oneNow && index != static_cast<size_t>(wanted) % frameCount) {
      frames.push_back(nullptr); // (made when it's first drawn: makeLazyFrame)
      continue;
    }
    SDL_Surface *surface = this->sourceFrame(direction_string, index);
    const int frame = static_cast<int>(index);
    if (!surface) {
      frames.push_back(nullptr);
      continue;
    }
    // addition: remove only retained source shadow indices in the optional
    // world-art cache. Keep the original cache and real black body pigment
    // intact; an RGB colour alone does not identify a shadow.
    SDL_Surface *clean=nullptr;
    SDL_Surface *original=surface;std::vector<Uint8> shadowMask;
    if(relight) {
      shadowMask.assign(size_t(surface->w)*surface->h,0);
      auto masks=shadowRuns.find(direction_string);
      if(masks!=shadowRuns.end() && size_t(frame)<masks->second.size())for(uint64_t run:masks->second[frame]) {
        const size_t start=size_t(run>>32),count=uint32_t(run);if(start+count<=shadowMask.size())std::fill_n(shadowMask.begin()+start,count,Uint8(1));
      }
    }
    if(relight) {
      clean=SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
      if(clean) {
        removePaletteShadow(clean,original,shadowMask);
        surface=clean;
      }
    }
    SDL_Texture *t = nullptr;
    // A shader mod's art shader at work (zoomed-in map art; not a tank's
    // water or the terrain): the frame at 4x - an HD art mod's, else the
    // original upscaled - through it (ShaderMods.hpp)
    if (hi && Shaders::artActive() && this->hd_dir.rfind("water/", 0) != 0 &&
        this->hd_dir.rfind("terrain/", 0) != 0) {
      std::string hdPath = this->hd_dir + "/" + direction_string + "_" + std::to_string(frame);
      SDL_Surface *big = nullptr;
      if (SDL_Surface *hd = takeHd(frame, hdPath)) {
        if (!this->recoloured.empty())
          hd = HdArt::recolour(hd, surface, this->recoloured, direction_string, frame);
        int k = surface->w > 0 ? hd->w / surface->w : 0;
        if (k >= 2 && hd->w == surface->w * k && hd->h == surface->h * k)
          big = SDL_ConvertSurface(hd, SDL_PIXELFORMAT_RGBA32);
        HdArt::freeFrame(hd);
      }
      if (!big)
        big = ArtScaler::upscaleSurface(surface, 4);
      if (big) {
        if(relight)removePaletteShadow(big,original,shadowMask);
        // (what it is, for the shader: 1 a fence's thin lines, 2 an object,
        // 0 an animal or a person)
        const int kind = this->hd_dir.rfind("fences/", 0) == 0    ? 1
                         : this->hd_dir.rfind("objects/", 0) == 0 ? 2
                                                                  : 0;
        t = Shaders::applyArt(renderer, big, kind, surface->w, surface->h);
        SDL_DestroySurface(big);
      }
    }
    if (!t && magnified && !this->hd_dir.empty()) {
      std::string hdPath =
          this->hd_dir + "/" + direction_string + "_" + std::to_string(frame);
      if (SDL_Surface *hd = takeHd(frame, hdPath)) {
        if (!this->recoloured.empty())
          hd = HdArt::recolour(hd, surface, this->recoloured, direction_string, frame);
        if(relight)removePaletteShadow(hd,original,shadowMask);
        t = ArtScaler::createHdTexture(renderer, hd, surface->w, surface->h,
                                       hdPath);
        HdArt::freeFrame(hd);
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
    if (hi && t) {
      const size_t bytes = ArtScaler::textureBytes(t);
      this->hi_bytes += bytes;
      g_hiBytes += bytes;
      g_hiHolders.insert(this);
    }
    frames.push_back(t);
    if(clean)SDL_DestroySurface(clean);
  }
  if (hi) {
    g_hiSpentMs += (SDL_GetPerformanceCounter() - madeFrom) * 1000.0 / SDL_GetPerformanceFrequency();
    this->hiLevel[lvKey(direction_string)] = anyHd ? builtLevel : 0;
  }
  for (SDL_Surface *hd : hdFrames) // (any not used: a frame without art)
    if (hd)
      HdArt::freeFrame(hd);
  if (!this->world_art)
    this->dropSources(direction_string);
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
      int sw = 0, sh = 0;
      if (t)
        ArtScaler::querySize(t, w, h);
      else if (this->sourceSize(direction_string, this->current_frame, sw, sh)) {
        // (not made yet: makeLazyFrame - its source's size)
        if (w)
          *w = sw;
        if (h)
          *h = sh;
      }
    }
  } else {
    direction_string = convertCompassDirectionToExistingAnimationString(
        direction, this->surfaces);
    if (!this->surfaces[direction_string].empty()) {
      if ((size_t)this->current_frame <
          this->surfaces[direction_string].size()) {
        int sw = 0, sh = 0;
        if (this->sourceSize(direction_string, this->current_frame, sw, sh)) {
          if (w)
            *w = sw;
          if (h)
            *h = sh;
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
  const bool reservedBlack=data->pallet && data->pallet->color_count>1 && (data->pallet->colors[1]&0xFFFFFF)==0;
  if(reservedBlack)paletteShadows=true;
  for(uint32_t i=0;i<data->frame_count;++i)paletteShadows=paletteShadows||data->frames[i].is_shadow;
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
  shadowRuns[direction_string].clear();

  // ours: each frame packed as it's drawn, a row at a time - the same
  // packed frame (and shadow runs) as drawing it whole and packing it after
  // (packAll, compressShadow), which drew every frame on a canvas the size
  // of the art's box, mostly clear, then read it all twice: most of an
  // animation's loading time. Colours go in as a byte each while the
  // direction has at most 256 (as packAll decides), four bytes once it has
  // more
  struct Packer {
    PackedDirection pd;
    ColourTable table;
    bool fits = true;
    int w = 0, lo = 0, hi = 0;
    std::vector<uint32_t> row;
    std::vector<Uint8> mask, index;
    PackedFrame f;
    std::vector<uint64_t> runs;
    void begin(int W, int H) {
      w = W;
      row.assign(size_t(W), 0);
      mask.assign(size_t(W), 0);
      index.assign(size_t(W), 0);
      lo = W;
      hi = 0;
      f = PackedFrame{};
      f.present = true;
      f.w = W;
      f.h = H;
      runs.clear();
    }
    void set(int x, uint32_t colour, bool shadow) {
      row[x] = colour;
      mask[x] = shadow;
      lo = std::min(lo, x);
      hi = std::max(hi, x + 1);
    }
    // (a direction past 256 colours: what's packed so far made four bytes
    // a colour - its earlier frames, and this one's rows)
    static std::vector<uint8_t> widened(const std::vector<uint8_t> &data, int rows, const std::vector<uint32_t> &colours) {
      std::vector<uint8_t> out;
      out.reserve(data.size() * 2);
      const uint8_t *q = data.data();
      for (int y = 0; y < rows; y++) {
        const unsigned count = get16(q);
        put16(out, count);
        for (unsigned r = 0; r < count; r++) {
          put16(out, get16(q));
          const unsigned length = get16(q);
          put16(out, length);
          for (unsigned k = 0; k < length; k++) {
            const uint32_t v = colours[*q++];
            const uint8_t *b = reinterpret_cast<const uint8_t *>(&v);
            out.insert(out.end(), b, b + 4);
          }
        }
      }
      return out;
    }
    void endRow(int y) {
      if (fits)
        for (int x = lo; x < hi; x++) {
          if (!row[x])
            continue;
          int at = table.find(row[x]);
          if (at < 0) {
            if (table.size == 256) {
              fits = false;
              for (PackedFrame &done : pd.frames)
                done.data = widened(done.data, done.h, pd.colours);
              f.data = widened(f.data, y, pd.colours);
              break;
            }
            at = table.size;
            table.add(row[x], static_cast<uint8_t>(at));
            pd.colours.push_back(row[x]);
          }
          index[x] = static_cast<uint8_t>(at);
        }
      const size_t countAt = f.data.size();
      put16(f.data, 0);
      unsigned count = 0;
      for (int x = lo, end = 0; x < hi;) {
        while (x < hi && !row[x])
          x++;
        if (x >= hi)
          break;
        const int from = x;
        while (x < hi && row[x])
          x++;
        put16(f.data, static_cast<unsigned>(from - end));
        put16(f.data, static_cast<unsigned>(x - from));
        const size_t at = f.data.size();
        if (fits) {
          f.data.resize(at + size_t(x - from));
          std::memcpy(f.data.data() + at, index.data() + from, size_t(x - from));
        } else {
          f.data.resize(at + size_t(x - from) * 4);
          std::memcpy(f.data.data() + at, row.data() + from, size_t(x - from) * 4);
        }
        end = x;
        count++;
      }
      f.data[countAt] = static_cast<uint8_t>(count & 0xFF);
      f.data[countAt + 1] = static_cast<uint8_t>(count >> 8);
      // (shadow runs over the frame's pixels in order: one ending a row
      // goes on into the next)
      for (int x = lo; x < hi;) {
        if (!mask[x]) {
          ++x;
          continue;
        }
        const int from = x;
        while (x < hi && mask[x])
          ++x;
        const uint64_t at = uint64_t(y) * uint64_t(w) + uint64_t(from);
        if (!runs.empty() && (runs.back() >> 32) + uint32_t(runs.back()) == at)
          runs.back() += uint64_t(x - from);
        else
          runs.push_back(at << 32 | uint32_t(x - from));
      }
      if (lo < hi) {
        std::fill(row.begin() + lo, row.begin() + hi, 0u);
        std::fill(mask.begin() + lo, mask.begin() + hi, Uint8(0));
      }
      lo = w;
      hi = 0;
    }
    void endFrame(std::vector<std::vector<uint64_t>> &shadows) {
      f.data.shrink_to_fit();
      pd.frames.push_back(std::move(f));
      shadows.push_back(std::move(runs));
      runs = {};
    }
    PackedDirection finish() {
      pd.indexed = fits;
      if (!fits)
        pd.colours.clear();
      return std::move(pd);
    }
  };
  auto packer = std::make_unique<Packer>(); // (its colour table: 5 KB)

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
    if (W > 65535) { // (wider than a packed row can say)
      AniFile::freeAnimationData(data);
      return;
    }
    const SDL_Point anchor = {-minX, -minY};
    this->anchor_points[direction_string] = anchor;
    this->anchor_offsets[direction_string] = {W / 2 - anchor.x, H / 2 - anchor.y};
    // (row yy of the surface: this frame's part of it)
    auto paint = [&](const AnimationFrameData &f, int yy) {
      if (!f.lines)
        return;
      const int left = anchor.x - f.offset_y, top = anchor.y - f.offset_x;
      const int y = yy - top;
      if (y < 0 || y >= f.height || !f.lines[y].instructions)
        return;
      int x = left;
      for (int k = 0; k < f.lines[y].instruction_count; k++) {
        const auto &ins = f.lines[y].instructions[k];
        x += ins.offset;
        for (int p = 0; p < ins.color_count; p++, x++) {
          if (x < 0 || x >= W)
            continue;
          uint32_t color = 0xFF000000;
          if (!f.is_shadow) {
            if (!data->pallet || !data->pallet->colors || !ins.colors)
              continue;
            color = data->pallet->colors[ins.colors[p]] | 0xFF000000;
          }
          packer->set(x, color, f.is_shadow || (reservedBlack && ins.colors && ins.colors[p] == 1));
        }
      }
    };
    for (int i = 0; i < count; i++) {
      packer->begin(W, H);
      for (int yy = 0; yy < H; yy++) {
        paint(data->frames[count], yy);
        paint(data->frames[i], yy);
        packer->endRow(yy);
      }
      packer->endFrame(shadowRuns[direction_string]);
    }
    this->surfaces[direction_string].assign(size_t(count), nullptr);
    this->packed[direction_string] = packer->finish();
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

  const int W = data->width, H = data->height;
  for (int i = 0; i < static_cast<int>(data->frame_count); i++) {
    const AnimationFrameData &f = data->frames[i];
    packer->begin(W, H);
    // Each frame placed by its own anchor (the header's offset_x is its y
    // anchor, offset_y its x anchor), lined up on frame 0's: frames whose
    // anchors differ (a zebra's walk: x anchor 11 to 15) no longer shift
    // about (they shook)
    const int left = frameAnchorX - f.offset_y;
    const int top = frameAnchorY - f.offset_x;
    for (int yy = 0; yy < H; yy++) {
      // (its own row y: one past the surface's height is left out, as it
      // always was)
      const int y = yy - top;
      if (f.lines && y >= 0 && y < f.height && y < H) {
        int x = left;
        for (int instruction = 0; instruction < f.lines[y].instruction_count; instruction++) {
          if (!f.lines[y].instructions)
            continue;
          const auto &ins = f.lines[y].instructions[instruction];
          x += ins.offset;
          for (int p = 0; p < ins.color_count; p++, x++) {
            if (x < 0 || x >= W)
              continue;
            uint32_t color = 0xFFFF00FF;
            if (f.is_shadow) {
              color = 0xFF000000;
            } else if (data->pallet && data->pallet->colors) {
              if (!ins.colors)
                continue;
              color = data->pallet->colors[ins.colors[p]] | 0xFF000000;
            }
            packer->set(x, color, f.is_shadow || (reservedBlack && ins.colors && ins.colors[p] == 1));
          }
        }
      }
      packer->endRow(yy);
    }
    packer->endFrame(shadowRuns[direction_string]);
  }
  this->surfaces[direction_string].assign(size_t(data->frame_count), nullptr);
  this->packed[direction_string] = packer->finish();
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
