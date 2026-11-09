#ifndef GPU_FSR_SDLGPU_HPP
#define GPU_FSR_SDLGPU_HPP

#include <SDL3/SDL.h>

// ours: GpuFsr's upscaling on SDL3's GPU renderer ("gpu": Direct3D 12 or
// Vulkan underneath). The passes are the Direct3D 11 ones' shaders
// (GpuFsrD3D11.cpp), compiled ahead to DXIL (GpuFsrSdlGpuShaders.hpp), run
// as SDL_GPURenderState fragment shaders on ordinary SDL_RenderTexture
// draws, so SDL keeps its own command buffer and state. Direct3D 12 only for
// now (Vulkan needs SPIR-V). Used through GpuFsr (GpuFsr.hpp).
namespace GpuFsr {
namespace SdlGpu {

// Sets up on a "gpu" SDL renderer whose device takes DXIL
bool init(SDL_Renderer *renderer);
// GpuFsr::endLayer's: as GpuFsr::D3D11::endLayer - the layer (low, the
// current target, lowW x lowH) upscaled by EASU into a window-sized buffer
// (layer 0 or 1's own), then RCAS - or Stylized at strength - onto the
// window at dst, blended (premultiplied) when blend is set
void endLayer(SDL_Renderer *renderer, int layer, SDL_Texture *low, int lowW, int lowH, float viewW, float viewH,
              bool blend, const SDL_Rect *dst, bool styled, float con, float strength);

} // namespace SdlGpu
} // namespace GpuFsr

#endif // GPU_FSR_SDLGPU_HPP
