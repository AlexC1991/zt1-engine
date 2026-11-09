#include "DayNightLighting.hpp"
#include "WorldRenderer.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

using LightingAssets::Point;

namespace LightingAssets {
// addition: native lens position with an optional rotating projector. The
// original has no night system; aperture, sweep, range and power are design
// parameters, not recovered original-game values or measured lamp photometry.
void appendProjectionDefinitions(std::vector<Definition> &definitions) {
  for(auto &a:definitions)if(std::string(a.item)=="lightho") {
    a.sources.clear();a.sources.push_back({"lighthouse projector",{0,0,10.49f},{1,0,-.22f},{230,240,255,255},260,24,.94f,.62831853f});
  }
  for(auto &a:definitions)if(std::string(a.item)=="streetlt") {
    // measured: least-squares position from the four native lens centroids.
    // addition: stationary downward cone; aperture/power are night design.
    a.sources={{"street-light head",{1.793633f,.010352f,6.386406f},{0,0,-1},{255,226,170,255},5.5f,7,.55f,0}};
  }
  for(auto &a:definitions)if(std::string(a.item)=="asitheat") {
    // measured: the four previously tagged floor-glass regions are planters
    // with green foliage and a purple rim. They are not floodlight lenses.
    for(auto &view:a.views)view.erase(std::remove_if(view.begin(),view.end(),[](const Region &r){return r.material==Material::Glass;}),view.end());
    a.sources.clear(); // facade windows retain their separate outward sources
  }
  const char *names[]={"__moray_fluorescence","__anemone_fluorescence","__tree_coral_fluorescence"};
  const SDL_Color colour[]={{45,245,92,255},{60,228,205,255},{69,242,122,255}};
  for(int i=0;i<3;++i){Definition tissue{5001+i,names[i]};tissue.animated=true;
    for(auto &v:tissue.views)v.push_back({"excitation-dependent tissue",Material::Fluorescent,{{-500,-500},{500,-500},{500,300},{-500,300}},0,colour[i],true,false,i?PixelMask::CoralTissue:PixelMask::MorayTissue});
    definitions.push_back(std::move(tissue));}
}
// addition: green-moray observations and named fluorescent-protein reference
// materials for the generic native anemone/tree coral. The latter are graphic
// approximations, not an assertion of a species identity absent from native data.
int fluorescenceProfile(const std::string &resource) {
  if(resource=="animals/morayeel.ai")return 5001;
  if(resource=="scenery/foliage/anemone.ai")return 5002;
  if(resource=="scenery/foliage/dtcoral.ai")return 5003;return 0;
}
}

// addition: infer an outward opening on the locally reconstructed facade
// from each authored native window/sign region. Orthographic rays land on
// real mesh faces; no entire painted roof is used as a light source.
const std::vector<LightingAssets::Source> &DayNightLighting::windowSources(const LightingAssets::Model &m,const LightingAssets::Definition &a) {
  auto found=facadeSources.find(&m);if(found!=facadeSources.end())return found->second;
  using namespace LightingAssets;struct Group {Point p,n;int count=0;};Group groups[4]{};
  const float vx[]={1,1,-1,-1},vy[]={1,-1,-1,1};
  for(int v=0;v<4;++v)for(const auto &r:a.views[v]) {
    if(r.material!=Material::Interior && r.material!=Material::Sign)continue;
    if(r.outline.empty())continue;float sx=0,sy=0;for(auto p:r.outline){sx+=p.x/r.outline.size();sy+=p.y/r.outline.size();}
    Point base=v==0?Point{(sx/32+sy/16)*.5f,(sy/16-sx/32)*.5f,0}:v==1?Point{(sy/16-sx/32)*.5f,(-sx/32-sy/16)*.5f,0}:v==2?Point{(-sy/16-sx/32)*.5f,(-sy/16+sx/32)*.5f,0}:Point{(sx/32-sy/16)*.5f,(sx/32+sy/16)*.5f,0};
    Point direction{vx[v],vy[v],2},best{},normal{};float near=-1e20f;
    for(const auto &f:m.faces) {
      const auto n=f.outward;const float dot=n.x*direction.x+n.y*direction.y+n.z*2;
      if(std::abs(n.z)>.2f || dot<=.01f)continue;
      const float t=(n.x*(f.p[0].x-base.x)+n.y*(f.p[0].y-base.y)+n.z*(f.p[0].z-base.z))/dot;
      Point p{base.x+t*direction.x,base.y+t*direction.y,t*2};
      Point lo=f.p[0],hi=lo;for(auto q:f.p){lo={std::min(lo.x,q.x),std::min(lo.y,q.y),std::min(lo.z,q.z)};hi={std::max(hi.x,q.x),std::max(hi.y,q.y),std::max(hi.z,q.z)};}
      if(p.x<lo.x-.02f || p.x>hi.x+.02f || p.y<lo.y-.02f || p.y>hi.y+.02f || p.z<lo.z-.02f || p.z>hi.z+.02f || t<=near)continue;
      near=t;best=p;normal=n;
    }
    if(near< -1e19f)continue;
    const int k=std::abs(normal.x)>.5f?(normal.x>0?0:1):(normal.y>0?2:3);auto &g=groups[k];g.p.x+=best.x;g.p.y+=best.y;g.p.z+=best.z;g.n=normal;++g.count;
  }
  std::vector<Source> out;for(const auto &g:groups)if(g.count)out.push_back({"outward window light",{g.p.x/g.count+g.n.x*.08f,g.p.y/g.count+g.n.y*.08f,g.p.z/g.count},g.n,{255,215,161,255},std::min(1.4f,.45f+.12f*std::sqrt(float(g.count))),5.0f});
  return facadeSources.emplace(&m,std::move(out)).first->second;
}

// addition: a cone changes direction without rebuilding its static occlusion
// samples. Normals and distances use the renderer's existing height metric.
float DayNightLighting::sourceDirection(const Light &l,LightingAssets::Point p)const {
  const auto n=l.axis;if(n.x*n.x+n.y*n.y+n.z*n.z<.000001f)return 1;
  const float x=p.x-l.x,y=p.y-l.y,z=(p.z-l.z)*.5f,d=std::sqrt(std::max(.0001f,x*x+y*y+z*z));
  const float dot=(n.x*x+n.y*y+n.z*z)/d;
  if(l.cone<=0)return std::max(0.0f,dot);
  float t=std::clamp((dot-l.cone)/std::max(.0001f,1-l.cone),0.0f,1.0f);return t*t*(3-2*t);
}
float DayNightLighting::sourceEnergy(const Light &l,LightingAssets::Point p,LightingAssets::Point normal,uint64_t receiver,bool angular)const {
  const float dx=l.x-p.x,dy=l.y-p.y,dz=(l.z-p.z)*.5f,flat=std::hypot(dx,dy);if(flat>=l.radius)return 0;
  const float d2=dx*dx+dy*dy+dz*dz,d=std::sqrt(std::max(.01f,d2));
  const float cosine=std::max(0.0f,(normal.x*dx+normal.y*dy+normal.z*dz)/d);if(cosine<=0)return 0;
  const float emission=angular?sourceDirection(l,p):1;if(emission<=0)return 0;
  // addition: a receiver offset prevents acne without discarding its entire
  // solid. Roofs and overhangs can now shadow other parts of the same model.
  const float offset=receiver==uint64_t(-1)?0:.04f;
  const Point target{p.x+normal.x*offset,p.y+normal.y*offset,p.z+normal.z*(2*offset)};
  float visibility=0;
  if(l.cone>0 && l.sweep!=0){
    // A small physical aperture softens only the obstruction edge, not the
    // finished image. Static samples are cached, including all four corners.
    const float inverse=1/std::max(.001f,flat),ux=-dy*inverse,uy=dx*inverse;
    const float vx=-dx*dz*inverse/d,vy=-dy*dz*inverse/d,vz=flat/d;
    for(int k=0;k<4;++k){const float a=(k&1)?.08f:-.08f,b=(k&2)?.08f:-.08f;
      if(!blocked({l.x+a*ux+b*vx,l.y+a*uy+b*vy,l.z+2*b*vz},target,l.owner))visibility+=.25f;}
  }else visibility=blocked({l.x,l.y,l.z},target,l.owner)?0:1;
  if(visibility==0)return 0;
  const float t=std::clamp((l.radius-flat)/(l.radius*.15f),0.0f,1.0f);
  return visibility*l.energy*cosine*emission/std::max(.16f,d2)*t*t*(3-2*t);
}
std::array<float,3> DayNightLighting::projectedIrradiance(size_t instance,size_t face,int corner,LightingAssets::Point p,std::array<float,3> value)const {
  for(size_t b=0;b<beamLights.size();++b) {
    if(b>=beamSurface.size() || instance>=beamSurface[b].size() || face>=beamSurface[b][instance].size())continue;
    const auto &l=lights[beamLights[b]];const float energy=beamSurface[b][instance][face][corner]*sourceDirection(l,p);
    const float colour[]={l.colour.r/255.0f,l.colour.g/255.0f,l.colour.b/255.0f};
    for(int c=0;c<3;++c)value[c]=1-(1-value[c])*std::exp(-energy*colour[c]);
  }return value;
}
void DayNightLighting::prepareBeamShapes() {
  for(size_t li:beamLights) {
    const auto &l=lights[li];auto &shape=beamShapes[{l.owner,li}];if(shape.stamp==sceneHash)continue;shape.stamp=sceneHash;
    const float vertical=2*l.normal.z/std::max(.001f,std::hypot(l.normal.x,l.normal.y));
    for(int i=0;i<128;++i) {
      const float a=i*6.283185307f/128,c=std::cos(a),s=std::sin(a);float reach=l.radius;
      for(int k=1;k<=32;++k){const float d=l.radius*k/32;Point p{l.x+c*d,l.y+s*d,l.z+vertical*d};
        if(p.z<=LightingAssets::terrainHeight(*frameMap,p.x,p.y)+.03f){reach=std::max(0.0f,d-l.radius/32);break;}}
      Point end{l.x+c*reach,l.y+s*reach,l.z+vertical*reach};
      if(blocked({l.x,l.y,l.z},end,l.owner)) {float low=0,high=reach;for(int k=0;k<10;++k){float d=(low+high)*.5f;
        if(blocked({l.x,l.y,l.z},{l.x+c*d,l.y+s*d,l.z+vertical*d},l.owner))high=d;else low=d;}reach=low;}
      shape.reach[i]=reach;
    }
  }
  for(auto it=beamShapes.begin();it!=beamShapes.end();)if(it->second.stamp!=sceneHash)it=beamShapes.erase(it);else ++it;
}
void DayNightLighting::drawBeamHaze(SDL_Renderer *r,WorldRenderer &view,float scale) {
  if(sky.lamps<=.001f)return;std::vector<ZT_Vertex> vertices;std::vector<int> indices;
  for(size_t li:beamLights) {
    const auto &l=lights[li];auto found=beamShapes.find({l.owner,li});if(found==beamShapes.end())continue;
    const float centre=std::atan2(l.normal.y,l.normal.x)+float(fixtureClock*l.sweep),width=std::acos(std::clamp(l.cone,-1.0f,1.0f));
    const float vertical=2*l.normal.z/std::max(.001f,std::hypot(l.normal.x,l.normal.y));
    for(int fan=0;fan<16;++fan) {
      const float a0=centre-width+2*width*fan/16,a1=centre-width+2*width*(fan+1)/16;
      auto reach=[&](float a){const float turn=std::fmod(a/6.283185307f+1000,1.0f)*128;const int k=int(turn)%128;return std::min(found->second.reach[k],found->second.reach[(k+1)%128]);};
      const float lengths[]={reach(a0),reach(a1)},angles[]={a0,a1};
      for(int band=0;band<4;++band) {
        const int first=int(vertices.size());for(int k=0;k<4;++k){const int edge=k==0 || k==3?0:1;const float t=(k<2?band:band+1)/4.0f,d=lengths[edge]*t;
          float x,y,z;view.worldToScreenF(l.x+std::cos(angles[edge])*d,l.y+std::sin(angles[edge])*d,l.z+vertical*d,x,y,z);
          const float across=std::max(0.0f,1-std::abs((fan+edge)/8.0f-1));const Uint8 alpha=Uint8(48*sky.lamps*across*(1-t)*(.4f+.6f*t));
          vertices.push_back({{x*scale,y*scale},{l.colour.r,l.colour.g,l.colour.b,alpha},{0,0}});}
        indices.insert(indices.end(),{first,first+1,first+2,first,first+2,first+3});
      }
    }
  }
  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);if(!vertices.empty())ZT_RenderGeometry(r,nullptr,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
}
