#include "World.hpp"
#include "Animation.hpp"
#include "ItemCatalog.hpp"
#include "IniReader.hpp"
#include "ZooSim.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

// Only reached from the Lab-only console guard. Uses the same definitions,
// clocks, gate renderer and specialist visit state machine as normal Play.
std::string runActivityPolishChecks(World &world, ResourceManager *rm, bool demo) {
  std::ostringstream report;
  int checks=0, failures=0;
  auto check=[&](const std::string &name,bool pass) {
    ++checks; failures+=!pass;
    report<<(pass?"PASS ":"FAIL ")<<name<<"\n";
    SDL_Log("[ActivityPolish] %s %s",pass?"PASS":"FAIL",name.c_str());
  };
  auto &map=world.getMap(); auto &fn=world.getFences();
  auto &st=world.getStaff(); auto &animals=world.getAnimals();
  if(map.getWidth()<55 || map.getHeight()<55 || !fn.exhibits().empty())
    return "Load flat Basic Grass (ff01), with no exhibits, in the Test Zoo first.";
  st.clear(); animals.clear(); world.getGuests().clear();
  auto &objects=world.objectsMutable();
  auto &list=objects.all();
  list.erase(std::remove_if(list.begin(),list.end(),[](const auto &o){return o.bought;}),list.end());
  world.reindex();

  if(!demo) {
    ZooReader empty, fixture;
    PlacedObjects probe; probe.load(empty,rm);
    auto &records=const_cast<std::vector<ZooReader::ZooObject>&>(fixture.getObjects());
    int drawable=0, idleAnimated=0, usedAnimated=0, staticOnly=0;
    int missing=0, clocks=0, background=0;
    std::set<std::string> usedTypes, bgTypes;
    for(const auto &item:ItemCatalog::get().all()) {
      if(item.file.rfind("scenery/building/",0)!=0)continue;
      IniReader *ai=rm->getIniReader(item.file);
      if(!ai)continue;
      const bool hasIdle=!ai->get("animations","idle").empty();
      const bool hasUsed=!ai->get("animations","used").empty();
      const bool hasBg=!ai->get("animations","bg").empty();
      delete ai;
      if(!probe.add(item.file,25.5f,25.5f,0)) {
        report<<"SKIP "<<item.file<<" (no drawable idle art)\n";
        missing+=hasIdle;
        continue;
      }
      auto *o=probe.last(); ++drawable;
      idleAnimated+=o->art->frameCount()>1;
      usedAnimated+=o->used && o->used->frameCount()>1;
      background+=o->bg!=nullptr;
      staticOnly+=o->art->frameCount()<=1 && (!o->used || o->used->frameCount()<=1);
      missing+=(hasUsed && !o->used)+(hasBg && !o->bg);
      if(hasUsed)usedTypes.insert(o->typeName);
      if(hasBg)bgTypes.insert(o->typeName);
      report<<"BUILDING "<<o->typeName<<" idle="<<o->art->frameCount()
            <<" used="<<(o->used?o->used->frameCount():0)
            <<" bg="<<(o->bg?o->bg->frameCount():0)<<"\n";
      o->inUse=false; o->animTime=o->useTime=0;
      const int idleFirst=PlacedObjects::animationFrame(*o,o->art);
      probe.tick(std::max(1u,o->art->frameTimeMs())*0.0011f);
      clocks+=o->art->frameCount()>1 && PlacedObjects::animationFrame(*o,o->art)==idleFirst;
      for(Animation *a:{o->art,o->used,o->bg}) {
        if(!a)continue;
        o->animTime=o->useTime=0;
        int before=PlacedObjects::animationFrame(*o,a);
        o->inUse=true; probe.tick(0.001f);
        probe.tick(std::max(1u,a->frameTimeMs())*0.0011f);
        int after=PlacedObjects::animationFrame(*o,a);
        bool valid=a->frameCount()<=1 ? after==before : after!=before;
        probe.tick(0);
        valid=valid && after==PlacedObjects::animationFrame(*o,a);
        clocks+=!valid;
      }
      ZooReader::ZooObject record;
      record.className="building"; record.subClass="building"; record.typeName=o->typeName;
      record.x=25*64+32; record.y=25*64+32; record.decoded=true;
      records.push_back(record);
      probe.remove(static_cast<int>(probe.objects().size())-1);
    }
    check("every declared catalogue building animation loads",missing==0);
    check("every building layer advances or stays static as its original art specifies, and pauses",clocks==0);
    PlacedObjects imported; imported.load(fixture,rm);
    int lostLayers=0;
    for(const auto &o:imported.objects())
      lostLayers+=(usedTypes.count(o.typeName) && !o.used)+(bgTypes.count(o.typeName) && !o.bg);
    check("original-map buildings retain used and background layers",lostLayers==0 && imported.objects().size()==records.size());
    report<<"AUDIT "<<drawable<<" drawable buildings; "<<idleAnimated<<" animated idle; "
          <<usedAnimated<<" animated used; "<<background<<" backgrounds; "<<staticOnly<<" static-only\n";

    // Decorative use cycles must run without guests, then return to idle.
    for(const char *file:{"scenery/building/geyser1.ai","scenery/building/geyser2.ai","scenery/building/volcano.ai","scenery/building/volcano2.ai","scenery/other/vent1.ai"}) {
      bool added=probe.add(file,25.5f,25.5f,0);
      auto *o=probe.last(); bool cycle=added && o->used && o->randomUse;
      if(cycle) {
        const double waiting=o->randomWait;
        probe.tick(0); cycle &= !o->randomActive && o->randomWait==waiting;
        probe.tick(static_cast<float>(waiting+0.02));
        cycle &= o->randomActive && PlacedObjects::displayArt(*o)==o->used;
        probe.tick(o->used->frameCount()*std::max(1u,o->used->frameTimeMs())*0.001f*o->useAnimCount);
        cycle &= !o->randomActive && o->randomWait>0 && PlacedObjects::displayArt(*o)==o->art;
      }
      check(std::string(file)+" automatically plays and finishes its use cycle",cycle);
      if(added)probe.remove(static_cast<int>(probe.objects().size())-1);
    }
    const Fences::Edge edge{true,25,25};
    int gateTypes=0, rates=0, endpoints=0, pause=0, rotations=0;
    for(size_t n=0;n<fn.types().size();n++) {
      const auto &type=fn.types()[n];
      Animation *gate=type.art[1][0];
      if(type.tank || type.zooWall || !gate || gate==type.art[0][0])continue;
      fn.place(edge,static_cast<int>(n),0,map);
      auto *p=const_cast<Fences::Piece*>(fn.at(edge));
      if(!p) {++endpoints;continue;}
      p->gate=true; p->open=0;
      ++gateTypes;
      const float last=std::max(0,gate->frameCount()-1);
      fn.updateGates(gate->frameTimeMs()*0.0011f,[](const auto &){return true;});
      rates+=std::abs(p->open-std::min(last,1.1f))>0.02f;
      float partial=p->open;
      fn.updateGates(0,[](const auto &){return false;});
      pause+=p->open!=partial;
      fn.updateGates(100,[](const auto &){return true;});
      endpoints+=std::abs(p->open-last)>0.01f;
      for(int turn=0;turn<4;turn++) {
        std::vector<Fences::Drawable> drawn;
        fn.collect(world.getRenderer(),map,drawn);
        bool seen=false;
        for(const auto &d:drawn)if(d.art==gate) {seen=true;rotations+=d.frame!=static_cast<int>(last);}
        rotations+=!seen;
        world.getRenderer().rotateView(1);
      }
      fn.updateGates(gate->frameTimeMs()*0.0011f,[](const auto &){return false;});
      rates+=std::abs(p->open-std::max(0.0f,last-1.1f))>0.02f;
      fn.updateGates(100,[](const auto &){return false;});
      endpoints+=p->open!=0;
      fn.remove(edge,map);
    }
    check("land gates use their own animation timing in both directions",gateTypes>0 && rates==0);
    check("land gates hold their closed and open endpoints",endpoints==0);
    check("land gates freeze while paused",pause==0);
    check("land gates hold the open frame through all four camera rotations",rotations==0);
    report<<"GATE AUDIT "<<gateTypes<<" gate definitions\n";
    const int heli=st.hire(st.typeOfFile("staff/helicopt.ai"),30.5f,24,map,fn);
    auto *h=st.member(heli);
    Animation *beacons=h?st.baseArt(*h):nullptr;
    check("occupied DRT base uses its original animated beacon artwork",beacons && beacons->frameCount()==9);
    if(h && beacons) {
      h->baseAnimTime=0; st.update(0.2f,map,fn);
      bool advancing=true;
      for(int turn=0;turn<4;turn++) {
        std::vector<Fences::Drawable> drawn; st.collect(world.getRenderer(),map,drawn);
        bool seen=false;
        for(const auto &d:drawn)if(d.art==beacons){seen=true;advancing &= d.frame>0;}
        advancing &= seen; world.getRenderer().rotateView(1);
      }
      check("DRT beacon frames advance through all four map rotations",advancing);
    }
    st.clear();
  }

  // Four tank sides, both staff variants, through a complete real visit.
  int tankType=fn.typeIndex("atltank"), trainerType=st.typeOfFile("staff/trainer.ai");
  check("marine specialist and tank definitions available",tankType>=0 && trainerType>=0);
  if(tankType<0 || trainerType<0)return report.str();
  auto tank=[&](int x,int y) {
    for(int k=0;k<4;k++) {
      fn.place({true,x+k,y},tankType,77,map); fn.place({true,x+k,y+4},tankType,77,map);
      fn.place({false,x,y+k},tankType,77,map); fn.place({false,x+4,y+k},tankType,77,map);
    }
    int id=-1;
    for(int made:fn.updateExhibits(map,1,0,1)) {
      fn.finishExhibit(made,"Ladder animation test",map); id=made;
    }
    return id;
  };
  int tankId=tank(36,30);
  auto *ex=fn.exhibit(tankId);
  check("ladder fixture tank built",ex && ex->tank);
  if(!ex)return report.str();
  ex->water=1; ex->testAnimals=2;
  const Fences::Edge sides[4]={{true,37,30},{false,40,31},{true,37,34},{false,36,31}};
  if(demo) {
    fn.moveGate(sides[0]); ex->purity=20;
    int id=st.hire(trainerType,37.5f,28.5f,map,fn);
    st.assign(id,tankId,fn);
    objects.add("scenery/building/carousal.ai",29,38,4);
    objects.add("scenery/building/restrant.ai",37,41,4);
    objects.add("scenery/building/bgrstnd.ai",27,31,4);
    objects.add("scenery/building/icecstnd.ai",27,35,4);
    objects.add("scenery/building/aviary.ai",26,42,4);
    objects.add("scenery/building/geyser1.ai",44,37,4);
    objects.add("scenery/building/geyser2.ai",45,33,4);
    st.hire(st.typeOfFile("staff/helicopt.ai"),30.5f,24,map,fn);
    world.reindex();
    for(int y=26;y<=45;y++)for(int x=25;x<=46;x++)
      if(fn.exhibitAt(x,y)<0 && world.canLayPath(x,y))map.setPath(x,y,"path");
    world.getGuests().setEntrance(25,28);
    for(int k=0;k<16;k++)world.getGuests().admit(k%4);
    for(auto &g:const_cast<std::vector<Guests::Guest>&>(world.getGuests().guests())) {
      g.hunger=90;g.thirst=60;g.hungerCheck=g.thirstCheck=0;
    }
    world.reindex();world.centreOn(35,34);
    report<<"Live fixture ready: marine specialist, tank ladder, carousel, restaurant and two stands.\n";
    return report.str();
  }
  for(int sex=0;sex<2;sex++)for(int side=0;side<4;side++) {
    st.clear(); world.getItems().removeOn(ex->tiles);
    fn.moveGate(sides[side]); ex->purity=30;
    float x=side==1?40.5f:side==3?35.5f:37.5f;
    float y=side==0?29.5f:side==2?34.5f:31.5f;
    int id=st.hire(trainerType,x,y,map,fn);
    auto *m=st.member(id);
    if(!m) {check("specialist hired for ladder side "+std::to_string(side),false);continue;}
    m->female=sex!=0;
    st.assign(id,tankId,fn);
    bool entered=false,exited=false,faceCorrect=true,frameCorrect=true,climbPause=true;
    std::set<int> phases;
    for(int step=0;step<6000;step++) {
      st.update(0.05f,map,fn);
      m=st.member(id); if(!m)break;
      if(m->climbing) {
        entered=true;
        phases.insert(m->phase);
        // Every actual wall segment faces the rungs on its own side.
        if(m->phase==30 || m->phase==20 || m->phase==26 || m->phase==29) {
          float nx=m->inX-m->outX,ny=m->inY-m->outY;
          float sign=m->climbInside?-1.0f:1.0f;
          faceCorrect &= (m->fx*nx+m->fy*ny)*sign>0.9f;
          // Inspect the real drawable (the tank pass hides inside members).
          auto copy=*m;
          m->tank=-1;
          std::vector<Fences::Drawable> drawn; st.collect(world.getRenderer(),map,drawn);
          Animation *a=st.art(*m,"climb");
          int frame=std::min(static_cast<int>(m->animTime*1000/std::max(1u,a->frameTimeMs())),a->frameCount()-1);
          if(m->climbDown)frame=a->frameCount()-1-frame;
          bool seen=false;
          for(const auto &d:drawn)if(d.art==a){seen=true;frameCorrect &= d.frame==frame;}
          frameCorrect &= seen;
          *m=copy;
        }
        float oldZ=m->z,oldTime=m->animTime;
        st.update(0,map,fn);
        climbPause &= m->z==oldZ && m->animTime==oldTime;
      }
      if(entered && m->tank<0 && !m->climbing && m->job==Staff::Job::None) {exited=true;break;}
    }
    const std::string who=std::string(sex?"female":"male")+" side "+std::to_string(side);
    check(who+" completes entry, submerged work and exit",entered && exited && phases.count(30) && phases.count(20) && phases.count(26) && phases.count(29) && ex->purity>=100);
    check(who+" faces the ladder on ascent and descent",faceCorrect && entered);
    check(who+" plays ascent forward and descent backward",frameCorrect && entered);
    check(who+" pauses during climbing",climbPause && entered);
  }
  st.clear();
  // A show portal uses a different, vertically sliding animation from land gates.
  tankType=fn.typeIndex("tankwal3");
  const int showId=tankType>=0?tank(40,30):-1;
  int portals=0; bool portalTiming=true,portalEnds=true;
  for(const auto &[edge,piece]:fn.pieces()) {
    int show=-1,home=-1;
    const auto *type=fn.portalType(edge);
    if(!type || !fn.showPortal(edge,show,home) || show!=showId || home!=tankId)continue;
    ++portals;
    auto *p=const_cast<Fences::Piece*>(&piece); p->open=0;
    const float last=std::max(0,type->portal->frameCount()-1);
    fn.updateGates(type->portal->frameTimeMs()*0.0011f,[](const auto &){return true;});
    portalTiming &= std::abs(p->open-std::min(last,1.1f))<0.02f;
    fn.updateGates(100,[](const auto &){return true;}); portalEnds &= p->open==last;
    fn.updateGates(type->portal->frameTimeMs()*0.0011f,[](const auto &){return false;});
    portalTiming &= std::abs(p->open-std::max(0.0f,last-1.1f))<0.02f;
    fn.updateGates(100,[](const auto &){return false;}); portalEnds &= p->open==0;
  }
  check("adjacent home and show tanks form a marine portal",portals==1);
  check("marine portal opens and closes at its original frame timing",portals==1 && portalTiming);
  check("marine portal holds its fully open and closed frames",portals==1 && portalEnds);
  world.reindex();world.centreOn(37,31);
  report<<checks-failures<<"/"<<checks<<" checks passed\n";
  return report.str();
}


std::string runParityChecks(World &world, ResourceManager *rm) {
  auto &map=world.getMap(); auto &f=world.getFences(); auto &a=world.getAnimals();
  auto &st=world.getStaff(); auto &g=world.getGuests(); auto &objects=world.objectsMutable();
  if (map.getWidth()<65 || map.getHeight()<65 || !f.exhibits().empty())
    return "Load flat Basic Grass (ff01), with no exhibits, in the Test Zoo first.";
  std::ostringstream out; int passed=0,total=0;
  auto check=[&](const std::string &name,bool ok){++total;passed+=ok;out<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
  a.clear();st.clear();g.clear();
  int drag=200;
  auto rectangle=[&](int x,int y,int w,int h,int type,const std::string &name) {
    for(int k=0;k<w;++k){f.place({true,x+k,y},type,drag,map);f.place({true,x+k,y+h},type,drag+1,map);}
    for(int k=0;k<h;++k){f.place({false,x,y+k},type,drag+2,map);f.place({false,x+w,y+k},type,drag+3,map);}
    drag+=4;
    for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,name,map);
    return f.exhibitAt(x,y);
  };
  const int land=rectangle(26,28,10,10,f.typeIndex("chainlnk"),"Parity paddock");
  check("land fixture built",land>=0);
  if(land<0)return out.str();
  for(int y=24;y<42;++y)for(int x=23;x<40;++x)
    if(f.exhibitAt(x,y)<0 && world.canLayPath(x,y))map.setPath(x,y,"path");
  world.reindex();g.setEntrance(25,26);
  const int allosaur=a.typeOfFile("animals/allo.ai");
  const int croc=a.typeOfFile("animals/swcroc.ai");
  int dino=allosaur;
  if(dino<0)for(size_t t=0;t<a.typeList.size();++t)if(a.typeList[t].dinosaur){dino=static_cast<int>(t);break;}
  check("original dinosaur and crocodile definitions available",dino>=0 && croc>=0);
  if(dino<0 || croc<0)return out.str();
  auto step=[&](Animals::Member &m,const std::vector<Animals::Step> &steps) {
    auto &sets=a.typeList[m.type].sets[m.baby?2:m.female?1:0];
    sets["__parity"]=steps;m.stack={{"__parity",0}};m.stepping=false;
    a.nextStep(m,map,f);sets.erase("__parity");
  };
  Animals::Member actor;actor.id=900001;actor.type=dino;actor.x=30.5f;actor.y=32.5f;actor.exhibit=land;
  step(actor,{{"fplaystopatend",{"stand"}}});
  check("stop-at-end plays one cycle and holds its last frame",actor.mode==Animals::Member::Mode::Once && actor.playFor>0);
  int specials=0,badSpecials=0,unknown=0;
  std::set<std::string> unknownNames;
  for(size_t t=0;t<a.typeList.size();++t)for(int sub=0;sub<3;++sub)
    for(const auto &[name,steps]:a.typeList[t].sets[sub])
      for(const auto &s:steps) {
        if(Animals::behaviorCommand(s.fn)==Animals::Command::Unknown){++unknown;unknownNames.insert(s.fn);}
        if(s.fn!="fplayspecial" && s.fn!="fplaysp")continue;
        Animals::Member m;m.type=static_cast<int>(t);m.female=sub==1;m.baby=sub==2;m.x=30.5f;m.y=32.5f;m.exhibit=land;
        step(m,{s});++specials;
        badSpecials+=m.anim!=s.args[0] || !m.stepping || m.playFor<=0;
      }
  check("all stock special-animation steps start their named animation",specials>0 && badSpecials==0);
  check("remaining raw commands are paired movement/play and the original typo",unknownNames==std::set<std::string>{"ffollow","fplaywith","fplaywithsoud"});
  for(const auto &name:unknownNames)out<<"UNSUPPORTED "<<name<<"\n";
  out<<"CATALOG "<<a.typeList.size()<<" species; "<<specials<<" special-animation steps; "<<unknown<<" raw paired/typo steps\n";
  a.list.push_back(actor);
  Animals::Member prey=actor;prey.id=900002;prey.x=33.5f;prey.y=actor.y;a.list.push_back(prey);
  a.list[0].huntAnimal=prey.id;
  step(a.list[0],{{"ffacetowardtarget",{}},{"fplay",{"stand"}}});
  check("face-target turns toward the actual prey",a.list[0].fx>0.99f && std::abs(a.list[0].fy)<0.01f);
  a.list[0].victim=prey.id;a.list[0].huntAnimal=-1;
  step(a.list[0],{{"fcaughtprey",{}},{"fcaughtprey",{}},{"fplay",{"stand"}}});
  check("caught-prey removal is queued exactly once",std::count(a.kills.begin(),a.kills.end(),prey.id)==1 && a.list[0].victim<0);
  a.clear();
  // A reachable target must survive while the animal is far away, then
  // become rubble after the approach, rather than being destroyed remotely.
  const bool targetAdded=objects.add("scenery/building/bgrstnd.ai",32.5f,34.5f,4);
  const int targetId=targetAdded?objects.last()->id:-1;
  actor.x=28.5f;actor.y=34.5f;actor.rampaging=true;actor.health=static_cast<float>(a.typeList[dino].maxHits);
  actor.happiness=-100;actor.pending=0;actor.energy=0;
  actor.huntAnimal=actor.victim=-1;actor.stack={{"__rampage",0}};actor.stepping=false;
  a.typeList[dino].sets[0]["__rampage"]={{"ftargetbuilding",{}},{"fplay",{"stand"}}};
  a.list.push_back(actor);world.reindex();a.nextStep(a.list[0],map,f);
  check("rampaging dinosaur routes before damaging its target",targetAdded && objects.byId(targetId) && !a.list[0].path.empty());
  for(int k=0;k<1200 && objects.byId(targetId);++k)a.update(0.05f,k*0.05,map,f);
  check("rampaging dinosaur reaches and destroys the building",targetAdded && !objects.byId(targetId));
  if(objects.byId(targetId))out<<"RAMPAGE "<<a.typeList[dino].file<<" at "<<a.list[0].x<<","<<a.list[0].y<<" path "<<a.list[0].pathAt<<"/"<<a.list[0].path.size()<<" active "<<a.list[0].rampaging<<" stack "<<a.list[0].stack.size()<<" animation "<<a.list[0].anim<<"\n";
  a.typeList[dino].sets[0].erase("__rampage");a.clear();world.reindex();
  Animals::Member egg;egg.type=dino;egg.exhibit=land;egg.x=30.5f;egg.y=32.5f;
  a.becomeEgg(egg);egg.hatchIn=1;egg.health=0;a.eggTick(egg);
  check("unhealthy dinosaur egg waits to hatch",egg.egg && egg.stack.empty() && egg.hatchIn==1);
  egg.health=static_cast<float>(a.typeList[dino].maxHits);a.eggTick(egg);
  check("healthy egg starts its original hatching sequence",!egg.egg || (!egg.stack.empty() && egg.stack.back().first=="bhatch"));
  step(egg,{{"fhatch",{}},{"fplay",{"stand"}}});
  check("hatching creates a live baby",!egg.egg && egg.baby);

  // Tour recruitment, movement, both talks, effects, interruption and removal.
  const int guideType=st.typeOfFile("staff/tour.ai");
  const int guide=st.hire(guideType,25.5f,30.5f,map,f);
  auto *leader=st.member(guide);
  check("tour guide hired",leader!=nullptr);
  if(leader) {
    for(int i=0;i<20;++i)g.admit(0);
    for(auto &guest:g.list){guest.x=25.5f;guest.y=30.5f;guest.hunger=guest.thirst=guest.bathroom=guest.tired=0;guest.state=Guests::State::Walking;}
    leader->visit=land;leader->goalX=25;leader->goalY=31;leader->job=Staff::Job::Tour;
    auto &type=st.typeList[guideType];const auto originalType=type;type.followChance=100;
    st.gatherFollowers(*leader);
    check("tour group respects its configured cap",leader->followers.size()==static_cast<size_t>(type.maxGroup));
    if(!g.list.empty()) {
      auto &guest=g.list.back();guest.guide=-1;guest.hunger=80;
      check("hungry guest declines tour",!g.joinTour(guest.id,guide,land,25,31));
      guest.hunger=0;guest.leaving=true;
      check("leaving guest declines tour",!g.joinTour(guest.id,guide,land,25,31));
      guest.leaving=false;guest.showTank=123;
      check("show spectator declines conflicting tour",!g.joinTour(guest.id,guide,land,25,31));guest.showTank=-1;
    }
    bool arrivedAtView=false;
    for(int k=0;k<300 && !arrivedAtView;++k) {
      g.update(0.05f,0,1000);
      arrivedAtView=std::any_of(g.list.begin(),g.list.end(),[&](const auto &v){return v.guide==guide && v.state==Guests::State::Gawking;});
    }
    check("tour follower walks its route and reaches the viewing area",arrivedAtView);
    for(auto &guest:g.list)if(guest.guide==guide){guest.state=Guests::State::Gawking;guest.dHappy=0;}
    g.tourTalk(guide,type.tourBonus);g.tourTalk(guide,type.tourBonus);
    bool once=true;for(const auto &guest:g.list)if(guest.guide==guide)once &= guest.dHappy==type.tourBonus;
    check("each follower receives one talk benefit",once);
    leader->path.clear();leader->speaking=true;leader->speakLeft=2.0f*type.informTime;
    st.update(0.05f,map,f);check("first talk uses speak animation",leader->anim=="speak");
    st.update(static_cast<float>(type.informTime),map,f);check("second talk uses speak2 animation",leader->anim=="speak2");
    st.update(static_cast<float>(type.informTime)+0.1f,map,f);
    check("finished tour releases every follower",leader->followers.empty() && std::none_of(g.list.begin(),g.list.end(),[&](const auto &v){return v.guide==guide;}));
    g.list[0].guide=guide;leader->followers={g.list[0].id};st.fire(guide);
    check("removing guide releases its group",g.list[0].guide<0);
    type=originalType;
  }
  // Native need-choice and use loop, including the path, price and benefit.
  st.clear();g.clear();
  const bool shopAdded=objects.add("scenery/building/bgrstnd.ai",24.5f,25.5f,4);
  auto *shop=shopAdded?objects.last():nullptr;
  if(!shop || !g.infoOf(PlacedObjects::fileOf(*shop)).satisfies.count("food")) {
    if(shop)objects.remove(static_cast<int>(objects.objects().size())-1);
    const bool added=objects.add("scenery/building/dksnacks.ai",24.5f,25.5f,4);shop=added?objects.last():nullptr;
  }
  check("food stand definition available",shop && g.infoOf(PlacedObjects::fileOf(*shop)).satisfies.count("food"));
  if(shop && g.infoOf(PlacedObjects::fileOf(*shop)).satisfies.count("food")) {
    world.reindex();g.setEntrance(25,26);g.admit(0);auto &guest=g.list.front();
    guest.state=Guests::State::Walking;guest.hunger=90;guest.thirst=guest.tired=guest.bathroom=0;
    guest.guide=123;
    const bool found=g.seekBuilding(guest,"food",1000);
    check("hungry guest routes to an accessible food stand",found && guest.building>=0 && !guest.path.empty());
    check("seeking food detaches a tour follower",!found || guest.guide<0);
    bool used=false;
    for(int k=0;k<1600 && guest.hunger>=90;++k){g.update(0.05f,0,1000);used |= guest.state==Guests::State::Using;}
    check("guest walks, buys food and reduces hunger",used && guest.hunger<90 && shop->income>0);
  }
  // A bank-level pool inside a fenced marine habitat: use the actual
  // animal routes across the shore in both directions.
  a.clear();st.clear();g.clear();
  const int shore=rectangle(42,25,20,18,f.typeIndex("chainlnk"),"Walrus shore");
  const int pool=rectangle(48,30,8,8,f.typeIndex("atltank"),"Walrus pool");
  auto *water=f.exhibit(pool);if(water)water->water=1;
  check("sunken pool shares its enclosing land habitat",water && f.habitatRoot(pool,map)==shore);
  world.reindex();
  const int walrusType=a.typeOfFile("animals/walrus.ai");
  const int walrus=a.adopt(walrusType,false,46.5f,34.5f,map,f);
  check("amphibious marine animal can be adopted on its shore",walrus>=0);
  if(auto *w=a.member(walrus)) {
    w->waterCheck=9999;w->boredCheck=9999;
    const bool into=a.walkTo(*w,51.5f,34.5f,map,f);
    for(int k=0;k<500 && !w->path.empty();++k)a.update(0.05f,k*0.05,map,f);
    check("walrus completes its land-to-pool route",into && f.exhibitAt(static_cast<int>(w->x),static_cast<int>(w->y))==pool);
    const bool back=a.walkTo(*w,46.5f,34.5f,map,f);
    for(int k=0;k<500 && !w->path.empty();++k)a.update(0.05f,k*0.05,map,f);
    check("walrus completes its pool-to-land route",back && f.exhibitAt(static_cast<int>(w->x),static_cast<int>(w->y))==shore);
  }
  // A free destination before another swimmer must not be avoided because
  // prediction incorrectly continues beyond the end of the route.
  a.clear();
  const int swimTank=rectangle(25,45,30,18,f.typeIndex("atltank"),"Steering regression");
  if(auto *tank=f.exhibit(swimTank))tank->water=12;
  world.reindex();
  const int shark=a.typeOfFile("animals/whleshrk.ai");
  check("whale shark steering fixture available",shark>=0 && swimTank>=0);
  out<<"STEERING fixture type="<<shark<<" tank="<<swimTank<<"\n";
  if(shark>=0 && swimTank>=0) {
    Animals::Member swimmer;swimmer.id=910001;swimmer.type=shark;
    swimmer.exhibit=swimTank;swimmer.x=34;swimmer.y=54;swimmer.swimZ=swimmer.swimTo=3;
    swimmer.fx=1;swimmer.fy=0;swimmer.speed=4;
    const float target=swimmer.x+3;
    Animals::Member obstacle=swimmer;obstacle.id=910002;
    obstacle.x=target+2*a.bodyRadius(swimmer)+0.13f;obstacle.speed=0;
    a.list={swimmer,obstacle};a.list[0].path={{target,54}};world.reindex();
    bool reached=false;int reversals=0;float previousTurn=0;
    for(int k=0;k<600 && !reached;++k) {
      auto &m=a.list[0];const float oldX=m.fx,oldY=m.fy;
      reached=a.moveAware(m,target,54,1.0f/60,map,f);
      const float turn=oldX*m.fy-oldY*m.fx;
      if(std::abs(turn)>0.2f && turn*previousTurn<0)++reversals;
      if(std::abs(turn)>0.2f)previousTurn=turn;
      world.reindex();
    }
    check("swimmer reaches free route endpoint before nearby animal",reached);
    check("swimmer avoids rapid alternating steering at route endpoint",reversals<4);
    out<<"STEERING reached="<<reached<<" reversals="<<reversals
       <<" x="<<a.list[0].x<<" y="<<a.list[0].y<<"\\n";
    check("swimmer retains body and tank wall clearance",
      a.destinationFree(a.list[0],a.list[0].x,a.list[0].y,a.list[0].swimZ,false) &&
      a.tankClearance(a.list[0],a.list[0].x,a.list[0].y,f)>=-0.001f);
  }
  a.clear();world.reindex();
  Guests::Guest arrived;arrived.arrived=0;
  for(const auto &[days,text]:std::vector<std::pair<double,int>>{{0,3420},{59.9,3420},{60,3421},{239.9,3421},{240,3422},{359.9,3422},{360,3423}}) {
    g.clock=days*ZooSim::kSecondsPerDay;check("original time-in-park boundary "+std::to_string(days),g.timeInParkText(arrived)==text);
  }
  a.clear();st.clear();g.clear();world.reindex();world.centreOn(31,32);
  out<<passed<<"/"<<total<<" parity checks passed\n";return out.str();
}
