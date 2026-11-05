#include "DayNightLighting.hpp"
#include "World.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
using LightingAssets::Point;
void DayNightLighting::addActor(Animation *art,int frame,Point position,float fx,float fy,float halfX,float halfY,uint64_t id) {
  if(!art || actors.size()>=maxActors)return;
  auto key=art->lightingKey();if(key.empty())return;
  const int count=std::max(1,art->frameCount()),raw=std::max(0,frame)%count;
  const int pose=count<=12?raw:(raw*12/count)*count/12;
  auto found=actorModels.find({key,pose});
  if(found==actorModels.end() && counters.actorBuilds<2) {
    LightingAssets::Definition body{0,"animated body"};
    auto model=LightingAssets::build(*art,body,halfX,halfY,pose,true);
    // Tiny food/litter sprites may contain only a single ground-facing view,
    // which cannot form a four-view hull. Preserve their native alpha as a
    // shallow contact surface rather than inventing a tall opaque box.
    if(model.faces.empty() && (id&(uint64_t(1)<<53))) {
      auto source=art->lightingFrame(CompassDirection::SE,pose);const bool north=!source.surface;if(north)source=art->lightingFrame(CompassDirection::NE,pose);auto *surface=source.surface;
      if(surface)for(int y=0;y<surface->h;++y)for(int x=0;x<surface->w;++x) {
        Uint8 r,g,b,a;SDL_GetRGBA(reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels)+y*surface->pitch)[x],surface->format,&r,&g,&b,&a);
        if(a<128)continue;bool shadow=false;const size_t pixel=size_t(y)*surface->w+x;
        if(source.shadowRuns)for(auto run:*source.shadowRuns)if(pixel>=size_t(run>>32) && pixel<size_t(run>>32)+uint32_t(run)){shadow=true;break;}
        if(shadow)continue;
        auto p=[&](float sx,float sy){sx-=source.anchor.x;sy-=source.anchor.y;if(source.mirrored)sx=-sx;return north?Point{sx/64+sy/32,-sx/64+sy/32,.15f}:Point{-sx/64+sy/32,-sx/64-sy/32,.15f};};
        model.faces.push_back({{p(float(x),float(y)),p(float(x+1),float(y)),p(float(x+1),float(y+1)),p(float(x),float(y+1))},{0,0,1},{0,0,1}});
      }
      if(!model.faces.empty()){model.solid.clear();model.occupied=0;model.minimum={-2,-2,0};model.step={4,4,.2f};model.nx=model.ny=model.nz=1;}
    }
    ++counters.actorBuilds;
    found=actorModels.emplace(std::make_pair(key,pose),ActorModel{std::move(model),actorTick}).first;
  }
  if(found==actorModels.end() || found->second.model.faces.empty()) {
    int nearest=std::numeric_limits<int>::max();
    for(auto i=actorModels.lower_bound({key,0});i!=actorModels.end() && i->first.first==key;++i) {
      const int gap=std::abs(i->first.second-pose);if(!i->second.model.faces.empty() && gap<nearest){nearest=gap;found=i;}
    }
    if(found==actorModels.end() || found->second.model.faces.empty())return;++counters.actorFallbacks;
  }
  found->second.used=actorTick;
  // The native renderer has eight facings. Match its snapped orientation;
  // continuous steering must not slide the shadow away from the shown pose.
  const float angle=std::round(std::atan2(fx,-fy)/(.7853981634f))*.7853981634f;
  actors.push_back({&found->second.model,nullptr,position,std::cos(angle),std::sin(angle),0,id,art,frame});
}
void DayNightLighting::collectActors(World &world) {
  actors.clear();actorFaces.clear();++actorTick;
  if(sky.sun<=.001f && sky.lamps<=.001f)return;
  struct Pose {Animation *art;int frame;Point p;float fx,fy,hx,hy,distance;uint64_t id;};std::vector<Pose> poses;
  auto &map=world.getMap();auto &view=world.getRenderer();
  auto add=[&](Animation *a,int frame,Point p,float fx,float fy,float hx,float hy,uint64_t id) {
    float x,y,d;view.worldToScreenF(p.x,p.y,p.z,x,y,d);
    if(x< -250 || x>logicalW+250 || y< -250 || y>logicalH+350)return;
    if(a){float l,t,r,b;const auto side=view.screenSide(fx,fy);if(a->anchoredBounds(x,y,side,l,t,r,b))covers.push_back({id,a,side,x,y,d,l,t,r,b,frame});}
    poses.push_back({a,frame,p,fx,fy,hx,hy,(x-logicalW*.5f)*(x-logicalW*.5f)+(y-logicalH*.5f)*(y-logicalH*.5f),id});
  };
  auto &animals=world.getAnimals();
  for(const auto &m:animals.members()) {
    if(m.id==animals.carried || (m.inside && m.insideFor>0) || m.maulFor>0)continue;
    if(m.anim.find("dust")!=std::string::npos || m.anim.find("fight")!=std::string::npos)continue;
    Animation *a=m.boxed?animals.crate(m):animals.art(m,m.anim);if(!a)continue;
    int count=std::max(1,a->frameCount()),f=int(m.animTime*1000/(a->frameTimeMs()?a->frameTimeMs():100u));
    if(m.mode==Animals::Member::Mode::Once)f=std::min(f,count-1);
    else if(m.mode==Animals::Member::Mode::Reverse)f=count-1-std::min(f,count-1);
    else if(m.mode==Animals::Member::Mode::PingPong){f%=2*count;f=f<count?f:2*count-1-f;}
    else f%=count;
    const bool tank=animals.tankAt && animals.tankAt(m.x,m.y)>=0;
    const auto &t=animals.types()[m.type];
    const Point p{m.x,m.y,LightingAssets::terrainHeight(map,m.x,m.y)+(tank?m.swimZ:0)};
    if(!m.boxed && !m.egg)if(const int material=LightingAssets::fluorescenceProfile(t.file)) {
      float x,y,d;view.worldToScreenF(p.x,p.y,p.z,x,y,d);
      const float excitation=fluorescenceCues.size()<64 && x>=-250 && x<=logicalW+250 && y>=-250 && y<=logicalH+350?excitationAt({p.x,p.y,p.z+.4f}):0;
      addFluorescence(a,x,y,excitation,material);
      if(auto *under=animals.underArt(m,a))addFluorescence(under,x,y,excitation,material);
    }
    if(!m.boxed)actorEmission(a,f,p,m.fx,m.fy,((uint64_t(1)<<54)|(uint64_t(uint32_t(m.id))<<3)),LightingAssets::actorEffect(t.file,m.anim,f));
    add(a,f,p,m.boxed?0:m.fx,m.boxed?-1:m.fy,std::max(.25f,t.footX/4.0f),std::max(.25f,t.footY/4.0f),((uint64_t(1)<<54)|(uint64_t(uint32_t(m.id))<<3)));
    if(tank && !m.boxed)if(auto *under=animals.underArt(m,a))add(under,f,p,m.fx,m.fy,std::max(.25f,t.footX/4.0f),std::max(.25f,t.footY/4.0f),((uint64_t(1)<<54)|(uint64_t(uint32_t(m.id))<<3)|4));
  }
  // addition: native food-stage, dung and litter silhouettes at their real floor.
  for(const auto &i:world.getItems().items())if(auto *a=world.getItems().artOf(i)) {
    const auto *tile=map.getTile(int(std::floor(i.x)),int(std::floor(i.y)));const float z=tile?float(tile->height):0;
    const int frame=int((world.getItems().clock+i.id*.37f)*1000/std::max(1u,a->frameTimeMs()));
    // ZooItems always requests SE artwork; recover the equivalent world facing.
    const bool south=a->lightingFrame(CompassDirection::SE,frame).surface!=nullptr;
    const float angle=(view.getRotation()-(south?2:3))*1.5707963268f;
    add(a,frame,{i.x,i.y,z},std::sin(angle),-std::cos(angle),.35f,.35f,(uint64_t(1)<<53)|size_t(uint32_t(i.id)));
  }
  auto &ambient=world.getAmbient();
  for(const auto &f:ambient.flyers) {
    const auto &k=ambient.kinds[f.kind];auto *a=k.idle;if(!a)continue;
    const int frame=int(f.age*1000/std::max(1u,a->frameTimeMs()));
    if(k.key=="santa")if(const auto *asset=LightingAssets::find("santa"))actorEmission(a,frame,{f.x,f.y,ambient.groundAt(map,f.x,f.y)+6.0f},f.vx,f.vy,(uint64_t(1)<<55)+size_t(&f-ambient.flyers.data()),asset->id);
    add(a,frame,{f.x,f.y,ambient.groundAt(map,f.x,f.y)+6.0f},f.vx,f.vy,.6f,.6f,(uint64_t(1)<<55)+size_t(&f-ambient.flyers.data()));
  }
  for(size_t i=0;i<world.objectsMutable().objects().size();++i){const auto &o=world.objectsMutable().objects()[i];const int material=LightingAssets::fluorescenceProfile(PlacedObjects::fileOf(o));if(!material)continue;
    const float ground=LightingAssets::terrainHeight(map,o.x,o.y),z=world.objectsMutable().tankHeight?world.objectsMutable().tankHeight(o,ground):ground;
    float x,y,d;view.worldToScreenF(o.x,o.y,z,x,y,d);
    if(fluorescenceCues.size()>=64 || x< -250 || x>logicalW+250 || y< -250 || y>logicalH+350)continue;
    const float excitation=excitationAt({o.x,o.y,z+.3f},i);
    if(auto *a=PlacedObjects::displayArt(o))addFluorescence(a,x,y,excitation,material);
  }
  auto &staff=world.getStaff();
  for(const auto &m:staff.members()) {
    if(m.id==staff.carried)continue;const auto &t=staff.types()[m.type];
    if(t.kind==Staff::Kind::Helicopter) {
      const float ground=LightingAssets::terrainHeight(map,m.x,m.y);
      const bool parked=m.heliStage==Staff::HeliStage::Parked;
      const float angle=t.padFacing*.7853981634f;
      auto *a=staff.art(m,m.anim);const Point p=parked?Point{m.x+t.padX,m.y+t.padY,ground+t.padZ}:Point{m.heliX,m.heliY,m.heliZ};
      const float fx=parked?std::sin(angle):m.fx,fy=parked?-std::cos(angle):m.fy;
      const auto owner=(uint64_t(1)<<54)|(uint64_t(uint32_t(m.id))<<3)|1;const int frame=staff.helicopterFrame(m,a);
      if(!parked){
        // addition: restrained night marker attachments on the existing
        // airframe. This does not reinterpret tail paint or rotor tips as lamps.
        actorEmission(a,frame,p,fx,fy,owner,LightingAssets::aircraftProfile(m.anim,0));
        const float phase=float(std::fmod(m.baseAnimTime,1.2));const float pulse=phase<.18f?std::sin(phase/.18f*3.1415926536f):0;
        actorEmission(a,frame,p,fx,fy,owner,LightingAssets::aircraftProfile(m.anim,1),pulse);
      }
      if(a)add(a,frame,p,fx,fy,1,1,owner);
      continue;
    }
    auto *a=staff.art(m,m.anim);if(!a)continue;
    int f=int(m.animTime*1000/(a->frameTimeMs()?a->frameTimeMs():100u));
    if(m.climbing){f=std::min(f,std::max(0,a->frameCount()-1));if(m.climbDown)f=std::max(0,a->frameCount()-1-f);}
    add(a,f,{m.x,m.y,staff.heightOf(m,map)},m.fx,m.fy,.35f,.35f,((uint64_t(1)<<54)|(uint64_t(uint32_t(m.id))<<3)|1));
  }
  auto &guests=world.getGuests();
  for(const auto &g:guests.guests()) {
    if(guests.hideAll || g.hidden)continue;auto *a=guests.art(g,g.anim);if(!a)continue;
    const int frame=int(g.animTime*1000/std::max(1u,a->frameTimeMs()))%std::max(1,a->frameCount());
    const Point p{g.x,g.y,LightingAssets::terrainHeight(map,g.x,g.y)+g.showZ};
    actorEmission(a,frame,p,g.fx,g.fy,((uint64_t(1)<<54)|(uint64_t(uint32_t(g.id))<<3)|2),LightingAssets::actorEffect("guests/"+guests.types()[g.type].key,g.anim,frame));
    add(a,frame,p,g.fx,g.fy,.35f,.35f,((uint64_t(1)<<54)|(uint64_t(uint32_t(g.id))<<3)|2));
  }
  std::stable_sort(poses.begin(),poses.end(),[](const auto &a,const auto &b){return a.distance<b.distance;});
  for(const auto &p:poses)addActor(p.art,p.frame,p.p,p.fx,p.fy,p.hx,p.hy,p.id);
  // Only evict entries which no actor in this frame borrows.
  while(actorModels.size()>maxActorModels) {
    auto oldest=actorModels.end();for(auto i=actorModels.begin();i!=actorModels.end();++i)
      if(i->second.used!=actorTick && (oldest==actorModels.end() || i->second.used<oldest->second.used))oldest=i;
    if(oldest==actorModels.end())break;actorModels.erase(oldest);modelTop.clear(); // (keyed by shape: one may go)
  }
  float ox,oy,od;view.worldToScreenF(0,0,0,ox,oy,od);int budget=maxActorFaces;
  for(size_t i=0;i<actors.size();++i) {
    const auto &o=actors[i];if(int(o.model->faces.size())>budget)continue;budget-=int(o.model->faces.size());
    const auto local=irradiance({o.position.x,o.position.y,o.position.z+1},{0,0,1},uint64_t(-1));
    for(const auto &f:o.model->faces) {
      ProjectedFace d{};d.instance=instances.size()+i;d.local=local;
      auto rotate=[&](Point p){return Point{p.x*o.cosine-p.y*o.sine,p.x*o.sine+p.y*o.cosine,p.z};};d.normal=rotate(f.normal);d.outward=rotate(f.outward);
      for(int k=0;k<4;++k){auto p=toWorld(o,f.p[k]);float sx,sy,depth;view.worldToScreenF(p.x,p.y,p.z,sx,sy,depth);d.p[k]={sx-ox,sy-oy};d.world[k]=p;d.terrain[k]=terrainClearance(p,view);d.height[k]=f.p[k].z;d.depth+=.25f*(depth+p.z*.5f);}
      float l=d.p[0].x,t=d.p[0].y,r=l,b=t;
      for(auto p:d.p){l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);}
      d.bounds={l,t,r-l,b-t};
      const float gx[]={-1,-1,1,1},gy[]={1,-1,-1,1};
      d.visible=d.outward.x*gx[view.getRotation()]+d.outward.y*gy[view.getRotation()]+d.outward.z*2>0;
      actorFaces.push_back(d);
    }
  }
  // ours: sorted by index, then moved into that order once (the same order:
  // a stable sort of the faces moved each large face about many times)
  faceOrder.resize(actorFaces.size());for(size_t i=0;i<faceOrder.size();++i)faceOrder[i]=uint32_t(i);
  std::stable_sort(faceOrder.begin(),faceOrder.end(),[this](uint32_t a,uint32_t b){return actorFaces[a].depth<actorFaces[b].depth;});
  faceScratch.clear();faceScratch.reserve(actorFaces.size());for(uint32_t i:faceOrder)faceScratch.push_back(std::move(actorFaces[i]));
  actorFaces.swap(faceScratch);
  finishEmitters();
  counters.actors=int(actors.size());counters.actorFaces=int(actorFaces.size());
  counters.memoryBytes=projectedFaces.capacity()*sizeof(ProjectedFace)+actorFaces.capacity()*sizeof(ProjectedFace);
  for(const auto &[k,m]:models)counters.memoryBytes+=m.solid.capacity()+m.faces.capacity()*sizeof(LightingAssets::Face);
  for(const auto &[k,m]:actorModels)counters.memoryBytes+=m.model.solid.capacity()+m.model.faces.capacity()*sizeof(LightingAssets::Face);
}

size_t DayNightLighting::bufferBytes()const {
  size_t bytes=projectedFaces.capacity()*sizeof(ProjectedFace)+actorFaces.capacity()*sizeof(ProjectedFace)+receivers.capacity()*sizeof(Receiver)+receiverPoints.capacity()*sizeof(ReceiverPoint);
  for(const auto &[k,m]:models)bytes+=m.solid.capacity()+m.faces.capacity()*sizeof(LightingAssets::Face);
  for(const auto &[k,m]:structureModels)bytes+=m.solid.capacity()+m.faces.capacity()*sizeof(LightingAssets::Face);
  for(const auto &[k,m]:actorModels)bytes+=m.model.solid.capacity()+m.model.faces.capacity()*sizeof(LightingAssets::Face);
  bytes+=(surfaceVertices.capacity()+shadowVertices.capacity()+fieldVertices.capacity())*sizeof(SDL_Vertex);
  bytes+=(surfaceIndices.capacity()+shadowIndices.capacity()+fieldIndices.capacity())*sizeof(int);
  for(const auto &m:sunMaps)bytes+=m.bytes();
  for(const auto &[k,c]:groundIrradianceCache)bytes+=c.samples.capacity()*sizeof(Irradiance);
  for(const auto &[k,c]:surfaceIrradianceCache){bytes+=c.samples.capacity()*sizeof(SurfaceSamples);for(const auto &b:c.beams)bytes+=b.capacity()*sizeof(BeamSamples);}
  for(const auto &b:beamSurface)for(const auto &s:b)bytes+=s.capacity()*sizeof(BeamSamples);
  for(const auto &[key,shape]:beamShapes)bytes+=sizeof(shape);
  for(const auto &[model,sources]:facadeSources)bytes+=sources.capacity()*sizeof(LightingAssets::Source);
  bytes+=fluorescenceCues.capacity()*sizeof(FluorescenceCue)+fluorescentArt.size()*sizeof(Animation*)*3;
  for(const auto &[k,c]:emissionClips)bytes+=c.rectangles.capacity()*sizeof(SDL_Rect);
  for(const auto &g:groundLight)bytes+=g.capacity()*sizeof(Irradiance);
  for(const auto &g:surfaceLight)bytes+=g.capacity()*sizeof(SurfaceSamples);
  for(const auto *index:{&lightSources,&lightOccluders})for(const auto &[key,items]:*index)bytes+=sizeof(key)+sizeof(items)+items.capacity()*sizeof(size_t);
  return bytes;
}
