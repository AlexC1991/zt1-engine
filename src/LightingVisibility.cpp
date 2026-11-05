#include "DayNightLighting.hpp"
#include "Animation.hpp"
#include "WorldRenderer.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

// addition: terrain is an occluder of receiving surfaces, not just a lighting
// receiver. Follow the orthographic camera ray towards the viewer. This is
// cached with the view geometry, so panning does not repeat terrain queries.
float DayNightLighting::terrainClearance(LightingAssets::Point p,const WorldRenderer &view)const {
  if(!frameMap)return 1;
  if(p.z>=frameMap->getMaxHeight()-.001f)return p.z-frameMap->getMaxHeight()+.02f;
  const float gx[]={-1,-1,1,1},gy[]={1,-1,-1,1};
  const float slope=view.getHeightUnitPixels()/view.getTileHeight();
  const float rise=frameMap->getMaxHeight()-p.z+.15f;float clear=1e20f;
  for(int k=0;k<=24;++k){const float h=.01f+rise*k/24;
    const float x=p.x+gx[view.getRotation()]*slope*h,y=p.y+gy[view.getRotation()]*slope*h;
    if(!frameMap->getTile(int(std::floor(x)),int(std::floor(y))))continue;
    clear=std::min(clear,p.z+h-LightingAssets::terrainHeight(*frameMap,x,y));
  }
  return clear+.01f;
}

// addition: a bounded atlas of native alpha silhouettes. These contain white
// coverage only, never replacement art. A projected hull may affect its real
// sprite pixels but must never paint a second outline onto the surroundings.
SDL_Texture *DayNightLighting::whiteMask() {
  if(maskPages.empty()){
    // Four logical pages share one GPU texture where supported. Switching
    // between sprite masks must not split the depth-sorted surface batch.
    SDL_RendererInfo info{};SDL_GetRendererInfo(frameRenderer,&info);maskAtlasSide=info.max_texture_width>=4096 && info.max_texture_height>=4096?4096:2048;
    auto *t=SDL_CreateTexture(frameRenderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,maskAtlasSide,maskAtlasSide);if(!t)return nullptr;
    std::vector<Uint32> empty(size_t(maskAtlasSide)*maskAtlasSide);empty[0]=0xFFFFFFFFu;
    if(SDL_UpdateTexture(t,nullptr,empty.data(),maskAtlasSide*4)){SDL_DestroyTexture(t);return nullptr;}
    SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(t,SDL_ScaleModeNearest);maskPages.push_back({t});
  }
  maskPages.front().used=actorTick;return maskPages.front().texture;
}
DayNightLighting::ReceiverMask DayNightLighting::receiverMask(const Instance &o,WorldRenderer &view) {
  ReceiverMask result;result.texture=whiteMask();result.uv={.5f/maskAtlasSide,.5f/maskAtlasSide,0,0};
  if(!o.artwork)return result;result.required=true;
  const auto source=o.artwork->lightingFrame(view.screenSide(o.sine,-o.cosine),o.frame);auto *s=source.surface;if(!s){result.texture=nullptr;return result;}
  float x,y,d;view.worldToScreenF(o.position.x,o.position.y,o.position.z,x,y,d);
  result.bounds={source.mirrored?x+source.anchor.x-s->w:x-source.anchor.x,y-source.anchor.y,float(s->w),float(s->h)};result.mirrored=source.mirrored;
  if(result.bounds.x+result.bounds.w<0 || result.bounds.x>logicalW || result.bounds.y+result.bounds.h<0 || result.bounds.y>logicalH){result.texture=nullptr;return result;}
  auto found=nativeMasks.find(s);
  if(found==nativeMasks.end()){
    const int w=s->w+4,h=s->h+4;if(w>2044 || h>2044){result.texture=nullptr;return result;}
    int page=-1;
    for(size_t i=0;i<maskPages.size();++i){auto &p=maskPages[i];if(p.x+w>2048){p.x=4;p.y+=p.row;p.row=0;}
      if(p.y+h<=2048){page=int(i);break;}}
    if(page<0 && maskPages.size()<4){
      if(maskAtlasSide==4096){page=int(maskPages.size());maskPages.push_back({maskPages.front().texture});}
      else {auto *t=SDL_CreateTexture(frameRenderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,2048,2048);
        if(t){std::vector<Uint32> empty(2048*2048);SDL_UpdateTexture(t,nullptr,empty.data(),2048*4);SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(t,SDL_ScaleModeNearest);page=int(maskPages.size());maskPages.push_back({t});}}}
    if(page<0){size_t oldest=size_t(-1);for(size_t i=1;i<maskPages.size();++i)if(maskPages[i].used!=actorTick && maskPages[i].used<oldest){oldest=maskPages[i].used;page=int(i);}
      if(page>=0){for(auto it=nativeMasks.begin();it!=nativeMasks.end();)if(it->second.page==page)it=nativeMasks.erase(it);else ++it;auto &p=maskPages[page];p.x=p.y=4;p.row=0;}}
    if(page<0){result.texture=nullptr;return result;}
    auto &p=maskPages[page];std::vector<Uint32> pixels(size_t(w)*h);
    for(int y=0;y<s->h;++y)for(int x=0;x<s->w;++x){const auto pixel=reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(s->pixels)+y*s->pitch)[x];
      if((pixel&s->format->Amask)>>s->format->Ashift)pixels[size_t(y+2)*w+x+2]=0xFFFFFFFFu;}
    if(source.shadowRuns)for(auto run:*source.shadowRuns)for(size_t i=size_t(run>>32);i<size_t(run>>32)+uint32_t(run);++i)
      pixels[(i/s->w+2)*w+i%s->w+2]=0;
    const int offsetX=maskAtlasSide==4096?(page%2)*2048:0,offsetY=maskAtlasSide==4096?(page/2)*2048:0;
    SDL_Rect rect{p.x+offsetX,p.y+offsetY,w,h};if(SDL_UpdateTexture(p.texture,&rect,pixels.data(),w*4)){result.texture=nullptr;return result;}
    found=nativeMasks.emplace(s,MaskEntry{page,rect.x+2,rect.y+2,s->w,s->h}).first;p.x+=w;p.row=std::max(p.row,h);
  }
  const auto &entry=found->second;maskPages[entry.page].used=actorTick;result.texture=maskPages[entry.page].texture;result.uv={float(entry.x)/maskAtlasSide,float(entry.y)/maskAtlasSide,float(entry.w)/maskAtlasSide,float(entry.h)/maskAtlasSide};return result;
}
