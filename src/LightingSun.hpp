#ifndef LIGHTING_SUN_HPP
#define LIGHTING_SUN_HPP
#include "LightingAssets.hpp"
#include <vector>
#include <cstddef>
#include <functional>
class WorldMap;
namespace LightingAssets {
// addition: orthographic depth from the sun, in world space. Geometry at
// every height shares this map, so roofs, hills and moving actors can both
// cast and receive shadows. The original visible art remains untouched.
struct SunDepth {
  int size=0;float x0=0,y0=0,span=0,dx=0,dy=0,pixelScale=0,tileScale=0;double hour=-1;uint64_t stamp=0;bool heightCull=true,fineActors=true,actorsActive=false;int changedStride=0;std::vector<unsigned char> actorChanged;
  std::vector<float> fixed,depth;std::vector<uint32_t> fixedOwner,owner;uint32_t caster=0;std::vector<SDL_Rect> actorRegions;std::array<float,4096> actorTop{};
  void reset(int resolution,float left,float top,float extent,double time,float directionX,float directionY);
  void triangle(Point a,Point b,Point c);
  void quad(const std::array<Point,4> &p){triangle(p[0],p[1],p[2]);triangle(p[0],p[2],p[3]);}
  void terrain(const WorldMap &);
  void seal(){fixed=depth;fixedOwner=owner;actorRegions.clear();}
  void beginActors();
  void actorRegion(Point minimum,Point maximum);
  float shadow(Point p,Point normal={0,0,1},uint32_t receiver=0xFFFFFFFFu,bool fixedOnly=false)const;
  const std::vector<float> *opacity=nullptr; // (by caster: how much light it stops; 1 if not listed)
  float soften=0; // addition: map texels the shadow's edge spreads over (more in soft, low light)
  bool actorNear(Point p)const;
  int actorCell(Point p)const; // (actorNear's 64 x 64 cell; -1 off the map)
  size_t bytes()const{return (fixed.capacity()+depth.capacity())*sizeof(float)+(fixedOwner.capacity()+owner.capacity())*sizeof(uint32_t)+actorChanged.capacity();}
};
// ours: points sorted by a sun map's actor cells (SunDepth::actorCell), made
// again with the map or the points; mark asks actorNear only for the points
// in cells an actor reached - actorNear is false in all the others
struct ActorCells {
  uint64_t stamp=0,points=0;size_t total=size_t(-1);std::vector<uint32_t> start,order;
  void sort(const SunDepth &map,uint64_t pointsKey,size_t count,const std::function<Point(size_t)> &at);
  void mark(const SunDepth &map,const std::function<Point(size_t)> &at,std::vector<uint8_t> &near)const;
};
float terrainHeight(const WorldMap &,float x,float y);
void terrainCasterQuads(const WorldMap &,const std::function<void(const std::array<Point,4> &)> &);
}
#endif
