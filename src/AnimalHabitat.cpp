#include "Fences.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>
#include <map>

// Marine Mania checks land, ordinary water and tank water separately
// (animalCheckWater 0x438f6c; collectors 0x42fd50 and 0x4a39cc).
// A shore connects a sunken pool to its enclosing land habitat. Raised
// display/show tanks and water below the bank do not provide a haul-out.
bool Fences::shoreAccess(const Edge &edge,const WorldMap &map,int &tank,int &land) const {
  int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
  int a=exhibitAt(ax,ay),b=exhibitAt(edge.x,edge.y);
  const auto *ea=exhibit(a),*eb=exhibit(b);
  if(!ea || !eb || a==b || ea->tank==eb->tank)return false;
  const auto *pool=ea->tank?ea:eb,*bank=ea->tank?eb:ea;
  if(pool->show || !pool->filling || pool->water<0.99f || !bank->named || bank->pocket)return false;
  int bx=ea->tank?edge.x:ax,by=ea->tank?edge.y:ay;
  const auto *tile=map.getTile(bx,by);if(!tile || tile->terrainType==9 || tile->terrainType==10)return false;
  float water=pool->floorHeight+waterLevel(*pool);
  float low=*std::min_element(tile->cornerHeight,tile->cornerHeight+4);
  float high=*std::max_element(tile->cornerHeight,tile->cornerHeight+4);
  if(high-low>1 || std::abs(water-tile->height)>0.25f || pool->wallTop()-water>1.05f)return false;
  tank=pool->id;land=bank->id;return true;
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
