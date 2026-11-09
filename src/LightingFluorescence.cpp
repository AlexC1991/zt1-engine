#include "DayNightLighting.hpp"
#include "Animation.hpp"
#include "WorldRenderer.hpp"
#include "RenderSettings.hpp"
#include <algorithm>
#include <cmath>

// addition: a blue-weighted RGB excitation approximation. This is not spectral
// radiometry; the species/reference papers establish the effect, not these
// artistic exposure constants. Red light, darkness and blocked lamps contribute
// nothing. The material never creates a secondary point light.
float DayNightLighting::excitationAt(LightingAssets::Point p,uint64_t receiver)const {
  float excitation=sky.sun*.035f;
  auto source=[&](const Light &l){
    const float blue=std::max(0.0f,(l.colour.b-.2f*l.colour.r-.15f*l.colour.g)/255);if(blue<=0)return;
    const float dx=l.x-p.x,dy=l.y-p.y,dz=(l.z-p.z)*.5f,d2=dx*dx+dy*dy+dz*dz;
    if(std::hypot(dx,dy)>=l.radius)return;const float direction=sourceDirection(l,p);
    if(direction<=0 || blocked({l.x,l.y,l.z},p,l.owner,receiver))return;
    const float edge=std::clamp((l.radius-std::hypot(dx,dy))/(l.radius*.15f),0.0f,1.0f);
    excitation+=sky.lamps*l.energy*blue*direction/std::max(.2f,d2)*edge*edge*(3-2*edge);
  };
  for(const auto &l:lights)source(l);for(const auto &l:transientLights)source(l);
  return std::clamp(1-std::exp(-excitation*2.8f),0.0f,.85f);
}
// addition: called inside the native drawAnchored pass, so front tank walls,
// water, scenery and its exact current strip/convex-mask clip still occlude the
// fluorescent animal. The normal world lighting subsequently attenuates it.
// addition: bounded visible material cues; ordinary sprites take one lookup.
void DayNightLighting::addFluorescence(Animation *a,float x,float y,float strength,int profile) {
  if(!a || strength<=.002f || fluorescenceCues.size()>=64 || x< -250 || x>logicalW+250 || y< -250 || y>logicalH+350)return;
  fluorescenceCues.push_back({a,x,y,strength,profile});fluorescentArt.insert(a);
}
void DayNightLighting::drawFluorescence(SDL_Renderer *r,Animation *a,float x,float y,CompassDirection side,int frame) {
  if(!a || !RenderSettings::relightWorld || !fluorescentArt.count(a))return;
  for(const auto &cue:fluorescenceCues) {
    if(cue.art!=a || std::abs(cue.x-x)>.1f || std::abs(cue.y-y)>.1f || cue.strength<=.002f)continue;
    for(const auto &patch:a->emissionPatches(r,side,frame,cue.profile)) {
      SDL_FlipMode flip;auto rect=a->emissionRect(patch,x,y,side,&flip);
      // (its soft glow round it, as strong as it's lit: the material's own
      // light spilling into the water, never a light of its own)
      if(patch.glow){const float pad=float(patch.glowPad);const SDL_FRect around{rect.x-pad,rect.y-pad,rect.w+2*pad,rect.h+2*pad};
        SDL_SetTextureAlphaMod(patch.glow,Uint8(std::clamp(170*cue.strength,0.0f,255.0f)));ZT_RenderCopyExF(r,patch.glow,nullptr,&around,0,nullptr,flip);}
      SDL_SetTextureBlendMode(patch.texture,SDL_BLENDMODE_ADD);SDL_SetTextureAlphaMod(patch.texture,Uint8(255*cue.strength));
      ZT_RenderCopyExF(r,patch.texture,nullptr,&rect,0,nullptr,flip);SDL_SetTextureAlphaMod(patch.texture,255);
    }break;
  }
}
