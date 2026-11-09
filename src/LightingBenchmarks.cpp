#include "World.hpp"
#include "LightingCache.hpp"
#include "Features.hpp"
#include "ItemCatalog.hpp"
#include "IniReader.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <numeric>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib,"psapi.lib")
#endif
namespace FrameProfile {extern bool active;extern std::map<std::string,double> totals;void begin();}
namespace {
double elapsed(Uint64 start){return 1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();}
size_t privateMemory(){
#ifdef _WIN32
 PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof p;
 if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof p))return p.PrivateUsage;
#endif
 return 0;
}
double threadMilliseconds(){
#ifdef _WIN32
 FILETIME a,b,k,u;if(GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u)) {
  ULARGE_INTEGER kernel{},user{};kernel.LowPart=k.dwLowDateTime;kernel.HighPart=k.dwHighDateTime;user.LowPart=u.dwLowDateTime;user.HighPart=u.dwHighDateTime;
  return (kernel.QuadPart+user.QuadPart)/10000.0;
 }
#endif
 return 0;
}
std::string category(const CatalogItem &i) {
 if(i.registrySection=="animals")return "animals";
 if(i.members.count("staff"))return "staff";
 if(i.members.count("foliage"))return "foliage";
 if(i.members.count("rocks"))return "rocks";
 if(i.file.find("scenery/building/")==0)return "buildings";
 if(i.registrySection=="fences" || i.registrySection=="tankwall")return "fences";
 if(i.members.count("paths"))return "paths";
 return "other";
}
}
// addition: measurements and catalogue audit from the actual renderer. Files
// are data for a local report; no performance claim is inferred from a check.
int runLightingBenchmarks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out,const std::function<void()> &draw) {
 const std::string scenario=std::getenv("ZT_LIGHTING_BENCH");
 if(scenario=="gpu"){extern int runGpuLightingChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);return runGpuLightingChecks(world,rm,renderer,out,draw);}
 if(scenario=="beacons" || scenario=="beacon-video"){extern int runLightingBeaconChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);return runLightingBeaconChecks(world,rm,renderer,out,draw);}
 if(scenario=="projection" || scenario=="projection-video" || scenario=="fixtures" || scenario=="fixtures-video" || scenario=="hd-materials" || scenario=="hd-review"){extern int runLightingProjectionChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);return runLightingProjectionChecks(world,rm,renderer,out,draw);}
 if(scenario=="beacon-audit"){extern int runLightingBeaconAudit(World &,ResourceManager *,SDL_Renderer *,const std::string &);return runLightingBeaconAudit(world,rm,renderer,out);}
 if(scenario=="details"){extern int runLightingDetailChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);return runLightingDetailChecks(world,rm,renderer,out,draw);}
 if(scenario=="materials") {extern int runLightingCatalogueChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &);return runLightingCatalogueChecks(world,rm,renderer,out);}
 if(scenario=="coverage") {extern int runLightingAssetAudit(World &,ResourceManager *,SDL_Renderer *,const std::string &);return runLightingAssetAudit(world,rm,renderer,out);}
 const bool simulation=std::getenv("ZT_BENCH_SIM")!=nullptr;
 if(const char *height=std::getenv("ZT_BENCH_RES"))if(auto *window=SDL_GetRenderWindow(renderer))SDL_SetWindowSize(window,std::atoi(height)*16/9,std::atoi(height));
 world.loadBlank(scenario=="busy"?120:80,scenario=="busy"?120:80);
 world.getFences().setEditing(true);
 auto &map=world.getMap();auto &objects=world.objectsMutable();auto &animals=world.getAnimals();auto &staff=world.getStaff();auto &guests=world.getGuests();
 std::ofstream inventory(out+"/catalogue.csv");inventory<<"file,category,section,authored_profile,source_count,mod\n";
 for(const auto &i:ItemCatalog::get().all()) {
  std::string name=i.file.substr(i.file.find_last_of('/')+1);if(name.size()>3)name.resize(name.size()-3);const auto *a=LightingAssets::find(name);
  inventory<<i.file<<","<<category(i)<<","<<i.registrySection<<","<<(a?a->id:0)<<","<<(a?a->sources.size():0)<<","<<i.fromMod<<"\n";
 }
 inventory.close();
 if(scenario=="audit") {
  std::ofstream audit(out+"/model-audit.csv");audit<<"file,category,faces,cells,buffer_bytes,build_ms,frames,success,native_frame_bytes\n";
  auto record=[&](const std::string &file,const std::string &group,Animation *art,float hx,float hy,bool actor) {
   Uint64 start=SDL_GetPerformanceCounter();LightingAssets::Model model;
   if(art){LightingAssets::Definition plain{0,"catalogue audit"};std::string name=file.substr(file.find_last_of('/')+1);if(name.size()>3)name.resize(name.size()-3);const auto *known=LightingAssets::find(name);
    model=LightingAssets::build(*art,known?*known:plain,hx,hy,actor?0:-1,actor);}
   size_t nativeBytes=0;if(art)art->forEachFrame([&](const std::string &,int,SDL_Surface *s){nativeBytes+=size_t(s->w)*s->h*4;});
   audit<<file<<","<<group<<","<<model.faces.size()<<","<<model.occupied<<","<<model.solid.capacity()+model.faces.capacity()*sizeof(LightingAssets::Face)<<","<<elapsed(start)<<","<<(art?art->frameCount():0)<<","<<(!model.faces.empty())<<","<<nativeBytes<<"\n";audit.flush();SDL_PumpEvents();
  };
  for(const auto &i:ItemCatalog::get().all())if(i.file.find("scenery/")==0) {
   float hx=.25f,hy=.25f;if(std::unique_ptr<IniReader> ai{rm->getIniReader(i.file)}){hx=std::max(.15f,ai->getInt("characteristics/integers","cfootprintx",1)/4.0f);hy=std::max(.15f,ai->getInt("characteristics/integers","cfootprinty",1)/4.0f);}
   record(i.file,category(i),objects.artOfFile(i.file),hx,hy,false);
  }
  for(size_t i=0;i<animals.types().size();++i) {
   const auto &t=animals.types()[i];Animals::Member m;m.type=int(i);m.id=-1000-int(i);m.x=40.5f;m.y=40.5f;m.anim=t.underwater?"subswim":"walk";
   record(t.file,t.underwater?"marine animals":t.dinosaur?"dinosaurs":"zoo animals",animals.art(m,m.anim),std::max(.25f,t.footX/4.0f),std::max(.25f,t.footY/4.0f),true);
   for(int subtype=1;subtype<3;++subtype){m.female=subtype==1;m.baby=subtype==2;m.anim=t.underwater?"subswim":"walk";
    // Geometry coverage needs an available native representative. Marine Mania
    // declares a young tiger subswim reference absent from the stock archives;
    // its existing land walk supplies the young-body geometry check instead.
    if(!animals.art(m,m.anim)){SDL_Log("[LightingAudit] %s subtype %d: unavailable %s, checking native land walk",t.file.c_str(),subtype,m.anim.c_str());m.anim="walk";}
    if(!animals.art(m,m.anim))SDL_Log("[LightingAudit] missing %s subtype %d state %s native %s",t.file.c_str(),subtype,m.anim.c_str(),t.anims[subtype].count(m.anim)?t.anims[subtype].at(m.anim).c_str():"adult fallback");
    record(t.file+(subtype==1?":female":":young"),"animal variants",animals.art(m,m.anim),std::max(.25f,t.footX/4.0f),std::max(.25f,t.footY/4.0f),true);}
   m.female=m.baby=false;
   for(const char *state:{"egg_idle","egg_hatch","egg_break","box_idle","box_used"})if(t.anims[0].count(state))record(t.file+":"+state,"animal components",animals.art(m,state),std::max(.25f,t.footX/4.0f),std::max(.25f,t.footY/4.0f),true);
  }
  for(size_t i=0;i<staff.types().size();++i){Staff::Member m;m.type=int(i);record(staff.types()[i].file,"staff",staff.art(m,"walk"),.35f,.35f,true);}
  for(size_t i=0;i<guests.types().size();++i){Guests::Guest g;g.type=int(i);record(guests.types()[i].key,"guests",guests.art(g,"walk"),.35f,.35f,true);}
  return 0;
 }
 std::vector<std::string> buildings,foliage,rocks;
 for(const auto &i:ItemCatalog::get().all()) {
  if(i.file.find("scenery/building/")==0 && buildings.size()<12)buildings.push_back(i.file);
  if(i.members.count("foliage") && foliage.size()<6)foliage.push_back(i.file);
  if(i.members.count("rocks") && rocks.size()<4)rocks.push_back(i.file);
 }
 int side=scenario=="small"?3:(scenario=="populated" || scenario=="placement")?6:scenario=="busy"?10:0;
 int lampCount=0;
 if(scenario=="preview") {
  for(const auto &[name,p]:std::vector<std::pair<std::string,SDL_FPoint>>{{"lamp",{37.5f,35.5f}},{"bklamp",{35.5f,35.5f}},{"campfire",{37.5f,40.5f}},{"carousal",{43.5f,37.5f}},{"fountain",{33.5f,37.5f}},{"watfall1",{33.5f,44.5f}},{"bgrstnd",{40.5f,44.5f}},{"restrant",{46.5f,44.5f}}})
   for(const auto &item:ItemCatalog::get().all())if(item.file.ends_with("/"+name+".ai")){objects.add(item.file,p.x,p.y,0);break;}
 }
 if(scenario.rfind("lamps",0)==0) {
  lampCount=std::stoi(scenario.substr(5));world.getLighting().diagnosticLightLimit=lampCount;
  for(int i=0;i<lampCount;++i)objects.add("scenery/other/lamp.ai",29.5f+(i%16)*1.3f,29.5f+(i/16)*1.3f,0);
 }
 if(side)for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
  const float px=24.5f+x*4.5f,py=24.5f+y*4.5f;
  if(!buildings.empty())objects.add(buildings[(x+y*side)%buildings.size()],px,py,(x%4)*2);
  objects.add("scenery/other/lamp.ai",px+1.8f,py+1.8f,0);
  if(!foliage.empty())objects.add(foliage[(x+y)%foliage.size()],px+2.7f,py-.8f,0);
  if(!rocks.empty())objects.add(rocks[(x+y)%rocks.size()],px-.8f,py+2.7f,0);
 }
 const int wantedAnimals=side==3?6:side==6?18:side==10?36:0,wantedStaff=side==3?2:side==6?6:side==10?12:0,wantedGuests=side==3?20:side==6?80:side==10?300:0;
 for(int y=20;y<map.getHeight()-12;++y)for(int x=20;x<map.getWidth()-12;++x)if(x%5==3 || y%5==3)map.setPath(x,y,"path");
 for(int i=0;i<wantedAnimals;++i) {
  const char *files[]={"animals/lion.ai","animals/elephant.ai","animals/giraffe.ai"};const int type=animals.typeOfFile(files[i%3]);if(type<0)continue;
  int id=animals.adopt(type,i%2,30.5f+(i%9)*2.0f,37.5f+(i/9)*2.0f,map,world.getFences(),true);
  if(auto *m=animals.member(id)){m->boxed=false;m->anim="walk";m->fx=0;m->fy=-1;}
 }
 std::vector<int> staffTypes;for(size_t i=0;i<staff.types().size();++i)if(staff.types()[i].kind!=Staff::Kind::Helicopter)staffTypes.push_back(int(i));
 for(int i=0;i<wantedStaff && !staffTypes.empty();++i)staff.hire(staffTypes[i%staffTypes.size()],30.5f+i*.75f,side==3?38.5f:53.5f,map,world.getFences());
 guests.setEntrance(33,33);for(int i=0;i<wantedGuests;++i)guests.admit(0);
 auto &people=const_cast<std::vector<Guests::Guest>&>(guests.guests());
 for(size_t i=0;i<people.size();++i){auto &g=people[i];g.hidden=false;g.x=28.5f+(i%22)*.85f;g.y=31.5f+(i/22)*.85f;g.anim="walk";g.fx=0;g.fy=-1;}
 map.touch();world.reindex();world.setPaused(true);world.getCamera().zoom=1;
 auto &light=world.getLighting();ZT_RendererInfo info{};ZT_GetRendererInfo(renderer,&info);SDL_SetRenderVSync(renderer,0);
 int w=0,h=0;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);
 std::ofstream meta(out+"/fixture.txt");meta<<"scenario="<<scenario<<"\nrenderer="<<(info.name?info.name:"")<<"\nwidth="<<w<<"\nheight="<<h<<"\nobjects="<<objects.objects().size()<<"\nanimals="<<animals.members().size()<<"\nstaff="<<staff.members().size()<<"\nguests="<<people.size()<<"\n";
 auto animate=[&](int frame) {
  for(const auto &m:animals.members())if(auto *a=animals.member(m.id)){a->animTime=frame/60.0f;a->fx=std::sin(frame*.012f);a->fy=-std::cos(frame*.012f);}
  for(auto &g:people){g.animTime=frame/60.0f;g.fx=std::sin(frame*.009f);g.fy=-std::cos(frame*.009f);}
 };
 Features::dayNightLighting=false;draw();world.getRenderer().centreOnWorld(39,39,0);
 for(int r=0;r<4;++r){for(int f=0;f<24;++f){animate(f*15);draw();SDL_RenderPresent(renderer);}world.rotateView(1);}
 const size_t before=privateMemory();
 Uint64 first=SDL_GetPerformanceCounter();Features::dayNightLighting=true;light.previewHour=12;draw();SDL_RenderPresent(renderer);meta<<"first_enable_ms="<<elapsed(first)<<"\nprivate_before="<<before<<"\n";
 // Warm all twelve geometry key poses, without advancing animal needs or AI.
 for(int f=0;f<240;++f){animate(f);draw();SDL_RenderPresent(renderer);}
 world.setPaused(!simulation);
 meta<<"simulation="<<simulation<<"\n";
 std::ofstream samples(out+"/frames.csv");samples<<"pass,mode,frame,frame_ms,thread_cpu_ms,simulation_ms,lights,faces,actors,actor_faces,sun_builds,actor_builds,actor_fallbacks\n";
 std::ofstream phases(out+"/phases.csv");phases<<"pass,mode,phase,total_ms,frames\n";
 // ours: each frame's own phases (those over 0.05 ms), to find what a slow frame spent its time on
 std::ofstream framePhases(out+"/frame_phases.csv");framePhases<<"pass,mode,frame,phase,ms\n";
 for(int pass=0;pass<2;++pass)for(int mode=0;mode<4;++mode) {
  const char *label=mode==0?"off":mode==1?"day":mode==2?"sunset":"night";Features::dayNightLighting=mode!=0;
  FrameProfile::totals.clear();FrameProfile::active=true;
  for(int f=0;f<204;++f) {
   SDL_PumpEvents();const int frame=f+pass*204;if(!simulation)animate(frame);
   light.previewHour=(mode==1?12:mode==2?17.5:0)+f/1800.0;
   world.getRenderer().centreOnWorld(39+std::sin(f*.035f)*5,39+std::cos(f*.035f)*5,0);
   // addition: model/place bursts exercise invalidation while the camera pans.
   if(scenario=="placement" && f%45==0){auto &list=objects.all();list.erase(std::remove_if(list.begin(),list.end(),[](const auto &o){return o.x>38 && o.x<40 && o.y>38 && o.y<40;}),list.end());
     objects.add(buildings.empty()?"scenery/other/lamp.ai":buildings[(f/45+pass)%buildings.size()],39.0f,39.0f,(f/45%4)*2);world.reindex();}
   const auto before=FrameProfile::totals;
   FrameProfile::begin();const double cpu=threadMilliseconds();Uint64 start=SDL_GetPerformanceCounter();
   if(simulation)world.update(SDL_GetKeyboardState(nullptr),1.0f/60);
   const double simulationMs=simulation?elapsed(start):0;
   draw();SDL_RenderPresent(renderer);const double ms=elapsed(start),cpuMs=threadMilliseconds()-cpu;
   if(f>=24)for(const auto &[name,total]:FrameProfile::totals){const auto was=before.find(name);const double spent=total-(was==before.end()?0:was->second);if(spent>.05)framePhases<<pass<<","<<label<<","<<f-24<<","<<name<<","<<spent<<"\n";}
   if(f>=24){const auto &s=light.stats();samples<<pass<<","<<label<<","<<f-24<<","<<ms<<","<<cpuMs<<","<<simulationMs<<","<<(mode?s.lights:0)<<","<<(mode?s.modelFaces:0)<<","<<(mode?s.actors:0)<<","<<(mode?s.actorFaces:0)<<","<<(mode?s.sunBuilds:0)<<","<<(mode?s.actorBuilds:0)<<","<<(mode?s.actorFallbacks:0)<<"\n";}
  }
  FrameProfile::active=false;for(const auto &[name,ms]:FrameProfile::totals)phases<<pass<<","<<label<<","<<name<<","<<ms<<",204\n";
  SDL_Log("[LightingBench] %s pass %d %s complete",scenario.c_str(),pass,label);
 }
 meta<<"private_after="<<privateMemory()<<"\nlighting_buffers="<<light.bufferBytes()<<"\nlighting_texture_payload="<<rm->lightingTextureBytes()+light.mapTextureBytes()<<"\n";
 const auto cache=LightingAssets::modelCacheStats();
 meta<<"model_cache_hits="<<cache.hits<<"\nmodel_cache_misses="<<cache.misses<<"\nmodel_cache_writes="<<cache.writes<<"\nmodel_cache_rejected="<<cache.rejected<<"\n";
 Features::dayNightLighting=false;light.previewHour=-1;light.diagnosticLightLimit=DayNightLighting::maxLights;return 0;
}
