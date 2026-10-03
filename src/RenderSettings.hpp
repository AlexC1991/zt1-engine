#ifndef RENDER_SETTINGS_HPP
#define RENDER_SETTINGS_HPP

#include <SDL2/SDL.h>

#include "ArtScaler.hpp"

// How game art is filtered when drawn scaled. Pixel art stays sharp
// (nearest) by default; menus scaled by a non-whole factor switch to linear
// while they draw so the painted art scales smoothly.
namespace RenderSettings {

inline SDL_ScaleMode artScaleMode = SDL_ScaleModeNearest;

// True while the map is drawn zoomed in past 1x: map art then draws from its
// upscaled textures
inline bool worldZoomedIn = false;

// Which part of the UI is being drawn. With GPU FSR the art is drawn into a
// 1:1 layer that FSR upscales (Art), then text on top at the window's own
// resolution so it stays sharp (Text). Otherwise everything at once (All).
enum class UiPass { All, Art, Text };
inline UiPass uiPass = UiPass::All;
inline bool drawsUiArt() { return uiPass != UiPass::Text; }
inline bool drawsUiText() { return uiPass != UiPass::Art; }

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
