#include "World.hpp"
#include "Features.hpp"
#include "RenderSettings.hpp"
#include "ItemCatalog.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

// addition: real native states in disposable scenes, plus independent checks
// of transparent openings, ramps and per-state emission gates.
int runLightingDetailChecks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out,const std::function<void()> &draw) {
  using namespace LightingAssets;int failed=0;std::ostringstream report;
  auto check=[&](const std::string &name,bool ok){report<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;};
  auto capture=[&](const std::string &name){draw();int w,h;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);auto *s=SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if(!s){++failed;return;}if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,s->pixels,s->pitch) || !IMG_SavePNG(s,(out+"/"+name+".png").c_str()))++failed;SDL_DestroySurface(s);SDL_RenderPresent(renderer);};
  auto &light=world.getLighting();const bool before=Features::dayNightLighting,paths=Features::elevatedPaths;
  Features::dayNightLighting=true;Features::elevatedPaths=true;world.loadBlank(100,100);world.setPaused(true);world.getCamera().zoom=1;
  auto &map=world.getMap();auto &fences=world.getFences();fences.setEditing(true);auto &view=world.getRenderer();
  Model aperture;aperture.faces.push_back({{Point{0,0,0},Point{1,0,0},Point{1,0,3},Point{0,0,3}},{0,-1,0},{0,-1,0}});
  aperture.faces.push_back({{Point{2,0,0},Point{3,0,0},Point{3,0,3},Point{2,0,3}},{0,-1,0},{0,-1,0}});
  check("analytic structures block rays through a physical post",aperture.ray({.5f,-1,1},{.5f,1,1}));
  check("analytic structures transmit rays through the open gap",!aperture.ray({1.5f,-1,1},{1.5f,1,1}));
  check("analytic structures handle reverse rays and finite segments",aperture.ray({.5f,1,1},{.5f,-1,1}) && !aperture.ray({.5f,-2,1},{.5f,-1,1}));
  int normal=0,gates=0;
  for(size_t i=0;i<fences.types().size();++i){const auto &t=fences.types()[i];if(t.tank)continue;
    const int x=25+int(normal%12)*2,y=25+int(normal/12)*3;
    fences.place({true,x,y},int(i),0,map);if(t.hasGate && t.art[1][0]){fences.place({true,x,y+1},int(i),0,map);auto *p=const_cast<Fences::Piece*>(fences.at({true,x,y+1}));if(p){p->gate=true;p->open=float(std::max(0,t.art[1][0]->frameCount()/2));++gates;}}
    ++normal;
  }
  Walkways::Deck d;d.type="path";for(int &h:d.h)h=6;world.getWalkways().set(40,43,d);world.getWalkways().set(41,43,d);
  d.h[0]=d.h[3]=5;world.getWalkways().set(39,43,d);
  for(int &h:d.h)h=1;world.getWalkways().set(37,43,d);map.setPath(36,43,"path");
  for(int &h:d.h)h=0;if(auto *t=map.getTileMutable(36,43)){t->cornerHeight[1]=t->cornerHeight[2]=1;}
  map.touch();world.reindex();view.centreOnWorld(38,35,0);light.previewHour=17.5;
  for(int i=0;i<12;++i)draw();
  check("native fence course includes ordinary and gate artwork",normal>=35 && gates>=10);
  report<<"Structure totals native="<<normal<<" gates="<<gates<<" instances="<<light.structureInstances.size()<<" pieces="<<fences.pieces().size()<<"\n";
  for(const auto &[k,m]:light.structureModels)if(m.faces.empty())report<<"Empty structure "<<k<<"\n";
  // (the land gates are moving shapes, with the animals and guests: collectGates)
  auto gateShapes=[&]{size_t n=0;for(const auto &o:light.actors)if(((o.owner>>56)&1) && ((o.owner>>48)&1))++n;return n;};
  check("native fence and raised-path structure geometry is nonempty",light.structureInstances.size()+gateShapes()>=size_t(normal+gates+4));
  check("every land gate is a moving shape",gateShapes()==size_t(gates));
  {
    // ours: a gate swinging changes its own shape only, not the scenery's
    const auto scene=light.structureHash;
    for(const auto &[edge,piece]:fences.pieces())if(auto *p=const_cast<Fences::Piece*>(fences.at(edge)))if(p->gate)p->open=0;
    draw();
    check("a gate's swing doesn't remake the scenery's lighting",light.structureHash==scene && gateShapes()==size_t(gates));
    for(const auto &[edge,piece]:fences.pieces())if(auto *p=const_cast<Fences::Piece*>(fences.at(edge))){const auto &t=fences.types()[p->type];
      if(p->gate && t.art[1][0])p->open=float(std::max(0,t.art[1][0]->frameCount()/2));}
    draw();
  }
  bool glass=false,rail=false;
  for(const auto &o:light.structureInstances){if(o.model->faces.size()>6)rail=true;}
  for(const auto &[key,m]:light.structureModels)if(key.find("fence:")!=std::string::npos && !m.faces.empty())glass=true;
  check("sparse fence meshes retain openings rather than solid slabs",rail && glass);
  check("structure irradiance shortlist matches exhaustive rays",light.lights.empty() || light.verifyLocalLightIndex());
  for(int r=0;r<4;++r){light.previewHour=17.5;capture("structures_sunset_"+std::to_string(r));light.previewHour=0;capture("structures_night_"+std::to_string(r));world.rotateView(1);}
  const auto initialStructureHash=light.structureHash;
  for(int phase=0;phase<3;++phase){
    for(const auto &[edge,piece]:fences.pieces())if(auto *p=const_cast<Fences::Piece*>(fences.at(edge))){const auto &t=fences.types()[p->type];
      if(p->gate && t.art[1][0])p->open=phase==0?0:float(std::max(0,t.art[1][0]->frameCount()-1));
      else if(!t.indestructible)p->life=phase==2?0:float(t.decayedLife);
    }
    light.previewHour=17.5;capture("gate_damage_phase_"+std::to_string(phase));
  }
  check("gate and damage changes invalidate structural shadow geometry",light.structureHash!=initialStructureHash);
  // Independent locality check: changing a near caster may not alter or
  // rebuild a distant lamp's irradiance, even though the scene hash changes.
  world.loadBlank(100,100);world.setPaused(true);fences.setEditing(true);view.centreOnWorld(40,32,0);light.previewHour=0;
  world.objectsMutable().add("scenery/other/lamp.ai",30.5f,30.5f,0);world.objectsMutable().add("scenery/other/lamp.ai",50.5f,30.5f,0);world.reindex();
  for(int i=0;i<12;++i)draw();
  const auto far=light.groundLight.size()>1?light.groundLight[1]:std::vector<DayNightLighting::Irradiance>{};
  std::string rock;for(const auto &item:ItemCatalog::get().all())if(item.members.count("rocks")){rock=item.file;break;}
  world.objectsMutable().add(rock,31.5f,30.5f,0);world.reindex();int rebuilt=0;for(int i=0;i<4;++i){draw();rebuilt+=light.stats().rays;}
  bool same=!far.empty() && light.groundLight.size()>1 && far.size()==light.groundLight[1].size();
  if(same)for(size_t i=0;i<far.size();++i){const auto a=far[i].colour,b=light.groundLight[1][i].colour;if(a.r!=b.r || a.g!=b.g || a.b!=b.b)same=false;}
  check("local placement preserves distant irradiance byte for byte",same);
  check("local placement rebuilds only the affected lamp grid",rebuilt>0 && rebuilt<=441);
  check("placement still agrees with exhaustive obstruction rays",light.verifyLocalLightIndex());
  {DayNightLighting fresh;fresh.previewHour=0;int w,h;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);
    for(int i=0;i<10;++i)fresh.prepare(world,rm,world.objectsMutable(),map,view,0,w,h,renderer);
    bool equal=fresh.groundLight.size()==light.groundLight.size() && fresh.surfaceLight.size()==light.surfaceLight.size();
    if(equal)for(size_t g=0;g<fresh.groundLight.size();++g){const auto &a=fresh.groundLight[g],&b=light.groundLight[g];if(a.size()!=b.size()){equal=false;break;}
      for(size_t j=0;j<a.size();++j)if(a[j].colour.r!=b[j].colour.r || a[j].colour.g!=b[j].colour.g || a[j].colour.b!=b[j].colour.b)equal=false;
    }
    if(equal)for(size_t i=0;i<fresh.surfaceLight.size();++i)if(fresh.surfaceLight[i]!=light.surfaceLight[i])equal=false;
    check("incremental placement lighting matches a fresh complete rebuild",equal);
  }
  // Every mapped actor must have a real native flash, while normal poses are dark.
  world.loadBlank(100,100);world.setPaused(true);fences.setEditing(true);view.centreOnWorld(44,43,0);light.previewHour=0;
  auto &animals=world.getAnimals();int n=0;std::ofstream effects(out+"/effects.csv");effects<<"resource,direction,peak_frame,pixels\n";
  for(const char *file:{"animals/allo.ai","animals/ankylo.ai","animals/caudipt.ai","animals/coelo.ai","animals/iguano.ai","animals/lambeo.ai","animals/smilo.ai","animals/spino.ai","animals/trex.ai","animals/velocira.ai"}) {
    int type=animals.typeOfFile(file);if(type<0){check(std::string(file)+" exists",false);continue;}
    Animals::Member temporary;temporary.type=type;auto *a=animals.art(temporary,"test_fence");int bestFrame=0,allPixels=0;
    if(a)for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW}) {
      int peak=0,at=0;for(int f=0;f<a->frameCount();++f)if(int p=actorEffect(file,"test_fence",f)) {
        int pixels=0;for(const auto &patch:a->emissionPatches(renderer,side,f,p))pixels+=patch.pixels;if(pixels>peak){peak=pixels;at=f;}
      }
      effects<<file<<','<<int(side)<<','<<at<<','<<peak<<'\n';allPixels+=peak;if(side==CompassDirection::NE)bestFrame=at;
    }
    check(std::string(file)+" native electrical emission exists",allPixels>0);
    check(std::string(file)+" normal poses never emit",actorEffect(file,"walk",bestFrame)==0 && actorEffect(file,"idle",bestFrame)==0);
    int id=animals.adopt(type,false,36.5f+(n%5)*4,40.5f+(n/5)*4,map,fences,true);
    if(auto *m=animals.member(id)){m->boxed=false;m->egg=false;m->anim="test_fence";m->animTime=(bestFrame+.1f)*std::max(1u,a->frameTimeMs())/1000.0f;m->fx=0;m->fy=-1;}++n;
  }
  auto &guests=world.getGuests();guests.setEntrance(44,45);for(int i=0;i<4;++i)guests.admit(0);
  auto &people=const_cast<std::vector<Guests::Guest>&>(guests.guests());int cameraPeople=0;
  for(auto &g:people){g.hidden=false;g.x=39.5f+cameraPeople*2;g.y=47.5f;const auto key=guests.types()[g.type].key;
    if(key=="man" || key=="woman"){g.anim="camera";if(auto *a=guests.art(g,g.anim)){g.animTime=13.1f*std::max(1u,a->frameTimeMs())/1000;int sum=0;
      for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW})for(const auto &p:a->emissionPatches(renderer,side,13,actorEffect("guests/"+key,"camera",13)))sum+=p.pixels;
      check(key+" native camera exposure has mapped pixels",sum>0);++cameraPeople;}}
  }
  for(size_t type=0;type<guests.types().size();++type){const auto &key=guests.types()[type].key;if(key!="man" && key!="woman")continue;
    Guests::Guest g;g.type=int(type);if(auto *a=guests.art(g,"camera")){int sum=0;
      for(auto side:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW})for(const auto &p:a->emissionPatches(renderer,side,13,actorEffect("guests/"+key,"camera",13)))sum+=p.pixels;
      check(key+" deterministic four-view camera flash",sum>0);
    }else check(key+" camera art exists",false);
  }
  check("camera gate excludes frames before and after exposure",actorEffect("guests/man","camera",12)==0 && actorEffect("guests/man","camera",14)==0 && actorEffect("guests/woman","camera",13)==1002);
  check("guest shock follows intermittent native flash frames",actorEffect("guests/man","fencezap",47)==1003 && actorEffect("guests/man","fencezap",46)==0 && actorEffect("guests/man","fencezap",48)==0);
  check("ordinary animals and research holds have no invented light",actorEffect("animals/orca.ai","test_fence",13)==0 && !find("jellyfsh") && !find("morayeel") && !find("buoy"));
  world.reindex();for(int i=0;i<25;++i)draw();
  check("simultaneous flash sources stay within eight-source bound",!light.transientLights.empty() && light.transientLights.size()<=8);
  for(int r=0;r<4;++r){capture("electric_camera_"+std::to_string(r));world.rotateView(1);}
  for(const auto &a:animals.members())if(auto *m=animals.member(a.id)){m->anim="walk";m->animTime=0;}
  for(auto &g:people){g.anim="walk";g.animTime=0;}draw();check("brief effects leave no persistent emitter after animation changes",light.transientLights.empty() && light.emitters.empty());
  // Material state and placement exercise uses the actual object renderer.
  world.loadBlank(100,100);world.setPaused(true);fences.setEditing(true);int placed=0;
  const char *files[]={"scenery/building/dfnt1.ai","scenery/building/fount2.ai","scenery/other/junpond.ai","scenery/building/geyser1.ai","scenery/other/vent1.ai","scenery/foliage/xmastree.ai","scenery/other/dgate.ai","scenery/other/streetlt.ai","scenery/building/junrest.ai"};
  for(const char *file:files)if(world.objectsMutable().add(file,33.5f+(placed%3)*6,34.5f+(placed/3)*6,0))++placed;
  auto &objects=const_cast<std::vector<PlacedObjects::Object>&>(world.objectsMutable().objects());
  for(auto &o:objects)if(o.used){o.randomActive=true;o.inUse=true;o.animTime=o.used->frameCount()*.5*std::max(1u,o.used->frameTimeMs())/1000;o.useTime=o.animTime;}
  world.getItems().add(ZooItems::Kind::Dung,-1,41,42);world.getItems().add(ZooItems::Kind::Litter,-1,42,42);world.getItems().addFood(-1,43,42,1,"herbchow");
  world.reindex();view.centreOnWorld(39,40,0);for(int i=0;i<30;++i)draw();
  check("expanded native fixture places water/fire/building/holiday families",placed>=8);
  report<<"Item shadow actors="<<light.stats().actors<<" items="<<world.getItems().items().size()<<"\n";
  for(const auto &i:world.getItems().items())if(auto *a=world.getItems().artOf(i))report<<"Item art "<<a->lightingKey()<<"\n";
  for(const auto &[k,m]:light.actorModels)if(k.first.find("food/")!=std::string::npos || k.first.find("poo")!=std::string::npos || k.first.find("trash")!=std::string::npos)report<<"Item geometry "<<k.first<<" faces="<<m.model.faces.size()<<"\n";
  check("food dung and litter participate in shadow collection",light.stats().actors>=3);
  for(int r=0;r<4;++r){light.previewHour=17.5;capture("materials_sunset_"+std::to_string(r));light.previewHour=0;capture("materials_night_"+std::to_string(r));world.rotateView(1);}
  Features::dayNightLighting=before;Features::elevatedPaths=paths;light.previewHour=-1;
  report<<"Failures: "<<failed<<'\n';std::ofstream(out+"/detail-checks.txt")<<report.str();SDL_Log("[LightingDetails] %s",report.str().c_str());return failed?1:0;
}

