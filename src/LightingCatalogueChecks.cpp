#include "World.hpp"
#include "ItemCatalog.hpp"
#include "Features.hpp"
#include "RenderSettings.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

// addition: inspect actual native material output for every authored profile.
// These local PNGs include original art and must never enter a distribution.
int runLightingCatalogueChecks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out) {
  namespace fs=std::filesystem;fs::create_directories(out+"/materials");
  std::ofstream report(out+"/materials.csv");report<<"id,item,side,pixels,patches,regions,animated,source_count\n";
  const bool was=Features::dayNightLighting;Features::dayNightLighting=true;
  auto *old=SDL_GetRenderTarget(renderer);SDL_Rect viewport,clip;SDL_GetRenderViewport(renderer,&viewport);SDL_GetRenderClipRect(renderer,&clip);
  float scaleX,scaleY;SDL_GetRenderScale(renderer,&scaleX,&scaleY);const bool clipped=SDL_RenderClipEnabled(renderer);
  auto *target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,640,640);
  if(!target)return 1;
  auto *pixels=SDL_CreateSurface(640, 640, SDL_PIXELFORMAT_RGBA32);int failures=0,profiles=0;
  if(!pixels){SDL_DestroyTexture(target);return 1;}
  auto capture=[&](const std::string &path){if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels->pixels,pixels->pitch)!=0 || !IMG_SavePNG(pixels,path.c_str()))++failures;};
  for(const auto &asset:LightingAssets::all()) {
    if(asset.id>=1000)continue;
    std::string file;for(const auto &item:ItemCatalog::get().all())if(fs::path(item.file).stem().string()==asset.item){file=item.file;break;}
    Animation *art=file.empty()?nullptr:world.objectsMutable().artOfFile(file);
    if(!art && std::string(asset.item)=="santa")for(auto &a:ItemCatalog::get().all())if(a.file=="ambient/santa.ai")file=a.file;
    std::unique_ptr<Animation> owned;
    if(!art && std::string(asset.item)=="santa"){owned.reset(rm->getAnimation("animals/santa/m/idle/idle"));art=owned.get();}
    if(!art){report<<asset.id<<','<<asset.item<<",MISSING,0,0,0,0,0\n";++failures;continue;}
    ++profiles;int total=0;const char *names[]={"NE","SE","SW","NW"};int sideIndex=0;
    for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW}) {
      SDL_SetRenderTarget(renderer,target);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);SDL_SetRenderScale(renderer,1,1);
      auto patches=art->emissionPatches(renderer,side,0,asset.id);
      std::unique_ptr<Animation> fixture;
      if(asset.nightFixtures){fixture.reset(rm->getAnimation(std::string("objects/")+asset.item+"/used/used"));if(fixture){const auto &extra=fixture->emissionPatches(renderer,side,0,asset.id+3000);patches.insert(patches.end(),extra.begin(),extra.end());}}
int count=0;for(const auto &p:patches)count+=p.pixels;total+=count;
      report<<asset.id<<','<<asset.item<<','<<names[sideIndex]<<','<<count<<','<<patches.size()<<','<<asset.views[sideIndex].size()<<','<<asset.animated<<','<<asset.sources.size()<<'\n';
      if(asset.id>=200 && patches.size()>size_t(asset.nightFixtures?2:1))++failures;
      float left,top,right,bottom;art->anchoredBounds(0,0,side,left,top,right,bottom);
      const float zoom=std::min({2.0f,530/std::max(1.0f,right-left),530/std::max(1.0f,bottom-top)});
      const float ax=320/zoom-(left+right)*.5f,ay=320/zoom-(top+bottom)*.5f;
      for(int mode=0;mode<3;++mode) {
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(renderer,mode==2?0:mode==1?41:132,mode==2?0:mode==1?54:155,mode==2?0:mode==1?68:141,255);SDL_RenderClear(renderer);
        SDL_SetRenderScale(renderer,zoom,zoom);RenderSettings::relightWorld=mode!=0;
        SDL_Color tint{82,103,148,255};if(mode<2)art->drawAnchored(renderer,ax,ay,side,mode?&tint:nullptr,0);
        if(mode)for(const auto &patch:patches) {
          SDL_FlipMode flip;auto box=art->emissionRect(patch,ax,ay,side,&flip);
          SDL_SetTextureAlphaMod(patch.texture,patch.reflective?70:230);SDL_SetTextureBlendMode(patch.texture,SDL_BLENDMODE_BLEND);
          ZT_RenderCopyExF(renderer,patch.texture,nullptr,&box,0,nullptr,flip);SDL_SetTextureAlphaMod(patch.texture,255);
        }
        SDL_SetRenderScale(renderer,1,1);capture(out+"/materials/"+asset.item+"-"+names[sideIndex]+(mode==0?"-day.png":mode==1?"-night.png":"-mask.png"));
      }
      if(!asset.animated && !asset.nightFixtures && art->frameCount()>1) {
        const auto &last=art->emissionPatches(renderer,side,art->frameCount()-1,asset.id);
        if(last.size()!=patches.size())++failures;
        for(size_t i=0;i<std::min(last.size(),patches.size());++i)if(last[i].texture!=patches[i].texture)++failures;
      }
      ++sideIndex;SDL_PumpEvents();
    }
    if(!total && asset.id!=102 && !(asset.id>=400 && asset.id<500)){report<<asset.id<<','<<asset.item<<",NO_EMISSION,0,0,0,0,0\n";++failures;}
    report.flush();
  }
  RenderSettings::relightWorld=false;Features::dayNightLighting=was;
  SDL_SetRenderTarget(renderer,old);SDL_SetRenderScale(renderer,scaleX,scaleY);SDL_SetRenderViewport(renderer,&viewport);SDL_SetRenderClipRect(renderer,clipped?&clip:nullptr);
  SDL_DestroySurface(pixels);SDL_DestroyTexture(target);
  std::ofstream(out+"/material-checks.txt")<<"Profiles inspected: "<<profiles<<"\nFailures: "<<failures<<"\n";
  return failures?1:0;
}
