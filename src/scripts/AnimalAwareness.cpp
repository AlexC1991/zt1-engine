// Run from Write Code in the ZT1 Lab. Uses the live Test Zoo, never a second client.
// Prepare one land exhibit and one non-show tank, each at least 900 tiles.
// This is an AI stress fixture: restore-adoption deliberately creates overlapping
// adults (including dinosaurs), then ordinary World::update must recover.
#include "../EngineScripts.hpp"
#include "../World.hpp"
#include "../SaveGame.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
struct Sweep {
  bool running=false, spawned=false, reviewOnly=false;
  int type=0, land=-1, tank=-1, passed=0, failed=0;
  float age=0, minClear=1000, minWall=1000, travel[2]={0,0}, minHunger[2]={100,100};
  bool overlaps=false, walls=false;
  std::array<int,2> ids{};
  std::array<std::pair<float,float>,2> last{};
  std::string root="E:/zt1-shots/ai-awareness-2026-10-27";
} sweep;
float radius(const Animals::Type &t) {return std::max(0.22f,std::max(t.footX,t.footY)*0.25f);}
float wallClear(const Zoo &z,const Animals::Member &m) {
  int tank=z.fences.exhibitAt(static_cast<int>(std::floor(m.x)),static_cast<int>(std::floor(m.y)));
  const auto *ex=z.fences.exhibit(tank);
  if(!ex || !ex->tank)return 1000;
  const auto &t=z.animals.types()[m.type];
  float r=std::max(0.25f,std::hypot(static_cast<float>(t.footX),static_cast<float>(t.footY))*0.25f)+0.12f;
  float result=1000;
  for(const auto &[e,p]:z.fences.pieces()) {
    if(p.tank!=tank && p.tank2!=tank)continue;
    float x=e.alongX?std::clamp(m.x,static_cast<float>(e.x),e.x+1.0f):static_cast<float>(e.x);
    float y=e.alongX?static_cast<float>(e.y):std::clamp(m.y,static_cast<float>(e.y),e.y+1.0f);
    result=std::min(result,std::hypot(m.x-x,m.y-y)-r);
  }
  return result;
}
void backup(Zoo &z) {
  std::filesystem::create_directories(sweep.root);
  z.say(SaveGame::saveMap(sweep.root+"/marine-show-fixture.ztmap",z.world,"maps/sm_aqua/sm_aqua.zoo")
    ?"Marine-show map geometry preserved before catalogue testing.":"Could not preserve the fixture.");
}
void restoreShow(Zoo &z) {
  sweep.running=false;
  std::ifstream in(sweep.root+"/marine-show-fixture.ztmap",std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
  z.say(!data.empty() && SaveGame::applyMap(data,z.world)?"Preserved marine-show map geometry restored; map designs do not store animals or staff.":"Could not restore the marine-show fixture.");
}
void start(Zoo &z) {
  Sweep fresh;
  for(const auto &e:z.fences.exhibits())if(e.named && e.tiles.size()>=900) {
    if(e.tank && !e.show)fresh.tank=e.id;
    else if(!e.tank)fresh.land=e.id;
  }
  if(fresh.land<0 || fresh.tank<0) {z.say("Prepare land and non-show tank exhibits of at least 900 tiles each, then run again.");return;}
  std::filesystem::create_directories(fresh.root);
  if(!SaveGame::saveMap(fresh.root+"/before-sweep.ztmap",z.world,"maps/ff03/ff03.zoo")) {
    z.say("Could not preserve the Test Zoo; sweep not started.");return;
  }
  sweep=fresh;sweep.running=true;
  z.world.getShows().clear();
  z.animals.clear();z.world.getItems().clear();
  if(std::filesystem::exists(sweep.root+"/catalogue-awareness.csv"))
    std::filesystem::copy_file(sweep.root+"/catalogue-awareness.csv",
      sweep.root+"/catalogue-awareness-previous.csv",std::filesystem::copy_options::overwrite_existing);
  std::ofstream out(sweep.root+"/catalogue-awareness.csv");
  out<<"species,category,overlap_after_recovery,wall_intrusion,min_clearance,min_wall,travel_1,travel_2,min_hunger_1,min_hunger_2,result\n";
  EngineScripts::switchedOn().insert("awareness_sweep_tick");
  z.say("Catalogue awareness sweep started in this Test Zoo; backup and CSV: "+sweep.root);
}
void review(Zoo &z) { start(z);sweep.reviewOnly=true; }
void tick(Zoo &z) {
  if(!sweep.running)return;
  if(sweep.reviewOnly) {
    static const std::array<std::string,8> files={"animals/tritops.ai","animals/trex.ai","animals/testosau.uca","animals/apato.ai","animals/plateo.ai","animals/lambeo.ai","animals/allo.ai","animals/mmaid.ai"};
    while(sweep.type<static_cast<int>(z.animals.types().size()) && std::find(files.begin(),files.end(),z.animals.types()[sweep.type].file)==files.end())++sweep.type;
  }
  if(sweep.type>=static_cast<int>(z.animals.types().size())) {
    sweep.running=false;
    z.say("Catalogue sweep complete: "+num(sweep.passed,0)+" passed, "+num(sweep.failed,0)+" need review. CSV: "+sweep.root+"/catalogue-awareness.csv");
    return;
  }
  const auto &t=z.animals.types()[sweep.type];
  const float feedUntil=radius(t)>=3.0f?150.0f:75.0f;
  const float finishAt=feedUntil+20.0f;
  if(!sweep.spawned) {
    z.animals.clear();z.world.getItems().clear();
    int habitat=t.underwater?sweep.tank:sweep.land;
    auto *e=z.fences.exhibit(habitat);
    if(e->tank){e->purity=100;e->purityClock=0;}
    auto tile=*std::next(e->tiles.begin(),e->tiles.size()/2);
    // A centre that gives the largest bodies room in every direction.
    float x=0,y=0;for(auto p:e->tiles){x+=p.first+0.5f;y+=p.second+0.5f;}
    x/=e->tiles.size();y/=e->tiles.size();
    for(int i=0;i<2;++i) {
      int id=z.animals.adopt(sweep.type,false,x,y,z.map,z.fences,true);
      sweep.ids[i]=id;
      if(auto *a=z.animals.member(id)) {
        a->hunger=100;a->food=0;a->energy=0;a->baby=a->egg=false;
        a->swimZ=a->swimTo=t.underwater?std::max(0,z.fences.waterLevel(*e)-2):0;
        sweep.last[i]={a->x,a->y};
      }
    }
    static const char *food[]={"herbchow","carnchow","fruichow","bambchow","graschow","fishchow","inschow","dinocarn","dinogras","krill","fish","chum","seagrass","urchin"};
    z.world.getItems().addFood(habitat,x+std::max(4.0f,radius(t)*2+1),y,2,
      t.customFood.empty()?food[std::clamp(t.keeperFood,0,13)]:t.customFood);
    sweep.spawned=true;sweep.age=0;sweep.minClear=sweep.minWall=1000;
    sweep.travel[0]=sweep.travel[1]=0;sweep.minHunger[0]=sweep.minHunger[1]=100;
    sweep.overlaps=sweep.walls=false;
    z.world.centreOn(x,y);
    z.say("Testing "+t.file+" (two overlapping adults, feeding, then wandering).");
    return;
  }
  sweep.age+=z.dt;
  if(sweep.reviewOnly && (std::floor(sweep.age/15)>std::floor((sweep.age-z.dt)/15)))z.say(num(sweep.age,1)+"s "+z.animals.awarenessReport());
  auto *a=z.animals.member(sweep.ids[0]),*b=z.animals.member(sweep.ids[1]);
  if(!a || !b){sweep.overlaps=true;sweep.age=finishAt;}
  else {
    for(int i=0;i<2;++i) {
      const auto *m=i?b:a;
      sweep.travel[i]+=std::hypot(m->x-sweep.last[i].first,m->y-sweep.last[i].second);
      sweep.last[i]={m->x,m->y};sweep.minHunger[i]=std::min(sweep.minHunger[i],m->hunger);
      if(sweep.age>=15) {
        float wall=wallClear(z,*m);sweep.minWall=std::min(sweep.minWall,wall);
        if(wall<-0.01f)sweep.walls=true;
      }
    }
    if(sweep.age>=15 && a->victim<0 && b->victim<0 && a->caughtBy<0 && b->caughtBy<0) {
      float r=radius(t)*2+0.08f,vertical=std::max(0.65f,r*0.6f);
      float d=std::sqrt(std::pow((a->x-b->x)/r,2)+std::pow((a->y-b->y)/r,2)+std::pow((a->swimZ-b->swimZ)/vertical,2));
      sweep.minClear=std::min(sweep.minClear,d);
      if(d<0.99f)sweep.overlaps=true;
    }
    if(sweep.age<feedUntil && sweep.age+z.dt>=feedUntil) {
      z.world.getItems().removeIn(a->exhibit,ZooItems::Kind::Food);
      z.animals.labBehave(a->id,"bwalk");z.animals.labBehave(b->id,"bwalk");
    }
  }
  if(sweep.age<finishAt)return;
  bool moved=sweep.travel[0]>radius(t) && sweep.travel[1]>radius(t);
  bool fed=sweep.minHunger[0]<99 && sweep.minHunger[1]<99;
  bool pass=!sweep.overlaps && !sweep.walls && moved && fed;
  (pass?sweep.passed:sweep.failed)++;
  std::ostringstream row;row<<std::fixed<<std::setprecision(4)<<t.file<<","<<(t.underwater?"marine":t.dinosaur?"dinosaur":"zoo")
    <<","<<sweep.overlaps<<","<<sweep.walls<<","<<sweep.minClear<<","<<sweep.minWall
    <<","<<sweep.travel[0]<<","<<sweep.travel[1]<<","<<sweep.minHunger[0]<<","<<sweep.minHunger[1]<<","<<(pass?"PASS":"REVIEW");
  std::ofstream out(sweep.root+"/catalogue-awareness.csv",std::ios::app);out<<row.str()<<"\n";
  z.say(row.str());
  ++sweep.type;sweep.spawned=false;
}
void status(Zoo &z) {
  z.say("Sweep "+num(sweep.type,0)+"/"+num(z.animals.types().size(),0)+": "+num(sweep.passed,0)+" passed, "+num(sweep.failed,0)+" review; "+(sweep.running?"running":"stopped"));
  if(!sweep.running) {
    std::ifstream in(sweep.root+"/catalogue-awareness.csv");std::string row;
    while(std::getline(in,row))if(row.find(",REVIEW")!=std::string::npos)z.say(row);
  }
}
}
ZT_SCRIPT("awareness_backup","Preserve the marine-show Test Zoo before broad testing",EngineScripts::Once,backup);
ZT_SCRIPT("awareness_restore_show","Restore preserved marine-show map geometry (animals and staff are separate)",EngineScripts::Once,restoreShow);
ZT_SCRIPT("animal_awareness","Start the catalogue awareness stress test in the Lab",EngineScripts::Once,start);
ZT_SCRIPT("awareness_review","Retest the eight catalogue failures in the Lab",EngineScripts::Once,review);
ZT_SCRIPT("awareness_sweep_status","Report catalogue test progress",EngineScripts::Once,status);
ZT_SCRIPT("awareness_sweep_tick","Catalogue awareness stress test running in this Test Zoo",EngineScripts::EveryTick,tick);
