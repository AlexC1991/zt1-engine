#ifndef RENDER_SETTINGS_HPP
#define RENDER_SETTINGS_HPP

#include <SDL2/SDL.h>

#include "ArtScaler.hpp"

// How game art is filtered when drawn scaled. Pixel art stays sharp
// (nearest) by default; menus scaled by a non-whole factor switch to linear
// while they draw so the painted art scales smoothly.
namespace RenderSettings {

inline SDL_ScaleMode artScaleMode = SDL_ScaleModeNearest;

inline void applyArtScaleMode(SDL_Texture *texture) {
  if (!texture)
    return;
  // Upscaled art already holds the detail; linear finishes the last bit of
  // scaling to the window smoothly
  SDL_SetTextureScaleMode(texture, ArtScaler::scaleOf(texture) > 1
                                       ? SDL_ScaleModeLinear
                                       : artScaleMode);
}

} // namespace RenderSettings

#endif // RENDER_SETTINGS_HPP
