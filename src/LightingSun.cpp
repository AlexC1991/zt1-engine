#include "LightingSun.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <cstdlib>
#include <atomic>
namespace LightingAssets {
float terrainHeight(const WorldMap &map,float x,float y) {
  const auto *t=map.getTile(int(std::floor(x)),int(std::floor(y)));if(!t)return 0;
  const float fx=x-std::floor(x),fy=y-std::floor(y);
  return (t->cornerHeight[0]*(1-fx)+t->cornerHeight[1]*fx)*(1-fy)+
    (t->cornerHeight[3]*(1-fx)+t->cornerHeight[2]*fx)*fy;
}
void SunDepth::reset(int resolution,float left,float top,float extent,double time,float directionX,float directionY) {
  static std::atomic<uint64_t> serial{0};stamp=++serial; // (maps are also made on a worker: SunJob)
  // ours: paired check-mode measurements can retain the previous 2D shortlist.
  heightCull=!(std::getenv("ZT_LIGHTING_BENCH") && std::getenv("ZT_BENCH_FULL_ACTOR_SAMPLING"));
  fineActors=!(std::getenv("ZT_LIGHTING_BENCH") && std::getenv("ZT_BENCH_COARSE_ACTORS"));
  actorsActive=false;changedStride=(resolution+3)/4;actorChanged.assign(size_t(changedStride)*changedStride,0);
  size=resolution;x0=left;y0=top;span=extent;hour=time;dx=directionX;dy=directionY;pixelScale=size/span;tileScale=64/span;
  depth.assign(size*size,-std::numeric_limits<float>::infinity());owner.assign(size*size,0);caster=0;
}
void SunDepth::beginActors() {
  for(const auto &r:actorRegions)for(int y=r.y;y<r.y+r.h;++y) {
    const size_t offset=size_t(y)*size+r.x;
    std::memcpy(depth.data()+offset,fixed.data()+offset,r.w*sizeof(float));
    std::memcpy(owner.data()+offset,fixedOwner.data()+offset,r.w*sizeof(uint32_t));
  }
  std::fill(actorChanged.begin(),actorChanged.end(),0);actorsActive=true;
  actorRegions.clear();actorTop.fill(-std::numeric_limits<float>::infinity());
}
void SunDepth::actorRegion(Point minimum,Point maximum) {
  float l=1e20f,r=-1e20f,t=1e20f,b=-1e20f;const float scale=size/span;
  for(int k=0;k<8;++k) {
    Point p{(k&1)?maximum.x:minimum.x,(k&2)?maximum.y:minimum.y,(k&4)?maximum.z:minimum.z};
    const float x=(p.x+dx*p.z-x0)*scale,y=(p.y+dy*p.z-y0)*scale;
    l=std::min(l,x);r=std::max(r,x);t=std::min(t,y);b=std::max(b,y);
  }
  const int x=std::max(0,int(std::floor(l))-1),y=std::max(0,int(std::floor(t))-1);
  const int x1=std::min(size,int(std::ceil(r))+2),y1=std::min(size,int(std::ceil(b))+2);
  if(x1>x && y1>y) {
    actorRegions.push_back({x,y,x1-x,y1-y});
    for(int yy=y*64/size;yy<=std::min(63,y1*64/size);++yy)for(int xx=x*64/size;xx<=std::min(63,x1*64/size);++xx)actorTop[yy*64+xx]=std::max(actorTop[yy*64+xx],maximum.z);
  }
}
void SunDepth::triangle(Point a,Point b,Point c) {
  if(!size)return;const float scale=pixelScale;
  auto project=[&](Point p){return Point{(p.x+dx*p.z-x0)*scale,(p.y+dy*p.z-y0)*scale,p.z};};
  a=project(a);b=project(b);c=project(c);
  const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);if(std::abs(area)<1e-6f)return;
  const int l=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),r=std::min(size-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
  const int t=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),bottom=std::min(size-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
  const float dzdx=((b.z-a.z)*(c.y-a.y)-(c.z-a.z)*(b.y-a.y))/area;
  const float dzdy=((b.x-a.x)*(c.z-a.z)-(c.x-a.x)*(b.z-a.z))/area;
  // addition: scanline edge increments preserve the same barycentric coverage
  // while avoiding two divisions and repeated edge products for every pixel.
  const float inverse=1/area,du=-(b.y-a.y)*inverse,dv=(c.y-a.y)*inverse;
  for(int y=t;y<=bottom;++y) {
    const float px=l+.5f-a.x,py=y+.5f-a.y;
    float u=((b.x-a.x)*py-(b.y-a.y)*px)*inverse;
    float v=(px*(c.y-a.y)-py*(c.x-a.x))*inverse;
    float candidate=a.z+dzdx*px+dzdy*py;
    for(int x=l;x<=r;++x,u+=du,v+=dv,candidate+=dzdx) {
      if(u<-.00001f || v<-.00001f || u+v>1.00001f)continue;
      auto &z=depth[y*size+x];if(candidate>z){z=candidate;owner[y*size+x]=caster;
        // addition: only a current actor that wins this actual depth texel
        // can change a cached static shadow. Four-pixel blocks are conservative;
        // PCF queries test every touched block, including border neighbours.
        if(actorsActive)actorChanged[(y>>2)*changedStride+(x>>2)]=1;
      }
    }
  }
}
void terrainCasterQuads(const WorldMap &map,const std::function<void(const std::array<Point,4> &)> &quad) {
  if(map.getMinHeight()==map.getMaxHeight())return;
  for(int y=0;y<map.getHeight();++y)for(int x=0;x<map.getWidth();++x) {
    const auto *t=map.getTile(x,y);if(!t)continue;
    std::array<Point,4> p={Point{float(x),float(y),float(t->cornerHeight[0])},
      Point{float(x+1),float(y),float(t->cornerHeight[1])},Point{float(x+1),float(y+1),float(t->cornerHeight[2])},Point{float(x),float(y+1),float(t->cornerHeight[3])}};
    quad(p);
    for(int edge=0;edge<2;++edge) {
      const auto *n=map.getTile(x+(edge==0),y+(edge==1));if(!n)continue;
      const int a=edge==0?1:3,b=2,na=edge==0?0:0,nb=edge==0?3:1;
      Point lowA=p[a],lowB=p[b];lowA.z=float(n->cornerHeight[na]);lowB.z=float(n->cornerHeight[nb]);
      if(lowA.z!=p[a].z || lowB.z!=p[b].z)quad({p[a],p[b],lowB,lowA});
    }
  }
}
void SunDepth::terrain(const WorldMap &map) {
  // Flat terrain is one plane, including pits below zero. Slopes and cliffs
  // share their source quads with the GPU rasterizer.
  if(map.getMinHeight()==map.getMaxHeight())std::fill(depth.begin(),depth.end(),float(map.getMinHeight()));
  else terrainCasterQuads(map,[this](const auto &p){quad(p);});
}
// addition: a caster cannot shade a receiver above its highest vertex. Keep
// full depth sampling for lower receivers, including aerial actors over roofs.
int SunDepth::actorCell(Point p)const {
  const float u=p.x+dx*p.z-x0,v=p.y+dy*p.z-y0;
  const int x=int(std::floor(u*tileScale)),y=int(std::floor(v*tileScale));
  return x<0 || y<0 || x>=64 || y>=64?-1:y*64+x;
}
void ActorCells::sort(const SunDepth &map,uint64_t pointsKey,size_t count,const std::function<Point(size_t)> &at) {
  if(stamp==map.stamp && points==pointsKey && total==count)return;
  stamp=map.stamp;points=pointsKey;total=count;start.assign(4097,0);
  std::vector<int> cell(count);
  for(size_t i=0;i<count;++i){cell[i]=map.actorCell(at(i));if(cell[i]>=0)++start[cell[i]+1];}
  for(int c=0;c<4096;++c)start[c+1]+=start[c];
  order.resize(start[4096]);std::vector<uint32_t> fill(start.begin(),start.end()-1);
  for(size_t i=0;i<count;++i)if(cell[i]>=0)order[fill[cell[i]]++]=uint32_t(i);
}
void ActorCells::mark(const SunDepth &map,const std::function<Point(size_t)> &at,std::vector<uint8_t> &near)const {
  for(int c=0;c<4096;++c) {
    if(start[c]==start[c+1] || !std::isfinite(map.actorTop[c]))continue;
    for(uint32_t k=start[c];k<start[c+1];++k){const uint32_t i=order[k];if(!near[i] && map.actorNear(at(i)))near[i]=1;}
  }
}
bool SunDepth::actorNear(Point p)const {
  const float u=p.x+dx*p.z-x0,v=p.y+dy*p.z-y0;
  const int cell=actorCell(p);
  if(cell<0 || !std::isfinite(actorTop[cell]) || (heightCull && p.z>=actorTop[cell]+.001f))return false;
  if(!fineActors)return true;
  const int px=int(std::floor(u*pixelScale-.5f)),py=int(std::floor(v*pixelScale-.5f));
  const int x0=std::max(0,px),x1=std::min(size-1,px+1),y0=std::max(0,py),y1=std::min(size-1,py+1);
  if(x0>x1 || y0>y1)return false;
  for(int yy=y0>>2;yy<=y1>>2;++yy)for(int xx=x0>>2;xx<=x1>>2;++xx)
    if(actorChanged[yy*changedStride+xx])return true;
  return false;
}
float SunDepth::shadow(Point p,Point normal,uint32_t receiver,bool fixedOnly)const {
  if(!size || depth.empty())return 0;
  const auto &zValues=fixedOnly?fixed:depth;const auto &owners=fixedOnly?fixedOwner:owner;
  const float scale=pixelScale,x=(p.x+dx*p.z-x0)*scale-.5f,y=(p.y+dy*p.z-y0)*scale-.5f;
  const int ix=int(std::floor(x)),iy=int(std::floor(y));const float fx=x-ix,fy=y-iy;
  // A slope-scaled depth tolerance avoids the receiver shadowing itself at
  // finite map resolution. It is measured in height units, not screen pixels.
  const float bias=.10f+(span/size)*(.35f+std::sqrt(normal.x*normal.x+normal.y*normal.y))*2;
  const float inverse=1/std::max(.08f,bias*.5f);
  auto tap=[&](float sx,float sy) {
    const int tx=int(std::floor(sx)),ty=int(std::floor(sy));const float gx=sx-tx,gy=sy-ty;float amount=0;
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
      const int xx=tx+dx,yy=ty+dy;if(xx<0 || yy<0 || xx>=size || yy>=size)continue;
      if(owners[yy*size+xx]==receiver)continue;
      const float weight=(dx?gx:1-gx)*(dy?gy:1-gy);
      const float gap=zValues[yy*size+xx]-p.z-bias;
      const uint32_t by=owners[yy*size+xx];
      const float stops=opacity && by<opacity->size()?(*opacity)[by]:1.0f;
      amount+=weight*std::clamp(gap*inverse,0.0f,1.0f)*stops;
    }
    return amount;
  };
  (void)ix;(void)iy;(void)fx;(void)fy;
  if(soften<=0)return tap(x,y);
  // addition: the edge spread over a few texels (the sun is a disc, and in
  // the soft light low in the sky more so): five taps, the middle weighted
  const float s=soften;
  return tap(x,y)*.4f+(tap(x-s,y-s)+tap(x+s,y-s)+tap(x-s,y+s)+tap(x+s,y+s))*.15f;
}
}
