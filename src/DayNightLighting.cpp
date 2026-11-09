#include "DayNightLighting.hpp"
#include "GraphicsQuality.hpp"
#include "Fences.hpp"
#include "Animation.hpp"
#include "World.hpp"
#include "ParallelFor.hpp"
#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "PlacedObjects.hpp"
#include "ResourceManager.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>

namespace FrameProfile {extern bool active;void record(const char *,double);double ms(Uint64);}
namespace {
struct LightTimer {
 Uint64 start=0;
 LightTimer(){if(FrameProfile::active)start=SDL_GetPerformanceCounter();}
 void mark(const char *name){if(start){FrameProfile::record(name,FrameProfile::ms(start));start=SDL_GetPerformanceCounter();}}
};
}

namespace {
constexpr float pi = 3.14159265359f;
constexpr float kFoliageShadow = .55f; // addition: how much sun a tree's or bush's leaves stop
float smooth(float a, float b, float x) {
  float t = std::clamp((x-a)/(b-a), 0.0f, 1.0f);
  return t*t*(3-2*t);
}
float heightAt(const WorldMap &map, float x, float y) {
  const auto *t = map.getTile(int(std::floor(x)), int(std::floor(y)));
  if (!t) return 0;
  float fx=x-std::floor(x), fy=y-std::floor(y);
  return (t->cornerHeight[0]*(1-fx)+t->cornerHeight[1]*fx)*(1-fy) +
         (t->cornerHeight[3]*(1-fx)+t->cornerHeight[2]*fx)*fy;
}
// Preserve the caller's SDL state even if target creation fails. In particular,
// a target switch resets SDL's viewport, scale and clip rectangle.
struct RenderState {
  SDL_Renderer *r; SDL_Texture *target; SDL_Rect viewport, clip;
  float sx,sy; SDL_BlendMode blend; Uint8 red,green,blue,alpha; bool clipped;
  explicit RenderState(SDL_Renderer *renderer):r(renderer),target(SDL_GetRenderTarget(r)) {
    SDL_GetRenderViewport(r,&viewport); SDL_GetRenderScale(r,&sx,&sy);
    clipped=SDL_RenderClipEnabled(r); SDL_GetRenderClipRect(r,&clip);
    SDL_GetRenderDrawBlendMode(r,&blend); SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
  }
  ~RenderState() {
    SDL_SetRenderTarget(r,target); SDL_SetRenderScale(r,sx,sy);
    SDL_SetRenderViewport(r,&viewport); SDL_SetRenderClipRect(r,clipped?&clip:nullptr);
    SDL_SetRenderDrawBlendMode(r,blend); SDL_SetRenderDrawColor(r,red,green,blue,alpha);
  }
};
}

DayNightLighting::~DayNightLighting() {
  SDL_DestroyTexture(lightmap);
  for(size_t i=0;i<maskPages.size();++i){bool shared=false;for(size_t j=0;j<i;++j)shared|=maskPages[i].texture==maskPages[j].texture;if(!shared)SDL_DestroyTexture(maskPages[i].texture);}
}

// addition: a twelve-minute visual day, opening at noon. This deliberately
// leaves the original game's calendar, opening hours and animal AI alone.
double DayNightLighting::hourAt(double clock) {
  double h=std::fmod(12.0+clock/30.0,24.0);
  return h<0?h+24:h;
}
std::string DayNightLighting::formatTime(double hour) {
  int minute=int(std::floor(std::fmod(hour+48,24)*60))%1440;
  char text[16];std::snprintf(text,sizeof text,"%02d:%02d",minute/60,minute%60);
  return text;
}
DayNightLighting::Sky DayNightLighting::skyAt(double hour) {
  const float h=float(std::fmod(hour+48,24));
  struct Key { float hour; SDL_Color colour; };
  const Key keys[]={{0,{82,103,148,255}},{5,{82,103,148,255}},
    {6,{190,137,139,255}},{8,{255,255,255,255}},{16,{255,255,255,255}},
    {18,{244,176,139,255}},{20,{82,103,148,255}},{24,{82,103,148,255}}};
  SDL_Color colour=keys[0].colour;
  for(size_t i=1;i<std::size(keys);++i) if(h<=keys[i].hour) {
    const auto &a=keys[i-1],&b=keys[i]; float t=smooth(a.hour,b.hour,h);
    auto ch=[&](int x,int y){return Uint8(std::lround(x+(y-x)*t));};
    colour={ch(a.colour.r,b.colour.r),ch(a.colour.g,b.colour.g),ch(a.colour.b,b.colour.b),255}; break;
  }
  float sun=std::max(0.0f,std::sin((h-6)*pi/12));
  float lamps=1-smooth(5,8,h)*(1-smooth(17,20,h));
  // East in the morning, west in the evening, fixed in world space.
  const float altitude=sun*1.13f;
  // (no longer than 1.25 heights - about the sun at 40 degrees: as it set they
  // reached 7, every person, post and tree drawn as a line across the screen)
  const float length=std::clamp(std::cos(altitude)/std::max(.10f,std::sin(altitude)),.35f,1.25f);
  // (and they fade as the light goes soft, from about two hours before
  // sunset - and after sunrise - rather than only in its last minutes)
  const float shadow=smooth(0.0f,.45f,sun);
  return {colour,lamps,sun,length,
          -std::cos((h-6)*pi/12),std::sin((h-6)*pi/12)*.8f,shadow};
}

DayNightLighting::Type DayNightLighting::typeOf(ResourceManager *rm,const std::string &file,const std::string &kind) {
  auto it=types.find(file); if(it!=types.end())return it->second;
  Type t;
  const CatalogItem *item=ItemCatalog::get().find(file);
  const bool building=kind=="building";
  const bool tree=item && item->members.count("foliage");
  const bool rock=item && item->members.count("rocks");
  t.casts=building||tree||rock||(item && item->members.count("scenery"));
  t.foliage=tree;
  // addition: lights come from the item's authored material/source definition.
  const bool lamp=file=="scenery/other/lamp.ai" || file=="scenery/other/bklamp.ai";
  std::string name=file.substr(file.find_last_of('/')+1);const auto dot=name.find_last_of('.');if(dot!=std::string::npos)name.resize(dot);
  if(const auto *asset=LightingAssets::find(name))t.profile=asset->id;
  if(lamp)t.casts=true;
  if(rm) if(std::unique_ptr<IniReader> ai{rm->getIniReader(file)}) {
    t.halfX=std::clamp(ai->getInt("characteristics/integers","cfootprintx",1)/4.0f,.15f,12.0f);
    t.halfY=std::clamp(ai->getInt("characteristics/integers","cfootprinty",1)/4.0f,.15f,12.0f);
    // addition: an explicit material definition can be selected by a mod.
    t.profile=ai->getInt("lighting","profile",t.profile);
    t.casts=ai->getInt("lighting","castshadow",t.casts?1:0)!=0;
    // addition: a mod item's own light maps and the lights it casts (the Lab's
    // Night light layer and Lamp point): light = x, y, z, r, g, b, power,
    // radius - x, y in tiles from its middle, turning with it, z its height
    // in the units of the lamps' own (the street lamp's lantern is 3.9)
    if(ai->getInt("lighting","lightmap",0)>0) {
      std::vector<LightingAssets::Source> sources;
      for(const auto &line:ai->getList("lighting","light")) {
        float v[8]={0,0,2,255,226,159,1,5};int n=0;std::stringstream in(line);std::string part;
        while(n<8 && std::getline(in,part,',')){try{v[n]=std::stof(part);}catch(...){break;}++n;}
        if(n<3)continue;
        auto byte=[](float c){return Uint8(std::clamp(c,0.0f,255.0f));};
        sources.push_back({"mod light",{v[0],v[1],std::clamp(v[2],0.0f,12.0f)},{0,0,0},{byte(v[3]),byte(v[4]),byte(v[5]),255},std::clamp(v[6],0.0f,4.0f),std::clamp(v[7],.5f,12.0f)});
      }
      t.profile=LightingAssets::modProfile(file,std::move(sources));
    }
  }
  return types[file]=t;
}

void DayNightLighting::prepare(World &world,ResourceManager *rm,const PlacedObjects &objects,const WorldMap &map,
                               WorldRenderer &view,double clock,int width,int height,SDL_Renderer *renderer) {
  counters={};lights.clear();emitters.clear();instances.clear();transientLights.clear();frameRenderer=renderer;frameView=&view;frameMap=&map;frameFences=&world.getFences();fluorescenceCues.clear();fluorescentArt.clear();fixtureClock=previewBeamTime>=0?previewBeamTime:clock;
  hour=previewHour>=0?previewHour:hourAt(clock);sky=skyAt(hour);
  const float zoom=view.getCamera().zoom,w=width/zoom,h=height/zoom;logicalW=w;logicalH=h;
  covers.clear();size_t hash=1469598103934665603ULL;
  auto mix=[&](size_t value){hash^=value;hash*=1099511628211ULL;};
  mix(size_t(GraphicsQuality::serial())); // (a graphics quality change: the lighting made again)
  const auto &all=objects.objects();int coldBuilds=0;const Uint64 coldStart=SDL_GetPerformanceCounter();
  auto mayBuild=[&]{return coldBuilds<2 && (!coldBuilds || 1000.0*(SDL_GetPerformanceCounter()-coldStart)/SDL_GetPerformanceFrequency()<3);};
  // ours: a shape is made on a worker thread (none until it's made, as one
  // past the frame's two); the check modes make it at once, as before
  static const bool atOnce=std::getenv("ZT_LIGHTING_CHECKS") || std::getenv("ZT_SHADOW_CHECKS") || std::getenv("ZT_LIGHTING_BENCH") || std::getenv("ZT_LIGHTING_GALLERY");
  for(auto i=pendingModels.begin();i!=pendingModels.end();) {
    if(i->second.result.wait_for(std::chrono::seconds(0))!=std::future_status::ready){++i;continue;}
    auto model=i->second.result.get();LightingAssets::finishBuild(*i->second.job,model);
    SDL_Log("[LightingModel] %s: %d solid cells, %zu surface quads",std::get<2>(i->first).c_str(),model.occupied,model.faces.size());
    models.emplace(i->first,std::move(model));i=pendingModels.erase(i);
  }
  auto buildModel=[&](const std::tuple<Animation*,int,std::string> &key,Animation &art,const LightingAssets::Definition &definition,float halfX,float halfY,int frame,bool actor) {
    if(atOnce)return models.emplace(key,LightingAssets::build(art,definition,halfX,halfY,frame,actor)).first;
    if(pendingModels.count(key) || pendingModels.size()+pendingActorModels.size()>=2)return models.end();
    LightingAssets::Model cached;bool hit=false;
    const auto job=LightingAssets::prepareBuild(art,definition,halfX,halfY,frame,actor,cached,hit);
    if(hit)return models.emplace(key,std::move(cached)).first;
    if(!job)return models.emplace(key,LightingAssets::Model{}).first;
    pendingModels.emplace(key,PendingActorModel{job,std::async(std::launch::async,[job]{return LightingAssets::runBuildInBackground(*job);})});
    return models.end();
  };
  for(size_t i=0;i<all.size();++i) {
    const auto &o=all[i];if(o.fence || !o.art || objects.hiddenNow(o))continue;

    const auto t=typeOf(rm,PlacedObjects::fileOf(o),o.className);
    if(!t.casts && !t.profile)continue;
    const auto *asset=LightingAssets::profile(t.profile);
    float sx,sy,depth;const float ground=heightAt(map,o.x,o.y);const float z=objects.tankHeight?objects.tankHeight(o,ground):ground;view.worldToScreenF(o.x,o.y,z,sx,sy,depth);
    const float distance=(sx-w*.5f)*(sx-w*.5f)+(sy-h*.5f)*(sy-h*.5f);
    const float angle=o.facing*pi/4,c=std::cos(angle),s=std::sin(angle);
    const auto side=view.screenSide(s,-c);
    if(t.casts) {
      auto key=std::make_tuple(o.art,t.profile,PlacedObjects::fileOf(o));auto found=models.find(key);
      if(found==models.end() && mayBuild()) {
        ++coldBuilds;LightingAssets::Definition plain{0,"solid scenery"};
        found=buildModel(key,*o.art,asset?*asset:plain,t.halfX,t.halfY,-1,false);
        if(found!=models.end())SDL_Log("[LightingModel] %s: %d solid cells, %zu surface quads",o.typeName.c_str(),found->second.occupied,found->second.faces.size());
      }
      if(found!=models.end() && !found->second.faces.empty()) {
        instances.push_back({&found->second,asset,{o.x,o.y,z},c,s,distance,i});
        // addition: leaves let some light through - a canopy's shadow lighter than a roof's
        if(t.foliage)instances.back().opacity=kFoliageShadow;
        // addition: everything but buildings casts from its own sprite (LightingShadows spriteShadow)
        instances.back().sprite=o.className!="building" && std::fabs(z-ground)<.01f;
        instances.back().artwork=PlacedObjects::displayArt(o);instances.back().frame=PlacedObjects::animationFrame(o,instances.back().artwork);
        mix(i);mix(reinterpret_cast<size_t>(&found->second));mix(size_t(found->second.occupied));mix(std::hash<float>{}(o.x));mix(std::hash<float>{}(o.y));mix(std::hash<float>{}(z));mix(o.facing);
      }
    }
    for(auto *layer:{o.bg,o.underIdle})if(layer) {
      const auto key=std::make_tuple(layer,0,PlacedObjects::fileOf(o)+":secondary");auto found=models.find(key);
      if(found==models.end() && mayBuild()){++coldBuilds;LightingAssets::Definition plain{0,"secondary scenery layer"};found=buildModel(key,*layer,plain,t.halfX,t.halfY,-1,layer==o.underIdle);}
      if(found!=models.end() && !found->second.faces.empty()){
        instances.push_back({&found->second,nullptr,{o.x,o.y,z},c,s,distance,i});
        if(t.foliage)instances.back().opacity=kFoliageShadow;
        instances.back().sprite=o.className!="building" && std::fabs(z-ground)<.01f;
        instances.back().artwork=layer;instances.back().frame=PlacedObjects::animationFrame(o,layer);
        mix(i);mix(reinterpret_cast<size_t>(&found->second));mix(std::hash<float>{}(o.x));mix(std::hash<float>{}(o.y));mix(std::hash<float>{}(z));mix(o.facing);
      }
    }
    Animation *display=PlacedObjects::displayArt(o);bool sourceActive=true;
    if(asset && asset->animated && !asset->sources.empty()) {
      bool fireOnly=true,hasRegion=false;
      for(const auto &v:asset->views)for(const auto &r:v){hasRegion=true;if(r.material!=LightingAssets::Material::Fire)fireOnly=false;}
      if(hasRegion && fireOnly) {
        sourceActive=false;
        if(display)for(auto direction:{CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW}) {
          for(const auto &patch:display->emissionPatches(renderer,direction,PlacedObjects::animationFrame(o,display),asset->id))if(patch.pixels){sourceActive=true;break;}
          if(sourceActive)break;
        }
      }
    }
    const std::vector<LightingAssets::Source> *sources=asset?&asset->sources:nullptr;
    if(asset && o.className=="building" && asset->id>=200 && asset->id<400 && asset->id!=283) {
      auto model=models.find(std::make_tuple(o.art,t.profile,PlacedObjects::fileOf(o)));
      if(model!=models.end()){const auto &windows=windowSources(model->second,*asset);if(!windows.empty())sources=&windows;}
    }
    if(sources && sourceActive)for(const auto &source:*sources) {
      LightingAssets::Point p{o.x+source.position.x*c-source.position.y*s,o.y+source.position.x*s+source.position.y*c,z+source.position.z};
      Light l{p.x,p.y,p.z,source.radius,source.position.z,i,distance};l.energy=source.power;l.colour=source.colour;l.cone=source.cone;l.sweep=source.sweep;
      l.normal={source.normal.x*c-source.normal.y*s,source.normal.x*s+source.normal.y*c,source.normal.z};lights.push_back(l);
    }
    if(sx < -400 || sx>w+400 || sy < -400 || sy>h+500)continue;
    float l,top,r,b;
    if(display && display->anchoredBounds(sx,sy,side,l,top,r,b))covers.push_back({i,display,side,sx,sy,depth,l,top,r,b,PlacedObjects::animationFrame(o,display)});
    if(!asset || sky.lamps<=.001f)continue;
    // Moving materials sample their own pose; steady regions use frame zero.
    // Reviewed active fixture animations can light an empty building at night.
    auto emission=[&](Animation *materialArt,int profile) {
      if(!materialArt)return;const int frame=PlacedObjects::animationFrame(o,materialArt);
      for(const auto &patch:materialArt->emissionPatches(renderer,side,frame,profile)) {
        SDL_FlipMode flip;const auto box=materialArt->emissionRect(patch,sx,sy,side,&flip);
        Light e{o.x,o.y,z,0,0,i,distance};e.sx=sx+(flip==SDL_FLIP_HORIZONTAL?-patch.offset.x:patch.offset.x);e.sy=sy+patch.offset.y;
        e.patch=box;e.texture=patch.texture;e.flip=flip;e.depth=depth;e.kind=profile;e.reflective=patch.reflective;e.glow=patch.glow;e.glowPad=patch.glowPad;emitters.push_back(e);
      }
    };
    const size_t before=emitters.size();
    emission(asset->animated?display:o.art,asset->id);
    // (a mod item lit only in its idle art: that glow while it's in use too)
    if(asset->lightMap && emitters.size()==before && display!=o.art)emission(o.art,asset->id);
    if(asset->nightFixtures && o.used)emission(o.used,asset->id+3000);
  }
  // The scene hash is independent of the camera: a pan reuses 3D irradiance.
  // Keep source selection stable for normal zoos; the cap bounds extreme maps.
  mix(collectStructures(world));
  // addition: over the shape budget, the ones nearest the screen's middle.
  // (projectModels took them in the zoo's object order: in a big zoo every
  // building after the first ~60 thousand faces had no shadow or shading,
  // however close to the camera.) Under the budget nothing changes.
  // ours: chosen again only when the view has moved a quarter of the screen,
  // turned or zoomed, or the shapes themselves changed - not on every pan.
  // (The choice is part of the scene's stamp: chosen each frame, every pan in
  // a big zoo remade all the lamps' rays, the faces' projection and the sun's
  // samples, and the game slowed with each building.)
  {
    size_t total=0,candidates=1469598103934665603ULL;
    for(const auto &o:instances){total+=o.model->faces.size();candidates^=o.owner*1099511628211ULL+size_t(o.model);candidates*=1099511628211ULL;}
    // (the budget as the graphics quality's shadow range has it - maxModelFaces
    // at most - chosen again when it changes)
    const int faceBudget=std::min(maxModelFaces,GraphicsQuality::shapeFaceBudget());
    candidates^=size_t(faceBudget);candidates*=1099511628211ULL;
    if(total>size_t(faceBudget)) {
      float cx=0,cy=0;view.screenToWorldAt(w*.5f,h*.5f,0,cx,cy);
      float lx=0,ly=0,ld=0;view.worldToScreenF(keptCentre[0],keptCentre[1],0,lx,ly,ld);
      const bool moved=std::hypot(lx-w*.5f,ly-h*.5f)>std::max(w,h)*.25f;
      if(moved || keptCandidates!=candidates || keptRotation!=view.getRotation() || keptZoom!=zoom || keptOwners.empty()) {
        std::vector<size_t> order(instances.size());for(size_t k=0;k<order.size();++k)order[k]=k;
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return instances[a].distance<instances[b].distance;});
        int budget=faceBudget;keptOwners.clear();
        for(size_t k:order){const int faces=int(instances[k].model->faces.size());if(faces>budget)continue;budget-=faces;keptOwners.insert({instances[k].owner,instances[k].model});}
        keptCentre[0]=cx;keptCentre[1]=cy;keptCandidates=candidates;keptRotation=view.getRotation();keptZoom=zoom;
      }
      std::vector<Instance> kept;kept.reserve(instances.size());
      for(const auto &o:instances)if(keptOwners.count({o.owner,o.model})){kept.push_back(o);mix(size_t(o.owner));}
      instances.swap(kept);
    } else {
      keptOwners.clear();
    }
  }
  // ours: the shapes' part, for the faces' projection (the lights mixed in
  // below only change the lamps' light on them) - with the shapes chosen
  // over the budget, just above: taken before them, a new choice kept the
  // old projection, its faces pointing at other buildings in the new list
  // (their masks and light) and some past its end; and whether it's night,
  // when fences and solid tank walls are shaded too (projectModels' visible)
  geometryHash=hash^(sky.shadow<.02f?0x9e3779b97f4a7c15ull:0);
  // addition: fence pieces with lights of their own (Fences::litPieces: the
  // electrified bars' beacons) - their glow as the piece's pose shows it, and
  // a small light on the ground under it, there while it glows
  {
    uint64_t serial=0;
    for(const auto &f:world.getFences().litPieces()) {
      const auto *asset=LightingAssets::find(f.key);if(!asset || !f.art)continue;++serial;
      const uint64_t owner=(uint64_t(1)<<53)|serial;
      // (where it is on the screen now: Fences::collect's may be a frame old)
      float fsx,fsy,fdepth;view.worldToScreenF(f.x,f.y,f.z,fsx,fsy,fdepth);
      const float distance=(fsx-w*.5f)*(fsx-w*.5f)+(fsy-h*.5f)*(fsy-h*.5f);
      bool glowing=false;
      const bool near=fsx>-200 && fsx<w+200 && fsy>-300 && fsy<h+300;
      if(near && sky.lamps>.001f)for(const auto &patch:f.art->emissionPatches(renderer,f.side,f.frame,asset->id)) {
        if(!patch.pixels)continue;glowing=true;
        SDL_FlipMode flip;const auto box=f.art->emissionRect(patch,fsx,fsy,f.side,&flip);
        Light e{f.x,f.y,f.z,0,0,owner,distance};e.sx=fsx+(flip==SDL_FLIP_HORIZONTAL?-patch.offset.x:patch.offset.x);e.sy=fsy+patch.offset.y;
        e.patch=box;e.texture=patch.texture;e.flip=flip;e.depth=fdepth;e.kind=asset->id;e.reflective=patch.reflective;e.glow=patch.glow;e.glowPad=patch.glowPad;emitters.push_back(e);
      }
      // (always listed, so blinking never remakes the lighting: only shown)
      for(const auto &source:asset->sources) {
        Light l{f.x+source.position.x,f.y+source.position.y,f.z+source.position.z,source.radius,source.position.z,owner,distance};
        l.energy=source.power;l.colour=source.colour;l.groundOnly=true;l.shown=glowing?1.0f:0.0f;lights.push_back(l);
      }
    }
  }
  for(const auto &o:structureInstances)if(o.asset)for(const auto &source:o.asset->sources) {
    const auto p=toWorld(o,source.position);Light l{p.x,p.y,p.z,source.radius,source.position.z,o.owner,0};l.energy=source.power;l.colour=source.colour;l.cone=source.cone;l.sweep=source.sweep;lights.push_back(l);
  }
  for(const auto &m:world.getStaff().members())if(world.getStaff().types()[m.type].kind==Staff::Kind::Helicopter) {
    const auto &t=world.getStaff().types()[m.type];auto *art=t.baseUsed?t.baseUsed:world.getStaff().baseArt(m);
    // addition: native pad lamps keep their nine-frame night sequence even
    // while the aircraft is away (the unoccupied body has a single dark pose).
    if(const auto *asset=LightingAssets::find("helibase"))actorEmission(art,world.getStaff().baseFrame(m,art),{m.x,m.y,LightingAssets::terrainHeight(map,m.x,m.y)},0,-1,(uint64_t(1)<<54)|(uint64_t(uint32_t(m.id))<<3)|3,asset->id);
  }
  const size_t limit=size_t(std::clamp(std::min(diagnosticLightLimit,GraphicsQuality::lampLimit()),1,512)); // (the graphics quality's lamps)
  // addition: reserve the bounded projector slots before ordinary fixtures.
  std::stable_partition(lights.begin(),lights.end(),[](const Light &l){return l.cone>0 && l.sweep!=0;});
  int projectors=0;lights.erase(std::remove_if(lights.begin(),lights.end(),[&](const Light &l){return l.cone>0 && l.sweep!=0 && ++projectors>4;}),lights.end());
  if(lights.size()>limit)lights.resize(limit);
  for(auto &l:lights){const float a=float(fixtureClock*l.sweep),c=std::cos(a),s=std::sin(a);l.axis={l.normal.x*c-l.normal.y*s,l.normal.x*s+l.normal.y*c,l.normal.z};const float n=std::sqrt(l.axis.x*l.axis.x+l.axis.y*l.axis.y+l.axis.z*l.axis.z);if(n>.001f){l.axis.x/=n;l.axis.y/=n;l.axis.z/=n;}}
  for(const auto &source:lights){mix(source.colour.r);mix(source.colour.g);mix(source.colour.b);mix(source.owner);mix(std::hash<float>{}(source.x));mix(std::hash<float>{}(source.y));mix(std::hash<float>{}(source.z));for(float f:{source.energy,source.radius,source.normal.x,source.normal.y,source.normal.z,source.cone,source.sweep})mix(std::hash<float>{}(f));}
  mix(lights.size());mix(map.getGeneration());
  // ours: the lamps' light on the ground and the shapes made only while the
  // lamps are lit at all (from about five in the afternoon to eight in the
  // morning; by day it's multiplied by nought everywhere it's used) - in a
  // big zoo, each new choice of shapes as the view moved cast every shape's
  // rays again by day too, tens of milliseconds. The index of what each lamp
  // can be blocked by (blocked: an animal's and fluorescence's light) is
  // made with the scene, as before
  if(hash!=sceneHash){sceneHash=hash;indexLocalLights();}
  if(sky.lamps>0 && (irradianceHash!=sceneHash || groundLight.size()!=lights.size())){irradianceHash=sceneHash;buildIrradiance(map);++irradianceSerial;}
  finishEmitters();
  prepareBeamShapes();
  projectModels(view);counters.modelFaces=int(projectedFaces.size());
  counters.models=int(instances.size());counters.lights=sky.lamps>.001f?int(lights.size()):0;
}

bool DayNightLighting::targets(SDL_Renderer *renderer,int w,int h) {
  if(!true)return false;
  if(lightmap && (w!=textureW || h!=textureH)) { SDL_DestroyTexture(lightmap);lightmap=nullptr; }
  if(!lightmap) {
    lightmap=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
    if(!lightmap)return false;
    textureW=w;textureH=h;
    SDL_SetTextureBlendMode(lightmap,SDL_BLENDMODE_MOD);
    SDL_SetTextureScaleMode(lightmap,SDL_SCALEMODE_LINEAR);
  }
  return true;
}

void DayNightLighting::composite(SDL_Renderer *renderer,WorldRenderer &view,const WorldMap &map,const SDL_Rect &rect) {
  LightTimer timer;RenderState saved(renderer);
  if(rect.w<=0 || rect.h<=0)return;
  const float down=std::min(1.0f,float(maxMapWidth)/std::max(rect.w,rect.h));
  const int w=std::max(1,int(std::ceil(rect.w*down))),h=std::max(1,int(std::ceil(rect.h*down)));
  if(!targets(renderer,w,h)) {
    if(!warned) { SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"[Lighting] render targets unavailable; using normal daylight: %s",SDL_GetError());warned=true; }
    return;
  }
  if(!SDL_SetRenderTarget(renderer,lightmap))return;
  SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderScale(renderer,1,1);SDL_SetRenderClipRect(renderer,nullptr);
  SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(renderer,sky.ambient.r,sky.ambient.g,sky.ambient.b,255);SDL_RenderClear(renderer);
  const float zoom=view.getCamera().zoom,scale=float(w)/rect.w*zoom;
  // A world-anchored dawn/dusk front, not a screen overlay that moves with
  // the camera. The colour changes smoothly across the map over ~30 seconds.
  auto &vertices=fieldVertices;auto &indices=fieldIndices;vertices.clear();indices.clear();
  constexpr int cols=16,rows=10;
  for(int y=0;y<=rows;++y)for(int x=0;x<=cols;++x) {
    float px=float(x)*w/cols,py=float(y)*h/rows,wx,wy;
    view.screenToWorldAt(px/scale,py/scale,0,wx,wy);
    auto c=skyAt(hour+(wx-map.getWidth()*.5f)*.008).ambient;
    vertices.push_back({{px,py},c,{0,0}});
  }
  for(int y=0;y<rows;++y)for(int x=0;x<cols;++x) {
    int a=y*(cols+1)+x,b=a+1,c=a+cols+1,d=c+1;
    indices.insert(indices.end(),{a,b,d,a,d,c});
  }
  ZT_RenderGeometry(renderer,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
  timer.mark("ambient field");
  // Cached 3D irradiance projected onto the floor. No planar footprint ray
  // or prepainted radial pool substitutes for object occlusion.
  SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_ADD);
  vertices.clear();indices.clear();
  // ours: a lamp's pool wholly off the lightmap draws nothing, so it isn't
  // sent: its samples' box (to the map's top: a tank's water can lift one),
  // put on the screen - the projection keeps every sample inside it - and
  // left out when the box misses the lightmap. The same pixels: its
  // triangles fell outside, and lamps add (in any order)
  groundBoxes.resize(groundLight.size());
  auto offMap=[&](size_t li) {
    const auto &grid=groundLight[li];auto &box=groundBoxes[li];
    if(box.data!=grid.data() || box.count!=grid.size()) {
      box.data=grid.data();box.count=grid.size();box.lo={1e30f,1e30f,1e30f};box.hi={-1e30f,-1e30f,-1e30f};
      for(const auto &s:grid){box.lo={std::min(box.lo.x,s.position.x),std::min(box.lo.y,s.position.y),std::min(box.lo.z,s.position.z)};
        box.hi={std::max(box.hi.x,s.position.x),std::max(box.hi.y,s.position.y),std::max(box.hi.z,s.position.z)};}
      box.hi.z=std::max(box.hi.z,float(map.getMaxHeight()));
    }
    if(grid.empty())return true;
    float l=1e30f,r=-1e30f,tp=1e30f,b=-1e30f;
    for(int k=0;k<8;++k){float x,y,d;view.worldToScreenF(k&1?box.hi.x:box.lo.x,k&2?box.hi.y:box.lo.y,k&4?box.hi.z:box.lo.z,x,y,d);
      l=std::min(l,x*scale);r=std::max(r,x*scale);tp=std::min(tp,y*scale);b=std::max(b,y*scale);}
    return r< -2 || l>w+2 || b< -2 || tp>h+2;
  };
  // (each lamp's vertices and triangles given their place, then made side
  // by side on the cores - the same ones, in the same order)
  struct GroundJob{size_t light,vertex,index;int cells;};std::vector<GroundJob> groundJobs;size_t groundVertices=0,groundIndices=0;
  if(sky.lamps>.001f)for(size_t li=0;li<groundLight.size();++li) {
    if(offMap(li))continue;
    const auto &source=lights[li];const int cells=source.sweep!=0 && source.cone>0?48:source.cone>0?32:20;
    groundJobs.push_back({li,groundVertices,groundIndices,cells});groundVertices+=groundLight[li].size();groundIndices+=size_t(cells)*cells*6;
  }
  vertices.resize(groundVertices);indices.resize(groundIndices);
  parallelFor(groundJobs.size(),[&](size_t begin,size_t end){for(size_t job=begin;job<end;++job) {
    const size_t li=groundJobs[job].light;const int cells=groundJobs[job].cells;
    const auto &grid=groundLight[li];const auto &source=lights[li];
    const int first=int(groundJobs[job].vertex);size_t at=groundJobs[job].vertex,ix=groundJobs[job].index;
    for(const auto &sample:grid) {
      // (on a tank's water: laid on its surface and mostly taken in by it -
      // the glints on top are drawEmission's - not lit as ground would be)
      const float water=waterLevelAt(sample.position.x,sample.position.y);
      const float z=water>-1e8f?std::max(sample.position.z,water):sample.position.z;
      float x,y,d;view.worldToScreenF(sample.position.x,sample.position.y,z,x,y,d);
      auto colour=sample.colour;if(source.cone>0 && source.sweep!=0){const float e=1-std::exp(-sample.energy*sourceDirection(source,sample.position));colour={Uint8(source.colour.r*e),Uint8(source.colour.g*e),Uint8(source.colour.b*e),255};}colour.a=Uint8(255*sky.lamps*std::clamp(source.shown,0.0f,1.0f));
      if(water>-1e8f){colour.r=Uint8(colour.r*.3f);colour.g=Uint8(colour.g*.3f);colour.b=Uint8(colour.b*.3f);}
      vertices[at++]={{x*scale,y*scale},colour,{0,0}};
    }
    for(int y=0;y<cells;++y)for(int x=0;x<cells;++x) {
      int a=first+y*(cells+1)+x,b=a+1,c=a+cells+1,d=c+1;
      for(int v:{a,b,d,a,d,c})indices[ix++]=v;
    }
  }},4);
  if(sky.lamps>.001f)for(const auto &l:transientLights) {
    constexpr int pulseCells=12;const int first=int(vertices.size());
    for(int y=0;y<=pulseCells;++y)for(int x=0;x<=pulseCells;++x) {
      LightingAssets::Point p{l.x+(float(x)/pulseCells*2-1)*l.radius,l.y+(float(y)/pulseCells*2-1)*l.radius,0};
      p.z=LightingAssets::terrainHeight(map,p.x,p.y);float energy=0;
      const float dx=l.x-p.x,dy=l.y-p.y,dz=(l.z-p.z)*.5f,flat=std::hypot(dx,dy),d2=dx*dx+dy*dy+dz*dz;
      if(flat<l.radius && dz>0 && !blocked({l.x,l.y,l.z},p,l.owner)) {
        const float t=std::clamp((l.radius-flat)/(l.radius*.15f),0.0f,1.0f);
        energy=1-std::exp(-l.energy*dz/std::sqrt(std::max(.01f,d2))/std::max(.16f,d2)*t*t*(3-2*t));
      }
      float sx,sy,depth;view.worldToScreenF(p.x,p.y,p.z,sx,sy,depth);
      vertices.push_back({{sx*scale,sy*scale},{Uint8(l.colour.r*energy),Uint8(l.colour.g*energy),Uint8(l.colour.b*energy),Uint8(255*sky.lamps)},{0,0}});
    }
    for(int y=0;y<pulseCells;++y)for(int x=0;x<pulseCells;++x) {
      int a=first+y*(pulseCells+1)+x,b=a+1,c=a+pulseCells+1,d=c+1;indices.insert(indices.end(),{a,b,d,a,d,c});
    }
  }
  if(!vertices.empty())ZT_RenderGeometry(renderer,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
  timer.mark("ground field");
  drawBeamHaze(renderer,view,scale);
  drawSurfaceLighting(renderer,view,scale,map.getWidth()*.5f);
  timer.mark("surface field");
  SDL_SetRenderTarget(renderer,saved.target);SDL_SetRenderScale(renderer,1,1);
  SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);
  ZT_RenderCopy(renderer,lightmap,nullptr,&rect);
  timer.mark("lighting target resolve");
  counters.width=w;counters.height=h;
}

void DayNightLighting::drawEmission(SDL_Renderer *renderer,WorldRenderer &view,const SDL_Rect &rect) {
  RenderState saved(renderer);SDL_SetRenderScale(renderer,1,1);SDL_SetRenderViewport(renderer,nullptr);
  SDL_SetRenderClipRect(renderer,&rect);const float zoom=view.getCamera().zoom;
  if(emissionClips.size()>320)for(auto it=emissionClips.begin();it!=emissionClips.end();) {
    if(it->second.used+2<actorTick)it=emissionClips.erase(it);else ++it;
  }
  for(const auto &e:emitters) {
    auto dest=e.patch;dest.x=rect.x+dest.x*zoom;dest.y=rect.y+dest.y*zoom;dest.w*=zoom;dest.h*=zoom;
    const int alpha=e.reflective?70:230;
    // addition: a merged building atlas can be partially hidden. Clip only
    // the actual foreground sprite pixels, instead of hiding every window
    // when an unrelated object's centre happens to overlap the atlas.
    std::vector<const Cover*> front;size_t hash=1469598103934665603ull;
    auto mix=[&](size_t v){hash^=v;hash*=1099511628211ull;};
    for(const auto &c:covers)if(c.owner!=e.owner && c.depth>e.depth+.01f &&
      c.l<e.patch.x+e.patch.w && c.r>e.patch.x && c.t<e.patch.y+e.patch.h && c.b>e.patch.y) {
      front.push_back(&c);mix(c.owner);mix(size_t(c.art));mix(c.frame);mix(int(c.side));
      mix(size_t(std::lround((c.x-e.patch.x)*2)));mix(size_t(std::lround((c.y-e.patch.y)*2)));
    }
    if(e.kind>=200 && !front.empty()) {
      auto &clip=emissionClips[{e.owner,e.texture}];clip.used=actorTick;
      const int width=int(e.patch.w),height=int(e.patch.h);
      if(clip.hash!=hash) {
        clip.hash=hash;clip.rectangles.clear();
        for(int y=0;y<height;++y)for(int x=0;x<width;) {
          auto hidden=[&](int xx){float px=e.patch.x+xx+.5f,py=e.patch.y+y+.5f;
            for(const auto *c:front)if(px>=c->l && px<c->r && py>=c->t && py<c->b && c->art->hitTestAnchored(px,py,c->x,c->y,c->side,c->frame))return true;
            return false;};
          if(hidden(x)){++x;continue;}int end=x+1;while(end<width && !hidden(end))++end;
          bool merged=false;
          for(auto it=clip.rectangles.rbegin();it!=clip.rectangles.rend();++it)if(it->y+it->h==y && it->x==x && it->w==end-x){++it->h;merged=true;break;}
          if(!merged)clip.rectangles.push_back({x,y,end-x,1});x=end;
        }
      }
      std::vector<ZT_Vertex> v;std::vector<int> indices;v.reserve(clip.rectangles.size()*4);indices.reserve(clip.rectangles.size()*6);
      const SDL_Color tint{255,255,255,Uint8(alpha*sky.lamps*e.energy)};
      for(const auto &r:clip.rectangles) {
        const float l=dest.x+r.x*zoom,t=dest.y+r.y*zoom,right=l+r.w*zoom,bottom=t+r.h*zoom;
        float u0=float(r.x)/width,u1=float(r.x+r.w)/width;if(e.flip==SDL_FLIP_HORIZONTAL){u0=1-u0;u1=1-u1;}
        const float v0=float(r.y)/height,v1=float(r.y+r.h)/height;const int first=int(v.size());
        v.insert(v.end(),{{{l,t},tint,{u0,v0}},{{right,t},tint,{u1,v0}},{{right,bottom},tint,{u1,v1}},{{l,bottom},tint,{u0,v1}}});
        indices.insert(indices.end(),{first,first+1,first+2,first,first+2,first+3});
      }
      if(!v.empty()){SDL_SetTextureBlendMode(e.texture,SDL_BLENDMODE_BLEND);ZT_RenderGeometry(renderer,e.texture,v.data(),int(v.size()),indices.data(),int(indices.size()));++counters.emissives;}
      continue;
    }
    // addition: its soft light round it (Animation::EmissionPatch::glow),
    // faint - the air lit, not the art washed out
    if(e.glow && !e.reflective) {
      const float pad=e.glowPad*zoom;
      const SDL_FRect around{dest.x-pad,dest.y-pad,dest.w+2*pad,dest.h+2*pad};
      const float strength=glowStrength*(e.glowPad>Animation::kGlowPad?1.6f:1.0f);
      SDL_SetTextureAlphaMod(e.glow,Uint8(std::clamp(strength*sky.lamps*e.energy,0.0f,255.0f)));
      ZT_RenderCopyExF(renderer,e.glow,nullptr,&around,0,nullptr,e.flip);
    }
    if(!e.reflective) {
      SDL_SetTextureBlendMode(e.texture,SDL_BLENDMODE_ADD);SDL_SetTextureAlphaMod(e.texture,Uint8(35*sky.lamps*e.energy));
      for(const auto offset:{SDL_FPoint{-1,0},SDL_FPoint{1,0},SDL_FPoint{0,-1},SDL_FPoint{0,1}}) {
        auto halo=dest;halo.x+=offset.x*zoom;halo.y+=offset.y*zoom;
        ZT_RenderCopyExF(renderer,e.texture,nullptr,&halo,0,nullptr,e.flip);
      }
      SDL_SetTextureBlendMode(e.texture,SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureAlphaMod(e.texture,Uint8(alpha*sky.lamps*e.energy));
    ZT_RenderCopyExF(renderer,e.texture,nullptr,&dest,0,nullptr,e.flip);
    SDL_SetTextureAlphaMod(e.texture,255);++counters.emissives;
  }
  // addition: where a light falls on a tank's water, small glints come and
  // go on its surface - the lighthouse's sweeping beam shimmers across a
  // tank, a lamp beside one sparkles faintly - instead of the water lit flat
  if(frameFences && sky.lamps>.001f && !frameFences->exhibits().empty()) {
    std::vector<ZT_Vertex> v;std::vector<int> idx;const float t=float(fixtureClock);
    for(size_t li=0;li<groundLight.size() && li<lights.size();++li) {
      const auto &l=lights[li];if(l.groundOnly)continue;
      const bool beam=l.cone>0 && l.sweep!=0;
      for(const auto &s:groundLight[li]) {
        const float e=beam?1-std::exp(-s.energy*sourceDirection(l,s.position)):1-std::exp(-s.energy);
        if(e<.04f)continue;
        const float level=waterLevelAt(s.position.x,s.position.y);if(level<-1e8f)continue;
        uint32_t h=uint32_t(int(s.position.x*16))*73856093u^uint32_t(int(s.position.y*16))*19349663u;h^=h>>13;h*=0x5bd1e995u;h^=h>>15;
        const float phase=(h&1023)/1023.0f*6.2831853f,speed=2.2f+((h>>10)&255)/255.0f*2.0f;
        const float twinkle=std::pow(std::max(0.0f,std::sin(t*speed+phase)),6.0f);if(twinkle<.05f)continue;
        const float jx=(((h>>18)&63)/63.0f-.5f)*.35f,jy=(((h>>24)&63)/63.0f-.5f)*.35f;
        float sx,sy,d;view.worldToScreenF(s.position.x+jx,s.position.y+jy,level,sx,sy,d);
        const float px=rect.x+sx*zoom,py=rect.y+sy*zoom,r=std::max(1.0f,1.7f*zoom);
        const float a=std::clamp(255*e*twinkle*sky.lamps*(beam?1.0f:.5f),0.0f,255.0f);if(a<4)continue;
        const SDL_Color c{Uint8(std::min(255,l.colour.r+40)),Uint8(std::min(255,l.colour.g+40)),Uint8(std::min(255,l.colour.b+40)),Uint8(a)};
        const int first=int(v.size());
        v.insert(v.end(),{{{px-r*2.2f,py},c,{0,0}},{{px,py-r*.8f},c,{0,0}},{{px+r*2.2f,py},c,{0,0}},{{px,py+r*.8f},c,{0,0}}});
        idx.insert(idx.end(),{first,first+1,first+2,first,first+2,first+3});
      }
    }
    if(!v.empty()){SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_ADD);ZT_RenderGeometry(renderer,nullptr,v.data(),int(v.size()),idx.data(),int(idx.size()));}
  }
}

std::string DayNightLighting::describeLights()const {
  std::string out=std::to_string(lights.size())+" lights, "+std::to_string(emitters.size())+" glowing patches, lamps at "+std::to_string(int(sky.lamps*100))+"%";
  char line[200];int n=0;
  for(const auto &l:lights){if(++n>40)break;std::snprintf(line,sizeof line,"\n  at %.1f,%.1f height %.1f  reach %.1f  power %.2f  rgb %d,%d,%d%s%s",l.x,l.y,l.z,l.radius,l.energy,l.colour.r,l.colour.g,l.colour.b,l.cone>0?" (beam)":"",l.groundOnly?(l.shown>0?" (ground, lit)":" (ground, dark)"):"");out+=line;}
  return out;
}

bool DayNightLighting::facesOnTheirShapes() const {
  if(projectedOwners.size()!=projectedFaces.size())return false;
  for(size_t i=0;i<projectedFaces.size();++i){const size_t k=projectedFaces[i].instance;if(k>=instances.size() || instances[k].owner!=projectedOwners[i])return false;}
  return true;
}
bool DayNightLighting::groundBoxesTrue(const WorldMap &map) const {
  if(irradianceHash!=sceneHash)return true; // (the lamps' light waits for the lamps, by day: nothing is drawn from its grids or boxes)
  for(size_t li=0;li<groundBoxes.size() && li<groundLight.size();++li) {
    const auto &box=groundBoxes[li];const auto &grid=groundLight[li];
    if(box.data!=grid.data() || box.count!=grid.size() || grid.empty())continue; // (made again when next drawn)
    LightingAssets::Point lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
    for(const auto &s:grid){lo={std::min(lo.x,s.position.x),std::min(lo.y,s.position.y),std::min(lo.z,s.position.z)};
      hi={std::max(hi.x,s.position.x),std::max(hi.y,s.position.y),std::max(hi.z,s.position.z)};}
    hi.z=std::max(hi.z,float(map.getMaxHeight()));
    if(lo.x!=box.lo.x || lo.y!=box.lo.y || lo.z!=box.lo.z || hi.x!=box.hi.x || hi.y!=box.hi.y || hi.z!=box.hi.z)return false;
  }
  return true;
}
float DayNightLighting::waterLevelAt(float x,float y)const {
  if(!frameFences)return -1e9f;
  const int id=frameFences->exhibitAt(int(std::floor(x)),int(std::floor(y)));
  const auto *ex=id>=0?frameFences->exhibit(id):nullptr;
  if(!ex || !ex->tank || ex->water<=.01f)return -1e9f;
  return ex->level>-1e8f?ex->level:ex->floorHeight+ex->water*(ex->wallTop()-ex->floorHeight);
}

void DayNightLighting::finishEmitters() {
  emitters.erase(std::remove_if(emitters.begin(),emitters.end(),[&](const Light &e){
    if(e.kind>=200)return false;
    for(const auto &c:covers)if(c.owner!=e.owner && c.depth>e.depth+.01f && e.sx>=c.l && e.sx<c.r && e.sy>=c.t && e.sy<c.b)
      if(c.art->hitTestAnchored(e.sx,e.sy,c.x,c.y,c.side,c.frame))return true;
    return false;
  }),emitters.end());
  std::stable_sort(emitters.begin(),emitters.end(),[](const auto &a,const auto &b){return a.distance<b.distance;});
  if(emitters.size()>maxEmissives)emitters.resize(maxEmissives);
}
