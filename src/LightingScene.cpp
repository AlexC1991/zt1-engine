#include <string_view>
#include "DayNightLighting.hpp"
#include "WorldRenderer.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>

namespace LightingDiagnostics {bool omitReceivers=false,fullReceiverClip=false;}
namespace FrameProfile {extern bool active;void record(const char *,double);double ms(Uint64);}
namespace {
struct LightTimer {
 Uint64 start=0;
 LightTimer(){if(FrameProfile::active)start=SDL_GetPerformanceCounter();}
 void mark(const char *name){if(start){FrameProfile::record(name,FrameProfile::ms(start));start=SDL_GetPerformanceCounter();}}
};
}

using LightingAssets::Point;
Point DayNightLighting::toWorld(const Instance &o,Point p)const {
  return {o.position.x+p.x*o.cosine-p.y*o.sine,o.position.y+p.x*o.sine+p.y*o.cosine,o.position.z+p.z};
}
namespace {
uint64_t lightCell(int x,int y){return uint64_t(uint32_t(x))<<32 | uint32_t(y);}
int cellAt(float x){return int(std::floor(x*.25f));}
}
// addition: index the same complete solids used by the original prototype.
// This only narrows candidates; no shape, ray rule or light radius changes.
void DayNightLighting::indexLocalLights() {
  lightOccluders.clear();lightSources.clear();
  for(size_t i=0;i<instances.size();++i) {
    const auto &o=instances[i];const auto &m=*o.model;
    float l=1e20f,t=l,r=-l,b=-l;
    for(int k=0;k<4;++k) {
      auto p=toWorld(o,{m.minimum.x+((k&1)?m.step.x*m.nx:0),m.minimum.y+((k&2)?m.step.y*m.ny:0),0});
      l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);
    }
    for(int y=cellAt(t);y<=cellAt(b);++y)for(int x=cellAt(l);x<=cellAt(r);++x)lightOccluders[lightCell(x,y)].push_back(i);
  }
  for(size_t i=0;i<lights.size();++i) {
    const auto &l=lights[i];
    for(int y=cellAt(l.y-l.radius);y<=cellAt(l.y+l.radius);++y)
      for(int x=cellAt(l.x-l.radius);x<=cellAt(l.x+l.radius);++x)lightSources[lightCell(x,y)].push_back(i);
  }
}
bool DayNightLighting::blocked(Point from,Point to,uint64_t owner,uint64_t receiver)const {
  const int l=cellAt(std::min(from.x,to.x)),r=cellAt(std::max(from.x,to.x));
  const int t=cellAt(std::min(from.y,to.y)),b=cellAt(std::max(from.y,to.y));
  for(int y=t;y<=b;++y)for(int x=l;x<=r;++x) {
    const auto found=lightOccluders.find(lightCell(x,y));if(found==lightOccluders.end())continue;
    for(size_t i:found->second) {
      const auto &o=instances[i];if(o.owner==owner || o.owner==receiver)continue;
      auto local=[&](Point p){float x=p.x-o.position.x,y=p.y-o.position.y;return Point{x*o.cosine+y*o.sine,-x*o.sine+y*o.cosine,p.z-o.position.z};};
      if(o.model->ray(local(from),local(to)))return true;
    }
  }
  return false;
}
// addition: compare the spatial shortlist with the exhaustive voxel ray path.
// Fixtures include rotated objects and rays crossing spatial cell boundaries.
bool DayNightLighting::verifyLocalLightIndex()const {
  if(lights.empty())return false;
  for(const auto &l:lights)for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
    Point from{l.x,l.y,l.z},to{l.x+x*l.radius*.5f,l.y+y*l.radius*.5f,0};bool full=false;
    for(const auto &o:instances) {
      if(o.owner==l.owner)continue;
      auto local=[&](Point p){float x=p.x-o.position.x,y=p.y-o.position.y;return Point{x*o.cosine+y*o.sine,-x*o.sine+y*o.cosine,p.z-o.position.z};};
      if(o.model->ray(local(from),local(to))){full=true;break;}
    }
    if(blocked(from,to,l.owner)!=full)return false;
    const auto candidates=lightSources.find(lightCell(cellAt(to.x),cellAt(to.y)));
    for(size_t i=0;i<lights.size();++i)if(std::hypot(lights[i].x-to.x,lights[i].y-to.y)<lights[i].radius)
      if(candidates==lightSources.end() || std::find(candidates->second.begin(),candidates->second.end(),i)==candidates->second.end())return false;
  }
  return true;
}
std::array<float,3> DayNightLighting::irradiance(Point p,Point normal,uint64_t receiver,bool transientOnly,bool includeProjectors)const {
  std::array<float,3> colour{};const auto near=lightSources.find(lightCell(cellAt(p.x),cellAt(p.y)));
  auto accumulate=[&](const Light &l){if(l.groundOnly || (!includeProjectors && l.cone>0 && l.sweep!=0))return;const float energy=sourceEnergy(l,p,normal,receiver);
    colour[0]+=energy*l.colour.r/255;colour[1]+=energy*l.colour.g/255;colour[2]+=energy*l.colour.b/255;};
  if(!transientOnly && near!=lightSources.end())for(size_t index:near->second)accumulate(lights[index]);
  for(const auto &l:transientLights)accumulate(l);for(auto &v:colour)v=1-std::exp(-v);return colour;
}
void DayNightLighting::buildIrradiance(const WorldMap &map) {
  // addition: static 3D occlusion and inverse-square irradiance are cached in
  // world space. Camera panning only projects these samples; it casts no rays.
  indexLocalLights();groundLight.clear();surfaceLight.clear();beamLights.clear();
  for(size_t i=0;i<lights.size();++i)if(lights[i].cone>0 && lights[i].sweep!=0)beamLights.push_back(i);
  beamSurface.assign(beamLights.size(),std::vector<std::vector<BeamSamples>>(instances.size()));
  // addition: invalidate by each light's actual neighbourhood. A nearby
  // placement rebuilds affected rays, while distant lights and surfaces reuse
  // byte-identical samples. Source geometry, pose, orientation and terrain
  // receiver heights all participate; camera position does not.
  ++irradianceTick;std::vector<uint64_t> sourceStamps;sourceStamps.reserve(lights.size());
  auto hashInstance=[](uint64_t &stamp,const Instance &o){
    auto mix=[&](uint64_t n){stamp^=n;stamp*=1099511628211ull;};
    mix(size_t(o.model));mix(o.model->faces.size());mix(o.model->occupied);
    for(float n:{o.position.x,o.position.y,o.position.z,o.cosine,o.sine})mix(std::hash<float>{}(n));
  };
  for(const auto &l:lights) {
    uint64_t stamp=1469598103934665603ull;auto mix=[&](uint64_t n){stamp^=n;stamp*=1099511628211ull;};
    for(float n:{l.x,l.y,l.z,l.radius,l.energy,l.normal.x,l.normal.y,l.normal.z,l.cone,l.sweep})mix(std::hash<float>{}(n));
    mix(l.colour.r);mix(l.colour.g);mix(l.colour.b);mix(l.groundOnly);
    std::vector<size_t> nearby;
    for(int y=cellAt(l.y-l.radius);y<=cellAt(l.y+l.radius);++y)for(int x=cellAt(l.x-l.radius);x<=cellAt(l.x+l.radius);++x){
      const auto found=lightOccluders.find(lightCell(x,y));if(found!=lightOccluders.end())nearby.insert(nearby.end(),found->second.begin(),found->second.end());
    }
    std::sort(nearby.begin(),nearby.end());nearby.erase(std::unique(nearby.begin(),nearby.end()),nearby.end());
    for(size_t i:nearby)if(instances[i].owner!=l.owner)hashInstance(stamp,instances[i]);
    sourceStamps.push_back(stamp);
  }
  for(size_t li=0;li<lights.size();++li) {
    const auto &l=lights[li];const int cells=l.sweep!=0 && l.cone>0?48:l.cone>0?32:20;
    std::vector<Irradiance> grid;grid.reserve((cells+1)*(cells+1));
    uint64_t stamp=sourceStamps[li];
    auto mix=[&](uint64_t n){stamp^=n;stamp*=1099511628211ull;};
    for(int y=0;y<=cells;++y)for(int x=0;x<=cells;++x) {
      const float wx=l.x+(float(x)/cells*2-1)*l.radius,wy=l.y+(float(y)/cells*2-1)*l.radius;
      mix(std::hash<float>{}(LightingAssets::terrainHeight(map,wx,wy)));
    }
    auto cached=groundIrradianceCache.find(stamp);
    if(cached!=groundIrradianceCache.end()){cached->second.used=irradianceTick;groundLight.push_back(cached->second.samples);continue;}
    for(int y=0;y<=cells;++y)for(int x=0;x<=cells;++x) {
      Point p{l.x+(float(x)/cells*2-1)*l.radius,l.y+(float(y)/cells*2-1)*l.radius,0};
      if(const auto *tile=map.getTile(int(std::floor(p.x)),int(std::floor(p.y))))p.z=LightingAssets::terrainHeight(map,p.x,p.y);
      if(std::hypot(l.x-p.x,l.y-p.y)<l.radius && l.z>p.z)++counters.rays;
      const float raw=sourceEnergy(l,p,{0,0,1},uint64_t(-1),l.cone<=0 || l.sweep==0),energy=1-std::exp(-raw);
      grid.push_back({p,{Uint8(l.colour.r*energy),Uint8(l.colour.g*energy),Uint8(l.colour.b*energy),255},raw});
    }
    groundIrradianceCache[stamp]={grid,irradianceTick};groundLight.push_back(std::move(grid));
  }
  surfaceLight.resize(instances.size());int budget=maxModelFaces;
  for(size_t i=0;i<instances.size();++i) {
    const auto &o=instances[i];if(int(o.model->faces.size())>budget)continue;
    budget-=int(o.model->faces.size());auto &cache=surfaceLight[i];cache.reserve(o.model->faces.size());
    uint64_t stamp=1469598103934665603ull;hashInstance(stamp,o);stamp^=o.owner;stamp*=1099511628211ull;
    const auto &m=*o.model;
    const float bound=std::hypot(std::max(std::abs(m.minimum.x),std::abs(m.minimum.x+m.step.x*m.nx)),std::max(std::abs(m.minimum.y),std::abs(m.minimum.y+m.step.y*m.ny)));
    for(size_t k=0;k<lights.size();++k)if(std::hypot(lights[k].x-o.position.x,lights[k].y-o.position.y)<=lights[k].radius+bound){stamp^=sourceStamps[k];stamp*=1099511628211ull;}
    // addition: beam arrays are indexed in source order. Include all beam
    // identities so adding/removing a distant projector cannot relabel data.
    for(size_t b:beamLights){stamp^=sourceStamps[b];stamp*=1099511628211ull;}
    auto found=surfaceIrradianceCache.find(stamp);
    if(found!=surfaceIrradianceCache.end()){found->second.used=irradianceTick;cache=found->second.samples;
      if(found->second.beams.size()==beamLights.size())for(size_t b=0;b<beamLights.size();++b)beamSurface[b][i]=found->second.beams[b];continue;}
    std::vector<std::vector<BeamSamples>> beamCache(beamLights.size());
    for(const auto &face:o.model->faces) {
      Point n{face.normal.x*o.cosine-face.normal.y*o.sine,face.normal.x*o.sine+face.normal.y*o.cosine,face.normal.z};
      // addition: sample receiver corners once, not a single flat value for
      // the entire greedy rectangle. The GPU interpolates the moving beam.
      // Steady fixtures retain one obstructed sample per rectangle. Only
      // projectors need separate corner rays as their aperture moves.
      Point centre{};for(auto p:face.p){centre.x+=p.x*.25f;centre.y+=p.y*.25f;centre.z+=p.z*.25f;}
      const auto values=irradiance(toWorld(o,centre),n,o.owner,false,false);std::vector<BeamSamples> energy(beamLights.size());
      for(int k=0;k<4;++k){const auto p=toWorld(o,face.p[k]);
        for(size_t b=0;b<beamLights.size();++b)energy[b][k]=sourceEnergy(lights[beamLights[b]],p,n,o.owner,false);}
      cache.push_back(values);for(size_t b=0;b<beamLights.size();++b)beamCache[b].push_back(energy[b]);
    }
    for(size_t b=0;b<beamLights.size();++b)beamSurface[b][i]=beamCache[b];
    surfaceIrradianceCache[stamp]={cache,irradianceTick,std::move(beamCache)};
  }
  auto prune=[&](auto &cache,size_t limit){if(cache.size()>limit)for(auto it=cache.begin();it!=cache.end();) {
    if(it->second.used!=irradianceTick)it=cache.erase(it);else ++it;
  }};
  prune(groundIrradianceCache,256);prune(surfaceIrradianceCache,1024);
}

// addition: cache camera-independent geometry in each of the four views.
// Panning changes one translation; it never transforms or sorts the 3D mesh.
void DayNightLighting::projectModels(const WorldRenderer &view) {
  if(projectionHash==sceneHash && projectionRotation==view.getRotation() && projectionUnit==view.getHeightUnitPixels())return;
  projectionHash=sceneHash;projectionRotation=view.getRotation();projectionUnit=view.getHeightUnitPixels();projectedFaces.clear();projectedSunStamp[0]=projectedSunStamp[1]=0;++projectionSerial;
  float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
  int budget=maxModelFaces;projectedFaces.reserve(budget);
  for(size_t i=0;i<instances.size();++i) {
    const auto &o=instances[i];if(int(o.model->faces.size())>budget)continue;budget-=int(o.model->faces.size());
    for(size_t j=0;j<o.model->faces.size();++j) {
      const auto &f=o.model->faces[j];ProjectedFace d{};d.instance=i;d.localIndex=j;
      auto rotate=[&](Point p){return Point{p.x*o.cosine-p.y*o.sine,p.x*o.sine+p.y*o.cosine,p.z};};
      d.normal=rotate(f.normal);d.outward=rotate(f.outward);
      if(i<surfaceLight.size() && j<surfaceLight[i].size())d.local=surfaceLight[i][j];
      for(int k=0;k<4;++k) {
        auto p=toWorld(o,f.p[k]);float sx,sy,depth;view.worldToScreenF(p.x,p.y,p.z,sx,sy,depth);
        d.p[k]={sx-ox,sy-oy};d.world[k]=p;d.terrain[k]=terrainClearance(p,view);d.height[k]=f.p[k].z;d.depth+=.25f*(depth+p.z*.5f);
      }
      float l=d.p[0].x,t=d.p[0].y,r=l,b=t;
      for(auto p:d.p){l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);}
      d.bounds={l,t,r-l,b-t};
      const float gx[]={-1,-1,1,1},gy[]={1,-1,-1,1};
      // (a fence or tank wall casts but is not shaded itself: its own shadow
      // drawn over its art turned the fence black - LightingStructures)
      // (at night, with no sun shadow to fall on itself, it is lit as any
      // wall: a lamp or the lighthouse's beam on a fence, a solid tank wall)
      d.visible=(o.shaded || (o.nightReceiver && sky.shadow<.02f)) && d.outward.x*gx[view.getRotation()]+d.outward.y*gy[view.getRotation()]+d.outward.z*2>0;
      projectedFaces.push_back(d);
    }
  }
  std::stable_sort(projectedFaces.begin(),projectedFaces.end(),[](const auto &a,const auto &b){return a.depth<b.depth;});
}

void DayNightLighting::drawSurfaceLighting(SDL_Renderer *renderer,WorldRenderer &view,float scale,float mapMiddle) {
  if(LightingDiagnostics::omitReceivers)return; // check-only paired image diagnosis
  LightTimer timer;
  const float gradients[][2]={{-1,1},{-1,-1},{1,-1},{1,1}};
  const auto *g=gradients[view.getRotation()];
  float dirLength=std::hypot(sky.dx,sky.dy),altitude=sky.sun*1.13f;
  const Point sun{dirLength>0?-sky.dx/dirLength*std::cos(altitude):0,dirLength>0?-sky.dy/dirLength*std::cos(altitude):0,std::sin(altitude)};
  const float direct=.35f*std::clamp(sky.sun/.06f,0.0f,1.0f),reference=.65f+direct*sun.z;
  std::vector<SDL_Color> ambient;ambient.reserve(instances.size());
  for(const auto *group:{&instances,&actors})for(const auto &o:*group)ambient.push_back(skyAt(hour+(o.position.x-mapMiddle)*.008).ambient);
  float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
  const float left=-ox,right=textureW/scale-ox,top=-oy,bottom=textureH/scale-oy;
  auto &vertices=surfaceVertices;auto &indices=surfaceIndices;vertices.clear();indices.clear();
  vertices.reserve((projectedFaces.size()+actorFaces.size())*4);indices.reserve((projectedFaces.size()+actorFaces.size())*6);
  // Native coverage and terrain visibility constrain every receiver. A
  // lighting hull is not visible artwork and must not draw its own outline.
  std::vector<ReceiverMask> masks;masks.reserve(instances.size()+actors.size());
  for(const auto *group:{&instances,&actors})for(const auto &o:*group)masks.push_back(receiverMask(o,view));
  // (a building face's corners an actor's shadow may reach: asked in the
  // cells an actor reached only)
  if(sky.sun>.001f) {
    const auto at=[this](size_t i){return projectedFaces[i/4].world[i%4];};
    faceNear.assign(projectedFaces.size()*4,0);
    for(int k=0;k<2;++k){faceCells[k].sort(sunMaps[k],projectionSerial,projectedFaces.size()*4,at);faceCells[k].mark(sunMaps[k],at,faceNear);}
  }
  const bool useGpu=gpu.available(renderer) && std::string_view(SDL_getenv("ZT_LIGHTING_GPU"))!="ground" && !gpuSurfaceFailed && (sky.sun<=.001f || gpuSunReady);
  if(useGpu){gpuSurfaceFaces.clear();gpuSurfaceBatches.clear();gpuSurfaceFaces.reserve(projectedFaces.size()+actorFaces.size());}
  SDL_Texture *batch=nullptr;
  auto flush=[&]{if(!vertices.empty())SDL_RenderGeometry(renderer,batch,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));vertices.clear();indices.clear();};
  struct ClipVertex {SDL_Vertex v;float terrain;};std::vector<ClipVertex> polygon,clipped;polygon.reserve(12);clipped.reserve(12);
  auto clip=[&](auto distance){if(polygon.empty())return;clipped.clear();auto a=polygon.back();float da=distance(a);
    for(auto b:polygon){float db=distance(b);if((da>=0)!=(db>=0)){float t=da/(da-db);ClipVertex c{};
      c.terrain=a.terrain+(b.terrain-a.terrain)*t;c.v.position={a.v.position.x+(b.v.position.x-a.v.position.x)*t,a.v.position.y+(b.v.position.y-a.v.position.y)*t};
      c.v.tex_coord={a.v.tex_coord.x+(b.v.tex_coord.x-a.v.tex_coord.x)*t,a.v.tex_coord.y+(b.v.tex_coord.y-a.v.tex_coord.y)*t};
      c.v.color={Uint8(a.v.color.r+(b.v.color.r-a.v.color.r)*t),Uint8(a.v.color.g+(b.v.color.g-a.v.color.g)*t),Uint8(a.v.color.b+(b.v.color.b-a.v.color.b)*t),255};clipped.push_back(c);}
      if(db>=0)clipped.push_back(b);a=b;da=db;}polygon.swap(clipped);};
  auto drawFace=[&](const ProjectedFace &f) {
    if(!f.visible)return;
    if(f.bounds.x+f.bounds.w<left || f.bounds.x>right || f.bounds.y+f.bounds.h<top || f.bounds.y>bottom)return;
    const auto &mask=masks[f.instance];if(!mask.texture)return;
    if(f.terrain[0]<0 && f.terrain[1]<0 && f.terrain[2]<0 && f.terrain[3]<0)return;
    const bool terrainClip=f.terrain[0]<0 || f.terrain[1]<0 || f.terrain[2]<0 || f.terrain[3]<0;
    const auto b=mask.bounds;const bool boundsClip=mask.required && (f.bounds.x+ox<b.x || f.bounds.y+oy<b.y || f.bounds.x+ox+f.bounds.w>b.x+b.w || f.bounds.y+oy+f.bounds.h>b.y+b.h);
    const bool needsClip=terrainClip || boundsClip || LightingDiagnostics::fullReceiverClip;
    if(!needsClip && batch!=mask.texture){flush();batch=mask.texture;}const int directFirst=int(vertices.size());
    polygon.clear();
    const auto a=ambient[f.instance];auto local=f.local;
    if(f.instance<instances.size() && !transientLights.empty()) {
      Point centre{};for(auto p:f.world){centre.x+=p.x*.25f;centre.y+=p.y*.25f;centre.z+=p.z*.25f;}
      const auto pulse=irradiance(centre,f.normal,instances[f.instance].owner,true);
      for(int c=0;c<3;++c)local[c]=1-(1-local[c])*(1-pulse[c]);
    }
    if(useGpu){
      GpuLighting::SurfaceFace face{};face.normal[0]=f.normal.x;face.normal[1]=f.normal.y;face.normal[2]=f.normal.z;
      face.ambient[0]=a.r;face.ambient[1]=a.g;face.ambient[2]=a.b;
      face.bounds[0]=mask.bounds.x;face.bounds[1]=mask.bounds.y;face.bounds[2]=mask.bounds.w;face.bounds[3]=mask.bounds.h;
      face.uv[0]=mask.uv.x;face.uv[1]=mask.uv.y;face.uv[2]=mask.uv.w;face.uv[3]=mask.uv.h;
      face.flags[0]=uint32_t(f.instance+1);face.flags[1]=mask.required;face.flags[2]=mask.mirrored;
      face.flags[3]=uint32_t(terrainClip || LightingDiagnostics::fullReceiverClip) | (uint32_t(boundsClip || (mask.required && LightingDiagnostics::fullReceiverClip))<<1);
      for(int k=0;k<4;++k){auto illumination=local;
        if(sky.lamps>.001f && !beamLights.empty() && f.instance<instances.size())illumination=projectedIrradiance(f.instance,f.localIndex,k,f.world[k],illumination);
        face.world[k][0]=f.world[k].x;face.world[k][1]=f.world[k].y;face.world[k][2]=f.world[k].z;
        face.screen[k][0]=f.p[k].x;face.screen[k][1]=f.p[k].y;face.screen[k][2]=f.terrain[k];
        for(int c=0;c<3;++c)face.illumination[k][c]=illumination[c];
        // Keep the native mask's reference corner UV rounding at pixel edges.
        float u=0,v=0;
        if(mask.required){u=(f.p[k].x+ox-mask.bounds.x)/mask.bounds.w;v=(f.p[k].y+oy-mask.bounds.y)/mask.bounds.h;if(mask.mirrored)u=1-u;}
        face.world[k][3]=mask.uv.x+u*mask.uv.w;
        face.illumination[k][3]=mask.uv.y+v*mask.uv.h;
      }
      if(gpuSurfaceBatches.empty() || gpuSurfaceBatches.back().mask!=mask.texture)gpuSurfaceBatches.push_back({mask.texture,int(gpuSurfaceFaces.size()),0});
      ++gpuSurfaceBatches.back().count;gpuSurfaceFaces.push_back(face);return;
    }
    const float ratio=(.65f+direct*std::max(0.0f,f.normal.x*sun.x+f.normal.y*sun.y+f.normal.z*sun.z))/reference;
    for(int k=0;k<4;++k) {
      auto illumination=local;
      if(sky.lamps>.001f && !beamLights.empty() && f.instance<instances.size())illumination=projectedIrradiance(f.instance,f.localIndex,k,f.world[k],illumination);
      float occlusion=0;
      if(sky.sun>.001f) {
        const bool moving=f.instance>=instances.size();
        const bool affected=moving || faceNear[size_t(&f-projectedFaces.data())*4+k];
        occlusion=affected?sunShadow(f.world[k],f.normal,uint32_t(f.instance+1)):f.sun[0][k]*(1-sunBlend)+f.sun[1][k]*sunBlend;
      }
      const float visibility=1-.39f*sky.shadow*occlusion;
      SDL_Color c{Uint8(std::clamp(a.r*ratio*visibility+255*illumination[0]*sky.lamps,0.0f,255.0f)),
        Uint8(std::clamp(a.g*ratio*visibility+255*illumination[1]*sky.lamps,0.0f,255.0f)),
        Uint8(std::clamp(a.b*ratio*visibility+255*illumination[2]*sky.lamps,0.0f,255.0f)),255};
      const float x=f.p[k].x+ox,y=f.p[k].y+oy;SDL_FPoint uv{mask.uv.x,mask.uv.y};
      if(mask.required){float u=(x-mask.bounds.x)/mask.bounds.w,v=(y-mask.bounds.y)/mask.bounds.h;if(mask.mirrored)u=1-u;uv={mask.uv.x+u*mask.uv.w,mask.uv.y+v*mask.uv.h};}
      SDL_Vertex vertex{{x*scale,y*scale},c,uv};if(needsClip)polygon.push_back({vertex,f.terrain[k]});else vertices.push_back(vertex);
    }
    if(!needsClip){indices.insert(indices.end(),{directFirst,directFirst+1,directFirst+2,directFirst,directFirst+2,directFirst+3});return;}
    if(terrainClip || LightingDiagnostics::fullReceiverClip)clip([](const auto &v){return v.terrain;});
    if(boundsClip || (mask.required && LightingDiagnostics::fullReceiverClip)){clip([&](const auto &v){return v.v.position.x-b.x*scale;});clip([&](const auto &v){return (b.x+b.w)*scale-v.v.position.x;});clip([&](const auto &v){return v.v.position.y-b.y*scale;});clip([&](const auto &v){return (b.y+b.h)*scale-v.v.position.y;});}
    if(polygon.size()<3)return;if(batch!=mask.texture){flush();batch=mask.texture;}
    const int first=int(vertices.size());for(const auto &v:polygon)vertices.push_back(v.v);
    for(int k=1;k+1<int(polygon.size());++k)indices.insert(indices.end(),{first,first+k,first+k+1});
  };
  size_t i=0,j=0;
  while(i<projectedFaces.size() || j<actorFaces.size()) {
    if(j==actorFaces.size() || (i<projectedFaces.size() && projectedFaces[i].depth<actorFaces[j].depth))drawFace(projectedFaces[i++]);
    else drawFace(actorFaces[j++]);
  }
  timer.mark("surface project");
  if(useGpu && !gpu.surfaces(renderer,gpuSurfaceFaces,gpuSurfaceBatches,scale,ox,oy,sun,direct,reference,sky.lamps,std::clamp(sky.sun/.06f,0.0f,1.0f))){
    gpuSurfaceFailed=true;
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"[GpuLighting] surface fallback: mask bind failed");
    if(gpuAtlasReady && frameMap)updateSun(*frameMap,view);
    drawSurfaceLighting(renderer,view,scale,mapMiddle);return;
  }
  flush();
  timer.mark("surface geometry queue");
}
