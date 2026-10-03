#ifndef ART_SCALER_HPP
#define ART_SCALER_HPP

#include <string>

#include <SDL2/SDL.h>

// ============================================================================
// ART UPSCALING
// ============================================================================
// The original art was drawn for 800x600. When it is drawn magnified (menus
// and the HUD on bigger windows), it can be upscaled once, at load time,
// instead of being stretched every frame:
//
//   fsr  - AMD FidelityFX Super Resolution 1: EASU upscale + RCAS sharpen
//          (AMD 2021, MIT), run on the CPU per texture so no shaders
//          needed - the default
//   mmpx - MMPX pixel-art magnification (McGuire & Gagiu 2021, MIT)
//   off  - the art as it is
//
// HD art packs: a PNG at hd/<art path>.png (images) or
// hd/<animation dir>/<direction>_<frame>.png (animation frames) replaces the
// original art when it is a whole multiple of the original's size.
//
// An upscaled texture still reports the art's own size through querySize(),
// so layouts and anchors stay exactly where the original puts them.
// ============================================================================
namespace ArtScaler {

enum class Mode { Off, Mmpx, Fsr };

// [user] artupscale=off|mmpx|fsr, artupscalefactor=0 (0 = from the desktop
// size), artsharpness=0.2 (FSR's RCAS, in stops: 0 = sharpest)
void configure(Mode mode, int factor, float sharpness);
Mode parseMode(const std::string &name);
const char *modeName(Mode mode);
Mode mode();
int factor();

// Texture for art that is drawn magnified (upscaled when enabled)
SDL_Texture *createTexture(SDL_Renderer *renderer, SDL_Surface *surface);

// Texture from an HD pack image standing in for art of size w x h; nullptr
// (and a log line) when it is not a whole multiple of that size
SDL_Texture *createHdTexture(SDL_Renderer *renderer, SDL_Surface *hd, int w,
                             int h, const std::string &path);

// Loads hd/<path>.png if an HD pack provides it (nullptr otherwise)
SDL_Surface *loadHdSurface(const std::string &path);

// The art's own size (texture size divided by its upscale factor)
void querySize(SDL_Texture *texture, int *w, int *h);

// Texture pixels per art pixel (1 for art that is not upscaled)
int scaleOf(SDL_Texture *texture);

// A rect in art pixels converted to the texture's pixels (for src rects)
SDL_Rect toTexture(SDL_Texture *texture, const SDL_Rect &rect);

} // namespace ArtScaler

#endif // ART_SCALER_HPP
