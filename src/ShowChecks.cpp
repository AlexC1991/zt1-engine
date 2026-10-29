#include "World.hpp"
#include "Shows.hpp"
#include "SaveGame.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiButton.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <functional>
#include <set>
#include <cmath>
#include "Animation.hpp"

// Lab-only real-time soak setup. Uses stock definitions and ordinary needs;
// it changes the initial zoo, never keeps animals healthy during the run.
std::string prepareMarineSoak(World &world,ResourceManager *rm) {
  auto &map=world.getMap();auto &f=world.getFences();auto &a=world.getAnimals();
  auto &st=world.getStaff();auto &objects=world.objectsMutable();auto &shows=world.getShows();
  if(!f.exhibits().empty()||map.getWidth()<70)return "Load flat ff01 first.";
  int drag=1900;
  auto rectangle=[&](int x,int y,int width,int height,int type,const char *name) {
    for(int k=0;k<width;++k){f.place({true,x+k,y},type,drag,map);f.place({true,x+k,y+height},type,drag+1,map);}
    for(int k=0;k<height;++k){f.place({false,x,y+k},type,drag+2,map);f.place({false,x+width,y+k},type,drag+3,map);}
    drag+=4;
    for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,name,map);
    const int id=f.exhibitAt(x,y);if(auto *ex=f.exhibit(id)) {
      while(ex->wallSub<12&&f.canAdjustWall(id,1))f.adjustWall(id,1);
      ex->water=1;ex->purity=100;
    }
    return id;
  };
  const int home=rectangle(22,22,20,20,f.typeIndex("tankwal2"),"Dolphin home");
  const int show=rectangle(42,22,20,20,f.typeIndex("tankwal3"),"Dolphin show");
  if(home<0||show<0)return "Tank setup failed.";
  // Use the dolphin's preferred kelp and ocean-floor rocks, with enough
  // open swimming space. Report the actual suitability after construction.
  for(int tankX:{22,42}) {
    for(int row=0;row<4;++row)for(int column=0;column<5;++column)
      objects.add("scenery/foliage/kelp.ai",tankX+2+column*3,24+row*3,0);
    objects.add("scenery/other/aqrock3.ai",tankX+5,38,0);
    objects.add("scenery/other/aqrock3.ai",tankX+13,38,0);
    objects.add("scenery/other/aqrock3.ai",tankX+16,28,0);
    objects.add("scenery/other/aqrock3.ai",tankX+16,39,0);
  }
  world.reindex();
  a.adopt(a.typeOfFile("animals/dolphin.ai"),false,36.5f,32.5f,map,f,true);
  a.adopt(a.typeOfFile("animals/dolphin.ai"),true,37.5f,35.5f,map,f,true);
  for(auto &ex:const_cast<std::vector<Fences::Exhibit>&>(f.exhibits()))ex.animals=a.countIn(ex.id);
  for(int k=0;k<4;++k) {
    const int worker=st.hire(st.typeOfFile("staff/trainer.ai"),45.5f+k*2,44.5f,map,f);
    if(worker>=0)st.assign(worker,k==0?show:home,f);
  }
  f.selectFilter("scenery/other/filter1.ai");
  for(int tank:{home,show}) {
    bool placed=false;
    for(int y=18;y<46&&!placed;++y)for(int x=18;x<65&&!placed;++x) {
      int beside=-1;if(f.filterFit(x,y,map,&beside)==Fences::Fit::Ok&&beside==tank)placed=f.placeFilter(x,y,map);
    }
  }
  st.hire(st.typeOfFile("staff/maint.ai"),20.5f,44.5f,map,f);
  st.hire(st.typeOfFile("staff/maint.ai"),22.5f,44.5f,map,f);
  for(const auto &[file,x]:std::vector<std::pair<std::string,float>>{
      {"bgrstnd",24.5f},{"drkstnd",29.5f},{"bathroom",34.5f},{"restrant",40.5f}})
    objects.add("scenery/building/"+file+".ai",x,49.5f,0);
  objects.add("scenery/building/gstand.ai",65.5f,30.5f,6);
  world.reindex();
  for(int y=1;y<map.getHeight()-1;++y)for(int x=1;x<map.getWidth()-1;++x)
    if(world.canLayPath(x,y))map.setPath(x,y,"path");
  world.reindex();shows.refresh();
  shows.selectSpecies(show,"animals/dolphin.ai");shows.add(show,"airflip");shows.add(show,"tailwalk");
  shows.setFrequency(show,0);world.centreOn(42,32);
  std::ostringstream report;report<<"Stock marine soak ready; normal needs and timers. home="<<home<<" show="<<show<<"\n";
  for(const auto &animal:a.members())report<<animal.name<<" suitability="<<a.suitability(animal.id,map,f)<<"\n";
  report<<shows.describe();return report.str();
}

// An opt-in integration fixture. Run through --panel-shots with
// ZT_SHOW_CHECKS=1 and ZT_FLAT_LAB=1 on sm_aqua. It exercises the real
// world, original behavior queues, player UI, guests and saved-zoo reader.
int runShowChecks(World &world,UiGameScreen &hud,ZooSim &sim,const std::string &out,
                  const std::string &mapPath,const std::function<void(const char*)> &capture) {
  std::ofstream report(out+"/marine-show-checks.txt");int failures=0,checks=0;
  auto check=[&](const std::string &name,bool okay){++checks;failures+=!okay;report<<(okay?"PASS ":"FAIL ")<<name<<"\n";report.flush();SDL_Log("[ShowChecks] %s %s",okay?"PASS":"FAIL",name.c_str());};
  auto &f=world.getFences();auto &map=world.getMap();auto &a=world.getAnimals();auto &st=world.getStaff();auto &shows=world.getShows();auto &gs=world.getGuests();
  shows.clear();a.clear();st.clear();
  auto &guests=const_cast<std::vector<Guests::Guest>&>(gs.guests());guests.clear();
  auto run=[&](float seconds){for(int k=0;k<static_cast<int>(seconds*20);++k)world.update(SDL_GetKeyboardState(nullptr),0.05f);};
  int drag=10;
  auto rectangle=[&](int x,int y,int w,int h,int type) {
    int base=drag;drag+=4;
    for(int i=x;i<x+w;i++){f.place({true,i,y},type,base,map);f.place({true,i,y+h},type,base+1,map);}
    for(int j=y;j<y+h;j++){f.place({false,x,j},type,base+2,map);f.place({false,x+w,j},type,base+3,map);}
    auto ids=f.updateExhibits(map,1,0,1);for(int id:ids)f.finishExhibit(id,"Marine test "+std::to_string(id),map);
    return f.exhibitAt(x,y);
  };
  int tank=rectangle(42,24,10,10,f.typeIndex("tankwal3"));
  int home=rectangle(28,24,14,14,f.typeIndex("tankwal2"));
  check("two adjacent tanks formed",tank>=0 && home>=0 && tank!=home);if(tank<0 || home<0)return 1;
  for(int id:{tank,home})while(f.exhibit(id)->wallSub<10 && f.canAdjustWall(id,1))f.adjustWall(id,1);
  run(5);shows.refresh();check("show fence classified separately",shows.show(tank) && !shows.show(home));
  int ports=0;for(const auto &[edge,p]:f.pieces()){int show=-1,h=-1;if(f.showPortal(edge,show,h) && show==tank && h==home)++ports;}
  check("one portal in shared tank wall",ports==1);
  int one=a.adopt(a.typeOfFile("animals/orca.ai"),false,34.5f,31.5f,map,f);
  int two=a.adopt(a.typeOfFile("animals/orca.ai"),true,36.5f,31.5f,map,f);
  check("orca pair adopted",one>=0 && two>=0);run(1);
  check("linked species listed",shows.selectSpecies(tank,"animals/orca.ai"));
  std::string why;
  check("empty script refused",!shows.start(tank,why) && why.find("script")!=std::string::npos);
  check("wave trick available",shows.add(tank,"finswim"));
  check("missing specialist refused",!shows.start(tank,why) && why.find("specialist")!=std::string::npos);
  check("research trick locked",!shows.add(tank,"bkswim"));
  check("missing show object refused",!shows.add(tank,"ballplay"));
  shows.add(tank,"spit");shows.add(tank,"finswim");
  int specialist=st.hire(st.typeOfFile("staff/trainer.ai"),54.5f,24.5f,map,f);
  check("marine specialist assigned",specialist>=0 && st.assign(specialist,home,f)==0);
  // The player's original show panel, including its real button handlers.
  world.centreOn(41,29);hud.showExhibit(tank);hud.refreshShow();capture("show_script");
  check("show tank opens show script window",hud.isPanelOpen(41));
  auto *available=dynamic_cast<UiListBox*>(hud.getElementById(11111));
  auto *script=dynamic_cast<UiListBox*>(hud.getElementById(11115));
  check("player trick lists populated",available && script && available->getItemCount()>0 && script->getItemCount()==3);
  if(script)script->setSelectedIndex(1);
  if(auto *b=dynamic_cast<UiButton*>(hud.getElementById(11117)))if(b->onClick)b->onClick();
  check("player can reorder script",shows.show(tank)->script.front()=="spit");
  if(auto *b=dynamic_cast<UiButton*>(hud.getElementById(11105)))if(b->onClick)b->onClick();
  check("player admission spinner works",std::abs(shows.show(tank)->admission-5.75)<0.01);
  shows.setAdmission(tank,5.5);shows.setFrequency(tank,2);
  hud.showPanel(0,"");
  check("grandstand placed",world.objectsMutable().add("scenery/building/gstand.ai",55.5f,29.5f,6));
  world.reindex();int stand=-1;
  for(const auto &o:world.getObjects().objects())if(o.typeName=="gstand")stand=o.id;
  if(auto *o=world.objectsMutable().byId(stand)) {
    auto doors=gs.doorTiles(*o);check("grandstand has door",!doors.empty());
    for(auto [x,y]:doors){map.setPath(x,y,"road");for(int k=0;k<4;k++)map.setPath(x,y+k,"road");}
    if(!doors.empty())for(int n=0;n<4;n++) {
      gs.admit(0);auto &g=guests.back();g.x=doors[0].first+0.5f;g.y=doors[0].second+0.5f;g.state=Guests::State::Walking;
      g.hunger=g.thirst=g.bathroom=g.tired=0;g.leaving=false;
    }
  }
  world.reindex();
  bool started=shows.start(tank,why);report<<"Start reason: "<<why<<"\n";report<<shows.describe();
  for(const auto &[edge,p]:f.pieces())if(p.gate)report<<"Gate "<<edge.x<<","<<edge.y<<" tank="<<p.tank<<" tank2="<<p.tank2<<"\n";
  check("show starts",started);
  int completedBefore=shows.show(tank)->completed;
  float pausedClock=shows.show(tank)->elapsed;shows.update(0);check("zero time leaves show paused",shows.show(tank)->elapsed==pausedClock);
  bool sawPerformance=false,saved=false,loaded=false,seated=false;int inShow=0;
  for(int sec=0;sec<170;sec++) {
    run(1);auto *s=shows.show(tank);if(!s)break;
    if(s->phase==Shows::Phase::Performing) {
      if(!sawPerformance){sawPerformance=true;capture("show_performing");}
      for(const auto &g:guests)if(g.showTank==tank && g.showSeat>=0){seated=true;inShow++;}
      if(!saved) {
        saved=SaveGame::save(out+"/active-show.zt1save",world,sim,hud,mapPath);
        loaded=saved && SaveGame::load(out+"/active-show.zt1save",world,sim,hud);s=shows.show(tank);
      }
    }
    if(s->completed>completedBefore && s->phase==Shows::Phase::Waiting)break;
  }
  auto *s=shows.show(tank);
  check("show reaches performing phase",sawPerformance);
  check("spectators sit in grandstand",seated);
  check("save during performance",saved && loaded);
  check("show completes after reload",s && s->completed==completedBefore+1 && s->phase==Shows::Phase::Waiting);
  check("all six visible tricks completed",s && s->tricksDone==6 && s->tricksSkipped==0);
  check("orcas return home unboxed",a.member(one) && a.member(two) && a.member(one)->exhibit==home && a.member(two)->exhibit==home && !a.member(one)->boxed && !a.member(two)->boxed);
  check("specialist released",st.member(specialist) && st.member(specialist)->showTank<0);
  report<<shows.describe();
  double earned=shows.takeIncome();check("show admissions charged once",s && s->visitors==4 && std::abs(s->income-22)<0.01 && std::abs(earned-22)<0.01);
  check("income drain cannot charge twice",shows.takeIncome()==0);
  sim.earn(ZooSim::ShowIncome,earned);
  run(10);
  bool released=true;for(const auto &g:guests)if(g.showTank==tank)released=false;
  check("spectators leave after reaction",released);
  if(auto *o=world.objectsMutable().byId(stand))check("seats freed",o->visitorsNow==0);
  capture("show_returned");
  // Exact script editing limits and repetition penalties.
  while(shows.remove(tank,0)){}
  for(int i=0;i<20;i++)shows.add(tank,"finswim");
  check("20-trick script limit",s->script.size()==20 && !shows.add(tank,"spit"));
  check("repetition reduces benefit",shows.benefit(tank)<40);
  while(shows.remove(tank,0)){}shows.add(tank,"finswim");
  // A too-shallow tank and research/object requirements remain gameplay constraints.
  while(f.exhibit(tank)->wallSub>4 && f.canAdjustWall(tank,-1))f.adjustWall(tank,-1);
  while(f.exhibit(home)->wallSub>4 && f.canAdjustWall(home,-1))f.adjustWall(home,-1);
  run(2);
  check("deep trick unavailable in shallow tank",!shows.add(tank,"ballplay"));
  for(int id:{tank,home})while(f.exhibit(id)->wallSub<10 && f.canAdjustWall(id,1))f.adjustWall(id,1);
  run(5);
  // Show-object behavior, including its hidden food reward and exclusive use.
  while(shows.remove(tank,0)){}
  check("orca ball placed",world.objectsMutable().add("scenery/toys/orcaball.ai",47.5f,29.5f,0));
  world.reindex();check("ball trick becomes available",shows.add(tank,"ballplay"));
  st.moveTo(specialist,54.5f,24.5f);
  int beforeBall=s->completed,doneBall=s->tricksDone;
  bool ballStarted=shows.start(tank,why);check("ball show starts",ballStarted);
  bool sawBall=false,hiddenBall=false;
  if(ballStarted)for(int i=0;i<170;i++) {
    run(1);
    for(const auto &m:a.members())if(m.showTrick && m.anim.find("ball")!=std::string::npos)sawBall=true;
    for(const auto &o:world.getObjects().objects())if(o.typeName=="orcaball" && o.showHidden)hiddenBall=true;
    if(s->completed>beforeBall && s->phase==Shows::Phase::Waiting)break;
  }
  check("orca ball behavior animates",sawBall);
  check("orca ball show completes for both animals",s->completed==beforeBall+1 && s->tricksDone==doneBall+2);
  bool visible=true;for(const auto &o:world.getObjects().objects())if(o.typeName=="orcaball" && o.showHidden)visible=false;
  check("show object restored after use",visible);
  for(const char *file:{"animals/dolphin.ai","animals/sealion.ai","animals/seaotter.ai","animals/gwshark.ai","animals/jellyfsh.ai"}) {
    while(shows.remove(tank,0)){}
    a.clear();st.moveTo(specialist,54.5f,24.5f);
    int first=a.adopt(a.typeOfFile(file),false,34.5f,31.5f,map,f);
    int second=a.adopt(a.typeOfFile(file),true,36.5f,31.5f,map,f);run(1);
    bool selected=shows.selectSpecies(tank,file);std::string key;
    for(const auto &t:shows.tricks(file))if(t.visible && t.kind==0 && shows.unavailable(tank,file,t).empty()){key=t.key;break;}
    check(std::string(file)+" has an available plain trick",selected && !key.empty());
    if(!selected || key.empty() || first<0 || second<0)continue;
    shows.add(tank,key);int before=s->completed;
    bool started=shows.start(tank,why);check(std::string(file)+" show starts",started);
    if(started)for(int i=0;i<160;i++){run(1);if(s->completed>before && s->phase==Shows::Phase::Waiting)break;}
    check(std::string(file)+" completes and returns home",s->completed==before+1 && s->phase==Shows::Phase::Waiting && a.member(first) && a.member(second) && a.member(first)->exhibit==home && a.member(second)->exhibit==home && !a.member(first)->boxed && !a.member(second)->boxed);
  }
  // Make the specialist idle between isolated cancellation cases.
  if(auto *m=st.member(specialist)){m->water=false;m->climbing=false;m->job=Staff::Job::None;m->x=54.5f;m->y=24.5f;}
  bool restarted=shows.start(tank,why);check("second show can prepare",restarted);
  if(restarted){f.drainExhibit(tank);run(0.1f);}
  check("draining tank cancels safely",s->phase==Shows::Phase::Waiting && st.member(specialist)->showTank<0);
  check("drained tank refuses next show",!shows.start(tank,why));
  hud.showExhibit(tank);hud.refreshShow();capture("show_after_cancel");
  report<<checks<<" checks, "<<failures<<" failures\n";
  return failures?1:0;
}


std::string runShowStartChecks(World &world) {
  auto &f=world.getFences();auto &map=world.getMap();auto &a=world.getAnimals();
  auto &st=world.getStaff();auto &shows=world.getShows();
  if(!f.exhibits().empty())return "Load flat ff01 with no exhibits first.";
  const bool wasPaused=world.isPaused();
  world.setPaused(false);
  struct RestorePause { World &world; bool paused; ~RestorePause(){world.setPaused(paused);} } restorePause{world,wasPaused};
  std::ostringstream out;int passed=0,total=0;
  auto check=[&](const char *name,bool ok){++total;passed+=ok;out<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
  shows.clear();a.clear();st.clear();world.getGuests().clear();
  int drag=800;
  auto rectangle=[&](int x,int y,int w,int h,int type) {
    const int base=drag;drag+=4;
    for(int i=x;i<x+w;++i){f.place({true,i,y},type,base,map);f.place({true,i,y+h},type,base+1,map);}
    for(int j=y;j<y+h;++j){f.place({false,x,j},type,base+2,map);f.place({false,x+w,j},type,base+3,map);}
    for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,"Show regression",map);
    return f.exhibitAt(x,y);
  };
  int tank=rectangle(42,24,10,10,f.typeIndex("tankwal3"));
  int home=rectangle(28,24,14,14,f.typeIndex("tankwal2"));
  check("linked show and home tanks created",tank>=0 && home>=0 && tank!=home);
  if(tank<0 || home<0)return out.str();
  for(int id:{tank,home})while(f.exhibit(id)->wallSub<10 && f.canAdjustWall(id,1))f.adjustWall(id,1);
  auto run=[&](float seconds){for(int k=0;k<seconds*20;++k)world.update(SDL_GetKeyboardState(nullptr),0.05f);};
  run(5);shows.refresh();
  int animal=a.adopt(a.typeOfFile("animals/orca.ai"),false,34.5f,31.5f,map,f);
  int trainer=st.hire(st.typeOfFile("staff/trainer.ai"),54.5f,24.5f,map,f);
  check("performer and specialist placed",animal>=0 && trainer>=0);
  if(animal<0 || trainer<0)return out.str();
  run(0.1f); // Refresh exhibit animal counts before assigning staff.
  check("performer species selected",shows.selectSpecies(tank,"animals/orca.ai"));
  check("plain trick added",shows.add(tank,"finswim"));
  check("show object placed",world.objectsMutable().add("scenery/toys/orcaball.ai",47.5f,29.5f,0));
  world.reindex();
  const auto &tricks=shows.tricks("animals/orca.ai");
  auto ball=std::find_if(tricks.begin(),tricks.end(),[](const auto &t){return t.key=="ballplay";});
  check("placed show toy recognized by its name id",ball!=tricks.end() && shows.unavailable(tank,"animals/orca.ai",*ball).empty());
  check("specialist assigned to show tank",st.assign(trainer,tank,f)==0);
  auto *m=st.member(trainer);
  m->job=Staff::Job::None;m->workCheck=100000;m->idleLeft=100000;m->water=false;m->climbing=false;
  auto *s=shows.show(tank);if(!s)return out.str();
  shows.setFrequency(tank,0);s->clock=shows.settings.interval[0];
  m->job=Staff::Job::Visit;shows.update(0.1f);
  check("automatic start exposes busy specialist reason",s->phase==Shows::Phase::Waiting && s->status.find("busy")!=std::string::npos);
  m->job=Staff::Job::None;m->path.clear();m->pathAt=0;
  const float disabledClock=s->clock;
  const bool disabled=shows.setFrequency(tank,-1);
  shows.update(200);
  check("Never disables scheduled starts without advancing their clock",disabled && s->frequency==-1 && s->phase==Shows::Phase::Waiting && s->clock==disabledClock);
  check("Never schedule survives show-state restore",shows.restore(shows.snapshot()) && shows.show(tank)->frequency==-1);
  s=shows.show(tank);shows.setFrequency(tank,0);
  const float paused=s->clock;shows.update(0);
  check("paused continuous schedule does not advance",s->clock==paused);
  bool gathered=false,performed=false;
  for(int i=0;i<240 && !(s->completed>0 && s->phase==Shows::Phase::Waiting);++i) {
    run(1);gathered |= s->phase==Shows::Phase::Gathering;performed |= s->phase==Shows::Phase::Performing;
  }
  check("continuous mode starts with show-tank-only assignment",gathered);
  check("continuous show reaches performance",performed);
  check("continuous show completes and returns performer home",s->completed>0 && a.member(animal) && a.member(animal)->exhibit==home && !a.member(animal)->boxed);
  out<<shows.describe();
  shows.cancel(tank,"regression reset");
  m=st.member(trainer);m->exhibits.clear();m->showTank=-1;m->water=false;m->climbing=false;
  m->job=Staff::Job::None;m->path.clear();m->pathAt=0;m->z=0;m->workCheck=100000;m->idleLeft=100000;
  check("specialist assigned to home tank",st.assign(trainer,home,f)==0);
  s->clock=0;std::string why;
  check("home-tank assignment also starts a show",shows.start(tank,why));
  out<<"Start reason: "<<why<<"\n";
  shows.cancel(tank,"regression finished");
  f.selectFilter("scenery/other/filter1.ai");
  bool filterPlaced=false;
  for(int y=20;y<42 && !filterPlaced;++y)for(int x=20;x<44 && !filterPlaced;++x) {
    int beside=-1;
    if(f.filterFit(x,y,map,&beside)==Fences::Fit::Ok && beside==home) {
      filterPlaced=f.placeFilter(x,y,map);
      if(filterPlaced)out<<"Filter at "<<x<<","<<y<<"\n";
    }
  }
  check("outside tank filter placed for render inspection",filterPlaced);
  int whale=a.adopt(a.typeOfFile("animals/whleshrk.ai"),false,35.5f,31.5f,map,f,true);
  check("large swimmer available for wall inspection",whale>=0);
  if(auto *w=a.member(whale)) {
    w->x=30.2f;w->y=25.2f;w->fx=0;w->fy=-1;
    w->swimZ=w->swimTo=std::max(0.0f,f.waterLevel(*f.exhibit(home))-2.0f);
    a.labPlay(whale,"subswim",300);
  }
  world.reindex();world.centreOn(35,31);
  out<<passed<<"/"<<total<<" show-start checks passed\n";return out.str();
}


std::string runShowPolishChecks(World &world,ResourceManager *rm,bool demo) {
  if(!world.getFences().exhibits().empty())return "Load flat ff01 with no exhibits first.";
  std::ostringstream out;out<<runShowStartChecks(world);
  auto &f=world.getFences();auto &map=world.getMap();auto &a=world.getAnimals();
  auto &st=world.getStaff();auto &g=world.getGuests();auto &shows=world.getShows();
  const int tank=1,home=2;
  if(!f.exhibit(tank) || !f.exhibit(home))return out.str();
  const bool paused=world.isPaused();world.setPaused(false);
  struct RestorePause {World &w;bool p;~RestorePause(){w.setPaused(p);}} restorePause{world,paused};
  auto run=[&](float sec){for(int i=0;i<sec*20;++i)world.update(SDL_GetKeyboardState(nullptr),0.05f);};
  int passed=0,total=0;
  auto check=[&](const std::string &name,bool okay){++total;passed+=okay;out<<(okay?"PASS ":"FAIL ")<<name<<"\n";};
  shows.cancel(tank,"Polish fixture");a.clear();st.clear();g.clear();shows.clear();shows.refresh();
  // Every species' declared performance sets must be gated, including a
  // stale queue and forged show flags while still in the home tank.
  int species=0,sets=0,blocked=0,allowed=0,stale=0,artBlocked=0;
  for(int type=0;type<static_cast<int>(a.typeList.size());++type) {
    auto &t=a.typeList[type];if(t.showSets.empty())continue;++species;
    Animals::Member m;m.id=990000+type;m.type=type;m.exhibit=home;m.x=35.5f;m.y=31.5f;
    m.happiness=50;m.health=static_cast<float>(t.maxHits);m.swimZ=m.swimTo=3;
    for(const auto &name:t.showSets) {
      if(!a.set(m,name))continue;++sets;m.stack.clear();a.pushSet(m,name);blocked+=m.stack.empty();
      m.showTank=tank;m.showTrick=true;a.pushSet(m,name);blocked+=m.stack.empty();
      m.x=47.5f;m.y=29.5f;a.pushSet(m,name);allowed+=!m.stack.empty();
      m.x=35.5f;m.y=31.5f;m.showTank=-1;m.showTrick=false;m.stack={{name,0}};
      a.nextStep(m,map,f);stale+=std::none_of(m.stack.begin(),m.stack.end(),[&](const auto &v){return t.showSets.count(v.first)!=0;});
    }
    const std::string probe="__show_gate_probe";
    for(const auto &anim:t.showAnimations) {
      t.sets[0][probe]={{"fplay",{anim}},{"fplaytime",{"idle","1"}}};
      m.stack={{probe,0}};m.stepping=false;m.path.clear();m.pathAt=0;m.showDone=false;
      a.nextStep(m,map,f);artBlocked+=m.anim!=anim && !m.showDone;
    }
    t.sets[0].erase(probe);
  }
  int arts=0;for(const auto &t:a.typeList)arts+=static_cast<int>(t.showAnimations.size());
  check("all show species have gated performance definitions",species>=6 && sets>20);
  check("home-tank tricks blocked even with stale show flags",blocked==sets*2);
  check("active show-tank performers may run their declared tricks",allowed==sets);
  check("stale home-tank performance queues are discarded",stale==sets);
  check("shared ambient performance artwork is skipped outside shows",artBlocked==arts && arts>20);
  out<<"GATING "<<species<<" species; "<<sets<<" sets; "<<arts<<" performance animations\n";
  a.clear();g.clear();world.reindex();
  const int first=a.adopt(a.typeOfFile("animals/orca.ai"),false,34.5f,31.5f,map,f);
  const int second=a.adopt(a.typeOfFile("animals/orca.ai"),true,36.5f,31.5f,map,f);
  const int trainer=st.hire(st.typeOfFile("staff/trainer.ai"),54.5f,24.5f,map,f);
  run(0.1f);check("show specialist assigned",st.assign(trainer,tank,f)==0);
  auto *m=st.member(trainer);m->workCheck=100000;m->idleLeft=100000;
  check("grandstand available",world.objectsMutable().add("scenery/building/gstand.ai",55.5f,29.5f,6));
  const int standId=world.objectsMutable().last()->id;
  auto *stand=world.objectsMutable().byId(standId);
  bool slotsOkay=true;std::set<std::pair<int,int>> distinct;
  for(int facing:{0,2,4,6}) {
    stand->facing=facing;distinct.clear();
    for(int seat=0;seat<12;++seat) {
      Guests::Guest seated;
      slotsOkay &= g.positionShowSeat(seated,*stand,seat) && seated.showZ>=0 && seated.showZ<4;
      const float hx=(facing==2 || facing==6)?2.0f:0.5f,hy=(facing==2 || facing==6)?0.5f:2.0f;
      slotsOkay &= std::abs(seated.x-stand->x)<hx && std::abs(seated.y-stand->y)<hy;
      distinct.insert({static_cast<int>(std::lround(seated.x*1000)),static_cast<int>(std::lround(seated.y*1000))});
    }
    slotsOkay &= distinct.size()==12;
  }
  check("all twelve seats stay inside the rotated stand footprint",slotsOkay);stand->facing=6;
  int aligned=0;auto &view=world.getRenderer();
  for(int rotation=0;rotation<4;++rotation) {
    const float angle=stand->facing*3.14159265f/4.0f;
    const auto side=view.screenSide(std::sin(angle),-std::cos(angle));
    float sx,sy,depth,left,top,right,bottom;view.worldToScreenF(stand->x,stand->y,0,sx,sy,depth);
    if(stand->art->anchoredBounds(sx,sy,side,left,top,right,bottom))for(int seat=0;seat<12;++seat) {
      Guests::Guest guest;g.positionShowSeat(guest,*stand,seat);
      float x,y,d;view.worldToScreenF(guest.x,guest.y,g.groundAt(guest.x,guest.y)+guest.showZ,x,y,d);
      aligned+=x>=left && x<right && y>=top && y<bottom;
    }
    view.rotateView(1);
  }
  check("all seat anchors project onto the stand artwork in every view",aligned==48);
  auto doors=g.doorTiles(*stand);check("grandstand entrance exists",!doors.empty());
  if(doors.empty())return out.str();
  for(const auto &[x,y]:doors)map.setPath(x,y,"path");
  world.reindex();
  for(int i=0;i<12;++i) {
    g.admit(i%4);auto &guest=g.list.back();guest.x=doors[0].first+0.5f;guest.y=doors[0].second+0.5f;
    guest.hunger=guest.thirst=guest.bathroom=guest.tired=0;guest.leaving=false;guest.state=Guests::State::Walking;
    // This fixture verifies all seat anchors and restore, independently of optional show interest.
    guest.showTank=tank;guest.showStand=stand->id;guest.showSeat=-1;guest.showExit=0;
    guest.showDoorX=guest.x;guest.showDoorY=guest.y;guest.need="show";guest.target=tank;
    guest.building=world.objectsMutable().indexOf(stand->id);guest.path.clear();guest.pathAt=0;
  }
  shows.selectSpecies(tank,"animals/orca.ai");
  shows.add(tank,"finswim");shows.add(tank,"ballplay");shows.add(tank,"spit");
  auto *s=shows.show(tank);shows.setFrequency(tank,0);s->clock=shows.settings.interval[0];
  bool climbed=false,descended=false,onDeck=false,seated=false,performed=false,snapshotChecked=false;
  auto fileRoundTrip=[&](const std::string &stage) {
    const auto animalsBefore=a.list.size(),guestsBefore=g.list.size(),staffBefore=st.members().size();
    const double incomeBefore=shows.show(tank)->income;
    ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
    Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
    for(const char *ext:{".zt1save",".zoo"}) {
      const auto file=std::filesystem::temp_directory_path()/
          ("zt1-show-save-"+std::to_string(SDL_GetTicks64())+ext);
      const bool saved=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn");
      const bool loaded=saved&&SaveGame::load(file.string(),world,books,hud);
      s=shows.show(tank);m=st.member(trainer);
      check(stage+" full-file "+ext+" restores actors and income",
            loaded&&s&&m&&a.list.size()==animalsBefore&&g.list.size()==guestsBefore&&
            st.members().size()==staffBefore&&std::abs(s->income-incomeBefore)<0.01);
      if(!saved)out<<"SAVE ERROR "<<SaveGame::lastError()<<"\n";
      std::error_code ec;std::filesystem::remove(file,ec);
    }
    SaveGame::goals=priorGoals;
  };
  std::set<int> savedPhases;
  for(int i=0;i<6000 && (s->completed==0 || s->phase!=Shows::Phase::Waiting);++i) {
    for(auto &animal:a.list){animal.happiness=100;animal.pending=0;animal.hunger=0;animal.energy=0;}
    world.update(SDL_GetKeyboardState(nullptr),0.05f);
    m=st.member(trainer);climbed |= m->climbing && !m->climbDown;descended |= m->climbing && m->climbDown;
    if(!demo && s->phase!=Shows::Phase::Waiting && savedPhases.insert(int(s->phase)).second)
      fileRoundTrip("orca phase "+std::to_string(int(s->phase)));
    if(s->phase==Shows::Phase::Performing) {
      performed=true;onDeck |= std::hypot(m->x-m->inX,m->y-m->inY)<0.01f &&
        std::abs(st.heightOf(*m,map)-(f.exhibit(tank)->wallTop()-0.5f))<0.01f;
      int seats=0;for(const auto &guest:g.list)seats+=guest.showTank==tank && guest.showSeat>=0;
      seated |= seats==12;
      if(!snapshotChecked && seats==12) {
        const std::string snapshot=shows.snapshot();
        const bool loaded=shows.restore(snapshot);s=shows.show(tank);m=st.member(trainer);
        int restoredSeats=0;for(const auto &guest:g.list)restoredSeats+=guest.showTank==tank && guest.showSeat>=0;
        check("active-show restore retains twelve seated guests and platform specialist",loaded && restoredSeats==12 && m->phase==2 && std::abs(m->x-m->inX)<0.01f && std::abs(m->y-m->inY)<0.01f);
        snapshotChecked=true;
      }
      if(demo && seats==12){world.centreOn(48,29);out<<"DEMO paused during performance with twelve seated guests\n";break;}
    }
  }
  check("specialist climbs onto the platform before performance",climbed && onDeck && performed);
  check("twelve guests reach separate grandstand seats",seated);
  if(!demo) {
    check("six orca tricks complete without skips",s->completed==1 && s->tricksDone==6 && s->tricksSkipped==0);
    check("both orcas return home unboxed",a.member(first)->exhibit==home && a.member(second)->exhibit==home && !a.member(first)->boxed && !a.member(second)->boxed);
    check("specialist descends before being released",descended && m->showTank<0 && std::abs(m->z)<0.01f && !m->climbing);
    a.clear();g.clear();
    m->exhibits.clear(); // The scheduled dolphin program must use a roaming specialist.
    const int d1=a.adopt(a.typeOfFile("animals/dolphin.ai"),false,34.5f,31.5f,map,f);
    const int d2=a.adopt(a.typeOfFile("animals/dolphin.ai"),true,36.5f,31.5f,map,f);
    shows.selectSpecies(tank,"animals/dolphin.ai");
    for(const auto &trick:shows.tricks("animals/dolphin.ai"))
      if(trick.visible && trick.kind==0 && shows.unavailable(tank,"animals/dolphin.ai",trick).empty())shows.add(tank,trick.key);
    const int before=s->completed,done=s->tricksDone,skipped=s->tricksSkipped;
    const int expected=static_cast<int>(s->script.size())*2;
    s->clock=shows.settings.interval[0];bool dolphinPerformed=false;
    savedPhases.clear();
    for(int i=0;i<8000 && (s->completed==before || s->phase!=Shows::Phase::Waiting);++i) {
      for(auto &animal:a.list){animal.happiness=100;animal.pending=0;animal.hunger=0;animal.energy=0;}
      world.update(SDL_GetKeyboardState(nullptr),0.05f);
      dolphinPerformed |= s->phase==Shows::Phase::Performing;
      if(s->phase!=Shows::Phase::Waiting && savedPhases.insert(int(s->phase)).second)
        fileRoundTrip("dolphin phase "+std::to_string(int(s->phase)));
    }
    check("roaming specialist automatically takes the scheduled dolphin show",dolphinPerformed && m->exhibits.empty());
    check("dolphins perform their available plain-trick program",dolphinPerformed && expected>0 && s->tricksDone==done+expected && s->tricksSkipped==skipped);
    check("dolphin show completes and returns both animals home",s->completed==before+1 && s->phase==Shows::Phase::Waiting && a.member(d1)->exhibit==home && a.member(d2)->exhibit==home && !a.member(d1)->boxed && !a.member(d2)->boxed);
    for(const std::string file:{"animals/gwshark.ai","animals/jellyfsh.ai","animals/sealion.ai","animals/seaotter.ai"}) {
      a.clear();g.clear();world.getItems().clear();m->exhibits.clear();
      const int type=a.typeOfFile(file);
      const int one=a.adopt(type,false,34.5f,31.5f,map,f,true);
      const int two=a.adopt(type,true,36.5f,31.5f,map,f,true);
      const bool selected=shows.selectSpecies(tank,file);s=shows.show(tank);
      for(const auto &trick:shows.tricks(file))
        if(trick.visible && trick.kind==0 && shows.unavailable(tank,file,trick).empty())shows.add(tank,trick.key);
      const int prior=s->completed,priorDone=s->tricksDone,priorSkipped=s->tricksSkipped;
      const int wanted=static_cast<int>(s->script.size())*2;s->clock=shows.settings.interval[0];
      bool performing=false;
      for(int i=0;i<12000 && (s->completed==prior || s->phase!=Shows::Phase::Waiting);++i) {
        for(auto &animal:a.list){animal.happiness=100;animal.pending=0;animal.hunger=0;animal.energy=0;}
        world.update(SDL_GetKeyboardState(nullptr),0.05f);performing |= s->phase==Shows::Phase::Performing;
      }
      check(file+" automatically performs its plain-trick program",selected && one>=0 && two>=0 && wanted>0 && performing && s->tricksDone==priorDone+wanted && s->tricksSkipped==priorSkipped);
      check(file+" completes and returns both adults home",s->completed==prior+1 && s->phase==Shows::Phase::Waiting && a.member(one) && a.member(two) && a.member(one)->exhibit==home && a.member(two)->exhibit==home && !a.member(one)->boxed && !a.member(two)->boxed);
      out<<file<<" "<<shows.describe();
    }
  }
  out<<shows.describe()<<passed<<"/"<<total<<" show-polish checks passed\n";
  world.centreOn(48,29);return out.str();
}

std::string runShowManagementChecks(World &world,ResourceManager *rm) {
  if(!world.getFences().exhibits().empty())return "Load flat ff01 with no exhibits first.";
  std::ostringstream out;out<<runShowStartChecks(world);int passed=0,total=0;
  auto check=[&](const char *name,bool ok){++total;passed+=ok;out<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
  auto &f=world.getFences();auto &map=world.getMap();auto &a=world.getAnimals();
  auto &st=world.getStaff();auto &g=world.getGuests();auto &shows=world.getShows();auto &objects=world.objectsMutable();
  const int tank=1,home=2;if(!f.exhibit(tank)||!f.exhibit(home))return out.str();
  const bool paused=world.isPaused();world.setPaused(false);
  struct RestorePause {World &w;bool p;~RestorePause(){w.setPaused(p);}} restore{world,paused};
  shows.cancel(tank,"Management fixture");g.clear();world.getItems().clear();
  auto run=[&](float sec){for(int i=0;i<sec*20;++i)world.update(SDL_GetKeyboardState(nullptr),0.05f);};

  // Use real cursor handlers for land refusal and in-water purchases.
  world.setObjectTool("scenery/foliage/britstar.ai",125,0);
  int px,py;world.pointToWindow(20.5f,20.5f,0,px,py);
  const size_t before=objects.objects().size();world.mouseMove(px,py);world.mouseDown(px,py);
  check("marine foliage cannot be bought on dry land",objects.objects().size()==before);
  world.pointToWindow(32.5f,28.5f,f.exhibit(home)->floorHeight,px,py);
  world.mouseMove(px,py);world.mouseDown(px,py);
  check("marine foliage can be bought on a filled tank floor",objects.objects().size()==before+1);
  world.setObjectTool("",0,0);
  const int star=objects.last()->id;
  bool pickedAll=true,removedOnlyObject=true;
  for(int rotation=0;rotation<4;++rotation) {
    auto *o=objects.byId(star);if(!o){pickedAll=false;break;}
    float sx,sy,depth;auto &view=world.getRenderer();
    view.worldToScreenF(o->x,o->y,f.exhibit(home)->floorHeight,sx,sy,depth);
    bool found=false;int wx=0,wy=0;
    // Find an opaque point through the real picker, then use that same screen point to demolish.
    for(int dy=-24;dy<8 && !found;++dy)for(int dx=-30;dx<=30 && !found;++dx)
      if(objects.pick(sx+dx,sy+dy,view,map)==objects.indexOf(star)) {
        world.pointToWindow(o->x,o->y,f.exhibit(home)->floorHeight,wx,wy);
        wx+=dx;wy+=dy;found=true;
      }
    pickedAll &= found;
    if(found) {
      const size_t walls=f.pieces().size();world.setBulldozer(true);world.mouseMove(wx,wy);
      pickedAll &= objects.highlight==objects.indexOf(star);
      // Preview on all sides; delete only on the final side.
      if(rotation==3){world.mouseDown(wx,wy);removedOnlyObject &= !objects.byId(star) && walls==f.pieces().size();}
      world.setBulldozer(false);
    }
    world.rotateView(1);
  }
  check("underwater object highlights at its visible pixels in four views",pickedAll);
  check("demolition removes the highlighted object without removing tank walls",removedOnlyObject);

  // Add a second home tank along the show's north side, without replacing the shared boundary.
  int drag=2200;
  auto rectangle=[&](int x,int y,int w,int h,int type) {
    for(int i=x;i<x+w;++i)for(int sy:{y,y+h})if(!f.at({true,i,sy}))f.place({true,i,sy},type,drag++,map);
    for(int j=y;j<y+h;++j)for(int sx:{x,x+w})if(!f.at({false,sx,j}))f.place({false,sx,j},type,drag++,map);
    for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,"Management test",map);
    return f.exhibitAt(x,y);
  };
  const int home2=rectangle(42,14,10,10,f.typeIndex("tankwal2"));
  if(home2>=0)while(f.exhibit(home2)->wallSub<10 && f.canAdjustWall(home2,1))f.adjustWall(home2,1);
  run(5);shows.refresh();
  auto ports=shows.links(tank);
  check("one show tank links two separate home tanks",home2>=0 &&
    std::any_of(ports.begin(),ports.end(),[&](auto p){return p.second==home;}) &&
    std::any_of(ports.begin(),ports.end(),[&](auto p){return p.second==home2;}));
  int platforms=0;bool clearPlatform=true;
  for(const auto &[edge,p]:f.pieces())if(p.gate && (p.tank==tank || p.tank2==tank)) {
    const int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
    const int left=f.exhibitAt(ax,ay),right=f.exhibitAt(edge.x,edge.y);
    ++platforms;clearPlatform &= (left>=0)!=(right>=0);
  }
  check("platform automatically relocates to an open bank when another tank connects",platforms==1 && clearPlatform);
  Fences::Edge moved;bool candidate=false;
  for(const auto &[edge,p]:f.pieces())if(p.tank==tank && f.canGate(edge)) {moved=edge;candidate=true;break;}
  world.setGateTool(true);
  if(candidate) {
    const float x=moved.alongX?moved.x+0.5f:static_cast<float>(moved.x);
    const float y=moved.alongX?static_cast<float>(moved.y):moved.y+0.5f;
    world.pointToWindow(x,y,f.exhibit(tank)->wallTop(),px,py);
    world.mouseMove(px,py);world.mouseDown(px,py);
  }
  world.setGateTool(false);
  check("the fence gate tool manually relocates a tank platform",candidate && f.at(moved) && f.at(moved)->gate);
  // Simulate a building covering the old approach, then repair through the normal show refresh.
  bool obstructionMoved=false,blockedManual=false;
  if(candidate && f.at(moved)->gate) {
    const int ax=moved.alongX?moved.x:moved.x-1,ay=moved.alongX?moved.y-1:moved.y;
    const bool aIn=f.exhibitAt(ax,ay)==tank;
    const int ox=aIn?moved.x:ax,oy=aIn?moved.y:ay;
    if(objects.add("scenery/building/gstand.ai",ox+0.5f,oy+0.5f,0)) {
      const int obstruction=objects.last()->id;world.reindex();shows.refresh();
      obstructionMoved=!f.at(moved)->gate;
      blockedManual=!f.canGate(moved);
      int gates=0;
      for(const auto &[edge,p]:f.pieces())if(p.gate && (p.tank==tank || p.tank2==tank)) {
        ++gates;
        const int x=edge.alongX?edge.x:edge.x-1,y=edge.alongX?edge.y-1:edge.y;
        const bool in=f.exhibitAt(x,y)==tank;
        obstructionMoved &= !f.platformBlocked(in?edge.x:x,in?edge.y:y);
      }
      obstructionMoved &= gates==1;
      objects.remove(objects.indexOf(obstruction));world.reindex();shows.refresh();
    }
  }
  check("automatic platform relocation avoids a building obstructing its bank",obstructionMoved);
  check("manual platform relocation rejects an obstructed bank",blockedManual);
  a.clear();st.clear();g.clear();
  const int first=a.adopt(a.typeOfFile("animals/orca.ai"),false,34.5f,31.5f,map,f);
  const int second=a.adopt(a.typeOfFile("animals/orca.ai"),true,47.5f,19.5f,map,f);
  const int trainer=st.hire(st.typeOfFile("staff/trainer.ai"),54.5f,24.5f,map,f);
  auto *m=st.member(trainer);if(m){m->workCheck=100000;m->idleLeft=100000;}
  shows.selectSpecies(tank,"animals/orca.ai");auto *s=shows.show(tank);s->script.clear();shows.add(tank,"finswim");s->frequency=-1;
  std::string why;bool started=shows.start(tank,why);out<<"MULTI-HOME start: "<<why<<"\n";
  check("performers are gathered from both home tanks",started && s->actors.size()==2);
  const int completed=s->completed;
  for(int i=0;i<8000 && (s->completed==completed || s->phase!=Shows::Phase::Waiting);++i) {
    for(auto &animal:const_cast<std::vector<Animals::Member>&>(a.members())){animal.happiness=100;animal.hunger=0;animal.energy=0;}
    world.update(SDL_GetKeyboardState(nullptr),0.05f);
  }
  check("performers complete the show and return to their own home tank",first>=0 && second>=0 &&
    s->completed==completed+1 && s->phase==Shows::Phase::Waiting &&
    a.member(first)->exhibit==home && a.member(second)->exhibit==home2);
  m=st.member(trainer);
  if(m) {
    // Relocating a platform during a show must not strand its specialist on the old bank.
    for(const auto &[edge,piece]:f.pieces())if(piece.gate && f.exhibitOf(edge)==tank) {
      const int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
      const bool aIn=f.exhibitAt(ax,ay)==tank;
      m->inX=(aIn?ax:edge.x)+0.5f;m->inY=(aIn?ay:edge.y)+0.5f;
      m->dropX=m->outX=(aIn?edge.x:ax)+0.5f;m->dropY=m->outY=(aIn?edge.y:ay)+0.5f;
      break;
    }
    const float oldX=m->outX,oldY=m->outY;
    s->phase=Shows::Phase::Performing;s->trainer=trainer;s->actors.clear();
    m->showTank=m->tank=tank;m->phase=2;m->z=1;m->x=m->inX;m->y=m->inY;
    int blocker=-1;
    if(objects.add("scenery/building/gstand.ai",oldX,oldY,0))blocker=objects.last()->id;
    world.reindex();shows.refresh();shows.update(0.05f);
    check("moving an active show platform releases its specialist onto the new clear bank",
      blocker>=0 && s->phase==Shows::Phase::Waiting && m->showTank<0 && m->z==0 &&
      std::hypot(m->x-oldX,m->y-oldY)>0.1f && f.exhibitAt(static_cast<int>(m->x),static_cast<int>(m->y))<0 &&
      !f.platformBlocked(static_cast<int>(m->x),static_cast<int>(m->y)));
    if(blocker>=0)objects.remove(objects.indexOf(blocker));
    world.reindex();shows.refresh();
    // A working diver also drops the old ladder coordinates and replans the next duty.
    for(const auto &[edge,piece]:f.pieces())if(piece.gate && f.exhibitOf(edge)==tank) {
      const int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
      const bool aIn=f.exhibitAt(ax,ay)==tank;
      m->inX=(aIn?ax:edge.x)+0.5f;m->inY=(aIn?ay:edge.y)+0.5f;
      m->outX=(aIn?edge.x:ax)+0.5f;m->outY=(aIn?edge.y:ay)+0.5f;
      break;
    }
    bool movedDive=false;
    for(const auto &[edge,piece]:f.pieces())if(piece.tank==tank && f.canGate(edge)){f.moveGate(edge);movedDive=true;break;}
    m->visit=m->tank=tank;m->job=Staff::Job::Visit;m->water=3;m->phase=24;
    st.visitStep(*m,map,f);
    check("a working diver safely leaves a relocated ladder before replanning",
      movedDive && m->tank<0 && m->water==0 && m->job==Staff::Job::None &&
      std::abs(m->x-m->outX)<0.01f && std::abs(m->y-m->outY)<0.01f);
    world.getItems().clear();const double cost=st.upkeep;
    m->visit=tank;m->job=Staff::Job::Visit;m->phase=2;m->dropX=47.5f;m->dropY=29.5f;
    st.visitStep(*m,map,f);
    check("specialists cannot leave ordinary food piles in show tanks",world.getItems().foodIn(tank)==0 && st.upkeep==cost);
    m->visit=home;m->job=Staff::Job::Visit;m->phase=2;m->dropX=34.5f;m->dropY=31.5f;
    st.visitStep(*m,map,f);
    check("specialists still feed animals in home tanks",world.getItems().foodIn(home)>0);
  }
  st.clear();

  // Two shows and several stands: prices belong to the show, not an individual seat bank.
  const int other=rectangle(24,44,10,10,f.typeIndex("tankwal3"));run(5);
  int stands[3]={-1,-1,-1};
  for(int i=0;i<3;++i) {
    const float x=i==2?37.5f:55.5f,y=i==2?49.5f:i==0?25.5f:31.5f;
    if(objects.add("scenery/building/gstand.ai",x,y,6))stands[i]=objects.last()->id;
  }
  world.reindex();shows.refresh();
  out<<"STAND LINKS other="<<other<<" exists="<<(shows.show(other)!=nullptr)<<" ids="<<stands[0]<<","<<stands[1]<<","<<stands[2]<<" links="<<shows.standShow(stands[0])<<","<<shows.standShow(stands[1])<<","<<shows.standShow(stands[2])<<"\n";
  check("each grandstand links to exactly the show tank it faces",other>=0 && shows.show(other) && shows.standShow(stands[0])==tank && shows.standShow(stands[1])==tank && shows.standShow(stands[2])==other);
  shows.setAdmission(other,6.5);shows.setAdmission(tank,8.25);
  check("show admission updates all its stands without changing another show",objects.byId(stands[0])->price==8.25f && objects.byId(stands[1])->price==8.25f && objects.byId(stands[2])->price==6.5f);
  shows.setStandAdmission(stands[0],9);
  check("editing a linked stand updates that show and its other stands",s->admission==9 && objects.byId(stands[1])->price==9 && objects.byId(stands[2])->price==6.5f);
  const std::string snapshot=shows.snapshot(false);shows.restore(snapshot);s=shows.show(tank);
  check("show prices and stand links rebuild after restore",s && s->admission==9 && shows.standShow(stands[0])==tank && objects.byId(stands[1])->price==9);

  // 1,000 guests, including hungry, distant and already busy guests. No synchronized crowd rush.
  shows.setAdmission(tank,5.5);s->phase=Shows::Phase::Gathering;s->audienceCheck=0;s->audienceCursor=0;
  g.clear();shows.audienceStats={};shows.guestShowRetry.clear();
  const auto doors=g.doorTiles(*objects.byId(stands[0]));if(doors.empty())return out.str();
  for(auto [x,y]:doors)map.setPath(x,y,"path");world.reindex();g.rng.seed(2001);
  for(int i=0;i<1000;++i) {
    g.admit(0);auto &guest=g.list.back();guest.x=doors[0].first+0.5f;guest.y=doors[0].second+0.5f;
    guest.state=Guests::State::Walking;guest.building=-1;guest.hunger=guest.thirst=guest.bathroom=guest.tired=0;
    if(i%4==0)guest.hunger=95;
    if(i%4==1){guest.x=110;guest.y=110;}
    if(i%4==2){guest.building=0;guest.need="food";}
  }
  const Uint64 begin=SDL_GetPerformanceCounter();
  for(int i=0;i<240;++i)shows.audiences(*s,1.0f/60);
  const double ms=1000.0*(SDL_GetPerformanceCounter()-begin)/SDL_GetPerformanceFrequency();
  bool protectedNeeds=true;int reservations=0;std::set<std::pair<int,int>> seats;
  bool unique=true;
  for(size_t i=0;i<g.list.size();++i) {
    const auto &guest=g.list[i];if(i%4!=3)protectedNeeds &= guest.showTank<0;
    if(guest.showTank==tank){++reservations;if(guest.showSeat>=0)unique &= seats.insert({guest.showStand,guest.showSeat}).second;}
  }
  check("hungry, distant and busy guests keep their existing priorities",protectedNeeds);
  check("show pathfinding is bounded instead of routing the whole crowd at once",shows.audienceStats.considered<=120 && shows.audienceStats.routeAttempts<=40);
  check("seat reservations respect stand capacity and never duplicate seats",reservations<=24 && unique);
  out<<"CROWD 1000 guests; 240 updates; "<<shows.audienceStats.considered<<" decisions; "<<shows.audienceStats.routeAttempts<<" routes; "<<ms<<" ms\n";
  shows.cancel(tank,"Management fixture complete");g.clear();
  // An old pile is removed without waiting for it to be eaten, including restore.
  world.getItems().clear();
  world.getItems().addFood(tank,47.5f,29.5f,2,"fish");
  world.getItems().addFood(home,34.5f,31.5f,2,"fish");
  shows.refresh();
  check("legacy show-tank food is removed while home food is preserved",world.getItems().foodIn(tank)==0 && world.getItems().foodIn(home)>0);
  world.getItems().addFood(tank,47.5f,29.5f,2,"fish");world.getItems().update(0.1f,f);
  check("show-tank food is rejected by the item update as well as specialist tasks",world.getItems().foodIn(tank)==0);
  // Cancel a real, entered two-home show; preserve both homes through restore.
  const int specialist=st.hire(st.typeOfFile("staff/trainer.ai"),54.5f,24.5f,map,f);
  if(auto *keeper=st.member(specialist)){keeper->workCheck=keeper->idleLeft=100000;}
  s=shows.show(tank);s->frequency=-1;s->script={"finswim"};s->species="animals/orca.ai";
  bool entered=shows.start(tank,why);
  for(int i=0;i<2400 && entered && s->phase!=Shows::Phase::Performing;++i)run(0.05f);
  entered &= s->phase==Shows::Phase::Performing;
  shows.cancel(tank,"Cancellation return regression");
  check("cancelling an entered show keeps live performers reserved for return",
    entered && s->phase==Shows::Phase::Returning && s->trainer<0 &&
    a.member(first)->showHome==home && a.member(second)->showHome==home2 &&
    !a.member(first)->boxed && !a.member(second)->boxed);
  bool restored=shows.restore(shows.snapshot());s=shows.show(tank);
  check("an interrupted show's separate home assignments survive restore",
    restored && s->phase==Shows::Phase::Returning && a.member(first)->showHome==home && a.member(second)->showHome==home2);
  for(int i=0;i<3600 && s->phase!=Shows::Phase::Waiting;++i)run(0.05f);
  check("cancelled performers swim home without reopening capsules in the show tank",
    s->phase==Shows::Phase::Waiting && a.member(first)->exhibit==home && a.member(second)->exhibit==home2 &&
    !a.member(first)->boxed && !a.member(second)->boxed);
  // If the portal is removed, keep a transport capsule closed until placed home.
  auto *stranded=a.member(first);
  if(stranded) {
    stranded->x=47.5f;stranded->y=29.5f;stranded->exhibit=tank;
    stranded->showTank=tank;stranded->showHome=home;
    Shows::Actor actor;actor.id=first;actor.home=home;actor.portal=ports.front().first;
    s->actors={actor};s->phase=Shows::Phase::Performing;s->trainer=-1;
    f.drainExhibit(tank);shows.cancel(tank,"Removed-portal regression");run(3);
  }
  check("an unavailable portal leaves one closed capsule instead of an unbox loop",
    stranded && stranded->boxed && stranded->showTank<0 && s->phase==Shows::Phase::Waiting);
  if(stranded){stranded->x=34.5f;stranded->y=31.5f;run(3);}
  check("placing that capsule back in the home tank releases the animal",
    stranded && !stranded->boxed && stranded->exhibit==home);
  // An automatic home-tank visit must reach food sites past ladder-side plants.
  st.clear();world.getItems().clear();float entryX=0,entryY=0,bankX=0,bankY=0;bool entry=false;
  for(const auto &[edge,piece]:f.pieces())if(piece.gate && f.exhibitOf(edge)==home) {
    const int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
    const bool aIn=f.exhibitAt(ax,ay)==home;
    entryX=(aIn?ax:edge.x)+0.5f;entryY=(aIn?ay:edge.y)+0.5f;
    bankX=(aIn?edge.x:ax)+0.5f;bankY=(aIn?edge.y:ay)+0.5f;entry=true;break;
  }
  if(entry)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
    const int x=static_cast<int>(entryX)+dx,y=static_cast<int>(entryY)+dy;
    if(f.exhibitAt(x,y)==home)objects.add("scenery/foliage/britstar.ai",x+0.5f,y+0.5f,0);
  }
  world.reindex();for(auto &animal:const_cast<std::vector<Animals::Member>&>(a.members())){animal.hunger=100;animal.health=400;}
  run(0.1f);
  const int feeder=entry?st.hire(st.typeOfFile("staff/trainer.ai"),bankX,bankY,map,f):-1;
  if(feeder>=0)st.assign(feeder,home,f);
  for(int i=0;i<2400 && world.getItems().foodIn(home)<=0;++i)run(0.05f);
  check("automatic home-tank feeding can route past plants surrounding the ladder",
    feeder>=0 && world.getItems().foodIn(home)>0 && world.getItems().foodIn(tank)==0);
  world.centreOn(48,29);out<<passed<<"/"<<total<<" show-management checks passed\n";return out.str();
}
