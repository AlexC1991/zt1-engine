#include "World.hpp"
#include "ItemCatalog.hpp"
#include "IniReader.hpp"
#include "SDL_image.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

// addition: native beacon/state inspection, kept outside the source/art bundle.
int runLightingBeaconAudit(World &world,ResourceManager *rm,SDL_Renderer *,const std::string &out) {
  namespace fs=std::filesystem;fs::create_directories(out+"/native");
  std::ofstream csv(out+"/frames.csv");csv<<"resource,state,side,frame,total,anchor_x,anchor_y,mirrored,path\n";
  auto token=[](std::string s){for(char &c:s)if(c=='/' || c=='.' || c==':')c='_';return s;};int count=0;
  auto save=[&](const std::string &file,const std::string &state,Animation *a){if(!a)return;const auto dir="native/"+token(file)+"/"+token(state);fs::create_directories(fs::path(out)/dir);const char *names[]={"NE","SE","SW","NW"};int v=0;
    for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW}){
      for(int f=0;f<a->frameCount();++f){auto native=a->lightingFrame(side,f);if(!native.surface)continue;const auto path=dir+"/"+names[v]+"-"+std::to_string(f)+".png";
        if(!IMG_SavePNG(native.surface,(fs::path(out)/path).string().c_str())){csv<<file<<','<<state<<','<<names[v]<<','<<f<<','<<a->frameCount()<<','<<native.anchor.x<<','<<native.anchor.y<<','<<native.mirrored<<','<<path<<'\n';++count;}
      }++v;SDL_PumpEvents();
    }
  };
  auto &staff=world.getStaff();
  for(size_t i=0;i<staff.types().size();++i){const auto &t=staff.types()[i];if(t.kind!=Staff::Kind::Helicopter)continue;Staff::Member m;m.type=int(i);
    for(const auto &[state,path]:t.anims[0])save(t.file,state,staff.art(m,state));save(t.baseFile,"idle",t.base);save(t.baseFile,"used",t.baseUsed);
  }
  for(const auto &i:ItemCatalog::get().all())if(i.file.starts_with("scenery/")) {
    const auto stem=fs::path(i.file).stem().string();const auto *profile=LightingAssets::find(stem);if(!profile || i.file=="scenery/building/helibase.ai")continue;
    save(i.file,"idle",world.objectsMutable().artOfFile(i.file));
    std::unique_ptr<IniReader> ai{rm->getIniReader(i.file)};if(!ai)continue;
    for(const auto &[state,value]:ai->getSection("animations"))if(state!="idle"){
      std::string path=value;std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return char(std::tolower(c));});std::replace(path.begin(),path.end(),'\\','/');if(path.ends_with(".ani"))path.resize(path.size()-4);
      if(path.find('/')==std::string::npos){const auto name=path;path.clear();for(const char *root:{"objects","food"})for(const auto &type:{stem,i.type}){const auto p=std::string(root)+"/"+type+"/"+name+"/"+name;if(path.empty() && rm->hasResource(p+".ani"))path=p;}}
      if(!path.empty() && rm->hasResource(path+".ani"))save(i.file,state,rm->getAnimation(path));
    }
  }
  std::ofstream(out+"/audit.txt")<<"Native frames exported: "<<count<<'\n';return 0;
}

#include "Features.hpp"
#include "RenderSettings.hpp"
#include <cmath>
#include <sstream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
// addition: native four-view fixture checks and a controlled visual state reel.
// The reel deliberately freezes AI: it demonstrates drawing/lighting states,
// not a claim that a scripted pursuit proves original helicopter task rules.
int runLightingBeaconChecks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out,const std::function<void()> &draw) {
  using namespace LightingAssets;int failed=0;std::ostringstream log;
  auto check=[&](const std::string &name,bool ok){log<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;};
  auto snapshot=[&](const std::string &name){int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);auto *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);if(!s){++failed;return;}
    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,s->pixels,s->pitch) || IMG_SavePNG(s,(out+"/"+name+".png").c_str()))++failed;SDL_FreeSurface(s);};
  auto checksum=[&](){int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);std::vector<Uint32> pixels(size_t(w)*h);SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4);uint64_t hash=1469598103934665603ull;for(auto p:pixels){hash^=p;hash*=1099511628211ull;}return hash;};
  const bool was=Features::dayNightLighting;world.loadBlank(80,80);world.setPaused(true);world.getCamera().zoom=1;
  auto &staff=world.getStaff();auto &map=world.getMap();auto &fences=world.getFences();fences.setEditing(true);int type=-1;
  for(size_t i=0;i<staff.types().size();++i)if(staff.types()[i].kind==Staff::Kind::Helicopter){type=int(i);break;}
  check("native helicopter team definition exists",type>=0);if(type<0){std::ofstream(out+"/beacon-checks.txt")<<log.str();return 1;}
  const int id=staff.hire(type,38.5f,37.5f,map,fences);auto *m=staff.member(id);check("real helicopter team can be hired",m!=nullptr);if(!m)return 1;
  const auto &t=staff.types()[type];m->anim="idle";m->heliX=m->x+t.padX;m->heliY=m->y+t.padY;m->heliZ=t.padZ;m->fx=0;m->fy=-1;
  world.objectsMutable().add("scenery/building/cinema.ai",44.5f,40.5f,0);world.objectsMutable().add("scenery/building/dkarcade.ai",34.5f,44.5f,0);
  for(int y=32;y<48;++y)for(int x=31;x<49;++x)if(x==41 || y==42)map.setPath(x,y,"path");
  world.reindex();world.getRenderer().centreOnWorld(40,39,2);auto &light=world.getLighting();Features::dayNightLighting=true;light.previewHour=0;
  for(int i=0;i<30;++i)draw();
  std::ofstream frames(out+"/beacon-frames.csv");frames<<"state,side,frame,steady_pixels,beacon_pixels\n";
  auto regionsCount=[&](Animation *a,CompassDirection side,int f,PixelMask mask){int pixels=0;const auto native=a->lightingFrame(side,f);if(!native.surface)return 0;
    const auto *d=find("helibase");const auto v=viewIndex(int(side));auto *s=native.surface;
    for(const auto &r:d->views[v])if(r.pixels==mask)for(int y=0;y<s->h;++y)for(int x=0;x<s->w;++x){float px=x+.5f-native.anchor.x;if(native.mirrored)px=-px;
      if(!inside(r,px,y+.5f-native.anchor.y))continue;Uint8 red,g,b,alpha;SDL_GetRGBA(reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(s->pixels)+y*s->pitch)[x],s->format,&red,&g,&b,&alpha);
      pixels+=alpha>0 && pixelAllowed(mask,red,g,b);
    }return pixels;};
  check("helipad has native used animation",t.baseUsed && t.baseUsed->frameCount()==9);
  int v=0;for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW}) {
    int peakBlue=0,peakRed=0;std::set<int> redCounts;bool stable=true;SDL_Texture *first=nullptr;
    for(int f=0;t.baseUsed && f<t.baseUsed->frameCount();++f){const int blue=regionsCount(t.baseUsed,side,f,PixelMask::BlueLens),red=regionsCount(t.baseUsed,side,f,PixelMask::RedLens);peakBlue=std::max(peakBlue,blue);peakRed=std::max(peakRed,red);redCounts.insert(red);
      const auto &patches=t.baseUsed->emissionPatches(renderer,side,f,239);if(f==0 && !patches.empty())first=patches.front().texture;
      stable=stable && !patches.empty();frames<<"helipad-used,"<<v<<','<<f<<','<<blue<<','<<red<<'\n';
    }
    check("helipad view "+std::to_string(v)+" blue guide lenses and red beacon emit",peakBlue>=12 && peakRed>=1 && stable);
    check("helipad view "+std::to_string(v)+" beacon follows native varying frames",redCounts.size()>1 && first);
    for(const char *state:{"warmup","fly","level","fire"}) {
      auto *a=staff.art(*m,state);int low=100000,lowBeacon=100000,high=0;
      if(a)for(int f=0;f<a->frameCount();++f){int steady=0,beacon=0;for(const auto &p:a->emissionPatches(renderer,side,f,aircraftProfile(state,0)))steady+=p.pixels;
        for(const auto &p:a->emissionPatches(renderer,side,f,aircraftProfile(state,1)))beacon+=p.pixels;
        low=std::min(low,steady);lowBeacon=std::min(lowBeacon,beacon);high=std::max(high,steady+beacon);frames<<state<<','<<v<<','<<f<<','<<steady<<','<<beacon<<'\n';
      }
      check(std::string(state)+" view "+std::to_string(v)+" attachments stay on airframe in every frame",a && low>=2 && lowBeacon>=1 && high<=24);
    }++v;
  }
  check("painted tail and rotor are not whole-sprite glow masks",!pixelAllowed(PixelMask::BlueLens,200,25,20) && !pixelAllowed(PixelMask::RedLens,200,180,60));
  check("unknown, idle and shadow aircraft states have no powered marker map",aircraftProfile("idle",0)==0 && aircraftProfile("shadowfly",0)==0 && aircraftProfile("missing",1)==0);
  // The display frame is shared with the real staff collector, including reverse.
  m->baseAnimTime=4.1*std::max(1u,t.baseUsed->frameTimeMs())/1000;check("helipad uses current native frame rather than frozen frame zero",staff.baseFrame(*m)==4);
  draw();bool actualPad=false;const auto &expected=t.baseUsed->emissionPatches(renderer,world.getRenderer().screenSide(0,-1),4,239);
  for(const auto &e:light.emitters)if(e.kind==239)for(const auto &p:expected)actualPad|=e.texture==p.texture;
  check("runtime helipad emission uses the same frame as its artwork",actualPad);
  const auto structure=light.structureHash;m->baseAnimTime+=.1;draw();check("beacon frames do not invalidate static geometry",light.structureHash==structure);
  auto aircraftCount=[&](){return std::count_if(light.emitters.begin(),light.emitters.end(),[](const auto &e){return e.kind>=2000 && e.kind<2010;});};
  check("parked aircraft markers are switched off",aircraftCount()==0);
  m->heliStage=Staff::HeliStage::SpinUp;m->anim="warmup";m->animTime=.2f;m->baseAnimTime=.09;draw();check("powered startup enables markers and pulse",aircraftCount()==2);
  m->baseAnimTime=.5;draw();check("beacon turns off between pulses while position markers stay lit",aircraftCount()==1);
  check("aircraft markers do not allocate camera-flash ground lights",light.transientLights.empty());
  auto *warm=staff.art(*m,"warmup");m->heliStage=Staff::HeliStage::SpinDown;m->animTime=.2f;
  check("spin-down lighting samples the reversed native pose",warm && staff.helicopterFrame(*m,warm)==std::max(0,warm->frameCount()-1-int(.2f*1000/std::max(1u,warm->frameTimeMs()))));
  light.previewHour=12;draw();check("daylight suppresses aircraft and pad emission",light.emitters.empty());
  Features::dayNightLighting=false;draw();const auto before=checksum();Features::dayNightLighting=true;light.previewHour=0;draw();Features::dayNightLighting=false;draw();
  check("switching lighting off restores the original rendered frame",checksum()==before && !RenderSettings::relightWorld);
  Features::dayNightLighting=true;light.previewHour=0;
  m->heliStage=Staff::HeliStage::Chase;m->anim="fly";m->animTime=.2f;m->heliX=m->x+1;m->heliY=m->y;m->heliZ=8;m->baseAnimTime=6.1*std::max(1u,t.baseUsed->frameTimeMs())/1000;
  draw();bool awayPad=false;for(const auto &e:light.emitters)if(e.kind==239)awayPad=true;
  check("pad guide lights keep operating while the helicopter is away",awayPad);
  for(const auto &object:world.objectsMutable().objects()) {
    const auto *d=find(object.typeName);if(!d || !d->nightFixtures || !object.used)continue;
    const auto side=world.getRenderer().screenSide(0,-1);const auto &patches=object.used->emissionPatches(renderer,side,PlacedObjects::animationFrame(object,object.used),d->id+3000);bool found=false;
    for(const auto &e:light.emitters)if(e.kind==d->id+3000)for(const auto &p:patches)found|=e.texture==p.texture;
    check(std::string(d->item)+" empty building runs its native fixture sequence at night",found && !object.inUse);
  }
  for(int r=0;r<4;++r){draw();snapshot("beacons-night-"+std::to_string(r));world.rotateView(1);}
  // A separate recorded state reel uses exactly the native frame helpers above.
  std::ofstream(out+"/beacon-checks.txt")<<log.str();
  const bool video=std::string(std::getenv("ZT_LIGHTING_BENCH"))=="beacon-video";
  if(video && !failed) {
    constexpr int fps=24,seconds=80;int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);if(auto *window=SDL_RenderGetWindow(renderer))SDL_HideWindow(window);
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    std::printf("\nZT_LIGHTING_FRAMES %d %d %d %d\n",w,h,fps,seconds*fps);std::fflush(stdout);std::vector<Uint32> pixels(size_t(w)*h);int rotation=0;std::string last;SDL_Texture *caption=nullptr;
    for(int f=0;f<seconds*fps;++f) {
      const float time=float(f)/fps,q=std::fmod(time,20.0f);int rot=int(time/20);if(rot!=rotation){world.rotateView(rot-rotation);rotation=rot;}
      std::string label;m->heliX=m->x+t.padX;m->heliY=m->y+t.padY;m->heliZ=t.padZ;m->fx=std::sin(t.padFacing*.7853981634f);m->fy=-std::cos(t.padFacing*.7853981634f);
      float start=0;
      if(q<2){m->heliStage=Staff::HeliStage::Parked;m->anim="idle";label="Parked - aircraft power off";}
      else if(q<5){start=2;m->heliStage=Staff::HeliStage::SpinUp;m->anim="warmup";label="Startup - powered markers and beacon";}
      else if(q<7){start=5;m->heliStage=Staff::HeliStage::Ascend;m->anim="fly";m->heliZ+=3*(q-5)/2;label="Climb - markers follow aircraft height";}
      else if(q<11){start=7;m->heliStage=Staff::HeliStage::Chase;m->anim="fly";m->heliZ=8;m->heliX+=2*std::sin((q-7)*1.57f);m->heliY+=1.5f*std::cos((q-7)*1.57f);m->fx=std::cos((q-7)*1.57f);m->fy=-std::sin((q-7)*1.57f);label="Flight - moving airframe lights";}
      else if(q<12.5f){start=11;m->heliStage=Staff::HeliStage::Fire;m->anim="fire";m->heliZ=8;label="Tranquillizer firing pose";}
      else if(q<14){start=12.5f;m->heliStage=Staff::HeliStage::Return;m->anim="level";m->heliZ=8;label="Return - steady position markers";}
      else if(q<16){start=14;m->heliStage=Staff::HeliStage::Descend;m->anim="level";m->heliZ+=3*(16-q)/2;label="Descent - lights stay attached";}
      else if(q<19){start=16;m->heliStage=Staff::HeliStage::SpinDown;m->anim="warmup";label="Shutdown - reversed rotor sequence";}
      else{start=19;m->heliStage=Staff::HeliStage::Parked;m->anim="idle";label="Parked - aircraft power off";}
      m->animTime=q-start;m->baseAnimTime=time;world.objectsMutable().tick(1.0f/fps);draw();
      label="Native lighting review - view "+std::to_string(rot+1)+" of 4 - "+label;
      if(last!=label){rm->setTextScale(1);caption=rm->getStringTexture(renderer,25001,label,{255,232,172,255},25002);last=label;}
      if(caption){int tw,th;SDL_QueryTexture(caption,nullptr,nullptr,&tw,&th);SDL_Rect bg{195,h-90,tw+24,th+18},dst{207,h-81,tw,th};SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(renderer,20,35,28,235);SDL_RenderFillRect(renderer,&bg);SDL_RenderCopy(renderer,caption,nullptr,&dst);}
      if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4) || std::fwrite(pixels.data(),4,pixels.size(),stdout)!=pixels.size()){failed=1;break;}if(f%fps==0)std::fflush(stdout);
    }std::fflush(stdout);
  }
  light.previewHour=-1;Features::dayNightLighting=was;return failed?1:0;
}
