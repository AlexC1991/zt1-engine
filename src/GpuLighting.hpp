#pragma once
#include "LightingSun.hpp"
#include <memory>
#include <string>
#include <vector>

// addition: GPU day/night calculations, owned by the world's GL context.
// CPU atlas uploads are a temporary, explicitly reported hybrid backend.
class GpuLighting {
public:
  struct GroundPoint {float x,y,z,nx,ny,nz,sx,sy;};
  struct alignas(16) SurfaceFace {
    float world[4][4]{},screen[4][4]{},illumination[4][4]{};
    float normal[4]{},ambient[4]{},bounds[4]{},uv[4]{};
    uint32_t flags[4]{};
  };
  struct SurfaceBatch {SDL_Texture *mask=nullptr;int first=0,count=0;};
  struct alignas(16) CasterTriangle {float point[3][4]{};uint32_t owner[4]{};};
  struct Stats {uint64_t groundDraws=0,surfaceDraws=0,atlasDispatches=0,uploadBytes=0;double groundGpuMs=0;size_t bytes=0;};
  GpuLighting();
  ~GpuLighting();
  GpuLighting(const GpuLighting&)=delete;
  GpuLighting &operator=(const GpuLighting&)=delete;
  bool available(SDL_Renderer *);
  bool uploadSun(SDL_Renderer *,const LightingAssets::SunDepth (&)[2],float blend,float strength);
  bool buildSun(SDL_Renderer *,const LightingAssets::SunDepth (&)[2],float blend,float strength,
                const std::vector<CasterTriangle> &fixed,const std::vector<CasterTriangle> &actors,
                uint64_t generation,float terrainBase);
  bool ground(SDL_Renderer *,const std::vector<GroundPoint> &,const std::vector<uint32_t> &,
              uint64_t generation,float originX,float originY);
  bool surfaces(SDL_Renderer *,const std::vector<SurfaceFace> &,const std::vector<SurfaceBatch> &,
                float scale,float originX,float originY,LightingAssets::Point sun,float direct,float reference,
                float lamps,float sunStrength);
  const Stats &stats()const;
  const std::string &reason()const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
