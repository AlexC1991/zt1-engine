#include "GpuSurfaces.hpp"

// ours: see GpuSurfaces.hpp

#include "GpuLightingSdlGpu.hpp"
#include "GpuSunD3D11.hpp"

namespace GpuSurfaces {
namespace {
enum class Card { None, D3D11, SdlGpu };
Card g_card = Card::None;
} // namespace

bool available(SDL_Renderer *renderer) {
  if (GpuSurfacesD3D11::available(renderer))
    g_card = Card::D3D11;
  else if (GpuSurfacesSdlGpu::available(renderer))
    g_card = Card::SdlGpu;
  else
    g_card = Card::None;
  return g_card != Card::None;
}

void setFaces(const std::vector<Face> &faces, uint64_t key) {
  if (g_card == Card::D3D11)
    GpuSurfacesD3D11::setFaces(faces, key);
  else if (g_card == Card::SdlGpu)
    GpuSurfacesSdlGpu::setFaces(faces, key);
}

uint64_t facesKey() {
  return g_card == Card::D3D11 ? GpuSurfacesD3D11::facesKey()
         : g_card == Card::SdlGpu ? GpuSurfacesSdlGpu::facesKey()
                                  : ~0ull;
}

void setFaceLight(const std::vector<FaceLight> &light, uint64_t key) {
  if (g_card == Card::D3D11)
    GpuSurfacesD3D11::setFaceLight(light, key);
  else if (g_card == Card::SdlGpu)
    GpuSurfacesSdlGpu::setFaceLight(light, key);
}

uint64_t faceLightKey() {
  return g_card == Card::D3D11 ? GpuSurfacesD3D11::faceLightKey()
         : g_card == Card::SdlGpu ? GpuSurfacesSdlGpu::faceLightKey()
                                  : ~0ull;
}

bool draw(SDL_Renderer *renderer, SDL_Texture *mask, const std::vector<Draw> &order, const std::vector<Face> &actorFaces,
          const std::vector<Shape> &shapes, const Frame &frame) {
  if (g_card == Card::D3D11)
    return GpuSurfacesD3D11::draw(renderer, mask, order, actorFaces, shapes, frame);
  if (g_card == Card::SdlGpu)
    return GpuSurfacesSdlGpu::draw(renderer, mask, order, actorFaces, shapes, frame);
  return false;
}

void setGround(const std::vector<GroundPoint> &points, const std::vector<std::array<uint32_t, 4>> &quads, uint64_t key) {
  if (g_card == Card::D3D11)
    GpuSurfacesD3D11::setGround(points, quads, key);
  else if (g_card == Card::SdlGpu)
    GpuSurfacesSdlGpu::setGround(points, quads, key);
}

uint64_t groundKey() {
  return g_card == Card::D3D11 ? GpuSurfacesD3D11::groundKey()
         : g_card == Card::SdlGpu ? GpuSurfacesSdlGpu::groundKey()
                                  : ~0ull;
}

void setGroundSun(const std::vector<std::array<float, 2>> &sun, uint64_t key) {
  if (g_card == Card::D3D11)
    GpuSurfacesD3D11::setGroundSun(sun, key);
  else if (g_card == Card::SdlGpu)
    GpuSurfacesSdlGpu::setGroundSun(sun, key);
}

uint64_t groundSunKey() {
  return g_card == Card::D3D11 ? GpuSurfacesD3D11::groundSunKey()
         : g_card == Card::SdlGpu ? GpuSurfacesSdlGpu::groundSunKey()
                                  : ~0ull;
}

bool setGroundLive(const std::vector<uint32_t> &bits) {
  if (g_card == Card::D3D11)
    return GpuSurfacesD3D11::setGroundLive(bits);
  if (g_card == Card::SdlGpu)
    return GpuSurfacesSdlGpu::setGroundLive(bits);
  return false;
}

bool drawGround(SDL_Renderer *renderer, const std::vector<uint32_t> &quads, const GroundFrame &frame) {
  if (g_card == Card::D3D11)
    return GpuSurfacesD3D11::drawGround(renderer, quads, frame);
  if (g_card == Card::SdlGpu)
    return GpuSurfacesSdlGpu::drawGround(renderer, quads, frame);
  return false;
}

void maskChanged() {
  if (g_card == Card::SdlGpu)
    GpuSurfacesSdlGpu::maskChanged();
}

bool uploadSun(const LightingAssets::SunDepth (&maps)[2], const std::vector<float> *opacity, uint64_t frame) {
  if (g_card == Card::D3D11)
    return GpuSunD3D11::upload(maps, opacity, frame);
  if (g_card == Card::SdlGpu)
    return GpuSurfacesSdlGpu::uploadSun(maps, opacity, frame);
  return false;
}

} // namespace GpuSurfaces
