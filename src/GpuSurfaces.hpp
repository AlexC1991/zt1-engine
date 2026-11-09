#ifndef GPU_SURFACES_HPP
#define GPU_SURFACES_HPP

#include <array>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include "GpuSurfacesD3D11.hpp"
#include "LightingSun.hpp"

// ours: the day and night lighting's graphics-card passes on whichever
// renderer runs - SDL's Direct3D 11 one (GpuSurfacesD3D11, GpuSunD3D11) or
// SDL3's GPU one (GpuSurfacesSdlGpu). The same faces, points, sun maps and
// arithmetic; see GpuSurfacesD3D11.hpp for what each call does.
namespace GpuSurfaces {

using Face = GpuSurfacesD3D11::Face;
using FaceLight = GpuSurfacesD3D11::FaceLight;
using Shape = GpuSurfacesD3D11::Shape;
using Draw = GpuSurfacesD3D11::Draw;
using Frame = GpuSurfacesD3D11::Frame;
using GroundPoint = GpuSurfacesD3D11::GroundPoint;
using GroundFrame = GpuSurfacesD3D11::GroundFrame;

bool available(SDL_Renderer *renderer);
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
// The receiving surfaces' mask atlas was just changed (SDL_UpdateTexture):
// SDL3's GPU renderer sends it with its frame, after the passes, so the next
// pass first has SDL send what it has queued (Direct3D 11: nothing to do)
void maskChanged();
// GpuSunD3D11::upload's
bool uploadSun(const LightingAssets::SunDepth (&maps)[2], const std::vector<float> *opacity, uint64_t frame);

} // namespace GpuSurfaces

#endif // GPU_SURFACES_HPP
