#ifndef GPU_TANK_LAYER_HPP
#define GPU_TANK_LAYER_HPP

#include <SDL3/SDL.h>
#include <functional>

// ours: a frame-local GPU composite shared by a tank's depth strips. Native
// rendering remains the fallback for unsupported transforms or blend modes.
class GpuTankLayer {
public:
  GpuTankLayer()=default;
  GpuTankLayer(const GpuTankLayer &)=delete;
  GpuTankLayer &operator=(const GpuTankLayer &)=delete;
  ~GpuTankLayer();
  bool draw(SDL_Renderer *,const std::function<void(SDL_Renderer *)> &);
  struct Stats { size_t builds=0,copies=0,peakBytes=0; };
  static Stats stats();
  static void resetStats();
  static void releasePool(SDL_Renderer *); // (before the renderer goes)
private:
  SDL_Texture *texture=nullptr;
  bool attempted=false;
  size_t bytes=0;
};

#endif
