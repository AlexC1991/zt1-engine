#include "GraphicsQuality.hpp"
#include "World.hpp"
#include "Features.hpp"
#include "ItemCatalog.hpp"
#include "RenderSettings.hpp"
#include "LightingCache.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <sstream>
#include <memory>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// addition: exercises the real world draw path, including its upscale/resolve,
// rather than timing an isolated ray loop. Only runs in a disposable check zoo.
namespace FrameProfile {
extern bool active;
extern std::map<std::string,double> totals;
void begin();
}

int runLightingChecks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out,
                      const std::function<void(const char*)> &capture,const SDL_Rect &viewRect) {
  std::ostringstream report; int failures=0;
  auto check=[&](const char *name,bool ok) { report<<(ok?"PASS ":"FAIL ")<<name<<"\n";failures+=!ok; };
  using L=DayNightLighting;
  check("visual cycle wraps at twelve minutes",L::hourAt(0)==12 && L::hourAt(720)==12 && L::hourAt(360)==0);
  check("visible clock uses 24-hour time and wraps at midnight",L::formatTime(0)=="00:00" && L::formatTime(18.5)=="18:30" && L::formatTime(24)=="00:00");
  check("night is readable blue ambient and daytime is untinted",L::skyAt(0).ambient.b>L::skyAt(0).ambient.r && L::skyAt(12).ambient.r==255);
  check("lights turn off in morning and on at night",L::skyAt(8).lamps==0 && L::skyAt(20).lamps==1 && L::skyAt(18).lamps>0 && L::skyAt(18).lamps<1);
  check("sun shadows fade out at night",L::skyAt(0).sun==0 && L::skyAt(12).sun>.99f);
  check("low sun casts longer shadows than noon, no longer than 1.25 heights",L::skyAt(17.5).shadowLength>L::skyAt(12).shadowLength*2 && L::skyAt(17.5).shadowLength<=1.25f);
  {
    // Source palette: exact black is the old shadow, near-black is real ink.
    Pallet palette{};palette.color_count=5;palette.colors[1]=0xFF000000;
    palette.colors[2]=0xFF010101;palette.colors[3]=0xFF0000FF;palette.colors[4]=0xFF000000;
    auto *data=static_cast<AnimationData*>(std::calloc(1,sizeof(AnimationData)));
    data->width=4;data->height=1;data->frame_count=1;data->pallet=&palette;
    data->frames=static_cast<AnimationFrameData*>(std::calloc(1,sizeof(AnimationFrameData)));
    auto &f=data->frames[0];f.width=4;f.height=1;
    f.lines=static_cast<AnimationLineData*>(std::calloc(1,sizeof(AnimationLineData)));f.lines[0].instruction_count=1;
    auto *run=static_cast<AnimationDrawInstruction*>(std::calloc(1,sizeof(AnimationDrawInstruction)));
    f.lines[0].instructions=run;run->color_count=4;run->colors=static_cast<Uint8*>(std::malloc(4));
    run->colors[0]=1;run->colors[1]=2;run->colors[2]=3;run->colors[3]=4;
    std::unordered_map<std::string,AnimationData*> frames{{"N",data}};Animation art(&frames);art.setWorldArt("");
    SDL_Texture *old=SDL_GetRenderTarget(renderer),*target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,8,8);
    const bool was=Features::dayNightLighting;bool good=target!=nullptr;
    std::vector<Uint32> original,relit,restored;
    if(target) {
      SDL_SetRenderTarget(renderer,target);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetScale(renderer,1,1);
      auto sample=[&](bool enabled,bool inWorld=true){Features::dayNightLighting=enabled;RenderSettings::relightWorld=enabled&&inWorld;SDL_SetRenderDrawColor(renderer,255,255,255,255);SDL_RenderClear(renderer);
        art.drawAnchored(renderer,0,0,CompassDirection::N,nullptr,0);std::vector<Uint32> p(64);SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,p.data(),32);return p;};
      original=sample(false);relit=sample(true);restored=sample(false);
      good=original[0]==0xFF000000 && relit[0]==0xFFFFFFFF && relit[1]==0xFF010101 && relit[3]==0xFF000000 && restored==original;
      check("interface art keeps its original shadow pixels",sample(true,false)==original);
      check("invisible old shadows do not block picking or lights",!art.hitTestAnchored(.5f,.5f,0,0,CompassDirection::N,0) && art.hitTestAnchored(1.5f,.5f,0,0,CompassDirection::N,0));
    }
    if(target) {
      art.setWorldArt("animals/lighting-pigment-check");art.setBlackIsBody(true);
      SDL_SetRenderTarget(renderer,target);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetScale(renderer,1,1);
      Features::dayNightLighting=true;RenderSettings::relightWorld=true;
      SDL_SetRenderDrawColor(renderer,255,255,255,255);SDL_RenderClear(renderer);art.drawAnchored(renderer,0,0,CompassDirection::N,nullptr,0);
      std::vector<Uint32> pigment(64);SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pigment.data(),32);
      check("real black animal pigment remains opaque with lighting enabled",pigment[0]==0xFF000000 && art.hitTestAnchored(.5f,.5f,0,0,CompassDirection::N,0));
    }
    SDL_SetRenderTarget(renderer,old);SDL_DestroyTexture(target);Features::dayNightLighting=was;
    RenderSettings::relightWorld=false;
    check("baked shadow pixels disappear, near-black ink stays, Off restores source",good);
  }
  {
    LightingAssets::Model cube;cube.nx=cube.ny=cube.nz=3;cube.step={1,1,1};cube.solid.resize(27);cube.solid[13]=1;
    check("3D ray hits a solid cell",cube.ray({-1,1.5f,1.5f},{4,1.5f,1.5f}));
    check("3D ray passes above geometry",!cube.ray({-1,1.5f,2.5f},{4,1.5f,2.5f}));
    check("3D ray handles reverse and inside starts",cube.ray({4,1.5f,1.5f},{-1,1.5f,1.5f}) && cube.ray({1.5f,1.5f,1.5f},{4,1.5f,1.5f}));
    check("3D ray misses parallel empty cells",!cube.ray({-1,.5f,.5f},{4,.5f,.5f}));
    check("3D geometry behind source does not occlude",!cube.ray({2.5f,1.5f,1.5f},{4,1.5f,1.5f}));
    cube.solid.assign(27,1);cube.solid[12]=cube.solid[13]=cube.solid[14]=0;
    check("3D ray passes through an opening",!cube.ray({-1,1.5f,1.5f},{4,1.5f,1.5f}) && cube.ray({-1,.5f,1.5f},{4,.5f,1.5f}));
  }
  float maxStep=0;
  for(int i=1;i<=2400;++i) {
    auto a=L::skyAt((i-1)*.01),c=L::skyAt(i*.01);
    maxStep=std::max(maxStep,float(std::max({std::abs(int(a.ambient.r)-c.ambient.r),std::abs(int(a.ambient.g)-c.ambient.g),std::abs(int(a.ambient.b)-c.ambient.b)})));
  }
  check("sunrise and sunset have no abrupt colour jumps",maxStep<=2);
  check("blank disposable zoo opens",world.loadBlank(80,80));
  {
    std::unique_ptr<Animation> ride(rm->getAnimation("objects/carousal/used/used"));bool stable=bool(ride),found=false;
    if(ride)for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW}) {
      const auto &reference=ride->emissionPatches(renderer,side,0,104);found=found||!reference.empty();
      for(int frame=1;frame<ride->frameCount();++frame) {
        const auto &current=ride->emissionPatches(renderer,side,frame,104);
        stable=stable && current.size()==reference.size();
        for(size_t i=0;i<std::min(current.size(),reference.size());++i)stable=stable && current[i].texture==reference[i].texture && current[i].pixels==reference[i].pixels;
      }
    }
    check("carousel bulbs remain steady through every native ride frame and view",stable && found);
  }
  if(std::unique_ptr<Animation> orca{rm->getAnimation("animals/orca/m/subswim/subswim")})
    check("original orca definition preserves its black body pixels",orca->lightingFrame(CompassDirection::SE,0).shadowRuns==nullptr);
  auto &map=world.getMap();auto &objects=world.objectsMutable();
  // Asset-specific checks: glass anchors come from pixels, not a guessed
  // building height; a broken post must not emit through its empty head.
  for(const auto &test:std::vector<std::pair<std::string,int>>{{"scenery/other/lamp.ai",Animation::LampGlass},
      {"scenery/building/campfire.ai",Animation::Flame}}) {
    Animation *art=objects.artOfFile(test.first);bool found=false;
    if(art)for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW}) {
      const auto &patches=art->emissionPatches(renderer,side,0,test.second);found=found||!patches.empty();
      if(test.second==Animation::LampGlass && !patches.empty())check("lamp glass source uses its actual high anchor",patches.front().offset.y < -50 && patches.front().offset.y > -72);
    }
    check(test.second==Animation::LampGlass?"lamp glass yields emission pixels":"campfire yields flame emission pixels",found);
  }
  if(auto *broken=objects.artOfFile("scenery/other/bklamp.ai"))
    check("broken lamp has no imaginary glass light",broken->emissionPatches(renderer,CompassDirection::NE,0,102).empty());
  std::vector<std::string> buildings,trees,rocks;
  for(const auto &item:ItemCatalog::get().all()) {
    if(item.file.find("scenery/building/")==0 && buildings.size()<12)buildings.push_back(item.file);
    if(item.members.count("foliage") && trees.size()<6)trees.push_back(item.file);
    if(item.members.count("rocks") && rocks.size()<4)rocks.push_back(item.file);
  }
  report<<"Building fixture:";for(const auto &f:buildings)report<<" "<<f;report<<"\n";
  int shops=0,lamps=0,scenery=0;
  for(int y=0;y<6;++y)for(int x=0;x<6;++x) {
    if(!buildings.empty())shops+=objects.add(buildings[(x+y*6)%buildings.size()],25.5f+x*5,25.5f+y*5,(x%4)*2);
    lamps+=objects.add("scenery/other/lamp.ai",27.5f+x*5,27.5f+y*5,0);
    if(!trees.empty())scenery+=objects.add(trees[(x+y)%trees.size()],28.5f+x*5,24.5f+y*5,0);
    if(!rocks.empty())scenery+=objects.add(rocks[(x+y)%rocks.size()],24.5f+x*5,28.5f+y*5,0);
  }
  for(int y=20;y<=57;++y)for(int x=20;x<=57;++x)
    if(x%5==3 || y%5==3)map.setPath(x,y,"path");
  map.touch();world.reindex();
  check("real building, lamp, foliage and rock art loaded",shops>=24 && lamps==36 && scenery>=36);
  int animals=0;
  for(const auto *file:{"animals/lion.ai","animals/elephant.ai","animals/giraffe.ai"}) {
    int type=world.getAnimals().typeOfFile(file);
    if(type>=0)for(int i=0;i<6;++i)animals+=world.getAnimals().adopt(type,i%2,31.5f+i,36.5f+animals*.1f,map,world.getFences(),true)>=0;
  }
  report<<"Fixture has "<<shops<<" buildings, "<<lamps<<" lamps, "<<scenery<<" scenery, "<<animals<<" animals.\n";
  check("animal sprites included in the populated fixture",animals>=12);
  world.setPaused(true);double clock=world.getClock();
  world.update(SDL_GetKeyboardState(nullptr),1);
  check("pausing the zoo stops the cycle clock",world.getClock()==clock);
  world.setView(viewRect,1.0f);auto &camera=world.getCamera();camera.zoom=1;
  world.draw(renderer);world.getRenderer().centreOnWorld(39,39,map.getTile(39,39)->height);
  {
    Pallet palette{};palette.color_count=3;palette.colors[1]=0xFF000000;palette.colors[2]=0xFF404040;
    auto *data=static_cast<AnimationData*>(std::calloc(1,sizeof(AnimationData)));
    data->width=data->height=100;data->frame_count=1;data->pallet=&palette;
    data->frames=static_cast<AnimationFrameData*>(std::calloc(1,sizeof(AnimationFrameData)));
    auto &f=data->frames[0];f.width=f.height=100;f.offset_x=100;f.offset_y=50;
    f.lines=static_cast<AnimationLineData*>(std::calloc(100,sizeof(AnimationLineData)));
    for(int y=0;y<100;++y) {
      f.lines[y].instruction_count=1;auto *run=static_cast<AnimationDrawInstruction*>(std::calloc(1,sizeof(AnimationDrawInstruction)));
      f.lines[y].instructions=run;run->color_count=100;run->colors=static_cast<Uint8*>(std::malloc(100));std::fill_n(run->colors,100,2);
    }
    const auto wallSide=world.getRenderer().screenSide(0,-1);
    const std::string direction=wallSide==CompassDirection::NE?"NE":wallSide==CompassDirection::NW?"NW":wallSide==CompassDirection::SE?"SE":"SW";
    std::unordered_map<std::string,AnimationData*> dataMap{{direction,data}};Animation wall(&dataMap);wall.setWorldArt("");
    if(const char *cache=std::getenv("ZT_LIGHTING_CACHE_DIR");cache && *cache) {
      auto *probeArt=objects.artOfFile("scenery/other/lamp.ai");const auto *asset=LightingAssets::profile(101);
      if(probeArt && asset) {
        const auto first=LightingAssets::build(*probeArt,*asset,.25f,.25f,0,false);const auto before=LightingAssets::modelCacheStats();
        const auto second=LightingAssets::build(*probeArt,*asset,.25f,.25f,0,false);const auto after=LightingAssets::modelCacheStats();
        check("a repeated source shape reuses its on-disk model exactly",!first.faces.empty() && after.hits==before.hits+1 && first.solid==second.solid && first.faces.size()==second.faces.size());
        auto native=probeArt->lightingFrame(CompassDirection::NE,0);Uint32 *pixel=nullptr;
        if(native.surface)for(int y=0;y<native.surface->h && !pixel;++y)for(int x=0;x<native.surface->w;++x) {
          auto *at=reinterpret_cast<Uint32*>(static_cast<Uint8*>(native.surface->pixels)+y*native.surface->pitch)+x;Uint8 red,green,blue,alpha;
          SDL_GetRGBA(*at,native.surface->format,&red,&green,&blue,&alpha);if(alpha<64)continue;
          bool shadow=false;const uint32_t index=y*native.surface->w+x;
          if(native.shadowRuns)for(uint64_t run:*native.shadowRuns){const uint32_t first=uint32_t(run>>32);if(index>=first && index-first<uint32_t(run)){shadow=true;break;}}
          if(!shadow){pixel=at;break;}
        }
        if(pixel) {
          const Uint32 original=*pixel;*pixel=SDL_MapRGBA(native.surface->format,0,0,0,0);
          LightingAssets::build(*probeArt,*asset,.25f,.25f,0,false);const auto changed=LightingAssets::modelCacheStats();*pixel=original;
          check("changed source pixels select a different geometry-cache entry",changed.lastKey!=after.lastKey);
        } else check("native cache probe has a non-shadow body pixel",false);
      } else check("native four-view cache probe loads",false);
    }
    PlacedObjects probe;ZooReader empty;probe.load(empty,rm);probe.add("scenery/other/lamp.ai",39.5f,39.5f,0);
    const bool was=Features::dayNightLighting;Features::dayNightLighting=true;
    DayNightLighting lighting;lighting.previewHour=0;
    lighting.prepare(world,rm,probe,map,world.getRenderer(),0,viewRect.w,viewRect.h,renderer);
    lighting.drawEmission(renderer,world.getRenderer(),viewRect);
    const int lamps=lighting.stats().lights,seen=lighting.stats().emissives;
    PlacedObjects::Object cover;cover.className="building";cover.subClass="building";cover.typeName="lighting_probe";
    float sx,sy,depth;const float ground=map.getTile(39,39)->height;
    world.getRenderer().worldToScreenF(39.5f,39.5f,ground,sx,sy,depth);
    world.getRenderer().screenToWorldAt(sx,sy+3,ground,cover.x,cover.y);
    cover.art=&wall;probe.all().push_back(cover);
    lighting.prepare(world,rm,probe,map,world.getRenderer(),0,viewRect.w,viewRect.h,renderer);
    lighting.drawEmission(renderer,world.getRenderer(),viewRect);
    report<<"Occlusion probe lights="<<lamps<<" seen="<<seen<<" coveredLights="<<lighting.stats().lights<<" coveredPixels="<<lighting.stats().emissives<<" cover="<<cover.x<<","<<cover.y<<"\n";
    check("foreground hides bulb pixels without switching off its ground light",lamps>0 && seen>0 && lighting.stats().lights==lamps && lighting.stats().emissives==0);
    Features::dayNightLighting=was;
  }
  auto &lighting=world.getLighting();
  const bool enabledWas=Features::dayNightLighting;
  Features::dayNightLighting=false;capture("lighting_off");
  int rw,rh;SDL_GetRendererOutputSize(renderer,&rw,&rh);
  auto pixels=[&]() {
    // capture() presents, so draw once again and read the current backbuffer.
    world.draw(renderer);
    std::vector<Uint32> p(rw*rh);
    SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,p.data(),rw*4);
    return p;
  };
  const auto off=pixels();
  Features::dayNightLighting=true;lighting.previewHour=12;for(int i=0;i<50;++i)world.draw(renderer);capture("lighting_noon");
  check("daytime projects solid mesh shadows",lighting.stats().shadows>0 && lighting.stats().modelFaces<=L::maxModelFaces);
  check("local-light index matches exhaustive obstruction rays",lighting.verifyLocalLightIndex());
  check("daylight shows no electric light sources",lighting.stats().lights==0 && lighting.stats().rays==0);
  lighting.previewHour=18;capture("lighting_sunset");
  lighting.previewHour=17.5;capture("lighting_low_sun");
  lighting.previewHour=0;capture("lighting_midnight");
  check("night has no directional sun shadows",lighting.stats().shadows==0);
  check("night restores the source art's emissive pixels",lighting.stats().emissives>0 && lighting.stats().emissives<=L::maxEmissives);
  auto night=pixels();
  int changed=0,outsideChanged=0;
  for(int y=0;y<rh;++y)for(int x=0;x<rw;++x) if(off[y*rw+x]!=night[y*rw+x]) {
    if(x>=viewRect.x && x<viewRect.x+viewRect.w && y>=viewRect.y && y<viewRect.y+viewRect.h)++changed;
    else ++outsideChanged;
  }
  check("lighting changes map pixels and leaves outside the viewport alone",changed>10000 && outsideChanged==0);
  check("light, mesh and lightmap budgets enforced",lighting.stats().lights>0 && lighting.stats().lights<=L::maxLights &&
        lighting.stats().modelFaces<=L::maxModelFaces && lighting.stats().width<=L::maxMapWidth && lighting.stats().height<=L::maxMapWidth);
  check("warm scene reuses static 3D light rays",lighting.stats().rays==0);
  report<<"Night: "<<lighting.stats().lights<<" lights, "<<lighting.stats().rays<<" new rays; "<<lighting.stats().width<<"x"<<lighting.stats().height<<" lightmap.\n";
  for(int rotation=0;rotation<4;++rotation) {
    lighting.previewHour=12;
    const auto day="lighting_day_rotation_"+std::to_string(rotation);capture(day.c_str());
    lighting.previewHour=0;
    const auto name="lighting_night_rotation_"+std::to_string(rotation);
    capture(name.c_str());world.rotateView(1);
  }
  for(float zoom:{.5f,2.0f}) {
    camera.zoom=zoom;capture(zoom<1?"lighting_night_zoom_out":"lighting_night_zoom_in");
    check("zoom preserves lightmap bounds",lighting.stats().width<=L::maxMapWidth && lighting.stats().height<=L::maxMapWidth);
  }
  camera.zoom=1;lighting.previewHour=6.5;capture("lighting_sunrise");
  world.getRenderer().centreOnWorld(39,39,map.getTile(39,39)->height);
  Features::dayNightLighting=false;
  auto after=pixels();
  check("switching off restores the original world pixels",off==after);
  // Flush and present each sample: includes real draw calls, driver submission
  // and camera pans. No FPS cap or screenshot readback in the measured section.
  SDL_RendererInfo info{};SDL_GetRendererInfo(renderer,&info);SDL_RenderSetVSync(renderer,0);
  report<<"Renderer: "<<(info.name?info.name:"unknown")<<", "<<rw<<"x"<<rh<<". Timings include moving sky, camera pans, world draw and present; HUD/simulation excluded.\n";
  for(int pass=0;pass<2;++pass)for(int mode=0;mode<3;++mode) {
    Features::dayNightLighting=mode!=0;lighting.previewHour=mode==1?12:0;
    std::vector<double> costs;
    FrameProfile::totals.clear();FrameProfile::active=true;
    for(int frame=0;frame<132;++frame) {
      SDL_PumpEvents();
      world.getRenderer().centreOnWorld(39+std::sin(frame*.04f)*5,39+std::cos(frame*.04f)*5,map.getTile(39,39)->height);
      if(mode)lighting.previewHour=(mode==1?12:0)+frame/1800.0;
      FrameProfile::begin();
      Uint64 start=SDL_GetPerformanceCounter();world.draw(renderer);SDL_RenderPresent(renderer);
      double ms=1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
      if(frame>=12)costs.push_back(ms);
    }
    FrameProfile::active=false;
    for(const auto &[name,ms]:FrameProfile::totals)report<<"  "<<name<<": "<<ms/132<<" ms.\n";
    std::sort(costs.begin(),costs.end());
    report<<"Pan pass "<<pass<<" "<<(mode==0?"off":mode==1?"noon":"night")<<": mean "<<std::accumulate(costs.begin(),costs.end(),0.0)/costs.size()<<" ms, median "<<costs[costs.size()/2]<<" ms, p95 "<<costs[costs.size()*95/100]<<" ms.\n";
  }
  SDL_RenderSetVSync(renderer,(info.flags&SDL_RENDERER_PRESENTVSYNC)?1:0);
  Features::dayNightLighting=true;lighting.previewHour=-1;world.setPaused(false);
  const double beforeTime=world.getClock();world.update(SDL_GetKeyboardState(nullptr),.5f);
  check("running zoo advances the lighting clock",std::abs(world.getClock()-beforeTime-.5)<.0001 && L::hourAt(world.getClock())>L::hourAt(beforeTime));
  capture("lighting_live_clock");world.setPaused(true);capture("lighting_paused_clock");
  // ours: the graphics quality's presets (GraphicsQuality.hpp) change what the
  // lighting makes, and High again is as it was
  {
    namespace Q=GraphicsQuality;
    world.setPaused(true);lighting.previewHour=12;for(int i=0;i<20;++i)world.draw(renderer);
    const int highSize=lighting.sunMaps[0].size,highFaces=lighting.stats().modelFaces;const size_t highActorFaces=lighting.actorFaces.size();
    report<<"Quality High: sun map "<<highSize<<", "<<highFaces<<" shape faces, "<<highActorFaces<<" actor faces.\n";
    Q::forThisRun(Q::Low);for(int i=0;i<30;++i)world.draw(renderer);capture("lighting_quality_low");
    report<<"Quality Low: sun map "<<lighting.sunMaps[0].size<<", "<<lighting.stats().modelFaces<<" shape faces, "<<lighting.actorFaces.size()<<" actor faces.\n";
    check("Low makes smaller, hard-edged sun maps",lighting.sunMaps[0].size>0 && lighting.sunMaps[0].size<highSize && lighting.sunMaps[0].soften==0);
    check("Low lights no more shapes than its budget",lighting.stats().modelFaces<=Q::shapeFaceBudget());
    bool creaturesFlat=true;
    for(const auto &f:lighting.actorFaces)if(f.instance>=lighting.instances.size()){const auto &o=lighting.actors[f.instance-lighting.instances.size()];
      if(!(((o.owner>>56)&1) && ((o.owner>>48)&1)))creaturesFlat=false;}
    check("Low's animals and guests are not lit in 3D",creaturesFlat);
    Q::forThisRun(Q::Ultra);for(int i=0;i<30;++i)world.draw(renderer);capture("lighting_quality_ultra");
    report<<"Quality Ultra: sun map "<<lighting.sunMaps[0].size<<".\n";
    check("Ultra makes larger sun maps",lighting.sunMaps[0].size>highSize);
    Q::forThisRun(Q::High);for(int i=0;i<30;++i)world.draw(renderer);
    report<<"Quality High again: sun map "<<lighting.sunMaps[0].size<<", "<<lighting.stats().modelFaces<<" shape faces, "<<lighting.actorFaces.size()<<" actor faces.\n";
    // (the shapes within 1%: a scenery item's animation can change one between the two)
    check("High again is as it was",lighting.sunMaps[0].size==highSize && lighting.stats().modelFaces<=Q::shapeFaceBudget() &&
          std::abs(lighting.stats().modelFaces-highFaces)<=highFaces/100 && lighting.actorFaces.size()==highActorFaces);
  }
  Features::dayNightLighting=enabledWas;lighting.previewHour=-1;
  report<<"Failures: "<<failures<<"\n";
  std::ofstream(out+"/lighting-checks.txt")<<report.str();SDL_Log("[LightingChecks] %s",report.str().c_str());
  if(!failures && std::getenv("ZT_LIGHTING_REVIEW")) {
    world.loadBlank(80,80);
    auto &scenery=world.objectsMutable();
    const std::vector<std::pair<std::string,SDL_FPoint>> choices={
      {"/lamp.ai",{37.5f,35.5f}},{"/bklamp.ai",{35.5f,35.5f}},
      {"/campfire.ai",{37.5f,40.5f}},{"/carousal.ai",{43.5f,37.5f}},
      {"/fountain.ai",{33.5f,37.5f}},{"/watfall1.ai",{33.5f,44.5f}},
      {"/bgrstnd.ai",{40.5f,44.5f}},{"/restrant.ai",{46.5f,44.5f}}};
    for(const auto &[suffix,point]:choices)for(const auto &item:ItemCatalog::get().all())
      if(item.file.size()>=suffix.size() && item.file.compare(item.file.size()-suffix.size(),suffix.size(),suffix)==0) {
        scenery.add(item.file,point.x,point.y,0);
        if(auto *placed=scenery.last())if(placed->used && (suffix=="/carousal.ai" || suffix=="/restrant.ai"))placed->inUse=true;
        SDL_Log("[LightingReview] %s at %.1f %.1f",item.file.c_str(),point.x,point.y);break;
      }
    for(int y=31;y<49;++y)for(int x=30;x<49;++x)
      if(x==39 || x==40 || y==33 || y==41 || y==42 || y==47)world.getMap().setPath(x,y,"path");
    world.getMap().touch();world.reindex();world.getCamera().zoom=1;
    world.getRenderer().centreOnWorld(39,39,world.getMap().getTile(39,39)->height);
    Features::dayNightLighting=true;world.setPaused(true);
    for(double h:{12.0,16.0,17.0,17.5,18.0,0.0,6.5}) {
      world.getLighting().previewHour=h;
      const auto name="review_hour_"+std::to_string(h);capture(name.c_str());
    }
    for(int r=0;r<4;++r) {
      world.getLighting().previewHour=0;capture(("review_night_view_"+std::to_string(r)).c_str());
      world.getLighting().previewHour=17.5;capture(("review_sunset_view_"+std::to_string(r)).c_str());world.rotateView(1);
    }
    std::ofstream audit(out+"/asset-audit.json");audit<<"{\"assets\":[";bool firstAsset=true;
    const char *directions[]={"NE","SE","SW","NW"};
    const CompassDirection sides[]={CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW};
    for(const auto &o:scenery.objects())if(const auto *asset=LightingAssets::find(o.typeName)) {
      const auto &cache=world.getLighting().assetModels();auto model=cache.find(std::make_tuple(o.art,asset->id,PlacedObjects::fileOf(o)));
      check((o.typeName+" has a reconstructed solid and a surface mesh").c_str(),model!=cache.end() && model->second.occupied>0 && !model->second.faces.empty());
      if(model==cache.end())continue;
      const auto &solid=model->second;std::ofstream mesh(out+"/"+o.typeName+".obj");int vertex=1;
      float highest=0;for(int z=0;z<solid.nz;++z)for(int y=0;y<solid.ny;++y)for(int x=0;x<solid.nx;++x)
        if(solid.contains(x,y,z))highest=std::max(highest,(z+1)*solid.step.z);
      auto occupiedAt=[&](float x,float y,float z){return solid.contains(int(std::floor((x-solid.minimum.x)/solid.step.x)),int(std::floor((y-solid.minimum.y)/solid.step.y)),int(std::floor(z/solid.step.z)));};
      if(asset->id==104)check("carousel has air between its support posts",!occupiedAt(.82f,.62f,2));
      if(asset->id==101)check("lamp uses a small authored mesh",solid.faces.size()<=96);
      if(asset->id==105)check("fountain water jet is excluded from solid shadow geometry",highest<3);
      for(const auto &face:solid.faces) {
        for(auto p:face.p)mesh<<"v "<<p.x<<" "<<p.y<<" "<<p.z<<"\n";
        mesh<<"f "<<vertex<<" "<<vertex+1<<" "<<vertex+2<<" "<<vertex+3<<"\n";vertex+=4;
      }
      if(!firstAsset)audit<<",";firstAsset=false;
      audit<<"{\"name\":\""<<o.typeName<<"\",\"cells\":"<<solid.occupied<<",\"faces\":"<<solid.faces.size()<<",\"height\":"<<highest<<",\"views\":[";
      for(int k=0;k<4;++k) {
        if(k)audit<<",";float left,top,right,bottom;o.art->anchoredBounds(0,0,sides[k],left,top,right,bottom);
        if(auto *s=o.art->frameSurface(directions[k],0))SDL_SaveBMP(s,(out+"/"+o.typeName+"-"+directions[k]+".bmp").c_str());
        audit<<"{\"side\":\""<<directions[k]<<"\",\"anchor\":["<<-left<<","<<-top<<"],\"regions\":[";
        bool firstRegion=true;for(const auto &region:asset->views[k]) {
          if(!firstRegion)audit<<",";firstRegion=false;
          audit<<"{\"name\":\""<<region.name<<"\",\"type\":"<<int(region.material)<<",\"stroke\":"<<region.stroke<<",\"points\":[";
          for(size_t p=0;p<region.outline.size();++p){if(p)audit<<",";audit<<"["<<region.outline[p].x<<","<<region.outline[p].y<<"]";}
          audit<<"]}";
        }
        audit<<"]}";
      }
      audit<<"]}";
    }
    audit<<"]}";
    report<<"Final failures after asset audit: "<<failures<<"\n";std::ofstream(out+"/lighting-checks.txt")<<report.str();
    world.getLighting().previewHour=-1;Features::dayNightLighting=false;
  }
  return failures?1:0;
}

// addition: raw frames for an external encoder. No encoder dependency ships
// with the game. The check launcher consumes this framed stdout stream; logs
// remain on stderr. A preview advances only lighting and sprite animation,
// keeping the disposable zoo's AI fixed so a long art review cannot kill it.
#include <filesystem>
// addition: full native lighting review, recorded from the real game renderer.
static int runCatalogueLightingVideo(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::function<void()> &draw) {
  constexpr int fps=24,duration=210;int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
  if(auto *window=SDL_RenderGetWindow(renderer))SDL_HideWindow(window);
#ifdef _WIN32
  _setmode(_fileno(stdout),_O_BINARY);
#endif
  std::printf("\nZT_LIGHTING_FRAMES %d %d %d %d\n",w,h,fps,duration*fps);std::fflush(stdout);
  std::vector<Uint32> pixels(size_t(w)*h);std::vector<std::string> catalogue;
  for(const auto &asset:LightingAssets::all())if(asset.id>=200 && asset.id<500 && std::string(asset.item)!="santa")
    for(const auto &item:ItemCatalog::get().all())if(std::filesystem::path(item.file).stem().string()==asset.item){catalogue.push_back(item.file);break;}
  auto &light=world.getLighting();auto &view=world.getRenderer();int lastScene=-1,lastRotation=0;SDL_Texture *caption=nullptr;std::string captionKey;
  for(int f=0;f<fps*duration;++f){SDL_PumpEvents();const double seconds=double(f)/fps;
    const int scene=seconds<96?0:seconds<196?1+int((seconds-96)/10):11;
    if(scene!=lastScene){world.loadBlank(100,100);world.setPaused(true);world.getFences().setEditing(true);world.getCamera().zoom=.85f;lastRotation=view.getRotation();world.rotateView(-lastRotation);lastRotation=0;
      std::vector<std::string> files;
      if(scene==0)files={"scenery/building/carousal.ai","scenery/building/restrant.ai","scenery/building/dfnt1.ai","scenery/building/bgrstnd.ai","scenery/other/lamp.ai","scenery/building/junrest.ai","scenery/building/fount2.ai","scenery/other/lightho.ai","scenery/foliage/xmastree.ai","scenery/building/campfire.ai","scenery/other/jappond.ai","scenery/other/dgate.ai"};
      else if(scene<11)for(int i=(scene-1)*11;i<std::min(int(catalogue.size()),scene*11);++i)files.push_back(catalogue[i]);
      int placed=0;for(const auto &file:files)if(world.objectsMutable().add(file,30.5f+(placed%4)*6,32.5f+(placed/4)*6,0))++placed;
      for(int y=28;y<=50;++y)for(int x=27;x<=54;++x)if(x%6==4 || y%6==0)world.getMap().setPath(x,y,"path");
      if(scene==0){auto &animals=world.getAnimals();int i=0;
        for(const char *file:{"animals/elephant.ai","animals/giraffe.ai","animals/lion.ai"}){int type=animals.typeOfFile(file);if(type<0)continue;int id=animals.adopt(type,false,34.5f+i*3,46.5f,world.getMap(),world.getFences(),true);if(auto *m=animals.member(id)){m->boxed=false;m->egg=false;m->anim="walk";m->fx=0;m->fy=-1;}++i;}
        auto &guests=world.getGuests();guests.setEntrance(39,44);for(int j=0;j<8;++j)guests.admit(0);auto &people=const_cast<std::vector<Guests::Guest>&>(guests.guests());for(size_t j=0;j<people.size();++j){auto &g=people[j];g.hidden=false;g.x=30.5f+j*2;g.y=47.5f;g.anim="walk";g.fx=0;g.fy=-1;}
      }
      if(scene==11){auto &animals=world.getAnimals();int i=0;
        for(const char *file:{"animals/trex.ai","animals/velocira.ai","animals/ankylo.ai","animals/smilo.ai"}){int type=animals.typeOfFile(file);if(type<0)continue;int id=animals.adopt(type,false,34.5f+i*4,38.5f,world.getMap(),world.getFences(),true);if(auto *m=animals.member(id)){m->boxed=false;m->egg=false;m->anim="test_fence";m->fx=0;m->fy=-1;}++i;}
        auto &guests=world.getGuests();guests.setEntrance(39,44);for(int i=0;i<8;++i)guests.admit(0);auto &people=const_cast<std::vector<Guests::Guest>&>(guests.guests());
        for(size_t i=0;i<people.size();++i){auto &g=people[i];g.hidden=false;g.x=33.5f+i*2;g.y=44.5f;const auto &key=guests.types()[g.type].key;g.anim=key=="man"?(i%2?"camera":"fencezap"):key=="woman"?"camera":"walk";g.fx=0;g.fy=-1;}
        int electric=-1;for(size_t t=0;t<world.getFences().types().size();++t)if(world.getFences().types()[t].electrified){electric=int(t);break;}
        if(electric>=0)for(int x=31;x<49;++x)world.getFences().place({true,x,37},electric,0,world.getMap());
      }
      world.getMap().touch();world.reindex();view.centreOnWorld(39.5f,39.5f,0);Features::dayNightLighting=true;light.previewHour=scene==0?12:0;for(int i=0;i<30;++i)draw();lastScene=scene;
    }
    const int rotation=scene>0 && scene<11?std::min(3,int(std::fmod(seconds-96,10)/2.5)):scene==11?int((seconds-196)/3.5)%4:0;
    if(rotation!=lastRotation){world.rotateView(rotation-lastRotation);lastRotation=rotation;}
    light.previewHour=scene==0?std::fmod(12+seconds*.25,24):scene==11?(seconds<201?17.5:0):0;
    world.objectsMutable().tick(1.0f/fps);
    for(const auto &m:world.getAnimals().members())if(auto *a=world.getAnimals().member(m.id))a->animTime+=1.0f/fps;
    auto &people=const_cast<std::vector<Guests::Guest>&>(world.getGuests().guests());for(auto &g:people)g.animTime+=1.0f/fps;
    draw();std::string text=scene==0?"Day and night - full cycle at 7.5x preview speed":scene<11?"Native windows, signs, lamps, fire and water - group "+std::to_string(scene)+" of 10":"Native electric shocks and camera flashes - cosmetic lighting";
    if(text!=captionKey){rm->setTextScale(1);caption=rm->getStringTexture(renderer,25001,text,{255,232,172,255},25002);captionKey=text;}
    if(caption){int tw,th;SDL_QueryTexture(caption,nullptr,nullptr,&tw,&th);SDL_Rect bg{195,h-90,tw+24,th+18},t{207,h-81,tw,th};SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(renderer,20,35,28,235);SDL_RenderFillRect(renderer,&bg);SDL_RenderCopy(renderer,caption,nullptr,&t);}
    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4) || std::fwrite(pixels.data(),4,pixels.size(),stdout)!=pixels.size())return 1;
    if(f%fps==0)std::fflush(stdout);
  }
  std::fflush(stdout);Features::dayNightLighting=false;light.previewHour=-1;return 0;
}

int runLightingVideo(World &world,ResourceManager *rm,SDL_Renderer *renderer,
                     const std::function<void()> &draw,const std::function<void(bool)> &options) {
  if(std::string(std::getenv("ZT_LIGHTING_VIDEO"))=="catalogue"){options(false);return runCatalogueLightingVideo(world,rm,renderer,draw);}
  const bool overview=std::string(std::getenv("ZT_LIGHTING_VIDEO"))=="overview";
  constexpr int fps=24;const int duration=overview?150:720;
  int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
  if(w%2 || h%2)return 1;
  if(auto *window=SDL_RenderGetWindow(renderer))SDL_HideWindow(window);
#ifdef _WIN32
  _setmode(_fileno(stdout),_O_BINARY);
#endif
  std::printf("\nZT_LIGHTING_FRAMES %d %d %d %d\n",w,h,fps,duration*fps);std::fflush(stdout);
  std::vector<Uint32> pixels(w*h);
  auto &light=world.getLighting();auto &camera=world.getCamera();auto &view=world.getRenderer();
  SDL_Texture *caption=nullptr;std::string lastCaption;
  int lastStage=-1,lastRotation=0;
  world.setPaused(false);
  for(int frame=0;frame<duration*fps;++frame) {
    SDL_PumpEvents();const double seconds=double(frame)/fps;
    int stage=overview?(seconds<4?0:seconds<8?1:seconds<104?2:seconds<136?3:seconds<144?4:5):2;
    Features::dayNightLighting=stage!=1;
    if(stage!=lastStage) { options(stage==0);lastStage=stage; }
    double cycleSeconds=overview?(seconds-8)*7.5:seconds;
    light.previewHour=stage==2?std::fmod(12+cycleSeconds/30,24.0):0;
    std::string description;
    if(stage==0)description="Graphics option: Day and night lighting";
    if(stage==1)description="Lighting off: original daylight";
    if(stage==2)description=overview?"Full day and night cycle - 7.5x preview speed":"Full day and night cycle - normal visual speed (12 minutes)";
    int rotation=stage==3?std::min(3,int((seconds-104)/8)):0;
    if(rotation!=lastRotation) { world.rotateView(rotation-lastRotation);lastRotation=rotation; }
    if(stage==3)description="Night lighting - camera rotation "+std::to_string(rotation+1)+" of 4";
    camera.zoom=stage==4?(seconds<140?.5f:2.0f):1.0f;
    if(stage==4)description=camera.zoom<1?"Night lighting - zoomed out":"Night lighting - zoomed in";
    if(stage==5) { world.setPaused(true);light.previewHour=-1;description="Paused zoo: time of day stops with the game"; }
    const float travel=stage==2?float(std::sin(seconds*.025)*2):0;
    view.centreOnWorld(39+travel,39,world.getMap().getTile(39,39)->height);
    if(!world.isPaused()) {
      world.objectsMutable().tick(1.0f/fps);
      for(const auto &m:world.getAnimals().members())if(auto *animal=world.getAnimals().member(m.id))animal->animTime+=1.0f/fps;
    }
    draw();
    if(description!=lastCaption) {
      rm->setTextScale(1);
      caption=rm->getStringTexture(renderer,25001,description,{255,232,172,255},25002);
      lastCaption=description;
    }
    if(caption) {
      int tw,th;SDL_QueryTexture(caption,nullptr,nullptr,&tw,&th);
      SDL_Rect bg={195,h-90,tw+24,th+18},text={207,h-81,tw,th};
      SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer,20,35,28,235);SDL_RenderFillRect(renderer,&bg);
      SDL_RenderCopy(renderer,caption,nullptr,&text);
    }
    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4)!=0 ||
       std::fwrite(pixels.data(),4,pixels.size(),stdout)!=pixels.size())return 1;
    if(frame%fps==0)std::fflush(stdout);
  }
  std::fflush(stdout);light.previewHour=-1;Features::dayNightLighting=false;options(false);
  return 0;
}
