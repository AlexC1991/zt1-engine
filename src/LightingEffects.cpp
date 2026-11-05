#include "DayNightLighting.hpp"
#include "Animation.hpp"
#include "WorldRenderer.hpp"
#include "WorldMap.hpp"
#include <algorithm>
#include <cmath>

namespace LightingAssets {
// addition: native exposure/shock poses from the local four-view effect audit.
// These are cosmetic gates only; original contact tests and AI are unchanged.
int actorEffect(const std::string &resource,const std::string &state,int frame) {
  if(resource=="guests/man" || resource=="guests/woman") {
    if(state=="camera" && frame==13)return resource=="guests/man"?1001:1002;
    if(resource=="guests/man" && state=="fencezap") {
      if((frame>=47 && frame<=67 && (frame&1)) || frame==86 || frame==90 || frame==94 || frame==98 || frame==102 || frame==106)return 1003;
    }
    return 0;
  }
  if(state!="test_fence" && state!="fencezap")return 0;
  struct Phase {const char *file;int first,last;};
  static const Phase phases[]={{"animals/allo.ai",10,27},{"animals/ankylo.ai",7,42},
    {"animals/caudipt.ai",23,54},{"animals/coelo.ai",23,54},{"animals/iguano.ai",14,38},
    {"animals/lambeo.ai",13,38},{"animals/smilo.ai",7,28},{"animals/spino.ai",11,25},
    {"animals/trex.ai",10,27},{"animals/velocira.ai",10,27}};
  for(const auto &p:phases)if(resource==p.file && frame>=p.first && frame<=p.last)return 1004;
  return 0;
}
void appendEffectDefinitions(std::vector<Definition> &definitions) {
  for(int id:{1001,1002,1003,1004}) {
    Definition a{id,id==1001?"__man_camera":id==1002?"__woman_camera":id==1003?"__guest_shock":"__animal_shock"};a.animated=true;
    if(id<=1002) {
      const float bounds[][4]={{1,-35,9,-23},{-2,-29,12,-18},{-12,-29,2,-18},{-9,-35,-1,-23}};
      for(int v=0;v<4;++v){const auto *b=bounds[v];a.views[v].push_back({"native camera flash",Material::Glass,
        {{b[0],b[1]},{b[2],b[1]},{b[2],b[3]},{b[0],b[3]}},0,{245,240,201,255},true,false,PixelMask::Camera});}
    } else for(auto &v:a.views)v.push_back({"native electrical flash and sparks",Material::Glass,
      {{-600,-600},{600,-600},{600,300},{-600,300}},0,{220,235,255,255},true,false,PixelMask::Shock});
    definitions.push_back(std::move(a));
  }
}
}

// addition: light only the playing native effect. The small transient source
// list has its own hard bound and never invalidates static irradiance caches.
void DayNightLighting::actorEmission(Animation *art,int frame,LightingAssets::Point p,float fx,float fy,uint64_t owner,int profile,float intensity) {
  if(intensity<=.001f || !art || !profile || !frameRenderer || sky.lamps<=.001f)return;
  if(!frameView)return;const auto &view=*frameView;const auto side=view.screenSide(fx,fy);
  float x,y,depth;view.worldToScreenF(p.x,p.y,p.z,x,y,depth);
  if(x< -200 || x>logicalW+200 || y< -300 || y>logicalH+200)return;
  bool visible=false;
  for(const auto &patch:art->emissionPatches(frameRenderer,side,frame,profile)) {
    if(!patch.pixels)continue;SDL_RendererFlip flip;auto box=art->emissionRect(patch,x,y,side,&flip);
    Light e{p.x,p.y,p.z,0,0,owner,0};e.distance=(x-logicalW*.5f)*(x-logicalW*.5f)+(y-logicalH*.5f)*(y-logicalH*.5f);
    e.sx=x+(flip==SDL_FLIP_HORIZONTAL?-patch.offset.x:patch.offset.x);e.sy=y+patch.offset.y;e.patch=box;e.texture=patch.texture;e.flip=flip;e.depth=depth;e.kind=profile;e.energy=std::clamp(intensity,0.0f,1.0f);emitters.push_back(e);visible=true;
  }
  if(visible && profile>=1000 && profile<=1004 && transientLights.size()<8) {
    Light l{p.x,p.y,p.z+1.5f,profile<=1002?2.2f:3.0f,1.5f,owner,0};l.energy=(profile<=1002?.9f:.5f)*intensity;l.colour=profile<=1002?SDL_Color{245,240,215,255}:SDL_Color{210,226,255,255};transientLights.push_back(l);
  }
}
