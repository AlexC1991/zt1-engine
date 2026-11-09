#include "Animation.hpp"
#include "LightingAssets.hpp"
#include "HdArt.hpp"
namespace HdArt {std::string materialKey(int);SDL_Surface *loadEmission(const std::string &,const std::string &);SDL_Surface *loadModLight(const std::string &);}
#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

// addition: the soft light round a glowing patch - its own pixels blurred a
// few pixels past it - drawn faintly over the scene at night
// (DayNightLighting::drawEmission), so a sign, a beacon or a spark lights the
// air round it, not only itself. Made once with the patch, at the art's own
// size (an HD patch scaled down to it).
static SDL_Texture *glowOf(SDL_Renderer *renderer,SDL_Surface *source,int w,int h,int pad=Animation::kGlowPad) {
  if(!renderer || !source || w<=0 || h<=0 || w>1024 || h>1024)return nullptr;
  SDL_Surface *rgba=ZT_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_RGBA32,0);if(!rgba)return nullptr;
  if(rgba->w!=w || rgba->h!=h) {
    SDL_Surface *small=ZT_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
    if(small){SDL_FillSurfaceRect(small,nullptr,0);SDL_SetSurfaceBlendMode(rgba,SDL_BLENDMODE_NONE);SDL_BlitSurfaceScaled(rgba,nullptr,small,nullptr, SDL_SCALEMODE_NEAREST);}
    SDL_DestroySurface(rgba);rgba=small;if(!rgba)return nullptr;
  }
  const int W=w+2*pad,H=h+2*pad;
  std::vector<float> a(size_t(W)*H*4,0.0f),b(a.size(),0.0f);
  for(int y=0;y<h;++y){const Uint8 *row=static_cast<const Uint8*>(rgba->pixels)+size_t(y)*rgba->pitch;
    for(int x=0;x<w;++x){const Uint8 *p=row+x*4;const float al=p[3]/255.0f;float *o=&a[(size_t(y+pad)*W+x+pad)*4];o[0]=p[0]*al;o[1]=p[1]*al;o[2]=p[2]*al;o[3]=al;}}
  SDL_DestroySurface(rgba);
  // (a box of 7 across then down, twice: near enough a soft round blur)
  auto pass=[&](const std::vector<float> &in,std::vector<float> &out,bool across) {
    const int r=std::max(2,pad/2);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
      float s[4]={0,0,0,0};
      for(int k=-r;k<=r;++k){const int xx=across?x+k:x,yy=across?y:y+k;if(xx<0 || yy<0 || xx>=W || yy>=H)continue;
        const float *p=&in[(size_t(yy)*W+xx)*4];s[0]+=p[0];s[1]+=p[1];s[2]+=p[2];s[3]+=p[3];}
      float *o=&out[(size_t(y)*W+x)*4];for(int c=0;c<4;++c)o[c]=s[c]/(2*r+1);
    }
  };
  pass(a,b,true);pass(b,a,false);pass(a,b,true);pass(b,a,false);
  SDL_Surface *out=ZT_CreateRGBSurfaceWithFormat(0,W,H,32,SDL_PIXELFORMAT_RGBA32);if(!out)return nullptr;
  for(int y=0;y<H;++y){Uint8 *row=static_cast<Uint8*>(out->pixels)+size_t(y)*out->pitch;
    for(int x=0;x<W;++x){const float *p=&a[(size_t(y)*W+x)*4];Uint8 *o=row+x*4;
      if(p[3]<=1e-4f){o[0]=o[1]=o[2]=o[3]=0;continue;}
      for(int c=0;c<3;++c)o[c]=Uint8(std::clamp(p[c]/p[3],0.0f,255.0f));
      // (thinned by the blur: lifted, so a one-pixel bulb still shows)
      o[3]=Uint8(std::min(1.0f,p[3]*1.8f)*255);}}
  SDL_Texture *t=SDL_CreateTextureFromSurface(renderer,out);SDL_DestroySurface(out);
  if(t){SDL_SetTextureBlendMode(t,SDL_BLENDMODE_ADD);SDL_SetTextureScaleMode(t,SDL_SCALEMODE_LINEAR);}
  return t;
}

void Animation::releaseLighting() {
  releaseHi(); // (its lit zoomed-in set goes with it: kept counted)
  ++textureGen;
  for(auto *set:{&textures_lit,&textures_lit_hi}) {
    for(auto &[key,frames]:*set)for(auto *t:frames)SDL_DestroyTexture(t);
    set->clear();
  }
  for(auto &[key,patches]:emissions)for(auto &p:patches){SDL_DestroyTexture(p.texture);if(p.glow)SDL_DestroyTexture(p.glow);}
  emissions.clear();
}

// addition: authored per-view material regions. Pixel colour rules apply only
// inside a named flame, lava, bulb or signal region; they never discover lamps
// from arbitrary bright scenery. A single atlas batches each catalogue item.
const std::vector<Animation::EmissionPatch> &Animation::emissionPatches(SDL_Renderer *renderer,CompassDirection side,int frame,int kind) {
  using namespace LightingAssets;
  if(kind==LampGlass)kind=101;if(kind==Flame)kind=103;if(kind==BulbStrings)kind=104;
  const auto *asset=profile(kind);const auto key=lightingDirection(side);
  auto it=surfaces.find(key);const bool dynamic=asset && asset->animated;
  const int index=dynamic && it!=surfaces.end() && !it->second.empty()?std::max(0,frame)%int(it->second.size()):0;
  // Mirrored directions may share source pixels but have distinct authored
  // stencils. Keep their material atlases separate and transform exactly once.
  const bool high=usesHi() && HdArt::active();
  const std::string generation="@"+std::to_string(HdArt::generation())+"@";
  if(!emissions.empty() && std::get<0>(emissions.begin()->first).find(generation)==std::string::npos) {
    for(auto &[k,list]:emissions)for(auto &p:list){SDL_DestroyTexture(p.texture);if(p.glow)SDL_DestroyTexture(p.glow);}emissions.clear();
  }
  auto cacheKey=std::make_tuple(key+":"+std::to_string(int(side))+generation+(high?"hd":"native"),index,kind);
  auto cached=emissions.find(cacheKey);if(cached!=emissions.end())return cached->second;
  auto &patches=emissions[cacheKey];if(!asset || it==surfaces.end() || it->second.empty() || !sourceFrame(key,size_t(index)))return patches;
  auto *source=sourceFrame(key,size_t(index));const auto anchor=anchor_points[key];const int view=viewIndex(int(side));
  const bool mirrored=renderer_flip==SDL_FLIP_HORIZONTAL,batched=kind>=200;
  if(asset->lightMap) {
    // addition: a mod item's light map (the Lab's Night light layer): a PNG
    // with the art's ground point at its middle, its colour the glow, one
    // for each frame - frame 0's serving the frames without one. Mirrored
    // views use their source's map, flipped with it (emissionRect).
    std::string base=hd_dir+"/light/"+key+"_";std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return char(std::tolower(c));});
    SDL_Surface *map=HdArt::loadModLight(base+std::to_string(index));
    if(!map && index)map=HdArt::loadModLight(base+"0");
    auto *rgba=map?ZT_ConvertSurfaceFormat(map,SDL_PIXELFORMAT_RGBA32,0):nullptr;if(map)SDL_DestroySurface(map);
    if(!rgba)return patches;
    int left=rgba->w,top=rgba->h,right=0,bottom=0,pixels=0;
    for(int y=0;y<rgba->h;++y)for(int x=0;x<rgba->w;++x)if(static_cast<Uint8*>(rgba->pixels)[y*rgba->pitch+x*4+3]){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);++pixels;}
    if(pixels) {
      SDL_Rect area{left,top,right-left,bottom-top};
      SDL_Rect box{left-rgba->w/2+anchor.x,top-rgba->h/2+anchor.y,area.w,area.h};
      auto *cropped=ZT_CreateRGBSurfaceWithFormat(0,area.w,area.h,32,SDL_PIXELFORMAT_RGBA32);SDL_Texture *texture=nullptr,*glow=nullptr;
      if(cropped){SDL_SetSurfaceBlendMode(rgba,SDL_BLENDMODE_NONE);SDL_BlitSurface(rgba,&area,cropped,nullptr);texture=SDL_CreateTextureFromSurface(renderer,cropped);if(texture)glow=glowOf(renderer,cropped,box.w,box.h);SDL_DestroySurface(cropped);}
      if(texture){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
        patches.push_back({texture,box,{box.x+box.w*.5f-anchor.x,box.y+box.h*.5f-anchor.y},{255,230,180,255},pixels,"mod light map",false,glow});}
    }
    SDL_DestroySurface(rgba);return patches;
  }
  if(high) {
    const auto path=hd_dir+"/"+key+"_"+std::to_string(index);
    const auto material="_lighting/"+HdArt::materialKey(kind)+"/"+std::to_string(kind)+"/"+std::to_string(int(side))+"/"+path;
    if(auto *baked=HdArt::loadEmission(path,material)) {
      const int scale=source->w?baked->w/source->w:0;
      if(scale>=2 && baked->w==source->w*scale && baked->h==source->h*scale) {
        // addition: retain only the lamp/window footprint on the GPU, not
        // a mostly transparent four-times building canvas for every pose.
        auto *rgba=ZT_ConvertSurfaceFormat(baked,SDL_PIXELFORMAT_RGBA32,0);
        int left=baked->w,top=baked->h,right=0,bottom=0,pixels=0;
        if(rgba)for(int y=0;y<rgba->h;++y)for(int x=0;x<rgba->w;++x)if(static_cast<Uint8*>(rgba->pixels)[y*rgba->pitch+x*4+3]){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);++pixels;}
        if(pixels) {
          SDL_Rect box{left/scale,top/scale,(right+scale-1)/scale-left/scale,(bottom+scale-1)/scale-top/scale};
          SDL_Rect area{box.x*scale,box.y*scale,box.w*scale,box.h*scale};auto *cropped=ZT_CreateRGBSurfaceWithFormat(0,area.w,area.h,32,SDL_PIXELFORMAT_RGBA32);
          SDL_Texture *texture=nullptr,*glow=nullptr;
          if(cropped){SDL_SetSurfaceBlendMode(rgba,SDL_BLENDMODE_NONE);SDL_BlitSurface(rgba,&area,cropped,nullptr);texture=SDL_CreateTextureFromSurface(renderer,cropped);if(texture)glow=glowOf(renderer,cropped,box.w,box.h);SDL_DestroySurface(cropped);}
          if(texture){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
            const bool reflective=std::all_of(asset->views[view].begin(),asset->views[view].end(),[](const Region &r){return r.material==Material::Water || r.material==Material::Steam;});
            patches.push_back({texture,box,{box.x+box.w*.5f-anchor.x,box.y+box.h*.5f-anchor.y},{255,230,180,255},std::max(1,pixels/(scale*scale)),"baked HD material",reflective,reflective?nullptr:glow});
            if(reflective && glow)SDL_DestroyTexture(glow);
            SDL_DestroySurface(rgba);SDL_DestroySurface(baked);return patches;}
        }SDL_DestroySurface(rgba);
      }SDL_DestroySurface(baked);
    }
  }
  SDL_Surface *atlas=batched?ZT_CreateRGBSurfaceWithFormat(0,source->w,source->h,32,SDL_PIXELFORMAT_RGBA32):nullptr;
  if(batched && !atlas)return patches;if(atlas)SDL_FillSurfaceRect(atlas,nullptr,0);
  const bool reflective=std::all_of(asset->views[view].begin(),asset->views[view].end(),[](const Region &r){return r.material==Material::Water || r.material==Material::Steam;});
  int atlasPixels=0,al=source->w,at=source->h,ar=0,ab=0;
  for(const auto &region:asset->views[view]) {
    if(region.material==Material::Steam || region.outline.empty())continue;
    float left=1e6f,top=1e6f,right=-1e6f,bottom=-1e6f;
    for(auto p:region.outline){left=std::min(left,p.x);top=std::min(top,p.y);right=std::max(right,p.x);bottom=std::max(bottom,p.y);}
    const float pad=region.stroke*.5f+1;
    const float sourceLeft=mirrored?anchor.x-right:anchor.x+left,sourceRight=mirrored?anchor.x-left:anchor.x+right;
    SDL_Rect box{std::max(0,int(std::floor(sourceLeft-pad))),std::max(0,int(std::floor(top+anchor.y-pad))),0,0};
    box.w=std::min(source->w,int(std::ceil(sourceRight+pad)))-box.x;
    box.h=std::min(source->h,int(std::ceil(bottom+anchor.y+pad)))-box.y;
    if(box.w<=0 || box.h<=0)continue;
    // A beacon follows its native frame while office panes in the same
    // atlas keep their reference pose. Occupants cannot flicker the windows.
    auto *regionSource=dynamic && !region.animated && it->second.front()?it->second.front():source;
    auto *surface=atlas?atlas:ZT_CreateRGBSurfaceWithFormat(0,box.w,box.h,32,SDL_PIXELFORMAT_RGBA32);if(!surface)continue;
    if(!atlas)SDL_FillSurfaceRect(surface,nullptr,0);int pixels=0;
    for(int y=0;y<box.h;++y)for(int x=0;x<box.w;++x) {
      const int sx=box.x+x,sy=box.y+y;
      const float px=mirrored?anchor.x-sx-.5f:sx+.5f-anchor.x;
      if(!inside(region,px,sy+.5f-anchor.y) || sx>=regionSource->w || sy>=regionSource->h)continue;
      Uint8 r,g,b,a;ZT_GetRGBA(reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(regionSource->pixels)+sy*regionSource->pitch)[sx],regionSource->format,&r,&g,&b,&a);
      if(!a || (!batched && r==0 && g==0 && b==0) || !pixelAllowed(region.pixels,r,g,b))continue;
      if(region.pixels==PixelMask::WhiteLamp && dynamic && it->second.size()>1) {
        // addition: reviewed native bulb strings brighten above their dark
        // sequence sample. Static white masonry and a pale aperture behind a
        // rail are not bulbs merely because the fixture stencil crosses them.
        int low=r+g+b;
        for(auto *other:it->second)if(other && sx<other->w && sy<other->h) {
          Uint8 rr,gg,bb,aa;ZT_GetRGBA(reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(other->pixels)+sy*other->pitch)[sx],other->format,&rr,&gg,&bb,&aa);
          low=std::min(low,aa?rr+gg+bb:0);
        }
        if(r+g+b-low<90)continue;
      }
      if(region.material==Material::Fluorescent) {
        const auto native=lightingFrame(side,index);bool shadow=false;const size_t offset=size_t(sy)*regionSource->w+sx;
        if(native.shadowRuns)for(auto run:*native.shadowRuns)if(offset>=size_t(run>>32) && offset<size_t(run>>32)+uint32_t(run)){shadow=true;break;}
        if(shadow)continue;const float tissue=.25f+.75f*std::max({r,g,b})/255.0f;
        r=Uint8(region.colour.r*tissue);g=Uint8(region.colour.g*tissue);b=Uint8(region.colour.b*tissue);
      }
      if(region.material==Material::Neon){r=region.colour.r;g=region.colour.g;b=region.colour.b;}
      if(region.material==Material::Interior) {
        const float transmission=(r+g+b)/(3.0f*255);
        r=Uint8(region.colour.r*(.35f+.65f*transmission));g=Uint8(region.colour.g*(.35f+.65f*transmission));b=Uint8(region.colour.b*(.35f+.65f*transmission));
      }
      const int dx=atlas?sx:x,dy=atlas?sy:y;auto *target=&reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+dy*surface->pitch)[dx];
      if(atlas){if(!*target)++atlasPixels;al=std::min(al,sx);ar=std::max(ar,sx+1);at=std::min(at,sy);ab=std::max(ab,sy+1);}
      *target=ZT_MapRGBA(surface->format,r,g,b,a);++pixels;
    }
    if(atlas)continue;
    if(!pixels){SDL_DestroySurface(surface);continue;}
    auto *texture=SDL_CreateTextureFromSurface(renderer,surface);
    const bool water=region.material==Material::Water;
    // (fire and lava: a wider, warmer light round them than a bulb's)
    const int glowPad=region.material==Material::Fire?Animation::kFireGlowPad:Animation::kGlowPad;
    SDL_Texture *glow=texture && !water?glowOf(renderer,surface,box.w,box.h,glowPad):nullptr;SDL_DestroySurface(surface);if(!texture)continue;
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
    patches.push_back({texture,box,{(box.x+box.w*.5f)-anchor.x,(box.y+box.h*.5f)-anchor.y},region.colour,pixels,region.name,water,glow,glowPad});
  }
  if(atlas) {
    // addition: ivory claws/horns must not become lights between electrical
    // flashes. Compare with this same native pose sequence before contact;
    // sparse yellow sparks remain eligible when the body is not flashing.
    if((kind==1003 || kind==1004) && index>0 && atlasPixels) {
      int baseline=0;const auto native=lightingFrame(side,0);auto *base=native.surface;
      if(base)for(int y=0;y<base->h;++y)for(int x=0;x<base->w;++x){Uint8 r,g,b,a;
        ZT_GetRGBA(reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(base->pixels)+y*base->pitch)[x],base->format,&r,&g,&b,&a);
        baseline+=a && pixelAllowed(PixelMask::Shock,r,g,b);
      }
      if(atlasPixels<=baseline*1.6f+12) {
        atlasPixels=0;al=atlas->w;at=atlas->h;ar=ab=0;
        for(int y=0;y<atlas->h;++y)for(int x=0;x<atlas->w;++x){auto *pixel=&reinterpret_cast<Uint32*>(static_cast<Uint8*>(atlas->pixels)+y*atlas->pitch)[x];Uint8 r,g,b,a;ZT_GetRGBA(*pixel,atlas->format,&r,&g,&b,&a);
          if(a && r>=230 && g>=190 && b<160){++atlasPixels;al=std::min(al,x);ar=std::max(ar,x+1);at=std::min(at,y);ab=std::max(ab,y+1);}else *pixel=0;
        }
      }
    }
    if(atlasPixels) {
      SDL_Rect box{al,at,ar-al,ab-at};auto *cropped=ZT_CreateRGBSurfaceWithFormat(0,box.w,box.h,32,SDL_PIXELFORMAT_RGBA32);
      if(cropped) {
        SDL_SetSurfaceBlendMode(atlas,SDL_BLENDMODE_NONE);SDL_BlitSurface(atlas,&box,cropped,nullptr);
        auto *texture=SDL_CreateTextureFromSurface(renderer,cropped);
        SDL_Texture *glow=texture && !reflective?glowOf(renderer,cropped,box.w,box.h):nullptr;SDL_DestroySurface(cropped);
        if(texture){SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR);
          patches.push_back({texture,box,{box.x+box.w*.5f-anchor.x,box.y+box.h*.5f-anchor.y},{255,230,180,255},atlasPixels,"mapped native surfaces",reflective,glow});}
      }
    }
    SDL_DestroySurface(atlas);
  }
  return patches;
}

SDL_FRect Animation::emissionRect(const EmissionPatch &patch,float x,float y,CompassDirection side,SDL_FlipMode *mirror) {
  const auto key=lightingDirection(side);const auto a=anchor_points[key];const bool flip=renderer_flip==SDL_FLIP_HORIZONTAL;
  if(mirror)*mirror=renderer_flip;
  return {x+(flip?a.x-patch.rect.x-patch.rect.w:patch.rect.x-a.x),y+patch.rect.y-a.y,float(patch.rect.w),float(patch.rect.h)};
}

// addition: texture payload for the optional lighting pass; excludes the
// original textures, driver padding and textures owned by other systems.
size_t Animation::lightingTextureBytes()const {
  size_t bytes=0;
  auto count=[&](SDL_Texture *t){int w=0,h=0;if(t && ZT_QueryTexture(t,nullptr,nullptr,&w,&h)==0)bytes+=size_t(w)*h*4;};
  for(const auto *set:{&textures_lit,&textures_lit_hi})for(const auto &[name,frames]:*set)for(auto *t:frames)count(t);
  for(const auto &[key,patches]:emissions)for(const auto &p:patches){count(p.texture);count(p.glow);}
  return bytes;
}

namespace LightingAssets {
bool pixelAllowed(PixelMask mask,Uint8 r,Uint8 g,Uint8 b) {
    switch(mask) {
    case PixelMask::Flame:return r>=170 && g>=65 && r>=g && g>b*1.25f;
    case PixelMask::Lava:return r>=120 && r>g*1.65f && r>b*2;
    case PixelMask::Bulb:return std::max({r,g,b})>=175 && (std::max({r,g,b})-std::min({r,g,b})>=90 || std::min({r,g,b})>=170);
    case PixelMask::Spark:return r>=200 && g>=170 && b<180;
    case PixelMask::Camera:return std::max(r,b)>=210 && g>=170;
    case PixelMask::Shock:return (r>=160 && g>=160 && b>=150 && g>=r*.86f && b>=r*.82f) || (r>=230 && g>=190 && b<160);
    case PixelMask::RedSignal:return r>=90 && r>g*1.2f;
    case PixelMask::GreenSignal:return g>=65 && g>r*1.2f;
    case PixelMask::Water:return b>=45 && g>r*1.04f && b>r*.92f;
    case PixelMask::Spray:return r>=175 && g>=175 && b>=165 && std::max({r,g,b})-std::min({r,g,b})<70;
    case PixelMask::CoralTissue:return std::max({r,g,b})>=35 && std::max({r,g,b})-std::min({r,g,b})>=12;
    case PixelMask::MorayTissue:return g>=30 && g>r*.65f && g>b*1.15f && r<200;
    case PixelMask::BlueLens:return b>=155 && b>r*1.12f && b>g*1.04f;
    case PixelMask::RedLens:return r>=180 && r>g*1.5f && r>b*1.5f;
    case PixelMask::WhiteLamp:return (r>=170 && g>=170 && b>=160 && std::max({r,g,b})-std::min({r,g,b})<85) || (r>=210 && g>=165 && b>=80 && r>g && g>b*1.2f);
    case PixelMask::Tar:return std::max({r,g,b})<80;
    default:return true;
    }
}
}

