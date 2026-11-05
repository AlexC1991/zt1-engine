#include "World.hpp"
#include "Features.hpp"
#include "ItemCatalog.hpp"
#include "LightingSun.hpp"
#include "LightingCache.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <functional>
#include <limits>
// addition: independent geometric expectations followed by real-art scenes.
// Test zoos are disposable; the player's save is never changed.
int runLightingShadowChecks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out,const std::function<void(const char*)> &capture) {
  using namespace LightingAssets;std::ostringstream log;int failed=0;
  auto check=[&](const char *name,bool ok){log<<(ok?"PASS ":"FAIL ")<<name<<"\n";failed+=!ok;};
  {
    Model m;m.minimum={-1,-1,0};m.step={1,1,1};m.nx=m.ny=m.nz=1;m.solid={1};m.occupied=1;
    m.faces.push_back({{Point{0,0,0},Point{1,0,0},Point{1,1,0},Point{0,1,0}},{0,0,1},{0,0,1}});
    auto bytes=encodeModelCache(123,m);Model restored;
    check("compressed geometry cache round-trips the actual solid and surface",!bytes.empty() && decodeModelCache(123,bytes,restored) && restored.solid==m.solid && restored.faces.size()==1 && restored.faces[0].p[2].x==1);
    check("different source-shape key rejects stale cached geometry",!decodeModelCache(124,bytes,restored));
    auto broken=bytes;broken.back()^=127;
    check("corrupt compressed geometry is rejected safely",!decodeModelCache(123,broken,restored));
    broken=bytes;broken.resize(12);
    check("truncated cache cannot replace a valid model",!decodeModelCache(123,broken,restored) && restored.faces.size()==1);
    broken=bytes;std::fill(broken.begin()+20,broken.begin()+24,255);
    check("oversized cache allocation is rejected before decompression",!decodeModelCache(123,broken,restored));
    m.step.x=std::numeric_limits<float>::quiet_NaN();check("non-finite geometry is never cached",encodeModelCache(123,m).empty());
  }
  SunDepth depth;depth.reset(256,-4,-4,16,12,.5f,0);
  depth.caster=1;depth.quad({Point{0,0,2},Point{1,0,2},Point{1,1,2},Point{0,1,2}});
  check("roof casts a displaced shadow onto lower ground",depth.shadow({1.5f,.5f,0})>.9f);
  check("ground outside the projected roof stays lit",depth.shadow({-.5f,.5f,0})==0);
  check("a receiver at roof height stays lit",depth.shadow({.5f,.5f,2})==0);
  check("a lower object receives a taller object's shadow",depth.shadow({1.2f,.5f,1},{-1,0,0},2)>.9f);
  check("coarse surface edges do not shadow their own smooth artwork",depth.shadow({1.2f,.5f,1},{-1,0,0},1)==0);
  depth.seal();depth.beginActors();depth.actorRegion({3,0,0},{4,1,1});depth.caster=3;
  depth.quad({Point{3,0,1},Point{4,0,1},Point{4,1,1},Point{3,1,1}});
  check("moving actor adds its own current shadow",depth.shadow({3.8f,.5f,0})>.9f);
  check("ground below an actor stays eligible for current-pose shadow sampling",depth.actorNear({3.8f,.5f,0}));
  check("low ground actor cannot invalidate a taller roof's cached shadow",!depth.actorNear({3.8f,.5f,3}));
  bool parity=true;int skipped=0;
  for(float z:{0.0f,.5f,1.0f,3.0f})for(int y=-8;y<24;++y)for(int x=-8;x<32;++x) {
    Point p{x*.25f,y*.25f,z};if(!depth.actorNear(p)){++skipped;parity=parity && depth.shadow(p)==depth.shadow(p,{0,0,1},0xFFFFFFFFu,true);}
  }
  check("height-aware pruning matches complete depth sampling at all skipped receivers",parity && skipped>1000);
  depth.actorRegion({2,0,4},{3,1,5});depth.caster=4;depth.quad({Point{2,0,5},Point{3,0,5},Point{3,1,5},Point{2,1,5}});
  check("airborne actor still casts onto a building roof",depth.actorNear({3.5f,.5f,3}) && depth.shadow({3.5f,.5f,3})>.9f);
  // addition: fine actor tracking may skip only queries whose complete PCF
  // footprint equals the sealed static map. Probe fractional texel borders,
  // oblique suns, raised receivers and a caster hidden behind a static roof.
  bool fineParity=true;int fineSkips=0,coarseHits=0;
  for(float dx:{-1.7f,-.35f,0.0f,.5f,2.3f}) {
    SunDepth field;field.reset(128,-5,-5,16,17,dx,.37f);field.caster=1;
    field.quad({Point{0,0,3},Point{2,0,3},Point{2,2,3},Point{0,2,3}});field.seal();field.beginActors();
    field.actorRegion({0,0,0},{3,3,4});field.caster=2;
    field.quad({Point{0,0,1},Point{1,0,1},Point{1,1,1},Point{0,1,1}});
    field.quad({Point{2,2,4},Point{3,2,4},Point{3,3,4},Point{2,3,4}});
    for(float z:{0.0f,.7f,2.5f,4.5f})for(int y=-10;y<45;++y)for(int x=-10;x<45;++x) {
      Point p{x*.137f,y*.129f,z};field.fineActors=false;const bool coarse=field.actorNear(p);field.fineActors=true;const bool fine=field.actorNear(p);
      if(coarse)++coarseHits;if(coarse && !fine)++fineSkips;
      if(!fine)fineParity=fineParity && field.shadow(p)==field.shadow(p,{0,0,1},0xFFFFFFFFu,true);
    }
    field.beginActors();for(auto changed:field.actorChanged)fineParity=fineParity && !changed;
  }
  check("fine actor blocks preserve full PCF results at fractional borders and raised receivers",fineParity);
  check("actual changed texels reject empty parts of actor bounding boxes",fineSkips>100 && fineSkips<coarseHits);
  depth.beginActors();check("previous actor position leaves no shadow trail",depth.shadow({3.8f,.5f,0})==0);
  WorldMap hill;hill.makeBlank(12,12,0);auto *high=hill.getTileMutable(4,4);high->height=4;for(auto &h:high->cornerHeight)h=4;hill.touch();
  depth.reset(256,-4,-4,16,12,.5f,0);depth.terrain(hill);
  check("a raised terrace and its cliff shadow the ground below",depth.shadow({5.5f,4.5f,0})>.8f);
  check("top of an isolated terrace does not self-shadow",depth.shadow({4.5f,4.5f,4})<.1f);
  check("night returns no sun occlusion",DayNightLighting::skyAt(0).sun==0);
  check("shadow fixture opens",world.loadBlank(80,80));
  auto &map=world.getMap();auto &objects=world.objectsMutable();world.getFences().setEditing(true);
  // A continuous ramp behind the buildings, then a sheer terrace edge.
  for(int y=33;y<=48;++y)for(int x=42;x<=49;++x)if(auto *t=map.getTileMutable(x,y)) {
    const int a=std::min(4,std::max(0,x-42)),b=std::min(4,std::max(0,x+1-42));
    t->height=a;t->cornerHeight[0]=t->cornerHeight[3]=a;t->cornerHeight[1]=t->cornerHeight[2]=b;
  }
  map.touch();
  for(const auto &[file,x,y]:std::vector<std::tuple<std::string,float,float>>{
    {"scenery/building/restrant.ai",38.5f,40.5f},{"scenery/building/bgrstnd.ai",42.0f,39.5f},
    {"scenery/building/carousal.ai",35.5f,34.5f},{"scenery/other/lamp.ai",40.5f,42.5f},
    {"scenery/other/lamp.ai",46.5f,40.5f},{"scenery/other/campfire.ai",40.5f,36.5f}})objects.add(file,x,y,0);
  auto &animals=world.getAnimals();int adopted=0;
  for(const char *file:{"animals/elephant.ai","animals/giraffe.ai","animals/trex.ai","animals/zebra.ai"}) {
    int type=animals.typeOfFile(file);if(type<0)continue;
    int id=animals.adopt(type,false,41.5f+adopted*.8f,43.5f+adopted*.9f,map,world.getFences(),true);
    if(auto *m=animals.member(id)){m->boxed=false;m->anim="walk";m->fx=0;m->fy=-1;++adopted;}
  }
  auto &staff=world.getStaff();int hired=0;
  for(size_t i=0;i<staff.types().size();++i)if(staff.types()[i].kind!=Staff::Kind::Helicopter) {
    const int id=staff.hire(int(i),38.5f+hired*.8f,43.5f,map,world.getFences());if(id>=0){++hired;if(auto *m=staff.member(id))m->anim="walk";}
  }
  for(int y=34;y<49;++y)for(int x=32;x<50;++x)if(y==45 || x==40)map.setPath(x,y,"path");
  auto &guests=world.getGuests();guests.setEntrance(40,45);
  for(int i=0;i<12;++i)guests.admit(0);
  auto &people=const_cast<std::vector<Guests::Guest>&>(guests.guests());
  for(size_t i=0;i<people.size();++i){auto &g=people[i];g.x=34.5f+float(i)*.8f;g.y=45.5f;g.hidden=false;g.anim="walk";g.fx=0;g.fy=-1;}
  map.touch();world.reindex();world.setPaused(true);world.getCamera().zoom=1;
  world.getRenderer().centreOnWorld(41,40,0);Features::dayNightLighting=true;
  auto &light=world.getLighting();light.previewHour=16;
  for(int i=0;i<40;++i)world.draw(renderer);
  // All actual animation states continue below without advancing AI.
  check("native land animal shadow geometry loaded",light.stats().actors>=4);
  check("keepers, guides and specialists are present in the shadow fixture",hired>=3);
  check("animated shadow geometry stays in its face budget",light.stats().actorFaces>0 && light.stats().actorFaces<=DayNightLighting::maxActorFaces);
  for(int r=0;r<4;++r) {
    light.previewHour=16;capture(("shadow_hills_afternoon_"+std::to_string(r)).c_str());
    light.previewHour=17.5;capture(("shadow_hills_sunset_"+std::to_string(r)).c_str());
    light.previewHour=0;capture(("shadow_hills_night_"+std::to_string(r)).c_str());world.rotateView(1);
  }
  light.previewHour=16;
  for(int f=0;f<8;++f) {
    for(const auto &m:animals.members())if(auto *a=animals.member(m.id))a->animTime=f*.12f;
    for(auto &g:people)g.animTime=f*.12f;
    for(const auto &m:staff.members())if(auto *s=staff.member(m.id))s->animTime=f*.12f;
    capture(("shadow_animation_"+std::to_string(f)).c_str());
  }
  auto &fences=world.getFences();int tankType=fences.typeIndex("tankwal2");if(tankType<0)tankType=fences.typeIndex("tankwall");
  if(tankType>=0) {
    const int x=29,y=23,w=12,h=8;
    for(int i=x;i<x+w;++i){fences.place({true,i,y},tankType,1,map);fences.place({true,i,y+h},tankType,2,map);}
    for(int j=y;j<y+h;++j){fences.place({false,x,j},tankType,3,map);fences.place({false,x+w,j},tankType,4,map);}
    for(int id:fences.updateExhibits(map,1,0,1))fences.finishExhibit(id,"Shadow tank",map);
    const int tank=fences.exhibitAt(x+1,y+1);
    if(tank>=0) {
      fences.fillExhibit(tank,true);int swimming=0;
      for(const char *file:{"animals/orca.ai","animals/dolphin.ai"}) {
        int type=animals.typeOfFile(file);if(type<0)continue;
        int id=animals.adopt(type,false,x+3.5f+swimming*4,y+3.5f,map,fences,true);
        if(auto *m=animals.member(id)){m->boxed=false;m->swimZ=2;m->anim="walk";m->fx=0;m->fy=-1;++swimming;}
      }
      world.reindex();world.getRenderer().centreOnWorld(35,28,0);light.previewHour=14;
      for(int i=0;i<40;++i)world.draw(renderer);
      check("native marine animals included in the shadow scene",swimming>=1);
      capture("shadow_tank_day");Features::dayNightLighting=false;capture("shadow_tank_original_art");Features::dayNightLighting=true;
      light.previewHour=0;capture("shadow_tank_night");
      for(int i=0;i<4;++i){light.previewHour=16;capture(("shadow_tank_view_"+std::to_string(i)).c_str());world.rotateView(1);}
    } else check("marine shadow tank closes correctly",false);
  }
  log<<"Actors="<<light.stats().actors<<" actorFaces="<<light.stats().actorFaces<<" modelsBuilt="<<light.stats().actorBuilds<<" fallbackPoses="<<light.stats().actorFallbacks<<"\n";
  log<<"Failures: "<<failed<<"\n";std::ofstream(out+"/shadow-checks.txt")<<log.str();SDL_Log("[ShadowChecks] %s",log.str().c_str());
  return failed?1:0;
}
