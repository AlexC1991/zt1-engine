#include "Fences.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>
#include <map>

// Marine Mania checks land, ordinary water and tank water separately
// (animalCheckWater 0x438f6c; collectors 0x42fd50 and 0x4a39cc).
// A shore connects a pool to the land beside it. measured: in the running
// original (2026-11-21, Play original, a walrus in a pool inside a fenced
// land, the tank adjustment's wall buttons, habitat +0x184 and the joined
// sets at +0x8 read live) the two habitats are joined exactly while the
// tank's wall height is its initialSink 4 - the wall's top level with the
// ground - and not at 3, 5 or 6, whatever the water level is (it joined at
// 2 and 3 of 3). Raised display/show tanks do not provide a haul-out.
// zoo.exe 0x414728's tank branch is the same rule: the pool's wall height (+0x184)
// plus its first tile's height against the bank tile's corner heights on the
// shared edge, equal to step between them.
bool Fences::shoreAccess(const Edge &edge,const WorldMap &map,int &tank,int &land) const {
  int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
  int a=exhibitAt(ax,ay),b=exhibitAt(edge.x,edge.y);
  const auto *ea=exhibit(a),*eb=exhibit(b);
  if(!ea || !eb || a==b || ea->tank==eb->tank)return false;
  const auto *pool=ea->tank?ea:eb,*bank=ea->tank?eb:ea;
  if(pool->show || !bank->named || bank->pocket)return false;
  int bx=ea->tank?edge.x:ax,by=ea->tank?edge.y:ay;
  const auto *tile=map.getTile(bx,by);if(!tile || tile->terrainType==9 || tile->terrainType==10)return false;
  float low=*std::min_element(tile->cornerHeight,tile->cornerHeight+4);
  float high=*std::max_element(tile->cornerHeight,tile->cornerHeight+4);
  if(high-low>1 || std::abs(pool->wallTop()-tile->height)>0.25f)return false;
  tank=pool->id;land=bank->id;return true;
}
std::vector<int> Fences::joinedLands(int id,const WorldMap &map) const {
  std::set<int> lands;
  const auto *ex=exhibit(id);if(!ex || !ex->tank)return {};
  for(const auto &[edge,p]:pieces()) {
    if(p.tank!=id && p.tank2!=id)continue;
    int pool=-1,land=-1;
    if(shoreAccess(edge,map,pool,land) && pool==id)lands.insert(land);
  }
  return std::vector<int>(lands.begin(),lands.end());
}
int Fences::habitatRoot(int id,const WorldMap &map) const {
  const auto *ex=exhibit(id);if(!ex || !ex->tank)return id;
  std::map<int,int> banks;
  for(const auto &[edge,p]:pieces()) {
    if(p.tank!=id && p.tank2!=id)continue;
    int pool=-1,land=-1;
    if(shoreAccess(edge,map,pool,land) && pool==id)++banks[land];
  }
  int root=id,count=0;
  for(auto [land,n]:banks)if(n>count){root=land;count=n;}
  return root;
}
std::set<int> Fences::habitatGroup(int id,const WorldMap &map) const {
  int root=habitatRoot(id,map);
  std::set<int> group{root};
  for(const auto &ex:exhibits())if(ex.tank && habitatRoot(ex.id,map)==root)group.insert(ex.id);
  return group;
}
