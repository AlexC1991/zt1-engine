#ifndef GPU_FSR_HPP
#define GPU_FSR_HPP

#include <SDL2/SDL.h>

// ============================================================================
// GPU FSR 1 (AMD FidelityFX Super Resolution 1: EASU upscale + RCAS sharpen)
// ============================================================================
// Real time, on the GPU, at the exact scale things are shown at. Whatever is
// drawn magnified - the map zoomed in, the menus and HUD on a window bigger
// than 800x600 - is drawn at the art's own size into an offscreen layer, and
// FSR scales that layer up to the window every frame:
//
//   beginLayer(...)   then draw as if at 1:1 (scale 1, art pixel = pixel)
//   endLayer(...)     EASU into a window-sized buffer, RCAS onto the window
//
// UI layers are kept transparent (premultiplied alpha) so they composite
// over the map. Text is not part of the layer: it is drawn afterwards at the
// window's resolution so it stays sharp.
//
// Needs SDL's OpenGL renderer (shaders); available() is false otherwise and
// callers draw the normal way (with load-time upscaling, see ArtScaler).
// ============================================================================
namespace GpuFsr {

enum class Layer { World = 0, Ui = 1 };

// Sets up the shaders on an OpenGL renderer. Returns whether GPU FSR works.
bool init(SDL_Renderer *renderer, float sharpness);
bool available();

// Whether something drawn at this scale gets upscaled by FSR
bool upscales(float scale);

// Starts a layer of lowW x lowH art pixels (cleared to clear) and makes it
// the render target
void beginLayer(SDL_Renderer *renderer, Layer layer, int lowW, int lowH,
                SDL_Color clear);

// Upscales the layer's viewW x viewH art pixels to the whole window and
// draws it there (over what is there when blend is set); the window is the
// render target again afterwards
void endLayer(SDL_Renderer *renderer, Layer layer, float viewW, float viewH,
              bool blend);

} // namespace GpuFsr

#endif // GPU_FSR_HPP
