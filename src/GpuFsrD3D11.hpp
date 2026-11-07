#ifndef GPU_FSR_D3D11_HPP
#define GPU_FSR_D3D11_HPP

#include <string>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>

// ours: GpuFsr's passes on SDL's Direct3D 11 renderer - the same shaders as
// the OpenGL ones in GpuFsr.cpp, in HLSL, so FSR, Stylized, the colour pass
// and the graphics effects work without switching the game to OpenGL (much
// slower on Windows). Every pass saves the Direct3D state SDL left bound and
// puts it back exactly, so SDL's own record of what's bound stays true.
// Used through GpuFsr (GpuFsr.hpp); Windows only.
namespace GpuFsr {
namespace D3D11 {

// Sets up on a "direct3d11" SDL renderer: the shader compiler, the passes
bool init(SDL_Renderer *renderer);
// A built-in effect's pixel shader (HLSL, ps_4_0), its settings named in the
// order they fill the constants p0.x, p0.y ... p3.w; 0 and log if it won't
// compile
unsigned compile(const std::string &hlsl, const std::vector<std::string> &params, std::string &log);
// GpuFsr::screenPass's: prog over the window's finished frame
bool screenPass(SDL_Renderer *renderer, unsigned prog, const std::vector<std::pair<std::string, float>> &uniforms,
                bool smooth);
// GpuFsr::finishFrame's colour pass: a 3x3 matrix (rows) then a gamma
void colourPass(SDL_Renderer *renderer, const float matrix[3][3], float gamma);
// GpuFsr::endLayer's: the layer (SDL's render target texture low, the
// current target, lowW x lowH) upscaled by EASU into a window-sized buffer
// (layer 0 or 1's own), then RCAS - or Stylized at strength - onto the
// window at dst, blended (premultiplied) when blend is set
void endLayer(SDL_Renderer *renderer, int layer, SDL_Texture *low, int lowW, int lowH, float viewW, float viewH,
              bool blend, const SDL_Rect *dst, bool styled, float con, float strength);

} // namespace D3D11
} // namespace GpuFsr

#endif // GPU_FSR_D3D11_HPP
