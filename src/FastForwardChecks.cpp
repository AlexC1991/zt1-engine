// ours: isolated timing, simulation and GPU regressions using stock assets.
// The same scheduler drives accelerated Play. No speed-button UI or user saves
// are created; the shared Lab and HD worker are left alone.
#include "World.hpp"
#include "ZooSim.hpp"
#include "Research.hpp"
#include "Features.hpp"
#include "GpuTankLayer.hpp"
#include "GameSpeedClock.hpp"
#include <thread>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <map>
#include <SDL3/SDL_opengl.h>

namespace FrameProfile {extern bool active;extern std::map<std::string,double> totals;}

extern std::string prepareMarineSoak(World &,ResourceManager *);
extern void fenceWorkReuse(bool);
extern void fenceGpuComposition(bool);
extern std::string runShowManagementChecks(World &,ResourceManager *);

namespace {
constexpr float quantum=1.0f/60;
constexpr bool noKeys[SDL_SCANCODE_COUNT]={};
double milliseconds(Uint64 start) {
  return 1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();
}
// Prototype: one fixed simulation timeline, with rendering occurring less
// often along it at higher speed. No larger movement delta at 2x or 5x.
int dueTicks(int displayFrame,int speed,int displayHz) {
  return int((int64_t(displayFrame+1)*speed*60)/displayHz-
             (int64_t(displayFrame)*speed*60)/displayHz);
}
int paddock(World &w,int x,int y,int width,int height,int drag) {
  auto &f=w.getFences();auto &map=w.getMap();const int type=f.typeIndex("chainlnk");
  // The marine soak lays public paths outside its tanks. Clear both sides
  // of these new boundaries before placing fences, as the build tool does.
  for(int yy=y-1;yy<=y+height;++yy)for(int xx=x-1;xx<=x+width;++xx)map.setPath(xx,yy,"");
  for(int k=0;k<width;++k){f.place({true,x+k,y},type,drag,map);f.place({true,x+k,y+height},type,drag+1,map);}
  for(int k=0;k<height;++k){f.place({false,x,y+k},type,drag+2,map);f.place({false,x+width,y+k},type,drag+3,map);}
  for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,"Speed test",map);
  for(int yy=y;yy<y+height;++yy)for(int xx=x;xx<x+width;++xx)map.setPath(xx,yy,"");
  return f.exhibitAt(x,y);
}
std::string signature(World &w,const ZooSim &books) {
  std::ostringstream s;s<<std::setprecision(9)<<w.getClock()<<' '<<books.cash()<<' '<<books.dayOfMonth()<<'\n';
  for(const auto &m:w.getAnimals().members())s<<m.id<<' '<<m.type<<' '<<m.x<<' '<<m.y<<' '<<m.anim<<' '<<m.animTime<<' '<<m.hunger<<' '<<m.health<<' '<<m.boxed<<' '<<m.exhibit<<'\n';
  for(const auto &m:w.getStaff().members())s<<m.id<<' '<<m.x<<' '<<m.y<<' '<<m.anim<<' '<<m.animTime<<' '<<int(m.job)<<'\n';
  for(const auto &g:w.getGuests().guests())s<<g.id<<' '<<g.x<<' '<<g.y<<' '<<g.anim<<' '<<g.animTime<<' '<<int(g.state)<<' '<<g.hunger<<' '<<g.happiness<<'\n';
  s<<w.getShows().snapshot();return s.str();
}
}

int runFastForwardChecks(ResourceManager *rm,SDL_Renderer *renderer,const std::string &out) {
  std::ofstream log(out+"/speed-checks.txt"),csv(out+"/speed-frames.csv"),summary(out+"/speed-summary.csv");
  std::ofstream phases(out+"/speed-phases.csv"),device(out+"/device.txt");
  phases<<"speed,lighting,phase,total_ms,frames\n";
  ZT_RendererInfo info{};ZT_GetRendererInfo(renderer,&info);
  device<<"SDL renderer="<<(info.name?info.name:"")<<"\naccelerated="<<bool(info.flags&ZT_RENDERER_ACCELERATED)<<'\n';
  using GetString=const GLubyte *(APIENTRY *)(GLenum);
  auto getString=reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
  if(SDL_GL_GetCurrentContext() && getString)for(auto key:{GL_VENDOR,GL_RENDERER,GL_VERSION})
    device<<key<<'='<<reinterpret_cast<const char*>(getString(key))<<'\n';
  int failed=0,passed=0;
  auto check=[&](bool ok,const std::string &name){(ok?++passed:++failed);log<<(ok?"PASS ":"FAIL ")<<name<<'\n';log.flush();SDL_Log("[SpeedCheck] %s %s",ok?"PASS":"FAIL",name.c_str());};
  csv<<"speed,lighting,frame,input_seconds,steps,simulation_ms,render_ms,total_ms,backlog_seconds\n";
  summary<<"speed,input_hz,game_seconds,virtual_real_seconds,steps,max_step_ms,simulation_cpu_ms,completed_shows,animals,staff,guests\n";
  const bool wasLighting=Features::dayNightLighting;
  if(const char *value=std::getenv("ZT_GPU_TANK"))fenceGpuComposition(std::string(value)!="0");
  if(std::getenv("ZT_FENCE_REFERENCE")){fenceWorkReuse(false);fenceGpuComposition(false);}
  const int seconds=std::getenv("ZT_SPEED_SECONDS")?std::max(5,std::atoi(std::getenv("ZT_SPEED_SECONDS"))):360;
  const bool fullShow=std::getenv("ZT_SPEED_SHOW_SOAK")!=nullptr;
  // Exercise the production scheduler's conservation, input and overload rules.
  for(int rate:{1,2,5}) {
    GameSpeedClock timing;double simulated=0,controls=0,maxStep=0;
    for(int frame=0;frame<600;++frame)timing.advance(1./60,rate,false,[&](float dt,float input){simulated+=dt;controls+=input;maxStep=std::max(maxStep,double(dt));return true;});
    check(std::abs(simulated-10*rate)<.001 && timing.pending()<GameSpeedClock::tickSeconds,"production timing reaches requested "+std::to_string(rate)+"x");
    check(std::abs(controls-10)<.001,"camera receives real time once per frame at "+std::to_string(rate)+"x");
    if(rate>1)check(maxStep<=float(GameSpeedClock::tickSeconds),"production acceleration never enlarges movement delta");
  }
  {
    GameSpeedClock timing;int calls=0;float step=0;
    timing.advance(.123,1,false,[&](float dt,float){++calls;step=dt;return true;});
    check(calls==1 && std::abs(step-.123)<1e-6,"normal 1x retains the original single variable update");
    const auto hitch=timing.advance(3,5,false,[](float,float){return true;});
    check(hitch.steps<=64 && timing.pending()<=2 && timing.discarded()>0,"stalls have bounded catch-up and report lost time");
    timing.advance(.5,5,true,[](float dt,float){return dt==0;});
    check(timing.pending()==0,"pause clears fast-forward backlog");
    const auto resume=timing.advance(1./60,5,false,[](float,float){return true;});
    check(resume.steps==5,"resume has no stored-time burst");
    timing.advance(.1,5,false,[](float,float){return false;});
    check(timing.pending()==0,"a modal appearing within a tick stops catch-up");
    const auto busy=timing.advance(.25,5,false,[](float,float){std::this_thread::sleep_for(std::chrono::milliseconds(2));return true;});
    check(busy.limited && busy.steps<16,"simulation CPU budget protects input and rendering");
    const auto normal=timing.advance(1./60,1,false,[](float,float){return true;});
    check(normal.steps==1 && timing.pending()==0,"switching to 1x drops pending accelerated work");
  }
  for(int rate:{1,2,5}) {
    World controlsWorld(rm);controlsWorld.loadBlank(16,16);controlsWorld.edgeScroll=false;
    GameSpeedClock timing;bool keys[SDL_SCANCODE_COUNT]={};keys[SDL_SCANCODE_RIGHT]=true;
    const int start=controlsWorld.getCamera().x;controlsWorld.setPaused(false);
    for(int frame=0;frame<60;++frame)timing.advance(1./60,rate,false,[&](float dt,float input){controlsWorld.update(input>0?keys:noKeys,dt,input);return true;});
    check(start-controlsWorld.getCamera().x==300,"real world camera distance stays normal at "+std::to_string(rate)+"x");
    const auto before=controlsWorld.getClock();controlsWorld.setPaused(true);
    for(int frame=0;frame<60;++frame)timing.advance(1./60,rate,true,[&](float dt,float input){controlsWorld.update(keys,dt,input);return true;});
    check(controlsWorld.getClock()==before && start-controlsWorld.getCamera().x==600,"paused world still pans normally without advancing");
  }
  std::string reference;
  SDL_SetWindowSize(SDL_GetRenderWindow(renderer),1280,720);SDL_HideWindow(SDL_GetRenderWindow(renderer));
  SDL_SetRenderVSync(renderer,0);
  if(std::getenv("ZT_SPEED_MANAGEMENT")) {
    World management(rm);management.loadBlank(128,128);management.getFences().setEditing(true);
    // The existing ff01 check admits guests and places its distant group at
    // 110,110. A blank map needs the entrance normally supplied by ff01.
    management.getGuests().setEntrance(20,49);
    management.cash=1000000;management.setView({0,0,1280,720},1);management.draw(renderer);
    const std::string result=runShowManagementChecks(management,rm);log<<result;
    check(result.find("FAIL ")==std::string::npos && result.find("show-management checks passed")!=std::string::npos,
          "existing show startup, linked homes, platform relocation and feeding checks");
    log<<"TOTAL "<<passed<<" passed, "<<failed<<" failed.\n";return failed?1:0;
  }
  for(int hz:{60,30,15})for(int speed:{1,2,5}) {
    if(const char *value=std::getenv("ZT_SPEED_HZ"))if(hz!=std::atoi(value))continue;
    if(const char *value=std::getenv("ZT_SPEED_RATE"))if(speed!=std::atoi(value))continue;
    if(fullShow && hz!=60)continue;
    Features::dayNightLighting=false;
    World w(rm);check(w.loadBlank(80,80),"blank isolated test zoo loads");w.edgeScroll=false;
    w.getFences().setEditing(true); // Lab fixture construction, outside zoo ownership checks.
    std::srand(7026);
    for(auto &b:Research::get().branches())for(auto &c:b.categories)for(auto &p:c.programs)if(p.effect==6)p.done=true;
    const std::string setup=prepareMarineSoak(w,rm);log<<setup;log.flush();
    auto &a=w.getAnimals();auto &f=w.getFences();auto &map=w.getMap();
    const int land=paddock(w,4,6,14,14,3000),dino=paddock(w,4,28,14,14,3010);
    const int zooType=a.typeOfFile("animals/zebra.ai"),dinoType=a.typeOfFile("animals/ankylo.ai");
    log<<"Fixture land="<<land<<" dino="<<dino<<" zooType="<<zooType<<" dinoType="<<dinoType<<'\n';
    const bool stock=land>=0 && dino>=0 && zooType>=0 && dinoType>=0;
    check(stock,"stock land and dinosaur paddocks available");if(!stock)return 1;
    for(int k=0;k<4;++k)a.adopt(zooType,k%2,7.5f+k*2,12.5f,map,f,true);
    for(int k=0;k<2;++k)a.adopt(dinoType,k%2,8.5f+k*3,35.5f,map,f,true);
    check(w.getStaff().hire(w.getStaff().typeOfFile("staff/keeper.ai"),20.5f,15.5f,map,f)>=0,"zookeeper hired alongside trainers and maintenance");
    check(w.getStaff().hire(w.getStaff().typeOfFile("staff/scient.ai"),20.5f,35.5f,map,f)>=0,"dinosaur scientist hired");
    auto &guests=w.getGuests();guests.setEntrance(20,49);
    const int visitors=std::getenv("ZT_SPEED_GUESTS")?std::clamp(std::atoi(std::getenv("ZT_SPEED_GUESTS")),0,1000):30;
    for(int k=0;k<visitors;++k)guests.admit(0);
    w.reindex();w.centreOn(38,32);w.setPaused(false);
    if(std::getenv("ZT_FENCE_COMPARE")) {
      if(const char *value=std::getenv("ZT_SPEED_PIXELS_AT"))
        for(int k=0;k<std::atoi(value)*60;++k)w.update(noKeys,quantum);
      w.setPaused(true);
      SDL_Rect compareView{0,0,1280,720};
      auto draw=[&](bool read=true) {
        SDL_SetRenderTarget(renderer,nullptr);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderScale(renderer,1,1);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);w.setView(compareView,1);w.draw(renderer);
        if(!read){SDL_FlushRenderer(renderer);return std::vector<Uint8>{};}
        std::vector<Uint8> image(1280*720*4);
        check(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,image.data(),1280*4)==0,"native comparison frame read");
        return image;
      };
      auto save=[&](const std::vector<Uint8> &image,const std::string &name) {
        auto *surface=SDL_CreateSurfaceFrom(1280, 720, SDL_PIXELFORMAT_RGBA32, const_cast<Uint8*>(image.data()), 1280*4);
        if(surface){IMG_SavePNG(surface,(out+"/"+name+".png").c_str());SDL_DestroySurface(surface);}
      };
      for(bool centred:{false,true})for(double hour:{-1.,12.,17.5,0.})for(int rotation=0;rotation<4;++rotation)for(float zoom:{.5f,.75f,1.f,1.5f}) {
        compareView=centred?SDL_Rect{240,60,800,600}:SDL_Rect{0,0,1280,720};
        w.getCamera().zoom=zoom;Features::dayNightLighting=hour>=0;w.getLighting().previewHour=hour<0?0:hour;
        fenceWorkReuse(false);fenceGpuComposition(false);
        for(int warm=0;warm<16;++warm)draw(false);
        const auto original=draw();fenceWorkReuse(true);const auto reused=draw();
        const std::string phase=hour<0?"off":hour==12?"day":hour==0?"night":"sunset";
        const std::string label=phase+"-r"+std::to_string(rotation)+"-z"+std::to_string(zoom)+(centred?"-centred":"");
        check(original==reused,"CPU geometry and portal reuse retains exact pixels "+label);
        // (drawn again, nothing changed: each tank's inside as kept)
        const auto kept=draw();
        check(original==kept,"tank insides kept from the last frame retain exact pixels "+label);
        GpuTankLayer::resetStats();fenceGpuComposition(true);const auto gpu=draw();
        const auto used=GpuTankLayer::stats();
        log<<"GPU layer usage "<<label<<" builds="<<used.builds<<" copies="<<used.copies<<" peak_bytes="<<used.peakBytes<<'\n';
        if(zoom==1 || zoom==.5f)check(used.builds>0 && used.copies>used.builds,"GPU composition was exercised "+label);
        int maxError=0;size_t bad=0,changed=0;
        for(size_t i=0;i<original.size();++i) {
          const int error=std::abs(int(original[i])-int(gpu[i]));maxError=std::max(maxError,error);
          changed+=error!=0;bad+=error>3;
        }
        log<<"GPU difference "<<label<<" max="<<maxError<<" changed_channels="<<changed<<" over3="<<bad<<'\n';
        check(bad==0,"GPU tank composite stays within 3 colour levels "+label);
        save(original,"reference-"+label);save(gpu,"gpu-"+label);
        if(zoom==1.5f)w.rotateView(1);
      }
      fenceWorkReuse(true);fenceGpuComposition(false);Features::dayNightLighting=wasLighting;
      log<<"TOTAL "<<passed<<" passed, "<<failed<<" failed. Native renderer equivalence.\n";
      return failed?1:0;
    }
    ZooSim books;books.start(1000000,50);books.setMonthlyCost(ZooSim::Marketing,0,1200);
    const double startClock=w.getClock();const int ticks=seconds*60;
    int done=0,frames=0,maxTicks=0;bool finite=true,marineUnboxed=true;int completed=0;
    const Uint64 start=SDL_GetPerformanceCounter();
    auto tick=[&](float seconds=quantum,float input=-1) {
      w.update(noKeys,seconds,input);books.update(seconds);
      for(const auto &m:a.members()) {
        finite &= std::isfinite(m.x)&&std::isfinite(m.y)&&std::isfinite(m.health)&&std::isfinite(m.animTime);
        if(a.types()[m.type].underwater)marineUnboxed &= !m.boxed;
      }
      for(const auto &[id,s]:w.getShows().all())completed=std::max(completed,s.completed);
    };
    GameSpeedClock timeline;
    while(done<ticks) {
      int steps=0;
      if(speed==1) {
        steps=std::min(dueTicks(frames,speed,hz),ticks-done);
        for(int k=0;k<steps;++k)tick();done+=steps;
      } else {
        const auto frame=timeline.advance(1./hz,speed,false,[&](float dt,float input){
          if(dt>0 && done<ticks){tick(dt,input);++done;}return done<ticks;
        });
        steps=frame.steps;
      }
      maxTicks=std::max(maxTicks,steps);++frames;
      if(frames%60==0)SDL_PumpEvents();
    }
    const double cpu=milliseconds(start),gameSeconds=w.getClock()-startClock;
    check(std::abs(gameSeconds-seconds)<.001,"world clock advances exact game time at "+std::to_string(speed)+"x / "+std::to_string(hz)+"Hz");
    check(std::abs(double(frames)/hz*speed-seconds)<=double(speed)/hz+.001,"requested speed ratio preserved");
    check(finite,"animal positions, health and animation clocks remain finite");
    check(marineUnboxed,"dolphins stay unboxed during accelerated simulation");
    const std::string state=signature(w,books);
    if(reference.empty())reference=state;
    check(state==reference,"animals, staff, guests, shows and books match the same-time 60Hz baseline");
    std::ofstream(out+"/state-"+std::to_string(speed)+"x-"+std::to_string(hz)+"hz.txt")<<state;
    check(books.dayOfMonth()==1+seconds/12,"calendar advances with the accelerated zoo");
    const double pausedClock=w.getClock();w.setPaused(true);w.update(noKeys,quantum*speed);
    check(w.getClock()==pausedClock && signature(w,books)==state,"pause stops actor state and the world clock");w.setPaused(false);
    summary<<speed<<','<<hz<<','<<gameSeconds<<','<<double(frames)/hz<<','<<done<<','<<quantum*1000<<','<<cpu<<','<<completed<<','<<a.members().size()<<','<<w.getStaff().members().size()<<','<<guests.guests().size()<<'\n';summary.flush();
    log<<"Maximum steps per display frame: "<<maxTicks<<"; simulation CPU: "<<cpu<<"ms; completed shows: "<<completed<<'\n';log.flush();
    if(seconds>=360)check(completed>0,"automatic dolphin show completes on the shared timing path");
    // Real renderer with measured-wall-time feedback, separate from the
    // equal-game-time comparison. Keep fractional ticks and unprocessed debt;
    // limit work per frame so an overloaded simulation cannot lock up input.
    // This remains a test driver, not the production game loop or HUD.
    if(hz==60 && !fullShow)for(bool night:{false,true}) {
      Features::dayNightLighting=night;w.getLighting().previewHour=0;
      auto draw=[&]{SDL_SetRenderTarget(renderer,nullptr);SDL_SetRenderScale(renderer,1,1);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);w.setView({0,0,1280,720},1);w.draw(renderer);SDL_RenderPresent(renderer);};
      GameSpeedClock realtime;double previous=1.0/60;
      auto advance=[&](double elapsed) {
        const auto frame=realtime.advance(elapsed,speed,w.isPaused(),[&](float dt,float input){tick(dt,input);return !w.isPaused();});
        return frame.steps;
      };
      for(int n=0;n<24;++n){const auto begin=SDL_GetPerformanceCounter();advance(previous);draw();previous=milliseconds(begin)/1000;}
      realtime.reset(); // exclude warm-up debt from the measured capacity sample
      FrameProfile::totals.clear();FrameProfile::active=true;
      const double initial=w.getClock();double inputSeconds=0;
      for(int n=0;n<120;++n) {
        SDL_PumpEvents();const Uint64 frameStart=SDL_GetPerformanceCounter();
        const double input=previous;inputSeconds+=input;const int steps=advance(input);const double simMs=milliseconds(frameStart);
        const Uint64 renderStart=SDL_GetPerformanceCounter();draw();const double renderMs=milliseconds(renderStart);
        const double totalMs=milliseconds(frameStart);previous=totalMs/1000;
        csv<<speed<<','<<(night?"night":"off")<<','<<n<<','<<input<<','<<steps<<','<<simMs<<','<<renderMs<<','<<totalMs<<','<<realtime.pending()<<'\n';
      }
      FrameProfile::active=false;
      for(const auto &[name,ms]:FrameProfile::totals)phases<<speed<<','<<(night?"night":"off")<<','<<name<<','<<ms<<",120\n";
      phases.flush();
      check(std::abs((w.getClock()-initial)+realtime.pending()+realtime.discarded()-inputSeconds*speed)<.001,
            "rendered driver accounts for elapsed time at "+std::to_string(speed)+"x "+(night?"night":"daylight"));
      check(realtime.pending()<quantum && realtime.discarded()==0,"rendered driver keeps up without accumulating simulation debt");
      log<<"Rendered ratio "<<speed<<"x "<<(night?"night":"off")<<": "<<(w.getClock()-initial)/inputSeconds
         <<"x, pending="<<realtime.pending()<<"s, discarded="<<realtime.discarded()<<"s\n";log.flush();
    }
    SDL_Log("[SpeedCheck] %dx at %dHz complete",speed,hz);
  }
  Features::dayNightLighting=wasLighting;
  log<<"TOTAL "<<passed<<" passed, "<<failed<<" failed. Shared production speed timing; no speed-button UI.\n";
  return failed?1:0;
}
