#ifndef ANIMATION_HPP
#define ANIMATION_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include <SDL2/SDL.h>

#include "AnimationData.hpp"
#include "CompassDirection.hpp"
#include "IniReader.hpp"
#include "Pallet.hpp"
#include "PalletManager.hpp"

class Animation {
public:
  Animation() = default;
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
  // Where drawAnchored would put it: its box on the screen (false: no frame)
  bool anchoredBounds(float x, float y, CompassDirection direction, float &left, float &top, float &right,
                      float &bottom);
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

private:
  // Per direction: frame 0's offset from its surface's corner to where the
  // original draws it (see anchorOffset)
  std::unordered_map<std::string, SDL_Point> anchor_offsets;
  bool upscale = false;
  bool world_art = false;
  std::string hd_dir;
  // Upscaled frames for the zoomed-in map (map art only)
  std::unordered_map<std::string, std::vector<SDL_Texture *>> textures_hi;

  std::vector<SDL_Texture *> *frameTextures(SDL_Renderer *renderer,
                                            CompassDirection direction,
                                            bool hi);

  int current_frame = 0;
  CompassDirection last_direction = CompassDirection::N;
  SDL_RendererFlip renderer_flip = SDL_FLIP_NONE;
  uint32_t frame_start_time = 0;

  uint32_t frame_time_in_ms = 0;
  bool has_background = 0;
  int frame_count = 0;

  std::unordered_map<std::string, std::vector<SDL_Surface *>> surfaces;
  std::unordered_map<std::string, std::vector<SDL_Texture *>> textures;

  template <typename T>
  std::string convertCompassDirectionToExistingAnimationString(
      CompassDirection direction,
      std::unordered_map<std::string, T> &animation_map);
  std::string convertCompassDirectionToString(
      CompassDirection direction); // TODO: Figure out if this should be here

  // Each direction's anchor point in its frames (frame 0)
  std::unordered_map<std::string, SDL_Point> anchor_points;
  void loadSurfaces(std::string direction_string, AnimationData *data);
};

#endif // ANIMATION_HPP