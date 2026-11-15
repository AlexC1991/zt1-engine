// ours: real elapsed-time benchmarking, with normal simulation and native HUD.
// Disposable stock fixture only. Never reads or writes the player's zoo/settings.
#include "GameBenchmark.hpp"
#include "World.hpp"
#include "Features.hpp"
#include "Research.hpp"
#include "RenderSettings.hpp"
#include "DisplaySettings.hpp"
#include "GpuFsr.hpp"
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <tuple>

extern std::string prepareMarineSoak(World &,ResourceManager *);
namespace FrameProfile { void start(); }
namespace {
double seconds(Uint64 start) { return double(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency(); }
int paddock(World &world,int x,int y,int height,int drag,const char *title) {
  auto &f=world.getFences();auto &map=world.getMap();const int type=f.typeIndex("chainlnk");
  for(int yy=y-1;yy<=y+height;++yy)for(int xx=x-1;xx<=18;++xx)map.setPath(xx,yy,"");
  for(int k=0;k<14;++k){f.place({true,x+k,y},type,drag,map);f.place({true,x+k,y+height},type,drag+1,map);}
  for(int k=0;k<height;++k){f.place({false,x,y+k},type,drag+2,map);f.place({false,x+14,y+k},type,drag+3,map);}
  for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,title,map);
  return f.exhibitAt(x,y);
}
}
bool GameBenchmark::start(World &world,ResourceManager *rm,SDL_Renderer *renderer) {
  const char *folder=std::getenv("ZT_GAME_BENCH_OUT");if(!folder || !*folder)return false;
  out=folder;std::filesystem::create_directories(out);
  name=std::getenv("ZT_GAME_BENCH_NAME")?std::getenv("ZT_GAME_BENCH_NAME"):"baseline";
  speed=std::getenv("ZT_GAME_BENCH_RATE")?std::atoi(std::getenv("ZT_GAME_BENCH_RATE")):1;
  if(speed!=1 && speed!=2 && speed!=5)speed=1;
  control=std::getenv("ZT_GAME_BENCH_CONTROL")!=nullptr;
  lighting=std::getenv("ZT_GAME_BENCH_LIGHTING")!=nullptr;cycle=lighting;
  minimum=std::getenv("ZT_GAME_BENCH_SECONDS")?std::max(1,std::atoi(std::getenv("ZT_GAME_BENCH_SECONDS"))):180;
  smoke=std::getenv("ZT_GAME_BENCH_SMOKE")!=nullptr;
  if(smoke){warmup=2;cycle=false;}
  frames.open(out+"/frames.csv");status.open(out+"/status.txt");checks.open(out+"/checks.txt");
  frames<<"elapsed_seconds,frame_ms,simulation_ms,game_seconds,hour,requested_rate,pending_seconds,discarded_seconds,moving,rotation,lights,faces,actors,guests,animals,paused\n";
  frames<<std::fixed<<std::setprecision(6);
  for(auto &feature:Features::all())*feature.value=false;
  Features::fastForward=control;Features::dayNightLighting=lighting;
  std::srand(7026);
  world.loadBlank(80,80);world.edgeScroll=false;world.cash=1000000;world.getFences().setEditing(true);
  for(auto &b:Research::get().branches())for(auto &c:b.categories)for(auto &p:c.programs)if(p.effect==6)p.done=true;
  status<<prepareMarineSoak(world,rm);
  const int land=paddock(world,4,6,14,3000,"Zebra paddock"),dino=paddock(world,4,28,14,3010,"Dinosaur paddock");
  auto &a=world.getAnimals();auto &f=world.getFences();auto &map=world.getMap();auto &staff=world.getStaff();
  const int zebra=a.typeOfFile("animals/zebra.ai"),ankylo=a.typeOfFile("animals/ankylo.ai");
  if(land<0 || dino<0 || zebra<0 || ankylo<0){checks<<"FAIL stock fixture construction\n";out.clear();return false;}
  for(int k=0;k<4;++k)a.adopt(zebra,k%2,7.5f+k*2,12.5f,map,f,true);
  for(int k=0;k<2;++k)a.adopt(ankylo,k%2,8.5f+k*3,35.5f,map,f,true);
  staff.hire(staff.typeOfFile("staff/keeper.ai"),20.5f,15.5f,map,f);
  staff.hire(staff.typeOfFile("staff/scient.ai"),20.5f,35.5f,map,f);
  auto &objects=world.objectsMutable();
  for(const auto &[file,x,y]:std::vector<std::tuple<const char*,float,float>>{
      {"scenery/building/carousal.ai",24.5f,14.5f},{"scenery/building/lightho.ai",48.5f,14.5f},
      {"scenery/building/helibase.ai",60.5f,49.5f}})objects.add(file,x,y,0);
  for(int k=0;k<12;++k)objects.add("scenery/other/lamp.ai",21.0f+(k%6)*8,19.0f+(k/6)*26,0);
  world.getGuests().setEntrance(20,49);for(int k=0;k<300;++k)world.getGuests().admit(0);
  world.reindex();world.setPaused(true);world.zoomStep(-1);
  world.setView({0,0,1280,720},1);world.centreOn(36,32);
  SDL_SetWindowSize(SDL_GetRenderWindow(renderer),1280,720);
  // (ZT_GAME_BENCH_FPS: the frame limit to measure at, 0 for none; 60 unless set)
  const int fpsLimit=std::getenv("ZT_GAME_BENCH_FPS")?std::max(0,std::atoi(std::getenv("ZT_GAME_BENCH_FPS"))):60;
  Display::setFrameLimit(fpsLimit);Display::setVsync(false);RenderSettings::uiScaleSetting=1;
  ZT_RendererInfo info{};ZT_GetRendererInfo(renderer,&info);
  status<<"\nname="<<name<<"\nrenderer="<<(info.name?info.name:"")<<"\nwidth=1280\nheight=720\nfps_limit="<<fpsLimit<<"\nvsync=0\nHD_pack=off\n"
        <<"zoom=0.5\nwarmup_seconds="<<warmup<<"\nminimum_seconds="<<minimum<<"\nfull_cycle_required="<<cycle<<"\nrequested_rate="<<speed
        <<"\nlighting="<<lighting<<"\nspeed_control="<<control<<"\nanimals="<<a.members().size()<<"\nstaff="<<staff.members().size()
        <<"\nguests="<<world.getGuests().guests().size()<<"\nobjects="<<objects.objects().size()<<"\nfences="<<f.pieces().size()<<"\n";
  using GetString=const GLubyte *(APIENTRY *)(GLenum);
  auto getString=reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
  if(SDL_GL_GetCurrentContext() && getString)status<<"GPU="<<reinterpret_cast<const char*>(getString(GL_RENDERER))<<"\n";
  status.flush();epoch=SDL_GetPerformanceCounter();return true;
}
// ours: the saved-zoo timing (ZT_GAME_BENCH_ZOO). Each phase: its name, how
// long, the hour it shows (-1: night/day as the phase before), whether the
// view pans and whether it's measured
namespace {
struct ZooPhase {const char *name;double seconds,hour;bool pan,measured;};
const ZooPhase kZooPhases[]={
  {"warmup-day",12,12,false,false},{"day-still",15,12,false,true},{"day-pan",15,12,true,true},
  {"warmup-night",6,0,false,false},{"night-still",15,0,false,true},{"night-pan",15,0,true,true},
  {"shots",0,12,false,false}};
constexpr int kZooPhaseCount=int(sizeof kZooPhases/sizeof kZooPhases[0]);
// (the shots, the world paused: panned kShotWarm frames, then kShotCount
// more each captured as it moves; then each of those places held still for
// kShotHold frames and captured again; then the last with the lighting off.
// At noon, then at midnight)
constexpr int kShotWarm=40,kShotCount=8,kShotHold=4,kShotPan=kShotWarm+kShotCount,
              kShotOff=kShotPan+kShotCount*kShotHold,kShotEnd=kShotOff+3;
}
bool GameBenchmark::startZoo(World &world,SDL_Renderer *renderer) {
  const char *folder=std::getenv("ZT_GAME_BENCH_OUT");if(!folder || !*folder)return false;
  out=folder;std::filesystem::create_directories(out);zoo=true;
  name=std::getenv("ZT_GAME_BENCH_NAME")?std::getenv("ZT_GAME_BENCH_NAME"):"zoo";
  frames.open(out+"/frames.csv");status.open(out+"/status.txt");checks.open(out+"/checks.txt");
  frames<<"phase,elapsed_seconds,frame_ms,hour,camera_x,camera_y\n"<<std::fixed<<std::setprecision(4);
  Features::dayNightLighting=true;world.edgeScroll=false;world.setPaused(false);
  const int fpsLimit=std::getenv("ZT_GAME_BENCH_FPS")?std::max(0,std::atoi(std::getenv("ZT_GAME_BENCH_FPS"))):0;
  Display::setFrameLimit(fpsLimit);Display::setVsync(false);
  int w=0,h=0;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);
  ZT_RendererInfo info{};ZT_GetRendererInfo(renderer,&info);
  status<<"name="<<name<<"\nzoo="<<std::getenv("ZT_GAME_BENCH_ZOO")<<"\nrenderer="<<(info.name?info.name:"")<<"\noutput="<<w<<"x"<<h
        <<"\nzoom="<<world.getCamera().zoom<<"\nfps_limit="<<fpsLimit<<"\nanimals="<<world.getAnimals().members().size()
        <<"\nguests="<<world.getGuests().guests().size()<<"\nobjects="<<world.objectsMutable().objects().size()<<"\n";
  status.flush();zooPhase=0;zooFrame=0;zooPhaseStart=lastPresent=epoch=SDL_GetPerformanceCounter();
  SDL_Log("[GameBench] zoo %s: %dx%d on %s",name.c_str(),w,h,info.name?info.name:"?");
  return true;
}
void GameBenchmark::zooFrameStep(World &world) {
  const ZooPhase &p=kZooPhases[zooPhase];const double t=seconds(zooPhaseStart);
  auto &light=world.getLighting();auto &cam=world.getCamera();
  if(p.name==std::string("shots")) {
    // (noon for the first kShotEnd frames, then midnight)
    // (ZT_GAME_BENCH_SHOT_HOUR: the day shots' hour, noon unless set)
    const double dayHour=std::getenv("ZT_GAME_BENCH_SHOT_HOUR")?std::atof(std::getenv("ZT_GAME_BENCH_SHOT_HOUR")):12;
    const int k=zooFrame%kShotEnd;light.previewHour=zooFrame<kShotEnd?dayHour:0;
    // (ZT_GAME_BENCH_SHOT_RUNNING: the world and the sun going on meanwhile)
    static const bool running=std::getenv("ZT_GAME_BENCH_SHOT_RUNNING")!=nullptr;
    if(running)light.previewHour+=t/30.0;
    // (ZT_GAME_BENCH_SHOT_OFF: every shot with the lighting off, to compare with)
    world.setPaused(!running);Features::dayNightLighting=k<kShotOff && !std::getenv("ZT_GAME_BENCH_SHOT_OFF");
    if(k==0)shotPos.clear();
    // (ZT_GAME_BENCH_KEYS: panned by the right arrow key held down - the
    // player's own way, World::handleCameraInput - not moved here)
    static const bool keys=std::getenv("ZT_GAME_BENCH_KEYS")!=nullptr;
    if(keys)const_cast<bool*>(SDL_GetKeyboardState(nullptr))[SDL_SCANCODE_RIGHT]=k<kShotPan;
    if(k<kShotPan){
      // (ZT_GAME_BENCH_SHOT_STEP: camera pixels a frame, 10 at zoom 1 unless set)
      const int step=std::getenv("ZT_GAME_BENCH_SHOT_STEP")?std::atoi(std::getenv("ZT_GAME_BENCH_SHOT_STEP")):std::max(1,int(std::lround(10/cam.zoom)));
      // (ZT_GAME_BENCH_SHOT_STEPY: down the screen a frame, half the step unless set)
      const int stepY=std::getenv("ZT_GAME_BENCH_SHOT_STEPY")?std::atoi(std::getenv("ZT_GAME_BENCH_SHOT_STEPY")):step/2;
      if(!keys){cam.x+=step;cam.y+=stepY;}
    } else if(k<kShotOff && (k-kShotPan)%kShotHold==0) {
      const auto &at=shotPos[size_t((k-kShotPan)/kShotHold)];cam.x=at.first;cam.y=at.second;
    }
    return;
  }
  // (the sun goes on as in play: an hour each 30 seconds, DayNightLighting::hourAt)
  light.previewHour=std::fmod(p.hour+t/30.0,24.0);
  // (shots only: the zoo held as saved, so two runs' pictures can be compared)
  if(std::getenv("ZT_GAME_BENCH_SHOTS_ONLY")){world.setPaused(true);light.previewHour=p.hour;}
  // (ZT_GAME_BENCH_KEYS: the pans by the arrow keys held down, as a player
  // pans - World::handleCameraInput - right then left, turning every 2.5 s)
  if(std::getenv("ZT_GAME_BENCH_KEYS")) {
    bool *keys=const_cast<bool*>(SDL_GetKeyboardState(nullptr));
    const bool right=int(t/2.5)%2==0;keys[SDL_SCANCODE_RIGHT]=p.pan && right;keys[SDL_SCANCODE_LEFT]=p.pan && !right;
  } else if(p.pan) {
    // (a player's pan: 600 screen pixels a second - between the arrow keys'
    // 300 and the edge scroll's 960 - turning back every 2.5 seconds, in
    // whole pixels as World::handleCameraInput moves it)
    if(int(t/2.5)%2!=(panDir>0?0:1))panDir=-panDir;
    static Uint64 last=0;const Uint64 now=SDL_GetPerformanceCounter();
    const float dt=last?float(double(now-last)/SDL_GetPerformanceFrequency()):0;last=now;
    panCarry+=600.0f*std::min(dt,.1f)/cam.zoom;const int step=int(panCarry);panCarry-=step;
    cam.x+=panDir*step;cam.y+=panDir*step/2;
  }
}
void GameBenchmark::zooPresent(World &world,SDL_Renderer *renderer) {
  // (where the view was as each captured pan frame was drawn)
  if(kZooPhases[zooPhase].name==std::string("shots")){const int k=zooFrame%kShotEnd;if(k>=kShotWarm && k<kShotPan)shotPos.push_back({world.getCamera().x,world.getCamera().y});}
  // (ZT_GAME_BENCH_PAN_SHOTS: three frames in a row captured mid-pan, the
  // world running, as a player sees them)
  if(kZooPhases[zooPhase].pan && std::getenv("ZT_GAME_BENCH_PAN_SHOTS") && zooFrame>=200 && zooFrame<203)
    shot(renderer,(std::string(kZooPhases[zooPhase].name)+"-live"+std::to_string(zooFrame-200)).c_str());
  if(kZooPhases[zooPhase].name!=std::string("shots"))return;
  const int k=zooFrame%kShotEnd;const char *when=zooFrame<kShotEnd?"day":"night";
  if(k>=kShotWarm && k<kShotPan)shot(renderer,(std::string(when)+"-pan"+std::to_string(k-kShotWarm)).c_str());
  else if(k>=kShotPan && k<kShotOff && (k-kShotPan)%kShotHold==kShotHold-1)shot(renderer,(std::string(when)+"-held"+std::to_string((k-kShotPan)/kShotHold)).c_str());
  else if(k==kShotEnd-1)shot(renderer,(std::string(when)+"-off").c_str());
}
bool GameBenchmark::zooAfter(World &world) {
  const ZooPhase &p=kZooPhases[zooPhase];
  const Uint64 now=SDL_GetPerformanceCounter();const double ms=1000.*(now-lastPresent)/SDL_GetPerformanceFrequency();lastPresent=now;
  ++zooFrame;
  if(p.measured){
    phaseMs.push_back(ms);
    frames<<p.name<<','<<seconds(zooPhaseStart)<<','<<ms<<','<<world.getLighting().previewHour<<','<<world.getCamera().x<<','<<world.getCamera().y<<'\n';
  }
  const bool done=p.name==std::string("shots")?zooFrame>=2*kShotEnd:seconds(zooPhaseStart)>=p.seconds;
  if(!done)return false;
  if(p.measured && !phaseMs.empty()) {
    std::vector<double> v=phaseMs;std::sort(v.begin(),v.end());double total=0;for(double x:v)total+=x;
    const double median=v[v.size()/2],p95=v[std::min(v.size()-1,v.size()*95/100)],p99=v[std::min(v.size()-1,v.size()*99/100)];
    char row[256];std::snprintf(row,sizeof row,"%-12s frames %5zu  fps %6.1f  median %6.2f ms  mean %6.2f  p95 %6.2f  p99 %6.2f  max %6.1f  >33ms %zu\n",
      p.name,v.size(),1000.0*v.size()/total,median,total/v.size(),p95,p99,v.back(),size_t(std::count_if(v.begin(),v.end(),[](double x){return x>33.333;})));
    status<<row;status.flush();SDL_Log("[GameBench] %s",row);
  }
  phaseMs.clear();zooFrame=0;
  // (ZT_GAME_BENCH_SHOTS_ONLY: the warm-up, then straight to the shots)
  static const bool shotsOnly=std::getenv("ZT_GAME_BENCH_SHOTS_ONLY")!=nullptr;
  ++zooPhase;while(shotsOnly && zooPhase<kZooPhaseCount-1)++zooPhase;
  if(zooPhase>=kZooPhaseCount){status<<"complete=1\n";status.flush();frames.flush();return true;}
  zooPhaseStart=SDL_GetPerformanceCounter();
  if(kZooPhases[zooPhase].measured && std::getenv("ZT_GAME_BENCH_PROFILE"))FrameProfile::start();
  SDL_Log("[GameBench] phase %s",kZooPhases[zooPhase].name);
  return false;
}
void GameBenchmark::beforeFrame(World &world) {
  if(!enabled())return;
  if(zoo){zooFrameStep(world);return;}
  simulationMs=0;
  if(phase==0) {
    const double t=seconds(epoch);const int r=int(t/5)%4;
    if(rotation!=r){world.rotateView((r-rotation+4)%4);rotation=r;}
    if(lighting)world.getLighting().previewHour=double(int(t/5)%4)*6;
    world.centreOn(36,32);
    if(t>=warmup){
      world.rotateView((4-rotation)%4);rotation=0;world.getLighting().previewHour=-1;world.setPaused(false);
      recordStart=lastPresent=SDL_GetPerformanceCounter();startClock=world.getClock();phase=1;
      if(std::getenv("ZT_GAME_BENCH_PROFILE"))FrameProfile::start();
      status<<"measurement_started=1\n";status.flush();SDL_Log("[GameBench] %s measurement started",name.c_str());
    }
  } else if(phase==1) {
    const double t=seconds(recordStart);const int r=int(t/90)%4;
    if(rotation!=r){world.rotateView((r-rotation+4)%4);rotation=r;}
    const double part=std::fmod(t,90.0);
    const double theta=(part-30)*6.283185307179586/60;
    world.centreOn(part<30?36:float(36+8*std::sin(theta)),part<30?32:float(32+6*std::cos(theta)));
  }
}
void GameBenchmark::beforeSimulation(){if(zoo)return;if(enabled())simulationStart=SDL_GetPerformanceCounter();}
void GameBenchmark::afterSimulation(){if(enabled())simulationMs=1000*seconds(simulationStart);}
void GameBenchmark::shot(SDL_Renderer *renderer,const char *name) {
  // (ZT_GAME_BENCH_SHOT_FINISHED: the picture as the screen gets it - the
  // brightness and colour filter pass, which the present step runs, done
  // first; the frame shown then has it twice, the picture once)
  static const bool finished=std::getenv("ZT_GAME_BENCH_SHOT_FINISHED")!=nullptr;
  if(finished)GpuFsr::finishFrame(renderer);
  int w=0,h=0;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);
  SDL_Surface *image=SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if(image){if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,image->pixels,image->pitch)==0)IMG_SavePNG(image,(out+"/"+name+".png").c_str());SDL_DestroySurface(image);}
}
void GameBenchmark::beforePresent(World &world,SDL_Renderer *renderer) {
  if(!enabled())return;
  if(zoo){zooPresent(world,renderer);return;}
  if(phase==2){shot(renderer,"daylight-before");Features::dayNightLighting=true;phase=3;sample=0;}
  else if(phase==3 && ++sample==4){shot(renderer,"night-before-switch-off");Features::dayNightLighting=false;phase=4;sample=0;}
  else if(phase==4 && ++sample==4){shot(renderer,"daylight-after-switch-off");checks<<"PASS night-to-off capture sequence complete\n";checks.flush();phase=5;}
}
bool GameBenchmark::afterPresent(World &world,double pending,double discarded) {
  if(!enabled())return false;
  if(zoo)return zooAfter(world);
  if(phase==5){status<<"complete=1\n";status.flush();return true;}
  if(phase!=1)return false;
  const Uint64 now=SDL_GetPerformanceCounter();
  const double t=double(now-recordStart)/SDL_GetPerformanceFrequency(),ms=1000.*(now-lastPresent)/SDL_GetPerformanceFrequency(),game=world.getClock()-startClock;
  lastPresent=now;
  const auto &s=world.getLighting().stats();
  frames<<t<<','<<ms<<','<<simulationMs<<','<<game<<','<<DayNightLighting::hourAt(world.getClock())<<','<<speed<<','<<pending<<','<<discarded<<','
    <<(std::fmod(t,90.)>=30)<<','<<rotation<<','<<(lighting?s.lights:0)<<','<<(lighting?s.modelFaces:0)<<','<<(lighting?s.actors:0)<<','
    <<world.getGuests().guests().size()<<','<<world.getAnimals().members().size()<<','<<world.isPaused()<<'\n';
  if(int(t/30)!=lastProgress){lastProgress=int(t/30);frames.flush();status<<"progress_seconds="<<t<<" game_seconds="<<game<<"\n";status.flush();SDL_Log("[GameBench] %s %.0f s real / %.1f s game",name.c_str(),t,game);}
  const bool timedOut=t>minimum+std::max(360.,1440./speed);
  if((t<minimum || (cycle && game<720)) && !timedOut)return false;
  if(timedOut && cycle && game<720)checks<<"FAIL cycle completion timed out\n";
  frames.flush();status<<"elapsed_seconds="<<t<<"\ngame_seconds="<<game<<"\nachieved_rate="<<game/t<<"\ndiscarded_seconds="<<discarded<<"\npending_seconds="<<pending<<"\n";status.flush();
  checks<<(game>0?"PASS ":"FAIL ")<<"simulation advances\n"<<(t>=minimum?"PASS ":"FAIL ")<<"minimum wall-clock duration\n";
  if(cycle)checks<<(game>=720?"PASS ":"FAIL ")<<"full natural day/night cycle\n";
  world.setPaused(true);Features::dayNightLighting=false;world.getLighting().previewHour=0;phase=2;
  return false;
}
