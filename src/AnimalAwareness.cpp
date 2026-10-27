#include "Animals.hpp"
#include "WorldMap.hpp"
#include "WorldIndex.hpp"
#include "Pathfinder.hpp"
#include "ZooItems.hpp"
#include "LabTrace.hpp"
#include "Animation.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

// Engine behaviour: body clearance and local avoidance. Footprints come from
// the species data; this steering policy is not a claimed zoo.exe formula.
float Animals::bodyRadius(const Member &m) const {
  const Type &t = typeList[m.type];
  float r = std::max(0.22f, std::max(t.footX, t.footY) * 0.25f);
  return r * (m.egg ? 0.3f : m.baby ? 0.6f : 1.0f);
}

// Reserve a circle enclosing the rotated footprint, so a turn cannot swing
// its head or tail through glass. Centre-only tile tests cannot guarantee this.
float Animals::tankClearance(const Member &m,float x,float y,const Fences &fences,bool planning) const {
  int tank=tankAt?tankAt(x,y):-1;
  if(tank<0)return 1000;
  const auto &type=typeList[m.type];
  float scale=m.egg?0.3f:m.baby?0.6f:1.0f;
  float radius=std::max(0.25f,std::hypot(static_cast<float>(type.footX),
                   static_cast<float>(type.footY))*0.25f*scale)+0.12f;
  float clearance=1000;
  for(const auto &[e,p]:fences.pieces()) {
    if(p.tank!=tank && p.tank2!=tank)continue;
    // Only nearby wall segments can intersect this body. In a large zoo,
    // avoid portal/shore queries and square roots for distant segments.
    if(e.alongX) {
      if(std::abs(y-e.y)>radius+0.5f || x<e.x-radius || x>e.x+1+radius)continue;
    } else if(std::abs(x-e.x)>radius+0.5f || y<e.y-radius || y>e.y+1+radius)continue;
    bool portal=false;
    // The enclosing turning circle is wider than the animal aligned through
    // a portal. Reserve its length beside the opening; awareStep still permits
    // the centre to cross only the actual, fully opened portal edge.
    const int portalMargin=std::max(1,static_cast<int>(std::ceil(radius)));
    if(m.showTank>=0)for(int offset=-portalMargin;offset<=portalMargin && !portal;++offset) {
      auto centre=e;if(e.alongX)centre.x+=offset;else centre.y+=offset;
      int show=-1,home=-1;
      if(fences.showPortal(centre,show,home) && show==m.showTank && home==m.showHome) {
        const auto *gate=fences.at(centre);
        const auto *kind=fences.portalType(centre);
        portal=planning || (gate && kind && gate->open>=kind->portal->frameCount()-1);
      }
    }
    if(portal)continue;
    if(type.underwater && !type.onlyUnderwater && type.swims && mapRef) {
      int pool=-1,land=-1;
      if(fences.shoreAccess(e,*mapRef,pool,land))continue;
    }
    float nx=e.alongX?std::clamp(x,static_cast<float>(e.x),e.x+1.0f):static_cast<float>(e.x);
    float ny=e.alongX?static_cast<float>(e.y):std::clamp(y,static_cast<float>(e.y),e.y+1.0f);
    clearance=std::min(clearance,std::hypot(x-nx,y-ny)-radius);
  }
  return clearance;
}

bool Animals::awareOf(const Member &a, const Member &b) const {
  if (a.id == b.id || b.id == carried || b.boxed || b.inside || b.maulFor > 0 ||
      b.caughtBy >= 0 || b.type < 0) return false;
  // Chasing/catching is intentional contact, unlike ordinary locomotion.
  if (a.victim == b.id || b.victim == a.id) return false;
  // Escaping animals still avoid bodies while their exhibit assignment changes.
  return a.escaped || b.escaped || a.exhibit == b.exhibit ||
         (a.showTank >= 0 && a.showTank == b.showTank);
}

float Animals::bodyHeight(const Member &m, float x, float y, float swim) const {
  return (mapRef ? groundAt(*mapRef, x, y) : 0.0f) +
         (tankAt && tankAt(x,y) >= 0 ? swim : 0.0f);
}

float Animals::bodyDistance(const Member &a, float ax, float ay, float az,
                           const Member &b, float bx, float by, float bz) const {
  float r = bodyRadius(a) + bodyRadius(b) + 0.08f;
  float vertical = std::max(0.65f, r * 0.6f);
  float dx = (ax-bx)/r, dy = (ay-by)/r, dz = (az-bz)/vertical;
  return std::sqrt(dx*dx + dy*dy + dz*dz);
}

bool Animals::destinationFree(const Member &m, float x, float y, float swim,
                              bool reservations) const {
  float z = bodyHeight(m,x,y,swim);
  for (const auto &other : list) {
    if (!awareOf(m,other)) continue;
    if (bodyDistance(m,x,y,z,other,other.x,other.y,
                     bodyHeight(other,other.x,other.y,other.swimZ)) < 1.0f) return false;
    if (reservations && other.pathAt < other.path.size()) {
      auto [gx,gy] = other.path.back();
      if (bodyDistance(m,x,y,z,other,gx,gy,
                       bodyHeight(other,gx,gy,other.swimTo)) < 1.0f) return false;
    }
  }
  return true;
}

bool Animals::awareStep(const Member &m, int x, int y, int nx, int ny,
                        const WorldMap &map, const Fences &fences) const {
  Fences::Edge edge = nx != x ? Fences::Edge{false,std::max(x,nx),y}
                             : Fences::Edge{true,x,std::max(y,ny)};
  if (m.crossing.x>=0 && edge.x==m.crossing.x && edge.y==m.crossing.y &&
      edge.alongX==m.crossing.alongX) return true;
  const auto &animal=typeList[m.type];
  int pool=-1,land=-1;
  if(animal.underwater && !animal.onlyUnderwater && animal.swims &&
     fences.shoreAccess(edge,map,pool,land))return true;
  if (m.showTank >= 0) {
    int show=-1, home=-1;
    if (fences.showPortal(edge,show,home) && show==m.showTank && home==m.showHome) {
      const auto *p=fences.at(edge);
      const auto *type=fences.portalType(edge);
      return p && type && p->open >= std::max(0,type->portal->frameCount()-1);
    }
  }
  return canStep(x,y,nx,ny,map,fences);
}

void Animals::consumeFood(Member &m) {
  const auto *food=items?items->item(m.foodItem):nullptr;
  if(!food || food->exhibit!=m.exhibit ||
     std::hypot(m.x-food->x,m.y-food->y)>bodyRadius(m)+1.1f)return;
  const auto &t=typeList[m.type];
  float got=items->eat(m.foodItem,static_cast<float>(t.foodEaten));
  if(got<=0){m.foodItem=-1;return;}
  float was=m.hunger;
  m.mealTaken=true;m.noFoodSeen=false;
  m.hunger=std::max(0.0f,m.hunger-got*t.foodUnitValue);
  m.food=std::clamp(100.0f-m.hunger,0.0f,100.0f);m.lastAte=now;
  ZT_STEP("","Eats a mouthful",{"hunger",was,m.hunger},{"food eaten",got});
}

bool Animals::feedingPlace(Member &m, float x, float y,
                           const WorldMap &map, const Fences &fences) {
  const Type &t=typeList[m.type];
  float reach=bodyRadius(m)+0.25f;
  // Keep an already reserved feeding place through a marine eating/dive step.
  float d=std::hypot(m.x-x,m.y-y);
  if (d >= 0.2f && d <= reach+0.85f &&
      destinationFree(m,m.x,m.y,m.swimTo,true) && walkTo(m,m.x,m.y,map,fences))
    return true;
  struct Spot { float x,y,cost; };
  std::vector<Spot> spots;
  float start=std::atan2(m.y-y,m.x-x);
  for (int ring=0;ring<2;++ring) for (int i=0;i<16;++i) {
    float angle=start+i*6.2831853f/16, radius=reach+ring*0.8f;
    float gx=x+std::cos(angle)*radius, gy=y+std::sin(angle)*radius;
    if (!standable(static_cast<int>(std::floor(gx)),static_cast<int>(std::floor(gy)),
                   t,m.exhibit,map,fences) ||
        !destinationFree(m,gx,gy,m.swimTo,true)) continue;
    spots.push_back({gx,gy,std::hypot(gx-m.x,gy-m.y)+ring*0.25f});
  }
  std::stable_sort(spots.begin(),spots.end(),[](const Spot &a,const Spot &b){return a.cost<b.cost;});
  for (const auto &p:spots) if (walkTo(m,p.x,p.y,map,fences)) return true;
  return false;
}

bool Animals::moveAware(Member &m, float tx, float ty, float seconds,
                        const WorldMap &map, const Fences &fences) {
  float dx=tx-m.x, dy=ty-m.y, distance=std::hypot(dx,dy);
  auto replan=[&] {
    auto goal=m.path.back();
    bool feeding=std::any_of(m.stack.begin(),m.stack.end(),[](const auto &s) {
      return s.first=="beat" || s.first=="bfindkeeperfood" || s.first=="bfindotherfood";
    });
    if(feeding && items)if(const auto *food=items->item(m.foodItem)) {
      feedingPlace(m,food->x,food->y,map,fences);return;
    }
    walkTo(m,goal.first,goal.second,map,fences);
  };
  if (distance < 0.0001f) {
    float swim=m.swimZ+std::clamp(m.swimTo-m.swimZ,-seconds*2.0f,seconds*2.0f);
    float z=bodyHeight(m,m.x,m.y,swim), beforeZ=bodyHeight(m,m.x,m.y,m.swimZ);
    for(const auto &other:list) if(awareOf(m,other)) {
      float oz=bodyHeight(other,other.x,other.y,other.swimZ);
      float before=bodyDistance(m,m.x,m.y,beforeZ,other,other.x,other.y,oz);
      float after=bodyDistance(m,m.x,m.y,z,other,other.x,other.y,oz);
      float nearestZ=std::clamp(oz,std::min(z,beforeZ),std::max(z,beforeZ));
      float closest=bodyDistance(m,m.x,m.y,nearestZ,other,other.x,other.y,oz);
      if(before<1.0f ? after<=before+0.00001f : closest<1.0f)return false;
    }
    m.swimZ=swim;
    return std::abs(m.swimZ-m.swimTo)<0.0001f;
  }
  float step=std::min(distance,std::max(0.0f,m.speed*seconds));
  if (step<=0) return false;
  const Type &t=typeList[m.type];
  int sx=static_cast<int>(std::floor(m.x)), sy=static_cast<int>(std::floor(m.y));
  auto pass=[&](int x,int y) {
    int ex=fences.exhibitAt(x,y);
    if (x==sx && y==sy) return true;
    if (m.crossing.x>=0) {
      int outX=m.crossingFromX+(m.crossing.alongX?0:(m.crossingFromX<m.crossing.x?1:-1));
      int outY=m.crossingFromY+(m.crossing.alongX?(m.crossingFromY<m.crossing.y?1:-1):0);
      if(x==outX && y==outY)return standable(x,y,t,-1,map,fences);
    }
    if (m.showTank>=0 && (ex==m.showTank || ex==m.showHome))
      return standable(x,y,t,ex,map,fences);
    return standable(x,y,t,m.exhibit,map,fences);
  };
  auto edge=[&](int x,int y,int nx,int ny){return awareStep(m,x,y,nx,ny,map,fences);};
  std::vector<const Member*> nearby;
  if (index) {
    float radius=8.0f;
    for(const auto &type:typeList)
      radius=std::max(radius,bodyRadius(m)+std::max(type.footX,type.footY)*0.25f+1.0f);
    for (const auto &p:index->near(m.x,m.y,step+radius))
      if (p.kind==WorldIndex::Kind::Animal)
        if (const Member *other=member(p.id);other && awareOf(m,*other)) nearby.push_back(other);
  } else {
    for (const auto &other:list) if (awareOf(m,other)) nearby.push_back(&other);
  }
  int fromTank=tankAt?tankAt(m.x,m.y):-1;
  bool leavingShore=false;
  if(t.underwater && !t.onlyUnderwater && fromTank>=0 && !m.waterMode) {
    auto goal=m.path.back();
    leavingShore=(!tankAt || tankAt(goal.first,goal.second)<0) &&
                 fences.habitatRoot(fromTank,map)!=fromTank;
    if(leavingShore)if(const auto *pool=fences.exhibit(fromTank)) {
      m.swimTo=static_cast<float>(fences.waterLevel(*pool));
      float rise=m.swimZ+std::clamp(m.swimTo-m.swimZ,-seconds*2.0f,seconds*2.0f);
      if(destinationFree(m,m.x,m.y,rise,false))m.swimZ=rise;
    }
  }
  float startZ=bodyHeight(m,m.x,m.y,m.swimZ);
  const float oldWall=tankClearance(m,m.x,m.y,fences);
  float best=-std::numeric_limits<float>::infinity(), bx=m.x, by=m.y, bs=m.swimZ;
  // Both animals choose their own right in a head-on encounter.
  const float turns[]={0,-0.44f,0.44f,-0.87f,0.87f,-1.31f,1.31f,-1.92f,1.92f,3.14159265f};
  for (int verticalChoice=0;verticalChoice<2;++verticalChoice) for (float turn:turns) {
    float vx=(dx*std::cos(turn)-dy*std::sin(turn))/distance;
    float vy=(dx*std::sin(turn)+dy*std::cos(turn))/distance;
    float nx=m.x+vx*step, ny=m.y+vy*step;
    float swim=verticalChoice ? m.swimZ : m.swimZ+(m.swimTo-m.swimZ)*std::min(step/distance,1.0f);
    int toTank=tankAt?tankAt(nx,ny):-1;
    if(leavingShore && toTank<0 && std::abs(m.swimZ-m.swimTo)>0.01f)continue;
    if(fromTank<0 && toTank>=0 && t.underwater && !t.onlyUnderwater)
      if(const auto *pool=fences.exhibit(toTank))swim=static_cast<float>(fences.waterLevel(*pool));
    float nz=bodyHeight(m,nx,ny,swim);
    if (!Pathfinder::clearLine(m.x,m.y,nx,ny,pass,edge)) continue;
    float wall=tankClearance(m,nx,ny,fences);
    if(wall<-0.001f && !(oldWall<0 && wall>oldWall+0.00001f))continue;
    bool safe=true;
    float proximity=0,recovery=0;
    for (const auto *other:nearby) {
      float oz=bodyHeight(*other,other->x,other->y,other->swimZ);
      float r=bodyRadius(m)+bodyRadius(*other)+0.08f, v=std::max(0.65f,r*0.6f);
      float ax=(m.x-other->x)/r, ay=(m.y-other->y)/r, az=(startZ-oz)/v;
      float ux=(nx-m.x)/r, uy=(ny-m.y)/r, uz=(nz-startZ)/v;
      float len=ux*ux+uy*uy+uz*uz;
      float along=len>0 ? std::clamp(-(ax*ux+ay*uy+az*uz)/len,0.0f,1.0f) : 0;
      float closest=std::sqrt(std::pow(ax+ux*along,2)+std::pow(ay+uy*along,2)+std::pow(az+uz*along,2));
      float before=std::sqrt(ax*ax+ay*ay+az*az);
      float after=bodyDistance(m,nx,ny,nz,*other,other->x,other->y,oz);
      if (before<1.0f) {
        // Old saves/placements may already overlap: allow escape, never deepen it.
        if (after<=before+0.00001f || closest<before-0.0001f) {safe=false;break;}
        recovery+=(after-before)*r/std::max(step,0.001f);
      } else if (closest<1.0f) {safe=false;break;}
      float px=other->x,py=other->y;
      if (other->pathAt<other->path.size()) {
        auto [gx,gy]=other->path[other->pathAt];
        float dd=std::hypot(gx-px,gy-py), horizon=std::min(dd,other->speed*0.65f);
        if(dd>0.001f){px+=(gx-px)/dd*horizon;py+=(gy-py)/dd*horizon;}
      }
      // Stop prediction at this waypoint. Projecting beyond a destination
      // invents a collision the animal will never make, so it can circle or
      // alternate avoidance directions instead of finishing its move.
      float horizon=std::min(std::hypot(tx-nx,ty-ny),m.speed*0.65f);
      float future=bodyDistance(m,nx+vx*horizon,ny+vy*horizon,nz,*other,px,py,oz);
      proximity+=std::max(0.0f,1.3f-future);
    }
    if (!safe) continue;
    float progress=(distance-std::hypot(tx-nx,ty-ny))/step;
    // Retain a safe steering direction when either side is nearly tied.
    // Without this continuity, tiny position changes flip left/right on
    // successive frames and rapidly swap the swimmer's directional sprites.
    // Collision rejection above still wins over this preference.
    float heading=std::clamp(vx*m.fx+vy*m.fy,-1.0f,1.0f);
    float score=progress+recovery*4.0f-proximity*2.0f-
                (1.0f-heading)*0.35f-std::abs(turn)*0.015f-verticalChoice*0.01f;
    if (score>best){best=score;bx=nx;by=ny;bs=swim;}
  }
  if (!std::isfinite(best)) {
    m.awarenessWait+=seconds;
    if (m.awarenessWait>2.0f) {
      // Keep the behaviour pending; reassign an occupied feeding place.
      replan();
      m.awarenessWait=0;
    }
    return false;
  }
  float moved=std::hypot(bx-m.x,by-m.y);
  float progress=distance-std::hypot(tx-bx,ty-by);
  m.awarenessWait=progress>moved*0.1f ? std::max(0.0f,m.awarenessWait-seconds*0.25f) : m.awarenessWait+seconds;
  if(moved>0.00001f){m.fx=(bx-m.x)/moved;m.fy=(by-m.y)/moved;}
  m.x=bx;m.y=by;m.swimZ=bs;
  int nowTank=tankAt?tankAt(m.x,m.y):-1;
  if(fromTank>=0 && nowTank<0)m.swimZ=m.swimTo=0;
  else if(fromTank<0 && nowTank>=0)m.swimTo=m.swimZ;
  bool arrived=std::hypot(tx-m.x,ty-m.y)<0.0001f;
  if(!arrived && m.awarenessWait>1.5f) {
    replan();
    m.awarenessWait=0;
  }
  return arrived;
}

void Animals::separateIdle(Member &m,float seconds,const WorldMap &map,const Fences &fences) {
  if (m.pathAt<m.path.size() || m.showTank>=0 || m.sleeping || m.egg ||
      m.dying || m.inside || m.tranquilised || m.building>=0) return;
  m.awarenessCheck-=seconds;
  if(m.awarenessCheck>0)return;
  m.awarenessCheck=0.5f;
  if(destinationFree(m,m.x,m.y,m.swimZ,false) && tankClearance(m,m.x,m.y,fences)>=-0.001f)return;
  const Type &t=typeList[m.type];
  std::string saved=m.moveAnim;
  float speed=m.speed;
  // art() selects walk, swim or submerged swim from the physical tile.
  m.moveAnim="walk";
  m.speed=std::max(0.3f,t.slow);m.swimTo=m.swimZ;
  float angle=m.id*2.39996323f;
  for(int ring=1;ring<=3;++ring) for(int i=0;i<16;++i) {
    float a=angle+i*6.2831853f/16, r=ring*(bodyRadius(m)+0.5f);
    float x=m.x+std::cos(a)*r,y=m.y+std::sin(a)*r;
    if(standable(static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),t,m.exhibit,map,fences) &&
       walkTo(m,x,y,map,fences)) {
      m.stack.clear();m.foodItem=-1;return;
    }
  }
  m.moveAnim=saved;m.speed=speed;
}

std::string Animals::awarenessReport() const {
  std::ostringstream out;out<<std::fixed<<std::setprecision(3);
  int overlaps=0;
  for(const auto &a:list) {
    if(a.boxed || a.inside || a.id==carried)continue;
    float nearest=999;
    for(const auto &b:list) if(awareOf(a,b)) {
      float d=bodyDistance(a,a.x,a.y,bodyHeight(a,a.x,a.y,a.swimZ),b,b.x,b.y,bodyHeight(b,b.x,b.y,b.swimZ));
      nearest=std::min(nearest,d);
      if(a.id<b.id && d<0.99f)++overlaps;
    }
    out<<"Animal "<<a.id<<" "<<typeList[a.type].file<<" x="<<a.x<<" y="<<a.y<<" z="<<a.swimZ
       <<" radius="<<bodyRadius(a)<<" clearance="<<nearest<<" waiting="<<a.awarenessWait;
    if(fencesRef)out<<" wall="<<tankClearance(a,a.x,a.y,*fencesRef);
    if(a.pathAt<a.path.size())out<<" goal="<<a.path.back().first<<","<<a.path.back().second;
    out<<" doing="<<a.anim<<" hunger="<<a.hunger<<" foodItem="<<a.foodItem;
    if(items)if(const auto *food=items->item(a.foodItem))
      out<<" foodAt="<<food->x<<","<<food->y<<" units="<<food->units;
    if(auto *drawn=art(a,a.anim))for(const auto &[path,cached]:artCache)if(cached==drawn){out<<" render="<<path;break;}
    out<<"\n";
  }
  out<<"Body overlaps="<<overlaps<<" (clearance 1.0 is touching; separate tank depths count).";
  return out.str();
}
