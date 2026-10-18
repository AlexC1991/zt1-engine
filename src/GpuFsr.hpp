#ifndef GPU_FSR_HPP
#define GPU_FSR_HPP

#include <string>
#include <utility>
#include <vector>

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
// RCAS's sharpening, in stops (0 the sharpest): at once, the next frame
// (the Game Options' Art sharpness)
void setSharpness(float sharpness);

// Whether something drawn at this scale gets upscaled by FSR
bool upscales(float scale);
// FSR picked for the art (Game Options > Graphics > Art upscaling): at once.
// Off, things drawn bigger are stretched the plain way (sharp or smooth)
void setUpscaling(bool on);
// Stylized picked (with upscaling on): FSR's upscale, then line art's look -
// dark steady outlines, sharper detail, livelier colours - in RCAS's place
void setStyled(bool on);

// The finished frame's colours, as the last thing drawn before it's shown
// (an addition): a colour-blind filter - 0 off, 1 protanopia, 2
// deuteranopia, 3 tritanopia - at a strength (0-1), and the brightness (a
// gamma: 1 as drawn, above brighter). Daltonised: the colours a viewer with
// that colour blindness can't tell apart are moved into ones they can
// (Machado, Oliveira & Fernandes 2009's simulation, Fidaner et al.'s error
// shift)
void setColour(int filter, float strength, float brightness);
// Applies setColour's to the window's frame (nothing when they're off)
void finishFrame(SDL_Renderer *renderer);

// Mods' shaders (ShaderMods.hpp). A fragment shader of theirs linked with
// ours; 0 and log when it won't compile or link
unsigned compileShader(const std::string &fragment, std::string &log);
// An art pass: prog run over w x h - uColor (unit 0) the frame's colour,
// uNormal (unit 1) its normals (rgb, 0.5 is 0) and height (a), uSize,
// uTime and the shader's settings - written into out (a target texture)
bool artPass(SDL_Renderer *renderer, unsigned prog, SDL_Texture *color, SDL_Texture *aux, SDL_Texture *out, int w,
             int h, const std::vector<std::pair<std::string, float>> &uniforms);
// A screen pass: prog run over the window's finished frame (uColor)
bool screenPass(SDL_Renderer *renderer, unsigned prog, const std::vector<std::pair<std::string, float>> &uniforms);

// Starts a layer of lowW x lowH art pixels (cleared to clear) and makes it
// the render target
void beginLayer(SDL_Renderer *renderer, Layer layer, int lowW, int lowH,
                SDL_Color clear);

// Upscales the layer's viewW x viewH art pixels to dst (default: the whole
// window) and draws them there (over what is there when blend is set); the
// window is the render target again afterwards
void endLayer(SDL_Renderer *renderer, Layer layer, float viewW, float viewH,
              bool blend, const SDL_Rect *dst = nullptr);

} // namespace GpuFsr

#endif // GPU_FSR_HPP
