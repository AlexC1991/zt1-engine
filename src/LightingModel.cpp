#include "LightingAssets.hpp"
#include "LightingCache.hpp"
#include "Animation.hpp"
#include "CompassDirection.hpp"
#include <algorithm>
#include <cmath>

namespace LightingAssets {
bool Model::contains(int x,int y,int z)const {
  return !solid.empty() && x>=0 && x<nx && y>=0 && y<ny && z>=0 && z<nz && solid[(z*ny+y)*nx+x];
}
bool Model::ray(Point from,Point to)const {
  if(solid.empty()) {
    // addition: analytic rail meshes reject rays outside their actual bounds
    // before visiting individual triangles. Independent check faces may omit
    // bounds; reconstructed structures always supply them.
    if(nx>0 && ny>0 && nz>0){const float a[]={from.x,from.y,from.z},b[]={to.x,to.y,to.z};
      const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={minimum.x+step.x*nx,minimum.y+step.y*ny,minimum.z+step.z*nz};float enter=0,leave=1;
      for(int k=0;k<3;++k){const float d=b[k]-a[k];if(std::abs(d)<1e-8f){if(a[k]<lo[k] || a[k]>hi[k])return false;}
        else {float t0=(lo[k]-a[k])/d,t1=(hi[k]-a[k])/d;if(t0>t1)std::swap(t0,t1);enter=std::max(enter,t0);leave=std::min(leave,t1);if(enter>leave)return false;}}
    }
    auto sub=[](Point a,Point b){return Point{a.x-b.x,a.y-b.y,a.z-b.z};};
    auto cross=[](Point a,Point b){return Point{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    auto dot=[](Point a,Point b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto direction=sub(to,from);
    for(const auto &face:faces)for(int triangle=0;triangle<2;++triangle) {
      const auto a=face.p[0],b=face.p[triangle?2:1],c=face.p[triangle?3:2];
      const auto edge1=sub(b,a),edge2=sub(c,a),h=cross(direction,edge2);const float determinant=dot(edge1,h);
      if(std::abs(determinant)<1e-8f)continue;
      const float inv=1/determinant;const auto offset=sub(from,a);const float u=dot(offset,h)*inv;
      if(u<0 || u>1)continue;const auto q=cross(offset,edge1);const float v=dot(direction,q)*inv;
      if(v<0 || u+v>1)continue;const float t=dot(edge2,q)*inv;if(t>.001f && t<.999f)return true;
    }
    return false;
  }
  const float a[]={from.x,from.y,from.z},b[]={to.x,to.y,to.z};
  const float lo[]={minimum.x,minimum.y,minimum.z},st[]={step.x,step.y,step.z};
  const int n[]={nx,ny,nz};float enter=0,leave=1;
  for(int k=0;k<3;++k) {
    float d=b[k]-a[k],hi=lo[k]+st[k]*n[k];
    if(std::abs(d)<1e-8f) {if(a[k]<lo[k] || a[k]>hi)return false;}
    else {float t0=(lo[k]-a[k])/d,t1=(hi-a[k])/d;if(t0>t1)std::swap(t0,t1);enter=std::max(enter,t0);leave=std::min(leave,t1);if(enter>leave)return false;}
  }
  enter=std::max(enter,.001f);leave=std::min(leave,.999f);if(enter>=leave)return false;
  int cell[3],direction[3];float next[3],stride[3];
  for(int k=0;k<3;++k) {
    float d=b[k]-a[k],p=a[k]+d*(enter+1e-6f);
    cell[k]=std::clamp(int(std::floor((p-lo[k])/st[k])),0,n[k]-1);
    direction[k]=d>=0?1:-1;
    next[k]=std::abs(d)<1e-8f?1e20f:(lo[k]+(cell[k]+(d>=0?1:0))*st[k]-a[k])/d;
    stride[k]=std::abs(d)<1e-8f?1e20f:std::abs(st[k]/d);
  }
  for(int budget=0;budget<nx+ny+nz+3;++budget) {
    if(contains(cell[0],cell[1],cell[2]))return true;
    int axis=next[0]<next[1]?(next[0]<next[2]?0:2):(next[1]<next[2]?1:2);
    if(next[axis]>leave)return false;
    cell[axis]+=direction[axis];next[axis]+=stride[axis];
    if(cell[axis]<0 || cell[axis]>=n[axis])return false;
  }
  return false;
}

// addition: intersection of the four measured orthographic silhouettes gives
// a 3D visual hull. This is a solid volume; the sun projects its surfaces,
// never stretches the 2D picture onto the ground.
struct BuildJob {
  struct View {int w,h;float ax,ay;std::vector<unsigned char> mask;};
  std::array<View,4> views{};float halfX=0,halfY=0,top=0;int assetId=0;bool actor=false;uint64_t cacheKey=0;
};
std::shared_ptr<const BuildJob> prepareBuild(Animation &art,const Definition &asset,float halfX,float halfY,int pose,bool actor,Model &cached,bool &hit) {
  hit=false;
  auto job=std::make_shared<BuildJob>();
  using View=BuildJob::View;auto &views=job->views;
  const char *names[]={"NE","SE","SW","NW"};
  const CompassDirection sides[]={CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW};
  float top=0;
  for(int k=0;k<4;++k) {
    const auto native=art.lightingFrame(sides[k],std::max(0,pose));
    SDL_Surface *s=native.surface;if(!s)return {};
    const float l=native.mirrored?native.anchor.x-s->w:-native.anchor.x,t=-native.anchor.y;
    auto &v=views[k];v.w=s->w;v.h=s->h;v.ax=-l;v.ay=-t;v.mask.resize(v.w*v.h);
    std::fill(v.mask.begin(),v.mask.end(),1);
    for(int frame=pose>=0?pose:0;frame<(pose>=0?pose+1:std::max(1,art.frameCount()));frame+=std::max(1,art.frameCount()/8)) {
      const auto nf=art.lightingFrame(sides[k],frame);auto *f=nf.surface;if(!f)continue;
      for(int y=0;y<v.h;++y)for(int x=0;x<v.w;++x) {
        const int sx=nf.mirrored?f->w-1-x:x;
        if(sx<0 || sx>=f->w || y>=f->h){v.mask[y*v.w+x]=0;continue;}
        Uint8 red,green,blue,alpha;SDL_GetRGBA(reinterpret_cast<Uint32*>(static_cast<Uint8*>(f->pixels)+y*f->pitch)[sx],f->format,&red,&green,&blue,&alpha);
        if(alpha<64)v.mask[y*v.w+x]=0;
      }
      if(nf.shadowRuns)for(uint64_t run:*nf.shadowRuns) {
        const size_t start=size_t(run>>32),count=uint32_t(run);
        for(size_t p=start;p<start+count;++p) {
          const int y=int(p/f->w),sx=int(p%f->w),x=nf.mirrored?f->w-1-sx:sx;
          if(x>=0 && x<v.w && y<v.h)v.mask[y*v.w+x]=0;
        }
      }
    }
    for(int y=0;y<v.h;++y)for(int x=0;x<v.w;++x)if(v.mask[y*v.w+x]) {
      for(const auto &material:asset.views[k])if(material.shadowCut && inside(material,x+.5f-v.ax,y+.5f-v.ay)) {
        const int sx=native.mirrored?s->w-1-x:x;Uint8 r,g,b,a;
        SDL_GetRGBA(reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(s->pixels)+y*s->pitch)[sx],s->format,&r,&g,&b,&a);
        if(pixelAllowed(material.pixels,r,g,b)){v.mask[y*v.w+x]=0;break;}
      }
      if(v.mask[y*v.w+x])top=std::max(top,v.ay-y);
    }
  }
  // addition: cache only locally reconstructed shapes. The key includes every
  // silhouette actually used, dimensions/anchors, footprint and mesh algorithm.
  uint64_t cacheKey=1469598103934665603ull;
  auto hash=[&](const void *data,size_t size){const auto *p=static_cast<const unsigned char*>(data);for(size_t n=0;n<size;++n){cacheKey^=p[n];cacheKey*=1099511628211ull;}};
  constexpr uint32_t algorithm=2;hash(&algorithm,sizeof algorithm);hash(&asset.id,sizeof asset.id);
  hash(&halfX,sizeof halfX);hash(&halfY,sizeof halfY);hash(&pose,sizeof pose);hash(&actor,sizeof actor);
  for(const auto &v:views){hash(&v.w,sizeof v.w);hash(&v.h,sizeof v.h);hash(&v.ax,sizeof v.ax);hash(&v.ay,sizeof v.ay);hash(v.mask.data(),v.mask.size());}
  job->halfX=halfX;job->halfY=halfY;job->top=top;job->assetId=asset.id;job->actor=actor;job->cacheKey=cacheKey;
  if(loadModelCache(cacheKey,cached))hit=true;
  return job;
}
Model runBuild(const BuildJob &job) {
  using View=BuildJob::View;const auto &views=job.views;
  float halfX=job.halfX,halfY=job.halfY;const float top=job.top;const bool actor=job.actor;
  Model model;
  halfX+=.08f;halfY+=.08f;
  const float floor=actor?-std::min(8.0f,halfX+halfY):0;
  const float height=std::clamp(top/16+halfX+halfY,.5f,24.0f);
  model.nx=actor?std::clamp(int(std::ceil(halfX*2/.15f)),12,28):std::clamp(int(std::ceil(halfX*2/.07f)),24,64);
  model.ny=actor?std::clamp(int(std::ceil(halfY*2/.15f)),12,28):std::clamp(int(std::ceil(halfY*2/.07f)),24,64);
  model.nz=actor?std::clamp(int(std::ceil((height-floor)/.25f)),12,48):std::clamp(int(std::ceil(height/.15f)),16,96);
  model.minimum={-halfX,-halfY,floor};model.step={halfX*2/model.nx,halfY*2/model.ny,(height-floor)/model.nz};
  model.solid.resize(model.nx*model.ny*model.nz);
  auto solidPixel=[](const View &v,float sx,float sy) {
    int x=int(std::floor(sx+v.ax)),y=int(std::floor(sy+v.ay));
    // One native pixel tolerance preserves thin posts and railings rather
    // than losing them between the voxel grid and the source pixel grid.
    for(int yy=y-1;yy<=y+1;++yy)for(int xx=x-1;xx<=x+1;++xx)
      if(xx>=0 && xx<v.w && yy>=0 && yy<v.h && v.mask[yy*v.w+xx])return true;
    return false;
  };
  for(int z=0;z<model.nz;++z)for(int y=0;y<model.ny;++y)for(int x=0;x<model.nx;++x) {
    float px=model.minimum.x+(x+.5f)*model.step.x,py=model.minimum.y+(y+.5f)*model.step.y,pz=model.minimum.z+(z+.5f)*model.step.z;
    // Camera rotations 3,2,1,0 correspond to NE,SE,SW,NW for a north-facing object.
    const float sx[]={32*(px-py),-32*(px+py),32*(-px+py),32*(px+py)};
    const float sy[]={16*(px+py)-16*pz,16*(px-py)-16*pz,-16*(px+py)-16*pz,16*(-px+py)-16*pz};
    bool occupied=true;for(int k=0;k<4;++k)if(!solidPixel(views[k],sx[k],sy[k])){occupied=false;break;}
    if(occupied && job.assetId==104) {
      // addition: the carousel is an open frame, not a solid cone. The four
      // silhouettes cannot recover the air behind painted horses, so author
      // the canopy thickness, centre mast and support ring explicitly.
      const float cx=.02f,cy=.42f,r=std::hypot(px-cx,py-cy);
      const float tent=std::hypot(px+1.43f,py+2.05f);
      const float roof=7.55f-2.03f*r;
      if(pz>.62f && tent>.72f && r<2.10f && pz<roof-.22f) {
        bool support=r<.09f;
        for(int n=0;n<16 && !support;++n) {
          const float angle=n*6.2831853f/16;
          support=std::hypot(px-cx-1.65f*std::cos(angle),py-cy-1.65f*std::sin(angle))<.075f;
        }
        occupied=support;
      }
    }
    if(occupied){model.solid[(z*model.ny+y)*model.nx+x]=1;++model.occupied;}
  }
  if(job.assetId==101) {
    // addition: authored eight-sided lamp post, glass housing and cap. A
    // small mesh keeps rows of lamps cheap while retaining their real height.
    const float profile[][2]={{0,.16f},{.10f,.16f},{.28f,.10f},{.44f,.065f},
      {3.05f,.035f},{3.28f,.13f},{3.53f,.075f},{3.63f,.11f},
      {4.02f,.11f},{4.13f,.19f},{4.44f,0}};
    for(size_t i=1;i<std::size(profile);++i)for(int n=0;n<8;++n) {
      const float a=n*6.2831853f/8,b=(n+1)*6.2831853f/8;
      const float z0=profile[i-1][0],z1=profile[i][0],r0=profile[i-1][1],r1=profile[i][1];
      Face face{{Point{r0*std::cos(a),r0*std::sin(a),z0},Point{r0*std::cos(b),r0*std::sin(b),z0},
        Point{r1*std::cos(b),r1*std::sin(b),z1},Point{r1*std::cos(a),r1*std::sin(a),z1}}};
      const float slope=(r0-r1)/((z1-z0)*.5f),length=std::sqrt(1+slope*slope);
      face.normal={std::cos((a+b)*.5f)/length,std::sin((a+b)*.5f)/length,slope/length};
      face.outward=face.normal;model.faces.push_back(face);
    }
    return model;
  }
  Model mesh;mesh.minimum=model.minimum;
  const int lod=actor?1:2;
  mesh.nx=(model.nx+lod-1)/lod;mesh.ny=(model.ny+lod-1)/lod;mesh.nz=(model.nz+lod-1)/lod;
  mesh.step={model.step.x*lod,model.step.y*lod,model.step.z*lod};
  mesh.solid.resize(mesh.nx*mesh.ny*mesh.nz);
  for(int z=0;z<mesh.nz;++z)for(int y=0;y<mesh.ny;++y)for(int x=0;x<mesh.nx;++x) {
    bool occupied=false;
    for(int dz=0;dz<lod;++dz)for(int dy=0;dy<lod;++dy)for(int dx=0;dx<lod;++dx)
      occupied=occupied || model.contains(x*lod+dx,y*lod+dy,z*lod+dz);
    mesh.solid[(z*mesh.ny+y)*mesh.nx+x]=occupied;
  }
  // Greedy surface meshing keeps broad walls and roofs in a few rectangles.
  // The geometric detail remains in the solid grid for real 3D light rays.
  const int dims[]={mesh.nx,mesh.ny,mesh.nz};
  const float origin[]={mesh.minimum.x,mesh.minimum.y,mesh.minimum.z};
  const float step[]={mesh.step.x,mesh.step.y,mesh.step.z};
  for(int axis=0;axis<3;++axis) {
    int u=(axis+1)%3,v=(axis+2)%3;std::vector<signed char> mask(dims[u]*dims[v]);
    for(int slice=0;slice<=dims[axis];++slice) {
      for(int j=0;j<dims[v];++j)for(int i=0;i<dims[u];++i) {
        int a[3]={};a[axis]=slice-1;a[u]=i;a[v]=j;
        bool before=mesh.contains(a[0],a[1],a[2]);a[axis]=slice;
        bool after=mesh.contains(a[0],a[1],a[2]);mask[j*dims[u]+i]=before==after?0:before?1:-1;
      }
      for(int j=0;j<dims[v];++j)for(int i=0;i<dims[u];) {
        int sign=mask[j*dims[u]+i];if(!sign){++i;continue;}
        int w=1,h=1;while(i+w<dims[u] && mask[j*dims[u]+i+w]==sign)++w;
        bool extend=true;while(j+h<dims[v] && extend) {
          for(int k=0;k<w;++k)if(mask[(j+h)*dims[u]+i+k]!=sign){extend=false;break;}
          if(extend)++h;
        }
        Face face{};float normal[3]={};normal[axis]=float(sign);face.normal={normal[0],normal[1],normal[2]};face.outward=face.normal;
        for(int corner=0;corner<4;++corner) {
          float p[3]={};p[axis]=origin[axis]+slice*step[axis];
          p[u]=origin[u]+(i+((corner==1 || corner==2)?w:0))*step[u];
          p[v]=origin[v]+(j+(corner>=2?h:0))*step[v];face.p[corner]={p[0],p[1],p[2]};
        }
        // Smooth the solid's density gradient for lighting. Geometry remains
        // the measured voxel surface; a sloping roof gets a sloping normal
        // instead of alternating horizontal/vertical lighting stripes.
        Point centre{};for(auto p:face.p){centre.x+=p.x*.25f;centre.y+=p.y*.25f;centre.z+=p.z*.25f;}
        // addition: evaluate a continuous Gaussian density gradient at the
        // actual surface position. Integer-cell box differences alternated
        // roof normals between stair treads and risers under a moving light.
        // Only lighting normals change; solid cells and silhouettes do not.
        const float at[]={centre.x,centre.y,centre.z},base[]={mesh.minimum.x,mesh.minimum.y,mesh.minimum.z},unit[]={mesh.step.x,mesh.step.y,mesh.step.z};
        int first[3];float weight[3][9],derivative[3][9];
        for(int axis=0;axis<3;++axis){first[axis]=int(std::floor((at[axis]-base[axis])/unit[axis]-.5f))-4;
          const float sigma=unit[axis]*1.5f*(axis==2?.5f:1);
          for(int k=0;k<9;++k){const float distance=(at[axis]-base[axis]-(first[axis]+k+.5f)*unit[axis])*(axis==2?.5f:1);
            weight[axis][k]=std::exp(-.5f*distance*distance/(sigma*sigma));derivative[axis][k]=distance/(sigma*sigma);}}
        Point gradient{};
        for(int z=0;z<9;++z)for(int y=0;y<9;++y)for(int x=0;x<9;++x)if(mesh.contains(first[0]+x,first[1]+y,first[2]+z)){
          const float w=weight[0][x]*weight[1][y]*weight[2][z];gradient.x+=w*derivative[0][x];gradient.y+=w*derivative[1][y];gradient.z+=w*derivative[2][z];}
        float length=std::sqrt(gradient.x*gradient.x+gradient.y*gradient.y+gradient.z*gradient.z);
        if(length>.001f && (gradient.x*face.normal.x+gradient.y*face.normal.y+gradient.z*face.normal.z)>length*.15f)
          face.normal={gradient.x/length,gradient.y/length,gradient.z/length};
        model.faces.push_back(face);
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)mask[(j+yy)*dims[u]+i+xx]=0;
        i+=w;
      }
    }
  }
  return model;
}
void finishBuild(const BuildJob &job,const Model &model){saveModelCache(job.cacheKey,model);}
Model build(Animation &art,const Definition &asset,float halfX,float halfY,int pose,bool actor) {
  Model model;bool hit=false;
  const auto job=prepareBuild(art,asset,halfX,halfY,pose,actor,model,hit);
  if(!job)return {};
  if(hit)return model;
  model=runBuild(*job);finishBuild(*job,model);return model;
}
}
