#ifndef GPU_FSR_SDLGPU_HPP
#define GPU_FSR_SDLGPU_HPP

#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

// ours: GpuFsr's passes on SDL3's GPU renderer ("gpu": Direct3D 12 or
// Vulkan underneath). The shaders are the Direct3D 11 ones (GpuFsrD3D11.cpp,
// GraphicsEffects.cpp), compiled ahead to DXIL (GpuFsrSdlGpuShaders.hpp),
// run as SDL_GPURenderState fragment shaders on ordinary SDL_RenderTexture
// draws, so SDL keeps its own command buffer and state. The renderer keeps
// the window's frame to itself, so while a pass needs to read it the frame
// is drawn into a texture standing in for the window (Sdl3Compat.hpp's
// ZT_WindowStandIn), copied to the window at the end. Direct3D 12 only for
// now (Vulkan needs SPIR-V). Used through GpuFsr (GpuFsr.hpp).
namespace GpuFsr {
namespace SdlGpu {

// Sets up on a "gpu" SDL renderer whose device takes DXIL
bool init(SDL_Renderer *renderer);
// A built-in effect, found by its HLSL among those compiled ahead; 0 and
// log if it isn't one of them
unsigned compile(const std::string &hlsl, const std::vector<std::string> &params, std::string &log);
// GpuFsr::screenPass's: prog over the frame so far. The frame is only
// readable from the frame after the first call (the stand-in starts at the
// next present): false until then
bool screenPass(SDL_Renderer *renderer, unsigned prog, const std::vector<std::pair<std::string, float>> &uniforms,
                bool smooth);
// GpuFsr::setColour's: whether the colour pass runs, its 3x3 matrix (rows)
// and gamma
void setColour(bool on, const float matrix[3][3], float gamma);
// GpuFsr::finishFrame's: the frame onto the window, through the colour pass
void finishFrame(SDL_Renderer *renderer);
// GpuFsr::endLayer's: as GpuFsr::D3D11::endLayer - the layer (low, the
// current target, lowW x lowH) upscaled by EASU into a window-sized buffer
// (layer 0 or 1's own), then RCAS - or Stylized at strength - onto the
// window at dst, blended (premultiplied) when blend is set
void endLayer(SDL_Renderer *renderer, int layer, SDL_Texture *low, int lowW, int lowH, float viewW, float viewH,
              bool blend, const SDL_Rect *dst, bool styled, float con, float strength);

} // namespace SdlGpu
} // namespace GpuFsr

#endif // GPU_FSR_SDLGPU_HPP
