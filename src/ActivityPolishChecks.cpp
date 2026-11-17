#include "RenameCheats.hpp"
#include "Ambient.hpp"
#include "World.hpp"
#include "GpuTankLayer.hpp"
#include "Animation.hpp"
#include "ItemCatalog.hpp"
#include "IniReader.hpp"
#include "ZooSim.hpp"
#include "SaveGame.hpp"
#include <fstream>
#include "ZooReader.hpp"
#include "ui/UiGameScreen.hpp"
#include <filesystem>
#include "Features.hpp"
#include "OriginalConstants.hpp"
#include "SpriteDatabase.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

// Compare visible traversal against the complete map traversal on the same
// real renderer and source terrain/path textures. This does not modify a zoo.
std::string runCameraRenderChecks(SDL_Renderer *renderer) {
  extern void worldTerrainCulling(bool);
  constexpr int width=517,height=389;
  SDL_Texture *saved=SDL_GetRenderTarget(renderer);
  float xs=1,ys=1;SDL_GetRenderScale(renderer,&xs,&ys);
  SDL_SetRenderScale(renderer,1,1);SDL_Rect viewport,clip;
  SDL_GetRenderViewport(renderer,&viewport);SDL_GetRenderClipRect(renderer,&clip);
  const bool clipped=SDL_RenderClipEnabled(renderer);
  SDL_Texture *target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,width,height);
  if(!target) {SDL_SetRenderScale(renderer,xs,ys);return "FAIL camera render target unavailable";}
  SDL_SetRenderTarget(renderer,target);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);
  WorldMap map;map.makeBlank(200,120,0);
  for(int y=0;y<120;++y)for(int x=0;x<200;++x) {
    auto *t=map.getTileMutable(x,y);
    const int h=(x>=60 && x<80 && y>=30 && y<50)?18:
                (x>=128 && x<148 && y>=70 && y<90)?-12:0;
    t->height=h;t->terrainType=(x/17+y/13)%6;
    for(int k=0;k<4;++k)t->cornerHeight[k]=h;
    if(x%19==0)t->cornerHeight[CORNER_X1Y0]++;
    if(y==40 || x==138)map.setPath(x,y,"path");
  }
  map.touch();WorldRenderer view;auto &camera=view.getCamera();
  camera.screenCenterX=width/2;camera.screenCenterY=height/2;
  view.renderTerrain(renderer,map,SpriteDatabase::get());
  std::set<std::pair<int,int>> cover;
  for(int y=26;y<54;++y)for(int x=56;x<84;++x)cover.insert({x,y});
  bool equal=true,sawPixels=false;int cases=0,replayed=0;
  const SDL_Point positions[]={{0,0},{199,0},{199,119},{0,119},{100,60},{70,40},{138,80}};
  for(float zoom:{0.45f,1.0f,1.1f,2.25f})for(int rotation=0;rotation<4;++rotation) {
    camera.zoom=zoom;
    for(const auto p:positions) {
      const auto *tile=map.getTile(p.x,p.y);view.centreOnWorld(p.x+0.5f,p.y+0.5f,tile->height);
      for(bool partial:{false,true}) {
        std::vector<Uint32> reference(width*height),pixels(width*height);
        for(bool bounded:{false,true}) {
          worldTerrainCulling(bounded);SDL_SetRenderScale(renderer,1,1);
          SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_SetRenderScale(renderer,zoom,zoom);
          if(partial)view.renderTerrainTiles(renderer,map,SpriteDatabase::get(),cover);
          else view.renderTerrain(renderer,map,SpriteDatabase::get());
          SDL_FlushRenderer(renderer);
          const bool read=ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA8888,pixels.data(),width*4)==0;
          equal &= read;
          if(!bounded) {reference=pixels;sawPixels |= std::any_of(pixels.begin(),pixels.end(),[](Uint32 p){return p!=0x000000ff;});}
          else equal &= pixels==reference;
        }
        // (the whole ground once more, nothing changed: its kept calls)
        if(!partial) {
          const int before=view.terrainReplays();
          SDL_SetRenderScale(renderer,1,1);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
          SDL_SetRenderScale(renderer,zoom,zoom);view.renderTerrain(renderer,map,SpriteDatabase::get());
          SDL_FlushRenderer(renderer);
          equal &= ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA8888,pixels.data(),width*4)==0;
          const bool same=pixels==reference;equal &= same;replayed+=view.terrainReplays()==before+1;
        }
        ++cases;
      }
    }
    view.rotateView(1);
  }
  // (a change to the ground: drawn afresh, the kept calls not used - its
  // pixels as a fresh renderer's)
  bool fresh=false;
  {
    camera.zoom=1.0f;view.centreOnWorld(70.5f,40.5f,map.getTile(70,40)->height);
    auto draw=[&](WorldRenderer &v,std::vector<Uint32> &out) {
      SDL_SetRenderScale(renderer,1,1);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
      v.renderTerrain(renderer,map,SpriteDatabase::get());SDL_FlushRenderer(renderer);
      return ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA8888,out.data(),width*4)==0;
    };
    std::vector<Uint32> a(width*height),b(width*height),c(width*height);
    bool ok=draw(view,a);
    for(int y=36;y<45;++y)for(int x=66;x<75;++x)map.getTileMutable(x,y)->terrainType=(map.getTile(x,y)->terrainType+3)%6;
    map.setPath(71,41,"path");map.touch();
    const int before=view.terrainReplays();
    ok&=draw(view,b);
    WorldRenderer other;
    for(int k=0;k<view.getRotation();++k)other.rotateView(1);
    other.getCamera()=camera;
    ok&=draw(other,c);
    fresh=ok && view.terrainReplays()==before && b==c && b!=a;
  }
  equal&=fresh;
  worldTerrainCulling(true);SDL_SetRenderTarget(renderer,saved);
  SDL_SetRenderScale(renderer,1,1);SDL_SetRenderViewport(renderer,&viewport);
  SDL_SetRenderClipRect(renderer,clipped?&clip:nullptr);SDL_SetRenderScale(renderer,xs,ys);SDL_DestroyTexture(target);
  std::ostringstream report;
  report<<(equal && sawPixels && cases==224 && replayed==112?"PASS ":"FAIL ")<<cases
        <<" terrain pixel comparisons: four zooms, four rotations, map edges, raised cliffs, deep pits, paths and partial ground redraw; "
        <<replayed<<" of 112 whole-ground draws again from the kept calls, the same pixels; a changed ground drawn afresh"
        <<(fresh?"":" (FAILED)");
  SDL_Log("[CameraRender] %s",report.str().c_str());return report.str();
}

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
  // ours: an animal's art loaded ahead on another thread (Animals::warmOne)
  // is the same art as loaded at once: every direction's frames, pixel for
  // pixel. A dolphin's show: the largest, 150 ms to read
  {
    const std::string path="animals/dolphin/m/belyflop/belyflop";
    auto frames=[](Animation *a) {
      std::vector<std::string> out;
      if(a)a->forEachFrame([&](const std::string &d,int i,SDL_Surface *f) {
        std::string key=d+"/"+std::to_string(i)+"/"+std::to_string(f->w)+"x"+std::to_string(f->h)+":";
        for(int y=0;y<f->h;y++)key.append(static_cast<const char *>(f->pixels)+size_t(y)*f->pitch,size_t(f->w)*SDL_BYTESPERPIXEL(f->format));
        out.push_back(std::move(key));
      });
      std::sort(out.begin(),out.end());
      return out;
    };
    Animation *direct=rm->hasResource(path+".ani")?rm->getAnimation(path):nullptr,*ahead=nullptr;
    const bool started=direct && rm->loadAnimationAhead(path);
    const bool taken=started && rm->takeAnimationAhead(path,ahead,true);
    const auto a=frames(direct),b=frames(ahead);
    check("art loaded ahead on another thread matches art loaded at once ("+std::to_string(a.size())+" frames)",
          taken && ahead && ahead!=direct && !a.empty() && a==b && ahead->frameTimeMs()==direct->frameTimeMs());
    delete direct;delete ahead;
  }
  // ours: a guest's outfit made as a copy of its art is the art loaded in it
  {
    const std::string r=world.getGuests().outfitCheck();
    check(r.substr(5),r.rfind("PASS ",0)==0);
  }
  auto fileRoundTrip=[&](const std::string &stage) {
    const auto na=world.getAnimals().members().size(),ns=world.getStaff().members().size(),ng=world.getGuests().guests().size();
    ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
    Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
    for(const char *extension:{".zt1save",".zoo"}) {
      const auto file=std::filesystem::temp_directory_path()/
        ("zt1-activity-save-"+std::to_string(SDL_GetTicks())+extension);
      const bool written=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn");
      const bool loaded=written&&SaveGame::load(file.string(),world,books,hud);
      check(stage+" full-file "+extension+" preserves every actor",loaded&&
        world.getAnimals().members().size()==na&&world.getStaff().members().size()==ns&&world.getGuests().guests().size()==ng);
      if(!loaded)report<<"SAVE ERROR "<<SaveGame::lastError()<<"\n";
      std::error_code ec;std::filesystem::remove(file,ec);
    }
    SaveGame::goals=priorGoals;
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
    // A later frame above frame 0 must survive decoding, with one common
    // anchor across the sequence (the dolphin's airborne flip regression).
    auto *data=static_cast<AnimationData*>(std::calloc(1,sizeof(AnimationData)));
    data->width=data->height=4;data->frame_count=2;data->frame_time_in_ms=100;
    data->frames=static_cast<AnimationFrameData*>(std::calloc(2,sizeof(AnimationFrameData)));
    for(int i=0;i<2;++i) {
      auto &frame=data->frames[i];frame.width=frame.height=2;
      frame.offset_y=1;frame.offset_x=1+i*10;frame.is_shadow=true;
      frame.lines=static_cast<AnimationLineData*>(std::calloc(2,sizeof(AnimationLineData)));
      for(int y=0;y<2;++y) {
        auto &line=frame.lines[y];line.instruction_count=1;
        line.instructions=static_cast<AnimationDrawInstruction*>(std::calloc(1,sizeof(AnimationDrawInstruction)));
        line.instructions[0].color_count=2;
      }
    }
    std::unordered_map<std::string,AnimationData*> frames{{"N",data}};
    Animation probeFrames(&frames);int completeFrames=0;
    probeFrames.forEachFrame([&](const auto &,int,SDL_Surface *surface) {
      int pixels=0;for(int y=0;y<surface->h;++y)for(int x=0;x<surface->w;++x) {
        const auto pixel=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(surface->pixels)+y*surface->pitch)[x];
        pixels+=(pixel & SDL_GetPixelFormatDetails(surface->format)->Amask)!=0;
      }
      completeFrames+=pixels==4;
    });
    check("airborne frames above the first frame retain every source pixel",completeFrames==2);
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
  // The animal pass nests inside the tank's device-pixel strips. A changed
  // clip or scale corrupts later glass, platform and filter drawing.
  SDL_Surface *canvas=SDL_CreateSurface(512, 512, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer *probe=canvas?SDL_CreateSoftwareRenderer(canvas):nullptr;if(probe)SDL_SetDefaultTextureScaleMode(probe,SDL_SCALEMODE_NEAREST); // (SDL2's default)
  bool restored=probe!=nullptr;const bool oldTankClip=Features::tankClip;Features::tankClip=true;
  world.centreOn(38,32);
  if(probe)for(float scale:{1.0f,1.5f,2.0f})for(int turn=0;turn<4;++turn) {
    SDL_SetRenderScale(probe,1,1);SDL_Rect expected{17,23,3000,3000};
    SDL_SetRenderClipRect(probe,&expected);SDL_SetRenderScale(probe,scale,scale);
    animals.drawInTank(probe,world.getRenderer(),map,tankId,false);
    float xScale=0,yScale=0;SDL_GetRenderScale(probe,&xScale,&yScale);
    SDL_SetRenderScale(probe,1,1);SDL_Rect got;SDL_GetRenderClipRect(probe,&got);
    restored &= SDL_RenderClipEnabled(probe) && got.x==expected.x && got.y==expected.y &&
      got.w==expected.w && got.h==expected.h && xScale==scale && yScale==scale;
    world.getRenderer().rotateView(1);
  }
  // Keep the probe alive for the pixel comparison below.
  check("tank animal clip preserves parent renderer state at every zoom and rotation",restored);
  // Compare accelerated mask shortcuts with the exact clipped path using real artwork.
  extern void tankAnimalFastPaths(bool);
  bool samePixels=probe!=nullptr,sawPixels=false;
  const int swimmer=animals.adopt(animals.typeOfFile("animals/whleshrk.ai"),false,37.5f,31.5f,map,fn,true);
  auto *fish=animals.member(swimmer);
  // (an update first, as every frame of play has one: it gives the animals
  // the tanks to clip to - without it there was no outline, and nothing
  // here was clipped at all)
  animals.update(0.0f,world.getClock(),map,fn);
  samePixels &= fish!=nullptr;
  const Camera oldCamera=world.getCamera();
  // (its facing pinned as its place is - adopt's is random; each of the
  // eight facings' differences is logged)
  std::string facingsOff;
  for(int facing=-1;facing<8;++facing)
  if(probe && fish)for(float scale:{1.0f,1.5f,2.0f})for(int rotation=0;rotation<4;++rotation) {
    float sx,sy,depth;world.getRenderer().worldToScreenF(38,32,ex->floorHeight,sx,sy,depth);
    world.getCamera().x+=128-sx;world.getCamera().y+=100-sy;
    fish->anim="subswim";fish->swimZ=fish->swimTo=fn.waterLevel(*ex)*0.5f;
    fish->x=rotation%2?36.1f:38.0f;fish->y=rotation%2?30.1f:32.0f;
    const float turn=facing<0?0.0f:facing*3.14159265f/4.0f;fish->fx=std::sin(turn);fish->fy=std::cos(turn);
    const bool before=samePixels;
    std::vector<Uint8> reference;
    for(bool fast:{false,true}) {
      // (each pass from fresh textures: SDL's software renderer, which this
      // probe is, left a texture it had drawn scaled by 1.5 blank when drawn
      // so again - a whale shark facing 3, its second pass blank. #179)
      Animation::releaseAllTextures();
      tankAnimalFastPaths(fast);
      SDL_SetRenderScale(probe,1,1);SDL_SetRenderClipRect(probe,nullptr);
      SDL_SetRenderDrawColor(probe,0,0,0,0);SDL_RenderClear(probe);
      SDL_Rect parent{17,23,450,450};SDL_SetRenderClipRect(probe,&parent);SDL_SetRenderScale(probe,scale,scale);
      animals.drawInTank(probe,world.getRenderer(),map,tankId,false);SDL_FlushRenderer(probe);
      const auto *bytes=static_cast<const Uint8*>(canvas->pixels);
      if(!fast) {
        reference.assign(bytes,bytes+canvas->pitch*canvas->h);
        sawPixels |= std::any_of(reference.begin(),reference.end(),[](Uint8 value){return value!=0;});
      } else samePixels &= std::equal(reference.begin(),reference.end(),bytes);
    }
    if(facing>=0 && before && !samePixels)
      facingsOff+=" facing "+std::to_string(facing)+" scale "+std::to_string(scale).substr(0,3)+" turn "+
                  std::to_string(rotation);
    samePixels |= facing>=0 && before; // (each facing logged; all of them counted below)
    world.getRenderer().rotateView(1);
  }
  tankAnimalFastPaths(true);world.getCamera()=oldCamera;if(swimmer>=0)animals.remove(swimmer);
  report<<"WHALECLIP fast path differs at:"<<(facingsOff.empty()?" none":facingsOff)<<"\n";
  check("optimized whale clipping matches exact pixel clips at three scales, four rotations and eight facings (#179)",
        samePixels && sawPixels && facingsOff.empty());
  // Compare sprite visibility pruning with unpruned drawing, including eaves,
  // animated buildings, clipped halves, custom passes, and all four view edges.
  extern void worldSpriteCulling(bool);
  bool cullPixels=probe!=nullptr,cullSawPixels=false,cullState=true;int cullCases=0;
  if(probe) {
    PlacedObjects scene;ZooReader empty;scene.load(empty,rm);
    bool assets=true;
    for(const char *file:{"scenery/building/carousal.ai","scenery/building/restrant.ai",
                         "scenery/building/geyser1.ai","scenery/building/drkstnd.ai"})
      assets &= scene.add(file,37.5f,31.5f,4);
    for(auto &o:scene.all()) {o.inUse=o.used!=nullptr;o.animTime=o.useTime=0.7f;}
    cullPixels &= assets;
    const Camera cameraWas=world.getCamera();
    for(float scale:{0.65f,1.0f,1.5f,2.0f})for(int rotation=0;rotation<4;++rotation) {
      auto &camera=world.getCamera();camera.screenCenterX=camera.screenCenterY=256;camera.zoom=scale;
      for(SDL_Point edge: {SDL_Point{0,0},{256,256},{512,0},{0,512},{512,512}}) {
        float sx,sy,depth;world.getRenderer().worldToScreenF(37.5f,31.5f,0,sx,sy,depth);
        camera.x+=static_cast<int>(std::lround(edge.x/scale-sx));
        camera.y+=static_cast<int>(std::lround(edge.y/scale-sy));
        std::vector<Fences::Drawable> extra;
        if(scene.last()) {
          float x,y,d;world.getRenderer().worldToScreenF(37.5f,31.5f,0,x,y,d);
          Fences::Drawable half{d+0.5f,x+32,y,scene.last()->art,CompassDirection::SW};
          half.clip=true;half.clipX0=x;half.clipX1=x+50;half.clipY1=y+10;
          extra.push_back(half);
        }
        Fences::Drawable custom{};custom.depth=10000;
        custom.custom=[](SDL_Renderer *r){SDL_SetRenderDrawColor(r,250,70,30,255);SDL_Rect box{10,10,8,8};ZT_RenderFillRect(r,&box);};
        extra.push_back(custom);
        // An off-screen custom strip must have no effect on visible pixels.
        custom.clip=true;custom.clipX0=-1000;custom.clipX1=-20;extra.push_back(custom);
        std::vector<Uint8> reference;
        for(bool cull:{false,true}) {
          worldSpriteCulling(cull);SDL_SetRenderScale(probe,1,1);SDL_SetRenderClipRect(probe,nullptr);
          SDL_SetRenderDrawColor(probe,0,0,0,0);SDL_RenderClear(probe);
          SDL_Rect parent{7,9,499,497};SDL_SetRenderClipRect(probe,&parent);SDL_SetRenderScale(probe,scale,scale);
          scene.draw(probe,world.getRenderer(),map,extra);SDL_FlushRenderer(probe);
          float xs=0,ys=0;SDL_GetRenderScale(probe,&xs,&ys);SDL_SetRenderScale(probe,1,1);
          SDL_Rect got;SDL_GetRenderClipRect(probe,&got);
          cullState &= xs==scale && ys==scale && SDL_RenderClipEnabled(probe) &&
            got.x==parent.x && got.y==parent.y && got.w==parent.w && got.h==parent.h;
          const auto *bytes=static_cast<const Uint8*>(canvas->pixels);
          if(!cull) {reference.assign(bytes,bytes+canvas->pitch*canvas->h);
            cullSawPixels |= std::any_of(reference.begin(),reference.end(),[](Uint8 p){return p!=0;});}
          else cullPixels &= std::equal(reference.begin(),reference.end(),bytes);
        }
        ++cullCases;
      }
      world.getRenderer().rotateView(1);
    }
    worldSpriteCulling(true);world.getCamera()=cameraWas;
  }
  check("off-screen sprite pruning preserves pixels at four zooms, four rotations and five view positions",cullPixels && cullSawPixels && cullCases==80);
  check("sprite pruning preserves the parent clip and renderer scale",cullState && probe);
  // (the art drawn on the probe made its textures there: let them go first)
  Features::tankClip=oldTankClip;if(probe){GpuTankLayer::releasePool(probe);Animation::releaseAllTextures();SDL_DestroyRenderer(probe);}if(canvas)SDL_DestroySurface(canvas);
  const Fences::Edge sides[4]={{true,37,30},{false,40,31},{true,37,34},{false,36,31}};
  // Full save/load deliberately does not persist fake Lab animal counts.
  // Use a real occupant so the specialist still has work after restoration.
  ex->testAnimals=0;
  const int occupant=animals.adopt(animals.typeOfFile("animals/dolphin.ai"),false,37.5f,31.5f,map,fn,true);
  check("ladder save fixture has a real marine animal",occupant>=0);
  ex->animals=animals.countIn(tankId);
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
    std::set<int> phases,savedPhases;
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
        if(sex==0 && side==0 && savedPhases.insert(m->phase).second) {
          const auto phase=m->phase;
          fileRoundTrip("specialist ladder phase "+std::to_string(phase));
          m=st.member(id);ex=fn.exhibit(tankId);
          check("interrupted specialist safely restarts on the bank",m&&m->tank<0&&!m->climbing&&
                m->showTank<0&&m->job==Staff::Job::None&&fn.exhibitAt(int(m->x),int(m->y))<0);
          if(!m||!ex)break;
          continue;
        }
      }
      if(entered && m->tank<0 && !m->climbing && m->job==Staff::Job::None &&
         phases.count(20) && phases.count(26) && phases.count(29) && ex->purity>=100) {exited=true;break;}
    }
    const std::string who=std::string(sex?"female":"male")+" side "+std::to_string(side);
    check(who+" completes entry, submerged work and exit",entered && exited && phases.count(30) && phases.count(20) && phases.count(26) && phases.count(29) && ex->purity>=100);
    check(who+" faces the ladder on ascent and descent",faceCorrect && entered);
    check(who+" plays ascent forward and descent backward",frameCorrect && entered);
    check(who+" pauses during climbing",climbPause && entered);
  }
  st.clear();
  fn.selectFilter("scenery/other/filter1.ai");
  bool pumpPlaced=false;
  for(int y=20;y<40&&!pumpPlaced;++y)for(int x=28;x<46&&!pumpPlaced;++x)
    if(fn.filterFit(x,y,map)==Fences::Fit::Ok)pumpPlaced=fn.placeFilter(x,y,map);
  check("maintenance save fixture has a real pump",pumpPlaced);
  if(pumpPlaced) {
    const int pump=int(fn.filters().size())-1;
    auto *filter=fn.filter(pump);filter->health=1;filter->decayClock=17;filter->filterClock=23;
    filter->upkeepLast=50;filter->upkeepCurrent=100;filter->upkeepTotal=350;
    const float decay=filter->decayClock,clean=filter->filterClock;
    int maintenance=-1;const int kind=st.typeOfFile("staff/maint.ai");
    for(int y=26;y<38&&maintenance<0;++y)for(int x=30;x<45&&maintenance<0;++x)
      maintenance=st.hire(kind,x+0.5f,y+0.5f,map,fn);
    // A broken pump (health 0: zoo.exe 0x615c29 posts 10036, flagged, to
    // the work list's front) comes before litter on the worker's own tile
    // (Task78_MaintFindWork 0x439cd0: once the best is flagged, the
    // unflagged aren't looked at)
    if(auto *w=st.member(maintenance)) {
      auto &items=world.getItems();
      filter->health=0;
      const int litter=items.add(ZooItems::Kind::Litter,-1,w->x,w->y);
      for(int k=0;k<200;++k){st.update(0.05f,map,fn);w=st.member(maintenance);if(!w||w->job!=Staff::Job::None)break;}
      check("a broken pump is seen to before litter at the worker's feet (0x439cd0, 0x615c29)",
            w&&w->job==Staff::Job::Filter&&w->jobFilter==pump);
      if(w){w->job=Staff::Job::None;w->jobFilter=-1;w->path.clear();w->pathAt=0;w->playsLeft=0;w->idleLeft=0;w->workCheck=0;}
      items.remove(litter);
      filter=fn.filter(pump);filter->health=1;
    }
    bool savedService=false,repaired=false;
    for(int k=0;k<6000 && !repaired;++k) {
      st.update(0.05f,map,fn);
      const auto *worker=st.member(maintenance);
      if(worker && worker->job==Staff::Job::Filter && worker->pathAt>=worker->path.size() && !savedService) {
        fileRoundTrip("maintenance servicing pump");savedService=true;
        filter=fn.filter(pump);
        check("pump clocks and costs survive both save formats",filter&&filter->decayClock==decay&&
          filter->filterClock==clean&&filter->upkeepLast==50&&filter->upkeepCurrent==100&&filter->upkeepTotal==350);
      }
      filter=fn.filter(pump);repaired=filter&&filter->health==fn.filterType(filter->file).startHealth;
    }
    check("maintenance resumes after reload and repairs the pump",savedService&&repaired);
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
  auto fileRoundTrip=[&](const std::string &stage) {
    const auto na=a.members().size(),ns=st.members().size(),ng=g.guests().size();
    ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
    Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
    for(const char *extension:{".zt1save",".zoo"}) {
      const auto file=std::filesystem::temp_directory_path()/
        ("zt1-parity-save-"+std::to_string(SDL_GetTicks())+extension);
      const bool written=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn");
      const bool loaded=written&&SaveGame::load(file.string(),world,books,hud);
      check(stage+" full-file "+extension+" preserves actors and money",loaded&&books.cash()==1000000&&
        a.members().size()==na&&st.members().size()==ns&&g.guests().size()==ng);
      if(!loaded)out<<"SAVE ERROR "<<SaveGame::lastError()<<"\n";
      std::error_code ec;std::filesystem::remove(file,ec);
    }
    SaveGame::goals=priorGoals;
  };
  a.clear();st.clear();g.clear();
  // (the flat test zoo has nothing to visit - no exhibit with animals, no
  // 'fun' building - so every guest's pick would roll cLeaveChanceDone,
  // chooseTask 0x43a6e4 at 0x440ee2: off for these checks, but where a
  // check sets it, and put back at the end)
  std::vector<int> leaveDoneWas;
  for(auto &t:g.typeList){leaveDoneWas.push_back(t.leaveDone);t.leaveDone=0;}
  // Picking up and putting down a guest (#227): src/GuestPickupChecks.cpp -
  // first, on the empty flat zoo (its fixture taken down after)
  {
    extern std::string runGuestPickupChecks(World &, ResourceManager *, int &, int &);
    out<<runGuestPickupChecks(world,rm,passed,total);
    a.clear();st.clear();g.clear();world.reindex();
  }
  // Tank Adjustment's prices and limits (#238) and the animals' messages
  // (#239): src/PanelMessageChecks.cpp, its fixture taken down after
  {
    extern std::string runPanelMessageChecks(World &, ResourceManager *, int &, int &);
    out<<runPanelMessageChecks(world,rm,passed,total);
    a.clear();st.clear();g.clear();world.reindex();
  }
  int drag=200;
  auto rectangle=[&](int x,int y,int w,int h,int type,const std::string &name) {
    for(int k=0;k<w;++k){f.place({true,x+k,y},type,drag,map);f.place({true,x+k,y+h},type,drag+1,map);}
    for(int k=0;k<h;++k){f.place({false,x,y+k},type,drag+2,map);f.place({false,x+w,y+k},type,drag+3,map);}
    drag+=4;
    for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,name,map);
    return f.exhibitAt(x,y);
  };
  // Viewing areas (zoo.exe 0x44ba57 / 0x44ea19 / 0x44ebe6): the tour zoo's
  // 8x8 chain-link paddock with a path round it and one from the west, as
  // the original grouped it (read live 2026-11-15, the tour zoo's paddock
  // at 17,38): 10 areas, tile for tile in the same order; and a tour guide
  // only goes to an area guests are at (0x58ae2c, 0x58b0a3)
  {
    int bx=-1,by=-1;
    for(int y=3;y<map.getHeight()-11&&bx<0;++y)for(int x=3;x<map.getWidth()-11&&bx<0;++x){
      bool ok=true;
      for(int yy=y-3;yy<=y+10&&ok;++yy)for(int xx=x-3;xx<=x+10&&ok;++xx)
        ok=f.exhibitAt(xx,yy)<0&&(xx<x-2||xx>x+9||yy<y-2||yy>y+9||f.insideZoo(xx,yy));
      if(ok){bx=x;by=y;}
    }
    // (the earlier fixtures' paths there taken up)
    if(bx>=0)for(int yy=by-3;yy<=by+10;++yy)for(int xx=bx-3;xx<=bx+10;++xx)map.setPath(xx,yy,"");
    check("room for the viewing-area paddock",bx>=0);
    if(bx>=0) {
      const int pad=rectangle(bx,by,8,8,f.typeIndex("chainlnk"),"Viewing paddock");
      for(int y=by-1;y<=by+8;++y)for(int x=bx-1;x<=bx+8;++x)
        if(x==bx-1||x==bx+8||y==by-1||y==by+8)map.setPath(x,y,"path");
      map.setPath(bx-2,by+4,"path"); // (the path in from the west: the one tile of it within reach)
      world.reindex();
      ViewingAreas &va=st.viewingAreas();va.clear();va.refresh(map,f);
      // (the original's, its paddock's corner 17,38 taken off)
      const std::vector<std::vector<std::pair<int,int>>> want={
        {{-1,-1},{-1,0},{-1,1},{-1,2},{0,-1},{1,-1},{2,-1}},{{3,-1},{4,-1},{5,-1},{6,-1}},
        {{7,-1},{8,-1},{8,0},{8,1},{8,2}},{{-1,3},{-1,4},{-1,5},{-1,6}},{{8,3},{8,4},{8,5},{8,6}},
        {{-2,4}},{{-1,7},{-1,8},{0,8},{1,8},{2,8}},{{8,7},{8,8}},{{5,8},{6,8},{7,8},{4,8}},{{3,8}}};
      std::vector<std::vector<std::pair<int,int>>> got;
      for(int i:va.of(pad)){got.emplace_back();for(auto [x,y]:va.areas()[i].viewing)got.back().push_back({x-bx,y-by});}
      check("viewing areas round the paddock are the original's, tile for tile ("+std::to_string(got.size())+" areas)",
            pad>=0&&got==want);
      // A guide and a zebra; no guests at any area: no trip
      const int zebra=a.typeOfFile("animals/zebra.ai");
      if(zebra>=0)a.adopt(zebra,false,bx+3.5f,by+3.5f,map,f,true);
      if(auto *e=f.exhibit(pad))e->animals=a.countIn(pad);
      const int guide=st.hire(st.typeOfFile("staff/tour.ai"),bx-0.5f,by+2.5f,map,f);
      Staff::Member *ld=st.member(guide);
      const bool none=ld&&!st.tourWork(*ld,map,f);
      check("no guests at any viewing area: no tour (0x58ae2c)",none);
      // One guest at the area of the paddock's top-middle (the original's
      // area 1): every trip goes there
      g.setEntrance(bx-1,by-1);g.admit(0);
      if(!g.list.empty()){auto &q=g.list.back();q.x=bx+4.5f;q.y=by-0.5f;q.state=Guests::State::Gawking;}
      // zoo.exe 0x4479fd through the original's areas: the zebra 3,3 in has
      // that one guest at the top-middle area, 4 tiles off - 0.9 a tile
      // truncates it to 0, so 1 (at least 1 when there are any); north of it
      // (+0x370 up)
      if(zebra>=0)for(auto &m:a.list)if(m.type==zebra&&int(m.x)==bx+3&&int(m.y)==by+3){
        va.countGuests(g);a.countGuestCrowd(m,f);
        out<<"GUESTCROWD areas "<<m.guestCrowd<<" "<<m.guestCrowdX<<" "<<m.guestCrowdY<<"\n";
        check("guests watching through the original's viewing areas: one guest 4 tiles off counts 1, north of it (0x4479fd)",
              m.guestCrowd==1&&m.guestCrowdY==1);
        break;
      }
      int trips=0,there=0;
      for(int k=0;k<20;++k){
        ld=st.member(guide);if(!ld)break;
        ld->job=Staff::Job::None;ld->path.clear();ld->pathAt=0;ld->x=bx-0.5f;ld->y=by+2.5f;ld->followers.clear();
        g.releaseTour(guide);
        if(st.tourWork(*ld,map,f)){++trips;there+=ld->goalY==by-1&&ld->goalX>=bx+3&&ld->goalX<=bx+6;}
      }
      check("a guide only tours the viewing area a guest is at ("+std::to_string(there)+" of "+std::to_string(trips)+", 0x58b0a3)",
            trips==20&&there==20);
      // The sight line (zoo.exe viewingSees 0x4f39a2 -> 0x4f2d44, 0x4f2fcf,
      // 0x4f2fa0): the lone area west of the paddock (-2,4: the tour zoo's
      // 15,42) sees all 64 tiles on flat ground through chain-link (its
      // cSeeThrough); a ridge of ground between hides them all; a maple
      // (cHeight 3, cBlocksLOS by default) on the tile in front hides the
      // tiles straight behind it, not the rest
      {
        auto lone=[&]()->const ViewingAreas::Area*{
          va.refresh(map,f);
          for(int i:va.of(pad)){const auto &ar=va.areas()[i];
            if(ar.viewing.size()==1&&ar.viewing[0]==std::make_pair(bx-2,by+4))return &ar;}
          return nullptr;};
        auto sees=[&](const ViewingAreas::Area *ar,int x,int y){
          return ar&&std::find(ar->seen.begin(),ar->seen.end(),std::make_pair(x,y))!=ar->seen.end();};
        const ViewingAreas::Area *ar=lone();
        const size_t flat=ar?ar->seen.size():0;
        for(int k=-3;k<=11;++k)map.raiseVertex(bx-1,by+k,2);
        ar=lone();const size_t ridge=ar?ar->seen.size():99;
        // the guest's area sees the zebra on flat ground, not over the ridge
        // (0x4232f4 / 0x447a00: the animals on its seen tiles)
        int seenRidge=-1,seenFlat=-1;
        if(!g.list.empty()){auto &q=g.list.back();q.x=bx-1.5f;q.y=by+4.5f;q.target=pad;q.state=Guests::State::Gawking;
          Guests::Sight s;g.findViewArea(q);seenRidge=g.areaSight(q,s)?s.animals:-1;}
        for(int k=-3;k<=11;++k)map.raiseVertex(bx-1,by+k,-2);
        ar=lone();const size_t back=ar?ar->seen.size():0;
        if(!g.list.empty()){auto &q=g.list.back();Guests::Sight s;g.findViewArea(q);seenFlat=g.areaSight(q,s)?s.animals:-1;}
        // (the maple: on the ground as it is, then with the ground there
        // lowered to 0 - the original weighs scenery against the line's
        // doubled height with its own corner and cHeight not doubled, so it
        // only hides the view on ground under cHeight - 2)
        const int ground=map.getTile(bx-2,by+4)->height;
        const bool tree=objects.add("scenery/foliage/maple.ai",bx-0.5f,by+4.5f,0);
        ar=lone();const size_t high=ar?ar->seen.size():0;
        for(int yy=by-3;yy<=by+12;++yy)for(int xx=bx-4;xx<=bx+10;++xx)map.raiseVertex(xx,yy,-ground);
        ar=lone();
        const size_t behind=ar?ar->seen.size():0;
        const bool straight=sees(ar,bx,by+4)||sees(ar,bx+7,by+4),corner=sees(ar,bx,by);
        if(tree)objects.remove(static_cast<int>(objects.objects().size())-1);
        ar=lone();const size_t low=ar?ar->seen.size():0;
        for(int yy=by-3;yy<=by+12;++yy)for(int xx=bx-4;xx<=bx+10;++xx)map.raiseVertex(xx,yy,ground);
        ar=lone();const size_t gone=ar?ar->seen.size():0;
        out<<"SIGHT lone area: flat "<<flat<<", ridge "<<ridge<<", back "<<back<<", maple at height "<<ground<<" "<<high
           <<", at 0 "<<behind<<", gone "<<low<<" / "<<gone
           <<"; guest sees "<<seenFlat<<" animal(s), over the ridge "<<seenRidge<<"; maple put down "<<tree<<"\n";
        check("the lone west area sees all 64 tiles through chain-link on flat ground (0x4f39a2)",flat==64&&back==64&&gone==64&&low==64);
        check("ground higher than the sight line hides the exhibit (0x4f2d44)",ridge==0);
        check("a maple in front at ground 0 hides the tiles straight behind it, not the rest (0x4f2d44: cBlocksLOS, cHeight)",
              tree&&behind<64&&behind>0&&!straight&&corner);
        check("...on ground 1 or higher it hides nothing (0x4f34fc: corner + cHeight against the doubled line)",
              ground<1||high==64);
        check("a watching guest's area sees the zebra on its seen tiles, none over the ridge (0x447a00)",
              seenFlat==1&&seenRidge<=0);
        // How long it watches (zoo.exe 0x42e793): guests.ai's cGawkTime by
        // the exhibit's suitability, 0-5 more, plus cGawkAttrTime by its
        // attractiveness
        if(!g.list.empty()&&zebra>=0){
          auto &q=g.list.back();const Guests::Type &t=g.typeList[q.type];
          const int s=a.exhibitSuitability(pad);
          const int band=s<=-60?0:s<=20?1:s<=60?2:s<=80?3:4;
          int attract=0;for(auto &m:a.members())if(m.exhibit==pad)attract+=static_cast<int>(a.types()[m.type].forAge(m.baby).attractiveness);
          const int bonus=t.gawkAttrTime[attract<20?0:attract<50?1:attract<90?2:attract<150?3:attract<200?4:5];
          int lo=999,hi=-1;for(int k=0;k<60;++k){const int v=g.gawkSeconds(q);lo=std::min(lo,v);hi=std::max(hi,v);}
          out<<"GAWK suitability "<<s<<" attractiveness "<<attract<<": "<<lo<<"-"<<hi<<" s\n";
          check("gawk time is cGawkTime by suitability + 0-5 + cGawkAttrTime (0x42e793, guests.ai)",
                t.gawkTime.size()==5&&t.gawkAttrTime.size()==6&&lo>=t.gawkTime[band]+bonus&&hi<=t.gawkTime[band]+bonus+(band?5:0)&&
                (band==0||hi>lo));
        }
        // Task70_ViewAnimals::begin (zoo.exe 0x42e527): at a tile with no
        // viewing area it can't watch; at the area's tile it begins - the
        // kinds its area sees go on its recent-species list (+0x278), 5 at
        // most, the oldest dropped (0x42e909); the gawk time is the task's
        if(!g.list.empty()&&zebra>=0){
          auto &q=g.list.back();
          q.x=bx-5.5f;q.y=by+4.5f;q.target=pad;q.visits.clear();q.state=Guests::State::Walking;
          const bool nowhere=!g.viewBegin(q)&&q.target<0&&q.viewWay<0;
          q.x=bx-1.5f;q.y=by+4.5f;q.target=pad;q.visits.clear();q.visitCount=0;q.recentSpecies={1,2,3,4,5};q.state=Guests::State::Walking;
          const bool began=g.viewBegin(q);
          const bool viewVisit=began&&q.visits.size()==1&&q.visitCount==1&&f.exhibitAt(q.visits[0].first,q.visits[0].second)==pad;
          const int zid=a.types()[zebra].nameId;
          out<<"VIEW begin: nowhere "<<nowhere<<", began "<<began<<" way "<<q.viewWay<<", recent";
          for(int id:q.recentSpecies)out<<" "<<id;
          out<<", "<<(q.viewEnd-q.viewStart)<<" s\n";
          check("no viewing area at its tile: the viewing task can't begin (0x42e527 -> 0x43b92e)",nowhere);
          check("at the area's tile it begins, its gawk time the task's (0x42e527, 0x42e793)",
                began&&q.state==Guests::State::Gawking&&q.viewEnd>q.viewStart);
          check("the kinds its area sees go on its recent-species list, 5 kept, oldest dropped (0x42e909)",
                q.recentSpecies==std::vector<int>({2,3,4,5,zid}));
          check("watching begun, a tile of the exhibit goes onto its visited list, its visit count 1 (0x42e527 -> 0x4a840e)",viewVisit);
          // Mimicking (0x425a15, 0x425b6d): only for a kind that has one -
          // the chimpanzee and gorilla bMimic, the roaring dinosaurs bMimic2
          auto &t=g.typeList[q.type];const int was=t.mimicChance;t.mimicChance=100;
          q.recentSpecies={zid,5001};const bool none=!g.mimic(q);
          q.recentSpecies={5003,zid};const bool back=g.mimic(q)&&q.anim=="mimic";
          q.recentSpecies={5021,5001,5097};const bool roar=g.mimic(q)&&q.anim=="mimic2";
          q.recentSpecies={5097,5003};const bool monkey=g.mimic(q)&&q.anim=="mimic";
          t.mimicChance=0;q.recentSpecies={5003};const bool never=!g.mimic(q);
          t.mimicChance=was;
          out<<"VIEW mimic: none "<<none<<", past a zebra "<<back<<", roar "<<roar<<", mimic "<<monkey<<", chance 0 "<<never<<"\n";
          check("a guest mimics the newest recent kind that has a mimic (0x425a15, 0x425b6d; cMimicChance)",
                none&&back&&roar&&monkey&&never);
          // The way's slot is only asked on zoo ground (0x43b8a2: the
          // habitat of its tile, slot 0x248, has +0x2c - not an exhibit)
          {
            const int way=q.viewWay;const float ox=q.x,oy=q.y;
            auto slots=g.areaInSlot;
            g.areaInSlot=[](int,int,int,Guests::ViewArea &o){o.seen={{0,0}};return true;};
            Guests::ViewArea ar;q.viewWay=0;
            q.x=bx+3.5f;q.y=by+3.5f;const bool inPad=g.viewArea(q,ar);
            q.x=bx-1.5f;q.y=by+4.5f;const bool onPath=g.viewArea(q,ar);
            g.areaInSlot=slots;q.viewWay=way;q.x=ox;q.y=oy;
            out<<"VIEW zoo ground: in the paddock "<<inPad<<", on the path "<<onPath<<"\n";
            check("a viewing area only on zoo ground, not in an exhibit (0x43b8a2: habitat +0x2c)",!inPad&&onPath);
          }
          // its time up: done (0x42ea14) - the time watched is game time
          q.viewStart=g.clock-12;const double before=g.takeExhibitBooks()[pad].viewed;(void)before;
          g.viewDone(q);
          const double viewed=g.takeExhibitBooks()[pad].viewed;
          check("done watching: the game time since it began goes on the exhibit's viewing time (0x42ea14)",
                std::abs(viewed-12)<1e-6&&q.target<0&&q.viewWay<0&&q.recheck);
        }
      }
      // (taken away again: the fixtures below start on the empty map)
      a.clear();st.clear();g.clear();
      for(int k=0;k<8;++k){f.remove({true,bx+k,by},map);f.remove({true,bx+k,by+8},map);f.remove({false,bx,by+k},map);f.remove({false,bx+8,by+k},map);}
      f.updateExhibits(map,1,0,1);
      for(int yy=by-3;yy<=by+10;++yy)for(int xx=bx-3;xx<=bx+10;++xx)map.setPath(xx,yy,"");
      va.clear();world.reindex();
    }
  }
  // Tapping a tank's glass while watching (zoo.exe 0x425a85): cTapWallChance
  // in 100; a tank wall on a side of its tile whose height is set (ZTFence
  // +0x464/+0x454: the tank's wall) - faces it and plays bTapTank (tap,
  // peek, map); its whileGoing looks again each update (0x42325d: it checks
  // 'bTapWall', not bTapTank) - the tap goes on only while it would tap again
  {
    int tx=-1,ty=-1;
    for(int y=map.getHeight()-12;y>=4&&tx<0;--y)for(int x=map.getWidth()-12;x>=4&&tx<0;--x){
      bool ok=true;
      for(int yy=y-3;yy<=y+7&&ok;++yy)for(int xx=x-3;xx<=x+7&&ok;++xx)ok=f.exhibitAt(xx,yy)<0&&f.insideZoo(xx,yy);
      if(ok){tx=x;ty=y;}
    }
    check("room for the glass-tapping tank",tx>=0);
    if(tx>=0) {
      const int chain=rectangle(tx+5,ty-2,1,1,f.typeIndex("chainlnk"),"Tap fence");
      const int tank=rectangle(tx,ty,4,4,f.typeIndex("atltank"),"Tap tank");
      for(int y=ty-1;y<=ty+4;++y)map.setPath(tx-1,y,"path");
      world.reindex();
      g.clear();g.setEntrance(tx-1,ty-1);g.admit(0);
      const Fences::Exhibit *ex=f.exhibit(tank);
      check("tank formed for the glass tapping ("+std::to_string(tank)+")",ex&&ex->tank&&ex->wallSub>0);
      if(!g.list.empty()&&ex) {
        auto &q=g.list.back();auto &t=g.typeList[q.type];
        const int was=t.tapWallChance,mim=t.mimicChance,zap=t.testFenceChance;
        t.mimicChance=0;t.testFenceChance=0;q.recentSpecies.clear();
        q.x=tx-0.5f;q.y=ty+1.5f;q.target=tank;q.state=Guests::State::Gawking;q.viewEnd=g.clock+60;q.viewStart=g.clock;
        q.fx=0;q.fy=1;
        t.tapWallChance=100;
        const bool taps=g.tapTank(q);
        const std::string first=q.anim;const bool faces=q.fx==1&&q.fy==0;
        q.animTime=0.03f;g.viewNext(q);
        const bool goesOn=q.tapStep==0&&q.anim==first&&q.animTime==0.03f;
        t.tapWallChance=0;g.viewNext(q);
        const bool replaced=q.tapStep<0&&(q.anim=="gawk1"||q.anim=="camera"||q.anim=="stand");
        t.tapWallChance=100;q.tapStep=-1;
        auto *wx=f.exhibit(tank);const int sub=wx->wallSub;wx->wallSub=0;
        const bool unset=!g.tapTank(q);wx->wallSub=sub;
        t.tapWallChance=0;const bool never=!g.tapTank(q);t.tapWallChance=100;
        q.x=tx+5.5f;q.y=ty-0.5f;const bool fence=!g.tapTank(q);
        q.x=tx-0.5f;q.y=ty+1.5f;
        const int ground=static_cast<int>(map.getTile(tx-1,ty+1)->height);
        out<<"TAP tank "<<tank<<" wall "<<sub<<" ground "<<ground<<": taps "<<taps<<" ("<<first<<", faces "<<faces
           <<"), goes on "<<goesOn<<", replaced "<<replaced<<", height unset "<<unset<<", chance 0 "<<never
           <<", chain-link "<<fence<<"\n";
        check("beside a tank's wall a guest taps the glass, facing it (0x425a85: bTapTank)",
              taps&&first==(t.tapSet.empty()?std::string():t.tapSet.front())&&faces);
        check("while it taps, each update looks again: tapping again goes on, else bGawk (0x42325d, 0x4a7e94)",goesOn&&replaced);
        check("no tap: wall height unset, cTapWallChance 0, a fence that isn't a tank wall (0x425a85)",unset&&never&&fence);
        t.tapWallChance=was;t.mimicChance=mim;t.testFenceChance=zap;
      }
      g.clear();
      for(int y=ty-1;y<=ty+4;++y)map.setPath(tx-1,y,"");
      for(int k=0;k<4;++k){f.remove({true,tx+k,ty},map);f.remove({true,tx+k,ty+4},map);f.remove({false,tx,ty+k},map);f.remove({false,tx+4,ty+k},map);}
      f.remove({true,tx+5,ty-2},map);f.remove({true,tx+5,ty-1},map);f.remove({false,tx+5,ty-2},map);f.remove({false,tx+6,ty-2},map);
      f.updateExhibits(map,1,0,1);world.reindex();(void)chain;
    }
  }
  // Objects' dead look (#240, zoo.exe 0x41038b; 0x4104cd -> showDead 0x44a6ae
  // / 0x410540): kelp (cDeadOnLand, cHeight 3) alive in a full tank, dead
  // once it's drained or the water's no higher than its cHeight, alive again
  // when it's filled; on land dead; a cSurface kind only once the water's
  // gone; the treasure chest (cDeadUnderwater, idledead = used) "dead" under
  // water, not drained; an aquatic cave (a building, cDeadOnLand) dying puts
  // its guests out (0x4174dd -> 0x5ae745)
  {
    int tx=-1,ty=-1;
    for(int y=map.getHeight()-12;y>=4&&tx<0;--y)for(int x=map.getWidth()-12;x>=4&&tx<0;--x){
      bool ok=true;
      for(int yy=y-1;yy<=y+5&&ok;++yy)for(int xx=x-1;xx<=x+7&&ok;++xx)ok=f.exhibitAt(xx,yy)<0&&f.insideZoo(xx,yy)&&!map.isPath(xx,yy);
      for(const auto &o:objects.objects())ok=ok&&!(o.x>x-2&&o.x<x+9&&o.y>y-2&&o.y<y+7);
      if(ok){tx=x;ty=y;}
    }
    check("room for the dead-look tank",tx>=0);
    if(tx>=0) {
      const int tank=rectangle(tx,ty,4,4,f.typeIndex("atltank"),"Dead look tank");
      Fences::Exhibit *ex=f.exhibit(tank);
      const size_t first=objects.objects().size();
      const bool kelp1=objects.add("scenery/foliage/kelp.ai",tx+1.0f,ty+1.0f,0),
                 chest=objects.add("scenery/other/trchest.ai",tx+3.0f,ty+3.0f,0),
                 kelp2=objects.add("scenery/foliage/kelp.ai",tx+6.0f,ty+1.0f,0),
                 cave=objects.add("scenery/building/aqcave.ai",tx+2.0f,ty+3.0f,0);
      const bool placed=ex&&ex->tank&&kelp1&&chest&&kelp2&&cave&&objects.objects().size()==first+4;
      out<<"DEAD LOOK fixture at "<<tx<<","<<ty<<" tank "<<tank<<" "<<(ex&&ex->tank)<<" kelp "<<kelp1<<kelp2<<" chest "<<chest<<" cave "<<cave
         <<" objects "<<objects.objects().size()-first<<"\n";
      check("dead-look fixture: a tank, kelp in it and on land, a treasure chest, an aquatic cave",placed);
      if(placed) {
        auto &list=objects.all();
        auto obj=[&](size_t k)->PlacedObjects::Object &{return list[first+k];};
        auto full=[&]{ex->filling=true;ex->water=1.0f;ex->level=-1e9f;world.updateDeadLooks();};
        // (as made, the walls 5 high and the water 4 - initialHeight 5,
        // waterOffset -1: the cave, cHeight 6, dead; then the walls raised to
        // 9, the water 8)
        full();
        const int standard=f.waterLevel(*ex);
        const bool caveShallow=obj(3).dead;
        const int wallWas=ex->wallSub;ex->wallSub=9;
        full();
        const int level=f.waterLevel(*ex);
        const bool flags=obj(0).deadOnLand&&!obj(0).deadOnFlatWater&&obj(0).height==3&&obj(0).deadArt&&
                         obj(1).deadUnderwater&&!obj(1).deadOnLand&&obj(1).deadArt;
        const bool aliveFull=!obj(0).dead&&PlacedObjects::displayArt(obj(0))==obj(0).art&&level>3;
        const bool chestUnder=obj(1).dead&&PlacedObjects::displayArt(obj(1))==obj(1).deadArt;
        const bool onLand=obj(2).dead;
        // (the water no higher than its cHeight, then a step above)
        obj(0).height=level;world.updateDeadLooks();const bool atHeight=obj(0).dead;
        obj(0).height=level-1;world.updateDeadLooks();const bool aboveHeight=!obj(0).dead;
        obj(0).height=3;
        // (a cSurface kind: alive at any level until the water's gone)
        obj(0).deadSurface=true;obj(0).height=99;world.updateDeadLooks();const bool surfaceKept=!obj(0).dead;
        // (a guest inside the cave as it dies)
        g.clear();g.setEntrance(tx-1,ty-1);g.admit(0);
        bool caveOut=false;
        if(!g.list.empty()) {
          auto &in=g.list[0];in.state=Guests::State::Using;in.building=static_cast<int>(first+3);in.hidden=true;
          in.need="x";in.animTime=0;in.playFor=30;
        }
        const bool caveAlive=!obj(3).dead;
        f.drainExhibit(tank);world.updateDeadLooks();
        if(!g.list.empty())caveOut=g.list[0].state!=Guests::State::Using&&!g.list[0].hidden;
        const bool drained=obj(0).dead&&PlacedObjects::displayArt(obj(0))==obj(0).deadArt&&!obj(1).dead&&obj(3).dead;
        obj(0).deadSurface=false;obj(0).height=3;
        full();
        const bool again=!obj(0).dead&&PlacedObjects::displayArt(obj(0))==obj(0).art&&obj(1).dead&&!obj(3).dead;
        ex->wallSub=wallWas;
        out<<"DEAD LOOK standard "<<standard<<" (wall "<<wallWas<<") caveShallow "<<caveShallow<<" level "<<level<<" flags "<<flags<<" aliveFull "<<aliveFull<<" chest "<<chestUnder<<" land "<<onLand
           <<" atHeight "<<atHeight<<" above "<<aboveHeight<<" surface "<<surfaceKept<<" drained "<<drained
           <<" caveOut "<<caveOut<<" again "<<again<<"\n";
        check("dead-look keys read: cDeadOnLand, cDeadUnderwater, cHeight, the idledead art (0x4ba5b0)",flags);
        check("kelp alive in a full tank, dead on land; the treasure chest's idledead under water (0x41038b)",
              aliveFull&&onLand&&chestUnder);
        check("kelp dead once the water's no higher than its cHeight, alive a step above (0x41038b: +0x188 <= +0x64)",
              atHeight&&aboveHeight);
        check("a cSurface kind kept alive while there's water (0x41038b: +0x131)",surfaceKept);
        check("drained: kelp and the cave dead, the chest not; filled again: alive again (0x4104cd, 0x410540)",
              caveAlive&&drained&&again);
        check("a building's dead look puts its guests out (ZTBuilding::showDead 0x4174dd -> 0x5ae745)",caveOut);
        check("a new exhibit tank's water is 4; the aquatic cave (cHeight 6) dead in it, alive with the walls at 9 (0x41038b)",
              standard==4&&caveShallow&&caveAlive);
        g.clear();
      }
      for(size_t k=objects.objects().size();k>first;--k)objects.remove(k-1);
      for(int k=0;k<4;++k){f.remove({true,tx+k,ty},map);f.remove({true,tx+k,ty+4},map);f.remove({false,tx,ty+k},map);f.remove({false,tx+4,ty+k},map);}
      f.updateExhibits(map,1,0,1);
      world.reindex();
    }
  }
  const int land=rectangle(26,28,10,10,f.typeIndex("chainlnk"),"Parity paddock");
  check("land fixture built",land>=0);
  if(land<0)return out.str();
  {
    // zoo.exe 0x4386a9 / 0x43c1be: own-kind crowding measures the room as
    // tiles / cHabitatSize, a fraction, never under 1 (30 tiles of 20 is room
    // for 1.5 x cNumberAnimalsMax, not 1 x)
    Animals::Type t;t.numberMax=2;t.habitatSize=20;
    check("own-kind crowding uses the original's fractional room (3 of max 2 in 30/20 tiles not crowded, 4 crowded)",
          !Animals::crowdedBySpecies(t,3,30) && Animals::crowdedBySpecies(t,4,30) && !Animals::crowdedBySpecies(t,2,5) &&
          Animals::crowdedBySpecies(t,3,5));
  }
  {
    // zoo.exe 0x434899: each fence piece's animation (the electric bars'
    // beacons) runs on its own clock from when it was put up, by the game
    // time passed - none while paused
    const int bars=f.typeIndex("dzbar");
    check("electric bar fence type available",bars>=0);
    if(bars>=0) {
      const Fences::Edge a{true,43,30},b{true,44,30};
      f.place(a,bars,drag++,map);f.update(0.5f);f.place(b,bars,drag++,map);f.update(0.25f);f.update(0.0f);
      const auto *pa=f.at(a),*pb=f.at(b);
      check("fence animations keep their own phase: put up 500 ms apart, 500 ms apart",
            pa && pb && std::abs((pa->animMs-pb->animMs)-500.0f)<0.5f && std::abs(pb->animMs-250.0f)<0.5f);
      f.remove(a,map);f.remove(b,map);
    }
  }
  {
    // zoo.exe 0x4479fd: the guests watching an animal - a viewing area of 20
    // guests whose picked tile is 3 tiles east: 20 -> 18 -> 16 -> 14 (a
    // tenth less a tile, truncated); east (way 2) puts it on +0x36c and
    // takes it off +0x370. Over its cCrowd at the social check (0x421ce8):
    // cCrowdHappinessChange, and the way away by the original's signs
    // (0x421d9c: +0x36c up, +0x370 down - 7); its next walk goes that way or
    // next to it (0x43e363 -> 0x42f6a9, 0x43e511) and forgets it
    const int zebra=a.typeOfFile("animals/zebra.ai");
    check("zebra type available for the guest crowd",zebra>=0);
    if(zebra>=0) {
      a.clear();
      const int id=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      Animals::Member *z=a.member(id);
      const auto hook=a.viewAreas;
      a.viewAreas=[&](int ex){
        Animals::ViewArea v;v.viewing={{33,32}};v.guests=20;
        if(const auto *e=f.exhibit(ex))v.seen.assign(e->tiles.begin(),e->tiles.end());
        return std::vector<Animals::ViewArea>{v};};
      Animals::Type &zt=a.typeList[zebra];const int keep=zt.crowd;
      if(z) {
        z->x=30.5f;z->y=32.5f;z->path.clear();z->pathAt=0;
        Animals::Member base=*z;base.socialCheck=0;base.pending=0;base.crowdDir=-1;
        auto run=[&](int crowd){*z=base;zt.crowd=crowd;const auto r=a.rng;a.tick(*z,map,f);a.rng=r;return *z;};
        const Animals::Member calm=run(1000),crowded=run(10);
        zt.crowd=keep;
        out<<"GUESTCROWD "<<crowded.guestCrowd<<" "<<crowded.guestCrowdX<<" "<<crowded.guestCrowdY<<" dir "<<crowded.crowdDir
           <<" pending "<<calm.pending<<" "<<crowded.pending<<"\n";
        check("guests watching: 20 guests 3 tiles off count 14, east of it (0x4479fd)",
              crowded.guestCrowd==14 && crowded.guestCrowdX==14 && crowded.guestCrowdY==-14);
        check("more watching than its cCrowd: cCrowdHappinessChange at the social check (0x421ce8)",
              crowded.pending-calm.pending==float(zt.crowdHappinessChange) && zt.crowdHappinessChange<0 && calm.crowdDir==-1);
        check("crowded: the way away by the original's signs (0x421d9c: 7)",crowded.crowdDir==7);
        *z=crowded;zt.crowd=10;
        bool red=false,said=false;
        std::string want=rm->getString(9957);
        if(size_t at=want.find("%s");at!=std::string::npos)want.replace(at,2,z->name);
        for(const auto &[line,isRed]:a.advice(id,map,f))if(line==want){said=true;red=isRed;}
        out<<"GUESTCROWD advice 9957: "<<want<<"\n";
        check("crowded by guests: the keeper's advice says so (9957, bit 56)",said && !want.empty() && red==(zt.crowdHappinessChange< -5));
        zt.crowd=keep;
        z->path.clear();z->pathAt=0;
        const bool walked=a.walkSomewhere(*z,map,f);
        bool away=walked && !z->path.empty();
        if(away){const auto [tx,ty]=z->path.back();away=Animals::nearDirection(7,Animals::direction8(int(std::floor(tx))-30,int(std::floor(ty))-32));}
        check("crowded: its next walk heads away (0x42f6a9: way 6, 7 or 0) and the way's forgotten",away && z->crowdDir==-1);
      }
      a.viewAreas=hook;
      a.clear();
    }
  }
  {
    // The behaviour-set runner. zoo.exe 0x437d9b reads a line: spaces and
    // quotes out, a piece per ',', an empty one kept but none after a
    // trailing ','. fPlaySetProb (0x43df67, 0x43e0d3) needs an even count
    // whose chances make exactly 100 and rolls 1..100, then what's left; a
    // line its handler can't run fails the sets it's in (0x435f0a,
    // 0x435e57 - measured: Mexican wolves leave bCalm1 at fPlay(lie_paw));
    // fPlay (0x440b92) plays each animation it names; fPlayTime (0x424ee7)
    // whole plays; a loose rampager's set is bEscapedRampage (0x437102)
    Animals::Step s1,s2,s3,s4;
    const bool p1=Animals::parseLine("fPlaySetProb(bTense1,70,bAngry3,30,)",s1);
    const bool p2=Animals::parseLine("fDie()",s2);
    const bool p3=Animals::parseLine("fPlay( stand , \"walk\")",s3);
    const bool p4=Animals::parseLine("fPlaySetProb(a,,b)",s4);
    check("behaviour lines read as 0x437d9b: trailing ',' adds nothing, '()' is one empty, spaces and quotes out",
          p1&&s1.fn=="fplaysetprob"&&s1.args.size()==4&&s1.args[3]=="30"&&p2&&s2.args.size()==1&&s2.args[0].empty()&&
          p3&&s3.args==std::vector<std::string>{"stand","walk"}&&p4&&s4.args.size()==3&&s4.args[1].empty());
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      a.clear();
      const int id=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      Animals::Member *z=a.member(id);
      Animals::Type &zt=a.typeList[zebra];
      auto setOf=[&](const std::vector<std::string> &lines){
        std::vector<Animals::Step> steps;
        for(const auto &l:lines){Animals::Step s;if(Animals::parseLine(l,s))steps.push_back(s);}
        return steps;};
      const auto keepSets=zt.sets[0];
      zt.sets[0]["bparityprob"]=setOf({"fPlaySetProb(bparitya,30,bparityb,70)"});
      zt.sets[0]["bparity80"]=setOf({"fPlaySetProb(bparitya,30,bparityb,50)","fPlay(stand)"});
      zt.sets[0]["bparityodd"]=setOf({"fPlaySetProb(bparitya,30,bparityb)","fPlay(stand)"});
      zt.sets[0]["bparitymiss"]=setOf({"fPlaySetProb(bparitya,30,bparitygone,70)"});
      zt.sets[0]["bparitya"]=setOf({"fPlayTime(stand,4)"});
      zt.sets[0]["bparityb"]=setOf({"fPlayTime(stand,4)"});
      zt.sets[0]["bparityplay"]=setOf({"fPlay(stand,nothere,walk)"});
      zt.sets[0]["bparitytime"]=setOf({"fPlayTime(stand)","fPlay(walk)"});
      zt.sets[0]["bparityanim"]=setOf({"fPlay(stand)","fPlay(nothere)","fPlay(walk)"});
      zt.sets[0]["bparityouter"]=setOf({"fPlaySet(bparityanim)","fPlay(walk)"});
      zt.sets[0]["bescapedrampage"]=setOf({"fPlayTime(stand,4)"});
      zt.sets[0]["bhabitatrampage"]=setOf({"fPlayTime(stand,4)"});
      zt.sets[0]["bparitysound"]=setOf({"fPlayWithSound(nothere,snort)","fPlay(walk)"});
      zt.sets[0]["bparitymove"]=setOf({"fMove(0,0,walk,30)"});
      zt.sets[0]["bparitymove5"]=setOf({"fMove(0,0,walk,30,200)"});
      zt.sets[0]["bparitywalk"]=setOf({"fWalk(0,0)"});
      zt.sets[0]["bparityrun"]=setOf({"fRun(0,0)"});
      zt.sets[0]["bparitytank"]=setOf({"fPlaySetTank(bparitya,bparityb)"});
      zt.sets[0]["bparitytank1"]=setOf({"fPlaySetTank(bparitya)","fPlay(stand)"});
      zt.sets[0]["bparitytankgone"]=setOf({"fPlaySetTank(bparitygone,bparityb)","fPlay(stand)"});
      zt.sets[0]["bparityterr4"]=setOf({"fPlaySetTerrain(bparitya,bparityb,bparityb,bparityb)"});
      zt.sets[0]["bparityterr2"]=setOf({"fPlaySetTerrain(bparitya,bparityb)"});
      zt.sets[0]["bparityterr1"]=setOf({"fPlaySetTerrain(bparitya)","fPlay(stand)"});
      zt.sets[0]["bparityterr3"]=setOf({"fPlaySetTerrain(bparitya,bparityb,bparityb)","fPlay(stand)"});
      zt.sets[0]["bparityterrgone"]=setOf({"fPlaySetTerrain(bparitygone,bparityb,bparityb,bparityb)","fPlay(stand)"});
      zt.sets[0]["bparitymovetime"]=setOf({"fMove(0,1,stand,30)"});
      zt.sets[0]["bparitymoverun"]=setOf({"fMove(0,1,run,30)"});
      zt.sets[0]["bparitymovetrot"]=setOf({"fMove(0,1,trot,30)"});
      zt.sets[0]["bparityfollow"]=setOf({"fFollow(keeper)","fPlay(walk)"});
      zt.sets[0]["bparityspray"]=setOf({"fSpray()","fPlay(walk)"});
      zt.sets[0]["bparityface0"]=setOf({"fFaceDirection()","fPlay(walk)"});
      zt.sets[0]["bparityface2"]=setOf({"fFaceDirection(1,2)","fPlay(walk)"});
      zt.sets[0]["bparityface1"]=setOf({"fFaceDirection(3)","fPlay(walk)"});
      if(z) {
        z->x=30.5f;z->y=32.5f;
        const int home=z->exhibit;
        auto start=[&](const char *set){a.labBehave(id,set);z->playQueue.clear();z->anim="parityheld";a.nextStep(*z,map,f);};
        int picksA=0,picksB=0;
        for(int i=0;i<2000;++i){
          start("bparityprob");
          const std::string top=z->stack.empty()?"":z->stack.back().first;
          picksA+=top=="bparitya";picksB+=top=="bparityb";
        }
        out<<"SETPROB 30/70 over 2000: "<<picksA<<" "<<picksB<<"\n";
        check("fPlaySetProb picks by its chances (30/70 within 4%: 0x43e0d3)",
              picksA+picksB==2000&&std::abs(picksA-600)<80);
        auto fails=[&](const char *set){start(set);return z->stack.empty()&&z->anim=="parityheld";};
        check("fPlaySetProb whose chances don't make 100 fails its set, its animation held (0x43df67)",fails("bparity80"));
        check("fPlaySetProb with an odd count fails its set (0x43df67)",fails("bparityodd"));
        check("fPlayTime with one argument fails its set (0x424e7f)",fails("bparitytime"));
        // fPlaySetTank (0x5b2be5): two arguments; out of a tank (water
        // state not 4) the first; one argument, or a set it hasn't, fails
        start("bparitytank");
        const bool tankFirst=!z->stack.empty()&&z->stack.back().first=="bparitya";
        check("fPlaySetTank plays its first set out of the water; one argument or a set it hasn't fails (0x5b2be5)",
              tankFirst&&fails("bparitytank1")&&fails("bparitytankgone"));
        // fPlaySetTerrain (the runner's slot 0x14: 0x4314d6, else the base's
        // 0x601d2d): four arguments, on land the first; two, off water the
        // first; one or three fail the line; a set it hasn't fails too
        start("bparityterr4");
        const bool terr4=!z->stack.empty()&&z->stack.back().first=="bparitya";
        start("bparityterr2");
        const bool terr2=!z->stack.empty()&&z->stack.back().first=="bparitya";
        check("fPlaySetTerrain on land: the first of four or of two; one or three arguments, or a set it hasn't, fail (0x4314d6, 0x601d2d)",
              terr4&&terr2&&fails("bparityterr1")&&fails("bparityterr3")&&fails("bparityterrgone"));
        // A name neither behaviourRunFn 0x436065 nor behaviourRunFnMoves
        // 0x437ccd knows (the stock fFollow) fails its line; one it knows
        // that ours doesn't run yet (fSpray) is passed over
        start("bparityspray");
        const bool sprayOn=z->anim=="walk";
        check("a line no handler knows (fFollow) fails its set; a known one we don't run (fSpray) is passed over (0x437ccd)",
              fails("bparityfollow")&&sprayOn);
        // fFaceDirection 0x601dbc: one argument, not empty - nothing done
        // (0x60cac6), the line done; none or two fail the line
        start("bparityface1");
        const bool faceOn=z->anim=="walk";
        check("fFaceDirection: one argument passes (does nothing), none or two fail the line (0x601dbc, 0x60cac6)",
              faceOn&&fails("bparityface0")&&fails("bparityface2"));
        int missA=0,missFail=0;
        for(int i=0;i<1000;++i){
          start("bparitymiss");
          missA+=z->stack.size()==2&&z->stack.back().first=="bparitya";
          missFail+=z->stack.empty();
        }
        out<<"SETPROB missing set 30/70 over 1000: picked "<<missA<<" failed "<<missFail<<"\n";
        check("fPlaySetProb picking a set it hasn't fails its set (0x43ddc1), else plays its pick",
              missA+missFail==1000&&std::abs(missA-300)<60);
        start("bparityplay");
        const std::string first=z->anim;
        z->animTime=z->playFor;a.nextStep(*z,map,f);
        const std::string second=z->anim;
        check("fPlay plays each animation it names in turn, missing ones passed over (0x440b92)",
              first=="stand"&&second=="walk");
        start("bparityouter");
        const bool stood=z->anim=="stand"&&z->stack.size()==2;
        z->animTime=z->playFor;a.nextStep(*z,map,f);
        check("fPlay of an animation it hasn't fails its set and the set that played it (0x440b92, 0x435e57)",
              stood&&z->stack.empty()&&z->anim=="stand");
        start("bparitya");
        const float loop=a.animSeconds(*z,"stand");
        float want=loop;for(float left=4.0f-loop;left>=0.001f&&loop>0;left-=loop)want+=loop;
        check("fPlayTime plays whole loops: 4 s rounded up to whole plays of stand (0x424ee7)",
              z->anim=="stand"&&loop>0&&std::abs(z->playFor-want)<0.01f&&z->playFor>=4.0f-0.001f);
        z->stack.clear();z->playQueue.clear();z->escaped=true;z->rampaging=true;z->exhibit=-1;
        a.nextStep(*z,map,f);
        check("loose and rampaging: bEscapedRampage first (0x437102)",
              !z->stack.empty()&&z->stack.front().first=="bescapedrampage");
        check("fPlayWithSound of an animation it hasn't fails its set (0x4383bb)",fails("bparitysound"));
        z->escaped=false;z->rampaging=false;z->exhibit=home;
        start("bparitymove");
        out<<"FMOVE 30: "<<z->speed<<" tiles/s\n";
        check("fMove's speed by the units' step rule: 30 is 2 steps an update, 0.466 tiles/s (0x425533, 0x435c42)",
              std::abs(z->speed-Original::unitTilesPerSecond(30))<1e-5f&&std::abs(z->speed-2000.0f/67/64)<1e-4f);
        // The move's animation rate (unit +0x1fc: 0x425533's 5th argument,
        // 100 without; fWalk/fRun the type's cSlowAnimSpeed/cFastAnimSpeed,
        // 0x4136bf, 0x4a5194), the animation run at it while it moves
        // (0x435bbe)
        const int rate4=z->animRate;
        start("bparitymove5");
        const int rate5=z->animRate;
        float advanced=-1;
        if(z->pathAt<z->path.size()){
          z->anim=z->animRateFor;const float t0=z->animTime;
          a.update(0.1f,a.now+0.1,map,f);
          if(z->anim==z->animRateFor)advanced=z->animTime-t0;
        }
        const auto keepSpeeds=std::make_pair(zt.animSpeed[0],zt.animSpeed[2]);
        zt.animSpeed[0]=125;zt.animSpeed[2]=200;
        start("bparitywalk");const int walkRate=z->animRate;
        start("bparityrun");const int runRate=z->animRate;
        zt.animSpeed[0]=keepSpeeds.first;zt.animSpeed[2]=keepSpeeds.second;
        int walrusSlow=-1,walrusFastWet=-1;
        if(const int w=a.typeOfFile("animals/walrus.ai");w>=0){walrusSlow=a.typeList[w].animSpeed[0];walrusFastWet=a.typeList[w].animSpeedWater[2];}
        out<<"ANIM RATE fMove4 "<<rate4<<" fMove5 "<<rate5<<" advanced "<<advanced<<" walk "<<walkRate<<" run "<<runRate
           <<" walrus slow "<<walrusSlow<<" fast water "<<walrusFastWet<<"\n";
        check("a move's animation rate: fMove's 5th (100 without), fWalk/fRun the type's cSlow/cFastAnimSpeed, played at it moving (0x425533, 0x435bbe)",
              rate4==100&&rate5==200&&std::abs(advanced-0.2f)<0.002f&&walkRate==125&&runRate==200&&
              (walrusSlow<0||(walrusSlow==125&&walrusFastWet==200)));
        // fMove's time (0x425533 x 1000, 0x43646e): its animation for
        // ceil(time / its length) whole plays, then its plain move (slot
        // 0x134) the rest of the way; without one, the whole move
        {
          const float len=a.animSeconds(*z,"stand");
          const int plays=len>0?std::max(1,int(std::ceil(1.0f/len-1e-6f))):1;
          start("bparitymovetime");
          const float left=z->moveAnimLeft;
          bool timed=z->moveAnim=="stand"&&std::abs(left-plays*len)<0.001f;
          float switched=-1,moved=0;
          if(timed && z->pathAt<z->path.size()){
            z->path={{z->x+6.0f,z->y}};z->pathAt=0;z->anim="stand";z->animRateFor="stand";
            for(int i=0;i<200&&z->pathAt<z->path.size();++i){
              a.update(0.05f,a.now+0.05,map,f);moved+=0.05f;
              if(z->anim=="walk"){switched=moved;break;}
            }
          }
          const std::string afterFor=z->animRateFor;
          start("bparitymove");
          const bool whole=z->moveAnimLeft<0;
          out<<"FMOVE TIME 1 s: stand "<<len<<" s, plays "<<plays<<" left "<<left<<" switched after "<<switched<<"\n";
          check("fMove's time: its animation for whole plays (ceil(time / length)), then its plain walk at the same rate; none, the whole move (0x43646e)",
                timed&&switched>0&&std::abs(switched-plays*len)<0.051f&&afterFor=="walk"&&whole);
          // (#242: the plain move by the line's speed - 0x4a9bb9: fMove(.., run)
          // the run, fMove(.., trot) the trot, any other the walk)
          start("bparitymoverun");const std::string afterRun=z->moveAnimAfter;
          start("bparitymovetrot");const std::string afterTrot=z->moveAnimAfter;
          start("bparitymovetime");const std::string afterStand=z->moveAnimAfter;
          const bool zebraTrots=(zt.anims[a.sub(*z)].count("trot")||zt.anims[0].count("trot"));
          out<<"FMOVE PLAIN run "<<afterRun<<" trot "<<afterTrot<<" stand "<<afterStand<<"\n";
          check("#242: after a timed fMove, the plain move by its speed: run for \"run\", trot for \"trot\", else the walk (0x4a9bb9, 0x4a9b17/0x4a9f38/0x4aa05a)",
                afterRun=="run"&&afterTrot==(zebraTrots?"trot":"run")&&afterStand=="walk");
        }
        // A shelter (animalCheckBuildingUse 0x438f20 -> findNearestOfKindB
        // 0x4290f3, 0x429330/0x42ac87): an "animalrest" building in its own
        // habitat with a slot with room for it - its users (+0x18c) those
        // in it, under cCapacity and the slot's capacity, its id list
        // holding its kind - the nearest by whole tiles, ties at random;
        // that sets its goal, which chooseTask takes up later (0x43735c ->
        // 0x43b2f6: off to the slot's entrance, 0x42c02b); one full by the
        // time it gets there it doesn't go in. The zebra (5004) may use a
        // lean-to (ids 5004...), not a burrow (5042, 5043)
        {
          const size_t before=objects.objects().size();
          const bool placed=objects.add("scenery/building/leanto1.ai",32.5f,32.0f,0)&&
                            objects.add("scenery/building/leanto1.ai",30.5f,34.0f,0)&&
                            objects.add("scenery/building/leanto1.ai",34.5f,35.0f,0)&&
                            objects.add("scenery/building/burrow1.ai",31.5f,31.5f,0);
          int near1=-1,near2=-1,far=-1,burrow=-1;
          if(placed){near1=objects.objects()[before].id;near2=objects.objects()[before+1].id;far=objects.objects()[before+2].id;
                     burrow=objects.objects()[before+3].id;}
          std::vector<int> extra;
          for(int k=0;k<4;++k)extra.push_back(a.adopt(zebra,false,27.5f,29.5f+k,map,f,true));
          z=a.member(id);
          auto others=[&](int building,bool inside,int second=-1){
            for(size_t k=0;k<extra.size();++k)if(auto *m=a.member(extra[k])){
              m->building=k<2?building:second;m->restSlot=0;m->inside=inside&&(k<2||second>=0);m->stack.clear();m->path.clear();}};
          auto reset=[&](){z=a.member(id);z->x=30.5f;z->y=32.5f;z->building=-1;z->buildingGoal=-1;z->restSlot=-1;z->inside=false;
                           z->stack.clear();z->path.clear();z->pathAt=0;z->nextSet.clear();};
          auto picks=[&](int n){
            std::map<int,int> got;
            for(int i=0;i<n;++i){reset();a.useShelter(*z,map,f);got[z->buildingGoal]++;}
            return got;};
          others(near1,false);
          auto open=picks(400);
          others(near1,true);
          auto oneFull=picks(100);
          others(near1,true,near2);
          const int toFar=picks(20)[far];
          // (the check sets only the goal; its next choice sets off, to the
          // lean-to's entrance: entrpos 0,80 - 1.25 tiles in front of it, tile 32,30)
          others(near1,false);
          bool goalOnly=false,setOff=false;
          for(int i=0;i<50&&!goalOnly;++i){
            reset();a.useShelter(*z,map,f);
            goalOnly=z->buildingGoal==near1&&z->building==-1&&z->path.empty();
          }
          if(goalOnly){
            z->hunger=0;z->sleeping=false;z->energy=0;z->rampaging=false;z->escaped=false;
            a.chooseBehaviour(*z);z=a.member(id);
            setOff=z->buildingGoal==-1&&z->building==near1&&z->restSlot==0&&!z->path.empty()&&
                   int(std::floor(z->path.back().first))==32&&int(std::floor(z->path.back().second))==30;
          }
          // (at the shelter, it full by then: not in)
          others(near1,true);
          z=a.member(id);z->x=32.5f;z->y=31.5f;z->building=near1;z->restSlot=0;z->inside=false;z->stack.clear();
          z->path={{32.51f,31.5f}};z->pathAt=0;z->stepping=true;z->anim=z->moveAnim="walk";
          a.update(0.05f,a.now+0.05,map,f);
          z=a.member(id);
          const bool notIn=z&&!z->inside&&z->building==-1;
          // (a slot's filter turns away the letters it holds - "y" the
          // young, monkbar2's "yf" the females too - 0x42ae68 -> 0x409ee3;
          // a return slot never an animal's)
          bool filtered=false;
          if(placed){
            Animals::ObjectInfo info;info.capacity=4;info.slots.resize(1);info.slots[0].capacity=4;info.slots[0].filter="yf";
            const PlacedObjects::Object &o=objects.objects()[before+2];
            Animals::Member male=*z,female=*z,young=*z;
            male.female=false;male.baby=false;female.female=true;female.baby=false;young.female=false;young.baby=true;
            const bool mOk=a.slotHasRoom(o.id,info,0,male),fOk=a.slotHasRoom(o.id,info,0,female),yOk=a.slotHasRoom(o.id,info,0,young);
            info.slots[0].filter.clear();info.slots[0].isReturn=true;
            filtered=mOk&&!fOk&&!yOk&&!a.slotHasRoom(o.id,info,0,male);
          }
          out<<"SHELTER picks over 400 (others walking to one): "<<open[near1]<<" / "<<open[near2]<<" / far "<<open[far]
             <<" / burrow "<<open[burrow]<<"; one full: "<<oneFull[near2]<<" of 100; both full: far "<<toFar
             <<"; goal only "<<goalOnly<<", set off to the entrance "<<setOff<<"; full on arrival, in: "<<!notIn
             <<"; filter/return slot "<<filtered<<"\n";
          check("a shelter: the nearest with a slot with room for it (its kind in the id list, under cCapacity and the slot's), ties at random; full on arrival, not in (0x4290f3, 0x42ac87, 0x43b2f6)",
                placed&&open[near1]+open[near2]==400&&std::abs(open[near1]-200)<50&&open[burrow]==0&&oneFull[near2]==100&&toFar==20&&notIn);
          check("#241: the building check sets a goal only; its next choice takes it to the slot's entrance (0x438f20, 0x43735c -> 0x43b2f6, 0x42c02b)",
                goalOnly&&setOff);
          check("#241: a slot's filter turns away its letters (y young, f female), a return slot never an animal's (0x42ae68)",filtered);
          for(int o:extra)a.remove(o);
          while(objects.objects().size()>before)objects.remove(static_cast<int>(objects.objects().size())-1);
          z=a.member(id);
        }
        // Sleep (0x437102 -> 0x440a37, 0x4384ba, 0x4378df): tired at a set's
        // end, no set till the tiredness check (its countdown one an update)
        // asks for bSleep, which starts then - on land, not in water mode; a
        // rampage comes first; asleep its energy doesn't rise, and woken it's
        // back to 0; awake where it can't lie down it rises to cMaxEnergy
        auto fresh=[&](){z->stack.clear();z->playQueue.clear();z->path.clear();z->pathAt=0;z->stepping=false;
                         z->sleeping=false;z->rampaging=false;z->rampageTicks=0;z->sick=false;z->hunger=0;
                         z->waterMode=false;z->showTank=-1;z->huntAnimal=-1;z->fleeFrom=-1;z->nextSet.clear();
                         z->energyCheck=0;z->health=float(zt.maxHits);};
        fresh();z->energy=float(zt.energyThreshold);a.nextStep(*z,map,f);
        const bool slept=z->sleeping&&!z->stack.empty()&&z->stack.front().first=="bsleep"&&z->nextSet.empty();
        fresh();z->waterMode=true;z->energy=float(zt.energyThreshold);a.chooseBehaviour(*z);
        const bool notInWater=!z->sleeping&&!z->stack.empty()&&z->stack.back().first!="bsleep";
        check("tired: it sleeps on land, not in water mode (0x440a37)",slept&&notInWater);
        fresh();z->energy=float(zt.energyThreshold);z->energyCheck=5;z->anim="parityheld";a.nextStep(*z,map,f);
        const bool waited=z->stack.empty()&&!z->sleeping&&z->energyCheck==4&&z->anim=="parityheld";
        z->energyCheck=0;a.nextStep(*z,map,f);
        check("tired at a set's end: no set till the tiredness check's countdown is out, then bSleep (0x437102 -> 0x4384ba)",
              waited&&z->sleeping&&!z->stack.empty()&&z->stack.front().first=="bsleep");
        fresh();z->stack={{"bparitya",0}};z->energy=float(zt.energyThreshold);a.tick(*z,map,f);
        const bool asked=z->sleeping&&z->nextSet=="bsleep"&&z->stack.front().first=="bparitya";
        z->energyCheck=0;a.tick(*z,map,f);
        const bool stillAsked=z->sleeping&&z->nextSet=="bsleep";
        z->stack.clear();a.nextStep(*z,map,f);
        check("bSleep asked for mid-set waits for the set's end, then starts (0x4384ba, 0x4300f6, 0x435cfb)",
              asked&&stillAsked&&!z->stack.empty()&&z->stack.front().first=="bsleep"&&z->nextSet.empty());
        fresh();z->rampaging=true;z->energy=float(zt.energyThreshold);a.chooseBehaviour(*z);
        check("rampaging and tired: its rampage set, not a sleep (0x437102)",
              !z->sleeping&&!z->stack.empty()&&z->stack.back().first=="bhabitatrampage");
        fresh();z->energy=float(zt.energyThreshold);a.nextStep(*z,map,f);
        const float asleepAt=z->energy;z->energyCheck=0;a.tick(*z,map,f);
        const bool held=z->sleeping&&z->energy==asleepAt;
        z->stack={{"bsleep",a.set(*z,"bsleep")?a.set(*z,"bsleep")->size():0}};z->playQueue.clear();z->stepping=false;
        a.nextStep(*z,map,f);
        const bool woke=!z->sleeping&&z->energy==0;
        // (a zebra's cEnergyThreshold is its cMaxEnergy, 100: halved here so
        // the cap is told from the threshold; in water mode, where it can't
        // lie down, so it isn't asked to sleep)
        const int keepThreshold=zt.energyThreshold;zt.energyThreshold=zt.maxEnergy/2;
        fresh();z->waterMode=true;z->stack={{"bparitya",0}};z->energy=float(zt.maxEnergy-1);a.tick(*z,map,f);
        zt.energyThreshold=keepThreshold;
        out<<"ENERGY asleep "<<asleepAt<<" held "<<held<<" woke "<<woke<<" capped "<<z->energy<<" of "<<zt.maxEnergy<<"\n";
        check("asleep its energy holds, woken it's 0; awake it rises to cMaxEnergy, past the threshold (0x4384ba, 0x4378df)",
              held&&woke&&z->energy==float(zt.maxEnergy));
        // Sick (0x4389aa): poorly, bSick asked for next and cSickChange once;
        // not again while it's asked for or running, nor over a bSleep asked
        // for; where it can't lie down, cSickChange every check, no bSick
        auto healthTick=[&](){z->healthCheck=0;z->energyCheck=1000;z->hungerCheck=1000;z->habitatCheck=1000;
                              z->captivityCheck=1000;z->socialCheck=1000;z->otherCheck=1000;
                              a.tick(*z,map,f);return z->pending;}; // (a tick's checks' sum)
        fresh();z->sick=true;z->health=0;z->stack={{"bparitya",0}};
        const float sickOnce=healthTick();const bool sickAsked=z->nextSet=="bsick";
        const float sickAgain=healthTick();
        z->stack.clear();a.nextStep(*z,map,f);
        const bool sickStarted=!z->stack.empty()&&z->stack.front().first=="bsick";
        const float inSick=healthTick();
        fresh();z->sick=true;z->health=0;z->stack={{"bparitya",0}};z->nextSet="bsleep";
        const float overSleep=healthTick();
        fresh();z->sick=true;z->health=0;z->waterMode=true;z->stack={{"bparitya",0}};
        const float wet1=healthTick(),wet2=healthTick();
        out<<"SICK first "<<sickOnce<<" second "<<sickAgain<<" in bSick "<<inSick<<" over bSleep "<<overSleep
           <<" water "<<wet1<<" "<<wet2<<" (cSickChange "<<zt.sickChange<<")\n";
        check("poorly: bSick next with cSickChange once, none while asked for, in it or over bSleep; can't lie down, every check (0x4389aa)",
              sickAsked&&sickStarted&&sickOnce-sickAgain==float(zt.sickChange)&&inSick==sickAgain&&overSleep==sickAgain&&
              wet1-sickAgain==float(zt.sickChange)&&wet2==wet1&&z->nextSet.empty());
        fresh();
      }
      // fKeepControl (behaviourRunFnMoves 0x4a751e): true while it has a
      // unit, its line never done - the set holds on its base, sick or not,
      // till a set is started over it. Healed, a cStaySick kind (the
      // sharks) starts bDoneSick at once (Job4_HealAnimal::atTarget
      // 0x5eccd0); any other carries on
      if(z){
        zt.sets[0]["bparitykeep"]=setOf({"fSetBase(stand)","fKeepControl()"});
        zt.sets[0]["bdonesick"]=setOf({"fPlayTime(stand,1)"});
        z->stack.clear();z->playQueue.clear();z->path.clear();z->pathAt=0;z->stepping=false;z->sick=false;
        z->dying=false;z->nextSet.clear();
        a.pushSet(*z,"bparitykeep");
        for(int k=0;k<5;++k){z->stepping=false;a.nextStep(*z,map,f);}
        const bool held=!z->stack.empty()&&z->stack.back().first=="bparitykeep";
        const bool wasStay=zt.staySick;
        zt.staySick=false;a.heal(z->id);
        const bool carriesOn=!z->stack.empty()&&z->stack.back().first=="bparitykeep";
        zt.staySick=true;a.heal(z->id);
        const bool doneSick=!z->stack.empty()&&z->stack.back().first=="bdonesick";
        zt.staySick=wasStay;
        bool sharkStays=false;
        if(const int gw=a.typeOfFile("animals/gwshark.ai");gw>=0)sharkStays=a.typeList[gw].staySick&&!zt.staySick;
        out<<"KEEPCONTROL held "<<held<<" carries on "<<carriesOn<<" bDoneSick "<<doneSick<<" shark cStaySick "<<sharkStays<<"\n";
        check("fKeepControl holds its set, sick or not, till one is started over it (0x4a751e)",held&&carriesOn);
        check("healed, a cStaySick kind starts bDoneSick at once; the great white has cStaySick (0x5eccd0, 0x4b6c51)",
              doneSick&&sharkStays);
        z->stack.clear();z->stepping=false;
      }
      zt.sets[0]=keepSets;
      a.clear();
    }
  }
  {
    // Air (zoo.exe 0x4389f7, 0x43750c, Task 0x7c): a dolphin (cBreathIncrement
    // 10, cBreathThreshold 30) holds its breath longer at each check under
    // the water; a check that comes round while it's at the surface leaves
    // the countdown running below 0, and its breath never rises again; at
    // the threshold under the water, it goes up and plays bBreathe
    const int dol=a.typeOfFile("animals/dolphin.ai");
    if(dol>=0) {
      const auto keepTankAt=a.tankAt;const auto keepDepth=a.tankDepth;
      a.tankAt=[](float,float){return 0;};a.tankDepth=[](int){return 6.0f;};
      const Animals::Type &dt=a.typeList[dol];
      Animals::Member d;d.id=900002;d.type=dol;d.x=30.5f;d.y=32.5f;d.swimZ=1.0f;
      d.breathCheck=0;a.breathTick(d);
      const bool rose=d.breath==dt.breathIncrement&&d.breathCheck==29;
      d.breathCheck=0;d.swimZ=5.9f;a.breathTick(d);const int atTop=d.breath;
      d.swimZ=1.0f;for(int k=0;k<200;++k)a.breathTick(d);
      const bool stuck=d.breath==atTop&&d.breathCheck<0;
      out<<"BREATH dolphin inc "<<dt.breathIncrement<<" threshold "<<dt.breathThreshold<<" rose "<<rose
         <<" stuck "<<stuck<<" countdown "<<d.breathCheck<<"\n";
      check("breath: +cBreathIncrement at its check under the water; checked at the surface, never again (0x4389f7)",
            dt.breathIncrement==10&&dt.breathThreshold==30&&rose&&stuck);
      d.breath=dt.breathThreshold;d.stack.clear();a.chooseBehaviour(d);
      check("out of air under the water: up to breathe, bBreathe (0x43750c, Task 0x7c)",
            d.surfacing==1&&!d.stack.empty()&&d.stack.back().first=="bbreathe");
      a.tankAt=keepTankAt;a.tankDepth=keepDepth;
    }
  }
  {
    // The in-exhibit hunt (zoo.exe chooseTask 0x436f5c): started only when
    // class slot 0x288 (0x420fed) says the hunter isn't busy with itself -
    // a lion asleep (bSleep its set) beside a zebra starts no chase, awake
    // it does (#200)
    const int zebra=a.typeOfFile("animals/zebra.ai"),lion=a.typeOfFile("animals/lion.ai");
    if(zebra>=0&&lion>=0) {
      a.clear();
      const int lid=a.adopt(lion,true,30.5f,32.5f,map,f,true); // (a lioness: cPrey is under f/)
      const int zid=a.adopt(zebra,false,31.5f,32.5f,map,f,true);
      Animals::Member *l=a.member(lid);
      const Animals::Member *zz=a.member(zid);
      if(l&&zz&&l->exhibit==zz->exhibit) {
        auto huntTick=[&](bool asleep){
          l->stack.clear();l->playQueue.clear();l->path.clear();l->pathAt=0;l->stepping=false;l->huntAnimal=-1;
          l->fleeFrom=-1;l->nextSet.clear();l->sleeping=asleep;l->hunger=0;l->x=30.5f;l->y=32.5f;
          if(asleep)l->stack={{"bsleep",0}};else l->stack={{"bparitya",0}};
          l->huntCheck=1;l->healthCheck=l->energyCheck=l->hungerCheck=l->habitatCheck=1000;
          l->captivityCheck=l->socialCheck=l->otherCheck=l->waterCheck=l->boredCheck=l->buildingCheck=1000;
          a.tick(*l,map,f);
          return l->huntAnimal;};
        const auto keepSets=a.typeList[lion].sets[0];
        a.typeList[lion].sets[0]["bparitya"]={};
        const int whileAsleep=huntTick(true),whileAwake=huntTick(false);
        a.typeList[lion].sets[0]=keepSets;
        out<<"HUNT GATE lion asleep -> "<<whileAsleep<<", awake -> "<<whileAwake<<" (zebra "<<zid<<", its id "
           <<a.typeList[zebra].nameId<<", the lioness's prey "<<a.typeList[lion].preyIds[1].size()<<")\n";
        check("a lion asleep beside a zebra starts no chase, awake it does (0x436f5c, slot 0x288 0x420fed)",
              whileAsleep<0&&whileAwake==zid);
      } else check("lion and zebra share the hunt fixture's exhibit",false);
      a.clear();
    }
  }
  {
    // A drink (zoo.exe Task 0x7a, whileGoing 0x4a0c55, atTarget 0x4265a0):
    // the habitat's nearest water tile, the species' bFindOtherFood -
    // fWalk(otherfood,0) - to it, then bEat there (#201)
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      a.clear();
      const int id=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      Animals::Member *z=a.member(id);
      const Fences::Exhibit *ex=z?f.exhibit(z->exhibit):nullptr;
      int wx=-1,wy=-1;
      if(ex)for(auto [x,y]:ex->tiles){
        const int d=std::abs(x-30)+std::abs(y-32);
        if(d>=3&&d<=4&&wx<0){wx=x;wy=y;}
      }
      MapTile *tile=wx>=0?map.getTileMutable(wx,wy):nullptr;
      if(z&&tile) {
        const auto was=tile->terrainType;tile->terrainType=9;
        Animals::Type &zt=a.typeList[zebra];const int keepDrink=zt.drinkWaterChance;
        if(zt.drinkWaterChance<=0)zt.drinkWaterChance=1;
        z->stack.clear();z->hunger=0;z->boredCheck=1;z->waterCheck=1000;z->huntAnimal=-1;z->fleeFrom=-1;
        a.tick(*z,map,f);
        const bool sent=z->drinkX==wx+0.5f&&z->drinkY==wy+0.5f&&!z->stack.empty()&&z->stack.back().first=="bfindotherfood";
        bool walked=false,ate=false;float nearest=1e9f;
        for(int k=0;k<1200&&!ate;++k){
          z->waterCheck=1000;z->boredCheck=1000;z->hunger=0;
          a.update(0.05f,a.now+0.05,map,f);
          if(!a.member(id))break;
          const float dw=std::hypot(z->x-(wx+0.5f),z->y-(wy+0.5f));
          nearest=std::min(nearest,dw);
          walked=walked||(!z->stack.empty()&&z->stack.back().first=="bfindotherfood"&&z->moveAnim=="walk"&&!z->path.empty());
          ate=!z->stack.empty()&&z->stack.back().first=="beat"&&dw<1.0f;
        }
        out<<"DRINK water "<<wx<<","<<wy<<" sent "<<sent<<" walked "<<walked<<" nearest "<<nearest<<" ate "<<ate<<"\n";
        check("a zebra sent to drink plays bFindOtherFood to its habitat's water, then bEat there (0x4a0c55, 0x4265a0)",
              sent&&walked&&ate);
        zt.drinkWaterChance=keepDrink;tile->terrainType=was;
      } else check("water tile for the drinking fixture",false);
      a.clear();
    }
  }
  {
    // Dung (zoo.exe 0x438a79, 0x4378df, 0x4a8f89): the meter is looked at
    // before this check's cDirtyIncrement goes on, kept to cMaxDirty; full
    // on land, a pile and back to 0 (then the increment); on water nothing,
    // the meter stays full; in its tank no pile, the purity less the type's
    // cPooWaterImpact (dolphin.ai 5, the type default 1 - 0x4b659e) (#191)
    const int zebra=a.typeOfFile("animals/zebra.ai"),dolphin=a.typeOfFile("animals/dolphin.ai");
    if(zebra>=0&&dolphin>=0) {
      a.clear();
      const int id=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      Animals::Member *z=a.member(id);
      Fences::Exhibit *home=z?f.exhibit(z->exhibit):nullptr;
      MapTile *tile=map.getTileMutable(30,32);
      if(z&&home&&tile) {
        const Animals::Type &zt=a.typeList[zebra];
        ZooItems &items=world.getItems();
        auto piles=[&]{return items.ofKind(ZooItems::Kind::Dung,z->exhibit).size();};
        auto dungTick=[&](float dirty){
          z->dirty=dirty;z->hunger=0;z->hungerCheck=0;z->healthCheck=z->energyCheck=z->habitatCheck=1000;
          z->captivityCheck=z->socialCheck=z->otherCheck=z->waterCheck=z->boredCheck=z->buildingCheck=1000;
          z->huntCheck=1000;z->x=30.5f;z->y=32.5f;
          a.tick(*z,map,f);};
        const size_t before=piles();
        dungTick(float(zt.dirtyThreshold-1));
        const bool notYet=piles()==before&&z->dirty==float(std::min(zt.maxDirty,zt.dirtyThreshold-1+zt.dirtyIncrement));
        dungTick(float(zt.dirtyThreshold));
        const bool pile=piles()==before+1&&z->dirty==float(zt.dirtyIncrement);
        const auto was=tile->terrainType;tile->terrainType=9;
        dungTick(float(zt.maxDirty));
        const bool water=piles()==before+1&&z->dirty==float(zt.maxDirty);
        tile->terrainType=was;
        const auto keepTankAt=a.tankAt;const auto keepSet=a.setTankPurity;const auto keepGet=a.tankPurity;
        float purity=80.0f;const int ownTank=z->exhibit;int setOn=-1;
        a.tankAt=[ownTank](float,float){return ownTank;};
        a.tankPurity=[&](int){return purity;};a.setTankPurity=[&](int t,float p){setOn=t;purity=p;};
        home->tank=true;
        dungTick(float(zt.dirtyThreshold));
        home->tank=false;
        const bool tank=piles()==before+1&&setOn==ownTank&&purity==80.0f-float(zt.pooWaterImpact)&&
                        z->dirty==float(zt.dirtyIncrement);
        a.tankAt=keepTankAt;a.setTankPurity=keepSet;a.tankPurity=keepGet;
        out<<"DUNG notYet "<<notYet<<" pile "<<pile<<" water "<<water<<" tank "<<tank<<" purity "<<purity
           <<" impact zebra "<<zt.pooWaterImpact<<" dolphin "<<a.typeList[dolphin].pooWaterImpact<<"\n";
        check("dung: the meter before this check's increment; a pile on land, none on water (meter kept), in a tank the water dirtier by cPooWaterImpact (0x438a79, 0x4a8f89)",
              notYet&&pile&&water&&tank&&zt.pooWaterImpact==1&&a.typeList[dolphin].pooWaterImpact==5);
        // (the purity set 0x49340d: kept to 0..100, the murky message only
        // on going from over 1 to at or under it)
        const float keepPurity=home->purity;f.takeMurkyTanks();
        home->purity=5;f.setPurity(home->id,-3);const bool low=home->purity==0;
        const auto murky1=f.takeMurkyTanks();f.setPurity(home->id,0.5f);const auto murky2=f.takeMurkyTanks();
        f.setPurity(home->id,140);const bool high=home->purity==100;
        home->purity=keepPurity;
        check("a tank's purity set: kept to 0..100, too murky once on going under (0x49340d)",
              low&&high&&murky1.size()==1&&murky1[0]==home->id&&murky2.empty());
        for(int pid:items.ofKind(ZooItems::Kind::Dung,z->exhibit))items.remove(pid);
      } else check("zebra on land for the dung fixture",false);
      a.clear();
    }
  }
  {
    // The flyers (zoo.exe 0x435adf, 0x4fc160, 0x4f7ebe; #202): only the
    // kinds the scenario lists, one each every cFrequency seconds (a crow
    // 40, a seagull 20), no cap; from a map edge, straight in or 45 degrees
    // off; the biplane on 15 September, Santa only on 25 December
    Ambient &amb=world.getAmbient();
    const int crow=amb.kindIndex("crow"),gull=amb.kindIndex("seagull"),plane=amb.kindIndex("biplane"),santa=amb.kindIndex("santa");
    if(crow>=0&&gull>=0&&plane>=0&&santa>=0) {
      amb.setDate(1,0);amb.clear();amb.setAllowed({5052});
      for(int k=0;k<39;++k)amb.update(1.0f,map);
      const int before40=amb.count();
      amb.update(1.0f,map);
      int kind=-1;float x=0,y=0,vx=0,vy=0;
      const bool one=amb.count()==1&&amb.flyer(0,kind,x,y,vx,vy)&&kind==crow;
      const float W=float(map.getWidth()),H=float(map.getHeight());
      // (from a tile's corner on the edge: 0 or the last tile)
      const bool edge=std::abs(x)<0.01f||std::abs(y)<0.01f||std::abs(x-(W-1))<0.01f||std::abs(y-(H-1))<0.01f;
      const bool inward=(std::abs(x)>=0.01f||vx>0)&&(std::abs(y)>=0.01f||vy>0)&&
                        (std::abs(x-(W-1))>=0.01f||vx<0)&&(std::abs(y-(H-1))>=0.01f||vy<0);
      // measured (#202): cSpeed 64ths of a tile a second on each axis it
      // goes along - a crow (cSpeed 50) 50/64 straight, and on both axes on
      // a diagonal
      const float perAxis=50.0f/64.0f;
      const bool speedOk=(std::abs(vx)<1e-5f||std::abs(std::abs(vx)-perAxis)<1e-4f)&&
                         (std::abs(vy)<1e-5f||std::abs(std::abs(vy)-perAxis)<1e-4f)&&(std::abs(vx)+std::abs(vy))>0;
      const float ang=std::atan2(vy,vx)*4.0f/3.14159265f;
      const bool eightWays=std::abs(ang-std::round(ang))<0.001f;
      amb.clear();amb.setAllowed({5052,5053});
      for(int k=0;k<40;++k)amb.update(1.0f,map);
      const int both=amb.sent();
      amb.clear();amb.setAllowed({5052});amb.setDate(15,8);amb.guestArrived(map);
      bool planeFlew=false;for(int i=0;amb.flyer(i,kind,x,y,vx,vy);++i)planeFlew=planeFlew||kind==plane;
      amb.clear();amb.setDate(24,11);amb.guestArrived(map);
      bool santaEve=false;for(int i=0;amb.flyer(i,kind,x,y,vx,vy);++i)santaEve=santaEve||kind==santa;
      amb.setDate(1,0);amb.clear();amb.setAllowed({});
      out<<"FLYERS before 40 s "<<before40<<", at 40 one crow "<<one<<" edge "<<edge<<" inward "<<inward<<" eight ways "<<eightWays<<" speed "<<vx<<","<<vy
         <<", crow+gull in 40 s "<<both<<", biplane 15 Sep "<<planeFlew<<", Santa 24 Dec "<<santaEve<<"\n";
      check("flyers: the scenario's kinds, one every cFrequency s, from an edge in one of eight ways; the biplane on 15 Sep, no Santa on the 24th (0x435adf, 0x4fc160, 0x4f7ebe)",
            before40==0&&one&&edge&&inward&&eightWays&&both==3&&planeFlew&&!santaEve);
      check("a flyer goes cSpeed 64ths of a tile a second on each axis, diagonals too (measured, #202)",one&&speedOk);
      // The special days come with the guests (zoo.exe 0x4f7ebe, from each
      // guest made at the gate 0x4f6e3c): a witch for every guest on 31
      // October; Santa once on 25 December, however many come; none on
      // another day; the day after, Santa again next year (#219)
      const int witch=amb.kindIndex("witch");
      auto countOf=[&](int k){int n=0;for(int i=0;amb.flyer(i,kind,x,y,vx,vy);++i)n+=kind==k;return n;};
      amb.clear();amb.setAllowed({});amb.setDate(31,9);
      for(int k=0;k<3;++k)amb.guestArrived(map);
      const int witches=countOf(witch);
      amb.update(0.0f,map);const int witchesNoGuest=countOf(witch);
      amb.clear();amb.setDate(25,11);
      for(int k=0;k<3;++k)amb.guestArrived(map);
      const int santas=countOf(santa);
      amb.setDate(26,11);amb.guestArrived(map);amb.setDate(25,11);amb.guestArrived(map);
      const int santasNextYear=countOf(santa);
      amb.clear();amb.setDate(14,8);amb.guestArrived(map);
      const int otherDay=amb.count();
      out<<"SPECIAL DAYS witches for 3 guests "<<witches<<" (no guest "<<witchesNoGuest<<"), Santas for 3 "<<santas
         <<", again next year "<<santasNextYear<<", 14 Sep "<<otherDay<<"\n";
      check("the special days' flyers come with the guests: a witch each on 31 Oct, Santa once on 25 Dec, none other days (0x4f7ebe, #219)",
            witch>=0&&witches==3&&witchesNoGuest==3&&santas==1&&santasNextYear==2&&otherDay==0);
      // Their own sounds (0x4f7ebe: sounds/sleigh.wav, sounds/biplane.wav
      // 1000 more), stopped with the flight (0x41e1fd)
      std::string sound;int atten=-1,channel=-1;
      amb.clear();amb.setAllowed({1});amb.setDate(25,11);amb.guestArrived(map);
      const bool sleigh=amb.ownSound(0,sound,atten,channel)&&sound=="sounds/sleigh.wav"&&atten==0;
      amb.clear();amb.setDate(15,8);amb.guestArrived(map);
      const bool biplaneSound=amb.ownSound(0,sound,atten,channel)&&sound=="sounds/biplane.wav"&&atten==1000;
      for(int k=0;k<400&&amb.count()>0;++k)amb.update(1.0f,map);
      const bool over=amb.count()==0;amb.setAllowed({});
      out<<"SPECIAL SOUNDS sleigh "<<sleigh<<", biplane "<<biplaneSound<<", the flight over "<<over<<"\n";
      check("Santa's sleigh.wav and the biplane's biplane.wav (1000 quieter) follow them, gone with the flight (0x4f7ebe, 0x435282, #219)",
            sleigh&&biplaneSound&&over);
      // The flight's end (0x434e9c): its tile off the map - the far edges at
      // the map's size, the near ones a whole tile past 0
      amb.clear();amb.setDate(1,0);
      const bool westward=amb.resume(map,"crow",2.0f,30.5f,6,64.0f);
      amb.update(2.9f,map);const bool flying=amb.count()==1;
      amb.update(0.2f,map);const bool goneWest=amb.count()==0;
      const bool eastward=amb.resume(map,"crow",W-2.0f,30.5f,2,64.0f);
      amb.update(1.9f,map);const bool flyingEast=amb.count()==1;
      amb.update(0.2f,map);const bool goneEast=amb.count()==0;
      out<<"FLIGHT END west "<<westward<<" "<<flying<<" "<<goneWest<<", east "<<eastward<<" "<<flyingEast<<" "<<goneEast<<"\n";
      check("a flight ends once its tile is off the map: at the map's size, or a whole tile past 0 (0x434e9c, #219)",
            westward&&flying&&goneWest&&eastward&&flyingEast&&goneEast);
      // Art with 4 views goes straight in only (0x4fc160 + 0x412ac4: the
      // type's art set's views, GXLLEAnimSet::attempt 0x40b967) - the
      // biplane's idle.ani lists NE, SE, SW, NW; the crow's five go any of
      // the three ways (#232)
      {
        amb.clear();amb.setDate(1,0);
        int planeStraight=0,planeSent=0,crowDiagonal=0;
        for(int k=0;k<60;++k) {
          const int p=amb.spawn(map,plane),c=amb.spawn(map,crow);
          if(p>=0&&amb.flyer(p,kind,x,y,vx,vy)){++planeSent;planeStraight+=std::abs(vx)<1e-6f||std::abs(vy)<1e-6f;}
          if(c>=0&&amb.flyer(c,kind,x,y,vx,vy))crowDiagonal+=std::abs(vx)>1e-6f&&std::abs(vy)>1e-6f;
        }
        amb.clear();
        out<<"FOUR VIEWS biplane straight "<<planeStraight<<" of "<<planeSent<<", crow diagonal "<<crowDiagonal<<" of 60\n";
        check("a flyer whose art has 4 views (the biplane) comes straight in only; 5-view art (a crow) any of three ways (0x4fc160, 0x40b967, #232)",
              planeSent==60&&planeStraight==60&&crowDiagonal>0);
      }
      // The step left over (+0x158, 0x434e9c: ms x cSpeed in thousandths of
      // a 64th, a step each 1000): the original's place is the whole steps,
      // the rest saved beside it, and read back (#232)
      {
        amb.clear();
        amb.resume(map,"crow",10.0f,30.0f,2,50.0f);amb.resume(map,"crow",40.0f,31.0f,6,50.0f);
        amb.resume(map,"crow",12.0f,33.0f,2,50.0f,250);
        amb.update(1.01f,map);
        const auto saved=amb.saveFlyers();
        const bool east=saved.size()==3&&saved[0].x64==640+50&&saved[0].y64==30*64&&std::abs(saved[0].stepLeft-500)<=2;
        const bool west=saved.size()==3&&saved[1].x64==2560-50&&std::abs(saved[1].stepLeft-500)<=2;
        const bool carried=saved.size()==3&&saved[2].x64==768+50&&std::abs(saved[2].stepLeft-750)<=2;
        out<<"STEP LEFT east "<<(saved.size()>0?saved[0].x64:-1)<<"/"<<(saved.size()>0?saved[0].stepLeft:-1)
           <<", west "<<(saved.size()>1?saved[1].x64:-1)<<"/"<<(saved.size()>1?saved[1].stepLeft:-1)
           <<", from 250 "<<(saved.size()>2?saved[2].x64:-1)<<"/"<<(saved.size()>2?saved[2].stepLeft:-1)<<"\n";
        check("a flyer's place is saved in whole 64ths with the step left over in thousandths, and read back (0x434e9c, 0x47a7fd, #232)",
              east&&west&&carried);
        amb.clear();
      }
      // Saved (ours, version 21; the original's part as its ambient records,
      // ZTAmbientAnimal::save 0x47a7fd: cSpeed at +0x154)
      {
        amb.clear();amb.setAllowed({5052});
        amb.resume(map,"crow",20.0f,20.0f,2,0.0f);amb.resume(map,"crow",30.0f,40.0f,4,0.0f);
        amb.resume(map,"crow",10.0f,24.0f,2,50.0f,400);
        ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
        Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
        const auto file=std::filesystem::temp_directory_path()/("zt1-parity-flyers-"+std::to_string(SDL_GetTicks())+".zoo");
        const bool saved=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn");
        int records=0,speeds=0,ways=0,steps=0;std::string scenario;
        if(saved) {
          std::ifstream in(file,std::ios::binary);
          std::vector<char> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
          AssetBuffer buffer;buffer.data=bytes.data();buffer.size=bytes.size();
          ZooReader decoded;
          if(decoded.load(buffer)&&decoded.hasGame())
            for(const auto &o:decoded.getObjects())if(o.className=="ambient") {
              ++records;speeds+=o.ambientSpeed==50&&o.subClass=="ambient"&&o.typeName=="crow";
              ways+=(o.x==20*64&&o.direction==2)||(o.x==30*64&&o.direction==4)||(o.x==10*64&&o.direction==2);
              steps+=o.x==10*64&&o.ambientStep==400;
            }
          scenario=decoded.getScenarioName();
          buffer.data=nullptr;buffer.size=0;
        }
        amb.clear();amb.setAllowed({});
        const bool loaded=saved&&SaveGame::load(file.string(),world,books,hud);
        int back=0,stepBack=0;
        for(int i=0;amb.flyer(i,kind,x,y,vx,vy);++i) {
          back+=kind==crow&&((std::abs(x-20)<0.01f&&vx>0)||(std::abs(y-40)<0.01f&&vy>0));
          stepBack+=kind==crow&&std::abs(x-(10.0f+0.4f/64.0f))<0.001f&&vx>0;
        }
        // A saved zoo's flyers' list: its scenario block's script's
        // [ambients] again, each kind due at once (resumeScript 0x58fb34 ->
        // applyStartSettings 0x58ed1f -> 0x59148e; measured, #232)
        std::vector<int> listed;
        if(!scenario.empty()&&rm->hasResource(scenario))
          if(IniReader *ini=rm->getIniReader(scenario)) {
            for(const std::string &id:ini->getList("ambients","id"))if(std::atoi(id.c_str())>0)listed.push_back(std::atoi(id.c_str()));
            delete ini;
          }
        const bool listBack=!listed.empty()&&amb.allowedIds()==listed;
        const int flying=amb.count();amb.update(1.0f,map);
        int due=0;for(int id:listed){const int k=amb.kindOf(id);due+=k>=0&&amb.allows(k)&&amb.frequencyOf(k)>0;}
        const bool atOnce=amb.count()-flying==due&&due>0;
        SaveGame::goals=priorGoals;
        std::error_code ec;std::filesystem::remove(file,ec);
        out<<"FLYERS SAVED original records "<<records<<" (cSpeed "<<speeds<<", way "<<ways<<", step "<<steps<<"), back in flight "<<back
           <<" (step "<<stepBack<<"); scenario '"<<scenario<<"' lists "<<listed.size()<<", list back "<<listBack<<", due at once "<<due<<" "<<atOnce<<"\n";
        check("flyers in flight are saved: the original's ambient records with cSpeed, way and step left, ours back in flight (0x47a7fd, #219, #232)",
              saved&&records==3&&speeds==3&&ways==3&&steps==1&&loaded&&back==2&&stepBack==1);
        check("a saved zoo loaded: its flyers' kinds from its scenario block's [ambients], each due at once (0x58fb34, 0x59148e, #232)",
              loaded&&listBack&&atOnce);
        amb.clear();amb.setAllowed({});
      }
    } else check("the flyers' kinds load (ambient.cfg)",false);
  }
  {
    // zoo.exe 0x4e1564: put down on water in its exhibit, an animal that
    // doesn't swim is refused with 10569, a sea animal with 10554
    MapTile *tile=map.getTileMutable(30,32);
    const int zebra=a.typeOfFile("animals/zebra.ai"),dolphin=a.typeOfFile("animals/dolphin.ai");
    if(tile && zebra>=0 && dolphin>=0) {
      const auto was=tile->terrainType;tile->terrainType=9;
      a.canPlace(zebra,30.5f,32.5f,map,f);const int onWater=a.placeMessage;
      a.canPlace(dolphin,30.5f,32.5f,map,f);const int seaOnWater=a.placeMessage;
      tile->terrainType=was;
      check("dropped on water: a zebra refused with 10569, a dolphin with 10554 (0x4e1564)",onWater==10569 && seaOnWater==10554);
    }
  }
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
  // Its target in its own habitat (fTargetBuilding: animalFindTargetBuilding
  // 0x5a5a90 -> findNearestOfKindB 0x4290f3, habitatAt through slot 0x248):
  // a nearer building outside its paddock isn't one; the one inside is
  {
    a.clear();
    const bool inAdded=objects.add("scenery/building/bgrstnd.ai",33.5f,30.5f,4);
    const int inId=inAdded?objects.last()->id:-1;
    const bool outAdded=objects.add("scenery/building/bgrstnd.ai",23.5f,34.5f,4);
    const int outId=outAdded?objects.last()->id:-1;
    // (#241: and only one with a slot with room for it - 0x429330 -> 0x42ac87:
    // a nearer burrow in its paddock, whose slot lists only meerkats and
    // prairie dogs, isn't one)
    const bool burrowAdded=objects.add("scenery/building/burrow1.ai",30.5f,33.5f,0);
    const int burrowId=burrowAdded?objects.last()->id:-1;
    actor.x=28.5f;actor.y=34.5f;actor.rampaging=true;actor.stack={{"__rampage",0}};actor.stepping=false;
    actor.path.clear();actor.pathAt=0;actor.rampageBuilding=-1;
    a.list.push_back(actor);world.reindex();a.nextStep(a.list[0],map,f);
    out<<"RAMPAGE TARGET "<<a.list[0].rampageBuilding<<" (inside "<<inId<<", outside "<<outId<<", burrow "<<burrowId<<")\n";
    check("a rampage's building is the nearest in its own habitat with a slot with room for it, not a nearer one outside or a burrow it can't use (0x5a5a90, 0x4290f3, 0x429330)",
          inAdded&&outAdded&&burrowAdded&&f.exhibitAt(23,34)!=land&&f.exhibitAt(30,33)==land&&a.list[0].rampageBuilding==inId);
    if(objects.byId(burrowId))objects.remove(objects.indexOf(burrowId));
    if(objects.byId(inId))objects.remove(objects.indexOf(inId));
    if(objects.byId(outId))objects.remove(objects.indexOf(outId));
  }
  a.typeList[dino].sets[0].erase("__rampage");a.clear();world.reindex();
  Animals::Member egg;egg.type=dino;egg.exhibit=land;egg.x=30.5f;egg.y=32.5f;
  a.becomeEgg(egg);egg.hatchIn=1;egg.health=0;a.eggTick(egg);
  check("unhealthy dinosaur egg waits to hatch",egg.egg && egg.stack.empty() && egg.hatchIn==1);
  egg.health=static_cast<float>(a.typeList[dino].maxHits);egg.hatchIn=0;a.eggTick(egg); // (0x437860: its last tick)
  check("healthy egg starts its original hatching sequence",!egg.egg || (!egg.stack.empty() && egg.stack.back().first=="bhatch"));
  step(egg,{{"fhatch",{}},{"fplay",{"stand"}}});
  check("hatching creates a live baby",!egg.egg && egg.baby);

  a.clear();
  const int embryo=a.adopt(dino,false,30.5f,32.5f,map,f,true);
  if(auto *m=a.member(embryo)) {a.becomeEgg(*m);m->hatchIn=1;m->health=float(a.typeList[dino].maxHits);a.eggTick(*m);}
  fileRoundTrip("dinosaur egg during hatch");
  for(int k=0;k<4000&&a.member(embryo)&&a.member(embryo)->egg;++k)a.update(0.05f,a.now+0.05,map,f);
  check("interrupted hatch produces one live baby with its identity",a.list.size()==1&&a.member(embryo)&&
        !a.member(embryo)->egg&&a.member(embryo)->baby);
  a.clear();
  const int sleeper=a.adopt(a.typeOfFile("animals/zebra.ai"),false,30.5f,32.5f,map,f,true);
  if(auto *m=a.member(sleeper)) {m->energy=float(a.typeList[m->type].energyThreshold);m->hunger=0;m->energyCheck=0;m->stack.clear();a.nextStep(*m,map,f);} // (the tiredness countdown's last: 0x437102 -> 0x4384ba)
  check("sleep save starts the real sleep behavior",a.member(sleeper)&&a.member(sleeper)->sleeping);
  fileRoundTrip("sleeping animal");
  for(int k=0;k<4000&&a.member(sleeper)&&a.member(sleeper)->sleeping;++k)a.update(0.05f,a.now+0.05,map,f);
  check("sleeping animal wakes after reload",a.member(sleeper)&&!a.member(sleeper)->sleeping);
  if(auto *m=a.member(sleeper)){m->sick=true;m->health=1;}
  fileRoundTrip("sick animal");
  check("illness is retained instead of silently healed",a.member(sleeper)&&a.member(sleeper)->sick&&a.member(sleeper)->health==1);
  a.heal(sleeper);a.update(0.05f,a.now+0.05,map,f);
  check("reloaded sick animal accepts treatment",a.member(sleeper)&&!a.member(sleeper)->sick);
  a.clear();

  // Breeding as the original's (zoo.exe 0x4385e5, 0x430a19): a grown one
  // keeps one mate; only a male tries, once a check, and only with her; no
  // mother or father with its young
  {
    const int zebra=a.typeOfFile("animals/zebra.ai");
    check("zebra definition for breeding",zebra>=0);
    if(zebra>=0) {
      Animals::Type &zt=a.typeList[zebra];const Animals::Type keep=zt;
      zt.reproductionChance=100;zt.happyReproduceThreshold=-100;zt.offspring=1;
      const int dad=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      const int mumA=a.adopt(zebra,true,31.5f,32.5f,map,f,true);
      const int mumB=a.adopt(zebra,true,32.5f,32.5f,map,f,true);
      auto check20=[&](int id){if(auto *m=a.member(id)){m->reproductionCheck=0;a.tick(*m,map,f);}};
      check20(mumA);check20(mumB);
      const int pairedA=a.member(mumA)?a.member(mumA)->mate:-2,pairedB=a.member(mumB)?a.member(mumB)->mate:-2;
      check("two females, one male: one female takes him as her mate, the other has none",
            (pairedA==dad)!=(pairedB==dad) && (pairedA<0||pairedA==dad) && (pairedB<0||pairedB==dad));
      const int mate=pairedA==dad?mumA:mumB;
      check20(dad);
      check("a ready male with every chance: one baby, by his mate only",
            a.births.size()==1 && a.births[0].mother==mate && a.births[0].father==dad);
      a.lifeEvents(map,f);
      int baby=-1;for(const auto &m:a.members())if(m.baby)baby=m.id;
      check("the young one knows its mother and father",a.member(baby)&&a.member(baby)->mother==mate&&
            a.member(baby)->father==dad&&a.member(baby)->mate<0);
      check20(dad);
      check("just after: not ready again (cReproductionInterval)",a.births.empty());
      fileRoundTrip("breeding pair");
      check("mates, mothers and fathers survive saving",a.member(dad)&&a.member(dad)->mate==mate&&a.member(baby)&&
            a.member(baby)->mother==mate&&a.member(baby)->father==dad);
      // (the young grown: not with its mother, the other female free)
      if(auto *y=a.member(baby)){y->baby=false;y->female=false;y->mate=-1;}
      a.remove(dad);
      const int other=mate==mumA?mumB:mumA;
      for(int k=0;k<4;++k){check20(mate);check20(baby);}
      check("a grown son never takes his mother; he pairs with the other female",
            a.member(baby)&&a.member(baby)->mate==other&&a.member(mate)&&a.member(mate)->mate!=baby);
      zt=keep;a.births.clear();a.clear();
    }
  }

  // Eggs as the original's: laid on a free tile from 3 away (zoo.exe
  // 0x60da6b); a newborn isn't a copy of its mother; an egg-eater of
  // another kind takes an egg - it breaks and is gone (0x43971c, 0x4a6043);
  // an egg hatches on its countdown's last tick (0x437860)
  {
    const int lambeo=a.typeOfFile("animals/lambeo.ai");
    check("lambeosaurus and allosaurus for eggs",lambeo>=0 && allosaur>=0);
    if(lambeo>=0 && allosaur>=0) {
      const int mum=a.adopt(lambeo,true,30.5f,32.5f,map,f,true);
      if(auto *m=a.member(mum)){m->sleeping=true;m->huntAnimal=12345;}
      a.births.push_back({mum,-1,lambeo});a.lifeEvents(map,f);
      const Animals::Member *egg=nullptr;for(const auto &m:a.members())if(m.egg)egg=&m;
      const Animals::Member *mother=a.member(mum);
      const int far=egg&&mother?std::abs(int(std::floor(egg->x))-int(std::floor(mother->x)))+
                                std::abs(int(std::floor(egg->y))-int(std::floor(mother->y))):-1;
      check("a laid egg lies on a free tile 3 from its mother ("+std::to_string(far)+")",egg && far==3 &&
            egg->exhibit==land);
      check("the young one doesn't take on what its mother was doing",egg && !egg->sleeping && egg->huntAnimal<0 &&
            egg->mother==mum);
      const int eggId=egg?egg->id:-1;
      // (hatching: cTimeToHatch ticks, then bHatch on the next)
      if(auto *e=a.member(eggId)) {
        const int ticks=a.typeList[lambeo].timeToHatch;int started=-1;
        for(int k=0;k<=ticks+1 && started<0;++k){a.eggTick(*e);if(!e->stack.empty())started=k+1;}
        check("an egg starts hatching on tick cTimeToHatch + 1 ("+std::to_string(started)+" of "+std::to_string(ticks)+")",
              started==ticks+1);
        e->stack.clear();e->stepping=false;e->hatchIn=ticks;
      }
      // (an allosaurus eats it: the egg breaks, its shell gone 15 ticks on)
      const int allo=a.adopt(allosaur,false,30.5f,34.5f,map,f,true);
      Animals::Member *e=a.member(eggId),*hunter=a.member(allo);
      check("an allosaurus hunts another kind's egg, a lambeosaurus doesn't hunt its own kind's",
            e&&hunter&&a.hunts(*hunter,*e)&&a.member(mum)&&!a.hunts(*a.member(mum),*e));
      if(e&&hunter){hunter->x=e->x;hunter->y=e->y;hunter->huntAnimal=eggId;hunter->stack.clear();hunter->stepping=false;}
      for(int k=0;k<3 && a.member(eggId) && !a.member(eggId)->eaten;++k)a.update(0.05f,a.now+0.05,map,f);
      e=a.member(eggId);
      check("caught, the egg breaks (bEatenEgg) and can't be hunted again",e && e->eaten && a.member(allo) &&
            !a.hunts(*a.member(allo),*e));
      for(int k=0;k<20 && a.member(eggId);++k)if(auto *g=a.member(eggId))a.eggTick(*g),a.update(0.0f,a.now,map,f);
      check("the broken shell's gone after its 15 ticks",!a.member(eggId));
      a.clear();
    }
  }

  // Eating as the original's (animalEatFood 0x4fd951 every tick while it
  // eats): a hungry zebra at its food eats a mouthful a tick through its
  // bEat, its hunger right down - not one mouthful a meal
  {
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      auto &items=world.getItems();
      const int eater=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      const int pile=items.addFood(land,31.5f,32.5f,1,"herbchow");
      Animals::Member *z=a.member(eater);
      const float before=items.item(pile)?items.item(pile)->units:0;
      if(z){z->hunger=90;z->foodItem=pile;z->stack.clear();z->stepping=false;a.pushSet(*z,"beat");}
      for(int k=0;k<400 && (z=a.member(eater)) && !z->stack.empty();++k)a.update(0.05f,a.now+0.05,map,f);
      z=a.member(eater);
      const float after=items.item(pile)?items.item(pile)->units:0;
      const auto &zt=a.typeList[zebra];
      check("a hungry zebra eats its fill in one bEat (hunger 90 -> "+std::to_string(int(z?z->hunger:-1))+
            ", food "+std::to_string(int(before-after))+" eaten)",
            z && z->hunger<=std::max(0,90-zt.foodEaten*zt.foodUnitValue*3) && before-after>=zt.foodEaten*3);
      items.remove(pile);a.clear();
    }
  }

  // Stopping eating (#204). While it eats with food left, animalEatFood
  // 0x4fd951 asks for bEat next, forced over a bSleep asked before and not
  // replaceable, so a bSleep asked during the meal isn't taken. Its hunger
  // down to its least, animalStopEating 0x4fd86e (from animalUpdate
  // 0x437620): eating over, its food let go, the set asked for next emptied
  // and replaceable again; the bEat it's in plays on
  {
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      auto &items=world.getItems();
      const int eater=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      const int pile=items.addFood(land,31.5f,32.5f,1,"herbchow");
      if(Animals::Member *z=a.member(eater)) {
        z->hunger=90;z->energy=0;z->foodItem=pile;z->stack.clear();z->stepping=false;z->sleeping=false;
        z->nextSet="bsleep";a.pushSet(*z,"beat");
        a.tick(*z,map,f);
        const bool forced=z->eating&&z->nextSet=="beat";
        const bool refused=!a.requestSet(*z,"bsleep")&&z->nextSet=="beat";
        z->hunger=0;a.tick(*z,map,f);
        const bool stopped=!z->eating&&z->nextSet.empty()&&z->foodItem<0&&!z->stack.empty()&&
                           z->stack.front().first=="beat";
        const bool takenAfter=a.requestSet(*z,"bsleep")&&z->nextSet=="bsleep";
        out<<"STOP EATING forced "<<forced<<" refused "<<refused<<" stopped "<<stopped<<" taken after "<<takenAfter<<"\n";
        check("eating with food left: bEat asked for next, forced over a bSleep, and a bSleep asked mid-meal isn't taken (0x4fd951)",
              forced&&refused);
        check("full: it stops eating, the set asked for next emptied and replaceable, its bEat plays on (0x4fd86e)",
              stopped&&takenAfter);
      }
      items.remove(pile);a.clear();
    }
  }

  // cKeepMoving (#214; type +0x44c - the sharks and big fish): animalEatFood
  // 0x4fd951 eats one mouthful as it gets to the food and sets neither its
  // eating (+0x3a9) nor bEat again, so neither animalUpdate 0x437620 nor
  // animalCheckHunger 0x438a79 calls it again - one mouthful a trip. The
  // zebra fixture, its cKeepMoving set for the check
  {
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      auto &items=world.getItems();
      auto &zt=a.typeList[zebra];
      const bool wasKeep=zt.keepMoving;
      const int eater=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      const int pile=items.addFood(land,31.5f,32.5f,1,"herbchow");
      if(Animals::Member *z=a.member(eater)) {
        auto meal=[&](bool keep,int ticks) {
          zt.keepMoving=keep;
          z->hunger=90;z->energy=0;z->eating=false;z->foodItem=pile;z->stack.clear();z->stepping=false;
          z->sleeping=false;z->nextSet.clear();a.pushSet(*z,"beat");
          z->hungerCheck=1000; // (no hunger check in these few ticks, whatever its random start)
          const float before=items.item(pile)?items.item(pile)->units:0;
          for(int k=0;k<ticks;++k)a.tick(*z,map,f);
          return before-(items.item(pile)?items.item(pile)->units:0);
        };
        const float keepAte=meal(true,5);
        const float keepHunger=z->hunger;
        const bool keepNotEating=!z->eating&&z->nextSet!="beat";
        const float secondTrip=meal(true,5);
        const float otherAte=meal(false,5);
        const bool otherEating=z->eating&&z->nextSet=="beat";
        int kinds=0;
        for(const char *k:{"baracuda","gwshark","hhshark","mako","mantaray","marlin","tigersh","tuna","whleshrk"})
          if(const int t=a.typeOfFile(std::string("animals/")+k+".ai");t>=0&&a.typeList[t].keepMoving)++kinds;
        out<<"KEEPMOVING ate "<<keepAte<<" hunger "<<keepHunger<<" not eating "<<keepNotEating<<" second trip "<<secondTrip
           <<" other ate "<<otherAte<<" eating "<<otherEating<<" kinds "<<kinds<<"\n";
        check("a cKeepMoving kind eats one mouthful a trip to its food, not eating after, no bEat asked again (0x4fd951)",
              keepAte==float(zt.foodEaten)&&keepHunger==90.0f-float(zt.foodEaten*zt.foodUnitValue)&&keepNotEating&&
              secondTrip==float(zt.foodEaten));
        check("any other eats a mouthful each tick of its meal and asks for bEat again (0x4fd951)",
              otherAte>=float(zt.foodEaten*3)&&otherEating);
        check("the nine sharks and big fish read cKeepMoving, the zebra doesn't ("+std::to_string(kinds)+" of 9)",
              kinds==9&&!wasKeep);
        // (#242: a cKeepMoving kind's bEat fMove(level, 1, ..): its food task
        // was replaced at the food (0x4fd66a), so it swims off somewhere at its
        // height, not back to the food; any other goes to the food)
        zt.sets[0]["bparityeatmove"]={{"fmove",{"level","1","stand","30"}}};
        auto byFood=[&](bool keep){
          int nearN=0;zt.keepMoving=keep;
          for(int k=0;k<20;++k){
            z->x=26.5f;z->y=32.5f;z->eating=false;z->foodItem=pile;z->stack={{"bparityeatmove",0}};z->path.clear();z->pathAt=0;z->stepping=false;
            a.nextStep(*z,map,f);
            if(!z->path.empty()&&std::hypot(z->path.back().first-31.5f,z->path.back().second-32.5f)<1.6f)++nearN;
          }
          return nearN;};
        const int keepNear=byFood(true),otherNear=byFood(false);
        zt.sets[0].erase("bparityeatmove");
        out<<"EATMOVE by the food of 20: cKeepMoving "<<keepNear<<", other "<<otherNear<<"\n";
        check("#242: a cKeepMoving kind's fMove(level, 1, ..) swims off at its height, not to its food; another kind's goes to the food (0x4fd66a, 0x43646e)",
              keepNear<20&&otherNear==20);
      }
      zt.keepMoving=wasKeep;
      items.remove(pile);a.clear();
    }
  }

  // "Not enough keepers" (#215): zoo.exe animalCheckKeepers 0x438d31 fires
  // when an animal's +0x378 is under its type's +0x28c (1). ZTAnimal::setup
  // 0x453e59 makes +0x378 1, so in the original it never does - and a zoo
  // we save must hold 1 there too (it held 0, and the 2001 game said "Not
  // enough keepers" for every animal we'd made, each cKeeperCheck)
  {
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      a.clear();
      a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
      Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
      const auto file=std::filesystem::temp_directory_path()/("zt1-keepers-"+std::to_string(SDL_GetTicks())+".zoo");
      const bool written=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn");
      SaveGame::goals=priorGoals;
      std::ifstream in(file,std::ios::binary);
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
      in.close();
      std::error_code ec;std::filesystem::remove(file,ec);
      ZooReader zr;AssetBuffer buf;buf.data=bytes.data();buf.size=bytes.size();buf.owns=false;
      const bool read=written&&!bytes.empty()&&zr.load(buf);
      int animals=0,ones=0;
      for(const auto &o:zr.getObjects())if(o.hasAnimal){++animals;ones+=o.animal.keepers==1;}
      out<<"KEEPERS written "<<written<<" read "<<read<<" animals "<<animals<<" with +0x378 1: "<<ones<<"\n";
      check("a saved zoo's animal holds 1 at +0x378 as ZTAnimal::setup makes it, so the original's \"Not enough keepers\" can't fire (0x453e59, 0x438d31)",
            read&&animals==1&&ones==1);
      a.clear();
    }
  }

  // Another very angry animal (zoo.exe animalCheckAngryNeighbours 0x438e70,
  // every cOtherCheck): cOtherAnimalAngryChange when another in its habitat
  // is at -81 or below (+0x3aa) - in an exhibit only: on zoo ground (habitat
  // +0x2c), two loose animals don't count each other
  {
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      auto &zt=a.typeList[zebra];
      const int wasChange=zt.otherAnimalAngryChange;
      zt.otherAnimalAngryChange=-5;
      a.clear();
      const int one=a.adopt(zebra,false,30.5f,32.5f,map,f,true),two=a.adopt(zebra,true,32.5f,32.5f,map,f,true);
      auto otherTick=[&](int exhibit) {
        Animals::Member *x=a.member(one),*y=a.member(two);
        if(!x||!y)return 0.0f;
        x->exhibit=y->exhibit=exhibit;x->happiness=-90;y->happiness=50;
        y->habitatCheck=y->captivityCheck=y->socialCheck=y->hungerCheck=y->healthCheck=y->energyCheck=1000;
        y->otherCheck=0;y->pending=0;
        a.tick(*y,map,f);
        return y->pending;
      };
      const float inExhibit=otherTick(land),loose=otherTick(-1);
      out<<"ANGRY NEIGHBOUR in exhibit "<<inExhibit<<" loose "<<loose<<"\n";
      check("another at -81 or below in its exhibit costs cOtherAnimalAngryChange; loose on zoo ground it doesn't (0x438e70)",
            inExhibit==-5.0f&&loose==0.0f);
      zt.otherAnimalAngryChange=wasChange;
      a.clear();
    }
  }

  // Keeper food chosen as Task 0x72's whileGoing 0x43849c chooses it: the
  // nearest three times in four (0x49fe15), else three in four the one with
  // the fewest units left (0x4a0025), else any at random (0x4a01f9)
  {
    const int zebra=a.typeOfFile("animals/zebra.ai");
    if(zebra>=0) {
      auto &items=world.getItems();
      const int eater=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
      const int nearF=items.addFood(land,31.5f,32.5f,1,"herbchow"),fewF=items.addFood(land,33.5f,32.5f,1,"herbchow"),
                farF=items.addFood(land,35.5f,32.5f,1,"herbchow");
      items.eat(fewF,900); // (the fewest units left)
      int nearest=0,fewest=0,other=0;
      if(Animals::Member *z=a.member(eater))
        for(int k=0;k<4800;++k){const int got=a.chooseFood(*z);nearest+=got==nearF;fewest+=got==fewF;other+=got==farF;}
      out<<"FOODPICK nearest "<<nearest<<" fewest "<<fewest<<" other "<<other<<" of 4800 (3700, 1000, 100 expected)\n";
      check("keeper food chosen by Task 0x72's odds: nearest 3/4, fewest units 3/16, at random 1/16 (0x49fe15, 0x4a0025, 0x4a01f9)",
            std::abs(nearest-3700)<150&&std::abs(fewest-1000)<120&&std::abs(other-100)<60);
      items.remove(nearF);items.remove(fewF);items.remove(farF);a.clear();
    }
  }

  // A young one's own numbers (its [y/Characteristics/Integers] over the
  // grown one's): a young T. rex walks at 30 not 60, doesn't jump fences, is
  // more attractive (120 to 100); a young lambeosaurus doesn't test fences
  {
    const int rex=a.typeOfFile("animals/trex.ai"),lambeo=a.typeOfFile("animals/lambeo.ai");
    if(rex>=0 && lambeo>=0) {
      const auto &rt=a.typeList[rex],&lt=a.typeList[lambeo];
      check("a young T. rex: half the walking pace, no jumping, more attractive",
            std::abs(rt.forAge(true).slow-Original::unitTilesPerSecond(30))<1e-4f &&
            std::abs(rt.forAge(false).slow-Original::unitTilesPerSecond(60))<1e-4f &&
            !rt.forAge(true).jumper && rt.forAge(false).jumper &&
            rt.forAge(true).attractiveness==120 && rt.forAge(false).attractiveness==100);
      const int y=a.adopt(lambeo,false,30.5f,32.5f,map,f,true);
      if(auto *m=a.member(y)){m->baby=true;for(int k=0;k<60;++k)a.testFence(*m,f);}
      check("a young lambeosaurus never tests the fence, its chance kept for when it's grown",
            lt.forAge(true).testFenceChance==0 && lt.forAge(false).testFenceChance==50 &&
            a.member(y) && a.member(y)->fenceTests<0);
      a.clear();
    }
  }

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
    // (there: the talk, bSpeak - whole plays of speak covering
    // cInformGuestTime, zoo.exe 0x424ee7; measured 6.8 s in the original,
    // speak2 never played)
    leader->speaking=false;leader->path={{leader->x,leader->y}};leader->pathAt=0;
    st.update(0.05f,map,f);
    const float speakOnce=st.animSeconds(*leader,"speak"),talk=leader->speakLeft;
    check("the talk is whole plays of speak covering cInformGuestTime ("+std::to_string(talk)+" s, 0x424ee7)",
          leader->speaking&&std::abs(talk-speakOnce*std::ceil(type.informTime/speakOnce))<0.01f&&talk>=type.informTime);
    st.update(0.05f,map,f);check("the talk plays speak",leader->anim=="speak");
    st.update(static_cast<float>(type.informTime),map,f);check("past cInformGuestTime it's still speak, never speak2",leader->speaking&&leader->anim=="speak");
    st.update(talk+0.1f,map,f);
    check("finished tour releases every follower",leader->followers.empty() && std::none_of(g.list.begin(),g.list.end(),[&](const auto &v){return v.guide==guide;}));
    g.list[0].guide=guide;leader->followers={g.list[0].id};st.fire(guide);
    check("removing guide releases its group",g.list[0].guide<0);
    type=originalType;
  }
  // Scenery to look at (zoo.exe guestPickAttraction 0x4282e1's third group,
  // 0x42945f, Task81_GawkObject): a guest with every exhibit seen and nothing
  // else to do goes to a fountain (cEstheticWeight 33), looks at it and
  // doesn't come back to its kind.
  st.clear();g.clear();
  {
    const bool added=objects.add("scenery/building/fountain.ai",27.5f,27.5f,4);
    const auto *fountain=added?objects.last():nullptr;
    check("fountain definition with an esthetic weight",
          fountain && g.infoOf(PlacedObjects::fileOf(*fountain)).estheticWeight>0);
    if(fountain && g.infoOf(PlacedObjects::fileOf(*fountain)).estheticWeight>0) {
      const int fountainId=fountain->id;
      world.reindex();g.setEntrance(25,26);g.admit(0);
      auto &guest=g.list.front();
      guest.x=25.5f;guest.y=26.5f;guest.state=Guests::State::Walking;guest.leaving=false;
      guest.hunger=guest.thirst=guest.tired=guest.bathroom=0;
      // (every exhibit and every other object on its visited list - more than
      // the original's 10, for this check - and, where there's nothing here
      // to visit (0x4281fd: no exhibit with animals, no 'fun' building),
      // Task64's roll of cLeaveChanceDone off)
      for(const auto &e:f.exhibits())for(auto tile:e.tiles) {
        guest.visits.push_back(tile);
        if(g.visitedObject(guest,*fountain))guest.visits.pop_back();else break;
      }
      for(const auto &o:objects.objects())if(o.id!=fountainId&&!o.fence) {
        guest.visits.push_back({int(std::floor(o.x)),int(std::floor(o.y))});
        if(g.visitedObject(guest,*fountain))guest.visits.pop_back(); // (not a tile the fountain stands on)
      }
      const int leaveDone=g.typeList[guest.type].leaveDone;
      g.typeList[guest.type].leaveDone=0;
      out<<"FOUNTAIN at "<<fountain->x<<","<<fountain->y<<" visits "<<guest.visits.size()<<", visited "<<g.visitedObject(guest,*fountain)
         <<", weight "<<g.sceneryWeight(guest)<<", esthetic "<<g.infoOf(PlacedObjects::fileOf(*fountain)).estheticWeight<<", inside "<<f.insideZoo(27,27)<<"\n";
      check("a guest with nothing else to see weighs the fountain",g.sceneryWeight(guest)>=1);
      g.decide(guest);
      check("it goes to look at the fountain, on a tile beside it",
            guest.gawkObject==fountainId && !guest.path.empty() &&
            std::abs(guest.path.back().first-27.5f)<=2.6f && std::abs(guest.path.back().second-27.5f)<=2.6f);
      g.gawkScenery(guest);
      check("there it looks (gawk, camera or stand), facing it",
            guest.state==Guests::State::Gawking && guest.gawkObject<0 &&
            (guest.anim=="gawk1" || guest.anim=="camera" || guest.anim=="stand"));
      // (Task81: the task over only once its bGawkObject is - checkArrived
      // 0x44118f - then +0x33e, atTarget 0x441290)
      {
        const bool whileLooking=!guest.recheck;
        guest.animTime=guest.playFor;guest.tickMs=0;g.update(0.0f,0,1000);
        check("looking at scenery: its needs looked at again only once the look is over (Task81 0x44118f, 0x441290)",
              whileLooking&&guest.recheck&&guest.state!=Guests::State::Gawking);
      }
      check("looked at, the fountain goes onto its visited list (Task80 0x4406a4 -> 0x4a840e)",
            !guest.visits.empty()&&guest.visits.back()==std::make_pair(int(std::floor(fountain->x)),int(std::floor(fountain->y))));
      guest.state=Guests::State::Walking;g.decide(guest);
      check("a kind looked at isn't gone back to",guest.gawkObject!=fountainId);
      g.typeList[guest.type].leaveDone=leaveDone;
    }
    if(added)objects.remove(static_cast<int>(objects.objects().size())-1);
  }
  // Where a guest has been (zoo.exe +0x260, +0x334; #164): guestRecordVisit
  // 0x4a840e keeps its last 10 places and counts every visit; a building of a
  // kind on the list is passed over (0x4285e2); and once its visits reach
  // the places to visit (Task64 0x4281be: 0x4281fd, the exhibits with
  // animals plus the 'fun' buildings) its list is emptied, its count back
  // to 0, and cLeaveChanceDone rolled (guestSeenEverything 0x4a6acc, 10238)
  st.clear();g.clear();
  {
    const size_t n0=objects.objects().size();
    const bool one=objects.add("scenery/building/bounce.ai",33.5f,27.5f,4);
    const bool two=one&&objects.add("scenery/building/bounce.ai",37.5f,27.5f,4);
    check("two bounce houses (a 'fun' building) for the visited list",two&&objects.objects().size()==n0+2);
    if(two) {
      world.reindex();g.setEntrance(25,26);g.admit(0);
      auto &guest=g.list.front();
      guest.x=25.5f;guest.y=26.5f;guest.state=Guests::State::Walking;guest.leaving=false;
      guest.hunger=guest.thirst=guest.tired=guest.bathroom=0;
      for(int k=0;k<12;++k)g.recordVisit(guest,k,1);
      check("its visited list keeps the last 10 places and counts all 12 visits (0x4a840e)",
            guest.visits.size()==10&&guest.visitCount==12&&guest.visits.front()==std::make_pair(2,1));
      guest.visits.clear();guest.visitCount=0;
      const auto &first=objects.objects()[n0],&second=objects.objects()[n0+1];
      g.recordVisit(guest,first);
      check("a bounce house on its list counts the other one of its kind as visited too (0x4285e2)",
            g.visitedObject(guest,first)&&g.visitedObject(guest,second));
      const int places=g.placesToVisit(),leaveDone=g.typeList[guest.type].leaveDone;
      out<<"VISITS places to visit "<<places<<" (cLeaveChanceDone "<<leaveDone<<")\n";
      g.typeList[guest.type].leaveDone=100;
      guest.visits.clear();guest.visitCount=places-1;guest.lastThought=0;
      g.decide(guest);
      const bool stays=!guest.leaving&&guest.lastThought!=10238;
      guest.leaving=false;guest.state=Guests::State::Walking;guest.building=-1;guest.path.clear();
      guest.visits={{1,1},{2,2}};guest.visitCount=places;guest.lastThought=0;
      g.decide(guest);
      const bool leaves=guest.leaving&&guest.lastThought==10238&&guest.visits.empty()&&guest.visitCount==0;
      g.typeList[guest.type].leaveDone=leaveDone;
      check("visits under the places to visit: it picks on (Task64 0x4281be)",places>=2&&stays);
      check("visits at the places to visit: its list emptied, its count 0, cLeaveChanceDone rolled - 10238 (0x4281be, 0x4a6acc)",
            leaves);
      // The 'fun' building drawn is the one it walks to (#223; zoo.exe
      // guestPickAttraction 0x4282e1 at 0x42c775: the roll less the
      // exhibits' count indexes the unvisited list), not the nearest - so
      // over many picks the far bounce house is taken as well as the near
      // one, and a full one drawn is still walked to (room is looked at at
      // the door, Task66 0x4a830c)
      std::vector<std::pair<int,int>> laid;
      // (a path along their north doors, the row above them)
      int row=1000;
      for(size_t k=n0;k<n0+2;++k)for(auto [x,y]:g.doorTiles(objects.objects()[k]))row=std::min(row,y-1);
      for(size_t k=n0;k<n0+2;++k)for(auto [x,y]:g.doorTiles(objects.objects()[k]))
        if(y==row+1&&!map.isPath(x,y)){map.setPath(x,y,"path");laid.push_back({x,y});}
      for(int x=24;x<=42&&row<1000;++x)if(!map.isPath(x,row)){map.setPath(x,row,"path");laid.push_back({x,row});}
      world.reindex();
      const int nearId=static_cast<int>(n0),farId=static_cast<int>(n0+1);
      // (a boy: the bounce house's slot takes boys and girls only, ids 9505, 9506)
      const int typeWas=guest.type;
      for(size_t k=0;k<g.typeList.size();++k)if(g.typeList[k].nameId==9505)guest.type=static_cast<int>(k);
      int nearPicks=0,farPicks=0,tries=0;
      for(int k=0;k<80;++k) {
        guest.x=25.5f;guest.y=row+0.5f;guest.state=Guests::State::Walking;guest.leaving=false;guest.building=-1;
        guest.path.clear();guest.pathAt=0;guest.visits.clear();guest.visitCount=0;guest.target=-1;guest.gawkObject=-1;guest.need.clear();
        g.decide(guest);
        if(guest.need!="fun")continue;
        ++tries;
        nearPicks+=guest.building==nearId;farPicks+=guest.building==farId;
      }
      out<<"FUNPICK near "<<nearPicks<<", far "<<farPicks<<" of "<<tries<<" fun picks (row "<<row<<")\n";
      check("the 'fun' building drawn is walked to, the far one as well as the near (0x4282e1 at 0x42c775)",
            nearPicks>0&&farPicks>0&&nearPicks+farPicks==tries);
      // (both full: 99 users each would be turned away at the door, but the
      // pick still sends the guest to the one drawn)
      int fullPicks=0;
      {
        std::vector<Guests::Guest> extra;
        const int cap=g.infoOf(PlacedObjects::fileOf(objects.objects()[n0])).capacity;
        Guests::Guest filler=guest;filler.state=Guests::State::Using;
        for(int k=0;k<cap;++k){filler.building=nearId;g.list.push_back(filler);filler.building=farId;g.list.push_back(filler);}
        for(int k=0;k<20;++k) {
          auto &q=g.list.front();
          q.x=25.5f;q.y=row+0.5f;q.state=Guests::State::Walking;q.leaving=false;q.building=-1;
          q.path.clear();q.pathAt=0;q.visits.clear();q.visitCount=0;q.target=-1;q.gawkObject=-1;q.need.clear();
          g.decide(q);
          fullPicks+=q.need=="fun"&&(q.building==nearId||q.building==farId);
        }
        g.list.resize(1);g.list.front().type=typeWas;
      }
      out<<"FUNPICK full ones still walked to "<<fullPicks<<" of 20\n";
      check("a full 'fun' building drawn is still walked to - room is looked at the door (0x4a830c)",fullPicks>0);
      // zoo.ini [AI] useAttractionAttractiveness set (#231; zoo.exe 0x42c775
      // -> 0x5cfc48: attractionHighestScore 0x60b086): the one scoring
      // highest, 100 - (1000 - squared distance, at most 990) / 10 - the far
      // bounce house every time; from between them (equal scores) the first
      // on the list
      check("attraction score: 100 - (1000 - squared distance, at most 990) / 10 (0x60b086)",
            Original::attractionScore(0,0)==0&&Original::attractionScore(3,4)==3&&Original::attractionScore(-4,-8)==8&&
            Original::attractionScore(31,5)==99&&Original::attractionScore(40,0)==99&&Original::attractionScore(30,10)==99);
      int scoredNear=0,scoredFar=0,scoredTries=0,tieFirst=0,tieTries=0;
      g.useAttractionAttractiveness=true;
      auto &sg=g.list.front();
      for(size_t k=0;k<g.typeList.size();++k)if(g.typeList[k].nameId==9505)sg.type=static_cast<int>(k);
      for(int k=0;k<80;++k) {
        auto &guest=sg;
        const bool tie=k>=40;
        guest.x=tie?35.5f:25.5f;guest.y=row+0.5f;guest.state=Guests::State::Walking;guest.leaving=false;guest.building=-1;
        guest.path.clear();guest.pathAt=0;guest.visits.clear();guest.visitCount=0;guest.target=-1;guest.gawkObject=-1;guest.need.clear();
        g.decide(guest);
        if(guest.need!="fun")continue;
        if(tie){++tieTries;tieFirst+=guest.building==nearId;}
        else{++scoredTries;scoredNear+=guest.building==nearId;scoredFar+=guest.building==farId;}
      }
      g.useAttractionAttractiveness=false;sg.type=typeWas;
      out<<"FUNPICK scored: near "<<scoredNear<<", far "<<scoredFar<<" of "<<scoredTries<<"; between them the first "
         <<tieFirst<<" of "<<tieTries<<"\n";
      check("useAttractionAttractiveness: the furthest 'fun' building every time (0x42c775 -> 0x60b086)",
            scoredTries>0&&scoredFar==scoredTries&&scoredNear==0);
      check("useAttractionAttractiveness: of equal scores the first on the list (0x60b10f)",
            tieTries>0&&tieFirst==tieTries);
      // Attacked by a predator (+0x33b, 0x4a6043): leaving from its next
      // behaviour set (ZTGuest::startBehaviour 0x422498), running to the gate
      // (guestLeaveZoo 0x4a6889: bRunFromPredator), the 10247 thought only
      // when afraid (0x4a67eb)
      {
        sg.x=25.5f;sg.y=row+0.5f;sg.state=Guests::State::Walking;sg.leaving=false;sg.sawEscape=false;sg.building=-1;
        sg.path.clear();sg.pathAt=0;sg.target=-1;sg.gawkObject=-1;sg.need.clear();sg.lastThought=0;
        sg.attacked=true;
        g.decide(sg);
        const bool leaves=sg.leaving&&sg.state==Guests::State::Leaving;
        const bool runs=sg.anim=="run_terror"&&sg.speed==g.typeList[sg.type].runSpeed;
        out<<"ATTACKED leaving "<<sg.leaving<<", anim "<<sg.anim<<", thought "<<sg.lastThought<<"\n";
        sg.attacked=false;sg.leaving=false;sg.state=Guests::State::Walking;sg.path.clear();sg.pathAt=0;
        check("a guest attacked by a predator leaves the zoo, running (0x422498, 0x4a6889)",
              leaves&&runs&&sg.lastThought!=10247);
      }
      for(auto [x,y]:laid)map.setPath(x,y,"");
    }
    while(objects.objects().size()>n0)objects.remove(static_cast<int>(objects.objects().size())-1);
  }
  // A bench (#205; zoo.exe Task68 0x42cd39 -> 0x5ad535): no cHideUser, no
  // cUserStaysOutside - the guest takes the free slot whose entrance is
  // nearest (0x42c02b), sits at its slotpos facing the bench's turn plus the
  // slot's facing (0x42ce0a, 0x5ad288), and plays b<name><goal><slot>:
  // fPlay(benchenergy<slot>), fPlayTime(it, 30) - whole plays to 30 s -
  // fPlay(idle), then out
  st.clear();g.clear();
  {
    const size_t n0=objects.objects().size();
    const bool added=objects.add("scenery/other/bench.ai",40.0f,30.5f,0);
    check("a bench to sit on",added&&objects.objects().size()==n0+1);
    if(added&&objects.objects().size()==n0+1) {
      std::vector<std::pair<int,int>> laid;
      for(int y=27;y<=34;++y)for(int x=36;x<=44;++x)if(!map.isPath(x,y)){map.setPath(x,y,"path");laid.push_back({x,y});}
      world.reindex();g.setEntrance(25,26);g.admit(0);
      auto &guest=g.list.front();const Guests::Type &t=g.typeList[guest.type];
      guest.x=40.5f;guest.y=33.5f;guest.state=Guests::State::Walking;guest.leaving=false;
      guest.hunger=guest.thirst=guest.bathroom=0;guest.tired=90;
      const int at=static_cast<int>(n0);
      const PlacedObjects::Object &bench=objects.objects()[n0];
      const Guests::BuildingInfo &bi=g.infoOf(PlacedObjects::fileOf(bench));
      const bool found=g.seekBuilding(guest,"energy",1000);
      const int slot=guest.slot;
      out<<"BENCH found "<<found<<", building "<<guest.building<<" (want "<<at<<"), slot "<<slot<<", slots "<<bi.slots.size()
         <<", type '"<<bi.typeName<<"', hides "<<bi.hideUser<<", outside "<<bi.staysOutside<<"\n";
      check("a tired guest heads for a free bench slot (0x42c02b)",found&&guest.building==at&&slot>=0&&slot<2);
      if(found&&slot>=0&&slot<static_cast<int>(bi.slots.size())) {
        const auto &s=bi.slots[static_cast<size_t>(slot)];
        float ex,ey;Guests::buildingPoint(bench,s.ex,s.ey,ex,ey);
        guest.x=ex;guest.y=ey;guest.path.clear();guest.pathAt=0;
        guest.dTired=0;guest.lastThought=0;
        const int visitors0=bench.visitorsNow;
        g.useBuilding(guest);
        // (#224: taken in as its user only at the set's fEnterBuilding, after
        // the first play - buildingAddUser 0x42ce0a: its visitors counted;
        // what the bench does for it, cEnergyChange, only as it gets up -
        // guestBuy 0x42d858 -> 0x42dd23)
        const bool notYet=objects.objects()[n0].visitorsNow==visitors0&&guest.dTired==0.0f;
        float sx,sy;Guests::buildingPoint(bench,s.sx,s.sy,sx,sy);
        const std::string anim="benchenergy"+std::to_string(slot);
        const float ang=((bench.facing+s.facing)&7)*3.14159265f/4.0f;
        const bool sat=guest.state==Guests::State::Using&&guest.anim==anim&&guest.sitStep==0&&
                       std::abs(guest.x-sx)<1e-3f&&std::abs(guest.y-sy)<1e-3f&&
                       std::abs(guest.fx-std::sin(ang))<1e-3f&&std::abs(guest.fy+std::cos(ang))<1e-3f&&!guest.hidden;
        check("it sits at its slot's spot, facing the bench's way, playing "+anim+" (0x42ce0a, 0x5ad288)",sat&&t.anims.count(anim));
        guest.animTime=guest.playFor;g.update(0.0f,0,1000);
        const bool sitting=guest.state==Guests::State::Using&&guest.anim==anim&&guest.sitStep==2&&guest.playFor>=30.0f-1e-3f;
        const bool takenIn=objects.objects()[n0].visitorsNow==visitors0+1&&guest.dTired==0.0f&&
                           (bi.usedThought==0||guest.lastThought==bi.usedThought);
        const float sitFor=guest.playFor;
        guest.animTime=guest.playFor;g.update(0.0f,0,1000);
        const bool idle=guest.state==Guests::State::Using&&guest.anim=="idle"&&guest.sitStep==3;
        const float tired0=guest.tired;
        guest.animTime=guest.playFor;g.update(0.0f,0,1000);
        const bool rested=guest.dTired==bi.energy-tired0;
        out<<"BENCH sat "<<sat<<", sitting "<<sitFor<<" s, idle "<<idle<<", then state "<<static_cast<int>(guest.state)<<"\n";
        out<<"BENCH visitors at sit-down "<<notYet<<", taken in after the first play "<<takenIn<<", cEnergyChange "<<bi.energy
           <<", dTired out "<<guest.dTired<<"\n";
        check("then it sits on for whole plays to 30 s or more (fPlayTime(..., 30), 0x424ee7)",sitting);
        check("then fPlay(idle), then up and out (fExitBuilding)",idle&&guest.state!=Guests::State::Using&&guest.sitStep<0);
        check("#224: taken in at fEnterBuilding, after the first play - counted, cUsedThought - not as it sits down (0x42ce0a)",
              notYet&&takenIn);
        check("#224: cEnergyChange only as it gets up (guestBuy 0x42d858 -> 0x42dd23)",takenIn&&rested);
        // Food in hand, the goal "eat": the food eaten at fEnterBuilding
        // (0x42ce0a -> 0x42d1e2 -> 0x42d74e -> guestConsumeItem 0x42d380)
        const Guests::BuildingInfo &stand=g.infoOf("scenery/building/bgrstnd.ai");
        const Guests::BuildingInfo *food=stand.sells.empty()?nullptr:&g.itemOf(stand.sells.front());
        const std::string eatAnim="bencheat"+std::to_string(slot);
        if(food&&food->itemId&&t.anims.count(eatAnim)) {
          guest.state=Guests::State::Walking;guest.building=at;guest.slot=slot;guest.need="eat";guest.sitStep=-1;
          guest.x=ex;guest.y=ey;guest.ride=0;guest.inventory[0]=food->itemId;guest.lastThought=0;guest.dHunger=0;
          g.useBuilding(guest);
          const bool heldAtSit=guest.anim==eatAnim&&guest.sitStep==0&&guest.inventory[0]==food->itemId&&guest.dHunger==0.0f;
          guest.animTime=guest.playFor;g.update(0.0f,0,1000);
          const bool eaten=guest.sitStep==2&&guest.inventory[0]==0&&guest.dHunger==food->itemHunger&&
                           (food->consumedThought==0||guest.lastThought==food->consumedThought);
          out<<"BENCH eat: "<<food->item<<" held at sit-down "<<heldAtSit<<", eaten after the first play "<<eaten
             <<" (hand "<<guest.inventory[0]<<", dHunger "<<guest.dHunger<<", thought "<<guest.lastThought<<")\n";
          check("#224: the held food eaten at fEnterBuilding, after the first play (0x42ce0a -> 0x42d74e)",heldAtSit&&eaten);
          guest.state=Guests::State::Walking;guest.building=-1;guest.need.clear();guest.sitStep=-1;
        } else {
          check("#224: a food and the bencheat set to test with",false);
        }
      }
      for(auto [x,y]:laid)map.setPath(x,y,"");
    }
    while(objects.objects().size()>n0)objects.remove(static_cast<int>(objects.objects().size())-1);
  }
  // The playground and games (#228; zoo.exe Task68 0x42cd39 -> 0x5ad535 ->
  // startSetNow 0x43c6c5): their slots take only the kinds with a set
  // (boy, girl; the dino putt man, woman), who play guests.ai's
  // [<kind>\BehaviorSet\b<name>fun0] line by line - fPlay once, fPlayReverse
  // backwards, fPlayWithSound - taken in at its fEnterBuilding (0x5ad3a6 ->
  // 0x42ce0a: counted, cUsedThought, the ring toss's prize), out at
  // fExitBuilding; no cTimeInside (Task6a 0x42a1db counts it only for
  // cHideUser / cUserStaysOutside). The bounce houses (cHideBuilding) aren't
  // drawn while they have a user (0x433aaf at 0x4413a5)
  st.clear();g.clear();
  {
    struct Case { const char *file, *kind; std::vector<std::string> plays; int enterAfter; std::string reversed; };
    const Case cases[]={
      {"scenery/building/swing.ai","boy",{"swing","swing","idle"},1,"swing"},
      {"scenery/building/dslide.ai","girl",{"dslide","idle"},1,""},
      {"scenery/building/bounce.ai","boy",{"bounce","idle"},1,""},
      {"scenery/building/aqbounce.ai","girl",{"aqbounce","idle"},1,""},
      {"scenery/building/ringtoss.ai","girl",{"ringwin","ringtoss","idle"},0,""},
      {"scenery/building/ringtoss.ai","boy",{"ringtoss","ringwin","idle"},0,""},
      {"scenery/building/dinoputt.ai","man",{"puttentr","puttwin","puttlose","puttexit","idle"},1,""},
      {"scenery/building/dinoputt.ai","woman",{"puttentr","puttlose","puttwin","puttexit","idle"},1,""},
    };
    int ran=0,ok=0;
    for(const Case &c:cases) {
      st.clear();g.clear();
      const size_t n0=objects.objects().size();
      if(!objects.add(c.file,40.0f,30.0f,0)||objects.objects().size()!=n0+1){out<<"PLAYGROUND "<<c.file<<" not placed\n";continue;}
      std::vector<std::pair<int,int>> laid;
      for(int y=20;y<=40;++y)for(int x=30;x<=50;++x)if(!map.isPath(x,y)){map.setPath(x,y,"path");laid.push_back({x,y});}
      world.reindex();g.setEntrance(25,26);g.admit(0);
      auto &guest=g.list.front();
      int kind=-1;for(size_t k=0;k<g.typeList.size();++k)if(g.typeList[k].key==c.kind)kind=static_cast<int>(k);
      guest.type=std::max(0,kind);
      guest.x=40.5f;guest.y=37.5f;guest.state=Guests::State::Walking;guest.leaving=false;guest.ride=0;
      for(auto &h:guest.inventory)h=0;
      const int at=static_cast<int>(n0);
      const PlacedObjects::Object &bo=objects.objects()[n0];
      const Guests::BuildingInfo &bi=g.infoOf(PlacedObjects::fileOf(bo));
      const bool found=g.seekBuilding(guest,"fun",1000);
      std::string line=std::string(c.kind)+" at "+bi.typeName+": found "+std::to_string(found)+" slot "+std::to_string(guest.slot)+";";
      bool good=kind>=0&&found&&guest.building==at&&guest.slot==0;
      // (another kind isn't let in: the slot's id list)
      {
        Guests::Guest other=guest;int otherKind=-1;
        for(size_t k=0;k<g.typeList.size();++k){const auto &tk=g.typeList[k].key;
          if(tk!=c.kind&&((tk=="man"||tk=="woman")!=(std::string(c.kind)=="man"||std::string(c.kind)=="woman")))otherKind=static_cast<int>(k);}
        other.type=std::max(0,otherKind);other.building=-1;other.slot=-1;
        const bool otherFound=g.seekBuilding(other,"fun",1000,false,at);
        line+=" other kind found "+std::to_string(otherFound)+";";
        good=good&&otherKind>=0&&!otherFound;
      }
      if(good) {
        guest.path.clear();guest.pathAt=0;guest.lastThought=0;
        const int visitors0=bo.visitorsNow;
        g.useBuilding(guest);
        std::vector<std::string> seen;std::string rev;int enteredAt=-1;bool timesOk=true,hiddenOk=true,neverHiddenBefore=true;
        for(int step=0;step<12&&guest.state==Guests::State::Using;++step) {
          if(enteredAt<0&&objects.objects()[n0].visitorsNow==visitors0+1)enteredAt=static_cast<int>(seen.size());
          seen.push_back(guest.anim);
          if(guest.reverse)rev+=guest.anim;
          if(Animation *a=g.art(guest,guest.anim)) {
            const float once=a->frameCount()*a->frameTimeMs()/1000.0f;
            timesOk=timesOk&&std::abs(guest.playFor-once)<1e-3f;
          }
          g.update(0.0f,0,1000); // (the hidden building looked at)
          const bool hid=objects.objects()[n0].showHidden;
          if(bi.hideBuilding)hiddenOk=hiddenOk&&(hid==guest.takenIn);else hiddenOk=hiddenOk&&!hid;
          if(!guest.takenIn)neverHiddenBefore=neverHiddenBefore&&!hid;
          guest.animTime=guest.playFor;g.update(0.0f,0,1000);
        }
        g.update(0.0f,0,1000);
        const bool shownAfter=!objects.objects()[n0].showHidden;
        const bool prize=bi.satisfies.count("fungift")==0||(guest.inventory[2]!=0);
        line+=" plays";for(auto &s:seen)line+=" "+s;
        line+="; taken in before play "+std::to_string(enteredAt)+"; reversed '"+rev+"'; whole plays "+std::to_string(timesOk)+
              "; cHideBuilding "+std::to_string(bi.hideBuilding)+" hidden while used "+std::to_string(hiddenOk)+
              " shown after "+std::to_string(shownAfter)+"; prize "+std::to_string(prize)+"; out "+std::to_string(guest.state!=Guests::State::Using);
        good=seen==c.plays&&enteredAt==c.enterAfter&&rev==c.reversed&&timesOk&&hiddenOk&&neverHiddenBefore&&shownAfter&&prize&&
             guest.state!=Guests::State::Using&&(bi.usedThought==0||objects.objects()[n0].visitorsNow==visitors0+1);
      }
      out<<"PLAYGROUND "<<line<<"\n";
      ran++;ok+=good;
      guest.state=Guests::State::Walking;guest.building=-1;guest.need.clear();guest.sitStep=-1;
      for(auto [x,y]:laid)map.setPath(x,y,"");
      while(objects.objects().size()>n0)objects.remove(static_cast<int>(objects.objects().size())-1);
    }
    check("#228: the swing, slide, bounce houses, ring toss and dino putt run each kind's b<name>fun0 set - its plays, taken in at fEnterBuilding, no cTimeInside, the bounce house hidden while used ("+std::to_string(ok)+"/"+std::to_string(ran)+")",
          ran==8&&ok==8);
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
    const int guestId=guest.id,shopId=shop->id;
    bool used=false,savedUse=false;
    for(int k=0;k<3200;++k) {
      g.update(0.05f,0,1000);auto *patron=g.guest(guestId);if(!patron)break;
      used |= patron->state==Guests::State::Using;
      if(patron->state==Guests::State::Using&&!savedUse) {
        const double income=objects.byId(shopId)->income;
        fileRoundTrip("guest using food stand");savedUse=true;
        patron=g.guest(guestId);
        check("shop reload releases transient occupancy without charging twice",patron&&patron->building<0&&
              !patron->hidden&&objects.byId(shopId)&&objects.byId(shopId)->income==income);
      }
      patron=g.guest(guestId);if(patron&&patron->hunger<90)break;
    }
    const auto *patron=g.guest(guestId);const auto *counter=objects.byId(shopId);
    // (#224: it bought as it went in - buildingAddUser 0x42ce0a; our saves
    // keep no task, so reloaded it eats what it holds - Guest::eatAfterLoad -
    // the stand unpaid for it, as it never came out)
    check("guest bought at the door, reloaded, eats what it holds and its hunger falls",used&&savedUse&&patron&&patron->hunger<90&&
          counter&&patron->inventory[0]==0);
    // At the door (zoo.exe 0x42c8af): a cCommerce building priced at
    // cItemExpensive x its default or more turns the guest away - 10073,
    // cPriceAngry1Change, nothing paid; at its default it goes in, and pays
    // its price x cPriceFactor coming out (guestBuy 0x42d858). The bounce
    // house has a price but no cCommerce: nothing paid there
    if(!g.list.empty()) {
      auto &q=g.list.front();const Guests::Type &t=g.typeList[q.type];
      int at=-1;
      for(size_t i=0;i<objects.all().size();++i)if(objects.all()[i].id==shopId)at=static_cast<int>(i);
      if(at>=0) {
        PlacedObjects::Object &stand=objects.all()[static_cast<size_t>(at)];
        const Guests::BuildingInfo &bi=g.infoOf(PlacedObjects::fileOf(stand));
        const float was=stand.price;
        auto reset=[&](int b,const char *need){q.state=Guests::State::Walking;q.building=b;q.need=need;q.ride=0;
          q.dHappy=0;q.lastThought=0;q.thoughtArg.clear();q.hidden=false;};
        const double before=stand.income;
        stand.price=bi.price*Original::economy().itemExpensive;reset(at,"food");
        g.useBuilding(q);
        const bool turned=q.state!=Guests::State::Using&&q.building<0&&q.lastThought==10073&&
                          q.dHappy==static_cast<float>(t.priceAngry)&&objects.all()[static_cast<size_t>(at)].income==before;
        const bool doorVisit=!q.visits.empty()&&q.visits.back()==std::make_pair(int(std::floor(stand.x)),int(std::floor(stand.y)));
        objects.all()[static_cast<size_t>(at)].price=bi.price;reset(at,"food");
        g.useBuilding(q);
        const bool inside=q.state==Guests::State::Using;
        q.animTime=q.playFor;g.update(0.0f,0,1000);
        const double paid=objects.all()[static_cast<size_t>(at)].income-before;
        objects.all()[static_cast<size_t>(at)].price=was;
        out<<"PRICE door: turned away "<<turned<<" (thought "<<q.lastThought<<"), at its default in "<<inside
           <<", paid "<<paid<<" of "<<bi.price*bi.priceFactor<<" (cCommerce "<<bi.commerce<<")\n";
        check("a shop at cItemExpensive x its price or more turns guests away at the door, unpaid (0x42c8af)",turned&&bi.commerce);
        check("turned away at the door, the shop goes onto its visited list (0x42c8af -> 0x4a840e)",turned&&doorVisit);
        check("at its price a guest goes in and pays price x cPriceFactor coming out (guestBuy 0x42d858)",
              inside&&std::abs(paid-bi.price*bi.priceFactor)<1e-3);
        const size_t n=objects.objects().size();
        if(objects.add("scenery/building/bounce.ai",30.5f,30.5f,4)&&objects.objects().size()>n) {
          const int b=static_cast<int>(objects.objects().size())-1;
          const Guests::BuildingInfo &bb=g.infoOf(PlacedObjects::fileOf(objects.all()[static_cast<size_t>(b)]));
          reset(b,"fun");g.useBuilding(q);
          const bool bin=q.state==Guests::State::Using;
          q.animTime=q.playFor;g.update(0.0f,0,1000);
          const double bounced=objects.all()[static_cast<size_t>(b)].income;
          out<<"PRICE bounce house: price "<<bb.price<<", cCommerce "<<bb.commerce<<", in "<<bin<<", takings "<<bounced<<"\n";
          check("a building with a price but no cCommerce (the bounce house) charges nothing (guestBuy 0x42d858)",
                bb.price>0&&!bb.commerce&&bin&&bounced==0);
          objects.remove(b);
        }
        // Bought food held and eaten later (#195): inside a seller, the item
        // into its hands by its goal kind (0x42d1e2 -> holdItem 0x42d54e,
        // +0x26c[0] the food hand: its nameID); consumed there only if
        // consumenow. Out again with food in hand (chooseTask 0x42a8b7):
        // Task6b_GoEat - an 'eat' building (a bench) in cSearchRadius, off
        // there with the goal "eat", and its use eats the held food
        // (0x42d74e); none, eats standing (Task6d_EatStanding 0x52dd30:
        // consumed, cStandAndEatChange, bEatStanding)
        {
          bool anyEat=false;
          for(const auto &o:objects.objects())anyEat=anyEat||(!o.fence&&g.infoOf(PlacedObjects::fileOf(o)).satisfies.count("eat"));
          auto buy=[&]() {
            std::map<std::string,int> was=objects.all()[static_cast<size_t>(at)].sold;
            reset(at,"food");q.inventory[0]=0;q.eatStep=-1;q.leaving=false;q.hunger=60;q.dHunger=0;
            g.useBuilding(q);q.animTime=q.playFor;g.update(0.0f,0,1000);
            for(const auto &[name,n]:objects.all()[static_cast<size_t>(at)].sold)
              if(n!=(was.count(name)?was[name]:0))return name;
            return std::string();
          };
          std::string item;
          // (the stand draws one of its Sells at random: try until a held one)
          for(int k=0;k<12&&!anyEat;++k) {
            item=buy();
            if(!item.empty()&&!g.itemOf(item).itemNow)break;
          }
          const Guests::BuildingInfo *held=item.empty()?nullptr:&g.itemOf(item);
          // (#233: no seat - Task6c_Eat's walk about first, unitWalkAbout
          // 0x43d948: a tile on a ring 2..10 tiles round it (a neighbour
          // when none routes), a path when it's on one, the food still
          // held; on that tile, task 0x6d)
          const int sx0=static_cast<int>(std::floor(q.x)),sy0=static_cast<int>(std::floor(q.y));
          const bool fromPath=g.walkable(sx0,sy0);
          const bool walking=q.eatStep==Guests::kEatWalkAbout&&!q.path.empty()&&held&&q.inventory[0]==held->itemId;
          int ring=-1;bool ringPath=false;
          if(walking) {
            const int ex=static_cast<int>(std::floor(q.path.back().first)),ey=static_cast<int>(std::floor(q.path.back().second));
            ring=std::max(std::abs(ex-sx0),std::abs(ey-sy0));ringPath=!fromPath||g.walkable(ex,ey);
            q.x=q.path.back().first;q.y=q.path.back().second;q.pathAt=q.path.size()-1;
          }
          out<<"EAT walk about: walking "<<walking<<", ring "<<ring<<", from a path "<<fromPath<<", to a path "<<ringPath<<"\n";
          if(!anyEat)check("food held and no seat: a walk about first (2..10 tiles, keeping to the paths), not eaten yet (#233: Task6c_Eat 0x52d955, unitWalkAbout 0x43d948)",
                walking&&ring>=1&&ring<=10&&ringPath);
          if(walking)g.update(0.0f,0,1000);
          out<<"EAT standing: item "<<item<<", consumenow "<<(held?held->itemNow:-1)<<", eatStep "<<q.eatStep<<", anim "<<q.anim
             <<", hand "<<q.inventory[0]<<", dHappy "<<q.dHappy<<", dHunger "<<q.dHunger<<", thought "<<q.lastThought<<"\n";
          if(anyEat)out<<"EAT: an 'eat' building already placed - standing case skipped\n";
          else check("bought food that isn't consumenow is held, then eaten standing at the walk about's end: consumed, cStandAndEatChange, bEatStanding (#195: 0x42d54e, 0x52dd30)",
                held&&!held->itemNow&&q.eatStep==0&&q.anim=="eatstanding"&&q.inventory[0]==0&&
                q.dHunger==bi.hunger+held->itemHunger&&q.lastThought==held->consumedThought&&
                q.dHappy==(t.child?bi.childChange:bi.adultChange)+(t.child?held->itemChild:held->itemAdult)+
                          static_cast<float>(t.standAndEat));
          // A bench near: to it to eat (its goal "eat"), the food eaten on it
          const size_t n0=objects.objects().size();
          if(!anyEat&&held&&objects.add("scenery/other/bench.ai",26.5f,27.5f,0)&&objects.objects().size()>n0) {
            const int bench=static_cast<int>(objects.objects().size())-1;
            world.reindex();
            q.state=Guests::State::Walking;q.building=-1;q.need.clear();q.eatStep=-1;q.leaving=false;
            q.inventory[0]=held->itemId;
            g.goEat(q);
            const bool going=q.building==bench&&q.need=="eat"&&q.eatStep<0&&q.inventory[0]==held->itemId;
            // (held food: no looking for more however hungry, 0x43bbb5)
            bool ate=false,hungryLook=false;
            for(int k=0;k<4000&&!ate;++k) {
              g.update(0.05f,0,1000);
              const Guests::Guest *u=g.guest(q.id);if(!u)break;
              hungryLook=hungryLook||(u->need=="food");
              ate=u->inventory[0]==0;
            }
            const Guests::Guest *u=g.guest(q.id);
            out<<"EAT bench: going "<<going<<", ate "<<ate<<", looked for food "<<hungryLook<<", standing "<<(u?u->eatStep:-2)<<"\n";
            check("food in hand and a bench in cSearchRadius: off to it to eat, eaten there - not standing (#195: Task6b_GoEat 0x59d479, 0x42d74e)",
                  going&&ate&&u&&u->eatStep<0&&!hungryLook);
            objects.remove(bench);world.reindex();
          }
          // consumenow: eaten in the stand, nothing held
          std::string now;
          for(int k=0;k<12&&!anyEat;++k) {
            now=buy();
            if(!now.empty()&&g.itemOf(now).itemNow)break;
          }
          if(!now.empty()&&g.itemOf(now).itemNow)
            check("a consumenow item is eaten in the stand, nothing held after (#195: 0x42d1e2)",q.inventory[0]==0&&q.eatStep<0);
          else
            out<<"EAT: the stand sells nothing consumenow - that case not checked here\n";
          // (several Sells: one of them at random each sale, #206 - 0x42d1e2
          // at 0x42d234)
          {
            const size_t n1=objects.objects().size();
            if(objects.add("scenery/building/dksnacks.ai",30.5f,24.5f,4)&&objects.objects().size()>n1) {
              const int ds=static_cast<int>(objects.objects().size())-1;
              const Guests::BuildingInfo &snacks=g.infoOf(PlacedObjects::fileOf(objects.all()[static_cast<size_t>(ds)]));
              bool nowSeen=false,nowEaten=true;
              for(int k=0;k<30;++k) {
                const std::map<std::string,int> before=objects.all()[static_cast<size_t>(ds)].sold;
                reset(ds,"food");q.inventory[0]=0;q.eatStep=-1;q.leaving=false;
                g.useBuilding(q);q.animTime=q.playFor;g.update(0.0f,0,1000);
                for(const auto &[name,n]:objects.all()[static_cast<size_t>(ds)].sold)
                  if(n!=(before.count(name)?before.at(name):0)&&g.itemOf(name).itemNow) {
                    nowSeen=true;
                    nowEaten=nowEaten&&q.inventory[0]==0&&q.eatStep<0;
                  }
              }
              if(nowSeen)
                check("a consumenow item is eaten in the stand, nothing held after (#195: 0x42d1e2)",nowEaten);
              int kinds=0,sales=0;
              for(const auto &[name,n]:objects.all()[static_cast<size_t>(ds)].sold){kinds+=n>0;sales+=n;}
              out<<"SELLS: "<<snacks.sells.size()<<" items, "<<sales<<" sales of "<<kinds<<" of them\n";
              check("a building selling several items sells one of them at random each time (#206: 0x42d234)",
                    snacks.sells.size()>1&&sales==30&&kinds>1);
              objects.remove(ds);
            }
          }
          q.eatStep=-1;q.inventory[0]=0;
        }
        // Thoughts as thoughtPush 0x42311a (#196): always onto the front of
        // the zoo's list, repeats kept, the oldest dropped past 1000;
        // Guest Information the guest's newest 5 (0x46a7c8)
        {
          q.thoughts.clear();const size_t logWas=g.thoughtLog.size();
          g.think(q,10079);g.think(q,10079);
          const bool both=q.thoughts.size()==2&&q.thoughts[0]==q.thoughts[1]&&g.thoughtLog.size()==logWas+2;
          for(int k=0;k<8;++k)g.think(q,10079+(k%3));
          const bool five=q.thoughts.size()==static_cast<size_t>(Original::guestInfoThoughts);
          for(int k=0;k<Original::thoughtListCap+20;++k)g.think(q,10082);
          const bool capped=g.thoughtLog.size()==static_cast<size_t>(Original::thoughtListCap);
          out<<"THOUGHTS: repeat kept "<<both<<", guest's "<<q.thoughts.size()<<", zoo's "<<g.thoughtLog.size()<<"\n";
          check("a thought is always added, a repeat too (#196: thoughtPush 0x42311a)",both);
          check("Guest Information keeps the guest's newest 5, the zoo's list 1000 (0x46a7c8, ZTThoughtMgr +0xc)",five&&capped);
          q.thoughts.clear();g.thoughtLog.clear();
        }
      }
      // Its recent-species list (zoo.exe +0x278) kept by a save and a
      // shared .zoo: the same kinds, oldest first, after loading
      {
        const int zebra=a.typeOfFile("animals/zebra.ai"),lion=a.typeOfFile("animals/lion.ai");
        if(zebra>=0&&lion>=0&&!g.list.empty()) {
          const std::vector<int> want={a.types()[lion].nameId,a.types()[zebra].nameId,a.types()[lion].nameId};
          const int id=g.list.front().id;g.list.front().recentSpecies=want;
          // (#236: and attacked by a predator, +0x33b - ZTGuest::save
          // 0x47873e at 0x478bc4, load 0x455fa2 at 0x456976; our save v22)
          g.list.front().attacked=true;
          ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
          Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
          for(const char *extension:{".zt1save",".zoo"}) {
            const auto file=std::filesystem::temp_directory_path()/("zt1-parity-recent-"+std::to_string(SDL_GetTicks())+extension);
            const bool ok=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn")&&SaveGame::load(file.string(),world,books,hud);
            const Guests::Guest *back=g.guest(id);
            out<<"RECENT "<<extension<<": "<<ok<<", "<<(back?static_cast<int>(back->recentSpecies.size()):-1)<<" kinds\n";
            check(std::string("a guest's recent-species list is kept by a ")+extension+" save (+0x278)",ok&&back&&back->recentSpecies==want);
            check(std::string("#236: a guest attacked by a predator is kept by a ")+extension+" save (+0x33b)",ok&&back&&back->attacked);
            std::error_code ec;std::filesystem::remove(file,ec);
            for(Guests::Guest &again:g.list)if(again.id==id){again.recentSpecies=want;again.attacked=true;}
          }
          SaveGame::goals=priorGoals;
        }
      }
    }
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
  // The sea dinosaurs (Plesiosaurus, Deinosuchus, Meiolania: cSwims,
  // cWaterNeeded/cLandNeeded, cUnderwater with an underwater section) on
  // the same shore and pool: in and out of the water, counted in the shore
  // (their habitat), which a scientist looks after
  for(const char *file:{"animals/plesio.ai","animals/deino.ai","animals/meiolan.ai"}) {
    a.clear();st.clear();world.reindex();
    const int kind=a.typeOfFile(file);
    const std::string name=std::string(file).substr(8,std::string(file).size()-11);
    check(name+" definition",kind>=0);
    if(kind<0)continue;
    const int dino=a.adopt(kind,false,46.5f,34.5f,map,f,true);
    auto *d=a.member(dino);
    check(name+" adopted on the shore, in its habitat",d && d->exhibit==shore);
    if(!d)continue;
    d->waterCheck=9999;d->boredCheck=9999;
    const bool into=a.walkTo(*d,51.5f,34.5f,map,f);
    for(int k=0;k<800 && (d=a.member(dino)) && !d->path.empty();++k)a.update(0.05f,k*0.05,map,f);
    d=a.member(dino);
    check(name+" swims from the shore into the pool",into && d && f.exhibitAt(int(d->x),int(d->y))==pool && d->exhibit==shore);
    const bool back=d && a.walkTo(*d,46.5f,34.5f,map,f);
    for(int k=0;k<800 && (d=a.member(dino)) && !d->path.empty();++k)a.update(0.05f,k*0.05,map,f);
    d=a.member(dino);
    check(name+" climbs back out onto the shore",back && d && f.exhibitAt(int(d->x),int(d->y))==shore);
    if(auto *e=f.exhibit(shore)){e->animals=a.countIn(shore);e->dinosaurs=a.dinosaursIn(shore);}
    const int sci=st.hire(st.typeOfFile("staff/scient.ai"),40.5f,30.5f,map,f);
    const int sciSays=sci>=0?st.assign(sci,shore,f):-1;
    check(name+": a scientist can be assigned to its shore ("+std::to_string(sci)+", "+std::to_string(sciSays)+")",
          sci>=0 && sciSays==0);
    const int keeper=st.hire(st.typeOfFile("staff/keeper.ai"),40.5f,31.5f,map,f);
    const int keeperSays=keeper>=0?st.assign(keeper,shore,f):-1;
    check(name+": a zookeeper can't ("+std::to_string(keeperSays)+")",keeperSays==10320);
    // (an egg-layer's egg goes on the shore's dry ground, never in the pool -
    // the mother out in the water: 0x60da6b's tiles are dry land)
    if(a.typeList[kind].laysEggs) {
      const int mum=a.adopt(kind,true,51.5f,34.5f,map,f,true);
      a.births.push_back({mum,-1,kind});a.lifeEvents(map,f);
      const Animals::Member *egg=nullptr;for(const auto &m:a.members())if(m.egg)egg=&m;
      check(name+": its egg is laid on the dry shore, not in the pool",egg && f.exhibitAt(int(egg->x),int(egg->y))==shore &&
            egg->exhibit==shore);
    }
  }
  a.clear();st.clear();
  // A free destination before another swimmer must not be avoided because
  // prediction incorrectly continues beyond the end of the route.
  a.clear();
  const int swimTank=rectangle(25,45,30,18,f.typeIndex("atltank"),"Steering regression");
  if(auto *tank=f.exhibit(swimTank))tank->water=12;
  world.reindex();
  const int shark=a.typeOfFile("animals/whleshrk.ai");
  check("whale shark swimming fixture available",shark>=0 && swimTank>=0);
  out<<"STEERING fixture type="<<shark<<" tank="<<swimTank<<"\n";
  if(shark>=0 && swimTank>=0) {
    // Planning: an animal's own plan straight at a point must succeed, with
    // nothing in the way, and end standing clear of the tank wall. (The
    // steering that used to be exercised here is gone - animals swim their
    // planned path the way the engine did before 2026.10.28.)
    Animals::Member swimmer;swimmer.id=910001;swimmer.type=shark;
    swimmer.exhibit=swimTank;swimmer.x=34;swimmer.y=54;swimmer.swimZ=swimmer.swimTo=3;
    swimmer.fx=1;swimmer.fy=0;swimmer.speed=4;swimmer.moveAnim="walk";
    const float target=swimmer.x+3;
    a.list={swimmer};world.reindex();
    check("swimmer can plan a clear route",a.walkTo(a.list[0],target,54,map,f));
    check("swimmer's plan ends clear of the wall",
      a.tankClearance(a.list[0],target,54,f)>=-0.001f);
    // zoo.exe 0x438a37: the tank water check. Very murky water (purity 10:
    // under 20, not under 1) against clean, the same tank otherwise - its
    // cVeryMurkyWaterChange and cVeryMurkyWaterHealthChange apart; then
    // nothing until cMurkyWaterCheck (5) ticks on
    if(auto *tk=f.exhibit(swimTank)) {
      const auto &T=a.typeList[shark];
      const bool wasTank=tk->tank;tk->tank=true; // (the steering fixture's walls alone don't make it one)
      auto once=[&](float purity,Animals::Member &w){tk->purity=purity;w.health=50;w.pending=0;w.murkyCheck=0;a.murkyWaterTick(w,map,f);};
      Animals::Member clean=a.list[0],murky=a.list[0];
      once(100,clean);once(10,murky);
      check("very murky tank water costs its cVeryMurkyWater happiness and health (0x438a37)",
            murky.pending-clean.pending==static_cast<float>(T.murkyChange[1]) &&
            murky.health-clean.health==static_cast<float>(T.murkyHealthChange[1]) && T.murkyHealthChange[1]<0);
      const float was=murky.health;int quiet=0;
      for(int k=0;k<4;++k){a.murkyWaterTick(murky,map,f);quiet+=murky.health==was;}
      check("the tank water check waits cMurkyWaterCheck ticks between goes",quiet==4 && murky.murkyCheck==0);
      tk->purity=100;tk->tank=wasTank;
    }
    // zoo.exe 0x459d94 / 0x459e09: no water while something in the tank
    // can't be under water - a zebra (no cUnderwater) or an egg, 10550 naming
    // its kind; the whale shark alone (cUnderwater 1) doesn't stop it
    if(auto *tk=f.exhibit(swimTank)) {
      const Fences::Exhibit before=*tk;tk->tank=true;
      const int zebra=a.typeOfFile("animals/zebra.ai");
      const auto keep=a.list;
      f.takeFillRefusals();
      auto fill=[&](){f.drainExhibit(swimTank);const bool ok=f.fillExhibit(swimTank,true);return ok && tk->water>0;};
      const bool sharkAlone=fill();
      bool zebraStops=false,eggStops=false;std::string named;
      if(zebra>=0) {
        Animals::Member z=a.list[0];z.id=910021;z.type=zebra;z.x=36.5f;z.y=55.5f;z.egg=false;
        a.list.push_back(z);world.reindex();
        zebraStops=!fill() && tk->water==0;
        const auto said=f.takeFillRefusals();named=said.empty()?"":said[0];
        a.list=keep;world.reindex();
      }
      a.list[0].egg=true;
      eggStops=!fill() && !f.takeFillRefusals().empty();
      a.list=keep;world.reindex();
      out<<"FILLBLOCK shark alone "<<sharkAlone<<" zebra "<<zebraStops<<" ("<<named<<") egg "<<eggStops<<"\n";
      check("a tank with only a whale shark in it fills (cUnderwater 1)",sharkAlone);
      check("a zebra in the tank: no water, 10550 names it (0x459d94)",zebraStops && zebra>=0 &&
            named==a.typeList[zebra].displayName && !named.empty());
      check("an egg in the tank: no water (0x459f52)",eggStops);
      *tk=before;
    }

    // Idling: two animals standing on the same spot must be parted. This is
    // the one thing 10.28 added that is kept, so it is what this guards.
    Animals::Member one=a.list[0], two=a.list[0];
    one.id=910011;one.x=40.5f;one.y=54.5f;one.fx=1;one.fy=0;
    one.path.clear();one.pathAt=0;one.speed=1;one.moveAnim="walk";one.anim="stand";
    one.awarenessCheck=0;
    two.id=910012;two.x=40.5f;two.y=54.5f;two.fx=1;two.fy=0;
    two.path.clear();two.pathAt=0;two.speed=1;two.moveAnim="walk";two.anim="stand";
    a.list={one,two};world.reindex();
    const float together=a.bodyDistance(a.list[0],a.list[0].x,a.list[0].y,
      a.bodyHeight(a.list[0],a.list[0].x,a.list[0].y,a.list[0].swimZ),
      a.list[1],a.list[1].x,a.list[1].y,
      a.bodyHeight(a.list[1],a.list[1].x,a.list[1].y,a.list[1].swimZ));
    for(int k=0;k<8;++k) a.separateIdle(a.list[0],0.5f,map,f);
    check("an idle pair really is standing on each other first",together<1.0f);
    check("the idle animal plans a step off the other",a.list[0].pathAt<a.list[0].path.size());
    out<<"IDLE together="<<together<<" steps="<<a.list[0].path.size()<<"\n";
  }
  a.clear();world.reindex();
  Guests::Guest arrived;arrived.arrived=0;
  for(const auto &[days,text]:std::vector<std::pair<double,int>>{{0,3420},{59.9,3420},{60,3421},{239.9,3421},{240,3422},{359.9,3422},{360,3423}}) {
    g.clock=days*ZooSim::kSecondsPerDay;check("original time-in-park boundary "+std::to_string(days),g.timeInParkText(arrived)==text);
  }
  // The dinosaurs only an electric fence holds (zoo.exe 0x414a73: their
  // cCrushesFences takes any fence no higher, unless its current is live -
  // T. rex, Allosaurus, Spinosaurus) stay in and come up to a normal
  // happiness (0x438c47) in a good electrified paddock - its terrain, the
  // foliage, rocks and objects they like, a shelter, a scientist with their
  // food - when its fence has stood 45 minutes unmended. A point of a
  // fence's wear takes each piece its own 8 to 20 minutes (0x434b1d,
  // 0x5b391c: fences/fence.ai [Decay]), so the current's still live then; it
  // took every piece the same few minutes before, the current gone in 40
  {
    a.clear();world.reindex();
    const int electric=f.typeIndex("dzchn");
    const int dex=rectangle(44,6,18,18,electric,"Dinosaur paddock");
    check("dinosaur paddock built",dex>=0);
    const Fences::Exhibit *dx=dex>=0?f.exhibit(dex):nullptr;
    // Its wear: no piece a whole point down before minDecayTime, each one
    // down by maxDecayTime, and not all at once
    if(dx) {
      std::vector<Fences::Edge> ring;
      for(const auto &[e,p]:f.pieces())if(f.exhibitOf(e)==dex && !p.gate)ring.push_back(e);
      const float whole=static_cast<float>(f.types()[electric].life);
      auto worn=[&]{int n=0;for(const auto &e:ring)n+=f.lifeOf(e)<=whole-1;return n;};
      for(int s=0;s<static_cast<int>(f.minDecayTime)-1;++s)f.update(1.0f);
      const int early=worn();
      int some=0;
      for(int s=static_cast<int>(f.minDecayTime)-1;s<static_cast<int>(f.maxDecayTime)+1;++s) {
        f.update(1.0f);
        const int n=worn();some+=n>0 && n<static_cast<int>(ring.size());
      }
      check("a fence's wear: a point takes each piece its own time, from minDecayTime to maxDecayTime ("+
            std::to_string(static_cast<int>(f.minDecayTime))+" to "+std::to_string(static_cast<int>(f.maxDecayTime))+" s)",
            f.minDecayTime>0 && f.maxDecayTime>f.minDecayTime && !ring.empty() && early==0 && some>0 &&
            worn()==static_cast<int>(ring.size()));
    }
    // (a path round it, its gate on the west side, and a scientist for it)
    for(int k=5;k<=24;++k)for(auto [x,y]:{std::pair{43,k},std::pair{62,k},std::pair{38+k,5},std::pair{38+k,24}})
      if(f.exhibitAt(x,y)<0 && world.canLayPath(x,y))map.setPath(x,y,"path");
    if(dx)f.moveGate({false,44,14});
    world.reindex();
    int scientist=-1;
    std::vector<std::string> pieces;
    for(const char *c:{"foliage","rocks","scenery","shelters","toys"})
      for(const CatalogItem *it:ItemCatalog::get().inCategory(c,false))pieces.push_back(it->file);
    for(size_t ti=0;dx && ti<a.typeList.size();++ti) {
      const Animals::Type &t=a.typeList[ti];
      if(!t.dinosaur || t.swims || t.crush<f.types()[electric].height)continue;
      const int kind=static_cast<int>(ti);
      // (a scientist of its own, starting at the gate)
      st.clear();world.reindex();
      scientist=st.hire(st.typeOfFile("staff/scient.ai"),43.5f,14.5f,map,f);
      if(scientist>=0)st.assign(scientist,dex,f);
      std::vector<std::pair<int,int>> tiles(dx->tiles.begin(),dx->tiles.end());
      const int N=static_cast<int>(tiles.size());
      // (its fence as new, then 45 minutes of wear with nobody mending it)
      std::vector<Fences::Edge> edges;
      for(const auto &[e,p]:f.pieces())if(f.exhibitOf(e)==dex)edges.push_back(e);
      for(const auto &e:edges)f.repair(e);
      for(int s=0;s<45*60;++s)f.update(1.0f);
      int live=0,ring=0;
      for(const auto &e:edges)if(const auto *p=f.at(e);p && !p->gate){++ring;live+=f.liveElectric(e);}
      check(t.key+": the paddock's current is still live after 45 minutes' wear ("+std::to_string(live)+" of "+
            std::to_string(ring)+" pieces)",ring>0 && live==ring);
      // Its terrain, in the shares it likes (the rest the one it likes best)
      int best=0;
      for(int k=0;k<18;++k)if(t.compatTerrain[k]>t.compatTerrain[best])best=k;
      size_t at=0;
      std::vector<int> terrainOf(tiles.size(),best);
      for(int k=0;k<18;++k)if(t.compatTerrain[k]>0 && k!=best)
        for(int n=0;n<(t.compatTerrain[k]*N+99)/100 && at<tiles.size();++n)terrainOf[at++]=k;
      for(size_t i=0;i<tiles.size();++i)map.getTileMutable(tiles[i].first,tiles[i].second)->terrainType=terrainOf[i];
      map.touch();
      // Free land tiles, spread about
      std::vector<std::pair<int,int>> spots;
      // (and the ground by the gate left clear, for the scientist and the food)
      for(size_t i=0;i<tiles.size();++i)if(terrainOf[i]!=9 && terrainOf[i]!=10 &&
          (std::abs(tiles[i].first-44)>2 || std::abs(tiles[i].second-14)>2))spots.push_back(tiles[i]);
      std::stable_sort(spots.begin(),spots.end(),[](auto p,auto q){return (p.first*7+p.second*3)%11<(q.first*7+q.second*3)%11;});
      size_t spot=0;
      // Its animals, as many as it wants of its kind: bought, so eggs
      const int n=std::max(1,t.numberMin);
      std::vector<int> herd;
      for(int i=0;i<n && spot<spots.size();++i,++spot)
        if(int id=a.adopt(kind,i%2==1,spots[spot].first+0.5f,spots[spot].second+0.5f,map,f);id>=0)herd.push_back(id);
      world.reindex();
      const size_t base=objects.objects().size();
      auto value=[&](const std::string &file){
        const Animals::ObjectInfo &o=a.infoOf(file);
        auto v=[&](int key){auto it=t.suitable.find(key);return it==t.suitable.end()?0:it->second;};
        return v(o.habitat)+v(o.nameId);
      };
      auto score=[&]{a.markDirty(dex);const Animals::Rating *r=herd.empty()?nullptr:a.rating(*a.member(herd[0]),map,f);return r?r->score:0.0f;};
      auto place=[&](const std::string &file){
        const Animals::ObjectInfo &o=a.infoOf(file);
        while(spot<spots.size()) {
          auto [x,y]=spots[spot++];
          if(objects.add(file,x+o.fx/4.0f,y+o.fy/4.0f,0))return true;
        }
        return false;
      };
      // The best liked of each sort: a tree or bush, a rock, anything else
      // small, and a shelter it can use
      std::string tree,rock,other,shelter;
      for(const std::string &file:pieces) {
        const Animals::ObjectInfo &o=a.infoOf(file);
        const int v=value(file);
        if(o.building) {
          if(o.shelter && o.capacity>=n && v>=0 && (shelter.empty() || v>value(shelter)))shelter=file;
          continue;
        }
        if(v<=0 || o.fx>2 || o.fy>2 || o.stink)continue;
        std::string &slot=o.foliage?tree:o.rock?rock:other;
        if(slot.empty() || v>value(slot))slot=file;
      }
      if(t.needShelter && !shelter.empty())place(shelter);
      // Then as a player would: whatever helps most, one at a time
      float now=score();
      for(int added=0;added<200;++added) {
        std::string pick;float gain=0;
        for(const std::string *c:{&tree,&rock,&other}) {
          if(c->empty())continue;
          const size_t s=spot;
          if(!place(*c))continue;
          const float sc=score();
          objects.remove(static_cast<int>(objects.objects().size())-1);spot=s;
          if(sc>now+gain){gain=sc-now;pick=*c;}
        }
        if(pick.empty())break;
        place(pick);now=score();
      }
      // Then 20 minutes, the scientist feeding them: hatched, zapped by the
      // fence till they've learnt it (0x438ebc), and happy after
      world.reindex();
      float peak=-100;
      const bool paused=world.isPaused();world.setPaused(false);
      for(int k=0;k<24000;++k) {
        world.update(SDL_GetKeyboardState(nullptr),0.05f);
        if(k<12000 || k%20)continue;
        float least=herd.empty()?-100.0f:100.0f;
        for(int id:herd)if(const auto *m=a.member(id))least=std::min(least,m->happiness);
        peak=std::max(peak,least);
      }
      world.setPaused(paused);
      int loose=0;
      for(int id:herd)if(const auto *m=a.member(id))loose+=m->exhibit!=dex;
      const Animals::Rating *r=herd.empty()||!a.member(herd[0])?nullptr:a.rating(*a.member(herd[0]),map,f);
      out<<"DINOSAUR "<<t.key<<" x"<<n<<": happiness up to "<<peak<<", "<<loose<<" out; its exhibit "<<(r?r->score:-1)
         <<" (it wants "<<t.habitatPreference<<"), food there "<<world.getItems().foodIn(dex)<<"\n";
      check(t.key+" stays in its electrified paddock and comes up to a normal happiness",
            !herd.empty() && loose==0 && peak>=50);
      a.clear();
      for(int i=static_cast<int>(objects.objects().size())-1;i>=static_cast<int>(base);--i)objects.remove(i);
      world.getItems().removeIn(dex,ZooItems::Kind::Food);world.getItems().removeIn(dex,ZooItems::Kind::Dung);
      for(auto [x,y]:tiles)map.getTileMutable(x,y)->terrainType=0;
      map.touch();world.reindex();
    }
  }
  // Zookeeper Recommendations for an animal its exhibit makes unhappier
  // (0x438c47: suitability under its cHabitatPreference) - a Herrerasaurus
  // (it wants 90) in a paddock of its terrain, a little foliage and rock
  // near what it likes, few native objects: the original's flags (0x447164)
  // find nothing and it says "well suited" while the animal grows angrier
  // (measured in zoo.exe with a player's zoo); with suitabilityAdvice on,
  // the parts costing it are named, the native objects first
  {
    a.clear();world.reindex();
    const int herrera=a.typeOfFile("animals/herrera.ai");
    // (clear ground in the zoo for it: no exhibit, path or object near)
    int px=-1,py=-1;
    for(int y=2;y+14<map.getHeight() && px<0;++y)for(int x=2;x+12<map.getWidth() && px<0;++x) {
      bool clear=true;
      for(int j=-1;j<=12 && clear;++j)for(int i=-1;i<=10 && clear;++i)
        clear=f.insideZoo(x+i,y+j) && f.exhibitAt(x+i,y+j)<0 && !map.isPath(x+i,y+j);
      for(const auto &o:objects.objects())clear=clear && (o.x<x-2 || o.x>x+12 || o.y<y-2 || o.y>y+14);
      if(clear)px=x,py=y;
    }
    const int hex=herrera<0 || px<0?-1:rectangle(px,py,10,12,f.typeIndex("chainlnk"),"Advice paddock");
    check("advice paddock built",herrera>=0 && hex>=0);
    const Fences::Exhibit *hx=hex>=0?f.exhibit(hex):nullptr;
    check("advice paddock: 120 tiles, as the player's",hx && hx->tiles.size()==120);
    if(hx) {
      const Animals::Type &t=a.typeList[herrera];
      std::vector<std::pair<int,int>> tiles(hx->tiles.begin(),hx->tiles.end());
      const int N=static_cast<int>(tiles.size());
      // (its terrain in the shares it likes, the rest its favourite)
      int best=0;
      for(int k=0;k<18;++k)if(t.compatTerrain[k]>t.compatTerrain[best])best=k;
      std::vector<int> terrainOf(tiles.size(),best);
      size_t at=0;
      for(int k=0;k<18;++k)if(t.compatTerrain[k]>0 && k!=best)
        for(int n=0;n<(t.compatTerrain[k]*N+50)/100 && at<tiles.size();++n)terrainOf[at++]=k;
      for(size_t i=0;i<tiles.size();++i)map.getTileMutable(tiles[i].first,tiles[i].second)->terrainType=terrainOf[i];
      map.touch();
      std::vector<std::pair<int,int>> land;
      for(size_t i=0;i<tiles.size();++i)if(terrainOf[i]!=9 && terrainOf[i]!=10)land.push_back(tiles[i]);
      // (trees it doesn't mind on its cTreePref share of the tiles, and two
      // rocks)
      std::string tree,rock;
      for(const char *c:{"foliage","rocks"})
        for(const CatalogItem *it:ItemCatalog::get().inCategory(c,false)) {
          const Animals::ObjectInfo &o=a.infoOf(it->file);
          auto v=[&](int key){auto i=t.suitable.find(key);return i==t.suitable.end()?0:i->second;};
          if(o.building || o.fx>2 || o.fy>2 || o.stink || v(o.habitat)+v(o.nameId)<0)continue;
          if(o.foliage && v(o.habitat)+v(o.nameId)==0 && tree.empty())tree=it->file;
          if(o.rock && !o.foliage && rock.empty())rock=it->file;
        }
      const size_t base=objects.objects().size();
      size_t spot=0;
      const int trees=(t.treePref*N+50)/100;
      for(int n=0;n<trees && !tree.empty() && spot<land.size();++n,spot+=7)
        objects.add(tree,land[spot].first+0.5f,land[spot].second+0.5f,0);
      for(int n=0;n<2 && !rock.empty() && spot<land.size();++n,spot+=7)
        objects.add(rock,land[spot].first+0.5f,land[spot].second+0.5f,0);
      // (one grown male: company and room as it likes)
      const int id=a.adopt(herrera,false,land[1].first+0.5f,land[1].second+0.5f,map,f,true);
      world.reindex();a.markDirty(hex);
      const int s=id>=0?a.suitability(id,map,f):-1;
      auto says=[&](const std::vector<std::pair<std::string,bool>> &lines,int sid,bool *red=nullptr){
        std::string text=rm?rm->getString(sid):"";text=text.substr(0,text.find("%s"));
        for(const auto &[line,r]:lines)if(!text.empty() && line.rfind(text,0)==0){if(red)*red=r;return true;}
        return false;
      };
      const bool was=Features::suitabilityAdvice;
      Features::suitabilityAdvice=false;
      const auto original=id>=0?a.advice(id,map,f):std::vector<std::pair<std::string,bool>>{};
      Features::suitabilityAdvice=true;
      const auto ours=id>=0?a.advice(id,map,f):std::vector<std::pair<std::string,bool>>{};
      Features::suitabilityAdvice=was;
      for(const auto &[line,r]:original)out<<"ADVICE original: "<<line<<"\n";
      for(const auto &[line,r]:ours)out<<"ADVICE suitabilityadvice: "<<line<<(r?" (red)":"")<<"\n";
      {int kind=0,kindAdults=0,adults=0;if(const auto *m=a.member(id))a.company(*m,kind,kindAdults,adults);
       out<<"ADVICE paddock "<<N<<" tiles, exhibit "<<f.exhibit(hex)->tiles.size()<<", kind "<<kind<<" adults "<<adults<<" density "<<t.animalDensity<<"\n";}
      out<<"ADVICE herrera suitability "<<s<<" (wants "<<t.habitatPreference<<"), "<<trees<<" "<<tree<<", 2 "<<rock<<"\n";
      bool objectsRed=false;
      check("a Herrerasaurus in a paddock short of native objects is under the suitability it wants",s>=0 && s<t.habitatPreference);
      check("off: its recommendations are the original's, the exhibit well suited",original.size()==1 && says(original,9900));
      check("suitabilityadvice: not called well suited, its missing native objects named first and red",
            !ours.empty() && !says(ours,9900) && says(ours,9915,&objectsRed) && objectsRed && ours.front().first.rfind(rm->getString(9915).substr(0,20),0)==0);
      // (and at what it wants, nothing more than the original says)
      Features::suitabilityAdvice=true;
      const int wants=t.habitatPreference;
      a.typeList[herrera].habitatPreference=std::max(0,s);
      const auto content=a.advice(id,map,f);
      a.typeList[herrera].habitatPreference=wants;
      Features::suitabilityAdvice=was;
      check("suitabilityadvice: an animal its exhibit suits is told what the original tells it",content==original);
      a.clear();
      for(int i=static_cast<int>(objects.objects().size())-1;i>=static_cast<int>(base);--i)objects.remove(i);
      for(auto [x,y]:tiles)map.getTileMutable(x,y)->terrainType=0;
      map.touch();world.reindex();
    }
  }
  // Staff. The maintenance work list (zoo.exe Task78_MaintFindWork 0x439cd0
  // over the list 0x638fa0 built by 0x434e30): the nearest work by squared
  // tile distance, a bin no different from litter; a strictly nearer worker
  // takes a job over and the other drops it (0x4a1ee2). Tour guides: an
  // unassigned one's trip only to habitats with animals (0x58af73 ->
  // 0x440d9a), and the crowd check's tiles x first, then y (0x58d349).
  a.clear();st.clear();g.clear();world.reindex();
  {
    auto &items=world.getItems();
    // (what earlier fixtures left: worn fences, dung and litter about)
    {std::vector<Fences::Edge> worn;for(const auto &kv:f.pieces())if(f.worn(kv.first))worn.push_back(kv.first);
     for(const auto &e:worn)f.repair(e);}
    for(int id:items.ofKind(ZooItems::Kind::Litter,-1))items.remove(id);
    for(int id:items.ofKind(ZooItems::Kind::Dung,-1))if(items.item(id)&&items.item(id)->exhibit<0)items.remove(id);
    for(auto &o:objects.all())if(o.trashCan)o.fill=0;
    const int maintType=st.typeOfFile("staff/maint.ai");
    check("maintenance worker definition",maintType>=0);
    const bool canAdded=objects.add("scenery/other/trshcan.ai",24.5f,38.5f,0);
    const int canId=canAdded&&objects.last()?objects.last()->id:-1;
    if(auto *can=objects.byId(canId))can->fill=20;
    check("a full trash can placed",canId>=0&&objects.byId(canId)&&objects.byId(canId)->trashCan);
    if(maintType>=0&&canId>=0) {
      const int litter=items.add(ZooItems::Kind::Litter,-1,24.5f,32.5f);
      const int w=st.hire(maintType,24.5f,30.5f,map,f);
      world.reindex();
      Staff::Member *m=st.member(w);
      const bool picked=m&&st.findWork(*m,map,f);
      if(m)out<<"STAFF first worker's job "<<static_cast<int>(m->job)<<" item "<<m->jobItem<<" (litter "<<litter<<", bin "<<canId<<")\n";
      check("maintenance: the nearest work first - litter 2 tiles off before a full bin 8 off (0x439cd0)",
            picked&&m->job==Staff::Job::Litter&&m->jobItem==litter);
      const int w2=st.hire(maintType,24.5f,33.5f,map,f);
      world.reindex();
      Staff::Member *m2=st.member(w2);
      const bool took=m2&&st.findWork(*m2,map,f);
      m=st.member(w);m2=st.member(w2);
      check("maintenance: a worker nearer the litter takes it over, the other drops its job (0x4a1ee2)",
            took&&m2&&m2->job==Staff::Job::Litter&&m2->jobItem==litter&&m&&m->job==Staff::Job::None);
      const bool next=m&&st.findWork(*m,map,f);
      m=st.member(w);
      check("maintenance: the one that lost it goes to the next nearest, the bin",
            next&&m&&m->job==Staff::Job::Trash&&m->jobItem==canId);
      items.remove(litter);
      const int w3=st.hire(maintType,24.5f,24.5f,map,f);
      world.reindex();
      Staff::Member *m3=st.member(w3);
      const bool farther=m3&&!st.findWork(*m3,map,f);
      m=st.member(w);
      check("maintenance: a farther worker doesn't take a job over",farther&&m&&m->job==Staff::Job::Trash);
    }
    for(size_t i=0;i<objects.objects().size();++i)if(objects.objects()[i].id==canId){objects.remove(static_cast<int>(i));break;}
    st.clear();world.reindex();
    // Tour guides
    const int guideType=st.typeOfFile("staff/tour.ai");
    const int zebra=a.typeOfFile("animals/zebra.ai");
    const int empty=rectangle(44,46,4,4,f.typeIndex("chainlnk"),"Parity empty");
    const int z=zebra>=0?a.adopt(zebra,false,30.5f,32.5f,map,f,true):-1;
    for(const auto &ex:f.exhibits())if(auto *e=f.exhibit(ex.id))e->animals=a.countIn(ex.id);
    world.reindex();
    check("tour fixtures: a paddock with a zebra and an empty exhibit",guideType>=0&&z>=0&&empty>=0&&
          f.exhibit(land)&&f.exhibit(land)->animals>0&&f.exhibit(empty)&&f.exhibit(empty)->animals==0);
    if(guideType>=0&&z>=0&&empty>=0) {
      const int guide=st.hire(guideType,24.5f,30.5f,map,f);
      int trips=0,toLand=0;
      // (a guest at the paddock's west side: a tour goes only where guests
      // are - 0x58ae2c)
      g.admit(0);
      for(int k=0;k<20;++k) {
        Staff::Member *ld=st.member(guide);
        if(!ld)break;
        g.releaseTour(guide);
        if(!g.list.empty()){auto &q=g.list.back();q.x=25.5f;q.y=30.5f;q.state=Guests::State::Gawking;q.guide=-1;q.path.clear();}
        ld->job=Staff::Job::None;ld->path.clear();ld->pathAt=0;ld->x=24.5f;ld->y=30.5f;
        if(st.tourWork(*ld,map,f)){++trips;toLand+=ld->visit==land;}
      }
      check("an unassigned guide's trips go only to habitats with animals ("+std::to_string(trips)+" of 20, 0x58af73)",
            trips==20&&toLand==20);
      g.releaseTour(guide);g.clear();
      // Crowd check order: guests listed right, then lower left, then upper
      // left of the guide; a cap of 2 takes the two at x-1 (y-1, then y+1)
      Staff::Member *ld=st.member(guide);
      auto &type=st.typeList[guideType];const auto originalType=type;
      if(ld) {
        type.followChance=100;type.maxGroup=2;type.crowdRadius=std::max(1,type.crowdRadius);
        ld->job=Staff::Job::Tour;ld->visit=land;ld->goalX=25;ld->goalY=31;ld->path.clear();ld->pathAt=0;
        ld->x=24.5f;ld->y=30.5f;ld->followers.clear();
        const std::pair<int,int> at[3]={{25,30},{23,31},{23,29}};
        for(auto [x,y]:at) {
          g.admit(0);auto &guest=g.list.back();
          guest.x=x+0.5f;guest.y=y+0.5f;guest.hunger=guest.thirst=guest.bathroom=guest.tired=0;
          guest.state=Guests::State::Walking;guest.guide=-1;
        }
        world.reindex();
        st.gatherFollowers(*ld);
        const bool order=g.list.size()==3&&ld->followers.size()==2&&
                         ld->followers[0]==g.list[2].id&&ld->followers[1]==g.list[1].id;
        check("the crowd check looks at the tiles x first, then y (0x58d349)",order);
        g.releaseTour(guide);
      }
      type=originalType;
    }
    // Walking speed (zoo.exe 0x435c42 at zoo.ini UpdateRate 15): whole 64ths
    // of a tile an update, at least one - cSlowRate 33 two, 0.466 tiles a
    // second (a tour guide measured 0.46-0.47, #152), cFastRate 66 four
    {
      bool speeds=true;
      for(const auto &t:st.typeList)
        if(t.kind!=Staff::Kind::Helicopter)speeds=speeds&&t.speed==Original::unitTilesPerSecond(t.slowRate);
      using Original::unitTilesPerSecond;
      out<<"STAFF speed 33 "<<unitTilesPerSecond(33)<<" 66 "<<unitTilesPerSecond(66)<<"\n";
      // Guests by the same rule (0x435c42), their rate by tiredness (0x42250a,
      // measured: 25 for every kind when rested): 1 step an update rested or
      // middling (25, 28), 2 when tired (30)
      {
        bool ok=g.typeList.size()==4;
        for(const auto &gt:g.typeList){
          out<<"GUEST speed "<<gt.key<<" cSlowRate "<<gt.slowRate<<" "<<gt.speed<<" run "<<gt.runSpeed<<"\n";
          ok=ok&&
             gt.slowRate==(gt.kind==Guests::Kind::Man?33:gt.kind==Guests::Kind::Girl?22:26)&&
             std::abs(gt.speed-1000.0f/67/64)<1e-5f&&
             gt.runSpeed==unitTilesPerSecond(gt.child?44:56);
        }
        ok=ok&&Original::guestWalkRate(10)==25&&Original::guestWalkRate(50)==28&&Original::guestWalkRate(80)==30&&
           unitTilesPerSecond(25)==unitTilesPerSecond(28)&&std::abs(unitTilesPerSecond(30)-2000.0f/67/64)<1e-5f;
        check("guests walk at rate 25/28/30 by tiredness, 1/64 a tile an update unless tired (0x42250a, 0x435c42; measured)",ok);
      }
      check("staff walk whole steps an update: cSlowRate 33 at 0.466 tiles a second, 34 no faster, 66 at 0.933, "
            "under 15 one step (0x435c42)",
            speeds&&std::abs(unitTilesPerSecond(33)-2000.0f/67/64)<1e-5f&&unitTilesPerSecond(34)==unitTilesPerSecond(33)&&
            std::abs(unitTilesPerSecond(66)-4000.0f/67/64)<1e-5f&&unitTilesPerSecond(10)==unitTilesPerSecond(1)&&
            guideType>=0&&std::abs(st.typeList[guideType].speed-0.4664f)<0.001f);
    }
    // Maintenance: work it has no way to goes on its list (the jobs' route
    // failures -> 0x4a97ed), passed over in its looks (0x4a1d4c) until the
    // list is emptied every cClearInvalidListInterval, 10 s (0x439c06)
    {
      const int maintType=st.typeOfFile("staff/maint.ai");
      const int shut=items.add(ZooItems::Kind::Litter,-1,27.5f,29.5f); // (in the paddock)
      const int open=items.add(ZooItems::Kind::Litter,-1,22.5f,38.5f);
      const int w=maintType>=0?st.hire(maintType,22.5f,32.5f,map,f):-1;
      out<<"STAFF maintenance worker "<<w<<"\n";
      world.reindex();
      Staff::Member *m=st.member(w);
      const bool took=m&&st.findWork(*m,map,f);
      m=st.member(w);
      if(m)out<<"STAFF unreachable litter "<<shut<<" (in "<<f.exhibitAt(27,29)<<"), open "<<open<<": took "<<took
             <<" job "<<static_cast<int>(m->job)<<" item "<<m->jobItem<<" list "<<m->invalid.size()<<"\n";
      check("maintenance: litter it can't get to goes on its list, the next nearest taken (0x4a97ed, 0x4a1d4c)",
            maintType>=0&&took&&m&&m->job==Staff::Job::Litter&&m->jobItem==open&&m->invalid.size()==1&&
            m->invalid[0]==std::make_pair(27,29));
      bool cleared=false;
      if(m) {
        m->workCheck=999;m->invalidClear=0.5f;
        st.update(0.3f,map,f);m=st.member(w);
        const bool kept=m&&m->invalid.size()==1;
        st.update(0.3f,map,f);m=st.member(w);
        cleared=kept&&m&&m->invalid.empty()&&std::abs(m->invalidClear-st.typeList[maintType].clearInvalidMs/1000.0f)<0.01f;
      }
      check("maintenance: its list is emptied every cClearInvalidListInterval ("+std::to_string(maintType>=0?st.typeList[maintType].clearInvalidMs:0)+
            " ms, 0x439c06)",cleared&&st.typeList[maintType].clearInvalidMs==10000);
      items.remove(shut);items.remove(open);
      // Litter swept by someone else while it's on the way (Job7_GoToTrashPile's
      // checkArrived, zoo.exe 0x52cef4, each tick): "Done with trash piles",
      // the job over where it stands, not walked to the empty tile (#172)
      m=st.member(w);
      if(m) {
        const int far=items.add(ZooItems::Kind::Litter,-1,22.5f,38.5f);
        m->job=Staff::Job::None;m->path.clear();m->pathAt=0;m->x=22.5f;m->y=32.5f;m->invalid.clear();
        m->anim="idle";m->playsLeft=0;m->idleLeft=0;
        world.reindex();
        const bool took=st.findWork(*m,map,f);
        st.update(0.1f,map,f);m=st.member(w);
        const bool going=m&&m->job==Staff::Job::Litter&&m->jobItem==far&&m->pathAt<m->path.size();
        items.remove(far);
        st.update(0.1f,map,f);m=st.member(w);
        out<<"STAFF litter gone on the way: took "<<took<<" going "<<going<<" job "<<(m?static_cast<int>(m->job):-1)
           <<" path "<<(m?m->path.size():0)<<" at "<<(m?m->y:0)<<"\n";
        check("maintenance: litter swept before it gets there - the job ends on the way, not at the empty tile (0x52cef4)",
              took&&going&&m&&m->job==Staff::Job::None&&m->path.empty()&&m->y<35);
        // (dung on the grounds the same: keeperDoneZoodoo 0x4a25df)
        m=st.member(w);
        if(m) {
          const int pile=items.add(ZooItems::Kind::Dung,-1,22.5f,38.5f);
          m->job=Staff::Job::None;m->path.clear();m->pathAt=0;m->x=22.5f;m->y=32.5f;m->invalid.clear();
          m->anim="idle";m->playsLeft=0;m->idleLeft=0;
          world.reindex();
          const bool tookDung=st.findWork(*m,map,f);
          st.update(0.1f,map,f);m=st.member(w);
          const bool goingDung=m&&m->job==Staff::Job::Dung&&m->jobItem==pile&&m->pathAt<m->path.size();
          items.remove(pile);
          st.update(0.1f,map,f);m=st.member(w);
          check("maintenance: dung bagged by someone else before it gets there - the job ends on the way (0x4a25df)",
                tookDung&&goingDung&&m&&m->job==Staff::Job::None&&m->path.empty()&&m->y<35);
        }
      }
      // A fence mended (Job17_FixFence 0x59a596 -> 0x59a51d): the piece
      // alone; with cFixFenceModifier over 0, the pieces either side too
      m=st.member(w);
      if(m&&maintType>=0) {
        auto &mt=st.typeList[maintType];const int modifier=mt.fixFenceModifier;
        const Fences::Edge row[3]={{true,28,28},{true,29,28},{true,30,28}};
        auto mend=[&](int mod){
          for(const auto &e:row)f.setLife(e,1.0f);
          mt.fixFenceModifier=mod;
          Staff::Member *x=st.member(w);
          if(!x)return std::string("?");
          x->job=Staff::Job::Fence;x->jobEdge=row[1];x->anim="fix";x->playsLeft=1;x->animTime=1000;x->path.clear();x->pathAt=0;
          st.update(0.01f,map,f);
          std::string s;for(const auto &e:row)s+=f.lifeOf(e)>1.0f?'M':'-';
          return s;
        };
        const std::string plain=mend(0),trained=mend(1);
        mt.fixFenceModifier=modifier;
        out<<"STAFF fence mend: untrained "<<plain<<" trained "<<trained<<"\n";
        check("a fence mended: the piece alone, with cFixFenceModifier the pieces either side too (0x59a51d)",
              plain=="-M-"&&trained=="MMM");
      }
      if(w>=0)st.fire(w);
    }
    // A keeper's visit with work it has no way to (Job5_GoCleanDirt 0x412629,
    // Job3_GoHealAnimal 0x508b3c): the dung pile stays, its tile on the
    // keeper's list; the sick animal on its other list; 11004 for each
    if(z>=0) {
      Animals::Member *zm=a.member(z);
      const float zx=zm?zm->x:0,zy=zm?zm->y:0,zh=zm?zm->health:0;
      std::vector<int> rocks;
      auto rock=[&](int x,int y){if(objects.add("scenery/other/lrock1.ai",x+0.5f,y+0.5f,0)&&objects.last())rocks.push_back(objects.last()->id);};
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy){rock(33+dx,35+dy);rock(29+dx,35+dy);}
      const int pile=items.add(ZooItems::Kind::Dung,land,33.5f,35.5f);
      if(zm){zm->x=29.5f;zm->y=35.5f;zm->health=a.types()[zm->type].maxHits*0.5f;}
      world.reindex();
      const int keeperType=st.typeOfFile("staff/keeper.ai");
      const int k=keeperType>=0?st.hire(keeperType,22.5f,30.5f,map,f):-1;
      Staff::Member *km=st.member(k);
      st.takeNotices();
      out<<"STAFF keeper for unreachable work "<<k<<" zebra "<<(zm?zm->name:"?")<<"\n";
      if(km&&zm) {
        km->x=27.5f;km->y=29.5f;
        km->job=Staff::Job::Visit;km->visit=land;km->access=land;km->phase=3;km->inX=27.5f;km->inY=29.5f;
        km->outX=27.5f;km->outY=27.5f;km->invalid.clear();km->unreachable.clear();
        st.visitStep(*km,map,f);
        km=st.member(k);
        const auto told=st.takeNotices();
        std::string said;for(const auto &n:told)said+="["+n.text+"]";
        out<<"STAFF keeper unreachable work: says "<<said<<" pile "<<(items.item(pile)?"kept":"gone")
           <<" tiles "<<(km?km->invalid.size():0)<<" animals "<<(km?km->unreachable.size():0)<<"\n";
        zm=a.member(z);
        check("a sick animal it has no way to: on its list, unhealed, \"...having trouble getting to\" it (11004, 0x508b3c)",
              km&&zm&&km->unreachable.size()==1&&km->unreachable[0]==z&&zm->health<a.types()[zm->type].maxHits*0.6f&&
              told.size()==2&&told[0].kind==2&&told[0].text.find(zm->name)!=std::string::npos);
        check("dung it has no way to: the pile stays, its tile on the list, 11004 with the exhibit's name (0x412629)",
              km&&items.item(pile)&&km->invalid.size()==1&&km->invalid[0]==std::make_pair(33,35)&&told.size()==2&&
              told[1].text.find("Parity paddock")!=std::string::npos);
      }
      // The dart's range (Job13_Tranquilize 0x50cffd -> 0x50cf5b): the
      // squared distance between the tiles against cWeaponRange squared
      km=st.member(k);zm=a.member(z);
      if(km&&zm) {
        auto fires=[&](float ax){
          Staff::Member *x=st.member(k);Animals::Member *y=a.member(z);
          if(!x||!y)return false;
          y->escaped=true;y->x=ax;y->y=40.5f;y->claimedBy=k;
          x->job=Staff::Job::Catch;x->jobAnimal=z;x->phase=0;x->x=10.9f;x->y=40.5f;x->playsLeft=0;x->anim="idle";x->path.clear();x->pathAt=0;
          st.catchStep(*x,0,map,f);
          x=st.member(k);
          return x&&x->playsLeft==1&&x->anim=="fire";
        };
        const bool five=fires(15.95f),six=fires(16.05f);
        out<<"STAFF dart: 5 tiles off (5.05) "<<five<<", 6 tiles off (5.15) "<<six<<" range "<<st.typeList[keeperType].weaponRange<<"\n";
        check("the dart flies at cWeaponRange tiles (5), counted tile to tile, not by the exact distance (0x50cf5b)",
              five&&!six&&st.typeList[keeperType].weaponRange==5);
        if(Animals::Member *y=a.member(z)){y->escaped=false;y->claimedBy=-1;}
      }
      if(Animals::Member *y=a.member(z)){y->x=zx;y->y=zy;y->health=zh;}
      items.remove(pile);
      for(int id:rocks)for(size_t i=0;i<objects.objects().size();++i)
        if(objects.objects()[i].id==id){objects.remove(static_cast<int>(i));break;}
      if(k>=0)st.fire(k);
      world.reindex();
    }
    // The food spot (keeperPickFoodSpot 0x4e7a7d, Job1_GoDropFood 0x4993b9):
    // the keeper takes the one spot it's given; no way to it, and the tile
    // goes on its +0x27c list and the food job ends (no other tile tried);
    // the 20th tile on the list empties it, puts the animal on its +0x270
    // list and says it can't get there (11004) (#169)
    if(z>=0) {
      std::vector<int> rocks;
      auto rock=[&](int x,int y){if(objects.add("scenery/other/lrock1.ai",x+0.5f,y+0.5f,0)&&objects.last())rocks.push_back(objects.last()->id);};
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)rock(33+dx,35+dy);
      for(int id:items.ofKind(ZooItems::Kind::Food,land))items.remove(id);
      for(int id:items.ofKind(ZooItems::Kind::Dung,land))items.remove(id);
      if(auto *e=f.exhibit(land))e->animals=a.countIn(land);
      world.reindex();
      const int keeperType=st.typeOfFile("staff/keeper.ai");
      const int k=keeperType>=0?st.hire(keeperType,22.5f,30.5f,map,f):-1;
      // (the keeper at 33,37, the paddock's bottom row: every free tile
      // within reach of the spot pick - squared distance under 10 - on its
      // list already, but the walled-in 33,35)
      std::vector<std::pair<int,int>> listed;
      for(int y=34;y<=37;++y)for(int x=30;x<=35;++x){
        const int dx=x-33,dy=y-37;
        if(dx*dx+dy*dy>=10||(x==33&&y==35)||(std::abs(x-33)<=1&&std::abs(y-35)<=1))continue;
        listed.push_back({x,y});
      }
      auto visit=[&](const std::vector<std::pair<int,int>> &pre){
        Staff::Member *km=st.member(k);
        if(!km)return;
        km->x=33.5f;km->y=37.5f;km->path.clear();km->pathAt=0;
        km->job=Staff::Job::Visit;km->visit=land;km->access=land;km->phase=1;km->inX=27.5f;km->inY=29.5f;
        km->outX=27.5f;km->outY=27.5f;km->invalid=pre;km->unreachable.clear();
        st.visitStep(*km,map,f);
      };
      st.takeNotices();
      visit(listed);
      Staff::Member *km=st.member(k);
      const auto told=st.takeNotices();
      const bool onList=km&&km->invalid.size()==listed.size()+1&&km->invalid.back()==std::make_pair(33,35);
      out<<"STAFF food spot unreachable: list "<<(km?km->invalid.size():0)<<" (had "<<listed.size()<<") phase "
         <<(km?km->phase:-1)<<" food "<<items.foodIn(land)<<" says "<<told.size()<<"\n";
      check("the food spot it has no way to: on its list, the food job over, no other tile tried (0x4993b9)",
            listed.size()==10&&onList&&km->phase!=9&&km->phase!=2&&items.foodIn(land)<=0&&told.empty());
      // (19 on the list already: 9 more far tiles - the 20th empties it)
      auto nineteen=listed;
      for(int x=26;nineteen.size()<19;++x)nineteen.push_back({x,28});
      visit(nineteen);
      km=st.member(k);
      const auto told2=st.takeNotices();
      const Animals::Member *zm=a.member(z);
      out<<"STAFF food spot 20th: list "<<(km?km->invalid.size():0)<<" animals "<<(km?km->unreachable.size():0)
         <<" says "<<(told2.empty()?"(nothing)":told2[0].text)<<"\n";
      check("the 20th food spot it has no way to: list emptied, the animal on its other list, "
            "\"...having trouble getting to\" it (11004, 0x4993b9)",
            km&&zm&&km->invalid.empty()&&km->unreachable.size()==1&&km->unreachable[0]==z&&told2.size()==1&&
            told2[0].kind==2&&told2[0].text.find(zm->name)!=std::string::npos);
      for(int id:items.ofKind(ZooItems::Kind::Food,land))items.remove(id);
      for(int id:rocks)for(size_t i=0;i<objects.objects().size();++i)
        if(objects.objects()[i].id==id){objects.remove(static_cast<int>(i));break;}
      if(k>=0)st.fire(k);
      world.reindex();
    }
    // Staff work by its behaviour sets (#174): each type's [BehaviorSet] read
    // from its .ai - a scientist's bClean (the vac twice, then idle) and
    // bHeal (heal, then backwards), a maintenance worker's bBagDino
    {
      auto sets=[&](const char *file,const char *set){
        std::string s;const int k=st.typeOfFile(file);
        if(k<0)return std::string("?");
        auto it=st.typeList[k].sets.find(set);
        if(it==st.typeList[k].sets.end())return std::string("-");
        for(const auto &step:it->second)s+=(s.empty()?"":",")+step.anim+(step.reverse?"<":"")+(step.sound.empty()?"":"/"+step.sound);
        return s;
      };
      const std::string sciClean=sets("staff/scient.ai","bclean"),sciHeal=sets("staff/scient.ai","bheal"),
        sciEgg=sets("staff/scient.ai","bhealegg"),keepHeal=sets("staff/keeper.ai","bheal"),
        keepClean=sets("staff/keeper.ai","bclean"),bagDino=sets("staff/maint.ai","bbagdino"),bag=sets("staff/maint.ai","bbag");
      out<<"STAFF sets: scientist bClean "<<sciClean<<" bHeal "<<sciHeal<<" bHealEgg "<<sciEgg<<"; keeper bHeal "<<keepHeal
         <<" bClean "<<keepClean<<"; maint bBag "<<bag<<" bBagDino "<<bagDino<<"\n";
      check("behaviour sets read from each staff .ai: a scientist's bClean is the vac twice then idle, its bHeal heal then "
            "backwards; a keeper's bClean the rake four times (#174)",
            sciClean=="clean/vac,clean/vac,idle"&&sciHeal=="heal/heal_animal,heal<"&&sciEgg=="healegg/heal_animal,healegg<"&&
            keepHeal=="heal/heal_animal"&&keepClean=="clean/rake,clean/rake,clean/rake,clean/rake"&&
            bag=="sweep/sweep,sweep/sweep"&&bagDino=="cleanup/vac,cleanup/vac,idle");
    }
    // The food's animation (Job2_PutDownFood's begin, zoo.exe 0x5084b3):
    // bFeedCarn only for carnchow, the keeper's food type +0x2a4 at 1 (the
    // index 0x5080ee's table 0x64e6ec makes the food from); dinocarn and chum
    // go down with bFeedHerb (#174)
    if(z>=0) {
      Animals::Member *zm=a.member(z);
      const int lion=a.typeOfFile("animals/lion.ai"),allo=a.typeOfFile("animals/allo.ai");
      std::string lionFood,alloFood;bool lionCarn=false,alloCarn=true;
      if(zm&&lion>=0&&allo>=0) {
        const int was=zm->type;
        zm->type=lion;lionFood=st.foodFor(land,f,lionCarn);
        zm->type=allo;alloFood=st.foodFor(land,f,alloCarn);
        zm->type=was;
      }
      out<<"STAFF feeding: lion "<<lionFood<<" carn "<<lionCarn<<", allosaurus "<<alloFood<<" carn "<<alloCarn<<"\n";
      check("bFeedCarn for carnchow only: a lion's keeper feeds with feedc, an allosaurus's dinocarn with feedh (0x5084b3)",
            lionFood=="carnchow"&&lionCarn&&alloFood=="dinocarn"&&!alloCarn);
    }
    // Feeding by species (zoo.exe keeperFoodDuty 0x49efa6 -> 0x49f01a, each
    // time Task 0x6e's duties come round, 0x42400a): a keeper in a mixed
    // paddock puts down each hungry species' own keeper food, one after the
    // other; and each animal goes only to its own (the finders 0x49fe15 /
    // 0x4a0025 / 0x4a01f9, and the hunger check's 0x4a8624) (#192, #197)
    {
      auto feedRun=[&](const char *staffFile,const char *one,const char *two,std::string &first,std::string &second,
                       std::string &third,bool &ownOnly,bool &noneOwn){
        first=second=third="?";ownOnly=false;noneOwn=false;
        const int ta=a.typeOfFile(one),tb=a.typeOfFile(two),stType=st.typeOfFile(staffFile);
        if(ta<0||tb<0||stType<0)return;
        for(int id:items.ofKind(ZooItems::Kind::Food,land))items.remove(id);
        for(int id:items.ofKind(ZooItems::Kind::Dung,land))items.remove(id);
        const auto keep=a.members();
        std::vector<int> others;for(const auto &x:keep)if(x.exhibit==land)others.push_back(x.id);
        for(int id:others)if(Animals::Member *x=a.member(id))x->exhibit=-99; // (out of the way)
        const int ia=a.adopt(ta,false,30.5f,32.5f,map,f,true),ib=a.adopt(tb,false,33.5f,33.5f,map,f,true);
        if(auto *e=f.exhibit(land))e->animals=a.countIn(land);
        for(int id:{ia,ib})if(Animals::Member *x=a.member(id))x->hunger=90;
        const int k=st.hire(stType,22.5f,30.5f,map,f);
        Staff::Member *km=st.member(k);
        auto step=[&](int phase){
          km=st.member(k);if(!km)return std::string("?");
          km->x=31.5f;km->y=34.5f;km->path.clear();km->pathAt=0;km->playsLeft=0;km->steps.clear();
          km->job=Staff::Job::Visit;km->visit=land;km->access=land;km->phase=phase;km->inX=27.5f;km->inY=29.5f;
          km->outX=27.5f;km->outY=27.5f;km->invalid.clear();km->unreachable.clear();
          st.visitStep(*km,map,f);
          km=st.member(k);
          return km&&km->phase==9?km->dropFood:std::string("(none)");
        };
        first=step(1);   // the first food it goes to put down
        second=step(2);  // that one down: the duty again, the other species'
        third=step(2);   // both down: nothing more to feed
        // each animal's own, only: its food finder and its hunger check
        ownOnly=true;
        for(int id:{ia,ib})if(Animals::Member *x=a.member(id)){
          const std::string want=a.keeperFoodOf(x->type);
          for(int n=0;n<64;++n){const ZooItems::Item *it=items.item(a.chooseFood(*x));ownOnly=ownOnly&&it&&it->food==want;}
          const ZooItems::Item *nf=items.item(a.nearestFood(*x));ownOnly=ownOnly&&nf&&nf->food==want;
        }
        // the other's food alone: none of its own to find
        for(int id:items.ofKind(ZooItems::Kind::Food,land))
          if(const ZooItems::Item *it=items.item(id))
            if(Animals::Member *x=a.member(ia))if(it->food==a.keeperFoodOf(x->type)){items.remove(id);break;}
        if(Animals::Member *x=a.member(ia))noneOwn=a.chooseFood(*x)<0&&a.nearestFood(*x)<0;
        for(int id:items.ofKind(ZooItems::Kind::Food,land))items.remove(id);
        if(k>=0)st.fire(k);
        a.remove(ia);a.remove(ib);
        for(int id:others)if(Animals::Member *x=a.member(id))x->exhibit=land;
        if(auto *e=f.exhibit(land))e->animals=a.countIn(land);
      };
      std::string f1,f2,f3;bool own=false,none=false;
      feedRun("staff/keeper.ai","animals/zebra.ai","animals/lion.ai",f1,f2,f3,own,none);
      out<<"STAFF mixed feeding: keeper "<<f1<<", then "<<f2<<", then "<<f3<<"; own food only "<<own<<", none of its own "<<none<<"\n";
      check("a zebra and a lion together: the keeper puts down herbchow, then carnchow, then nothing more (0x49efa6, 0x49f01a)",
            f1=="herbchow"&&f2=="carnchow"&&f3=="(none)");
      check("each goes only to its own keeper food; with only the other's down, it finds none (0x49fe15, 0x4a8624)",own&&none);
      feedRun("staff/scient.ai","animals/allo.ai","animals/lambeo.ai",f1,f2,f3,own,none);
      out<<"STAFF mixed feeding: scientist "<<f1<<", then "<<f2<<", then "<<f3<<"; own food only "<<own<<", none of its own "<<none<<"\n";
      check("an allosaurus and a lambeosaurus together: the scientist puts down dinocarn, then dinogras (0x49efa6)",
            f1=="dinocarn"&&f2=="dinogras"&&f3=="(none)");
      check("each dinosaur eats only its own kind (0x49fe15, 0x4a8624)",own&&none);
    }
    // Each keeper type cleans up only the dirt its cDirt lists (ZTKeeperType::
    // loadConfig 0x4c34ae +0x20c; ZTKeeper slot 0x324, 0x42404e, from
    // keeperPickDung 0x4a2ab8, the visit choice 0x49d202 and the count
    // 0x4a8940): a zookeeper poo, a scientist dinopoo, a marine specialist
    // neither (#190)
    {
      for(int id:items.ofKind(ZooItems::Kind::Dung,land))items.remove(id);
      const int poo=items.addDung(land,32.5f,33.5f,false,0),dino=items.addDung(land,34.5f,33.5f,true,0);
      auto rakes=[&](const char *file,int &picked){
        picked=-2;std::string got;
        const int t=st.typeOfFile(file);
        const int k=t>=0?st.hire(t,22.5f,30.5f,map,f):-1;
        Staff::Member *km=st.member(k);
        if(!km)return std::string("?");
        for(int id:st.dungFor(*km,land))got+=id==poo?"poo ":id==dino?"dinopoo ":"other ";
        km->x=31.5f;km->y=34.5f;km->path.clear();km->pathAt=0;km->playsLeft=0;km->steps.clear();
        km->job=Staff::Job::Visit;km->visit=land;km->access=land;km->phase=3;km->inX=27.5f;km->inY=29.5f;
        km->outX=27.5f;km->outY=27.5f;km->invalid.clear();km->unreachable.clear();
        st.visitStep(*km,map,f);
        if((km=st.member(k)))picked=km->phase==4?km->jobItem:-1;
        st.fire(k);
        return got.empty()?std::string("(none)"):got;
      };
      int kp=0,sp=0,tp=0;
      const std::string keeper=rakes("staff/keeper.ai",kp),sci=rakes("staff/scient.ai",sp),ms=rakes("staff/trainer.ai",tp);
      const auto &kt=st.typeList[st.typeOfFile("staff/keeper.ai")].dirt,&scit=st.typeList[st.typeOfFile("staff/scient.ai")].dirt;
      out<<"STAFF cDirt: keeper "<<keeper<<"(goes to "<<(kp==poo?"poo":kp==dino?"dinopoo":"none")<<"), scientist "<<sci
         <<"(goes to "<<(sp==poo?"poo":sp==dino?"dinopoo":"none")<<"), marine specialist "<<ms<<"; poo id "
         <<(items.item(poo)?items.dirtType(*items.item(poo)):0)<<" dinopoo id "<<(items.item(dino)?items.dirtType(*items.item(dino)):0)<<"\n";
      check("cDirt read: keeper.ai 6062 and 6093, scient.ai 6094 (0x4c34ae)",
            kt==std::vector<int>{6062,6093}&&scit==std::vector<int>{6094});
      check("a zookeeper rakes the poo and leaves the dinopoo; a scientist the other way round; a marine specialist neither (0x42404e)",
            keeper=="poo "&&kp==poo&&sci=="dinopoo "&&sp==dino&&ms=="(none)");
      items.remove(poo);items.remove(dino);
    }
    // Healing an egg (Job4_HealAnimal's begin, zoo.exe 0x47002e, by the
    // animal's +0x395): bHealEgg - the scientist's healegg, then backwards
    if(z>=0) {
      const int sciType=st.typeOfFile("staff/scient.ai");
      const int sci=sciType>=0?st.hire(sciType,22.5f,30.5f,map,f):-1;
      std::string egg,animal;size_t eggSteps=0;bool eggBack=false;
      if(st.member(sci)&&a.member(z)) {
        auto heal=[&](bool isEgg){
          Animals::Member *y=a.member(z);Staff::Member *x=st.member(sci);
          y->egg=isEgg;
          x->x=y->x-0.3f;x->y=y->y;x->job=Staff::Job::Visit;x->visit=land;x->access=land;x->phase=8;x->jobAnimal=z;
          x->path.clear();x->pathAt=0;x->playsLeft=0;
          st.visitStep(*x,map,f);
          y->egg=false;
          return st.member(sci)->anim;
        };
        animal=heal(false);
        egg=heal(true);
        Staff::Member *sm=st.member(sci);
        eggSteps=sm->steps.size();eggBack=eggSteps==2&&sm->steps[1].reverse;
        sm->playsLeft=0;sm->steps.clear();sm->job=Staff::Job::None;
      }
      out<<"STAFF heal: an animal "<<animal<<", an egg "<<egg<<" ("<<eggSteps<<" steps)\n";
      check("a scientist heals an egg with bHealEgg (healegg, then backwards), an animal with bHeal (0x47002e)",
            animal=="heal"&&egg=="healegg"&&eggBack);
      if(sci>=0)st.fire(sci);
    }
    // A trash can it can't get to (Job9_GoToTrashCan's whileGoing, zoo.exe
    // 0x4271a1): the can's tile on its list, then one of its four
    // neighbours picked at random - on the list too when there's no way
    // there either - and no job (#174)
    {
      std::vector<int> rocks;
      auto rock=[&](int x,int y){if(objects.add("scenery/other/lrock1.ai",x+0.5f,y+0.5f,0)&&objects.last())rocks.push_back(objects.last()->id);};
      const int cx=20,cy=41;
      const bool canAdded=objects.add("scenery/other/trshcan.ai",cx+0.5f,cy+0.5f,0);
      const int canId=canAdded&&objects.last()?objects.last()->id:-1;
      if(auto *can=objects.byId(canId))can->fill=20;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)rock(cx+dx,cy+dy);
      world.reindex();
      const int maintType=st.typeOfFile("staff/maint.ai");
      const int w=maintType>=0?st.hire(maintType,20.5f,34.5f,map,f):-1;
      Staff::Member *m=st.member(w);
      const PlacedObjects::Object *canObj=objects.byId(canId);
      out<<"STAFF walled-in can "<<canId<<" "<<(canObj&&canObj->trashCan)<<" fill "<<(canObj?canObj->fill:-1)
         <<" inside "<<f.insideZoo(cx,cy)<<" can tile blocked "<<world.getIndex().blocked(cx,cy)<<"\n";
      const bool took=m&&st.findWork(*m,map,f);
      m=st.member(w);
      bool neighbour=false;
      if(m&&m->invalid.size()==2)
        neighbour=std::abs(m->invalid[1].first-cx)+std::abs(m->invalid[1].second-cy)==1;
      out<<"STAFF trash can walled in: took "<<took<<" list "<<(m?m->invalid.size():0)<<"\n";
      check("a trash can it can't reach: the can's tile and one neighbour on its list, no job (0x4271a1)",
            canId>=0&&!took&&m&&m->invalid.size()==2&&m->invalid[0]==std::make_pair(cx,cy)&&neighbour);
      if(w>=0)st.fire(w);
      for(int id:rocks)for(size_t i=0;i<objects.objects().size();++i)
        if(objects.objects()[i].id==id){objects.remove(static_cast<int>(i));break;}
      for(size_t i=0;i<objects.objects().size();++i)if(objects.objects()[i].id==canId){objects.remove(static_cast<int>(i));break;}
      world.reindex();
    }
    // Dung by its animal (zoo.exe 0x4a8f89): dinopoo for cDinoZoodoo, its
    // size by cSmallZoodoo (1, the small art) and cGiantZoodoo (2, giant;
    // a young apatosaurus's [y/...] takes it back to 0) - setPooKind
    // 0x4f429d; its own art; a maintenance worker bags it with bBagDino
    // (0x50e211); written to the original's part as objects/other/dinopoo
    // with its size at +0x154 and kept by our own (#173)
    {
      auto type=[&](const char *file)->const Animals::Type*{const int k=a.typeOfFile(file);return k>=0?&a.types()[k]:nullptr;};
      const Animals::Type *allo=type("animals/allo.ai"),*ant=type("animals/anteater.ai"),*zeb=type("animals/zebra.ai"),
                          *apato=type("animals/apato.ai"),*raptor=type("animals/velocira.ai");
      out<<"STAFF dung kinds: allo "<<(allo?allo->dinoDung:0)<<"/"<<(allo?allo->dungSize:-1)<<" anteater "<<(ant?ant->dinoDung:0)
         <<"/"<<(ant?ant->dungSize:-1)<<" zebra "<<(zeb?zeb->dungSize:-1)<<" apato "<<(apato?apato->dungSize:-1)<<" young "
         <<(apato?apato->youngDungSize:-1)<<" raptor "<<(raptor?raptor->dinoDung:0)<<"/"<<(raptor?raptor->dungSize:-1)<<"\n";
      check("dung by kind: an allosaurus's giant dinopoo, a velociraptor's small dinopoo, an anteater's small poo, a zebra's "
            "poo; a young apatosaurus's not giant (0x4a8f89)",
            allo&&allo->dinoDung&&allo->dungSize==2&&raptor&&raptor->dinoDung&&raptor->dungSize==1&&ant&&!ant->dinoDung&&
            ant->dungSize==1&&zeb&&!zeb->dinoDung&&zeb->dungSize==0&&apato&&apato->dungSize==2&&apato->youngDungSize==0);
      const int poo=items.addDung(-1,20.5f,44.5f,false,0),small=items.addDung(-1,21.5f,44.5f,false,1),
                dino=items.addDung(-1,22.5f,44.5f,true,0),giant=items.addDung(-1,23.5f,44.5f,true,2);
      const Animation *aPoo=items.artOf(*items.item(poo)),*aSmall=items.artOf(*items.item(small)),
                      *aDino=items.artOf(*items.item(dino)),*aGiant=items.artOf(*items.item(giant));
      check("dinopoo and the small and giant piles have their own art (dinopoo.ai: mid, full; poo.ai: small)",
            aPoo&&aSmall&&aDino&&aGiant&&aPoo!=aSmall&&aPoo!=aDino&&aDino!=aGiant&&aSmall!=aGiant);
      // (bagged: a maintenance worker at the giant pile)
      const int maintType=st.typeOfFile("staff/maint.ai");
      const int w=maintType>=0?st.hire(maintType,23.5f,43.5f,map,f):-1;
      world.reindex();
      Staff::Member *m=st.member(w);
      std::string bagging;
      if(m) {
        m->job=Staff::Job::Dung;m->jobItem=giant;m->path={{m->x,m->y}};m->pathAt=0;m->anim="walk";m->playsLeft=0;m->idleLeft=0;
        m->workCheck=999;
        st.update(0.01f,map,f);m=st.member(w);
        bagging=m?m->anim+" x"+std::to_string(m->playsLeft):"";
      }
      out<<"STAFF bagging dinopoo: "<<bagging<<"\n";
      check("a maintenance worker bags dinopoo with bBagDino: cleanup twice with the vac, then idle (0x50e211)",
            bagging=="cleanup x3");
      if(w>=0)st.fire(w);
      // (saved: the original's part and ours)
      ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
      Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
      const auto file=std::filesystem::temp_directory_path()/("zt1-parity-dung-"+std::to_string(SDL_GetTicks())+".zoo");
      const bool saved=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn");
      int origDino=0,origGiant=0,origSmall=0;
      if(saved) {
        std::ifstream in(file,std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        AssetBuffer buffer;buffer.data=bytes.data();buffer.size=bytes.size();
        ZooReader decoded;
        if(decoded.load(buffer)&&decoded.hasGame())
          for(const auto &o:decoded.getObjects()) {
            origDino+=o.typeName=="dinopoo";
            origGiant+=o.typeName=="dinopoo"&&o.food==2;
            origSmall+=o.typeName=="poo"&&o.food==1;
          }
        buffer.data=nullptr;buffer.size=0;
      }
      const bool loaded=saved&&SaveGame::load(file.string(),world,books,hud);
      int keptGiant=0,keptSmall=0;
      for(const auto &it:items.items()) {
        keptGiant+=ZooItems::dinoDung(it)&&it.units==2;
        keptSmall+=it.kind==ZooItems::Kind::Dung&&!ZooItems::dinoDung(it)&&it.units==1;
      }
      SaveGame::goals=priorGoals;
      std::error_code ec;std::filesystem::remove(file,ec);
      out<<"STAFF dung saved: original dinopoo "<<origDino<<" giant "<<origGiant<<" small poo "<<origSmall
         <<"; ours giant "<<keptGiant<<" small "<<keptSmall<<"\n";
      check("dinopoo saved as objects/other/dinopoo with its size at +0x154 (2 dinopoo, one giant; the small poo 1), "
            "and kept by our own save (0x44f625)",
            saved&&origDino==2&&origGiant==1&&origSmall==1&&loaded&&keptGiant==1&&keptSmall==1);
      for(int id:items.ofKind(ZooItems::Kind::Dung,-1))items.remove(id);
      world.reindex();
    }
    // A keeper with no way to its exhibit's gate (Task65 0x423f57 ->
    // staffPathTrouble 0x4e85e8): "%s is having trouble getting to %s."
    // (11004); something in front of the gate, "The entrance to %s is
    // blocked..." (11001); the look ends
    if(z>=0) {
      int ox=-1,oy=-1;
      for(const auto &kv:f.pieces()) {
        if(!kv.second.gate||f.exhibitOf(kv.first)!=land)continue;
        const Fences::Edge &e=kv.first;
        const int ax=e.alongX?e.x:e.x-1,ay=e.alongX?e.y-1:e.y;
        const bool aIn=f.exhibitAt(ax,ay)==land;
        ox=aIn?e.x:ax;oy=aIn?e.y:ay;
        break;
      }
      std::vector<int> cans;
      auto can=[&](int x,int y){if(objects.add("scenery/other/lrock1.ai",x+0.5f,y+0.5f,0)&&objects.last())cans.push_back(objects.last()->id);};
      // A new exhibit's staff access check (zoo.exe exhibitAccessCheck
      // 0x44e4eb, #145): open ground to its gate, nothing said. (the route
      // starts at the zoo's entrance: here beside the keeper's start, the
      // lab's own entrance being walled off by other fixtures)
      int entX=-1,entY=-1;const bool hadEnt=world.getGuests().entrance(entX,entY);
      world.getGuests().setEntrance(22,30);
      st.takeNotices();
      const int open=st.exhibitAccess(land,map,f);
      st.takeNotices();
      // (the gate's outside tile walled in by rocks, a tile each: it's clear,
      // but no way to it)
      if(ox>=0)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        if((dx||dy)&&f.exhibitAt(ox+dx,oy+dy)<0)can(ox+dx,oy+dy);
      world.reindex();
      // ... walled in: no route from the zoo's entrance to it - "%s is
      // inaccessible to zoo staff." (11000), as the original says for a gate
      // onto grass ringed by rocks (measured 2026-11-16)
      const int walled=st.exhibitAccess(land,map,f);
      const auto walledSaid=st.takeNotices();
      st.takeNotices();
      const int keeperType=st.typeOfFile("staff/keeper.ai");
      const int k=keeperType>=0?st.hire(keeperType,22.5f,30.5f,map,f):-1;
      const int says=k>=0?st.assign(k,land,f):-1;
      world.reindex();
      Staff::Member *km=st.member(k);
      const bool went=km&&st.findWork(*km,map,f);
      km=st.member(k);
      const auto told=st.takeNotices();
      const std::string name=km?km->name:"?";
      const std::string want=rm?rm->getString(11004):"";
      out<<"STAFF keeper gate "<<ox<<","<<oy<<" rocks "<<cans.size()<<" blocked "<<world.getIndex().blocked(ox-1,oy)
         <<" went "<<went<<" job "<<(km?static_cast<int>(km->job):-1)<<" says "<<(told.empty()?"(nothing)":told[0].text)<<"\n";
      check("a keeper with no way to its exhibit's gate: \""+name+" is having trouble getting to Parity paddock.\" (11004), "
            "the look over (0x4e85e8)",
            ox>=0&&says==0&&!went&&km&&km->job==Staff::Job::None&&told.size()==1&&told[0].kind==2&&
            want.find("%s")!=std::string::npos&&told[0].text.find(name)!=std::string::npos&&
            told[0].text.find("Parity paddock")!=std::string::npos);
      can(ox,oy);
      world.reindex();
      km=st.member(k);
      const bool went2=km&&st.findWork(*km,map,f);
      const auto told2=st.takeNotices();
      // ... and something on the gate's outer tile: "The entrance to %s is
      // blocked..." (11001: gateCrossCheck 0x46676a's code 3)
      const int blockedCheck=st.exhibitAccess(land,map,f);
      const auto blockedSaid=st.takeNotices();
      if(hadEnt)world.getGuests().setEntrance(entX,entY);
      out<<"STAFF access check: open "<<open<<", walled in "<<walled<<" ("<<(walledSaid.empty()?"(nothing)":walledSaid[0].text)
         <<"), blocked "<<blockedCheck<<" ("<<(blockedSaid.empty()?"(nothing)":blockedSaid[0].text)<<")\n";
      check("a new exhibit's staff access check (0x44e4eb): open ground nothing, a gate onto ground walled in by rocks 11000, "
            "something on the gate's outer tile 11001",
            open==0&&walled==11000&&walledSaid.size()==1&&walledSaid[0].text.find("Parity paddock")!=std::string::npos&&
            walledSaid[0].kind==5&&blockedCheck==11001&&blockedSaid.size()==1);
      out<<"STAFF keeper blocked gate says "<<(told2.empty()?"(nothing)":told2[0].text)<<"\n";
      check("something in front of the gate: \"The entrance to Parity paddock is blocked...\" (11001, 0x4e8537)",
            !went2&&told2.size()==1&&told2[0].text.find("Parity paddock")!=std::string::npos&&
            told2[0].text.find(name)==std::string::npos);
      for(int id:cans)for(size_t i=0;i<objects.objects().size();++i)
        if(objects.objects()[i].id==id){objects.remove(static_cast<int>(i));break;}
      if(k>=0)st.fire(k);
      world.reindex();
    }
  }
  // Guests' needs and surroundings as zoo.exe runs them (ZTGuestType::
  // guestTypeLoad 0x4c4cb9, ZTGuest::setup 0x4f6372, needsTick 0x43ab00,
  // guestSouvenirTick 0x43af5b, guestSurroundingsTick 0x43abac,
  // blockGridRefresh 0x41ffdc)
  {
    a.clear();st.clear();g.clear();world.reindex();g.setEntrance(25,26);
    auto kind=[&](const char *key)->const Guests::Type*{for(const auto &t:g.typeList)if(t.key==key)return &t;return nullptr;};
    const auto *man=kind("man"),*woman=kind("woman"),*boy=kind("boy"),*girl=kind("girl");
    check("guest kinds read",man&&woman&&boy&&girl);
    if(man&&woman&&boy&&girl) {
      check("guests.ai needs read (cHungerCheck 30, cHungerIncrement 4, cThirstThreshold 50, cAngryEnergyChange -15)",
            man->hungerCheck==30&&man->hungerIncrement==4&&man->thirstThreshold==50&&man->angryEnergy==-15);
      check("a woman takes the man's needs (only his section has them)",woman->hungerCheck==30&&woman->angryFood==-30&&
            woman->souvenirHigh==10&&woman->environmentCheck==20);
      check("a boy's own cStinkThreshold (3), a girl's (2)",boy->stinkThreshold==3&&girl->stinkThreshold==2);
    }
    // Arrival: cInitialHappiness, every other guest one need in 41..49
    // (hunger or thirst: energy's and the restroom's ranges are 0..0), each
    // countdown within its period, never a special animal as favourite
    for(int i=0;i<400;++i)g.admit(0);
    bool happy=true,needs=true,countdowns=true,favourites=true;int started=0;
    for(const auto &guest:g.list) {
      const auto &t=g.typeList[guest.type];
      happy=happy&&guest.happiness==t.initialHappiness;
      needs=needs&&guest.tired==0&&guest.bathroom==0&&(guest.hunger==0||(guest.hunger>=41&&guest.hunger<=49))&&
            (guest.thirst==0||(guest.thirst>=41&&guest.thirst<=49))&&!(guest.hunger>0&&guest.thirst>0);
      started+=guest.hunger>0||guest.thirst>0;
      countdowns=countdowns&&guest.hungerCheck>=0&&guest.hungerCheck<t.hungerCheck&&guest.trashCheck>=0&&
                 guest.trashCheck<t.trashCheck&&guest.souvenirCheck>=0&&guest.souvenirCheck<t.souvenirCheck&&
                 guest.environmentCheck>=0&&guest.environmentCheck<t.environmentCheck;
      favourites=favourites&&guest.favourite>=0&&!g.specialAnimals.count(guest.favourite);
    }
    check("new guests start at cInitialHappiness",happy);
    check("one need in four randomised for every other guest, in 41..49",needs&&started>=60&&started<=140);
    out<<"GUESTS starting hungry or thirsty: "<<started<<" of "<<g.list.size()<<"\n";
    check("each check's countdown starts within its period",countdowns);
    const int unicorn=a.typeOfFile("animals/unicorn.ai");
    check("the unicorn is special (cIsSpecialAnimal) and never a favourite",unicorn>=0&&g.specialAnimals.count(unicorn)&&favourites);
    // The souvenir chance by raw happiness: 71..100 cBuySouvenirChanceHigh
    // (10), 31..70 cBuySouvenirChanceMed (5), under 31 none
    g.clear();g.admit(0);
    auto rate=[&](float happiness){
      int wants=0;
      for(int i=0;i<4000;++i) {
        auto &guest=g.list.front();
        guest.happiness=happiness;guest.dHappy=0;guest.souvenir=false;guest.building=-1;guest.leaving=false;
        guest.path.clear();guest.state=Guests::State::Walking;guest.hunger=guest.thirst=guest.tired=guest.bathroom=0;
        guest.souvenirCheck=0;guest.chaseCheck=guest.leaveCheck=guest.viewCheck=guest.trashCheck=guest.environmentCheck=99;
        guest.hungerCheck=guest.thirstCheck=guest.bathroomCheck=guest.energyCheck=99;guest.recheck=false;
        g.tick(guest);wants+=g.list.front().wantsSouvenir;
      }
      return wants/40.0;
    };
    const double at50=rate(50),at80=rate(80),at20=rate(20);
    out<<"GUESTS souvenir wanted: happiness 50 "<<at50<<"%, 80 "<<at80<<"%, 20 "<<at20<<"%\n";
    check("souvenir: happiness 50 is the middle band (5%), not the top",at50>=3.5&&at50<=6.5);
    check("souvenir: happiness 80 the top band (10%)",at80>=8&&at80<=12);
    check("souvenir: happiness 20 wants none",at20==0);
    // Just done with something (+0x33e): a hungry guest goes for food at
    // once, its hunger countdown held; otherwise it waits for the check
    const bool shopAdded=objects.add("scenery/building/bgrstnd.ai",24.5f,25.5f,4);
    if(shopAdded) {
      world.reindex();
      auto &guest=g.list.front();
      auto reset=[&](bool recheck){
        guest.x=25.5f;guest.y=27.5f;guest.path.clear();guest.building=-1;guest.need.clear();guest.leaving=false;
        guest.state=Guests::State::Walking;guest.happiness=50;guest.hunger=60;guest.thirst=guest.tired=guest.bathroom=0;
        guest.hungerCheck=25;guest.thirstCheck=guest.bathroomCheck=guest.energyCheck=25;guest.souvenirCheck=99;
        guest.chaseCheck=guest.leaveCheck=guest.viewCheck=guest.trashCheck=guest.environmentCheck=99;guest.recheck=recheck;
      };
      reset(false);g.tick(guest);
      check("hungry but not at its check: it waits (countdown 25 -> 24)",guest.building<0&&guest.hungerCheck==24);
      reset(true);g.tick(guest);
      check("hungry and just done with something: off to food now, countdown held",guest.building>=0&&
            guest.need=="food"&&guest.hungerCheck==25&&!guest.recheck);
      objects.remove(static_cast<int>(objects.objects().size())-1);world.reindex();
    }
    check("food stand for the recheck fixture",shopAdded);
    // Surroundings: two dung piles in its block (poo.ai cStink 1, -5 for
    // each kind) - 2 of stink, at cStinkThreshold: cSickChange (-40) and
    // "This place really smells bad!" once, until it's out of it
    {
      auto &items=world.getItems();
      items.add(ZooItems::Kind::Dung,-1,26.5f,27.5f);items.add(ZooItems::Kind::Dung,-1,27.5f,27.5f);
      auto &guest=g.list.front();
      guest.type=0;for(size_t k=0;k<g.typeList.size();++k)if(g.typeList[k].key=="man")guest.type=static_cast<int>(k);
      guest.x=26.5f;guest.y=28.5f;guest.smelled=false;
      g.refreshBlocks();
      const auto *block=g.blockAt(guest.x,guest.y);
      check("dung counted in its block: stink 2, esthetic -10 for a man",block&&block->stink==2&&
            block->esthetic.count(9503)&&block->esthetic.at(9503)==-10);
      auto look=[&](){guest.dHappy=0;guest.environmentCheck=0;guest.chaseCheck=guest.leaveCheck=guest.viewCheck=guest.trashCheck=99;
        guest.hungerCheck=guest.thirstCheck=guest.bathroomCheck=guest.energyCheck=guest.souvenirCheck=99;guest.recheck=false;
        g.tick(guest);return guest.dHappy;};
      const float first=look(),second=look();
      check("stink at cStinkThreshold: cSickChange once, then not again while there",first==-40&&second==0&&guest.smelled&&
            guest.lastThought==10241);
      // Smell-sick (chooseTask 0x5b0509, Task79_GuestSick): each time it
      // picks what to do next, cSickChance in 100 (man 20, boy and girl 40)
      // that it's sick on the spot - its puke, "Ohh, I don't feel so good"
      // - then its needs looked at; drawn with cSickPalName's skin meanwhile
      {
        auto kindOf=[&](const char *key){for(const auto &t:g.typeList)if(t.key==key)return &t;return (const Guests::Type*)nullptr;};
        check("cSickChance and cSickPalName read (man 20, boy 40, girl 40; guests/skpuke.pal)",kindOf("man")&&kindOf("boy")&&
              kindOf("girl")&&kindOf("man")->sickChance==20&&kindOf("boy")->sickChance==40&&kindOf("girl")->sickChance==40&&
              kindOf("man")->sickPal=="guests/skpuke.pal");
        int worn[4];g.wornColours(guest,worn);
        Animation *sickArt=g.art(guest,"stand");
        guest.smelled=false;Animation *wellArt=g.art(guest,"stand");guest.smelled=true;
        check("smell-sick, drawn in the sick skin",worn[3]!=guest.colours[3]&&sickArt&&wellArt&&sickArt!=wellArt);
        int sick=0;const int tries=2000;
        for(int i=0;i<tries;++i){guest.puking=false;guest.leaving=false;guest.building=-1;guest.state=Guests::State::Walking;
          g.decide(guest);sick+=guest.puking;}
        out<<"GUESTS smell-sick men sick: "<<sick*100.0/tries<<"%\n";
        check("a smell-sick man is sick about 1 time in 5 (cSickChance 20)",sick*100>=tries*16&&sick*100<=tries*24);
        guest.puking=false;for(int i=0;i<50&&!guest.puking;++i){guest.state=Guests::State::Walking;g.decide(guest);}
        check("being sick: its puke, \"Ohh, I don't feel so good...\"",guest.puking&&guest.anim=="puke"&&guest.lastThought==10285);
        guest.recheck=false;guest.smelled=false;g.decide(guest);
        check("done being sick: its needs looked at at once (+0x33e)",!guest.puking&&guest.recheck);
        guest.smelled=true;
      }
      items.removeIn(-1,ZooItems::Kind::Dung);g.refreshBlocks();look();
      check("out of the stink: free to be sickened again",!guest.smelled);
      // A crowd: 190 guests in a block, 190 x 10 / 25 = 76 >= 75
      g.clear();for(int i=0;i<190;++i)g.admit(0);
      for(auto &v:g.list){v.x=26.5f;v.y=28.5f;}
      g.refreshBlocks();
      auto &one=g.list.front();one.dHappy=0;one.environmentCheck=0;
      one.chaseCheck=one.leaveCheck=one.viewCheck=one.trashCheck=one.souvenirCheck=99;
      one.hungerCheck=one.thirstCheck=one.bathroomCheck=one.energyCheck=99;one.recheck=false;
      g.tick(one);
      check("190 guests in a block: \"This zoo is awfully crowded.\", cCrowdedViewingChange",
            one.dHappy==g.typeList[one.type].crowdedChange&&one.lastThought==10104);
    }
    // Joining a tour (guestJoinTour 0x58d79e, isBusy 0x58d9c0, 0x58d5eb):
    // watching an exhibit isn't busy - it joins; at a need's threshold
    // (>=, 50) it doesn't
    {
      g.clear();g.admit(0);auto &guest=g.list.front();
      guest.x=25.5f;guest.y=30.5f;guest.path.clear();guest.hunger=guest.thirst=guest.tired=guest.bathroom=0;
      guest.leaving=false;guest.state=Guests::State::Gawking;guest.target=land;guest.guide=-1;
      check("a guest watching an exhibit joins a tour (not busy, 0x58d9c0)",g.joinTour(guest.id,999,land,25,31)&&
            guest.guide==999&&guest.state==Guests::State::Walking);
      guest.guide=-1;guest.hunger=static_cast<float>(g.typeList[guest.type].hungerThreshold);guest.state=Guests::State::Walking;
      check("a guest at its hunger threshold (50, not over it) declines (0x58d5eb)",!g.joinTour(guest.id,999,land,25,31));
      // where followers go (0x58d809, 0x4171a1): 1 in 10 the guide's spot,
      // else one of the four tiles round it, out of the exhibit, with a route
      int beside=0,atGuide=0,other=0;const int joins=400;
      for(int i=0;i<joins;++i){guest.guide=-1;guest.hunger=0;guest.state=Guests::State::Walking;guest.x=24.5f;guest.y=26.5f;
        guest.path.clear();if(!g.joinTour(guest.id,999,land,25,31)||guest.path.empty())continue;
        const int ex=static_cast<int>(std::floor(guest.path.back().first)),ey=static_cast<int>(std::floor(guest.path.back().second));
        const int d=std::abs(ex-25)+std::abs(ey-31);atGuide+=d==0;beside+=d==1;other+=d>1;}
      out<<"GUESTS tour followers: beside the guide's spot "<<beside<<", on it "<<atGuide<<", elsewhere "<<other<<" of "<<joins<<"\n";
      check("tour followers go to the guide's spot or a tile beside it (0x58d809, 0x4171a1)",other==0&&beside>joins/2&&atGuide>=joins/20);
      g.clear();
    }
    // Viewing areas (World's ViewingAreas: zoo.exe habitat +0x34): where a
    // guest watches from (0x428741 -> 0x428917: a random seeing area, a
    // random viewing tile of it) and how crowded that area is (0x425f6a:
    // guests on its viewing tiles / its tiles x 10, cut)
    {
      g.clear();a.clear();world.reindex();g.setEntrance(25,26);
      const int zebra=a.typeOfFile("animals/zebra.ai");
      const int z=zebra>=0?a.adopt(zebra,false,30.5f,32.5f,map,f):-1;
      const auto areas=g.viewAreas?g.viewAreas(land):std::vector<Guests::ViewArea>();
      check("the paddock has viewing areas (World's ViewingAreas)",z>=0&&!areas.empty());
      if(z>=0&&!areas.empty()) {
        g.admit(0);auto &guest=g.list.front();
        int inArea=0;const int picks=200;
        for(int i=0;i<picks;++i){guest.visits.clear();guest.x=24.5f;guest.y=26.5f;int vx=-1,vy=-1;
          if(g.pickExhibit(guest,vx,vy)!=land)continue;
          bool found=false;for(const auto &ar:areas)if(!ar.seen.empty())for(auto t:ar.viewing)found=found||(t.first==vx&&t.second==vy);
          inArea+=found;}
        check("a guest watches from a viewing tile of one of the exhibit's seeing areas (0x428741, 0x428917)",inArea==picks);
        // crowd: k guests on one area's viewing tiles
        size_t best=0;for(size_t i=1;i<areas.size();++i)if(areas[i].viewing.size()>areas[best].viewing.size())best=i;
        const auto &ar=areas[best];const int tiles=static_cast<int>(ar.viewing.size());const int k=tiles+1;
        for(int i=1;i<k;++i)g.admit(0);
        for(size_t i=0;i<g.list.size();++i){auto &v=g.list[i];auto t=ar.viewing[i%ar.viewing.size()];v.x=t.first+0.5f;v.y=t.second+0.5f;
          v.state=Guests::State::Gawking;v.target=land;v.building=-1;v.hidden=false;}
        g.clock+=5;
        // (the front guest's area: the way of its tile's slot holding it -
        // 0x43b8a2)
        {auto &v=g.list.front();v.viewWay=-1;Guests::ViewArea in;
         for(int w=0;w<8&&v.viewWay<0;++w)if(g.areaInSlot&&g.areaInSlot(static_cast<int>(v.x),static_cast<int>(v.y),w,in)&&in.viewing==ar.viewing)v.viewWay=w;}
        const int crowd=g.areaCrowd(g.list.front());
        const int want=static_cast<int>(static_cast<float>(k)/static_cast<float>(tiles)*10.0f);
        out<<"GUESTS area crowd: "<<k<<" guests on "<<tiles<<" tiles -> "<<crowd<<"\n";
        check("an area's crowd is its guests over its viewing tiles x 10, cut (0x425f6a)",crowd==want);
      }
      g.clear();a.clear();world.reindex();
    }
    // A tank to watch (#226; zoo.exe 0x427b08 over habitatsWithAnimals
    // 0x440d9a: the habitat manager's list holds tanks too - a guest was
    // seen to put a plain tank's tile on its visited list by itself, with
    // no tour guide in the zoo, read live 2026-11-16 - less a tank with a
    // show, its +4; 0x4281fd counts every habitat with animals): a guest
    // picks a tank with animals to watch, not a show tank, and the tank
    // counts among the places to visit either way
    {
      g.clear();a.clear();world.reindex();
      const int wall=f.typeIndex("atltank"),dolphin=a.typeOfFile("animals/dolphin.ai");
      int bx=-1,by=-1;
      for(int y=3;y<map.getHeight()-9&&bx<0;++y)for(int x=3;x<map.getWidth()-9&&bx<0;++x){
        bool ok=true;
        for(int yy=y-2;yy<=y+6&&ok;++yy)for(int xx=x-2;xx<=x+6&&ok;++xx)
          ok=f.exhibitAt(xx,yy)<0&&f.insideZoo(xx,yy)&&!map.isPath(xx,yy);
        if(ok){bx=x;by=y;}
      }
      check("room for a tank guests watch, its walls and a dolphin",wall>=0&&dolphin>=0&&bx>=0);
      if(wall>=0&&dolphin>=0&&bx>=0) {
        const int tk=rectangle(bx,by,4,4,wall,"Guests' tank");
        Fences::Exhibit *te=f.exhibit(tk);
        std::vector<std::pair<int,int>> laid;
        for(int y=by-1;y<=by+4;++y)for(int x=bx-1;x<=bx+4;++x)
          if((x==bx-1||x==bx+4||y==by-1||y==by+4)&&!map.isPath(x,y)){map.setPath(x,y,"path");laid.push_back({x,y});}
        if(te)te->water=1;
        const int d=te?a.adopt(dolphin,false,bx+1.5f,by+1.5f,map,f,true):-1;
        world.reindex();
        const Animals::Member *dm=d>=0?a.member(d):nullptr;
        const auto areas=g.viewAreas&&te?g.viewAreas(tk):std::vector<Guests::ViewArea>();
        int seeing=0;for(const auto &ar:areas)seeing+=!ar.seen.empty()&&!ar.viewing.empty();
        out<<"TANKPICK tank "<<tk<<" (tank "<<(te&&te->tank)<<"), dolphin in it "<<(dm&&dm->exhibit==tk)<<", areas "<<areas.size()
           <<", seeing "<<seeing<<"\n";
        check("the tank holds its dolphin and has viewing areas that see it",te&&te->tank&&dm&&dm->exhibit==tk&&seeing>0);
        if(te&&te->tank&&dm&&dm->exhibit==tk&&seeing>0) {
          g.setEntrance(bx-1,by-1);g.admit(0);auto &guest=g.list.front();
          int picked=0;const int picks=50;
          for(int i=0;i<picks;++i){guest.visits.clear();guest.x=bx-0.5f;guest.y=by-0.5f;int vx=-1,vy=-1;picked+=g.pickExhibit(guest,vx,vy)==tk;}
          const int places=g.placesToVisit();
          check("a guest picks a tank with animals to watch on its own (0x427b08, 0x428917)",picked==picks);
          check("the tank counts among the places to visit (0x4281fd)",places==1);
          te->show=true;
          int showPicked=0;
          for(int i=0;i<picks;++i){guest.visits.clear();int vx=-1,vy=-1;showPicked+=g.pickExhibit(guest,vx,vy)==tk;}
          const int showPlaces=g.placesToVisit();
          te->show=false;
          out<<"TANKPICK picked "<<picked<<"/"<<picks<<", places "<<places<<"; as a show tank picked "<<showPicked<<", places "<<showPlaces<<"\n";
          check("a show tank isn't picked to watch (0x427b08: its show, +4), but still counts as a place (0x4281fd)",showPicked==0&&showPlaces==1);
        }
        for(auto [x,y]:laid)map.setPath(x,y,"");
      }
      g.clear();a.clear();world.reindex();
    }
    // A loose predator (zoo.exe 0x43abf9, animalFindPredator 0x4369b9,
    // chooseTask 0x50d079): seen within cPredatorRadius (7) - its tiles'
    // squared distance under 49 - a leaving guest stays, afraid, loses
    // cEscapedAnimalChange, thinks "RUN, %s has escaped!" and runs; 7 tiles
    // off it's not seen; once none is loose it calms down at its
    // cSawAnimalReset
    {
      g.clear();a.clear();world.reindex();
      const int lion=a.typeOfFile("animals/lion.ai");
      check("the lion would eat a guest (cPrey Man)",lion>=0&&a.types()[lion].eatsPeople);
      const int id=lion>=0?a.adopt(lion,false,30.5f,32.5f,map,f):-1;
      auto *cat=a.member(id);
      check("a lion adopted for the predator fixture",cat!=nullptr);
      if(cat) {
        cat->escaped=true;cat->x=31.5f;cat->y=26.5f;cat->path.clear();
        g.admit(0);auto &guest=g.list.front();
        auto at=[&](float x){guest.x=x;guest.y=26.5f;guest.path.clear();guest.state=Guests::State::Walking;guest.leaving=true;
          guest.dHappy=0;guest.sawEscape=false;guest.sawPredator=false;guest.predator=-1;guest.chaseCheck=0;
          guest.leaveCheck=guest.viewCheck=guest.trashCheck=guest.souvenirCheck=guest.environmentCheck=guest.sawResetCheck=99;
          guest.hungerCheck=guest.thirstCheck=guest.bathroomCheck=guest.energyCheck=99;guest.recheck=false;g.tick(guest);};
        at(24.5f); // 7 tiles: 49, not under it
        check("a predator 7 tiles off isn't seen (squared distance 49, not under cPredatorRadius squared)",
              guest.state!=Guests::State::Fleeing&&guest.leaving&&!guest.sawPredator);
        at(25.5f); // 6 tiles: 36
        check("a predator 6 tiles off: afraid, no longer leaving, cEscapedAnimalChange, \"RUN, %s has escaped!\", fleeing",
              guest.state==Guests::State::Fleeing&&!guest.leaving&&guest.sawEscape&&guest.sawPredator&&
              guest.predator==id&&guest.dHappy==g.typeList[guest.type].escapedChange&&guest.lastThought==10245);
        check("it runs away from it",!guest.path.empty()&&guest.path.back().first<guest.x+0.5f);
        cat->escaped=false;cat->x=30.5f;cat->y=32.5f;
        guest.state=Guests::State::Walking;guest.path.clear();guest.sawResetCheck=0;guest.chaseCheck=99;g.tick(guest);
        check("none loose: calm again at its cSawAnimalReset",!guest.sawEscape);
      }
      g.clear();a.clear();world.reindex();
    }
    // Staff run from a loose animal that would hunt them (zoo.exe ZTStaff::
    // tick 0x440602 every cChaseCheck status ticks, animalFindPredator
    // 0x4369b9 with the staff prey test 0x4391a2 / 0x42670b, the choosers
    // 0x423d04 / 0x439c33, task 0x75 0x4a557f to the ring 0x4a639e): an
    // allosaurus lists zookeepers (9500) and maintenance workers (9501) as
    // prey, a lion neither (#189)
    {
      g.clear();a.clear();st.clear();world.reindex();
      const int allo=a.typeOfFile("animals/allo.ai"),lion=a.typeOfFile("animals/lion.ai");
      const int keeperType=st.typeOfFile("staff/keeper.ai"),maintType=st.typeOfFile("staff/maint.ai");
      if(allo>=0&&lion>=0&&keeperType>=0&&maintType>=0) {
        const int id=a.adopt(allo,false,30.5f,32.5f,map,f,true);
        Animals::Member *dino=a.member(id);
        auto runs=[&](int type,float x,float y,bool loose,float &endD){
          endD=0;
          if(!(dino=a.member(id)))return std::string("?");
          dino->escaped=loose;
          if(loose){dino->x=31.5f;dino->y=26.5f;dino->exhibit=-1;}
          dino->path.clear();
          const int k=st.hire(type,25.5f,26.5f,map,f);
          Staff::Member *km=st.member(k);
          if(!km)return std::string("?");
          km->x=x;km->y=y;km->job=Staff::Job::None;km->idleLeft=5;km->workCheck=5;km->statusMs=0;km->chaseCount=0;
          km->path.clear();km->pathAt=0;
          st.update(1.05f,map,f);
          km=st.member(k);
          std::string got=km&&km->job==Staff::Job::Flee?"runs":"stays";
          if(km&&!km->path.empty()){const auto &e=km->path.back();endD=std::hypot(e.first-dino->x,e.second-dino->y);}
          if(km&&km->job==Staff::Job::Flee){
            // its run: the bRunFromPredator anim, at the set's rate
            for(int n=0;n<3;++n)st.update(0.1f,map,f);
            km=st.member(k);got+=km?" "+km->anim:"";
          }
          st.fire(k);
          return got;
        };
        float d6=0,d7=0,dm=0,din=0,dl=0;
        const std::string six=runs(keeperType,25.5f,26.5f,true,d6);   // 6 tiles off: 36, under 49
        const std::string seven=runs(keeperType,24.5f,26.5f,true,d7); // 7 tiles off: 49, not under it
        const std::string maint=runs(maintType,25.5f,26.5f,true,dm);
        // (in its exhibit, not loose: a keeper inside isn't hunted - 0x42670b)
        const std::string inside=runs(keeperType,32.5f,33.5f,false,din);
        if((dino=a.member(id)))dino->type=lion;
        const std::string lionRun=runs(keeperType,25.5f,26.5f,true,dl);
        if((dino=a.member(id)))dino->type=allo;
        out<<"STAFF flee: keeper 6 off "<<six<<" (to "<<d6<<" tiles), 7 off "<<seven<<", maintenance "<<maint<<" (to "<<dm
           <<"), in its exhibit "<<inside<<", from a lion "<<lionRun<<"; cChaseCheck "<<st.typeList[keeperType].chaseCheck
           <<", maint "<<st.typeList[maintType].fleeAnim<<" at "<<st.typeList[maintType].fleeRate<<"\n";
        check("cChaseCheck 2; maint.ai's bRunFromPredator fMove(0,0,run,55) (0x4c3783)",
              st.typeList[keeperType].chaseCheck==2&&st.typeList[maintType].fleeAnim=="run"&&st.typeList[maintType].fleeRate==55);
        check("a zookeeper 6 tiles from a loose allosaurus runs from it, farther off (0x440602, 0x4a557f)",
              six=="runs run"&&d6>6.0f);
        check("7 tiles off it doesn't see it (squared distance 49, not under cPredatorRadius squared - 0x4369b9)",seven=="stays");
        check("a maintenance worker runs from it too (cPrey 9501)",maint=="runs run"&&dm>6.0f);
        check("not in an exhibit, and not from a lion (no staff in its cPrey - 0x4391a2, 0x42670b)",
              inside=="stays"&&lionRun=="stays");
      }
      a.clear();st.clear();world.reindex();
    }
    // #213: a tour guide runs too (ZTTourGuide::tick 0x58ceb9 always goes on
    // to the staff tick; its chooser 0x58ce7e first lets its group go,
    // 0x58b362) - the dinosaurs' cPrey lists tour guides (9502). Running,
    // the keeper's status line is empty (job 0 after taskStateSet 0x413304;
    // 0x471399) and the guide's 10402 (0x61b756). A keeper's exhibit down to
    // 1 tile comes off its list (0x423d95: 0x423e01 gives 1 for a habitat
    // with under 2 tiles), one of 2 tiles stays
    {
      g.clear();a.clear();st.clear();world.reindex();
      const int allo=a.typeOfFile("animals/allo.ai");
      const int keeperType=st.typeOfFile("staff/keeper.ai"),guideType=st.typeOfFile("staff/tour.ai");
      if(allo>=0&&keeperType>=0&&guideType>=0) {
        const int id=a.adopt(allo,false,30.5f,32.5f,map,f,true);
        const int home=a.member(id)?a.member(id)->exhibit:-1;
        if(Animals::Member *dino=a.member(id)){dino->escaped=true;dino->x=31.5f;dino->y=26.5f;dino->exhibit=-1;dino->path.clear();}
        // the guide, on a tour with one follower, 6 tiles from it
        const int gd=st.hire(guideType,25.5f,26.5f,map,f);
        g.admit(0);
        const int follower=g.list.empty()?-1:g.list.front().id;
        std::string guideGot="?";int guideDuty=-1;bool released=false;
        if(Staff::Member *gm=st.member(gd);gm&&follower>=0) {
          g.list.front().guide=gd;
          gm->job=Staff::Job::Tour;gm->visit=home;gm->followers={follower};gm->speaking=true;gm->speakLeft=30;
          gm->x=25.5f;gm->y=26.5f;gm->idleLeft=5;gm->workCheck=5;gm->statusMs=0;gm->chaseCount=0;gm->path.clear();gm->pathAt=0;
          st.update(1.05f,map,f);
          gm=st.member(gd);
          guideGot=gm&&gm->job==Staff::Job::Flee?"runs":"stays";
          guideDuty=gm?gm->dutyId:-1;
          released=gm&&gm->followers.empty()&&!gm->speaking&&!g.list.empty()&&g.list.front().guide==-1;
        }
        st.fire(gd);g.clear();
        // a keeper, its status line while running
        std::string keeperGot="?",keeperText="?";
        const int k=st.hire(keeperType,25.5f,26.5f,map,f);
        if(Staff::Member *km=st.member(k)) {
          km->dutyId=10301; // (it was healing an animal)
          km->x=25.5f;km->y=26.5f;km->job=Staff::Job::None;km->idleLeft=5;km->workCheck=5;km->statusMs=0;km->chaseCount=0;
          km->path.clear();km->pathAt=0;
          st.update(1.05f,map,f);
          km=st.member(k);
          keeperGot=km&&km->job==Staff::Job::Flee?"runs":"stays";
          keeperText=km?st.dutyText(*km):"?";
        }
        st.fire(k);
        out<<"STAFF flee #213: guide "<<guideGot<<" (status "<<guideDuty<<", group let go "<<released<<"), keeper "<<keeperGot
           <<" (status \""<<keeperText<<"\")\n";
        check("a tour guide 6 tiles from a loose allosaurus runs, its group let go (0x58ceb9, 0x58ce7e, 0x58b362)",
              guideGot=="runs"&&released);
        check("running, the guide's status line is 10402 (0x61b756), the keeper's empty (0x413304, 0x471399)",
              guideDuty==10402&&keeperGot=="runs"&&keeperText.empty());
        a.clear();world.reindex();
        // the exhibit drop: the allosaurus's paddock given to a keeper, its
        // tiles cut to 2 and then 1 (on our side only - its fences stay)
        bool keptAt2=false,droppedAt1=false;int tiles=0;
        if(Fences::Exhibit *ex=f.exhibit(home)) {
          const auto saved=ex->tiles;tiles=static_cast<int>(saved.size());
          const int kk=st.hire(keeperType,25.5f,26.5f,map,f);
          if(Staff::Member *km=st.member(kk)) {
            km->exhibits={home};
            ex->tiles={*saved.begin(),*std::next(saved.begin())};
            st.update(0.05f,map,f);
            km=st.member(kk);keptAt2=km&&km->exhibits.size()==1;
            ex->tiles={*saved.begin()};
            st.update(0.05f,map,f);
            km=st.member(kk);droppedAt1=km&&km->exhibits.empty();
          }
          ex->tiles=saved;
          st.fire(kk);
        }
        out<<"STAFF exhibit drop #213: paddock of "<<tiles<<" tiles, kept at 2 "<<keptAt2<<", dropped at 1 "<<droppedAt1<<"\n";
        check("a keeper's exhibit with under 2 tiles comes off its list, 2 stays (0x423d95, 0x423e01 -> 1)",
              tiles>=2&&keptAt2&&droppedAt1);
        // the helicopter's pick (0x423dae, 0x50cd14): the nearest loose
        // dinosaur by whole tiles |dx| + |dy| from its own tile - 5 tiles
        // straight off before one 3 and 3 off (6, though nearer in a line)
        const int heliType=st.typeOfFile("staff/helicopt.ai");
        int picked=-2,straight=-1,diagonal=-1;
        if(heliType>=0) {
          // (somewhere its base fits)
          int h=-1;
          for(float hy:{24.0f,20.0f,16.0f,12.0f,40.0f,44.0f})for(float hx:{30.5f,20.5f,40.5f,14.5f,46.5f})if(h<0)h=st.hire(heliType,hx,hy,map,f);
          if(Staff::Member *hm=st.member(h)) {
            hm->workCheck=1;st.update(0.05f,map,f);hm=st.member(h);
            const int hx=static_cast<int>(std::floor(hm->heliX)),hy=static_cast<int>(std::floor(hm->heliY));
            // (adopted into the paddock, then let out)
            straight=a.adopt(allo,false,30.5f,32.5f,map,f,true);
            diagonal=a.adopt(allo,false,31.5f,32.5f,map,f,true);
            for(int one:{straight,diagonal})
              if(Animals::Member *d=a.member(one)){d->escaped=true;d->exhibit=-1;d->x=std::floor(d->x)+0.5f;d->y=std::floor(d->y)+0.5f;
                d->path.clear();d->tranquilised=false;d->claimedBy=-1;}
            if(Animals::Member *d=a.member(straight)){d->x=hx+5.5f;d->y=hy+0.5f;}
            if(Animals::Member *d=a.member(diagonal)){d->x=hx+3.5f;d->y=hy+3.5f;}
            hm->workCheck=0;st.update(0.05f,map,f);
            hm=st.member(h);picked=hm?hm->jobAnimal:-2;
          }
          // Its flight (#217; ZTHelicopter::chooseTask 0x506807 and its
          // tasks): the picked dinosaur moved 12 tiles off, the other one
          // gone, then a tick at a time (67 ms) until it's back on its pad
          std::vector<int> order;std::map<int,std::set<int>> lines;
          float climbStep=0,chaseStep=0,homeStep=0,lowest=1e9f,highest=-1e9f;long long fireAt=-1;bool darted=false,landed=false;
          if(Staff::Member *hm=st.member(h);hm&&picked==straight) {
            a.remove(diagonal);
            if(Animals::Member *d=a.member(straight)){d->x=std::floor(hm->heliX)+12.5f;d->y=std::floor(hm->heliY)+0.5f;}
            const float dt=Original::updateMs/1000.0f;
            auto task=[](Staff::HeliStage s){using H=Staff::HeliStage;
              return s==H::Parked?0x6a:s==H::SpinUp||s==H::Ascend?0x69:s==H::Chase||s==H::Fire?0x6f:s==H::Return?0x67:0x68;};
            order.push_back(task(hm->heliStage));
            for(int n=0;n<3000&&!landed;n++) {
              const float x0=hm->heliX,y0=hm->heliY,z0=hm->heliZ;const auto s0=hm->heliStage;
              st.update(dt,map,f);hm=st.member(h);if(!hm)break;
              const auto s1=hm->heliStage;const float moved=std::hypot(hm->heliX-x0,hm->heliY-y0)*64;
              if(s0==s1&&s1==Staff::HeliStage::Ascend)climbStep=std::max(climbStep,(hm->heliZ-z0)*16);
              if(s0==s1&&s1==Staff::HeliStage::Chase)chaseStep=std::max(chaseStep,moved);
              if(s0==s1&&s1==Staff::HeliStage::Return) {
                homeStep=std::max(homeStep,moved);
                const float over=(hm->heliZ-st.groundAt(map,hm->heliX,hm->heliY))*16;lowest=std::min(lowest,over);highest=std::max(highest,over);
              }
              if(s0!=s1&&s1==Staff::HeliStage::Fire)if(const Animals::Member *d=a.member(straight)) {
                const long long tx=static_cast<int>(std::floor(d->x))-static_cast<int>(std::floor(hm->heliX)),ty=static_cast<int>(std::floor(d->y))-static_cast<int>(std::floor(hm->heliY));
                fireAt=tx*tx+ty*ty;
              }
              if(const Animals::Member *d=a.member(straight))darted|=d->tranquilised;
              lines[task(s1)].insert(hm->dutyId);
              if(task(s1)!=order.back())order.push_back(task(s1));
              landed=s0!=s1&&s1==Staff::HeliStage::Parked;
            }
          }
          const auto &ht=st.types()[heliType];
          const float per=Original::unitStepsPerTile*Original::updateMs/1000.0f;
          const float climbWant=Original::unitTilesPerSecond(ht.ascendRate)*per,chaseWant=Original::unitTilesPerSecond(ht.chaseRate)*per,
                      homeWant=Original::unitTilesPerSecond(ht.flyRate)*per;
          std::ostringstream seq;for(int k:order)seq<<std::hex<<"0x"<<k<<' ';
          out<<"STAFF helicopter flight: tasks "<<seq.str()<<"rates "<<ht.ascendRate<<'/'<<ht.chaseRate<<'/'<<ht.flyRate<<'/'<<ht.descendRate
             <<" heights "<<ht.minHeight<<'-'<<ht.maxHeight<<", climb "<<climbStep<<" (want "<<climbWant<<"), chase "<<chaseStep<<" ("<<chaseWant
             <<"), home "<<homeStep<<" ("<<homeWant<<") at "<<lowest<<'-'<<highest<<", fired at tiles^2 "<<fireAt<<", darted "<<darted<<", landed "<<landed<<"\n";
          check("the helicopter's rates and heights are helicopt.ai's (fFly bAscend/bChaseAnimal/bFly/bDescend 150/250/150/150, cMinHeight 128, cMaxHeight 256)",
                ht.ascendRate==150&&ht.chaseRate==250&&ht.flyRate==150&&ht.descendRate==150&&ht.minHeight==128&&ht.maxHeight==256);
          // (the pick above already ended its wait, 0x6a: it's taking off)
          check("the helicopter's tasks run 0x69, 0x6f, 0x67, 0x68 and back to 0x6a (0x48d3e1, 0x48c4d1, 0x506807, 0x5a47e7, 0x5a47f3)",
                order==std::vector<int>{0x69,0x6f,0x67,0x68,0x6a}&&landed);
          check("it climbs, chases and flies home at its sets' rates a tick (moveAccumulate 0x435c42, 0x499be7, 0x48c0c6)",
                std::abs(climbStep-climbWant)<0.05f&&std::abs(chaseStep-chaseWant)<0.05f&&std::abs(homeStep-homeWant)<0.05f);
          check("flying home it keeps cMinHeight to cMaxHeight over the ground (0x48c0c6)",lowest>=ht.minHeight-0.5f&&highest<=ht.maxHeight+0.5f);
          check("it fires on coming within cWeaponRange whole tiles and darts the dinosaur (0x50cf5b, 0x50dd6e)",
                fireAt>=0&&fireAt<=static_cast<long long>(ht.weaponRange*ht.weaponRange)&&
                fireAt>static_cast<long long>((ht.weaponRange-1)*(ht.weaponRange-1))&&darted);
          check("its status line by task: 0x6a 10316, 0x69 10318, 0x6f 10308, 0x67 10319, 0x68 10317 (0x48c9ef)",
                lines[0x6a]==std::set<int>{10316}&&lines[0x69]==std::set<int>{10318}&&lines[0x6f]==std::set<int>{10308}&&
                lines[0x67]==std::set<int>{10319}&&lines[0x68]==std::set<int>{10317});
          st.fire(h);
        }
        out<<"STAFF helicopter pick: 5 straight off "<<straight<<", 3+3 off "<<diagonal<<", picked "<<picked<<"\n";
        check("the helicopter goes for the nearest loose dinosaur by whole tiles |dx| + |dy| (0x423dae)",
              straight>=0&&diagonal>=0&&picked==straight);
      }
      g.clear();a.clear();st.clear();world.reindex();
    }
    // #221: the helicopter's rotor loop at helicopt.ai's cLoopSoundAtten
    // (ZTHelicopterType::loadConfig 0x4c42ff, +0x1f8); a keeper at a pile
    // raked by another goes on to the next without raking (Job6_CleanDirt's
    // atTarget 0x42f8cb: none there, the job ends), and rakes one that's
    // there (bClean). #216: the keeper's habitat pick (0x49d7c7) leaves out the
    // one it stands in, counts a keeper at its duties there as at work (+0xf0)
    // and headed (+0xf4) - under 2 together for the second look - and on
    // arrival more than one at work there aborts (0x49f644)
    {
      g.clear();a.clear();st.clear();world.reindex();
      const int heliType=st.typeOfFile("staff/helicopt.ai"),keeperType=st.typeOfFile("staff/keeper.ai");
      const int zebra=a.typeOfFile("animals/zebra.ai");
      int fileAtten=-1;
      if(rm)if(IniReader *ai=rm->getIniReader("staff/helicopt.ai")){fileAtten=ai->getInt("characteristics/integers","cloopsoundatten",-1);delete ai;}
      const int typeAtten=heliType>=0?st.typeList[heliType].rotorAtten:-2;
      out<<"STAFF #221 rotor: cLoopSoundAtten in helicopt.ai "<<fileAtten<<", the type's "<<typeAtten<<"\n";
      check("the helicopter's rotor loop at helicopt.ai's cLoopSoundAtten, 1200 (0x4c42ff)",fileAtten==1200&&typeAtten==fileAtten);
      if(keeperType>=0&&zebra>=0) {
        const int z=a.adopt(zebra,false,30.5f,32.5f,map,f,true);
        const int home=a.member(z)?a.member(z)->exhibit:-1;
        Fences::Exhibit *ex=f.exhibit(home);
        if(ex)ex->animals=a.countIn(home);
        // three tiles inside it the keeper can stand on: its own, pile A's, pile B's
        std::vector<std::pair<int,int>> in;
        if(ex)for(auto [x,y]:ex->tiles)if(in.size()<3&&st.walkable(x,y,map,f,home))in.push_back({x,y});
        auto &items=world.getItems();
        int hired=0;
        auto keeperAt=[&](float x,float y){
          // (each hired on its own spot: hire refuses one on top of another)
          const int k=st.hire(keeperType,22.5f+hired++,24.5f,map,f);
          Staff::Member *km=st.member(k);
          if(km){km->x=x;km->y=y;km->path.clear();km->pathAt=0;km->idleLeft=5;km->workCheck=5;km->statusMs=0;km->chaseCount=99;}
          return k;
        };
        auto visiting=[&](Staff::Member &km,int phase){km.job=Staff::Job::Visit;km.visit=home;km.access=home;km.phase=phase;};
        std::string raked="?",afterText="?";
        if(in.size()==3) {
          const int pa=items.addDung(home,in[1].first+0.5f,in[1].second+0.5f,false,0);
          const int pb=items.addDung(home,in[2].first+0.5f,in[2].second+0.5f,false,0);
          const int k=keeperAt(in[0].first+0.5f,in[0].second+0.5f);
          if(Staff::Member *km=st.member(k)) {
            visiting(*km,4);km->jobItem=pa;
            items.remove(pa); // (raked by another on the way)
            st.visitStep(*km,map,f);
            const bool movedOn=km->playsLeft==0&&km->anim!="clean"&&km->phase==4&&km->jobItem==pb&&!km->path.empty();
            // (there at B: it's raked)
            km->x=in[2].first+0.5f;km->y=in[2].second+0.5f;km->path.clear();km->pathAt=0;
            st.visitStep(*km,map,f);
            const bool rakes=km->playsLeft>0&&km->anim=="clean"&&km->phase==5;
            raked=std::string(movedOn?"moves on":"stays")+", "+(rakes?"rakes the next":"doesn't rake the next");
            // (raked, nothing left: out, its status line task 1's - measured)
            st.visitStep(*km,map,f);
            afterText=km->phase==6?st.dutyText(*km):"(phase "+std::to_string(km->phase)+")";
          }
          st.fire(k);
          items.remove(pb);
        }
        out<<"STAFF #221 raked pile: "<<raked<<"; then, all done: \""<<afterText<<"\"\n";
        check("a keeper at a pile already raked plays no bClean and goes on to the next, which it rakes (0x42f8cb)",
              raked=="moves on, rakes the next");
        check("its duties done, a keeper's status line is \"Monitoring zoo\" (task 1: 10304, 0x471399; measured), not \"Exiting\"",
              rm&&afterText==rm->getString(10304));
        // #216: dung in it, so it wants a visit; nobody's been (the second
        // look's "over 1000 ago")
        std::string standing="?",withWorker="?",withHeaded="?",abort="?",outside="?",entering="?";
        if(in.size()==3&&ex) {
          const int pile=items.addDung(home,in[1].first+0.5f,in[1].second+0.5f,false,0);
          st.lastVisit.erase(home);
          auto picks=[&](Staff::Member &km){
            km.job=Staff::Job::None;km.visit=-1;km.access=-1;km.phase=0;km.exhibits={home};
            const bool got=st.keeperWork(km,map,f);
            const bool it=got&&km.job==Staff::Job::Visit&&km.visit==home;
            km.job=Staff::Job::None;km.visit=-1;km.access=-1;km.path.clear();km.pathAt=0;
            return std::string(it?"picks it":"doesn't");
          };
          const int k1=keeperAt(in[0].first+0.5f,in[0].second+0.5f);
          if(Staff::Member *km=st.member(k1))standing=picks(*km);
          // (outside: by the zoo's entrance side, where the fixture hires)
          if(Staff::Member *km=st.member(k1)){km->x=25.5f;km->y=26.5f;outside=picks(*km);}
          // another keeper at its duties inside: at work and headed - 2
          const int k2=keeperAt(in[2].first+0.5f,in[2].second+0.5f);
          if(Staff::Member *o=st.member(k2))visiting(*o,3);
          if(Staff::Member *km=st.member(k1))withWorker=picks(*km);
          // the other only headed for it (outside, on its way): 1
          if(Staff::Member *o=st.member(k2)){o->x=25.5f;o->y=27.5f;o->phase=0;}
          if(Staff::Member *km=st.member(k1))withHeaded=picks(*km);
          // two at their duties inside: one arriving at its gate gives up
          if(Staff::Member *o=st.member(k2)){o->x=in[2].first+0.5f;o->y=in[2].second+0.5f;o->phase=3;}
          const int k3=keeperAt(in[0].first+0.5f,in[0].second+0.5f);
          if(Staff::Member *o=st.member(k3))visiting(*o,3);
          if(Staff::Member *km=st.member(k1)) {
            km->x=25.5f;km->y=26.5f;visiting(*km,0);km->exhibits={home};
            st.visitStep(*km,map,f);
            abort=km->job==Staff::Job::None&&km->access==-1?"gives up":"goes in";
          }
          // with nobody at work there it goes in, its status line still
          // "Going to %s" (0x471399: 10303 for task 0x65; never 10307)
          for(int k:{k2,k3})if(Staff::Member *o=st.member(k)){o->job=Staff::Job::None;o->visit=-1;o->access=-1;}
          if(Staff::Member *km=st.member(k1)) {
            km->x=25.5f;km->y=26.5f;visiting(*km,0);
            st.visitStep(*km,map,f);
            entering=km->job==Staff::Job::Visit&&km->phase==1?st.dutyText(*km):"(didn't go in)";
          }
          for(int k:{k1,k2,k3})st.fire(k);
          items.remove(pile);
        }
        out<<"STAFF #216 chooser: standing in it "<<standing<<", from outside "<<outside<<", one at work there "<<withWorker
           <<", one headed there "<<withHeaded<<", arriving with two at work "<<abort<<", going in: \""<<entering<<"\"\n";
        check("the keeper's pick leaves out the habitat it stands in (0x49d7c7: slot 0x24c)",standing=="doesn't"&&outside=="picks it");
        check("a keeper at its duties there counts as at work and headed: not picked; one only headed for it, picked (+0xf0 + +0xf4 < 2)",
              withWorker=="doesn't"&&withHeaded=="picks it");
        check("arriving where more than one keeper is at work: Aborted going to habitat (0x49f644, +0xf0 > 1)",abort=="gives up");
        {
          std::string goingText=rm?rm->getString(10303):std::string();
          if(const size_t at=goingText.find("%s");at!=std::string::npos&&ex)goingText.replace(at,2,ex->name);
          check("going in through the gate, its status line is still \"Going to %s\" (0x471399: task 0x65 gives 10303)",
                !goingText.empty()&&entering==goingText);
        }
        // #229 (staffpaths off, the original): its duties done, the keeper
        // is in task 1 where it stands and walks about (0x49dafe, 0x43d948):
        // to a tile on a ring round it, half widths 2..10 (zoo.ini [AI]
        // mRandomXStaff / mRandomYStaff 9 + 1 + BaseX / BaseY 1), in the
        // paddock or out through its gate; and one left standing in it with
        // no work walks about from there too (before, it couldn't move)
        if(in.size()==3&&ex) {
          const bool oldPaths=Features::staffPaths;
          Features::staffPaths=false;
          int runs=0,routed=0,onRing=0,inside=0,outsideEnds=0,taskOne=0;
          int minR=99,maxR=0;
          const int k=keeperAt(in[0].first+0.5f,in[0].second+0.5f);
          for(int i=0;i<60;i++) {
            Staff::Member *km=st.member(k);
            if(!km)break;
            km->x=in[0].first+0.5f;km->y=in[0].second+0.5f;km->path.clear();km->pathAt=0;
            runs++;
            visiting(*km,3);km->jobItem=-1;
            st.visitStep(*km,map,f);
            if(km->job==Staff::Job::None&&km->visit==-1&&km->phase!=6&&rm&&st.dutyText(*km)==rm->getString(10304))taskOne++;
            if(!km->path.empty()&&km->access==home) {
              routed++;
              const int ex0=in[0].first,ey0=in[0].second;
              const int gx=static_cast<int>(std::floor(km->path.back().first)),gy=static_cast<int>(std::floor(km->path.back().second));
              const int r=std::max(std::abs(gx-ex0),std::abs(gy-ey0));
              minR=std::min(minR,r);maxR=std::max(maxR,r);
              if(r>=2&&r<=10)onRing++;
              (f.exhibitAt(gx,gy)==home?inside:outsideEnds)++;
            }
          }
          // one standing in the paddock with nothing to do (task 1, its access cleared)
          std::string idle="?";
          if(Staff::Member *km=st.member(k)) {
            km->x=in[0].first+0.5f;km->y=in[0].second+0.5f;km->path.clear();km->pathAt=0;
            km->job=Staff::Job::None;km->visit=-1;km->access=-1;
            st.wander(*km,map,f);
            idle=!km->path.empty()&&km->access==home?"walks about":"stays";
          }
          st.fire(k);
          Features::staffPaths=oldPaths;
          out<<"STAFF #229 walk about: "<<runs<<" keepers done in the paddock, task 1 "<<taskOne<<", routed "<<routed
             <<", ends on the ring "<<onRing<<" (rings "<<minR<<".."<<maxR<<"), inside "<<inside<<", outside "<<outsideEnds
             <<"; one standing in it idle: "<<idle<<"\n";
          check("its duties done (staffpaths off), the keeper is in task 1 where it stands, \"Monitoring zoo\" (0x49dafe; measured)",
                runs>0&&taskOne==runs);
          check("it walks about to a tile on a ring 2..10 round it (0x43d948, zoo.ini [AI] 9 + 1 + 1), in the paddock or out by its gate",
                routed==runs&&onRing==routed&&inside>0&&outsideEnds>0);
          check("a keeper standing in a paddock with nothing to do walks about from there (0x43d948)",idle=="walks about");
        }
        // #243 (staffpaths off, the original; measured 2026-11-17): staff
        // with nothing to do look for work as each walk ends (the chooser
        // runs every frame, 0x43494e -> 0x423d04); a keeper in a zoo with
        // habitats then stands one play of its idle animation (the
        // zookeeper's stand, 33 frames at 125 ms: 4.14-4.22 s measured) and
        // walks about; one with no habitats at all (0x49d524 counts none),
        // or a maintenance worker, walks on at once; on a path, the walk
        // ends on a path
        if(in.size()==3&&ex) {
          const bool oldPaths=Features::staffPaths;
          Features::staffPaths=false;
          const int maintType=st.typeOfFile("staff/maint.ai");
          const int k=keeperAt(in[0].first+0.5f,in[0].second+0.5f);
          const int w=maintType>=0?st.hire(maintType,26.5f,24.5f,map,f):-1;
          auto still=[&](Staff::Member &m,float x,float y){
            m.x=x;m.y=y;m.path.clear();m.pathAt=0;m.job=Staff::Job::None;m.visit=-1;m.access=-1;m.phase=0;
            m.idleLeft=0;m.walksLeft=0;m.workCheck=999;m.chaseCount=99;m.exhibits.clear();
          };
          float stand=-1,standWant=-1;
          std::string after="?",maint="?",noHabitat="?",onArrival="?",tick="?";
          if(Staff::Member *km=st.member(k)) {
            standWant=st.animSeconds(*km,"idle");
            still(*km,25.5f,26.5f);
            st.idleStep(*km,map,f);
            stand=km->path.empty()&&km->walksLeft==1?km->idleLeft:-1;
            st.idleStep(*km,map,f); // (the stand over)
            after=!km->path.empty()&&km->walksLeft==0?"walks about":"stays";
            // no habitats at all: the exhibits left out for a moment
            still(*km,25.5f,26.5f);
            std::vector<int> hidden;
            for(const Fences::Exhibit &e:f.exhibits())if(!e.pocket)hidden.push_back(e.id);
            for(int id:hidden)f.exhibit(id)->pocket=true;
            st.idleStep(*km,map,f);
            for(int id:hidden)f.exhibit(id)->pocket=false;
            noHabitat=!km->path.empty()&&km->idleLeft<=0?"walks on at once":"stands";
            // work there as it arrives: dung in the paddock, nobody been
            const int pile=items.addDung(home,in[1].first+0.5f,in[1].second+0.5f,false,0);
            st.lastVisit.erase(home);
            still(*km,25.5f,26.5f);km->workCheck=0;
            st.idleStep(*km,map,f);
            onArrival=km->job==Staff::Job::Visit&&km->visit==home?"goes to it":"doesn't";
            items.remove(pile);
            // the walk's end in the tick: no stand of our own before the
            // look (before, 2 s every fifth walk and a look every 3 s)
            still(*km,25.5f,26.5f);
            km->path={{25.5f,26.5f}};km->pathAt=0;km->anim="walk";
            float t=0,started=-1;
            for(int i=0;i<200&&started<0;i++){st.update(0.05f,map,f);t+=0.05f;if(Staff::Member *m2=st.member(k);m2&&!m2->path.empty()&&m2->pathAt<m2->path.size()&&t>0.06f)started=t;}
            std::ostringstream o;o.precision(2);o<<std::fixed<<started;tick=o.str();
            if(started<0||std::abs(started-standWant)>0.3f)tick+=" (want ~"+std::to_string(standWant)+")";
            else tick="ok "+tick;
          }
          if(Staff::Member *wm=st.member(w)) {
            still(*wm,25.5f,26.5f);
            st.idleStep(*wm,map,f);
            maint=!wm->path.empty()&&wm->idleLeft<=0?"walks on at once":"stands";
          }
          // on a path: 40 walks about, each to a path tile; from grass, grass
          // (a straight path of 15 tiles laid on open grass for it, as the
          // measured zoos' paths, then lifted)
          int px=-1,py=-1;
          std::vector<std::pair<int,int>> laid;
          int openTiles=0;
          for(int y=2;y+6<map.getHeight()&&px<0;y++)for(int x=8;x+8<map.getWidth()&&px<0;x++) {
            bool open=true;
            for(int dy:{0,3})for(int dx=-7;dx<=7&&open;dx++)
              open=f.exhibitAt(x+dx,y+dy)<0&&!map.isPath(x+dx,y+dy)&&st.walkable(x+dx,y+dy,map,f,-1);
            if(open)px=x,py=y;
          }
          for(int y=0;y<map.getHeight();y++)for(int x=0;x<map.getWidth();x++)openTiles+=st.walkable(x,y,map,f,-1);
          if(px>=0)for(int dx=-7;dx<=7;dx++){map.setPath(px+dx,py,"path");laid.push_back({px+dx,py});}
          int walks=0,onPath=0,fromGrass=0,grassEnds=0;
          if(Staff::Member *km=st.member(k);km&&px>=0)for(int i=0;i<40;i++){
            still(*km,px+0.5f,py+0.5f);
            if(st.walkAbout(*km,map,f)&&!km->path.empty()){walks++;onPath+=map.isPath(static_cast<int>(std::floor(km->path.back().first)),static_cast<int>(std::floor(km->path.back().second)));}
            still(*km,px+0.5f,py+3.5f);
            if(st.walkAbout(*km,map,f)&&!km->path.empty()){fromGrass++;grassEnds+=!map.isPath(static_cast<int>(std::floor(km->path.back().first)),static_cast<int>(std::floor(km->path.back().second)));}
          }
          for(auto [x,y]:laid)map.setPath(x,y,"");
          st.fire(k);if(w>=0)st.fire(w);
          Features::staffPaths=oldPaths;
          out<<"STAFF #243 nothing to do: keeper stands "<<stand<<" s (its idle "<<standWant<<" s) then "<<after<<"; no habitats: "<<noHabitat
             <<"; maintenance worker: "<<maint<<"; work on arrival: "<<onArrival<<"; in the tick, the next walk after "<<tick
             <<" s; from a path ("<<px<<","<<py<<"; zoo ground tiles "<<openTiles<<") "<<onPath<<"/"<<walks<<" end on a path, from grass "<<grassEnds<<"/"<<fromGrass<<" on grass\n";
          check("a keeper with nothing to do in a zoo with habitats stands one play of its idle animation, then walks about "
                "(measured: 4.14-4.22 s, a zookeeper's stand 33 frames at 125 ms; a woman's 24, 3 s)",
                standWant>2.9f&&std::abs(stand-standWant)<0.01f&&after=="walks about");
          check("a keeper with no habitats at all walks on at once (0x49d539: task 1, \"Can't find a habitat\"; measured)",
                noHabitat=="walks on at once");
          check("a maintenance worker with nothing to do walks on at once (measured)",maintType>=0&&maint=="walks on at once");
          check("work is looked for as a walk ends, not on a timer (0x423d04 each frame; cWorkCheck unused)",onArrival=="goes to it");
          check("in the tick, a keeper's next walk about starts one idle play after the last ends (no 2 s stand of ours)",tick.rfind("ok",0)==0);
          check("a walk about from a path ends on a path; from grass, it can end on grass (measured, as 0x61b639)",
                walks>=30&&onPath==walks&&fromGrass>0&&grassEnds>0);
        }
      }
      g.clear();a.clear();st.clear();world.reindex();
    }
    // Marine Mania's water rides (#116; guests.ai bReturn<building><n>,
    // bInWater, bSuitIdle, bSuitDrown, bSurfDrown; ZTBuildingType::
    // loadConfig 0x4c0b4d; guestBuy 0x42d858; Task82 0x6335d4; Task83_InWater
    // 0x5a5d09/0x5a5d73; Task84_Drown 0x61a705; chooseTask 0x59cbb8, Task7f)
    {
      g.clear();a.clear();st.clear();world.reindex();
      auto typeIndex=[&](const char *key){for(size_t i=0;i<g.typeList.size();++i)if(g.typeList[i].key==key)return static_cast<int>(i);return -1;};
      const int manT=typeIndex("man"),womanT=typeIndex("woman"),boyT=typeIndex("boy");
      const auto &room=g.infoOf("scenery/building/chroom.ai"),&shack=g.infoOf("scenery/building/boatshak.ai"),
                 &dolphins=g.infoOf("scenery/building/dolshack.ai");
      check("the changing room: a return building, its swimmers idle (cReturnBuilding, cIdler; out on the water, back in from it)",
            room.returnBuilding&&room.idler&&!room.viewer&&room.nameId==8061&&room.slots.size()==2&&room.slots[0].exitOnWater&&
            !room.slots[0].isReturn&&room.slots[1].isReturn&&room.slots[1].entranceOnWater&&room.slots[0].xy==-124&&room.slots[1].xy==124);
      check("the boat shack: an exhibit viewer for men and women ([slot] id 9503, 9504)",shack.returnBuilding&&shack.viewer&&!shack.idler&&
            shack.slots.size()==2&&shack.slots[0].ids==std::vector<int>{9503,9504});
      check("the dolphin shack tracks its riders, 2 at most (cUserTracker, cStrictBuildingCapacity 2)",
            dolphins.returnBuilding&&dolphins.tracker&&dolphins.strictCap==2);
      // (and a mauling's roar: [BehaviorSet\bCaughtBy<animal><m|f>]'s
      // fPlayWithSound, read with the backslash - a "\b" escape read none)
      check("a mauled guest's roar is read for each predator (bCaughtBylionm: soft_growl)",
            g.caughtSound.count("lionm")&&g.caughtSound["lionm"]=="soft_growl");
      if(manT>=0&&womanT>=0&&boyT>=0) {
        g.admit(0);g.admit(0);auto &m=g.list[0],&w=g.list[1];m.type=manT;w.type=womanT;
        auto move=[&](const Guests::Guest &who,const char *set){auto v=g.behaviour(who,set);return v.empty()?Guests::Move{}:v[0];};
        const auto ms=move(m,"bReturnchroom0"),ws=move(w,"bReturnchroom0"),inWater=move(m,"bInWater"),idle=move(m,"bSuitIdle");
        check("swimming out of the changing room: a man's suitswim at 66, a woman's at 56, with surfswim",
              ms.anim=="suitswim"&&ms.speed==66&&ms.sound=="surfswim"&&ws.anim=="suitswim"&&ws.speed==56);
        check("the boats by the shack's program: padboat, kayak, swanboat at 76; the dolphin ride at 56",
              move(w,"bReturnboatshak0").anim=="padboat"&&move(w,"bReturnboatshak1").anim=="kayak"&&
              move(w,"bReturnboatshak2").anim=="swanboat"&&move(w,"bReturnboatshak1").speed==76&&
              move(w,"bReturndolshack0").anim=="dolride"&&move(w,"bReturndolshack0").speed==56);
        check("in the water: bInWater's surfswim at 56; idling: bSuitIdle's suitidle",
              inWater.anim=="surfswim"&&inWater.speed==56&&idle.anim=="suitidle"&&idle.seconds==30);
        const auto suit=g.behaviour(m,"bSuitDrown"),surf=g.behaviour(m,"bSurfDrown");
        check("a man's drowning: suitdrwn (mandrown) or surfdrwn (mandrown2), four plays",suit.size()==4&&surf.size()==4&&
              suit[0].anim=="suitdrwn"&&suit[0].sound=="mandrown"&&suit[1].sound.empty()&&surf[0].anim=="surfdrwn"&&
              surf[0].sound=="mandrown2");
        g.clear();
      }
      // The fixture: a pond of 6 x 3 (18 tiles, X - 7..X - 2, Y - 1..Y + 1)
      // west of a changing room facing 2 at (X + 0.5, Y) - its land door at
      // (X + 2, Y), its water door at (X - 2, Y) - with paths east of it and
      // at (X - 8, Y); in the zoo with no exhibit or path there. And a lake
      // inside the big exhibit (x 30..50, y 48..58: its land is the
      // exhibit's, not a guest's) - no land 5 tiles round (40, 53) or east
      auto clear=[&](int x0,int y0,int x1,int y1){
        for(int yy=y0;yy<=y1;++yy)for(int xx=x0;xx<=x1;++xx)
          if(!map.getTile(xx,yy)||!f.insideZoo(xx,yy)||f.exhibitAt(xx,yy)>=0||map.isPath(xx,yy))return false;
        return true;};
      int X=-1,Y=-1;
      for(int y=3;y+3<map.getHeight()&&X<0;++y)for(int x=10;x+6<map.getWidth()&&X<0;++x)
        if(clear(x-9,y-2,x+5,y+2)){X=x;Y=y;}
      check("a place for the water fixture (no exhibit or path)",X>=0);
      out<<"GUESTS water fixture at "<<X<<","<<Y<<"\n";
      std::vector<std::pair<int,int>> wet;std::vector<uint8_t> was;
      auto water=[&](int x,int y){if(auto *t=map.getTileMutable(x,y)){wet.push_back({x,y});was.push_back(t->terrainType);t->terrainType=9;}};
      std::vector<std::pair<int,int>> laid;
      auto path=[&](int x,int y){if(!map.isPath(x,y)){map.setPath(x,y,"path");laid.push_back({x,y});}};
      const size_t before=objects.objects().size();
      if(X>=0) {
        for(int y=Y-1;y<=Y+1;++y)for(int x=X-7;x<=X-2;++x)water(x,y);
        for(int y=48;y<=58;++y)for(int x=30;x<=50;++x)water(x,y);
        path(X+2,Y);path(X+3,Y);path(X+4,Y);path(X-8,Y);
      }
      const bool placed=X>=0&&objects.add("scenery/building/chroom.ai",X+0.5f,float(Y),2);
      world.reindex();
      check("pond tiles counted from the water door: 18, so 2 legs (0x596fb4: (18 + 7) / 10)",X>=0&&g.waterSize(X-2,Y)==18);
      if(placed&&manT>=0&&womanT>=0) {
        const int roomAt=static_cast<int>(objects.objects().size())-1;const int roomId=objects.objects()[roomAt].id;
        g.admit(0);auto &guest=g.list.front();guest.type=manT;guest.x=X+2.5f;guest.y=Y+0.5f;guest.hunger=guest.thirst=guest.bathroom=guest.tired=0;
        guest.state=Guests::State::Walking;guest.leaving=false;guest.building=roomAt;guest.need="fun";
        const int id=guest.id;
        g.useBuilding(guest);
        bool out1=false,idledFirst=false,swam=false,back=false,home=false;int legs=-9,picks=0,idles=0;float idleFor=0;
        std::string lastAnim,seq;int lastTask=0;
        for(int k=0;k<6000&&!home;++k) {
          g.update(0.05f,0,1000);
          const Guests::Guest *v=g.guest(id);if(!v)break;
          if(!out1&&v->state==Guests::State::Water&&v->waterTask==Guests::kRideTask) {
            out1=v->ride==8061&&v->rideSet=="breturnchroom0"&&v->rideFrom==roomId&&static_cast<int>(std::floor(v->x))==X-2&&
                 static_cast<int>(std::floor(v->y))==Y&&v->idler;
            legs=v->legs;
          }
          if(v->state==Guests::State::Water&&v->waterTask==Guests::kRideTask&&v->anim!="stand"&&(v->anim!=lastAnim||v->waterTask!=lastTask)) {
            if(v->anim=="suitidle"){++idles;idleFor=v->playFor;if(picks==0)idledFirst=true;}
            if(v->anim=="suitswim")swam=true;
            ++picks;seq+=" "+v->anim;
          }
          lastAnim=v->anim;lastTask=v->waterTask;
          if(v->state==Guests::State::Water&&v->waterTask==Guests::kRideBackTask)back=true;
          if(back&&v->state!=Guests::State::Using&&v->state!=Guests::State::Water)
            home=v->ride==0&&!v->inWater&&std::find(v->thoughts.begin(),v->thoughts.end(),rm?rm->getString(10542):std::string())!=v->thoughts.end()&&static_cast<int>(std::floor(v->x))>=X+2;
        }
        out<<"GUESTS changing room: legs "<<legs<<", leg changes "<<picks<<", idles "<<idles<<" (last "<<idleFor<<" s):"<<seq<<"\n";
        check("through the changing room onto the water at its exitonwater exitpos, on its ride (guestBuy 0x42d858)",out1&&legs==2);
        check("a swimmer idles first, 6 to 12 s (bSuitIdle), then swims (suitswim)",idledFirst&&idleFor>=6&&idleFor<=12&&swam);
        check("its legs over, back to the return slot on the water (0x5a05c8, 0x5a05e7)",back);
        check("in at the return slot, out on land: off its ride, its cUsedThought (10542)",home);
        // A boat: the shack's program 1 is the kayak; the ride's set by it
        g.clear();objects.remove(roomAt);
        const bool boat=objects.add("scenery/building/boatshak.ai",float(X),Y+0.5f,2);world.reindex();
        if(boat) {
          const int bi=static_cast<int>(objects.objects().size())-1;objects.all()[bi].program=1;
          g.admit(0);auto &rower=g.list.front();rower.type=womanT;rower.x=X+2.5f;rower.y=Y+0.5f;
          rower.hunger=rower.thirst=rower.bathroom=rower.tired=0;rower.state=Guests::State::Walking;rower.building=bi;rower.need="fun";
          const int rid=rower.id;g.useBuilding(rower);
          bool kayak=false;
          for(int k=0;k<600&&!kayak;++k){g.update(0.05f,0,1000);const auto *v=g.guest(rid);if(!v)break;
            kayak=v->state==Guests::State::Water&&v->rideSet=="breturnboatshak1"&&v->viewer&&!v->path.empty()&&v->anim=="kayak"&&
                  std::abs(v->speed-Original::unitTilesPerSecond(76))<0.001f;} // (0x435c42: its fMove rate by the step rule)
          check("the boat shack's program 1: out in a kayak at rate 76 (bReturnboatshak1, 0x435c42)",kayak);
          // A boy isn't taken (the slot's ids: men and women)
          bool otherFun=false;
          for(size_t i=0;i<objects.objects().size();++i)if(static_cast<int>(i)!=bi&&!objects.objects()[i].fence&&
            g.infoOf(PlacedObjects::fileOf(objects.objects()[i])).satisfies.count("fun"))otherFun=true;
          g.admit(0);auto &kid=g.list.back();kid.type=boyT;kid.x=X+3.5f;kid.y=Y+0.5f;kid.state=Guests::State::Walking;
          if(!otherFun)check("a boy isn't taken by the boat shack (its slot's ids 9503, 9504)",!g.seekBuilding(kid,"fun",1000));
          // Sold while it's out: no way back - in the water, making for land
          auto *v=g.guest(rid);
          if(v&&v->state==Guests::State::Water) {
            objects.remove(bi);world.reindex();
            auto &r2=*const_cast<Guests::Guest*>(v);r2.legs=-1;r2.path.clear();r2.pathAt=0;r2.playFor=0;r2.animTime=0;
            bool task83=false,ashore=false;
            for(int k=0;k<2000&&!ashore;++k){g.update(0.05f,0,1000);const auto *u=g.guest(rid);if(!u)break;
              task83=task83||(u->state==Guests::State::Water&&u->waterTask==Guests::kInWaterTask&&u->inWater);
              ashore=task83&&u->state!=Guests::State::Water&&u->ride==0&&!u->inWater;}
            check("its shack sold while it's out: in the water (Task83), it makes for land and gets out",task83&&ashore);
          }
        }
        // Drowning: a man far out on the lake, no land in reach
        g.clear();
        auto drownTest=[&](int type,const std::string &set,const char *anim,bool &noticed,bool &gone,bool &ashore,bool &played) {
          g.admit(0);auto &d=g.list.front();d.type=type;d.x=40.5f;d.y=53.5f;d.hunger=d.thirst=d.bathroom=d.tired=0;
          d.ride=set.empty()?0:8061;d.rideSet=set;const int did=d.id;g.inWater(d);g.takeNotices();
          noticed=gone=ashore=played=false;
          for(int k=0;k<4000&&!gone&&!ashore;++k){g.update(0.05f,0,1000);
            for(const auto &n:g.takeNotices())noticed=noticed||(n.guest==did&&n.kind==2);
            const auto *u=g.guest(did);
            if(!u){gone=true;break;}
            played=played||(u->waterTask==Guests::kDrownTask&&u->anim==anim&&u->lastThought==10566);
            ashore=u->state!=Guests::State::Water&&u->ride==0&&!u->inWater;}
          g.clear();
        };
        bool noticed,gone,ashore,played;
        drownTest(manT,"",  "surfdrwn",noticed,gone,ashore,played);
        check("a man in the water with no land in reach drowns: \"%s is drowning!\", 10566, surfdrwn, \"%s has drowned!\", gone",
              noticed&&played&&gone&&!ashore);
        drownTest(manT,"breturnchroom0","suitdrwn",noticed,gone,ashore,played);
        check("a swimmer from the changing room drowns in his suit (bSuitDrown: suitdrwn)",played&&gone);
        drownTest(womanT,"breturnchroom0","suitdrwn",noticed,gone,ashore,played);
        check("a woman isn't drowned: put on the nearest land (Task84 0x61a93c, +0x320 set)",ashore&&!gone&&!played);
        // In the lake and not on a ride (#225, guestEnterWater 0x46b6a3): no
        // longer leaving (+0x339), task 0x83 - making for land (Task83) -
        // and "Hey! I don't want to be in the water!"; then out, on land
        g.admit(0);auto &dip=g.list.front();dip.type=womanT;dip.x=X-4.5f;dip.y=Y+0.5f;dip.state=Guests::State::Walking;dip.path.clear();
        dip.chaseCheck=dip.leaveCheck=dip.viewCheck=dip.trashCheck=dip.souvenirCheck=dip.environmentCheck=dip.sawResetCheck=99;
        dip.hungerCheck=dip.thirstCheck=dip.bathroomCheck=dip.energyCheck=99;dip.recheck=false;dip.leaving=true;
        const int dipId=dip.id;g.tick(dip);
        const bool hey=dip.lastThought==10568&&dip.state==Guests::State::Water&&dip.waterTask==Guests::kInWaterTask&&
                       dip.inWater&&!dip.leaving&&dip.tries>=2&&dip.tries<=4;
        out<<"WATER lake: thought "<<dip.lastThought<<", task 0x"<<std::hex<<dip.waterTask<<std::dec<<", leaving "<<dip.leaving<<"\n";
        bool outOf=false;
        for(int k=0;k<2000&&!outOf;++k){g.update(0.05f,0,1000);const auto *u=g.guest(dipId);if(!u)break;
          outOf=u->state!=Guests::State::Water&&!u->inWater&&u->x>-999;}
        out<<"WATER lake: out on land "<<outOf<<"\n";
        check("#225: in the lake off a ride: no longer leaving, task 0x83 (making for land) and \"Hey! I don't want to be in the water!\" (0x46b6a3)",hey);
        check("#225: then out of the lake, on land (Task83 0x5a5d73)",outOf);
        // An exhibit's water (ours as a tank's: chooseTask 0x59cbb8): 10568
        // and out by Task7f, its leaving kept
        g.clear();g.admit(0);
        {
          auto &tankDip=g.list.front();tankDip.type=womanT;tankDip.x=40.5f;tankDip.y=53.5f;tankDip.state=Guests::State::Walking;
          tankDip.path.clear();tankDip.recheck=false;tankDip.leaving=true;
          tankDip.chaseCheck=tankDip.leaveCheck=tankDip.viewCheck=tankDip.trashCheck=tankDip.souvenirCheck=99;
          tankDip.environmentCheck=tankDip.sawResetCheck=99;
          tankDip.hungerCheck=tankDip.thirstCheck=tankDip.bathroomCheck=tankDip.energyCheck=99;
          g.tick(tankDip);
          const bool inTank=f.exhibitAt(40,53)>=0;
          check("an exhibit's water: 10568 and out (Task7f, 0x59cbb8), still leaving",inTank&&tankDip.lastThought==10568&&
                tankDip.state==Guests::State::Water&&(tankDip.waterTask==Guests::kOutOfWaterTask||tankDip.waterTask==Guests::kInWaterTask)&&
                tankDip.leaving);
        }
        // The dolphin shack: two riders out, it's full for a third
        g.clear();
        const bool dol=objects.add("scenery/building/dolshack.ai",float(X),Y+0.5f,2);world.reindex();
        if(dol) {
          const int di=static_cast<int>(objects.objects().size())-1;
          bool otherFun=false;
          for(size_t i=0;i<objects.objects().size();++i)if(static_cast<int>(i)!=di&&!objects.objects()[i].fence&&
            g.infoOf(PlacedObjects::fileOf(objects.objects()[i])).satisfies.count("fun"))otherFun=true;
          for(int i=0;i<3;++i){g.admit(0);auto &r=g.list.back();r.type=womanT;r.x=X+3.5f;r.y=Y+0.5f;r.state=Guests::State::Walking;}
          g.list[0].ride=g.list[1].ride=8060;g.list[0].rideFrom=g.list[1].rideFrom=objects.objects()[di].id;
          g.list[0].state=g.list[1].state=Guests::State::Water;g.list[0].x=g.list[1].x=X-4.5f;g.list[0].y=g.list[1].y=Y+0.5f;
          if(!otherFun)check("the dolphin shack with 2 riders out is full for a third (0x59fe9e)",!g.seekBuilding(g.list[2],"fun",1000)&&g.seekFull);
          g.list[1].ride=0;g.list[1].state=Guests::State::Walking;
          if(!otherFun)check("one back, there's room again",g.seekBuilding(g.list[2],"fun",1000));
          // A rider goes back to its own shack, not another nearer one (0x42ac87)
          g.list[0].waterTask=Guests::kRideTask;g.list[0].legs=-1;g.list[0].rideSet="breturndolshack0";
          g.list[0].rideFrom=objects.objects()[di].id+100000;g.list[0].path.clear();
          g.rideBack(g.list[0]);
          check("a dolphin rider isn't taken back by a shack not its own (0x42ac87: its users list)",
                g.list[0].waterTask==Guests::kInWaterTask);
          g.list[0].rideFrom=objects.objects()[di].id;g.list[0].waterTask=Guests::kRideTask;g.list[0].state=Guests::State::Water;
          g.rideBack(g.list[0]);
          check("its own shack takes it back",g.list[0].waterTask==Guests::kRideBackTask&&g.list[0].building==di);
          if(otherFun)out<<"GUESTS another attraction stands in the zoo: the shacks' kind and capacity checks skipped\n";
          g.clear();objects.remove(di);
        }
      }
      while(objects.objects().size()>before)objects.remove(static_cast<int>(objects.objects().size())-1);
      for(size_t i=0;i<wet.size();++i)map.getTileMutable(wet[i].first,wet[i].second)->terrainType=was[i];
      for(auto [x,y]:laid)map.setPath(x,y,"");
      g.clear();world.reindex();
    }
  }
  // A building a rampage wrecks (zoo.exe ZTBuilding::toRubble 0x611bd6):
  // its own rubble, named 10512 "Rubble of %s" with its name - a Restroom
  // (cNameID 8008) bathrubl, any other building brubble1-4 - a piece on
  // every tile of it, none missed, each at its tile's middle (#210)
  {
    const size_t before=objects.objects().size();
    auto wreck=[&](const char *file,float x,float y,std::set<std::string> &kinds,int &pieces,bool &named,bool &centred) {
      kinds.clear();pieces=0;named=true;centred=true;
      if(!objects.add(file,x,y,0))return -1;
      const std::string name=objects.last()->label;
      std::string want=rm->getString(10512);
      if(size_t at=want.find("%s");at!=std::string::npos)want.replace(at,2,name);
      world.reindex();
      world.rubbleTile(static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)));
      for(size_t i=before;i<objects.objects().size();++i) {
        const auto &o=objects.objects()[i];
        kinds.insert(o.typeName);++pieces;
        named=named&&o.label==want&&!name.empty();
        centred=centred&&std::abs(o.x-std::floor(o.x)-0.5f)<0.01f&&std::abs(o.y-std::floor(o.y)-0.5f)<0.01f;
      }
      out<<"RUBBLE "<<file<<" \""<<name<<"\" -> "<<pieces<<" pieces, first named \""<<
        (pieces?objects.objects()[before].label:std::string())<<"\"\n";
      auto [fx,fy]=world.footprint(file);
      fx=std::max(1,fx);fy=std::max(1,fy);
      return ((fx+1)/2)*((fy+1)/2);
    };
    auto clearUp=[&]{while(objects.objects().size()>before)objects.remove(static_cast<int>(objects.objects().size())-1);world.reindex();};
    std::set<std::string> kinds;int pieces=0;bool named=false,centred=false;
    const int restroomTiles=wreck("scenery/building/bathroom.ai",10.0f,58.0f,kinds,pieces,named,centred);
    check("a wrecked Restroom leaves bathrubl named \"Rubble of\" it, a piece a tile (0x611bd6)",
      restroomTiles>0 && pieces==restroomTiles && kinds==std::set<std::string>{"bathrubl"} && named && centred);
    clearUp();
    bool every=true,mixed=true;
    for(int k=0;k<5;++k) {
      const int tiles=wreck("scenery/building/bgrstnd.ai",10.5f,58.5f,kinds,pieces,named,centred);
      every=every&&tiles>0&&pieces==tiles&&named&&centred;
      for(const auto &kind:kinds)mixed=mixed&&kind.rfind("brubble",0)==0;
      clearUp();
    }
    check("any other wrecked building leaves brubble1-4 named \"Rubble of\" it on every tile, none missed (0x611bd6)",every&&mixed);
    // The pieces share the list of their ids and keep the building's type
    // (+0x158, +0x15c: 0x612705, 0x612820); one taken away takes the rest
    // (0x48e50d), for half the building's cost (0x615a89) (#210)
    {
      wreck("scenery/building/bgrstnd.ai",10.5f,58.5f,kinds,pieces,named,centred);
      const CatalogItem *stand=ItemCatalog::get().find("scenery/building/bgrstnd.ai");
      bool grouped=pieces>1;
      std::vector<int> ids;for(size_t i=before;i<objects.objects().size();++i)ids.push_back(objects.objects()[i].id);
      for(size_t i=before;i<objects.objects().size();++i)
        grouped=grouped&&objects.objects()[i].rubbleGroup==ids&&stand&&objects.objects()[i].rubbleOf==stand->nameId;
      const float value=pieces?world.sellValue(objects.objects()[before]):-1;
      const bool half=stand&&std::abs(value-stand->cost*0.5f)<0.01f;
      // (saved: the original's rubble/other records, each with the list and
      // the type, ZTRubble::save 0x6159c0; ours kept)
      ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
      Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
      const auto file=std::filesystem::temp_directory_path()/("zt1-parity-rubble-"+std::to_string(SDL_GetTicks())+".zoo");
      const bool saved=SaveGame::save(file.string(),world,books,hud,"freeform/ff01.scn");
      // (this wreck's pieces only, by their places: an earlier rampage
      // fixture may have left rubble of its own)
      std::set<std::pair<int,int>> places;
      for(int id:ids)if(const auto *o=objects.byId(id))places.insert({int(std::lround(o->x*64)),int(std::lround(o->y*64))});
      int records=0,listed=0;
      if(saved) {
        std::ifstream in(file,std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        AssetBuffer buffer;buffer.data=bytes.data();buffer.size=bytes.size();
        ZooReader decoded;
        if(decoded.load(buffer)&&decoded.hasGame())
          for(const auto &o:decoded.getObjects())if(o.className=="rubble"&&o.subClass=="other"&&places.count({o.x,o.y})) {
            ++records;listed+=static_cast<int>(o.rubbleIds.size())==pieces&&stand&&o.rubbleOf==stand->nameId;
          }
        buffer.data=nullptr;buffer.size=0;
      }
      const bool loaded=saved&&SaveGame::load(file.string(),world,books,hud);
      bool kept=loaded;
      for(int id:ids){const auto *o=objects.byId(id);kept=kept&&o&&o->rubbleGroup==ids&&stand&&o->rubbleOf==stand->nameId;}
      SaveGame::goals=priorGoals;
      std::error_code ec;std::filesystem::remove(file,ec);
      const int at=objects.indexOf(ids.empty()?-1:ids.front());
      const float refund=at>=0?world.bulldozeObject(at):-1;
      int left=0;for(int id:ids)left+=objects.byId(id)!=nullptr;
      out<<"RUBBLE GROUP "<<pieces<<" pieces grouped "<<grouped<<", sells for "<<value<<" (stand $"<<(stand?stand->cost:-1)
         <<"), saved records "<<records<<" listed "<<listed<<", kept "<<kept<<", bulldozed one: refund "<<refund<<", left "<<left<<"\n";
      check("a wreck's rubble pieces share their ids and the building's type, saved as the original's rubble records (0x612820, 0x6159c0, #210)",
            grouped&&saved&&records==pieces&&listed==pieces&&kept);
      check("bulldozing one piece of a wreck's rubble takes the rest, for half the building's cost (0x48e50d, 0x615a89, #210)",
            half&&std::abs(refund-value)<0.01f&&left==0);
      clearUp();
    }
    // The guests inside a wrecked building are put out (0x5ae745); those
    // heading for it look again; the ones going elsewhere keep their place
    {
      g.clear();g.setEntrance(25,26);
      for(int k=0;k<3;++k)g.admit(0);
      if(objects.add("scenery/building/bathroom.ai",10.0f,58.0f,0)&&objects.add("scenery/building/bgrstnd.ai",14.5f,58.5f,0)&&g.list.size()==3) {
        const int loo=static_cast<int>(objects.objects().size())-2,standId=objects.objects().back().id;
        auto &in=g.list[0],&going=g.list[1],&other=g.list[2];
        in.state=Guests::State::Using;in.building=loo;in.hidden=true;in.need="bathroom";in.animTime=0;in.playFor=30;
        going.state=Guests::State::Walking;going.building=loo;going.need="bathroom";
        other.state=Guests::State::Walking;other.building=loo+1;other.need="food";
        world.reindex();
        world.rubbleTile(10,58);
        const bool out1=in.state!=Guests::State::Using&&in.building<0&&!in.hidden;
        const bool out2=going.building<0;
        const bool kept2=other.building>=0&&other.building<static_cast<int>(objects.objects().size())&&objects.objects()[static_cast<size_t>(other.building)].id==standId;
        out<<"WRECKED WITH GUESTS put out "<<out1<<", heading there looks again "<<out2<<", another's building kept "<<kept2<<"\n";
        check("a wrecked building's guests are put out, those heading there look again (0x5ae745, #210)",out1&&out2&&kept2);
      } else check("a wrecked building's guests are put out (fixture)",false);
      g.clear();clearUp();
    }
  }
  // The rename cheats (#235, zoo.exe 0x44940b): a guest named "Alfred H"
  // (0x607552): every guest attacked, so leaving; the crow and the bluejay on
  // the spawn list and 15 of each sent at once. The colour names (0x60739a,
  // 0x6072e4) and "Mr. White": every guest's shirt 0, its second colour 6
  // for a man or boy, 1 for a woman or girl; a colour-replaced building's
  // parts 0x17 and 4. Exact names only
  {
    Ambient &amb=world.getAmbient();
    g.clear();g.setEntrance(25,26);
    for(int k=0;k<4;++k)g.admit(k%4);
    const size_t firstObj=objects.objects().size();
    const bool painted=objects.add("scenery/building/aqgftshp.ai",14.0f,58.0f,0);
    if(g.list.size()==4) {
      const std::string none=RenameCheats::guestRenamed(world,0,"alfred h")+RenameCheats::guestRenamed(world,0,"Alfred H ");
      bool calm=true;for(auto &q:g.list)calm=calm&&!q.attacked;
      amb.clear();amb.setAllowed({5052});
      const std::string birds=RenameCheats::guestRenamed(world,1,"Alfred H");
      bool all=true;for(auto &q:g.list)all=all&&q.attacked;
      const auto &ids=amb.allowedIds();
      const int crows=static_cast<int>(std::count(ids.begin(),ids.end(),5052)),jays=static_cast<int>(std::count(ids.begin(),ids.end(),5053));
      const int flying=amb.count();
      amb.clear();amb.setAllowed({});
      for(auto &q:g.list)q.attacked=false;
      const std::string white=RenameCheats::guestRenamed(world,2,"Mr. White");
      bool clothes=true;
      for(auto &q:g.list)clothes=clothes&&q.colours[0]==0&&q.colours[1]==(g.typeList[q.type].female?1:6);
      bool building=painted&&objects.colouringOf(PlacedObjects::fileOf(objects.objects()[firstObj]));
      if(building){auto &o=objects.all()[firstObj];const size_t parts=objects.colouringOf(PlacedObjects::fileOf(o))->parts.size();building=o.colours.size()>=1&&o.colours[0]==0x17&&(parts<2||(o.colours.size()>=2&&o.colours[1]==4));}
      out<<"RENAME none '"<<none<<"' calm "<<calm<<" birds "<<birds<<" all attacked "<<all<<" crows "<<crows<<" jays "<<jays
         <<" flying "<<flying<<" white "<<white<<" clothes "<<clothes<<" building "<<building<<"\n";
      check("rename cheats: only the exact name (\"alfred h\", \"Alfred H \" nothing) (0x44940b)",none.empty()&&calm);
      check("\"Alfred H\": every guest attacked, crow and bluejay listed 15 times each, 30 sent (0x607552)",
            birds=="0x607552"&&all&&crows==16&&jays==15&&flying==30);
      check("\"Mr. White\": every guest's shirt 0, a man's or boy's second colour 6, a woman's or girl's 1; a colour-replaced building's parts 0x17/4 (0x60739a, 0x6072e4)",
            white=="0x60739a"&&clothes&&building);
    } else check("rename cheats fixture: four guests",false);
    for(size_t k=objects.objects().size();k>firstObj;--k)objects.remove(k-1);
    g.clear();world.reindex();
  }
  // Hide Buildings (zoo.exe ZTBuilding::draw 0x433aaf): only a building whose
  // [Member] lists "structures" - a restroom hidden, a burrow (shelters) not
  {
    const size_t firstObj=objects.objects().size();
    if(objects.add("scenery/building/bathroom.ai",10.0f,58.0f,0)&&objects.add("scenery/building/burrow1.ai",14.5f,58.5f,0)) {
      const bool was=objects.hideBuildings;objects.hideBuildings=true;
      const bool loo=objects.hiddenNow(objects.objects()[firstObj]),burrow=objects.hiddenNow(objects.objects()[firstObj+1]);
      objects.hideBuildings=false;
      const bool shown=!objects.hiddenNow(objects.objects()[firstObj]);
      objects.hideBuildings=was;
      out<<"HIDE BUILDINGS restroom "<<loo<<" burrow "<<burrow<<" off "<<shown<<"\n";
      check("Hide Buildings: a restroom (structures) hidden, a burrow (shelters) still drawn (0x433aaf)",loo&&!burrow&&shown);
    } else check("Hide Buildings fixture",false);
    for(size_t k=objects.objects().size();k>firstObj;--k)objects.remove(k-1);
  }
  a.clear();st.clear();g.clear();world.reindex();world.centreOn(31,32);
  for(size_t k=0;k<leaveDoneWas.size()&&k<g.typeList.size();++k)g.typeList[k].leaveDone=leaveDoneWas[k];
  out<<passed<<"/"<<total<<" parity checks passed\n";
  return out.str();
}

// ours: the zoo for the tour guides' side by side with the original (#129):
// on the map as loaded, a chain-link paddock of zebras near the entrance
// with a path round it and on to the entrance, two tour guides and guests
// about, saved as a shared .zoo the original loads too
std::string buildTourZoo(World &world, ZooSim &sim, UiGameScreen &hud, ResourceManager *rm,
                         const std::string &mapPath, const std::string &file) {
  (void)rm;
  auto &map=world.getMap(); auto &f=world.getFences(); auto &a=world.getAnimals();
  auto &st=world.getStaff(); auto &g=world.getGuests();
  int ex=-1,ey=-1;
  if(!g.entrance(ex,ey))world.update(SDL_GetKeyboardState(nullptr),0.01f); // (it finds the entrance)
  if(!g.entrance(ex,ey))return "no entrance";
  const int W=8,H=8;
  int best=-1,bx=0,by=0;
  for(int oy=ey-30;oy<=ey+30;++oy)for(int ox=ex-30;ox<=ex+30;++ox) {
    const int d=std::abs(ox+W/2-ex)+std::abs(oy+H/2-ey);
    if(d<6 || (best>=0 && d>=best))continue;
    bool ok=true;
    for(int y=oy-1;y<=oy+H&&ok;++y)for(int x=ox-1;x<=ox+W&&ok;++x)ok=world.canLayPath(x,y);
    if(ok){best=d;bx=ox;by=oy;}
  }
  if(best<0)return "no room near the entrance";
  const int type=f.typeIndex("chainlnk");
  int drag=900;
  for(int k=0;k<W;++k){f.place({true,bx+k,by},type,drag,map);f.place({true,bx+k,by+H},type,drag+1,map);}
  for(int k=0;k<H;++k){f.place({false,bx,by+k},type,drag+2,map);f.place({false,bx+W,by+k},type,drag+3,map);}
  for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,"Tour paddock",map);
  const int paddock=f.exhibitAt(bx+1,by+1);
  if(paddock<0) {
    int placed=0;
    for(const auto &kv:f.pieces())placed+=kv.first.x>=bx-1&&kv.first.x<=bx+W&&kv.first.y>=by-1&&kv.first.y<=by+H;
    return "the paddock didn't close ("+std::to_string(placed)+" pieces placed at "+std::to_string(bx)+","+std::to_string(by)+")";
  }
  std::set<std::pair<int,int>> ring;
  for(int y=by-1;y<=by+H;++y)for(int x=bx-1;x<=bx+W;++x)
    if(x==bx-1||x==bx+W||y==by-1||y==by+H){map.setPath(x,y,"path");ring.insert({x,y});}
  // (a path from the entrance to the ring, the shortest over ground that takes one)
  std::map<std::pair<int,int>,std::pair<int,int>> from;
  std::vector<std::pair<int,int>> q={{ex,ey}};from[{ex,ey}]={ex,ey};
  std::pair<int,int> hit{-1,-1};
  for(size_t i=0;i<q.size()&&hit.first<0;++i) {
    const auto c=q[i];
    if(ring.count(c)){hit=c;break;}
    const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(auto &dd:d) {
      std::pair<int,int> n={c.first+dd[0],c.second+dd[1]};
      if(from.count(n)||!(ring.count(n)||world.canLayPath(n.first,n.second)||n==std::make_pair(ex,ey)))continue;
      if(n.first>=bx&&n.first<bx+W&&n.second>=by&&n.second<by+H)continue;
      from[n]=c;q.push_back(n);
    }
  }
  if(hit.first<0)return "no way from the entrance";
  for(auto c=hit;c!=std::make_pair(ex,ey);c=from[c])map.setPath(c.first,c.second,"path");
  map.setPath(ex,ey,"path");
  world.reindex();
  const int zebra=a.typeOfFile("animals/zebra.ai");
  if(zebra<0)return "no zebra";
  for(int i=0;i<4;++i)a.adopt(zebra,i%2==0,bx+1.5f+2*i,by+3.5f,map,f,true);
  if(auto *e=f.exhibit(paddock))e->animals=a.countIn(paddock);
  // (ZT_TOUR_ZOO_KEEPER=1: a zookeeper and three dung piles instead of the
  // guides and guests - the keeper's chooser side by side, #216)
  if(std::getenv("ZT_TOUR_ZOO_KEEPER")) {
    const int keeper=st.typeOfFile("staff/keeper.ai");
    if(keeper<0)return "no keeper";
    st.hire(keeper,bx-0.5f,by+2.5f,map,f);
    for(int i=0;i<3;++i)world.getItems().addDung(paddock,bx+1.5f+2*i,by+6.5f,false,0);
    world.reindex();
    if(!SaveGame::save(file,world,sim,hud,mapPath))return "save failed: "+SaveGame::lastError();
    return "ok keeper paddock at "+std::to_string(bx)+","+std::to_string(by)+" ("+std::to_string(W)+"x"+std::to_string(H)+
           "), animals "+std::to_string(a.countIn(paddock))+", staff "+std::to_string(st.members().size());
  }
  const int guide=st.typeOfFile("staff/tour.ai");
  if(guide<0)return "no tour guide";
  st.hire(guide,bx-0.5f,by+2.5f,map,f);
  st.hire(guide,bx+W+0.5f,by+5.5f,map,f);
  for(int i=0;i<20;++i)g.admit(0);
  world.reindex();
  if(!SaveGame::save(file,world,sim,hud,mapPath))return "save failed: "+SaveGame::lastError();
  std::ostringstream r;
  r<<"ok paddock at "<<bx<<","<<by<<" ("<<W<<"x"<<H<<"), entrance "<<ex<<","<<ey<<", animals "<<a.countIn(paddock)
   <<", staff "<<st.members().size()<<", guests "<<g.guests().size()<<", viewing tiles "<<g.viewingTiles(paddock).size();
  return r.str();
}

// ours: ZT_ACCESS_ZOO=<file.zoo> - exhibits whose staff access differs, to
// load in the original and see which say 11000 / 11001 (#145): each gate on
// its top side; a path along the top to the entrance for some
std::string buildAccessZoo(World &world, ZooSim &sim, UiGameScreen &hud, const std::string &mapPath,
                           const std::string &file) {
  auto &map=world.getMap(); auto &f=world.getFences(); auto &g=world.getGuests();
  int ex=-1,ey=-1;
  if(!g.entrance(ex,ey))world.update(SDL_GetKeyboardState(nullptr),0.01f);
  if(!g.entrance(ex,ey))return "no entrance";
  const int AW=31,AH=17;
  int best=-1,bx=0,by=0;
  for(int oy=ey-40;oy<=ey+40;++oy)for(int ox=ex-40;ox<=ex+40;++ox) {
    const int d=std::abs(ox-ex)+std::abs(oy-1-ey);
    if(d<4 || (best>=0 && d>=best))continue;
    bool ok=true;
    for(int y=oy-2;y<oy+AH&&ok;++y)for(int x=ox-2;x<ox+AW&&ok;++x)ok=world.canLayPath(x,y);
    if(ok){best=d;bx=ox;by=oy;}
  }
  if(best<0)return "no room near the entrance";
  const int type=f.typeIndex("chainlnk");
  int drag=900;
  std::ostringstream r;
  auto pen=[&](int x,int y,int w,int h,const std::string &name)->int {
    for(int k=0;k<w;++k){f.place({true,x+k,y},type,drag,map);f.place({true,x+k,y+h},type,drag+1,map);}
    for(int k=0;k<h;++k){f.place({false,x,y+k},type,drag+2,map);f.place({false,x+w,y+k},type,drag+3,map);}
    drag+=4;
    for(int id:f.updateExhibits(map,1,0,1))f.finishExhibit(id,name,map);
    const int id=f.exhibitAt(x+w/2,y+h/2);
    if(auto *e=f.exhibit(id))e->name=name;
    const Fences::Edge gate{true,x+w/2,y};
    const bool can=f.canGate(gate);
    f.moveGate(gate);
    r<<name<<" #"<<id<<" at "<<x<<","<<y<<" "<<w<<"x"<<h<<" gate "<<gate.x<<","<<gate.y<<(can?"":" (gate refused)")<<"; ";
    return id;
  };
  // (the top row: the path the gates of A, C and F open on)
  for(int x=bx-1;x<=bx+29;++x)map.setPath(x,by-1,"path");
  pen(bx,by,5,5,"Gate path");             // A: its gate on the path
  pen(bx+8,by,5,5,"Gate blocked");        // C: a trash can on the path before its gate
  pen(bx+24,by,5,5,"Blocked inside");     // F: a trash can just inside its gate
  pen(bx,by+8,5,5,"No path");             // B: its gate onto grass
  pen(bx+16,by+6,9,9,"Outer pen");        // E: round D, its gate onto a path down from the row
  pen(bx+19,by+9,3,3,"Inner pen");        // D: its gate into E
  for(int y=by;y<=by+5;++y)map.setPath(bx+20,y,"path");
  // G: a 3x3 pen left open (its bottom middle piece missing, to be closed in
  // the original so its access check runs), the grass round it ringed by
  // rocks two tiles out: whatever gate it gets opens onto ground staff
  // can't reach (#145)
  {
    // (just inside the zoo wall's top corner, the tiles with the least
    // x + y: where the original's view opens on this save)
    int cx=-1,cy=-1;
    for(int d=0;d<2*map.getWidth()&&cx<0;++d)for(int x=0;x<=d&&cx<0;++x)if(f.insideZoo(x,d-x)){cx=x;cy=d-x;}
    const int gx=cx+5,gy=cy+5,gw=3,gh=3;
    for(int k=0;k<gw;++k){f.place({true,gx+k,gy},type,drag,map);if(k!=1)f.place({true,gx+k,gy+gh},type,drag+1,map);}
    for(int k=0;k<gh;++k){f.place({false,gx,gy+k},type,drag+2,map);f.place({false,gx+gw,gy+k},type,drag+3,map);}
    drag+=4;
    int rocks=0;
    for(int y=gy-2;y<=gy+gh+1;++y)for(int x=gx-2;x<=gx+gw+1;++x)
      if(x==gx-2||x==gx+gw+1||y==gy-2||y==gy+gh+1)
        rocks+=world.objectsMutable().add("scenery/other/lrock1.ai",x+0.5f,y+0.5f,0)?1:0;
    r<<"Walled (open) at "<<gx<<","<<gy<<" 3x3, the missing piece h "<<gx+1<<","<<gy+gh<<", rocks "<<rocks<<"; ";
  }
  // (a path from the entrance to the row, over ground that takes one)
  std::set<std::pair<int,int>> row;for(int x=bx-1;x<=bx+29;++x)row.insert({x,by-1});
  std::map<std::pair<int,int>,std::pair<int,int>> from;
  std::vector<std::pair<int,int>> q={{ex,ey}};from[{ex,ey}]={ex,ey};
  std::pair<int,int> hit{-1,-1};
  for(size_t i=0;i<q.size()&&hit.first<0;++i) {
    const auto c=q[i];
    if(row.count(c)){hit=c;break;}
    const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(auto &dd:d) {
      std::pair<int,int> n={c.first+dd[0],c.second+dd[1]};
      if(from.count(n)||!(row.count(n)||world.canLayPath(n.first,n.second)||n==std::make_pair(ex,ey)))continue;
      if(n.first>=bx-1&&n.first<bx+AW&&n.second>=by&&n.second<by+AH)continue;
      from[n]=c;q.push_back(n);
    }
  }
  if(hit.first<0)return "no way from the entrance";
  for(auto c=hit;c!=std::make_pair(ex,ey);c=from[c])map.setPath(c.first,c.second,"path");
  map.setPath(ex,ey,"path");
  world.reindex();
  const bool canC=world.objectsMutable().add("scenery/other/trshcan.ai",bx+10.5f,by-0.5f,0);
  const bool canF=world.objectsMutable().add("scenery/other/trshcan.ai",bx+26.5f,by+0.5f,0);
  r<<"trash cans "<<canC<<canF<<"; ";
  // (each pen's staff access check, as the original runs it after OK on
  // its name - zoo.exe 0x44e4eb)
  for(const auto &e:f.exhibits())if(e.named)r<<"access "<<e.name<<" "<<world.getStaff().exhibitAccess(e.id,map,f)<<"; ";
  world.getStaff().takeNotices();
  for(const auto &[e,pc]:f.pieces())if(pc.gate)r<<"gate "<<(e.alongX?"h ":"v ")<<e.x<<","<<e.y<<" of #"<<f.exhibitOf(e)<<"; ";
  world.reindex();
  if(!SaveGame::save(file,world,sim,hud,mapPath))return "save failed: "+SaveGame::lastError();
  return "ok "+r.str()+"entrance "+std::to_string(ex)+","+std::to_string(ey);
}

// ours: the tour guides of a loaded zoo followed while it runs (seconds, 30
// steps a second, not drawn): each trip, its group and its talk, and a
// summary - the side by side with the original (#129, main.cpp
// ZT_VIEW_GUIDES)
std::string watchGuides(World &world, int seconds) {
  std::ostringstream out;
  const bool *keys = SDL_GetKeyboardState(nullptr);
  Staff &staff = world.getStaff();
  Fences &fz = world.getFences();
  struct Seen { bool touring = false, spoke = false; int trips = 0, talks = 0, alone = 0, groupSum = 0, groupMax = 0, tanks = 0; std::set<int> exhibits; };
  std::map<int, Seen> seen;
  for (const Staff::Member &m : staff.members())
    if (staff.types()[m.type].kind == Staff::Kind::Guide) {
      out << "guide " << m.id << " assigned to";
      for (int e : m.exhibits)
        out << " " << e;
      out << "\n";
    }
  for (const Fences::Exhibit &e : fz.exhibits())
    out << "exhibit " << e.id << " " << e.name << (e.named ? "" : " (unnamed)") << (e.tank ? " tank" : "")
        << " animals " << e.animals << " viewing tiles " << world.getGuests().viewingTiles(e.id).size() << "\n";
  {
    ViewingAreas &va = staff.viewingAreas();
    va.refresh(world.getMap(), fz);
    for (const Fences::Exhibit &e : fz.exhibits()) {
      const std::vector<int> areas = va.of(e.id);
      out << "exhibit " << e.id << " viewing areas " << areas.size() << "\n";
      for (size_t i = 0; i < areas.size(); i++) {
        out << " area " << i << " view";
        for (auto [x, y] : va.areas()[areas[i]].viewing)
          out << " (" << x << "," << y << ")";
        out << " sees " << va.areas()[areas[i]].seen.size() << "\n";
      }
    }
  }
  // (looked at every step: a trip can end and the next start within a second)
  for (int step = 0; step <= seconds * 30; step++) {
    const int s = step / 30;
    for (const Staff::Member &m : staff.members()) {
      if (staff.types()[m.type].kind != Staff::Kind::Guide)
        continue;
      Seen &v = seen[m.id];
      const bool touring = m.job == Staff::Job::Tour;
      const Fences::Exhibit *ex = fz.exhibit(m.visit);
      const bool newTrip = touring && (!v.touring || (v.spoke && !m.speaking));
      if (v.touring && !v.spoke && (!touring || newTrip)) {
        v.alone++;
        out << s << "s guide " << m.id << " trip over with no group\n";
      }
      if (newTrip) {
        v.trips++;
        v.spoke = false;
        v.exhibits.insert(m.visit);
        v.tanks += ex && ex->tank ? 1 : 0;
        out << s << "s guide " << m.id << " trip to " << (ex ? ex->name : "?") << (ex && ex->tank ? " (tank)" : "")
            << " animals " << (ex ? ex->animals : 0) << " from " << int(m.x) << "," << int(m.y) << " to " << m.goalX << "," << m.goalY << "\n";
      }
      if (touring && m.speaking && !v.spoke) {
        v.spoke = true;
        v.talks++;
        v.groupSum += static_cast<int>(m.followers.size());
        v.groupMax = std::max(v.groupMax, static_cast<int>(m.followers.size()));
        out << s << "s guide " << m.id << " talks to " << m.followers.size() << "\n";
      }
      v.touring = touring;
      if (step % 300 == 0)
        out << s << "s guide " << m.id << " at " << int(m.x) << "," << int(m.y) << " job " << static_cast<int>(m.job)
            << " followers " << m.followers.size() << (m.speaking ? " speaking" : "") << "\n";
    }
    if (step < seconds * 30)
      world.update(keys, 1.0f / 30.0f);
  }
  out << "guests " << world.getGuests().guests().size() << ", exhibits " << fz.exhibits().size() << "\n";
  for (const auto &[id, v] : seen)
    out << "SUMMARY guide " << id << ": trips " << v.trips << ", talks " << v.talks << ", ended with no group " << v.alone
        << ", average group " << (v.talks ? double(v.groupSum) / v.talks : 0.0) << ", largest " << v.groupMax
        << ", to tanks " << v.tanks << ", exhibits " << v.exhibits.size() << " over " << seconds << " s\n";
  return out.str();
}
