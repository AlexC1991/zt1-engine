#include "GpuLighting.hpp"
#include "World.hpp"
#include "Features.hpp"
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

// addition: paired real-renderer checks. Only disposable check zoos and
// targets are used; no player settings, mods or saves are written.
int runGpuLightingChecks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out,const std::function<void()> &draw){
  std::ostringstream report;int failed=0;auto check=[&](const std::string &n,bool ok){report<<(ok?"PASS ":"FAIL ")<<n<<'\n';failed+=!ok;};
  auto pixels=[&]{int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);std::vector<Uint32> p(size_t(w)*h);if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,p.data(),w*4))p.clear();return p;};
  auto compare=[&](const auto &a,const auto &b){int delta=0;if(a.empty() || a.size()!=b.size())return 255;for(size_t i=0;i<a.size();++i)for(int s:{0,8,16,24})delta=std::max(delta,std::abs(int((a[i]>>s)&255)-int((b[i]>>s)&255)));return delta;};
  auto save=[&](const std::string &name){int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);auto *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);if(s){SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,s->pixels,s->pitch);IMG_SavePNG(s,(out+"/"+name+".png").c_str());SDL_FreeSurface(s);}};
  const char *old=SDL_getenv("ZT_LIGHTING_GPU");const std::string setting=old?old:"";const bool enabled=Features::dayNightLighting;
  SDL_setenv("ZT_LIGHTING_GPU","1",1);GpuLighting gpu;check("production SDL context supports GPU receivers",gpu.available(renderer));
  if(!gpu.available(renderer)){report<<gpu.reason()<<'\n';std::ofstream(out+"/gpu-checks.txt")<<report.str();return 1;}
  auto *previous=SDL_GetRenderTarget(renderer);auto *target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,128,128);
  check("paired target allocated",target!=nullptr);
  if(target){
    LightingAssets::SunDepth maps[2];for(auto &m:maps){m.reset(64,-4,-4,16,12,.6f,.2f);std::fill(m.depth.begin(),m.depth.end(),-1.0f);m.caster=3;m.quad({LightingAssets::Point{1,1,4},{3,1,4},{3,3,4},{1,3,4}});m.seal();}
    std::vector<GpuLighting::GroundPoint> points;std::vector<uint32_t> ids;std::vector<SDL_Vertex> cpu;
    for(int y=0;y<=16;++y)for(int x=0;x<=16;++x){const float wx=x*.5f,wy=y*.5f;points.push_back({wx,wy,-1,0,0,1,x*7.0f+4,y*7.0f+4});
      const auto alpha=Uint8(100*(maps[0].shadow({wx,wy,-1})*.75f+maps[1].shadow({wx,wy,-1})*.25f));cpu.push_back({{x*7.0f+4,y*7.0f+4},{0,0,0,alpha},{0,0}});}
    for(int y=0;y<16;++y)for(int x=0;x<16;++x){const uint32_t a=y*17+x;for(auto k:{a,a+1,a+18,a,a+18,a+17})ids.push_back(k);}
    const std::vector<int> cpuIds(ids.begin(),ids.end());
    for(int scaled=0;scaled<2;++scaled){SDL_SetRenderTarget(renderer,target);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetScale(renderer,1,1);SDL_RenderSetClipRect(renderer,nullptr);
      auto clear=[&]{SDL_SetRenderDrawColor(renderer,178,189,200,255);SDL_RenderClear(renderer);SDL_RenderSetScale(renderer,scaled?.75f:1,scaled?.75f:1);SDL_Rect clip{12,9,100,100};SDL_RenderSetClipRect(renderer,scaled?&clip:nullptr);};
      clear();SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_RenderGeometry(renderer,nullptr,cpu.data(),int(cpu.size()),cpuIds.data(),int(cpuIds.size()));const auto reference=pixels();
      clear();const bool uploaded=gpu.uploadSun(renderer,maps,.25f,1);const bool drawn=gpu.ground(renderer,points,ids,1,0,0);const auto result=pixels();
      const int delta=compare(reference,result);report<<"target delta scaled="<<scaled<<" max="<<delta<<'\n';
      check("GPU shadow filter matches CPU target scale/clip "+std::to_string(scaled),uploaded && drawn && delta<=2);
    }
    SDL_RenderSetScale(renderer,1,1);SDL_RenderSetClipRect(renderer,nullptr);SDL_SetRenderDrawColor(renderer,23,190,66,255);SDL_Rect r{3,4,11,13};SDL_RenderFillRect(renderer,&r);const auto state=pixels();
    check("SDL drawing works after direct GL pass",state.size()==128*128 && state[4*128+3]==0xff17be42u);
    SDL_SetRenderTarget(renderer,previous);SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetClipRect(renderer,nullptr);SDL_DestroyTexture(target);
  }
  world.loadBlank(80,80);world.getFences().setEditing(true);
  extern std::string prepareMarineSoak(World &,ResourceManager *);report<<prepareMarineSoak(world,rm)<<'\n';
  check("marine fixture has two tanks",world.getFences().exhibits().size()>=2);
  auto &objects=world.objectsMutable();for(const auto &[file,x,y]:std::vector<std::tuple<const char*,float,float>>{
    {"scenery/building/restrant.ai",40.5f,29.5f},{"scenery/other/streetlt.ai",36.5f,31.5f},{"scenery/other/lightho.ai",48.5f,30.5f},{"scenery/building/carousal.ai",46.5f,25.5f}})objects.add(file,x,y,0);
  world.reindex();world.setPaused(true);world.getRenderer().centreOnWorld(36,32,0);Features::dayNightLighting=true;auto &light=world.getLighting();light.previewHour=12;light.previewBeamTime=0;
  for(int i=0;i<36;++i)draw();
  int cases=0,maxDelta=0;
  for(int rotation=0;rotation<4;++rotation){for(float zoom:{.5f,1.0f,1.5f})for(double hour:{12.0,17.5,0.0}){
    world.getCamera().zoom=zoom;light.previewHour=hour;
    const auto before=light.gpuStats().groundDraws+light.gpuStats().surfaceDraws;
    SDL_setenv("ZT_LIGHTING_GPU","0",1);draw();const auto cpu=pixels();
    check("reference skipped GPU draws",before==light.gpuStats().groundDraws+light.gpuStats().surfaceDraws);
    draw();const auto cpuAgain=pixels();const int noise=compare(cpu,cpuAgain);
    SDL_setenv("ZT_LIGHTING_GPU","ground",1);draw();const auto ground=pixels();const int groundDelta=compare(cpuAgain,ground);
    SDL_setenv("ZT_LIGHTING_GPU","surface",1);draw();const auto surface=pixels();const int surfaceDelta=compare(cpuAgain,surface);
    SDL_setenv("ZT_LIGHTING_GPU","1",1);draw();const auto candidate=pixels();const int delta=compare(cpuAgain,candidate);maxDelta=std::max(maxDelta,delta);++cases;
    const auto dispatches=light.gpuStats().atlasDispatches;
    SDL_setenv("ZT_LIGHTING_GPU","atlas",1);draw();const auto atlas=pixels();const int atlasDelta=compare(candidate,atlas);
    check("GPU atlas matches uploaded reference map "+std::to_string(cases),atlasDelta<=2 && (hour==0 || light.gpuStats().atlasDispatches>dispatches));
    report<<"atlas_delta="<<atlasDelta<<'\n';
    report<<"reference_delta="<<noise<<" ground_delta="<<groundDelta<<" surface_delta="<<surfaceDelta<<'\n';
    report<<"rotation="<<rotation<<" zoom="<<zoom<<" hour="<<hour<<" delta="<<delta<<'\n';
    check("paired world "+std::to_string(cases),delta<=2);
    if(delta>2){
      save("failed-gpu-"+std::to_string(cases));
      SDL_setenv("ZT_LIGHTING_GPU","0",1);draw();save("reference-"+std::to_string(cases));
      SDL_setenv("ZT_LIGHTING_GPU","1",1);
    }
  }world.rotateView(1);}
  check("real world exercised GPU ground draws",light.gpuStats().groundDraws>0);
  Features::dayNightLighting=false;draw();const auto off=pixels();Features::dayNightLighting=true;light.previewHour=0;draw();Features::dayNightLighting=false;draw();check("off restores exact daylight",off==pixels() && !off.empty());
  report<<"backend="<<light.gpuStatus()<<"\nground_draws="<<light.gpuStats().groundDraws<<"\natlas_dispatches="<<light.gpuStats().atlasDispatches<<"\nupload_bytes="<<light.gpuStats().uploadBytes<<"\nground_gpu_ms="<<light.gpuStats().groundGpuMs<<"\nmax_delta="<<maxDelta<<"\nfailures="<<failed<<'\n';
  light.previewHour=-1;light.previewBeamTime=-1;Features::dayNightLighting=enabled;SDL_setenv("ZT_LIGHTING_GPU",setting.c_str(),1);
  std::ofstream(out+"/gpu-checks.txt")<<report.str();return failed?1:0;
}
