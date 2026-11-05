#include <string_view>
#include <limits>
#include <cstdlib>
#include "DayNightLighting.hpp"
#include "WorldRenderer.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <array>
using LightingAssets::Point;
namespace FrameProfile {extern bool active;void record(const char *,double);double ms(Uint64);}
namespace {struct ProfScope{const char *n;Uint64 t;explicit ProfScope(const char *m):n(m),t(FrameProfile::active?SDL_GetPerformanceCounter():0){}~ProfScope(){if(t)FrameProfile::record(n,FrameProfile::ms(t));}};}
// addition: people, animals, trees and scenery cast their shadow from their
// own sprite, pixel by pixel along the sun's line (Animation::drawCastShadow,
// in drawShadows), in its current pose: their real outline - leaves, legs,
// an arch's opening - where their 3D shapes in the sun's map were rough hulls
// at ten pixels a tile (a guest a pixel or two wide, gone at midday). Only
// buildings, fences and tank walls (boxes, the hull right) keep the map's.
// On the ground or a tank's floor (not swimming, flying or carried).
bool DayNightLighting::spriteShadow(const Instance &o,const WorldMap &map,bool actor)const {
  if(!o.artwork)return false;
  if(!actor)return o.sprite;
  if((o.owner>>55)&1 || !((o.owner>>54)&1))return false;
  return o.position.z<=LightingAssets::terrainHeight(map,o.position.x,o.position.y)+.5f;
}
// (what the jobs are made from: kept, and shared with them, until the scene,
// the ground grid or the map changes - copied for every job, starting one
// stalled the frame as long as the rebuild it saved)
// addition: how far a building's shadow edge spreads in the sun's map: a
// little at midday, more in the softer light of morning and evening
static float shadowSoften(double time){return 1.0f+2.5f*(1-DayNightLighting::skyAt(time).shadow);}

struct DayNightLighting::SunScene {
  size_t scene=0;uint64_t projection=0;
  struct Caster{std::array<Point,4> p;Point outward;uint32_t id;};std::vector<Caster> casters;
  std::vector<std::array<Point,4>> facePoints;std::vector<Point> faceNormals;std::vector<uint32_t> faceIds;std::vector<char> faceVisible;
};
struct DayNightLighting::SunGround {uint64_t generation=0;std::vector<Point> points,normals;};
struct DayNightLighting::SunTerrain {uint32_t generation=0;bool flat=true;float flatHeight=0;std::vector<std::array<Point,4>> quads;};

struct DayNightLighting::SunJob {
  // (what it was made from)
  double time=0;float left=0,top=0,extent=0;
  std::shared_ptr<const SunScene> scene;std::shared_ptr<const SunGround> ground;std::shared_ptr<const SunTerrain> terrain;
  std::vector<float> opacity;
  // (what it made)
  LightingAssets::SunDepth map;std::vector<std::array<float,4>> faceSun;std::vector<float> receiverSun;
  LightingAssets::ActorCells faceCells,receiverCells;
  void run() {
    const auto sun=DayNightLighting::skyAt(time);const float n=std::max(.001f,std::hypot(sun.dx,sun.dy));
    map.reset(std::min(1024,int(extent*8)),left,top,extent,time,sun.dx/n*sun.shadowLength*.5f,sun.dy/n*sun.shadowLength*.5f);
    map.soften=shadowSoften(time);
    // (as SunDepth::terrain and updateSun's own rebuild, in the same order)
    if(terrain->flat)std::fill(map.depth.begin(),map.depth.end(),terrain->flatHeight);
    else for(const auto &q:terrain->quads)map.quad(q);
    for(const auto &c:scene->casters) {
      if(-c.outward.x*map.dx-c.outward.y*map.dy+c.outward.z*.5f<=0)continue;
      map.caster=c.id;map.quad(c.p);
    }
    map.seal();map.opacity=&opacity;
    const auto &faces=scene->facePoints;
    faceSun.assign(faces.size(),{0,0,0,0});
    for(size_t i=0;i<faces.size();++i)if(scene->faceVisible[i])for(int k=0;k<4;++k)faceSun[i][k]=map.shadow(faces[i][k],scene->faceNormals[i],scene->faceIds[i],true);
    receiverSun.resize(ground->points.size());
    for(size_t i=0;i<ground->points.size();++i)receiverSun[i]=map.shadow(ground->points[i],ground->normals[i],0xFFFFFFFFu,true);
    faceCells.sort(map,scene->projection,faces.size()*4,[&](size_t i){return faces[i/4][i%4];});
    receiverCells.sort(map,ground->generation,ground->points.size(),[&](size_t i){return ground->points[i];});
  }
};

void DayNightLighting::startSunJob(const WorldMap &map,double time,float left,float top,float extent) {
  if(!sunScene || sunScene->scene!=sceneHash || sunScene->projection!=projectionSerial) {
    auto made=std::make_shared<SunScene>();made->scene=sceneHash;made->projection=projectionSerial;
    for(const auto &f:projectedFaces) {
      if(f.instance<instances.size() && instances[f.instance].sprite)continue;
      made->casters.push_back({{f.world[0],f.world[1],f.world[2],f.world[3]},f.outward,uint32_t(f.instance+1)});
    }
    made->facePoints.reserve(projectedFaces.size());
    for(const auto &f:projectedFaces) {
      made->facePoints.push_back({f.world[0],f.world[1],f.world[2],f.world[3]});made->faceNormals.push_back(f.normal);
      made->faceIds.push_back(uint32_t(f.instance+1));made->faceVisible.push_back(f.visible);
    }
    sunScene=made;
  }
  if(!sunGround || sunGround->generation!=gpuReceiverGeneration || sunGround->points.size()!=receiverPoints.size()) {
    auto made=std::make_shared<SunGround>();made->generation=gpuReceiverGeneration;
    for(const auto &p:receiverPoints){made->points.push_back(p.p);made->normals.push_back(p.normal);}
    sunGround=made;
  }
  if(!sunTerrain || sunTerrain->generation!=map.getGeneration()) {
    auto made=std::make_shared<SunTerrain>();made->generation=map.getGeneration();
    made->flat=map.getMinHeight()==map.getMaxHeight();made->flatHeight=float(map.getMinHeight());
    if(!made->flat)LightingAssets::terrainCasterQuads(map,[&](const std::array<Point,4> &q){made->quads.push_back(q);});
    sunTerrain=made;
  }
  auto job=std::make_shared<SunJob>();
  job->time=time;job->left=left;job->top=top;job->extent=extent;
  job->scene=sunScene;job->ground=sunGround;job->terrain=sunTerrain;job->opacity=casterOpacity;
  sunJob=std::async(std::launch::async,[job]{job->run();return job;});
}

// (the job's map for map k, if it was made from what is there now)
bool DayNightLighting::takeSunJob(int k,double time,float left,float top,float extent,const WorldMap &map) {
  if(!sunJob.valid())return false;
  std::shared_ptr<SunJob> job;{ProfScope wait("ls job wait");job=sunJob.get();} // (made 0.6 s ago: done; if not, waited for rather than made twice)
  if(!job || std::abs(job->time-time)>.000001 || job->left!=left || job->top!=top || job->extent!=extent ||
     job->scene->scene!=sceneHash || job->scene->projection!=projectionSerial || job->terrain->generation!=map.getGeneration() ||
     job->scene->facePoints.size()!=projectedFaces.size() || job->opacity!=casterOpacity)return false;
  sunMaps[k]=std::move(job->map);sunMaps[k].opacity=&casterOpacity;++counters.sunBuilds;
  projectedSunStamp[k]=sunMaps[k].stamp;
  for(size_t i=0;i<projectedFaces.size();++i)if(projectedFaces[i].visible)for(int n=0;n<4;++n)projectedFaces[i].sun[k][n]=job->faceSun[i][n];
  faceCells[k]=std::move(job->faceCells);
  if(job->ground->generation==gpuReceiverGeneration && job->receiverSun.size()==receiverPoints.size()) {
    for(size_t i=0;i<receiverPoints.size();++i)receiverPoints[i].sun[k]=job->receiverSun[i];
    receiverSunStamp[k]=sunMaps[k].stamp;receiverCells[k]=std::move(job->receiverCells);
  }
  return true;
}

void DayNightLighting::updateSun(const WorldMap &map,const WorldRenderer &view) {
  gpuSunReady=false;
  gpuAtlasReady=false;
  ++receiverFrame;
  if(sky.sun<=.001f)return;
  if(projectedFaces.empty() && actorFaces.empty() && map.getMinHeight()==map.getMaxHeight()){sunMaps[0].size=sunMaps[1].size=0;return;}
  float cx,cy;view.screenToWorldAt(logicalW*.5f,logicalH*.5f,0,cx,cy);
  const float extent=std::max(96.0f,std::ceil((logicalW/64+logicalH/32+64)/32)*32);
  const float left=std::floor(cx/16)*16-extent*.5f,top=std::floor(cy/16)*16-extent*.5f;
  constexpr double interval=.02;const double h0=std::floor(hour/interval)*interval;
  sunBlend=float((hour-h0)/interval);
  const char *mode=SDL_getenv("ZT_LIGHTING_GPU");
  if(mode && std::string_view(mode)=="atlas" && !gpuSurfaceFailed && gpu.available(frameRenderer)) {
    auto add=[](auto &list,const std::array<Point,4> &q,Point n,uint32_t owner){
      const float normal[]={n.x,n.y,n.z};
      for(const auto &indices:{std::array<int,3>{0,1,2},std::array<int,3>{0,2,3}}){
        GpuLighting::CasterTriangle triangle;triangle.owner[0]=owner;
        for(int k=0;k<3;++k){const auto &p=q[indices[k]];triangle.point[k][0]=p.x;triangle.point[k][1]=p.y;triangle.point[k][2]=p.z;triangle.point[k][3]=normal[k];}
        list.push_back(triangle);
      }
    };
    if(gpuCasterScene!=sceneHash || gpuCasterRotation!=view.getRotation() || !gpuCasterGeneration){
      gpuCasterScene=sceneHash;gpuCasterRotation=view.getRotation();++gpuCasterGeneration;gpuStaticCasters.clear();
      LightingAssets::terrainCasterQuads(map,[&](const auto &q){add(gpuStaticCasters,q,{},0);});
      for(const auto &f:projectedFaces)if(f.instance>=instances.size() || !instances[f.instance].sprite)add(gpuStaticCasters,{f.world[0],f.world[1],f.world[2],f.world[3]},f.outward,uint32_t(f.instance+1));
    }
    gpuActorCasters.clear();
    for(const auto &f:actorFaces)add(gpuActorCasters,{f.world[0],f.world[1],f.world[2],f.world[3]},f.outward,uint32_t(f.instance+1));
    LightingAssets::SunDepth parameters[2];
    for(int k=0;k<2;++k){
      auto &atlas=parameters[k];atlas.hour=h0+k*interval;const auto sun=skyAt(atlas.hour);
      const float n=std::max(.001f,std::hypot(sun.dx,sun.dy));
      atlas.size=std::min(1024,int(extent*8));atlas.x0=left;atlas.y0=top;atlas.span=extent;
      atlas.dx=sun.dx/n*sun.shadowLength*.5f;atlas.dy=sun.dy/n*sun.shadowLength*.5f;
    }
    const float base=map.getMinHeight()==map.getMaxHeight()?float(map.getMinHeight()):-std::numeric_limits<float>::infinity();
    gpuAtlasReady=gpu.buildSun(frameRenderer,parameters,sunBlend,sky.shadow,gpuStaticCasters,gpuActorCasters,gpuCasterGeneration,base);
    if(gpuAtlasReady){gpuSunReady=true;return;}
  }
  const bool changed=sunHash!=sceneHash;sunHash=sceneHash;
  if(!changed && std::abs(sunMaps[1].hour-h0)<.000001 && sunMaps[1].x0==left && sunMaps[1].y0==top && sunMaps[1].span==extent){
    std::swap(sunMaps[0],sunMaps[1]);std::swap(projectedSunStamp[0],projectedSunStamp[1]);std::swap(receiverSunStamp[0],receiverSunStamp[1]);
    for(auto &f:projectedFaces)for(int v=0;v<4;++v)std::swap(f.sun[0][v],f.sun[1][v]);
    for(auto &p:receiverPoints)std::swap(p.sun[0],p.sun[1]);
    std::swap(receiverCells[0],receiverCells[1]);std::swap(faceCells[0],faceCells[1]); // ours: (keyed by map: kept)
  }
  // (how much each caster stops: foliage some, everything else all)
  casterOpacity.assign(instances.size()+actors.size()+1,1.0f);
  for(size_t k=0;k<instances.size();++k)casterOpacity[k+1]=instances[k].opacity;
  for(int k=0;k<2;++k) {
    auto &atlas=sunMaps[k];const double time=h0+k*interval;
    atlas.opacity=&casterOpacity;
    if((changed || std::abs(atlas.hour-time)>.000001 || atlas.x0!=left || atlas.y0!=top || atlas.span!=extent) &&
       !(!changed && takeSunJob(k,time,left,top,extent,map))) {
      ProfScope rebuild("ls map rebuild");
      const auto sun=skyAt(time);const float n=std::max(.001f,std::hypot(sun.dx,sun.dy));
      atlas.reset(std::min(1024,int(extent*8)),left,top,extent,time,sun.dx/n*sun.shadowLength*.5f,sun.dy/n*sun.shadowLength*.5f);
      atlas.soften=shadowSoften(time);
      atlas.terrain(map);
      for(const auto &f:projectedFaces) {
        if(-f.outward.x*atlas.dx-f.outward.y*atlas.dy+f.outward.z*.5f<=0)continue;
        if(f.instance<instances.size() && instances[f.instance].sprite)continue; // (cast from its sprite)
        atlas.caster=uint32_t(f.instance+1);atlas.quad({f.world[0],f.world[1],f.world[2],f.world[3]});
      }
      atlas.seal();++counters.sunBuilds;
    }
    if(projectedSunStamp[k]!=atlas.stamp) {
      ProfScope faces("ls face samples");
      projectedSunStamp[k]=atlas.stamp;
      // addition: back-facing surfaces still cast; only drawn faces need receiver samples.
      for(auto &f:projectedFaces)if(f.visible)for(int n=0;n<4;++n)f.sun[k][n]=atlas.shadow(f.world[n],f.normal,uint32_t(f.instance+1),true);
    }
    ProfScope actorsScope("ls actor raster");
    atlas.beginActors();
    for(const auto &o:actors) {
      if(spriteShadow(o,map,true))continue;
      Point lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
      const auto &m=*o.model;
      for(int n=0;n<8;++n){auto p=toWorld(o,{m.minimum.x+((n&1)?m.step.x*m.nx:0),m.minimum.y+((n&2)?m.step.y*m.ny:0),m.minimum.z+((n&4)?m.step.z*m.nz:0)});
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
      atlas.actorRegion(lo,hi);
    }
    for(const auto &f:actorFaces) {
      if(-f.outward.x*atlas.dx-f.outward.y*atlas.dy+f.outward.z*.5f<=0)continue;
      if(f.instance>=instances.size() && spriteShadow(actors[f.instance-instances.size()],map,true))continue;
      atlas.caster=uint32_t(f.instance+1);atlas.quad({f.world[0],f.world[1],f.world[2],f.world[3]});
    }
    counters.memoryBytes+=atlas.bytes();
  }
  // (the map after next, made ahead while the zoo plays)
  if(!sunJob.valid()){ProfScope start("ls job start");startSunJob(map,h0+2*interval,left,top,extent);}
  gpuSunReady=gpu.uploadSun(frameRenderer,sunMaps,sunBlend,sky.shadow);
}
float DayNightLighting::sunShadow(Point p,Point normal,uint32_t receiver)const {
  if(sky.sun<=.001f)return 0;
  return sunMaps[0].shadow(p,normal,receiver)*(1-sunBlend)+sunMaps[1].shadow(p,normal,receiver)*sunBlend;
}
void DayNightLighting::prepareReceivers(const WorldMap &map,const WorldRenderer &view) {
  float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f;
  for(float z:{float(map.getMinHeight()),float(map.getMaxHeight())})for(int k=0;k<4;++k) {
    float x,y;view.screenToWorldAt((k&1)?logicalW:0,(k&2)?logicalH:0,z,x,y);
    x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
  }
  const int l=std::max(0,int(std::floor(x0/8))*8-2),t=std::max(0,int(std::floor(y0/8))*8-2);
  const int r=std::min(map.getWidth(),int(std::ceil(x1/8))*8+2),b=std::min(map.getHeight(),int(std::ceil(y1/8))*8+2);
  if(receiverGeneration==map.getGeneration() && receiverRotation==view.getRotation() && l==receiverX0 && r==receiverX1 && t==receiverY0 && b==receiverY1)return;
  receiverGeneration=map.getGeneration();receiverRotation=view.getRotation();receiverX0=l;receiverX1=r;receiverY0=t;receiverY1=b;receivers.clear();receiverPoints.clear();receiverSampleFrame=size_t(-1);receiverSunStamp[0]=receiverSunStamp[1]=0;
  ++gpuReceiverGeneration;gpuGroundPoints.clear();
  // addition: a flat map is a regular quarter-tile grid. Build each vertex
  // once without hashing or projecting it four times through adjacent quads.
  // Slopes and cliffs retain the general receiver path below.
  if(map.getMinHeight()==map.getMaxHeight()) {
    const int cols=std::max(0,r-l)*4,rows=std::max(0,b-t)*4,stride=cols+1;
    const float z=float(map.getMinHeight());const size_t points=size_t(stride)*(rows+1);
    receiverPoints.reserve(points);receivers.reserve(size_t(cols)*rows);
    std::vector<SDL_FPoint> screen;screen.reserve(points);
    float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
    for(int y=0;y<=rows;++y)for(int x=0;x<=cols;++x) {
      Point p{l+x*.25f,t+y*.25f,z};float sx,sy,d;view.worldToScreenF(p.x,p.y,p.z,sx,sy,d);
      receiverPoints.push_back({p,{0,0,1},0});screen.push_back({sx-ox,sy-oy});
    }
    for(int y=0;y<rows;++y)for(int x=0;x<cols;++x) {
      const int a=y*stride+x;const int at[4]={a,a+1,a+stride+1,a+stride};Receiver f{};f.normal={0,0,1};
      for(int k=0;k<4;++k){f.sample[k]=at[k];f.p[k]=receiverPoints[at[k]].p;f.screen[k]=screen[at[k]];}
      receivers.push_back(f);
    }
    return;
  }
  std::unordered_map<uint64_t,int> samples;
  const size_t pointBudget=size_t(std::max(0,r-l)*4+1)*size_t(std::max(0,b-t)*4+1);
  samples.reserve(pointBudget);receiverPoints.reserve(pointBudget);receivers.reserve(size_t(std::max(0,r-l))*std::max(0,b-t)*16);
  float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
  auto add=[&](std::array<Point,4> points,Point normal) {
    Receiver f{};f.p=points;f.normal=normal;
    for(int k=0;k<4;++k){float sx,sy,d;view.worldToScreenF(points[k].x,points[k].y,points[k].z,sx,sy,d);f.screen[k]={sx-ox,sy-oy};}
    for(int k=0;k<4;++k) {
      const auto p=points[k];
      const uint64_t key=uint64_t(uint16_t(std::lround(p.x*4))) | uint64_t(uint16_t(std::lround(p.y*4)))<<16 | uint64_t(uint32_t(std::lround(p.z*16)))<<32;
      auto [it,added]=samples.emplace(key,int(receiverPoints.size()));if(added)receiverPoints.push_back({p,normal,0});f.sample[k]=it->second;
    }
    receivers.push_back(f);
  };
  for(int y=t;y<b;++y)for(int x=l;x<r;++x) {
    const auto *tile=map.getTile(x,y);if(!tile)continue;
    auto z=[&](float u,float v){return (tile->cornerHeight[0]*(1-u)+tile->cornerHeight[1]*u)*(1-v)+(tile->cornerHeight[3]*(1-u)+tile->cornerHeight[2]*u)*v;};
    const float dx=(tile->cornerHeight[1]+tile->cornerHeight[2]-tile->cornerHeight[0]-tile->cornerHeight[3])*.25f;
    const float dy=(tile->cornerHeight[2]+tile->cornerHeight[3]-tile->cornerHeight[0]-tile->cornerHeight[1])*.25f;
    const float len=std::sqrt(1+dx*dx+dy*dy);const Point normal{-dx/len,-dy/len,1/len};
    constexpr int divisions=4;
    for(int j=0;j<divisions;++j)for(int i=0;i<divisions;++i) {
      const float u=i/float(divisions),v=j/float(divisions),q=1.0f/divisions;
      add({Point{x+u,y+v,z(u,v)},Point{x+u+q,y+v,z(u+q,v)},Point{x+u+q,y+v+q,z(u+q,v+q)},Point{x+u,y+v+q,z(u,v+q)}},normal);
    }
    // The original terrain draws vertical cliff faces. They receive the
    // same world-space sun depth as the ground above and below them.
    for(int e=0;e<2;++e) {
      const auto *n=map.getTile(x+(e==0),y+(e==1));if(!n)continue;
      const float hi0=tile->cornerHeight[e==0?1:3],hi1=tile->cornerHeight[2],lo0=n->cornerHeight[0],lo1=n->cornerHeight[e==0?3:1];
      if(hi0==lo0 && hi1==lo1)continue;
      const float sign=(hi0+hi1>=lo0+lo1)?1.0f:-1.0f;
      const float vx[4]={-1,-1,1,1},vy[4]={1,-1,-1,1};if((e==0?vx[view.getRotation()]:vy[view.getRotation()])*sign<=0)continue;
      for(int i=0;i<4;++i)for(int j=0;j<4;++j) {
        const float a=i*.25f,b=(i+1)*.25f,c=j*.25f,d=(j+1)*.25f;
        auto p=[&](float along,float down){return Point{e==0?float(x+1):x+along,e==0?y+along:float(y+1),
          (hi0+(hi1-hi0)*along)*(1-down)+(lo0+(lo1-lo0)*along)*down};};
        add({p(a,c),p(b,c),p(b,d),p(a,d)},e==0?Point{sign,0,0}:Point{0,sign,0});
      }
    }
  }
}
void DayNightLighting::drawShadows(SDL_Renderer *renderer,const WorldRenderer &view,const WorldMap &map,const std::function<bool(float,float)> &accept) {
  if(sky.sun<=.001f || (!gpuAtlasReady && !sunMaps[0].size))return;prepareReceivers(map,view);
  // addition: vertices retain the original quarter-tile sampling locations.
  // Only tank membership and draw submission remain here; the GPU evaluates
  // the four-tap shadow filter and quantizes each vertex's alpha.
  if(gpuSunReady && std::string_view(SDL_getenv("ZT_LIGHTING_GPU"))!="surface") {
    if(gpuGroundPoints.empty()) {
      gpuGroundPoints.reserve(receiverPoints.size());
      float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
      for(const auto &p:receiverPoints){float x,y,d;view.worldToScreenF(p.p.x,p.p.y,p.p.z,x,y,d);
        gpuGroundPoints.push_back({p.p.x,p.p.y,p.p.z,p.normal.x,p.normal.y,p.normal.z,x-ox,y-oy});}
    }
    gpuGroundIndices.clear();gpuGroundIndices.reserve(receivers.size()*6);
    for(const auto &f:receivers){const float cx=(f.p[0].x+f.p[2].x)*.5f,cy=(f.p[0].y+f.p[2].y)*.5f;
      if(accept && !accept(cx,cy))continue;
      for(int k:{0,1,2,0,2,3})gpuGroundIndices.push_back(uint32_t(f.sample[k]));}
    float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
    if(gpu.ground(renderer,gpuGroundPoints,gpuGroundIndices,gpuReceiverGeneration,ox,oy)){
      counters.shadows=int(gpuGroundIndices.size()/6);return;
    }
  }
  if(receiverSampleFrame!=receiverFrame) {
    receiverSampleFrame=receiverFrame;
    for(int k=0;k<2;++k)if(receiverSunStamp[k]!=sunMaps[k].stamp) {
      ProfScope ground("ls ground samples");
      receiverSunStamp[k]=sunMaps[k].stamp;
      for(auto &p:receiverPoints)p.sun[k]=sunMaps[k].shadow(p.p,p.normal,0xFFFFFFFFu,true);
    }
    // (an actor's shadow near it: asked in the cells an actor reached only)
    const auto at=[this](size_t i){return receiverPoints[i].p;};
    receiverNear.assign(receiverPoints.size(),0);
    for(int k=0;k<2;++k){receiverCells[k].sort(sunMaps[k],gpuReceiverGeneration,receiverPoints.size(),at);receiverCells[k].mark(sunMaps[k],at,receiverNear);}
    for(size_t i=0;i<receiverPoints.size();++i) {
      auto &p=receiverPoints[i];
      const bool affected=receiverNear[i]!=0;
      p.amount=affected?sunShadow(p.p,p.normal):p.sun[0]*(1-sunBlend)+p.sun[1]*sunBlend;
    }
    // ours: each point's shade and the receivers on the screen with any,
    // once a frame for the map's pass and every tank's
    const float strength=100*sky.shadow;
    shadowAlpha.resize(receiverPoints.size());
    for(size_t i=0;i<receiverPoints.size();++i)shadowAlpha[i]=Uint8(strength*receiverPoints[i].amount);
    float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
    shadowShown.clear();
    for(size_t j=0;j<receivers.size();++j) {
      const auto &f=receivers[j];
      float l=f.screen[0].x+ox,r=l,t=f.screen[0].y+oy,b=t;
      for(auto p:f.screen){l=std::min(l,p.x+ox);r=std::max(r,p.x+ox);t=std::min(t,p.y+oy);b=std::max(b,p.y+oy);}
      if(r<0 || l>logicalW || b<0 || t>logicalH)continue;
      int total=0;for(int k=0;k<4;++k)total+=shadowAlpha[f.sample[k]];
      if(total)shadowShown.push_back(uint32_t(j));
    }
  }
  float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);
  auto &vertices=shadowVertices;auto &indices=shadowIndices;vertices.clear();indices.clear();vertices.reserve(8192);indices.reserve(12288);
  for(uint32_t j:shadowShown) {
    const auto &f=receivers[j];
    const float cx=(f.p[0].x+f.p[2].x)*.5f,cy=(f.p[0].y+f.p[2].y)*.5f;
    if(accept && !accept(cx,cy))continue;
    const int first=int(vertices.size());
    for(int k=0;k<4;++k)vertices.push_back({{f.screen[k].x+ox,f.screen[k].y+oy},{0,0,0,shadowAlpha[f.sample[k]]},{0,0}});
    indices.insert(indices.end(),{first,first+1,first+2,first,first+2,first+3});
  }
  SDL_BlendMode was;SDL_GetRenderDrawBlendMode(renderer,&was);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
  if(!vertices.empty())SDL_RenderGeometry(renderer,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
  SDL_SetRenderDrawBlendMode(renderer,was);
  // addition: the shadows cast from sprites (spriteShadow), over the ground's.
  // (where the foot already stands in a building's or tree's shadow, less of
  // it: a shadow inside a shadow is no darker)
  if(sky.shadow>.001f && sunMaps[0].size) {
    const float sdx=sunMaps[0].dx*(1-sunBlend)+sunMaps[1].dx*sunBlend,sdy=sunMaps[0].dy*(1-sunBlend)+sunMaps[1].dy*sunBlend;
    const float unit=view.getHeightUnitPixels();
    // (across the sun's line on the ground; the art's pixels to tiles as a
    // tile's width across the screen)
    float ax=sdx,ay=sdy,an=std::hypot(ax,ay);
    if(an<1e-4f){ax=sky.dx;ay=sky.dy;an=std::max(1e-4f,std::hypot(ax,ay));}
    float acrossX=-ay/an,acrossY=ax/an;const float tilePx=view.getTileWidth()/1.41421356f;
    // (the art's left to right is the camera's: laid across the sun's line
    // the same way round, whatever the view's turn - one fixed way round on
    // the ground, a turned camera saw an animal's shadow facing backwards.
    // Where the camera's left-right runs along the sun's line it turns over:
    // narrower there, so it doesn't flip at full width)
    float acrossScale=1;
    {
      float x0,y0,x1,y1,x2,y2,d;
      view.worldToScreenF(0,0,0,x0,y0,d);view.worldToScreenF(1,0,0,x1,y1,d);view.worldToScreenF(0,1,0,x2,y2,d);
      const float gx=x1-x0,gy=x2-x0,gn=std::max(1e-4f,std::hypot(gx,gy));
      const float c=(acrossX*gx+acrossY*gy)/gn; // (cosine: the across line to the screen's right)
      if(c<0){acrossX=-acrossX;acrossY=-acrossY;}
      acrossScale=std::min(1.0f,.4f+std::fabs(c));
    }
    auto cast=[&](const Instance &o) {
      if(accept && !accept(o.position.x,o.position.y))return;
      float fx,fy,d;
      view.worldToScreenF(o.position.x,o.position.y,o.position.z,fx,fy,d);
      if(fx< -400 || fx>logicalW+400 || fy< -400 || fy>logicalH+500)return;
      const float under=sunMaps[0].shadow(o.position,{0,0,1},0xFFFFFFFFu,true)*(1-sunBlend)+sunMaps[1].shadow(o.position,{0,0,1},0xFFFFFFFFu,true)*sunBlend;
      const Uint8 alpha=Uint8(std::clamp(100*sky.shadow*o.opacity*(1-under),0.0f,255.0f));
      if(!alpha)return;
      // (nothing flat casts one: flowers, grass, litter barely above the ground)
      auto top=modelTop.find(o.model);
      if(top==modelTop.end()) {
        float z=0;for(const auto &f:o.model->faces)for(const auto &v:f.p)z=std::max(z,v.z);
        top=modelTop.emplace(o.model,z).first;
      }
      if(top->second<.4f)return;
      // (the figure turned to face the sun, as its shadow sees it: its width
      // across the sun's line on the ground, each pixel's height along it -
      // cast as it faces the camera, a sun from the side saw it edge on, a line)
      // (soft: three copies a little apart in angle - the sun is a disc - that
      // meet at the foot and part towards the tip, wider in low, soft light;
      // fading to the tip)
      constexpr int copies=3;
      const float spread=.035f+.10f*(1-sky.shadow);
      const float each=1-std::pow(1-alpha/255.0f,1.0f/copies);
      const Uint8 near=Uint8(std::clamp(each*255,0.0f,255.0f)),far=Uint8(near*.45f);
      for(int c=0;c<copies;++c) {
        const float turn=(c-(copies-1)*.5f)*spread,cs=std::cos(turn),sn=std::sin(turn);
        const float rx=sdx*cs-sdy*sn,ry=sdx*sn+sdy*cs;
        o.artwork->drawCastShadow(renderer,view.screenSide(o.sine,-o.cosine),o.frame,[&](float across,float up) {
          const float wide=acrossScale*across/tilePx;
          const float wx=o.position.x+acrossX*wide+rx*up/unit,wy=o.position.y+acrossY*wide+ry*up/unit;
          float x,y,depth;view.worldToScreenF(wx,wy,o.position.z,x,y,depth);
          return SDL_FPoint{x,y};
        },near,far);
      }
    };
    for(const auto &o:instances)if(spriteShadow(o,map,false))cast(o);
    for(const auto &o:actors)if(spriteShadow(o,map,true))cast(o);
  }
  counters.shadows=int(indices.size()/6);counters.memoryBytes+=receivers.capacity()*sizeof(Receiver)+receiverPoints.capacity()*sizeof(ReceiverPoint);
}
