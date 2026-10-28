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
// The space one animal occupies, measured the way it is drawn.
//
// Its species file gives a footprint - cFootprintX across and cFootprintY along
// the way it faces, in half tiles - and its art gives how tall it is drawn. An
// oriented box from those two matches the animal on screen instead of one
// blanket radius for every species and size: a dolphin is long and thin, a
// walrus is nearly round, and in a tank it is the height that decides whether
// two can pass one above the other.
//
// halfAlong / halfAcross are in tiles; halfHeight is in height units, turned
// into the same tiles' worth of screen by the caller's scale.
//
// cFootprint is in HALF tiles, so its quarter is the body's half extent: a whale
// shark's cFootprintY of 8 is four tiles long, two either side of its centre.
// The draw code lays out the same box with footY / 4.0f, so the two agree;
// halving it again here made every body half the size it is drawn.
Animals::BodyVolume Animals::bodyVolume(const Member &m) const {
  const Type &t = typeList[m.type];
  const float scale = m.egg ? 0.3f : m.baby ? 0.6f : 1.0f;
  // (the old minimum of 0.22 tiles a side keeps a very young or very small
  // animal a real body)
  const float halfAcross = std::max(0.22f, t.footX / 4.0f) * scale;
  const float halfAlong = std::max(0.22f, t.footY / 4.0f) * scale;
  // Its own art, if it has been loaded: the drawn height is the honest one.
  float drawn = 0.0f;
  if (Animation *a = art(m, m.anim)) {
    int w = 0, h = 0;
    a->queryTexture(CompassDirection::SE, &w, &h);
    // A 64-px tile is one tile: convert the drawn pixels to tiles.
    if (h > 0)
      drawn = h / 64.0f;
  }
  // Without art, fall back to the footprint's own size with a share for the
  // body standing above it.
  float height = drawn > 0.05f ? drawn : std::max(halfAcross, halfAlong) * 1.8f;
  // Never a paper-thin body: a third of a tile is the least anything occupies.
  height = std::max(height, 0.34f);
  BodyVolume v;
  v.halfAcross = halfAcross;
  v.halfAlong = halfAlong;
  v.halfHeight = height * 0.5f;
  return v;
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

// How far apart two bodies are, as a multiple of the distance at which they
// touch: 1.0 is touching, under 1.0 is overlapping.
//
// Flat distance and height are compared in the units the sprites are drawn
// in. A height unit shows as elevationScale*(tileHeight/32) pixels of screen -
// 16 by the standard 64x32 tile, a quarter of the tile's width - while two
// bodies standing together are worth a whole tile of it. Giving height a fixed
// divisor of its own therefore made DEPTH separate animals far more cheaply
// than water ever can: in a crowded tank it read as "apart" for two fish drawn
// one over the other. Seven marine animals in a 7x7 tank sat pinned at exactly
// 1.000 clearance, each reversing direction twelve times in twenty seconds -
// the twitch. Air in a tank must not buy an animal room it does not have; they
// have to give each other room in the water, at whatever depth.
// How much room two animals have, as a multiple of what they need to touch:
// 1.0 means their boxes are exactly touching, under 1.0 that they overlap.
//
// The separation is taken per axis and each axis is measured against what the
// two bodies actually need there, rather than wrapping one shared radius round
// both. Up the screen, a height unit is elevationScale*(tileHeight/32) pixels -
// 16 by the standard 64x32 tile, a quarter of a tile's width - so heights are
// scaled into tiles before being compared. That is what lets two fish pass one
// above the other in deep water while refusing them the same water.
float Animals::bodyOverlap(const Member &a, float ax, float ay, float az,
                           float afx, float afy,
                           const Member &b, float bx, float by, float bz,
                           float bfx, float bfy) const {
  // Pixels of screen per tile of ground, and per height unit.
  constexpr float kHeightPerTile = 16.0f / 64.0f;
  const BodyVolume va = bodyVolume(a), vb = bodyVolume(b);
  const float needAcross = va.halfAcross + vb.halfAcross;
  const float needAlong = va.halfAlong + vb.halfAlong;
  // Height in tiles: what the two bodies need between them, up the screen.
  const float needUp = (va.halfHeight + vb.halfHeight) * kHeightPerTile;

  // Flat separation, resolved along each animal's own facing. Of the two
  // boxes, take the direction that separates them best, which is what a
  // swept pair does: an animal lengthways to another can pass close.
  float nx = ax - bx, ny = ay - by;
  const float flat = std::hypot(nx, ny);
  float alongA = 0.0f, acrossA = 0.0f, alongB = 0.0f, acrossB = 0.0f;
  auto split = [&](float fx, float fy, float &along, float &across) {
    const float len = std::hypot(fx, fy);
    if (len < 0.001f) { along = 0.0f; across = flat; return; }
    fx /= len; fy /= len;
    along = std::abs(nx * fx + ny * fy);
    across = std::abs(nx * fy - ny * fx);
  };
  split(afx, afy, alongA, acrossA);
  split(bfx, bfy, alongB, acrossB);
  // Each body's share: how much of the gap it must cover on the worse axis.
  const float along = std::max(alongA / std::max(needAlong, 0.001f),
                              alongB / std::max(needAlong, 0.001f));
  const float across = std::max(acrossA / std::max(needAcross, 0.001f),
                               acrossB / std::max(needAcross, 0.001f));
  const float up = std::abs(az - bz) * kHeightPerTile / std::max(needUp, 0.001f);
  // The boxes are clear when ANY axis clears (a box test), so the axis with
  // the most room is the one that decides; 1.0 is where all three touch.
  return std::max({along, across, up});
}

// How wide an animal is, for the places that only need a horizontal radius
// (spacing at a show, a swimmer turning by a wall).
float Animals::bodyRadius(const Member &m) const {
  const BodyVolume v = bodyVolume(m);
  return std::max(v.halfAcross, v.halfAlong);
}

// Room between two animals, measured with each one's own facing, in the units
// the old function used: 1.0 is touching, under 1.0 is overlapping.
float Animals::bodyDistance(const Member &a, float ax, float ay, float az,
                           const Member &b, float bx, float by, float bz) const {
  return bodyOverlap(a, ax, ay, az, a.fx, a.fy, b, bx, by, bz, b.fx, b.fy);
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

// What to do about two animals IDLING on top of each other.
//
// Animals may swim through one another - in a tank there is often no room to
// go round, and refusing those steps is what made them stall and turn on the
// spot. The fault was narrower than that: an animal that stops for a while
// would pick a spot another animal was already standing on, and the two were
// drawn one over the other like a doubled fish, sometimes for minutes. So
// only the idle case is handled here, and a moving animal is never touched.
//
// It is also a nudge, not a standoff. A crowded tank has no clear spot to
// offer, so an animal that finds nowhere free still takes one step toward the
// roomiest place it can see, without having to satisfy every neighbour. And
// it only moves if that really gives it more room than it has now.
void Animals::separateIdle(Member &m,float seconds,const WorldMap &map,const Fences &fences) {
  if (m.pathAt<m.path.size() || m.showTank>=0 || m.sleeping || m.egg ||
      m.dying || m.inside || m.tranquilised || m.building>=0) return;
  m.awarenessCheck-=seconds;
  if(m.awarenessCheck>0)return;
  m.awarenessCheck=0.5f;
  // How close the nearest other body is, in bodyDistance units: 1.0 is
  // touching, under 1.0 means standing inside it.
  auto clearanceAt=[&](float x,float y) {
    const float z=bodyHeight(m,x,y,m.swimZ);
    float clear=1000;
    for(const auto &o:list) if(awareOf(m,o))
      clear=std::min(clear,bodyDistance(m,x,y,z,o,o.x,o.y,
        bodyHeight(o,o.x,o.y,o.swimZ)));
    return clear;
  };
  if(clearanceAt(m.x,m.y)>=1.0f)return; // standing clear: nothing to do
  // The nearest spot that is genuinely clear of every other body. Nearest,
  // so an animal steps aside rather than crossing the tank to the far side.
  const Type &t=typeList[m.type];
  const std::string savedAnim=m.moveAnim;
  const float savedSpeed=m.speed;
  m.moveAnim="walk";
  const float angle=m.id*2.39996323f;
  for(int ring=1;ring<=3;++ring) {
    int found=-1;
    float foundX=0, foundY=0, foundD=1e9f;
    for(int i=0;i<16;++i) {
      const float a=angle+i*6.2831853f/16, r=ring*(bodyRadius(m)+0.5f);
      const float x=m.x+std::cos(a)*r, y=m.y+std::sin(a)*r;
      const int tx=static_cast<int>(std::floor(x)), ty=static_cast<int>(std::floor(y));
      if(!standable(tx,ty,t,m.exhibit,map,fences))continue;
      if(tankClearance(m,x,y,fences)<-0.001f)continue;
      if(clearanceAt(x,y)<1.0f)continue;          // another body stands there
      const float d=std::hypot(x-m.x,y-m.y);
      if(d<foundD){foundD=d;foundX=x;foundY=y;found=i;}
    }
    if(found>=0 && walkTo(m,foundX,foundY,map,fences)) {
      // A proper walk at the animal's fast pace, so it gets there before the
      // spot is taken; the route itself is the old one.
      m.speed=std::max(0.3f,t.fast);
      m.stack.clear();m.foodItem=-1;
      return;
    }
  }
  // Nowhere clear within reach (a tank that is genuinely full): leave the
  // animal exactly as it was rather than shoving it about.
  m.moveAnim=savedAnim;m.speed=savedSpeed;m.swimTo=m.swimZ;
}

std::string Animals::awarenessReport() const {
  std::ostringstream out;out<<std::fixed<<std::setprecision(3);
  int overlaps=0;
  for(const auto &a:list) {
    if(a.boxed || a.inside || a.id==carried)continue;
    const BodyVolume v=bodyVolume(a);
    float nearest=999;
    for(const auto &b:list) if(awareOf(a,b)) {
      float d=bodyDistance(a,a.x,a.y,bodyHeight(a,a.x,a.y,a.swimZ),b,b.x,b.y,bodyHeight(b,b.x,b.y,b.swimZ));
      nearest=std::min(nearest,d);
      if(a.id<b.id && d<0.99f)++overlaps;
    }
    out<<"Animal "<<a.id<<" "<<typeList[a.type].file<<" x="<<a.x<<" y="<<a.y<<" z="<<a.swimZ
       <<" radius="<<bodyRadius(a)<<" vsize="<<v.halfAcross<<","<<v.halfAlong<<","<<v.halfHeight
       <<" face="<<a.fx<<","<<a.fy
       <<" clearance="<<nearest<<" waiting="<<a.awarenessWait;
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
