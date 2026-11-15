#include "World.hpp"
#include "GpuTankLayer.hpp"
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
      // (taken away again: the fixtures below start on the empty map)
      a.clear();st.clear();g.clear();
      for(int k=0;k<8;++k){f.remove({true,bx+k,by},map);f.remove({true,bx+k,by+8},map);f.remove({false,bx,by+k},map);f.remove({false,bx+8,by+k},map);}
      f.updateExhibits(map,1,0,1);
      for(int yy=by-3;yy<=by+10;++yy)for(int xx=bx-3;xx<=bx+10;++xx)map.setPath(xx,yy,"");
      va.clear();world.reindex();
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

  // A young one's own numbers (its [y/Characteristics/Integers] over the
  // grown one's): a young T. rex walks at 30 not 60, doesn't jump fences, is
  // more attractive (120 to 100); a young lambeosaurus doesn't test fences
  {
    const int rex=a.typeOfFile("animals/trex.ai"),lambeo=a.typeOfFile("animals/lambeo.ai");
    if(rex>=0 && lambeo>=0) {
      const auto &rt=a.typeList[rex],&lt=a.typeList[lambeo];
      check("a young T. rex: half the walking pace, no jumping, more attractive",
            std::abs(rt.forAge(true).slow-30/60.0f)<1e-4f && std::abs(rt.forAge(false).slow-60/60.0f)<1e-4f &&
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
      for(const auto &e:f.exhibits())guest.seen.insert(e.id);
      for(const auto &o:objects.objects())if(o.id!=fountainId)guest.visited.insert(o.id);
      check("a guest with nothing else to see weighs the fountain",g.sceneryWeight(guest)>=1);
      g.decide(guest);
      check("it goes to look at the fountain, on a tile beside it",
            guest.gawkObject==fountainId && !guest.path.empty() &&
            std::abs(guest.path.back().first-27.5f)<=2.6f && std::abs(guest.path.back().second-27.5f)<=2.6f);
      g.gawkScenery(guest);
      check("there it looks (gawk, camera or stand), facing it",
            guest.state==Guests::State::Gawking && guest.gawkObject<0 &&
            (guest.anim=="gawk1" || guest.anim=="camera" || guest.anim=="stand"));
      guest.state=Guests::State::Walking;g.decide(guest);
      check("a kind looked at isn't gone back to",guest.gawkObject!=fountainId);
    }
    if(added)objects.remove(static_cast<int>(objects.objects().size())-1);
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
        for(int i=0;i<picks;++i){guest.seen.clear();guest.x=24.5f;guest.y=26.5f;int vx=-1,vy=-1;
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
        const int crowd=g.areaCrowd(g.list.front());
        const int want=static_cast<int>(static_cast<float>(k)/static_cast<float>(tiles)*10.0f);
        out<<"GUESTS area crowd: "<<k<<" guests on "<<tiles<<" tiles -> "<<crowd<<"\n";
        check("an area's crowd is its guests over its viewing tiles x 10, cut (0x425f6a)",crowd==want);
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
                  std::abs(v->speed-76/60.0f)<0.001f;}
          check("the boat shack's program 1: out in a kayak at 76 (bReturnboatshak1)",kayak);
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
        // In the water and not on a ride: "Hey! I don't want to be in the
        // water!" and out (Task7f) - to the path at (X - 8, Y)
        g.admit(0);auto &dip=g.list.front();dip.type=womanT;dip.x=X-4.5f;dip.y=Y+0.5f;dip.state=Guests::State::Walking;dip.path.clear();
        dip.chaseCheck=dip.leaveCheck=dip.viewCheck=dip.trashCheck=dip.souvenirCheck=dip.environmentCheck=dip.sawResetCheck=99;
        dip.hungerCheck=dip.thirstCheck=dip.bathroomCheck=dip.energyCheck=99;dip.recheck=false;
        const int dipId=dip.id;g.tick(dip);
        const bool hey=dip.lastThought==10568&&dip.state==Guests::State::Water&&dip.waterTask==Guests::kOutOfWaterTask&&!dip.path.empty();
        bool outOf=false;
        for(int k=0;k<1000&&!outOf;++k){g.update(0.05f,0,1000);const auto *u=g.guest(dipId);if(!u)break;
          outOf=u->state!=Guests::State::Water&&u->waterTask==0;}
        check("in the water off a ride: \"Hey! I don't want to be in the water!\" and out (Task7f)",hey&&outOf);
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
  a.clear();st.clear();g.clear();world.reindex();world.centreOn(31,32);
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
