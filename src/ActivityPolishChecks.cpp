#include "World.hpp"
#include "Animation.hpp"
#include "ItemCatalog.hpp"
#include "IniReader.hpp"
#include "ZooSim.hpp"
#include "SaveGame.hpp"
#include "ui/UiGameScreen.hpp"
#include <filesystem>
#include "Features.hpp"
#include "SpriteDatabase.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

// Compare visible traversal against the complete map traversal on the same
// real renderer and source terrain/path textures. This does not modify a zoo.
std::string runCameraRenderChecks(SDL_Renderer *renderer) {
  extern void worldTerrainCulling(bool);
  constexpr int width=517,height=389;
  SDL_Texture *saved=SDL_GetRenderTarget(renderer);
  float xs=1,ys=1;SDL_RenderGetScale(renderer,&xs,&ys);
  SDL_RenderSetScale(renderer,1,1);SDL_Rect viewport,clip;
  SDL_RenderGetViewport(renderer,&viewport);SDL_RenderGetClipRect(renderer,&clip);
  const bool clipped=SDL_RenderIsClipEnabled(renderer);
  SDL_Texture *target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,width,height);
  if(!target) {SDL_RenderSetScale(renderer,xs,ys);return "FAIL camera render target unavailable";}
  SDL_SetRenderTarget(renderer,target);SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetClipRect(renderer,nullptr);
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
  bool equal=true,sawPixels=false;int cases=0;
  const SDL_Point positions[]={{0,0},{199,0},{199,119},{0,119},{100,60},{70,40},{138,80}};
  for(float zoom:{0.45f,1.0f,1.1f,2.25f})for(int rotation=0;rotation<4;++rotation) {
    camera.zoom=zoom;
    for(const auto p:positions) {
      const auto *tile=map.getTile(p.x,p.y);view.centreOnWorld(p.x+0.5f,p.y+0.5f,tile->height);
      for(bool partial:{false,true}) {
        std::vector<Uint32> reference(width*height),pixels(width*height);
        for(bool bounded:{false,true}) {
          worldTerrainCulling(bounded);SDL_RenderSetScale(renderer,1,1);
          SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderSetScale(renderer,zoom,zoom);
          if(partial)view.renderTerrainTiles(renderer,map,SpriteDatabase::get(),cover);
          else view.renderTerrain(renderer,map,SpriteDatabase::get());
          SDL_RenderFlush(renderer);
          const bool read=SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA8888,pixels.data(),width*4)==0;
          equal &= read;
          if(!bounded) {reference=pixels;sawPixels |= std::any_of(pixels.begin(),pixels.end(),[](Uint32 p){return p!=0x000000ff;});}
          else equal &= pixels==reference;
        }
        ++cases;
      }
    }
    view.rotateView(1);
  }
  worldTerrainCulling(true);SDL_SetRenderTarget(renderer,saved);
  SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,&viewport);
  SDL_RenderSetClipRect(renderer,clipped?&clip:nullptr);SDL_RenderSetScale(renderer,xs,ys);SDL_DestroyTexture(target);
  std::ostringstream report;
  report<<(equal && sawPixels && cases==224?"PASS ":"FAIL ")<<cases
        <<" terrain pixel comparisons: four zooms, four rotations, map edges, raised cliffs, deep pits, paths and partial ground redraw";
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
  auto fileRoundTrip=[&](const std::string &stage) {
    const auto na=world.getAnimals().members().size(),ns=world.getStaff().members().size(),ng=world.getGuests().guests().size();
    ZooSim books;books.start(1000000,50);UiGameScreen hud(rm,"","");
    Goals *priorGoals=SaveGame::goals;SaveGame::goals=nullptr;
    for(const char *extension:{".zt1save",".zoo"}) {
      const auto file=std::filesystem::temp_directory_path()/
        ("zt1-activity-save-"+std::to_string(SDL_GetTicks64())+extension);
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
        pixels+=(pixel & surface->format->Amask)!=0;
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
  SDL_Surface *canvas=SDL_CreateRGBSurfaceWithFormat(0,512,512,32,SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer *probe=canvas?SDL_CreateSoftwareRenderer(canvas):nullptr;
  bool restored=probe!=nullptr;const bool oldTankClip=Features::tankClip;Features::tankClip=true;
  world.centreOn(38,32);
  if(probe)for(float scale:{1.0f,1.5f,2.0f})for(int turn=0;turn<4;++turn) {
    SDL_RenderSetScale(probe,1,1);SDL_Rect expected{17,23,3000,3000};
    SDL_RenderSetClipRect(probe,&expected);SDL_RenderSetScale(probe,scale,scale);
    animals.drawInTank(probe,world.getRenderer(),map,tankId,false);
    float xScale=0,yScale=0;SDL_RenderGetScale(probe,&xScale,&yScale);
    SDL_RenderSetScale(probe,1,1);SDL_Rect got;SDL_RenderGetClipRect(probe,&got);
    restored &= SDL_RenderIsClipEnabled(probe) && got.x==expected.x && got.y==expected.y &&
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
  samePixels &= fish!=nullptr;
  const Camera oldCamera=world.getCamera();
  if(probe && fish)for(float scale:{1.0f,1.5f,2.0f})for(int rotation=0;rotation<4;++rotation) {
    float sx,sy,depth;world.getRenderer().worldToScreenF(38,32,ex->floorHeight,sx,sy,depth);
    world.getCamera().x+=128-sx;world.getCamera().y+=100-sy;
    fish->anim="subswim";fish->swimZ=fish->swimTo=fn.waterLevel(*ex)*0.5f;
    fish->x=rotation%2?36.1f:38.0f;fish->y=rotation%2?30.1f:32.0f;
    std::vector<Uint8> reference;
    for(bool fast:{false,true}) {
      tankAnimalFastPaths(fast);
      SDL_RenderSetScale(probe,1,1);SDL_RenderSetClipRect(probe,nullptr);
      SDL_SetRenderDrawColor(probe,0,0,0,0);SDL_RenderClear(probe);
      SDL_Rect parent{17,23,450,450};SDL_RenderSetClipRect(probe,&parent);SDL_RenderSetScale(probe,scale,scale);
      animals.drawInTank(probe,world.getRenderer(),map,tankId,false);SDL_RenderFlush(probe);
      const auto *bytes=static_cast<const Uint8*>(canvas->pixels);
      if(!fast) {
        reference.assign(bytes,bytes+canvas->pitch*canvas->h);
        sawPixels |= std::any_of(reference.begin(),reference.end(),[](Uint8 value){return value!=0;});
      } else samePixels &= std::equal(reference.begin(),reference.end(),bytes);
    }
    world.getRenderer().rotateView(1);
  }
  tankAnimalFastPaths(true);world.getCamera()=oldCamera;if(swimmer>=0)animals.remove(swimmer);
  check("optimized whale clipping matches exact pixel clips at three scales and four rotations",samePixels && sawPixels);
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
        custom.custom=[](SDL_Renderer *r){SDL_SetRenderDrawColor(r,250,70,30,255);SDL_Rect box{10,10,8,8};SDL_RenderFillRect(r,&box);};
        extra.push_back(custom);
        // An off-screen custom strip must have no effect on visible pixels.
        custom.clip=true;custom.clipX0=-1000;custom.clipX1=-20;extra.push_back(custom);
        std::vector<Uint8> reference;
        for(bool cull:{false,true}) {
          worldSpriteCulling(cull);SDL_RenderSetScale(probe,1,1);SDL_RenderSetClipRect(probe,nullptr);
          SDL_SetRenderDrawColor(probe,0,0,0,0);SDL_RenderClear(probe);
          SDL_Rect parent{7,9,499,497};SDL_RenderSetClipRect(probe,&parent);SDL_RenderSetScale(probe,scale,scale);
          scene.draw(probe,world.getRenderer(),map,extra);SDL_RenderFlush(probe);
          float xs=0,ys=0;SDL_RenderGetScale(probe,&xs,&ys);SDL_RenderSetScale(probe,1,1);
          SDL_Rect got;SDL_RenderGetClipRect(probe,&got);
          cullState &= xs==scale && ys==scale && SDL_RenderIsClipEnabled(probe) &&
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
  Features::tankClip=oldTankClip;if(probe)SDL_DestroyRenderer(probe);if(canvas)SDL_FreeSurface(canvas);
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
        ("zt1-parity-save-"+std::to_string(SDL_GetTicks64())+extension);
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

  a.clear();
  const int embryo=a.adopt(dino,false,30.5f,32.5f,map,f,true);
  if(auto *m=a.member(embryo)) {a.becomeEgg(*m);m->hatchIn=1;m->health=float(a.typeList[dino].maxHits);a.eggTick(*m);}
  fileRoundTrip("dinosaur egg during hatch");
  for(int k=0;k<4000&&a.member(embryo)&&a.member(embryo)->egg;++k)a.update(0.05f,a.now+0.05,map,f);
  check("interrupted hatch produces one live baby with its identity",a.list.size()==1&&a.member(embryo)&&
        !a.member(embryo)->egg&&a.member(embryo)->baby);
  a.clear();
  const int sleeper=a.adopt(a.typeOfFile("animals/zebra.ai"),false,30.5f,32.5f,map,f,true);
  if(auto *m=a.member(sleeper)) {m->energy=float(a.typeList[m->type].energyThreshold);m->hunger=0;a.chooseBehaviour(*m);}
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
    check("guest replans, buys food and reduces hunger after reload",used&&savedUse&&patron&&patron->hunger<90&&counter&&counter->income>0);
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
  a.clear();st.clear();g.clear();world.reindex();world.centreOn(31,32);
  out<<passed<<"/"<<total<<" parity checks passed\n";return out.str();
}
