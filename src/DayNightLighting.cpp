#include "DayNightLighting.hpp"
#include "Animation.hpp"
#include "World.hpp"
#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "PlacedObjects.hpp"
#include "ResourceManager.hpp"
#include "WorldMap.hpp"
#include "WorldRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

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
    SDL_RenderGetViewport(r,&viewport); SDL_RenderGetScale(r,&sx,&sy);
    clipped=SDL_RenderIsClipEnabled(r); SDL_RenderGetClipRect(r,&clip);
    SDL_GetRenderDrawBlendMode(r,&blend); SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
  }
  ~RenderState() {
    SDL_SetRenderTarget(r,target); SDL_RenderSetScale(r,sx,sy);
    SDL_RenderSetViewport(r,&viewport); SDL_RenderSetClipRect(r,clipped?&clip:nullptr);
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
  }
  return types[file]=t;
}

void DayNightLighting::prepare(World &world,ResourceManager *rm,const PlacedObjects &objects,const WorldMap &map,
                               WorldRenderer &view,double clock,int width,int height,SDL_Renderer *renderer) {
  counters={};lights.clear();emitters.clear();instances.clear();transientLights.clear();frameRenderer=renderer;frameView=&view;frameMap=&map;fluorescenceCues.clear();fluorescentArt.clear();fixtureClock=previewBeamTime>=0?previewBeamTime:clock;
  hour=previewHour>=0?previewHour:hourAt(clock);sky=skyAt(hour);
  const float zoom=view.getCamera().zoom,w=width/zoom,h=height/zoom;logicalW=w;logicalH=h;
  covers.clear();size_t hash=1469598103934665603ULL;
  auto mix=[&](size_t value){hash^=value;hash*=1099511628211ULL;};
  const auto &all=objects.objects();int coldBuilds=0;const Uint64 coldStart=SDL_GetPerformanceCounter();
  auto mayBuild=[&]{return coldBuilds<2 && (!coldBuilds || 1000.0*(SDL_GetPerformanceCounter()-coldStart)/SDL_GetPerformanceFrequency()<3);};
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
        found=models.emplace(key,LightingAssets::build(*o.art,asset?*asset:plain,t.halfX,t.halfY)).first;
        SDL_Log("[LightingModel] %s: %d solid cells, %zu surface quads",o.typeName.c_str(),found->second.occupied,found->second.faces.size());
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
      if(found==models.end() && mayBuild()){++coldBuilds;LightingAssets::Definition plain{0,"secondary scenery layer"};found=models.emplace(key,LightingAssets::build(*layer,plain,t.halfX,t.halfY,-1,layer==o.underIdle)).first;}
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
        SDL_RendererFlip flip;const auto box=materialArt->emissionRect(patch,sx,sy,side,&flip);
        Light e{o.x,o.y,z,0,0,i,distance};e.sx=sx+(flip==SDL_FLIP_HORIZONTAL?-patch.offset.x:patch.offset.x);e.sy=sy+patch.offset.y;
        e.patch=box;e.texture=patch.texture;e.flip=flip;e.depth=depth;e.kind=profile;e.reflective=patch.reflective;emitters.push_back(e);
      }
    };
    emission(asset->animated?display:o.art,asset->id);
    if(asset->nightFixtures && o.used)emission(o.used,asset->id+3000);
  }
  // The scene hash is independent of the camera: a pan reuses 3D irradiance.
  // Keep source selection stable for normal zoos; the cap bounds extreme maps.
  mix(collectStructures(world));
  // addition: over the shape budget, the ones nearest the screen's middle.
  // (projectModels took them in the zoo's object order: in a big zoo every
  // building after the first ~60 thousand faces had no shadow or shading,
  // however close to the camera.) Under the budget nothing changes.
  {
    size_t total=0;for(const auto &o:instances)total+=o.model->faces.size();
    if(total>size_t(maxModelFaces)) {
      std::vector<size_t> order(instances.size());for(size_t k=0;k<order.size();++k)order[k]=k;
      std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return instances[a].distance<instances[b].distance;});
      std::vector<char> keep(instances.size(),0);int budget=maxModelFaces;
      for(size_t k:order){const int faces=int(instances[k].model->faces.size());if(faces>budget)continue;budget-=faces;keep[k]=1;}
      std::vector<Instance> kept;kept.reserve(instances.size());
      for(size_t k=0;k<instances.size();++k)if(keep[k]){kept.push_back(instances[k]);mix(size_t(instances[k].owner));}
      instances.swap(kept);
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
  const size_t limit=size_t(std::clamp(diagnosticLightLimit,1,512));
  // addition: reserve the bounded projector slots before ordinary fixtures.
  std::stable_partition(lights.begin(),lights.end(),[](const Light &l){return l.cone>0 && l.sweep!=0;});
  int projectors=0;lights.erase(std::remove_if(lights.begin(),lights.end(),[&](const Light &l){return l.cone>0 && l.sweep!=0 && ++projectors>4;}),lights.end());
  if(lights.size()>limit)lights.resize(limit);
  for(auto &l:lights){const float a=float(fixtureClock*l.sweep),c=std::cos(a),s=std::sin(a);l.axis={l.normal.x*c-l.normal.y*s,l.normal.x*s+l.normal.y*c,l.normal.z};const float n=std::sqrt(l.axis.x*l.axis.x+l.axis.y*l.axis.y+l.axis.z*l.axis.z);if(n>.001f){l.axis.x/=n;l.axis.y/=n;l.axis.z/=n;}}
  for(const auto &source:lights){mix(source.colour.r);mix(source.colour.g);mix(source.colour.b);mix(source.owner);mix(std::hash<float>{}(source.x));mix(std::hash<float>{}(source.y));mix(std::hash<float>{}(source.z));for(float f:{source.energy,source.radius,source.normal.x,source.normal.y,source.normal.z,source.cone,source.sweep})mix(std::hash<float>{}(f));}
  mix(lights.size());mix(map.getGeneration());
  if(hash!=sceneHash || groundLight.size()!=lights.size()) {sceneHash=hash;buildIrradiance(map);}
  finishEmitters();
  prepareBeamShapes();
  projectModels(view);counters.modelFaces=int(projectedFaces.size());
  counters.models=int(instances.size());counters.lights=sky.lamps>.001f?int(lights.size()):0;
}

bool DayNightLighting::targets(SDL_Renderer *renderer,int w,int h) {
  if(!SDL_RenderTargetSupported(renderer))return false;
  if(lightmap && (w!=textureW || h!=textureH)) { SDL_DestroyTexture(lightmap);lightmap=nullptr; }
  if(!lightmap) {
    lightmap=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
    if(!lightmap)return false;
    textureW=w;textureH=h;
    SDL_SetTextureBlendMode(lightmap,SDL_BLENDMODE_MOD);
    SDL_SetTextureScaleMode(lightmap,SDL_ScaleModeLinear);
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
  if(SDL_SetRenderTarget(renderer,lightmap)!=0)return;
  SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetScale(renderer,1,1);SDL_RenderSetClipRect(renderer,nullptr);
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
  SDL_RenderGeometry(renderer,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
  timer.mark("ambient field");
  // Cached 3D irradiance projected onto the floor. No planar footprint ray
  // or prepainted radial pool substitutes for object occlusion.
  SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_ADD);
  vertices.clear();indices.clear();
  if(sky.lamps>.001f)for(size_t li=0;li<groundLight.size();++li) {
    const auto &grid=groundLight[li];const auto &source=lights[li];const int cells=source.sweep!=0 && source.cone>0?48:source.cone>0?32:20;
    const int first=int(vertices.size());
    for(const auto &sample:grid) {
      float x,y,d;view.worldToScreenF(sample.position.x,sample.position.y,sample.position.z,x,y,d);
      auto colour=sample.colour;if(source.cone>0 && source.sweep!=0){const float e=1-std::exp(-sample.energy*sourceDirection(source,sample.position));colour={Uint8(source.colour.r*e),Uint8(source.colour.g*e),Uint8(source.colour.b*e),255};}colour.a=Uint8(255*sky.lamps);
      vertices.push_back({{x*scale,y*scale},colour,{0,0}});
    }
    for(int y=0;y<cells;++y)for(int x=0;x<cells;++x) {
      int a=first+y*(cells+1)+x,b=a+1,c=a+cells+1,d=c+1;
      indices.insert(indices.end(),{a,b,d,a,d,c});
    }
  }
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
  if(!vertices.empty())SDL_RenderGeometry(renderer,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
  timer.mark("ground field");
  drawBeamHaze(renderer,view,scale);
  drawSurfaceLighting(renderer,view,scale,map.getWidth()*.5f);
  timer.mark("surface field");
  SDL_SetRenderTarget(renderer,saved.target);SDL_RenderSetScale(renderer,1,1);
  SDL_RenderSetViewport(renderer,nullptr);SDL_RenderSetClipRect(renderer,nullptr);
  SDL_RenderCopy(renderer,lightmap,nullptr,&rect);
  timer.mark("lighting target resolve");
  counters.width=w;counters.height=h;
}

void DayNightLighting::drawEmission(SDL_Renderer *renderer,WorldRenderer &view,const SDL_Rect &rect) {
  RenderState saved(renderer);SDL_RenderSetScale(renderer,1,1);SDL_RenderSetViewport(renderer,nullptr);
  SDL_RenderSetClipRect(renderer,&rect);const float zoom=view.getCamera().zoom;
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
      std::vector<SDL_Vertex> v;std::vector<int> indices;v.reserve(clip.rectangles.size()*4);indices.reserve(clip.rectangles.size()*6);
      const SDL_Color tint{255,255,255,Uint8(alpha*sky.lamps*e.energy)};
      for(const auto &r:clip.rectangles) {
        const float l=dest.x+r.x*zoom,t=dest.y+r.y*zoom,right=l+r.w*zoom,bottom=t+r.h*zoom;
        float u0=float(r.x)/width,u1=float(r.x+r.w)/width;if(e.flip==SDL_FLIP_HORIZONTAL){u0=1-u0;u1=1-u1;}
        const float v0=float(r.y)/height,v1=float(r.y+r.h)/height;const int first=int(v.size());
        v.insert(v.end(),{{{l,t},tint,{u0,v0}},{{right,t},tint,{u1,v0}},{{right,bottom},tint,{u1,v1}},{{l,bottom},tint,{u0,v1}}});
        indices.insert(indices.end(),{first,first+1,first+2,first,first+2,first+3});
      }
      if(!v.empty()){SDL_SetTextureBlendMode(e.texture,SDL_BLENDMODE_BLEND);SDL_RenderGeometry(renderer,e.texture,v.data(),int(v.size()),indices.data(),int(indices.size()));++counters.emissives;}
      continue;
    }
    if(!e.reflective) {
      SDL_SetTextureBlendMode(e.texture,SDL_BLENDMODE_ADD);SDL_SetTextureAlphaMod(e.texture,Uint8(35*sky.lamps*e.energy));
      for(const auto offset:{SDL_FPoint{-1,0},SDL_FPoint{1,0},SDL_FPoint{0,-1},SDL_FPoint{0,1}}) {
        auto halo=dest;halo.x+=offset.x*zoom;halo.y+=offset.y*zoom;
        SDL_RenderCopyExF(renderer,e.texture,nullptr,&halo,0,nullptr,e.flip);
      }
      SDL_SetTextureBlendMode(e.texture,SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureAlphaMod(e.texture,Uint8(alpha*sky.lamps*e.energy));
    SDL_RenderCopyExF(renderer,e.texture,nullptr,&dest,0,nullptr,e.flip);
    SDL_SetTextureAlphaMod(e.texture,255);++counters.emissives;
  }
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
