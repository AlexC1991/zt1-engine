#include "World.hpp"
#include "Animation.hpp"
#include "ItemCatalog.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

// Explicit Lab-only fixture; the console refuses it while a player HUD exists.
// Adults are restored as an escaped-save fixture, rather than adopted as eggs.
std::string runBuildingPolishChecks(World &world, bool demo) {
  std::ostringstream report;
  int checks=0, failures=0;
  auto check=[&](const std::string &name,bool pass) {
    ++checks;failures+=!pass;report<<(pass?"PASS ":"FAIL ")<<name<<"\n";
    SDL_Log("[BuildingPolish] %s %s",pass?"PASS":"FAIL",name.c_str());
  };
  auto &map=world.getMap();auto &fences=world.getFences();
  auto &animals=world.getAnimals();auto &staff=world.getStaff();
  auto &objects=world.objectsMutable();auto &view=world.getRenderer();
  if(map.getWidth()<55 || map.getHeight()<55)return "Load flat Basic Grass (ff01) in the Test Zoo first.";
  staff.clear();animals.clear();
  auto &list=objects.all();
  list.erase(std::remove_if(list.begin(),list.end(),[](const auto &o){return o.bought;}),list.end());
  world.reindex();
  const int heliType=staff.typeOfFile("staff/helicopt.ai");
  const int keeperType=staff.typeOfFile("staff/keeper.ai");
  const int scientistType=staff.typeOfFile("staff/scient.ai");
  int dinoType=-1;
  for(size_t n=0;n<animals.types().size();n++)
    if(animals.types()[n].dinosaur && !animals.types()[n].underwater && !animals.types()[n].eatsPeople){dinoType=static_cast<int>(n);break;}
  check("stock staff and land dinosaur definitions available",heliType>=0 && keeperType>=0 && scientistType>=0 && dinoType>=0);
  if(heliType<0 || dinoType<0)return report.str();
  auto adult=[&](int type,float x,float y) {
    const int id=animals.adopt(type,false,x,y,map,fences,true);
    if(auto *a=animals.member(id)){a->escaped=true;a->egg=false;a->baby=false;}
    return id;
  };
  int animal=adult(dinoType,36.5f,28.5f);
  int keeper=staff.hire(keeperType,34.5f,29.5f,map,fences);
  int scientist=staff.hire(scientistType,34.5f,31.5f,map,fences);
  auto runStaff=[&](float seconds){for(int n=0;n<static_cast<int>(seconds*20);n++)staff.update(0.05f,map,fences);};
  if(!demo) {
    runStaff(8);
    check("keeper and scientist do not shoot an awake escaped dinosaur",
          animals.member(animal) && !animals.member(animal)->tranquilised && !animals.member(animal)->boxed &&
          staff.member(keeper) && staff.member(keeper)->job!=Staff::Job::Catch &&
          staff.member(scientist) && staff.member(scientist)->job!=Staff::Job::Catch);
  }
  int heli=staff.hire(heliType,25.35f,25.7f,map,fences);
  check("DRT hires on a clear full footprint",staff.member(heli)!=nullptr);
  if(!staff.member(heli))return report.str();
  const float baseX=staff.member(heli)->x,baseY=staff.member(heli)->y;
  const auto &type=staff.types()[heliType];
  check("DRT footprint edges lie exactly on grid",
        std::abs((baseX-type.footX*0.5f)-std::round(baseX-type.footX*0.5f))<0.001f &&
        std::abs((baseY-type.footY*0.5f)-std::round(baseY-type.footY*0.5f))<0.001f);
  check("overlapping second DRT is refused",staff.canPlaceType(heliType,baseX,baseY,map,fences)!=Fences::Fit::Ok);
  world.reindex();
  if(!demo) {
    std::set<Staff::HeliStage> stages;bool asleep=false,baseFixed=true;
    for(int n=0;n<1600;n++) {
      staff.update(0.05f,map,fences);
      const auto *h=staff.member(heli);const auto *a=animals.member(animal);
      stages.insert(h->heliStage);asleep|=a && a->tranquilised;
      baseFixed&=std::abs(h->x-baseX)<0.001f && std::abs(h->y-baseY)<0.001f;
    }
    check("helicopter plays spin-up, ascent, chase and firing",
          stages.count(Staff::HeliStage::SpinUp) && stages.count(Staff::HeliStage::Ascend) &&
          stages.count(Staff::HeliStage::Chase) && stages.count(Staff::HeliStage::Fire));
    check("helicopter darts the dinosaur",asleep);
    check("scientist contains the darted dinosaur",animals.member(animal) && animals.member(animal)->boxed);
    check("helicopter returns, descends, spins down and parks",
          stages.count(Staff::HeliStage::Return) && stages.count(Staff::HeliStage::Descend) &&
          stages.count(Staff::HeliStage::SpinDown) && staff.member(heli)->heliStage==Staff::HeliStage::Parked);
    check("base anchor stays fixed throughout mission",baseFixed);
    check("rotor loop stops on landing",staff.member(heli)->rotorChannel<0);
    bool allArt=true;
    for(const char *name:{"idle","warmup","level","fly","fire"})allArt&=staff.art(*staff.member(heli),name)!=nullptr;
    check("every flight animation has original artwork",allArt);
    const int rotation=view.getViewRotation();
    std::set<CompassDirection> sides;bool rotationCorrect=true,overRoof=true;
    for(int n=0;n<4;n++) {
      std::vector<Fences::Drawable> drawn;staff.collect(view,map,drawn);
      float front=-1e30f,air=-1e30f;
      for(const auto &d:drawn) {
        if(d.art==staff.baseArt(*staff.member(heli))){sides.insert(d.side);rotationCorrect&=d.side==view.screenSide(0,-1);front=std::max(front,d.depth);}
        if(d.art==staff.art(*staff.member(heli),"idle"))air=std::max(air,d.depth);
      }
      overRoof&=air>front;view.rotateView(1);
    }
    check("base follows the world correctly in all four camera rotations",rotationCorrect && sides.size()==4 && view.getViewRotation()==rotation);
    check("parked helicopter sorts above its roof in every rotation",overRoof);
    const int cancelled=adult(dinoType,38.5f,28.5f);
    runStaff(2);animals.remove(cancelled);runStaff(15);
    check("removed target cancels safely and releases helicopter",staff.member(heli)->heliStage==Staff::HeliStage::Parked && staff.member(heli)->jobAnimal<0);
    int zebraType=animals.typeOfFile("animals/zebra.ai");
    int zebra=adult(zebraType,staff.member(keeper)->x+1.5f,staff.member(keeper)->y);
    runStaff(50);
    check("keeper still captures ordinary escaped zoo animals",animals.member(zebra) && animals.member(zebra)->boxed);
  }
  // Audit every installed stock building's real idle/used assets, not just
  // the sampled stores. Static buildings legitimately have only one frame.
  if(!demo) {
    int buildingCount=0,animated=0,used=0;
    for(const auto &item:ItemCatalog::get().all()) {
      if(item.file.rfind("scenery/building/",0)!=0)continue;
      if(objects.add(item.file,48.5f,48.5f,0)) {
        const auto *o=objects.last();buildingCount++;
        animated+=o->art && o->art->frameCount()>1;used+=o->used && o->used->frameCount()>1;
        objects.remove(static_cast<int>(objects.objects().size())-1);
      }
    }
    report<<"BUILDING AUDIT "<<buildingCount<<" drawable definitions; "<<animated<<" animated idle; "<<used<<" animated used\n";
    check("building audit covers stock zoo and expansion art",buildingCount>50 && animated>10 && used>10);
  }
  if (demo) { map.setPath(29,35,"road"); world.getGuests().setEntrance(29,35); }
  const char *shops[]={"bgrstnd","hdogstnd","drkstnd","pizzstnd","restrant","dbrgstnd","ddogstnd","ddnkstnd","dpzzastd"};
  for(int n=0;n<9;n++) {
    std::string file=std::string("scenery/building/")+shops[n]+".ai";
    if(!objects.add(file,30.5f+(n%3)*5,36.5f+(n/3)*5,4)) {check(std::string(shops[n])+" placed",false);continue;}
    auto *o=objects.last();
    if(!demo) {
      const int idleFrames=o->art?o->art->frameCount():0;
      o->animTime=0;int first=PlacedObjects::animationFrame(*o,o->art);
      objects.tick(0.2f);int next=PlacedObjects::animationFrame(*o,o->art);
      check(std::string(shops[n])+" idle follows original animated/static art",idleFrames>0 && (idleFrames==1 ? next==first : next!=first));
      objects.tick(0);check(std::string(shops[n])+" zero game time freezes animation",next==PlacedObjects::animationFrame(*o,o->art));
      o->inUse=true;objects.tick(0.1f);objects.tick(0.2f);
      check(std::string(shops[n])+" customer-use animation advances",o->used && o->used->frameCount()>1 && PlacedObjects::animationFrame(*o,o->used)!=0);
      o->inUse=false;
    }
    const auto doors=world.getGuests().doorTiles(*o);
    for(auto [x,y]:doors) {
      map.setPath(x,y,"road");
      for(int xx=29;xx<=x;xx++)map.setPath(xx,y,"road");
      for(int yy=33;yy<=y;yy++)map.setPath(29,yy,"road");
    }
    if(demo && !doors.empty()) {
      auto &gs=world.getGuests();gs.admit(0);
      auto &guests=const_cast<std::vector<Guests::Guest>&>(gs.guests());
      if(!guests.empty()) {
        auto &g=guests.back();g.x=doors.front().first+0.5f;g.y=doors.front().second+0.5f;
        g.state=Guests::State::Walking;g.hunger=90;g.thirst=80;g.bathroom=g.tired=0;g.leaving=false;
        g.hungerCheck=g.thirstCheck=0;g.leaveCheck=g.viewCheck=120;
      }
    }
  }
  world.reindex();world.centreOn(31,31);
  report<<checks-failures<<"/"<<checks<<" checks passed\n";
  if(demo)report<<"Live fixture ready: escaped adult dinosaur, keeper, scientist, DRT and nine stores. Advance Test Zoo time to watch.\n";
  return report.str();
}

