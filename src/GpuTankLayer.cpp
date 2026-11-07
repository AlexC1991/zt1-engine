#include "GpuTankLayer.hpp"
#include <algorithm>
#include <vector>
#include "RenderSettings.hpp"

namespace {
size_t liveBytes=0;
// ours: zoom-out renders at native size before shrinking. A 1080p view at
// half zoom needs about 32 MiB, so the earlier 8 MiB cap rejected this path
// and repeated tank contents and floor shadows for every depth strip.
constexpr size_t layerLimit=32*1024*1024,totalLimit=64*1024*1024;
GpuTankLayer::Stats counters;
// ours: the layers kept between frames. Made new for each tank every frame,
// each also got a new OpenGL framebuffer from SDL - a quarter of the frame
// went to the driver making them. Cleared before each use, as a new one is.
struct Pooled { SDL_Texture *texture; SDL_Renderer *renderer; int w, h; bool busy; };
std::vector<Pooled> pool;
}
void GpuTankLayer::releasePool(SDL_Renderer *renderer) {
  for(auto it=pool.begin();it!=pool.end();) {
    if(it->renderer!=renderer){++it;continue;}
    SDL_DestroyTexture(it->texture);liveBytes-=size_t(it->w)*it->h*4;it=pool.erase(it);
  }
}
GpuTankLayer::Stats GpuTankLayer::stats(){return counters;}
void GpuTankLayer::resetStats(){counters={};}

GpuTankLayer::~GpuTankLayer() {
  for(auto &p:pool)if(p.texture==texture)p.busy=false;
}

// ours: render once on the GPU and copy through the existing strip scissors.
// SDL's target stores premultiplied colour after normal alpha draws, so the
// final copy uses ONE / ONE_MINUS_SRC_ALPHA, not a second alpha multiply.
// https://wiki.libsdl.org/SDL2/SDL_ComposeCustomBlendMode
bool GpuTankLayer::draw(SDL_Renderer *renderer,const std::function<void(SDL_Renderer *)> &paint) {
  float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);
  SDL_Rect viewport,clip;SDL_RenderGetViewport(renderer,&viewport);SDL_RenderGetClipRect(renderer,&clip);
  // Fractional/full-screen transformed paths keep the proven renderer until
  // they have their own equivalence coverage. GPU FSR's native layer is 1:1.
  // The nonlinear FSR zoom-in pass can magnify the intermediate target's
  // 1-2-level blend rounding into visible edge differences. Keep its direct
  // composition path until that pass has a matching representation.
  if(RenderSettings::lastWorldZoom>1.001f || sx!=1 || sy!=1 || viewport.x<0 || viewport.y<0)return false;
  SDL_Texture *target=SDL_GetRenderTarget(renderer);
  int width=0,height=0;
  if(target)SDL_QueryTexture(target,nullptr,nullptr,&width,&height);
  else SDL_GetRendererOutputSize(renderer,&width,&height);
  if(width<=0 || height<=0 || viewport.x+viewport.w>width || viewport.y+viewport.h>height)return false;
  const bool clipped=SDL_RenderIsClipEnabled(renderer);
  if(!attempted) {
    attempted=true;
    const size_t need=size_t(width)*height*4;
    if(need>layerLimit || !SDL_RenderTargetSupported(renderer))return false;
    const SDL_BlendMode over=SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
    for(auto &p:pool)if(!p.busy && p.renderer==renderer && p.w==width && p.h==height){p.busy=true;texture=p.texture;break;}
    if(!texture) {
      // (idle ones of another size made way first)
      for(auto it=pool.begin();it!=pool.end();)
        if(!it->busy && it->renderer==renderer){SDL_DestroyTexture(it->texture);liveBytes-=size_t(it->w)*it->h*4;it=pool.erase(it);}else ++it;
      if(need>totalLimit-liveBytes)return false;
      texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,width,height);
      if(!texture)return false;
      if(SDL_SetTextureBlendMode(texture,over)!=0){SDL_DestroyTexture(texture);texture=nullptr;return false;}
      liveBytes+=need;pool.push_back({texture,renderer,width,height,true});
    }
    bytes=need;
    counters.peakBytes=std::max(counters.peakBytes,liveBytes);
    SDL_BlendMode blend;Uint8 red,green,blue,alpha;
    SDL_GetRenderDrawBlendMode(renderer,&blend);SDL_GetRenderDrawColor(renderer,&red,&green,&blue,&alpha);
    if(SDL_SetRenderTarget(renderer,texture)!=0) {
      for(auto &p:pool)if(p.texture==texture)p.busy=false;
      texture=nullptr;bytes=0;return false;
    }
    SDL_RenderSetViewport(renderer,&viewport);SDL_RenderSetScale(renderer,1,1);SDL_RenderSetClipRect(renderer,nullptr);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);SDL_RenderClear(renderer);
    paint(renderer);
    ++counters.builds;
    SDL_SetRenderTarget(renderer,target);SDL_RenderSetViewport(renderer,&viewport);
    SDL_RenderSetScale(renderer,sx,sy);SDL_RenderSetClipRect(renderer,clipped?&clip:nullptr);
    SDL_SetRenderDrawBlendMode(renderer,blend);SDL_SetRenderDrawColor(renderer,red,green,blue,alpha);
  }
  if(!texture)return false;
  // The layer uses physical target coordinates; compensate the view's origin
  // while retaining the original viewport and strip clip for centred layouts.
  const SDL_Rect destination{-viewport.x,-viewport.y,width,height};
  if(SDL_RenderCopy(renderer,texture,nullptr,&destination)!=0)return false;
  ++counters.copies;return true;
}
