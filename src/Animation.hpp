#ifndef ANIMATION_HPP
#define ANIMATION_HPP

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <map>
#include <tuple>

#include <SDL3/SDL.h>

#include "AnimationData.hpp"
#include "CompassDirection.hpp"
#include "IniReader.hpp"
#include "Pallet.hpp"
#include "PalletManager.hpp"

class Animation {
  friend class ResourceManager;
  // Registrations belong to the allocation (not its moved frame buffers).
  // A manager-owned animation unregisters if a caller deletes it early.
  std::function<void()> onRelease;
public:
  Animation();
  Animation(std::unordered_map<std::string, AnimationData *> *data);
  explicit Animation(SDL_Surface *single_frame);
  ~Animation();

  // Rule of Five: Delete Copy, Implement Move
  Animation(const Animation &) = delete;
  Animation &operator=(const Animation &) = delete;
  Animation(Animation &&other) noexcept;
  Animation &operator=(Animation &&other) noexcept;

  void draw(SDL_Renderer *renderer, int x, int y,
            CompassDirection direction = CompassDirection::N);
  void draw(SDL_Renderer *renderer, SDL_Rect *draw_rect,
            CompassDirection direction = CompassDirection::N);

  void queryTexture(CompassDirection direction, int *w, int *h);

  // World art: draws the frame with its anchor point (where the object
  // stands) at x, y (logical pixels). False when it has no such frame.
  // frame >= 0 picks the frame (counted round), for art that plays (a bird
  // flapping)
  bool drawAnchored(SDL_Renderer *renderer, float x, float y,
                    CompassDirection direction, const SDL_Color *tint = nullptr,
                    int frame = -1);
  // Its frame stretched over a four-cornered shape on the screen: dst
  // corners, each with its spot in the art (0-1 across and down), tinted
  // (a water surface tile laid exactly on its tile: drawn as art, the
  // original's 62 px diamonds left a pixel of the floor showing between
  // them)
  bool drawMapped(SDL_Renderer *renderer, CompassDirection direction, int frame, const SDL_FPoint dst[4],
                  const SDL_FPoint uv[4], SDL_Color tint);
  bool drawMapped(SDL_Renderer *renderer, CompassDirection direction, int frame, const SDL_FPoint dst[4],
                  const SDL_FPoint uv[4], SDL_Color tint, bool anchoredMirror);
  // addition: its frame as a shadow cast on the ground (the day/night
  // lighting). place(across, up) gives where on the screen the shadow of the
  // frame's point that many pixels right of its anchor and above it lands.
  // Black, alpha times its own.
  bool drawCastShadow(SDL_Renderer *renderer, CompassDirection direction, int frame,
                      const std::function<SDL_FPoint(float across, float up)> &place, Uint8 alpha,
                      Uint8 tipAlpha); // (alpha at the foot, tipAlpha at the frame's top: fading along it)
  // Where drawAnchored would put it: its box on the screen (false: no frame)
  bool anchoredBounds(float x, float y, CompassDirection direction, float &left, float &top, float &right,
                      float &bottom);
  // Test the source frame's opaque pixels with the same anchor and mirror as drawing.
  bool hitTestAnchored(float px,float py,float x,float y,CompassDirection direction,int frame=-1);
  // Its frames' time (ms; 0 when the file gives none)
  uint32_t frameTimeMs() const { return this->frame_time_in_ms; }
  // How many frames it plays (its first direction's)
  int frameCount() const { return this->frame_count; }

  // Returns true if animation has at least one valid surface or texture
  // Returns true if animation has at least one valid surface or texture
  bool isValid() const {
    for (const auto &pair : surfaces) {
      if (!pair.second.empty())
        return true;
    }
    for (const auto &pair : textures) {
      if (!pair.second.empty())
        return true;
    }
    return false;
  }

  bool hasFrames(CompassDirection direction);

  // addition: emission is sampled from the current art, with its own anchor
  // and mirror. Cropped islands keep a bulb or flame separate from its roof.
  enum EmissionKind { LampGlass=1, Flame=2, BuildingLights=3, WaterHighlights=4, BulbStrings=5 };
  struct EmissionPatch { SDL_Texture *texture=nullptr; SDL_Rect rect{}; SDL_FPoint offset{}; SDL_Color colour{}; int pixels=0; const char *material="";bool reflective=false;
    SDL_Texture *glow=nullptr; int glowPad=kGlowPad; }; // (glow: its soft light round it, glowPad past its rect)
  static constexpr int kGlowPad=6, kFireGlowPad=14;
  const std::vector<EmissionPatch> &emissionPatches(SDL_Renderer *,CompassDirection,int frame,int kind);
  SDL_FRect emissionRect(const EmissionPatch &,float x,float y,CompassDirection,SDL_FlipMode *flip=nullptr);

  // Art drawn magnified (UI): upscaled per the art upscaling setting, and
  // frames can come from an HD pack at <hdDir>/<direction>_<frame>.png
  void setArtOptions(bool upscale, const std::string &hdDir) {
    this->upscale = upscale;
    this->hd_dir = hdDir;
  }
  // Where the original draws a UI frame inside its element's box: it
  // centres the frame on the frame's own anchor point, i.e. shifts it by
  // (width / 2 - anchor x, height / 2 - anchor y). Art anchored off-centre
  // (the 32 px HUD buttons at x=15, the cash spinners at x=8) lands 1 px
  // right. Measured against the original on the HUD and the freeform menu.
  // The offset is from where this animation's own surface puts the frame.
  void anchorOffset(CompassDirection direction, int *dx, int *dy);

  // Map art (objects, animals): drawn as is at 1:1, and from an upscaled
  // set (or HD pack frames) while the map is zoomed in
  void setWorldArt(const std::string &hdDir) {
    this->world_art = true;
    this->hd_dir = hdDir;
  }
  // Map art's own frames (it keeps them), each direction's in order (for
  // making HD art: HdArt::exportFrames)
  template <typename F> void forEachFrame(F f) const {
    for (const auto &[direction, frames] : this->surfaces)
      for (size_t i = 0; i < frames.size(); i++)
        if (SDL_Surface *frame = this->sourceFrame(direction, i))
          f(direction, static_cast<int>(i), frame);
  }
  // Map art drawn in a palette of its own (a building's picked colours, a
  // keeper's hair and skin, a guest's clothes): its name, for HD art - made
  // from the art's own palette - to be recoloured to match (HdArt::recolour)
  void setRecoloured(const std::string &name) { this->recoloured = name; }
  // One of its own frames (nullptr: it hasn't that one)
  SDL_Surface *frameSurface(const std::string &direction, int frame) const {
    return frame < 0 ? nullptr : this->sourceFrame(direction, static_cast<size_t>(frame));
  }

  // addition: native geometry input with the same directional mirroring and
  // anchor as drawing. The caller borrows the surface; no art is copied.
  struct LightingFrame {SDL_Surface *surface=nullptr;SDL_Point anchor{};bool mirrored=false;const std::vector<uint64_t> *shadowRuns=nullptr;};
  LightingFrame lightingFrame(CompassDirection side,int frame);
  const std::string &lightingKey()const {return hd_dir;}
  size_t lightingTextureBytes()const;
  // ours: the zoomed-in frames (the upscaled set, or an HD art mod's 4x
  // ones) are kept up to a budget. Called as a frame starts, before any
  // drawing: past the budget, the sets drawn longest ago are freed (never
  // one drawn in the last frame), to be made again if they're seen again.
  // Without it a long look around a big zoo zoomed in ran out of memory.
  static void trimZoomedIn();
  static size_t zoomedInBytes();
  // ours: what all the animations hold - their source frames, their
  // normal and lit textures, the zoomed-in ones - for finding where the
  // memory goes (the tour check prints it)
  static std::string memoryStats();
  // ours: and they're made a few at a time - up to 30 ms of a frame - with
  // the normal frames drawn meanwhile, so a busy view zoomed into doesn't
  // stop the game while hundreds of HD frames load. Off: all made at once
  // (the check modes, whose pictures must be complete).
  static void setGradualZoomedIn(bool on);
  // ours: unpacked source frames kept up to a budget (packed above): called
  // as a frame starts; and told of each one let go (a cache keyed by the
  // surface - the lighting's masks - forgets it)
  static void trimSources();
  // (owner: who set it; an empty hook clears it only if it's still theirs)
  static void setFrameFreedHook(const void *owner, std::function<void(SDL_Surface *)> hook);
  // ours: every animation's textures let go, to be made again when next
  // drawn - for a check that drew with a renderer of its own, before that
  // renderer goes (the textures made on it would go with it, and be freed
  // a second time on the way out)
  static void releaseAllTextures();
  void setBlackIsBody(bool value){blackIsBody=value;}
  // ours: a copy in other colours - each colour of its frames put through
  // map (its directions' tables, or each pixel of one with more than 256) -
  // its shapes, anchors, shadows, timing and options as they are, no
  // textures yet. Null when a direction isn't packed. (A guest's outfit:
  // the same frames in other colours, made from one load rather than read
  // and decoded again for every outfit)
  Animation *recolouredCopy(const std::function<uint32_t(uint32_t)> &map) const;
  // ours: (checks) the same art: every frame's pixels, its shadows, anchors,
  // timing and options
  bool sameArt(const Animation &other) const;

private:
  // Per direction: frame 0's offset from its surface's corner to where the
  // original draws it (see anchorOffset)
  std::unordered_map<std::string, SDL_Point> anchor_offsets;
  bool upscale = false;
  bool world_art = false;
  std::string hd_dir;
  std::string recoloured;
  // Upscaled frames for the zoomed-in map (map art only), and the HD art
  // roots they were made from (HdArt::generation: made again on a change)
  std::unordered_map<std::string, std::vector<SDL_Texture *>> textures_hi;
  int hi_generation = -1;
  // ours: their size (and the lit sets'), and the frame they were last
  // drawn in - for keeping them all within a budget (trimZoomedIn)
  size_t hi_bytes = 0;
  uint32_t hi_drawn = 0;
  void releaseHi();
  // ours: its HD frames come from the background loader (HdArt::
  // requestFrames) under this number; each zoomed-in direction's level
  // (2 or 4; 0: no HD frames in it, any level will do), by its set ("L" for
  // the lit set) and direction
  uint64_t loaderId = nextLoaderId();
  std::unordered_map<std::string, int> hiLevel;
  static uint64_t nextLoaderId();
  // Whether map art draws from its zoomed-in set now
  bool usesHi() const;

  // (wanted: the frame about to be drawn - plain map art makes its frames
  // one at a time as they're first drawn, so a new animation coming into
  // view costs one frame's texture, not all of them at once; -1 all)
  std::vector<SDL_Texture *> *frameTextures(SDL_Renderer *renderer,
                                            CompassDirection direction,
                                            bool hi, int wanted = -1);

  int current_frame = 0;
  CompassDirection last_direction = CompassDirection::N;
  SDL_FlipMode renderer_flip = SDL_FLIP_NONE;
  uint32_t frame_start_time = 0;

  uint32_t frame_time_in_ms = 0;
  bool has_background = 0;
  int frame_count = 0;

  // Its frames, unpacked (null: packed - sourceFrame unpacks it - or no
  // such frame). Each direction keeps a place for every frame.
  mutable std::unordered_map<std::string, std::vector<SDL_Surface *>> surfaces;
  // ours: its frames packed as they load - each row's clear gaps skipped,
  // its colours a byte each, indices into its direction's own table of at
  // most 256 (the art's palette), or four bytes where a direction has more -
  // and unpacked when asked for (sourceFrame), let go again when they
  // haven't been for a while (trimSources). Kept unpacked for good, every
  // animal in a zoo with them all held 6 GB of mostly clear pixels.
  struct PackedFrame {
    bool present = false;
    int w = 0, h = 0;
    std::vector<uint8_t> data;
  };
  struct PackedDirection {
    bool indexed = true;
    std::vector<uint32_t> colours;
    std::vector<PackedFrame> frames;
  };
  mutable std::unordered_map<std::string, PackedDirection> packed;
  mutable size_t src_bytes = 0;  // (its unpacked frames')
  mutable uint32_t src_used = 0; // (the frame one was last asked for in)
  void packAll();
  SDL_Surface *sourceFrame(const std::string &direction, size_t frame) const;
  bool sourceSize(const std::string &direction, size_t frame, int &w, int &h) const;
  void releaseSources() const;
  void dropSources(const std::string &direction);
  std::unordered_map<std::string, std::vector<SDL_Texture *>> textures;
  // addition: the untouched original and shadow-free art have separate caches.
  bool paletteShadows=false,blackIsBody=false;
  std::unordered_map<std::string,std::vector<std::vector<uint64_t>>> shadowRuns;
  std::unordered_map<std::string,std::vector<SDL_Texture *>> textures_lit,textures_lit_hi;
  std::map<std::tuple<std::string,int,int>,std::vector<EmissionPatch>> emissions;
  void releaseLighting();
  std::string lightingDirection(CompassDirection side);

  template <typename T>
  std::string convertCompassDirectionToExistingAnimationString(
      CompassDirection direction,
      std::unordered_map<std::string, T> &animation_map);
  std::string convertCompassDirectionToString(
      CompassDirection direction); // TODO: Figure out if this should be here

  // Each direction's anchor point in its frames (frame 0)
  std::unordered_map<std::string, SDL_Point> anchor_points;
  // ours: what a draw looks up for a direction, kept - every sprite drawn
  // asked the same string-keyed maps several times. The anchor points are
  // set once, as it loads; the frame sets' memo holds while textureGen does
  // (moved on whenever a set gains, loses or remakes frames). Each keeps the
  // mirroring (renderer_flip) the lookups it stands for left behind.
  struct AnchorMemo { int8_t state = 0; SDL_FlipMode flip = SDL_FLIP_NONE; SDL_Point anchor{}; };
  AnchorMemo anchorMemo[10];
  const AnchorMemo *anchorFor(CompassDirection direction);
  struct FramesMemo { uint32_t gen = 0; bool setsFlip = false; SDL_FlipMode flip = SDL_FLIP_NONE; std::vector<SDL_Texture *> *frames = nullptr; uint32_t levelGen = 0; std::string key; };
  // (a plain map-art frame made when first drawn: frameTextures' wanted)
  void makeLazyFrame(SDL_Renderer *renderer, std::vector<SDL_Texture *> &frames, const std::string &direction, bool relight, int wanted);
  FramesMemo framesMemo[4][10];
  uint32_t textureGen = 1;
  void loadSurfaces(std::string direction_string, AnimationData *data);
};

#endif // ANIMATION_HPP
