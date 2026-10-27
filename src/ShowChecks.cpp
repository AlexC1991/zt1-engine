#include "World.hpp"
#include "Shows.hpp"
#include "SaveGame.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiButton.hpp"
#include <fstream>
#include <functional>
#include <set>
#include <cmath>

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
