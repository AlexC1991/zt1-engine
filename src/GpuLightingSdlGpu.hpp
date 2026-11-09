#ifndef GPU_LIGHTING_SDLGPU_HPP
#define GPU_LIGHTING_SDLGPU_HPP

#include <array>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include "GpuSurfacesD3D11.hpp"
#include "LightingSun.hpp"

// ours: GpuSurfacesD3D11 and GpuSunD3D11 on SDL3's GPU renderer ("gpu":
// Direct3D 12 or Vulkan underneath) - the day and night lighting's receiving
// surfaces and the ground's shadow, with the same faces, points, sun maps
// and arithmetic. SDL_GPU has no geometry shaders, so a compute pass clips
// the faces into vertex slots (src/shaders/lighting_clip.comp.hlsl) and the
// ground's quads are expanded by their vertex shader. SDL's renderer records
// its frame into one command buffer it submits at present, so each pass is
// drawn on a command buffer of ours, submitted at once - ahead of SDL's on
// the graphics card - into a float layer that SDL then lays onto its target
// (premultiplied: the same as blending each triangle there, to rounding).
// Used through GpuSurfaces (GpuSurfaces.hpp).
namespace GpuSurfacesSdlGpu {

using Face = GpuSurfacesD3D11::Face;
using FaceLight = GpuSurfacesD3D11::FaceLight;
using Shape = GpuSurfacesD3D11::Shape;
using Draw = GpuSurfacesD3D11::Draw;
using Frame = GpuSurfacesD3D11::Frame;
using GroundPoint = GpuSurfacesD3D11::GroundPoint;
using GroundFrame = GpuSurfacesD3D11::GroundFrame;

// The passes can run here: SDL's GPU renderer with GpuFsr's shaders set up
bool available(SDL_Renderer *renderer);
// As GpuSurfacesD3D11's, one for one
void setFaces(const std::vector<Face> &faces, uint64_t key);
uint64_t facesKey();
void setFaceLight(const std::vector<FaceLight> &light, uint64_t key);
uint64_t faceLightKey();
bool draw(SDL_Renderer *renderer, SDL_Texture *mask, const std::vector<Draw> &order, const std::vector<Face> &actorFaces,
          const std::vector<Shape> &shapes, const Frame &frame);
void setGround(const std::vector<GroundPoint> &points, const std::vector<std::array<uint32_t, 4>> &quads, uint64_t key);
uint64_t groundKey();
void setGroundSun(const std::vector<std::array<float, 2>> &sun, uint64_t key);
uint64_t groundSunKey();
bool setGroundLive(const std::vector<uint32_t> &bits);
bool drawGround(SDL_Renderer *renderer, const std::vector<uint32_t> &quads, const GroundFrame &frame);
// As GpuSunD3D11::upload
bool uploadSun(const LightingAssets::SunDepth (&maps)[2], const std::vector<float> *opacity, uint64_t frame);

} // namespace GpuSurfacesSdlGpu

#endif // GPU_LIGHTING_SDLGPU_HPP
