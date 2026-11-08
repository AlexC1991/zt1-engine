#include "DayNightLighting.hpp"
#include "World.hpp"
#include "Features.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

using LightingAssets::Point;
using LightingAssets::Model;
namespace {
// addition: real deck/support geometry mirrors World::collectWalkways. These
// surfaces have no artwork and are never an extruded path-texture silhouette.
void quad(Model &m,std::array<Point,4> p,Point normal) {
  m.faces.push_back({p,normal,normal});
  for(auto v:p) {
    m.minimum={std::min(m.minimum.x,v.x),std::min(m.minimum.y,v.y),std::min(m.minimum.z,v.z)};
    m.step={std::max(m.step.x,v.x),std::max(m.step.y,v.y),std::max(m.step.z,v.z)};
  }
}
void box(Model &m,float l,float t,float r,float b,float low,float high) {
  if(high<=low || r<=l || b<=t)return;
  quad(m,{{{l,t,low},{r,t,low},{r,t,high},{l,t,high}}},{0,-1,0});
  quad(m,{{{r,b,low},{l,b,low},{l,b,high},{r,b,high}}},{0,1,0});
  quad(m,{{{l,b,low},{l,t,low},{l,t,high},{l,b,high}}},{-1,0,0});
  quad(m,{{{r,t,low},{r,b,low},{r,b,high},{r,t,high}}},{1,0,0});
  quad(m,{{{l,t,high},{r,t,high},{r,b,high},{l,b,high}}},{0,0,1});
  quad(m,{{{l,b,low},{r,b,low},{r,t,low},{l,t,low}}},{0,0,-1});
}
void slab(Model &m,const float h[4],float thickness,const float *base=nullptr) {
  Point top[4]={{-.5f,-.5f,h[0]},{.5f,-.5f,h[1]},{.5f,.5f,h[2]},{-.5f,.5f,h[3]}};
  Point n{-(h[1]-h[0]+h[2]-h[3])*.5f,-(h[3]-h[0]+h[2]-h[1])*.5f,1};
  float length=std::sqrt(n.x*n.x+n.y*n.y+1);n={n.x/length,n.y/length,1/length};
  quad(m,{{top[0],top[1],top[2],top[3]}},n);
  if(thickness<=0)return;
  Point bottom[4];for(int k=0;k<4;++k)bottom[k]={top[k].x,top[k].y,base?base[k]:top[k].z-thickness};
  quad(m,{{bottom[3],bottom[2],bottom[1],bottom[0]}},{-n.x,-n.y,-n.z});
  const Point sides[4]={{0,-1,0},{1,0,0},{0,1,0},{-1,0,0}};
  for(int k=0;k<4;++k)quad(m,{{bottom[k],bottom[(k+1)%4],top[(k+1)%4],top[k]}},sides[k]);
}
// addition: native fence pixels sampled on their actual vertical wall plane.
// A sparse rail/chain-link remains sparse; no one-pixel visual-hull dilation
// closes its holes. The glass aperture is an authored material exclusion.
Model fencePlane(Animation &art,int frame,float height,bool glass,bool mullion,float lower=0) {
  Model model;
  const auto source=art.lightingFrame(CompassDirection::NE,frame);
  if(!source.surface)return model;
  constexpr int nx=40;const int nz=std::clamp(int(std::ceil((height+1-lower)*16)),16,144);
  const float dz=(height+1-lower)/nz;
  std::vector<unsigned char> mask(nx*nz);
  for(int z=0;z<nz;++z)for(int x=0;x<nx;++x) {
    const float px=-.5f+(x+.5f)/nx,pz=lower+(z+.5f)*dz;
    if(glass && std::abs(px)<.40f && pz>.20f && pz<height-.25f && !(mullion && std::abs(px)<.035f))continue;
    const float sx=32*px,sy=16*px-16*pz;
    const int ix=int(std::floor(source.mirrored?source.anchor.x-sx:source.anchor.x+sx));
    const int iy=int(std::floor(source.anchor.y+sy));auto *s=source.surface;
    if(ix<0 || iy<0 || ix>=s->w || iy>=s->h)continue;
    Uint8 r,g,b,a;SDL_GetRGBA(reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(s->pixels)+iy*s->pitch)[ix],s->format,&r,&g,&b,&a);
    bool shadow=false;if(source.shadowRuns) {
      const size_t pixel=size_t(iy)*s->w+ix;
      for(auto run:*source.shadowRuns)if(pixel>=size_t(run>>32) && pixel<size_t(run>>32)+uint32_t(run)){shadow=true;break;}
    }
    mask[z*nx+x]=a>=128 && !shadow;
  }
  for(int z=0;z<nz;++z)for(int x=0;x<nx;) {
    if(!mask[z*nx+x]){++x;continue;}
    int width=1,rows=1;while(x+width<nx && mask[z*nx+x+width])++width;
    bool more=true;while(z+rows<nz && more) {
      for(int k=0;k<width;++k)if(!mask[(z+rows)*nx+x+k]){more=false;break;}
      if(more)++rows;
    }
    const float l=-.5f+float(x)/nx,r=-.5f+float(x+width)/nx,lo=lower+z*dz,hi=lower+(z+rows)*dz;
    box(model,l,-.022f,r,.022f,lo,hi);
    for(int y=0;y<rows;++y)for(int k=0;k<width;++k)mask[(z+y)*nx+x+k]=0;
    x+=width;
  }
  return model;
}
}

// addition: shared structures use the same native type, damage/open frame,
// tank heights and raised-path dimensions as the visible game geometry.
size_t DayNightLighting::collectStructures(World &world) {
  auto &fences=world.getFences();auto &map=world.getMap();auto &decks=world.getWalkways();
  size_t hash=1469598103934665603ull;
  auto mix=[&](size_t n){hash^=n;hash*=1099511628211ull;};
  mix(map.getGeneration());mix(Features::elevatedPaths);
  for(const auto &[e,p]:fences.pieces()) {
    mix(e.x);mix(e.y);mix(e.alongX);mix(p.type);mix(p.gate);mix(p.facing);mix(p.life<=0?0:p.type>=0 && p.type<int(fences.types().size()) && p.life<=fences.types()[p.type].decayedLife?1:2);mix(p.gate && p.open>0);mix(p.tank);mix(p.tank2);
  }
  for(const auto &ex:fences.exhibits())if(ex.tank){mix(ex.id);mix(ex.floorHeight);mix(ex.wallSub);}
  for(const auto &[xy,d]:decks.all()){mix(xy.first);mix(xy.second);for(int h:d.h)mix(h);}
  for(const auto &m:world.getStaff().members())if(world.getStaff().types()[m.type].kind==Staff::Kind::Helicopter){mix(m.id);mix(m.type);mix(size_t(world.getStaff().baseArt(m)));mix(std::hash<float>{}(m.x));mix(std::hash<float>{}(m.y));}
  for(const auto &f:fences.filters()){mix(f.x);mix(f.y);mix(std::hash<std::string>{}(f.file));mix(int(fences.filterState(f)));mix(std::hash<float>{}(f.dx));mix(std::hash<float>{}(f.dy));}
  if(hash!=structureHash) {
    structureHash=hash;structureInstances.clear();
    auto add=[&](const std::string &key,Point p,float angle,uint64_t owner,const std::function<Model()> &build,const LightingAssets::Definition *asset=nullptr,
                 bool shaded=true,float opacity=1) {
      auto found=structureModels.find(key);if(found==structureModels.end()){
        Model model=build();
        if(model.solid.empty() && !model.faces.empty()) {
          Point lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
          for(const auto &f:model.faces)for(auto v:f.p){lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
          model.minimum=lo;model.step={std::max(.001f,hi.x-lo.x),std::max(.001f,hi.y-lo.y),std::max(.001f,hi.z-lo.z)};model.nx=model.ny=model.nz=1;
        }
        found=structureModels.emplace(key,std::move(model)).first;
      }
      if(!found->second.faces.empty()) {
        structureInstances.push_back({&found->second,asset,p,std::cos(angle),std::sin(angle),0,owner});
        structureInstances.back().shaded=shaded;structureInstances.back().opacity=opacity;
      }
    };
    uint64_t serial=uint64_t(1)<<56;
    for(const auto &[e,p]:fences.pieces()) {
      if(p.type<0 || p.type>=int(fences.types().size()))continue;const auto &t=fences.types()[p.type];
      int ax,ay,bx,by,h0,h1;fences.edgeGeometry(e,p,map,ax,ay,bx,by,h0,h1);
      Point position{(ax+bx)*.5f,(ay+by)*.5f,(h0+h1)*.5f};
      const float angle=p.facing*.7853981634f;
      if(p.tank<0) {
        Animation *art=t.art[p.gate?1:0][0];
        if(!p.gate && !t.indestructible && p.life>=0) {
          if(p.life<=0 && t.broke[0])art=t.broke[0];else if(p.life<=t.decayedLife && t.det[0])art=t.det[0];
        }
        if(!art)continue;
        // ours: a gate's shape in two poses, shut and open (open from the
        // moment it starts to swing until it is shut again): every frame of
        // its swing as its own shape remade the whole zoo's lighting, several
        // times as a keeper walked through
        const int pose=p.gate && p.open>0?std::max(0,art->frameCount()-1):0;
        const bool glass=t.key=="glass" || t.key=="smglass" || t.key=="dglas";
        const std::string key="fence:"+art->lightingKey()+":"+std::to_string(pose)+":"+std::to_string(t.height)+":"+std::to_string(h1-h0);
        const size_t before=structureInstances.size();
        add(key,position,angle,serial++,[&]{Model m=fencePlane(*art,pose,float(t.height),glass,t.key=="dglas",t.key=="buoyrope"?-1.0f:0);
          const float slope=float(h1-h0);const float sign=e.alongX?std::cos(angle):std::sin(angle);
          for(auto &f:m.faces)for(auto &v:f.p)v.z+=v.x*slope*sign;
          return m;},nullptr,false);
        // (lit at night through its own art: a lamp or a beam on the fence)
        if(structureInstances.size()>before && !glass){auto &o=structureInstances.back();o.artwork=art;o.frame=pose;o.nightReceiver=true;}
        continue;
      }
      const auto *ex=fences.exhibit(p.tank);if(!ex || !ex->tank)continue;
      const float top=ex->wallTop(),low=float(ex->floorHeight);position.z=low;
      // (a plain box from the floor to the rim, its shadow all a tank casts: no
      // rails, frames, ladders, platforms or gates - their thin shadows drew
      // lines across the floor - and the walls not shaded themselves; glass
      // stops half the light)
      std::string key="tankbox:"+std::to_string(top-low);
      const size_t before=structureInstances.size();
      add(key,position,e.alongX?0:1.5707963268f,serial++,[&]{Model m;box(m,-.5f,-.05f,.5f,.05f,0,top-low);return m;},
          nullptr,false,t.seeThrough?.5f:1.0f);
      // (a solid wall is lit at night as a wall is - the box is the wall, so
      // no art mask; glass lets the light through to the water behind it)
      if(structureInstances.size()>before && !t.seeThrough)structureInstances.back().nightReceiver=true;
    }
    if(Features::elevatedPaths)for(const auto &[xy,d]:decks.all()) {
      const auto *tile=map.getTile(xy.first,xy.second);if(!tile)continue;
      float ground=0;for(int h:tile->cornerHeight)ground+=h*.25f;
      float mid=0;for(int h:d.h)mid+=h*.25f;
      std::ostringstream key;key<<"deck:"<<ground;for(int h:d.h)key<<':'<<h;
      int join=0,railJoin=0;for(int dir=0;dir<4;++dir){
        const bool joined=decks.joins(xy.first,xy.second,dir);if(joined){join|=1<<dir;railJoin|=1<<dir;continue;}
        int dx,dy,a,b,na,nb;Walkways::step(dir,dx,dy);Walkways::edgeCorners(dir,a,b);Walkways::edgeCorners((dir+2)&3,na,nb);
        const auto *n=map.getTile(xy.first+dx,xy.second+dy);
        if(n && map.isPath(xy.first+dx,xy.second+dy) && d.h[a]==n->cornerHeight[na] && d.h[b]==n->cornerHeight[nb])railJoin|=1<<dir;
      }key<<':'<<join<<':'<<railJoin;for(int h:tile->cornerHeight)key<<':'<<h;
      add(key.str(),{xy.first+.5f,xy.second+.5f,0},0,serial++,[&]{Model m;float h[4];for(int k=0;k<4;++k)h[k]=float(d.h[k]);
        const bool solid=*std::max_element(d.h,d.h+4)-*std::min_element(tile->cornerHeight,tile->cornerHeight+4)<=2;
        float base[4];for(int k=0;k<4;++k)base[k]=float(tile->cornerHeight[k]);
        const bool level=*std::min_element(d.h,d.h+4)==*std::max_element(d.h,d.h+4);
        slab(m,h,solid?std::max(.35f,mid-ground):level?.35f:0,solid?base:nullptr);
        if(!solid && mid-.35f>ground+.1f) {
          box(m,-.22f,-.22f,.22f,.22f,ground,ground+.3f);box(m,-.13f,-.13f,.13f,.13f,ground+.3f,mid-.35f);
          if(mid-.35f-ground>=4) {
            float z=ground+(mid-.35f-ground)*.5f;
            if(join&2)box(m,0,-.06f,1,.06f,z-.25f,z);
            if(join&4)box(m,-.06f,0,.06f,1,z-.25f,z);
          }
        }
        // Same five native posts and sloping top rail as collectWalkways.
        const float cx[]={-.5f,.5f,.5f,-.5f},cy[]={-.5f,-.5f,.5f,.5f};
        for(int dir=0;dir<4;++dir)if(!(railJoin&(1<<dir))) {
          int a,b;Walkways::edgeCorners(dir,a,b);
          const float ax=cx[a],ay=cy[a],bx=cx[b],by=cy[b];
          for(int k=0;k<=4;++k){const float t=k*.25f,x=ax+(bx-ax)*t,y=ay+(by-ay)*t,z=h[a]+(h[b]-h[a])*t;box(m,x-.016f,y-.016f,x+.016f,y+.016f,z,z+.7f);}
          const float width=.018f;
          Point p[4]={{ax,ay,h[a]+.575f},{bx,by,h[b]+.575f},{bx,by,h[b]+.7f},{ax,ay,h[a]+.7f}};
          Point normal{by-ay,ax-bx,0};quad(m,{{p[0],p[1],p[2],p[3]}},normal);
          quad(m,{{p[3],p[2],p[1],p[0]}},{-normal.x,-normal.y,0});
        }
        return m;});
    }
    for(const auto &f:fences.filters()) {
      const auto &t=fences.filterType(f.file);Animation *a=t.idle;
      switch(fences.filterState(f)){case Fences::FilterState::Broken:a=t.broken?t.broken:a;break;case Fences::FilterState::Off:a=t.off?t.off:a;break;case Fences::FilterState::Decayed:a=t.decayed?t.decayed:a;break;default:break;}
      if(!a)continue;
      const float angle=std::atan2(f.dx,-f.dy),x=f.x+t.footprintX*.5f,y=f.y+t.footprintY*.5f;
      add("filter:"+a->lightingKey(),{x,y,fences.filterGround(f,map)},angle,serial++,[&]{LightingAssets::Definition d{0,"filter body"};return LightingAssets::build(*a,d,t.footprintX*.5f,t.footprintY*.5f);});
    }
    for(const auto &m:world.getStaff().members()) {
      const auto &t=world.getStaff().types()[m.type];if(t.kind!=Staff::Kind::Helicopter)continue;
      auto *a=world.getStaff().baseArt(m);if(!a)continue;const auto *asset=LightingAssets::find("helibase");
      add("DRT base:"+a->lightingKey(),{m.x,m.y,LightingAssets::terrainHeight(map,m.x,m.y)},0,(uint64_t(1)<<54)|(uint64_t(uint32_t(m.id))<<3)|3,
        [&]{LightingAssets::Definition plain{0,"DRT base"};return LightingAssets::build(*a,asset?*asset:plain,t.footX*.5f,t.footY*.5f);},asset);
    }
    // State changes must not retain every visited gate pose forever. Entries
    // borrowed by this frame stay valid; the rest may be rebuilt on demand.
    if(structureModels.size()>512)for(auto it=structureModels.begin();it!=structureModels.end();) {
      bool used=false;for(const auto &o:structureInstances)if(o.model==&it->second){used=true;break;}
      if(!used){it=structureModels.erase(it);groundIrradianceCache.clear();surfaceIrradianceCache.clear();modelTop.clear();}else ++it;
    }
  }
  instances.insert(instances.end(),structureInstances.begin(),structureInstances.end());
  return structureHash;
}
