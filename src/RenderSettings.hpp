#ifndef RENDER_SETTINGS_HPP
#define RENDER_SETTINGS_HPP

#include <SDL2/SDL.h>

#include "ArtScaler.hpp"

// How game art is filtered when drawn scaled. Pixel art stays sharp
// (nearest) by default; menus scaled by a non-whole factor switch to linear
// while they draw so the painted art scales smoothly.
namespace RenderSettings {

inline SDL_ScaleMode artScaleMode = SDL_ScaleModeNearest;

// How art drawn bigger looks without FSR (Game Options > Graphics > Art
// upscaling): Auto - sharp at whole sizes, smooth between - Sharp (every
// pixel a square, the original's look) or Smooth
enum class ArtLook { Auto, Sharp, Smooth };
inline ArtLook artLook = ArtLook::Auto;
// The scale mode for art drawn this many times bigger
inline SDL_ScaleMode lookFor(float scale) {
  if (artLook == ArtLook::Sharp)
    return SDL_ScaleModeNearest;
  if (artLook == ArtLook::Smooth)
    return SDL_ScaleModeLinear;
  return SDL_fabsf(scale - SDL_roundf(scale)) < 0.01f ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
}

// The interface's size (Game Options > Graphics > Interface Size, zoo.ini
// [user] uiScale): 0 Auto - 1:1 as the original, whole steps up on a big
// window - or the size picked, from half the original's (0.5x) to the
// largest that lets the 800x600 screens fit, in 0.05 steps
inline float uiScaleSetting = 0.0f;
constexpr float kUiScaleMin = 0.5f, kUiScaleStep = 0.05f;
// While Interface Size's slider is dragged in the Options window: the size
// the window keeps till it's let go (0: none), the rest growing as it goes
inline float uiScaleHeld = 0.0f;
// The largest a window that size (in pixels) can show, in steps
inline float uiScaleMax(int w, int h) {
  float fit = SDL_min(w / 800.0f, h / 600.0f);
  return SDL_max(kUiScaleMin, SDL_floorf(fit / kUiScaleStep + 0.001f) * kUiScaleStep);
}
// Auto's size for it: 1:1 as the original, whole steps up on a big window
// (shrunk to fit one under 800x600)
inline float uiScaleAuto(int w, int h) {
  float fit = SDL_min(w / 800.0f, h / 600.0f);
  return SDL_max(kUiScaleMin, fit < 1.0f ? fit : SDL_floorf(fit));
}
// The size at work for a window that size
inline float uiScaleFor(int w, int h) {
  if (uiScaleSetting <= 0.0f)
    return uiScaleAuto(w, h);
  float fit = SDL_min(w / 800.0f, h / 600.0f);
  return SDL_max(kUiScaleMin, SDL_min(uiScaleSetting, SDL_max(uiScaleMax(w, h), fit < 1.0f ? fit : 0.0f)));
}

// True while the map is drawn zoomed in past 1x: map art then draws from its
// upscaled textures
inline bool worldZoomedIn = false;
// While set, art's edges land on whole screen pixels, each edge rounded
// from where it falls on its own: art stacked edge to edge or half over
// half (a tank's glass and water sides, a height unit apart) then covers
// every pixel the same number of times. Drawn bigger at a fractional zoom,
// each piece rounded on its own left a row covered once or three times - a
// line - at every piece
inline bool snapEdges = false;
// Fences.hpp drawLayered's work so far: run as one picture, drawn as it is
inline int layeredRuns = 0, layeredDirect = 0;
// What the map's last frame was drawn at (the console's hdart says it)
inline float lastWorldZoom = 0.0f;
inline bool lastWorldZoomedIn = false;

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
