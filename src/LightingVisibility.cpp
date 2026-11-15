#include "DayNightLighting.hpp"
#include "GpuSurfaces.hpp"
#include "Animation.hpp"
#include "WorldRenderer.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

// addition: terrain is an occluder of receiving surfaces, not just a lighting
// receiver. Follow the orthographic camera ray towards the viewer. This is
// cached with the view geometry, so panning does not repeat terrain queries.
float DayNightLighting::terrainClearance(LightingAssets::Point p,const WorldRenderer &view)const {
  if(!frameMap)return 1;
  return clearanceAt(*frameMap,view.getRotation(),view.getHeightUnitPixels()/view.getTileHeight(),p);
}
// (the same from the map and the view's turn and slope only: the worker
// making the ground ahead asks it too)
float DayNightLighting::clearanceAt(const WorldMap &map,int rotation,float slope,LightingAssets::Point p) {
  if(p.z>=map.getMaxHeight()-.001f)return p.z-map.getMaxHeight()+.02f;
  const float gx[]={-1,-1,1,1},gy[]={1,-1,-1,1};
  const float rise=map.getMaxHeight()-p.z+.15f;float clear=1e20f;
  for(int k=0;k<=24;++k){const float h=.01f+rise*k/24;
    const float x=p.x+gx[rotation]*slope*h,y=p.y+gy[rotation]*slope*h;
    if(!map.getTile(int(std::floor(x)),int(std::floor(y))))continue;
    clear=std::min(clear,p.z+h-LightingAssets::terrainHeight(map,x,y));
  }
  return clear+.01f;
}

// ours: terrainClearance asks the map 25 times along the camera's line from
// a point; for an animal's or guest's every face corner, every frame, that
// was most of the lighting's work. The line runs along a tile diagonal
// (equal steps in x and y), so every tile it can cross from a tile is on
// that diagonal or beside it: rayMax holds, for each tile, the highest
// corner of all of those as far as the line reaches from the map's lowest
// ground, each less how far the line has risen by then. A point that high
// can't be hidden - its clearance is at least clearBound's (the projection
// checks compare the two over a whole map, in four views)
void DayNightLighting::prepareRayMax(const WorldMap &map,const WorldRenderer &view) {
  const int rotation=view.getRotation(),w=map.getWidth(),h=map.getHeight();
  const float slope=view.getHeightUnitPixels()/view.getTileHeight();
  if(rayMaxMap==&map && rayMaxGeneration==map.getGeneration() && rayMaxRotation==rotation && rayMaxW==w && rayMaxH==h && rayMaxSlope==slope)return;
  rayMaxMap=&map;rayMaxGeneration=map.getGeneration();rayMaxRotation=rotation;rayMaxW=w;rayMaxH=h;rayMaxSlope=slope;
  rayMaxFloor=float(map.getMinHeight());
  // (terrainClearance's last sample, from the lowest ground: .01 above the
  // map's top plus .15; a tile further on for the point's place in its tile)
  const int reach=int(std::ceil(slope*(map.getMaxHeight()-rayMaxFloor+.16f)))+1;
  const int gx[]={-1,-1,1,1},gy[]={1,-1,-1,1};const int sx=gx[rotation],sy=gy[rotation];
  constexpr float none=-std::numeric_limits<float>::infinity();
  std::vector<float> top(size_t(w)*h,none);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(const auto *t=map.getTile(x,y))
    top[size_t(y)*w+x]=float(std::max(std::max(t->cornerHeight[0],t->cornerHeight[1]),std::max(t->cornerHeight[2],t->cornerHeight[3])));
  auto at=[&](int x,int y){return x<0 || y<0 || x>=w || y>=h?none:top[size_t(y)*w+x];};
  // (less how far the line has risen by the time it reaches a tile: it's
  // into the n-th tile along only after more than n-1 tiles, so it's at
  // least (n-1)/slope above the point there - and always .01)
  auto lift=[&](int n){return std::max(.01f,(n-1)/slope);};
  rayMax.assign(size_t(w)*h,none);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){float m=none;
    for(int t=0;t<=reach;++t)m=std::max(m,std::max(at(x+sx*t,y+sy*t)-lift(t),std::max(at(x+sx*(t+1),y+sy*t),at(x+sx*t,y+sy*(t+1)))-lift(t+1)));
    rayMax[size_t(y)*w+x]=m;}
}
float DayNightLighting::clearBound(LightingAssets::Point p)const {
  const int x=int(std::floor(p.x)),y=int(std::floor(p.y));
  // (ours: ZT_LIGHTING_SLOW_PATHS - the shortcuts off, every point asked of
  // the map - for comparing pictures in the checks)
  static const bool slow=std::getenv("ZT_LIGHTING_SLOW_PATHS")!=nullptr;
  if(slow || x<0 || y<0 || x>=rayMaxW || y>=rayMaxH || rayMax.empty() || p.z<rayMaxFloor)return -1;
  // (every sample is at least as far above the point as rayMax took off, and
  // the map there no higher than it was; terrainClearance adds .01)
  return p.z+.01f-rayMax[size_t(y)*rayMaxW+x];
}

// addition: a bounded atlas of native alpha silhouettes. These contain white
// coverage only, never replacement art. A projected hull may affect its real
// sprite pixels but must never paint a second outline onto the surroundings.
SDL_Texture *DayNightLighting::whiteMask() {
  if(maskPages.empty()){
    // Four logical pages share one GPU texture where supported. Switching
    // between sprite masks must not split the depth-sorted surface batch.
    ZT_RendererInfo info{};ZT_GetRendererInfo(frameRenderer,&info);maskAtlasSide=info.max_texture_width>=4096 && info.max_texture_height>=4096?4096:2048;
    auto *t=SDL_CreateTexture(frameRenderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,maskAtlasSide,maskAtlasSide);if(!t)return nullptr;
    std::vector<Uint32> empty(size_t(maskAtlasSide)*maskAtlasSide);empty[0]=0xFFFFFFFFu;
    if(!GpuSurfaces::updateMask(frameRenderer,t,nullptr,empty.data(),maskAtlasSide*4)){SDL_DestroyTexture(t);return nullptr;}
    SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(t,SDL_SCALEMODE_NEAREST);maskPages.push_back({t});
  }
  maskPages.front().used=actorTick;return maskPages.front().texture;
}
DayNightLighting::ReceiverMask DayNightLighting::receiverMask(const Instance &o,WorldRenderer &view) {
  ReceiverMask result;result.texture=whiteMask();result.uv={.5f/maskAtlasSide,.5f/maskAtlasSide,0,0};
  if(!o.artwork)return result;result.required=true;
  const auto source=o.artwork->lightingFrame(artSide(o,view),o.frame);auto *s=source.surface;if(!s){result.texture=nullptr;return result;}
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
        if(t){std::vector<Uint32> empty(2048*2048);GpuSurfaces::updateMask(frameRenderer,t,nullptr,empty.data(),2048*4);SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(t,SDL_SCALEMODE_NEAREST);page=int(maskPages.size());maskPages.push_back({t});}}}
    if(page<0){size_t oldest=size_t(-1);for(size_t i=1;i<maskPages.size();++i)if(maskPages[i].used!=actorTick && maskPages[i].used<oldest){oldest=maskPages[i].used;page=int(i);}
      if(page>=0){for(auto it=nativeMasks.begin();it!=nativeMasks.end();)if(it->second.page==page)it=nativeMasks.erase(it);else ++it;auto &p=maskPages[page];p.x=p.y=4;p.row=0;}}
    if(page<0){result.texture=nullptr;return result;}
    auto &p=maskPages[page];std::vector<Uint32> pixels(size_t(w)*h);
    // (ours: the format's alpha looked up once, not twice for every pixel)
    const auto *format=SDL_GetPixelFormatDetails(s->format);const Uint32 amask=format->Amask;const int ashift=format->Ashift;
    for(int y=0;y<s->h;++y){const auto *row=reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(s->pixels)+y*s->pitch);Uint32 *to=&pixels[size_t(y+2)*w+2];
      for(int x=0;x<s->w;++x)if((row[x]&amask)>>ashift)to[x]=0xFFFFFFFFu;}
    if(source.shadowRuns)for(auto run:*source.shadowRuns)for(size_t i=size_t(run>>32);i<size_t(run>>32)+uint32_t(run);++i)
      pixels[(i/s->w+2)*w+i%s->w+2]=0;
    const int offsetX=maskAtlasSide==4096?(page%2)*2048:0,offsetY=maskAtlasSide==4096?(page/2)*2048:0;
    SDL_Rect rect{p.x+offsetX,p.y+offsetY,w,h};if(!GpuSurfaces::updateMask(frameRenderer,p.texture,&rect,pixels.data(),w*4)){result.texture=nullptr;return result;}
    found=nativeMasks.emplace(s,MaskEntry{page,rect.x+2,rect.y+2,s->w,s->h}).first;p.x+=w;p.row=std::max(p.row,h);
  }
  const auto &entry=found->second;maskPages[entry.page].used=actorTick;result.texture=maskPages[entry.page].texture;result.uv={float(entry.x)/maskAtlasSide,float(entry.y)/maskAtlasSide,float(entry.w)/maskAtlasSide,float(entry.h)/maskAtlasSide};return result;
}
