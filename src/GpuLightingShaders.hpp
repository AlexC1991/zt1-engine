#pragma once

// addition: the reference SunDepth filter, executed at receiver vertices.
namespace GpuLightingShaders {
// Height uses an ordered uint encoding while selecting winners. A second
// dispatch chooses the first primitive among equal heights, so height and
// owner always describe the same surface. Each row follows the CPU scanline
// arithmetic including the edge tolerance and incremental height.
inline constexpr const char *atlasCompute=R"glsl(#version 430 compatibility
layout(local_size_x=64) in;
struct Triangle {vec4 a,b,c;uvec4 owner;};
layout(std430,binding=0) buffer Heights {uint heights[];};
layout(std430,binding=1) buffer Owners {uint owners[];};
layout(std430,binding=2) readonly buffer BaseHeights {uint baseHeight[];};
layout(std430,binding=3) readonly buffer BaseOwners {uint baseOwner[];};
layout(std430,binding=4) readonly buffer Casters {Triangle triangles[];};
layout(std430,binding=5) buffer Ranks {uint ranks[];};
uniform vec4 atlasParams;
uniform vec2 sunDirection;
uniform int size,phase,triCount,fromStatic;
uniform float terrainBase;
uint ordered(float z){uint bits=floatBitsToUint(z==0.0?0.0:z);return (bits&0x80000000u)!=0u?~bits:bits^0x80000000u;}
uint unpackHeight(uint bits){return (bits&0x80000000u)==0u?~bits:bits^0x80000000u;}
vec3 project(vec3 p){precise vec2 q=(p.xy+sunDirection*p.z-atlasParams.xy)*atlasParams.z;return vec3(q,p.z);}
void main(){
  uint i=gl_GlobalInvocationID.x;
  if(phase==0 || phase==3){
    if(i>=uint(size*size))return;
    if(phase==0){heights[i]=ordered(fromStatic!=0?uintBitsToFloat(baseHeight[i]):terrainBase);ranks[i]=0xffffffffu;}
    else{heights[i]=unpackHeight(heights[i]);owners[i]=ranks[i]==0xffffffffu?(fromStatic!=0?baseOwner[i]:0u):triangles[ranks[i]].owner.x;}
    return;
  }
  if(i>=uint(triCount))return;
  Triangle tri=triangles[i];
  if(tri.owner.x!=0u && -tri.a.w*sunDirection.x-tri.b.w*sunDirection.y+tri.c.w*.5<=0.0)return;
  vec3 a=project(tri.a.xyz),b=project(tri.b.xyz),c=project(tri.c.xyz);
  precise float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
  if(abs(area)<1e-6)return;
  int l=max(0,int(floor(min(a.x,min(b.x,c.x))))),r=min(size-1,int(ceil(max(a.x,max(b.x,c.x)))));
  int top=max(0,int(floor(min(a.y,min(b.y,c.y))))),bottom=min(size-1,int(ceil(max(a.y,max(b.y,c.y)))));
  precise float dzdx=((b.z-a.z)*(c.y-a.y)-(c.z-a.z)*(b.y-a.y))/area;
  precise float dzdy=((b.x-a.x)*(c.z-a.z)-(c.x-a.x)*(b.z-a.z))/area;
  float inv=1.0/area,du=-(b.y-a.y)*inv,dv=(c.y-a.y)*inv;
  for(int y=top+int(gl_WorkGroupID.y);y<=bottom;y+=int(gl_NumWorkGroups.y)){
    float px=float(l)+.5-a.x,py=float(y)+.5-a.y;
    precise float u=((b.x-a.x)*py-(b.y-a.y)*px)*inv;
    precise float v=(px*(c.y-a.y)-py*(c.x-a.x))*inv;
    precise float z=a.z+dzdx*px+dzdy*py;
    for(int x=l;x<=r;++x,u+=du,v+=dv,z+=dzdx){
      if(u<-.00001 || v<-.00001 || u+v>1.00001)continue;
      int at=y*size+x;uint value=ordered(z);
      if(phase==1)atomicMax(heights[at],value);
      else if(value==heights[at] && value>ordered(fromStatic!=0?uintBitsToFloat(baseHeight[at]):terrainBase))atomicMin(ranks[at],i);
    }
  }
}
)glsl";
inline constexpr const char *shadow=R"glsl(
layout(std430,binding=0) readonly buffer Height0 {float height0[];};
layout(std430,binding=1) readonly buffer Owner0 {uint owner0[];};
layout(std430,binding=2) readonly buffer Height1 {float height1[];};
layout(std430,binding=3) readonly buffer Owner1 {uint owner1[];};
uniform vec4 atlas[2]; // origin XY, span, dimension
uniform vec2 direction[2];
uniform float sunBlend;
uniform float sunStrength;
float shadowAt(vec3 p,vec3 n,uint receiver,int k) {
  int size=int(atlas[k].w);if(size==0)return 0.0;
  vec2 q=(p.xy+direction[k]*p.z-atlas[k].xy)*(float(size)/atlas[k].z)-.5;
  ivec2 base=ivec2(floor(q));vec2 f=q-vec2(base);
  float bias=.10+(atlas[k].z/float(size))*(.35+length(n.xy))*2.0;
  float inv=1.0/max(.08,bias*.5),amount=0.0;
  for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
    ivec2 t=base+ivec2(x,y);if(any(lessThan(t,ivec2(0))) || any(greaterThanEqual(t,ivec2(size))))continue;
    int i=t.y*size+t.x;uint owner=k==0?owner0[i]:owner1[i];if(owner==receiver)continue;
    float z=k==0?height0[i]:height1[i];
    amount+=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y)*clamp((z-p.z-bias)*inv,0.0,1.0);
  }
  return amount;
}
float shadowPair(vec3 p,vec3 n,uint receiver) {
  return shadowAt(p,n,receiver,0)*(1.0-sunBlend)+shadowAt(p,n,receiver,1)*sunBlend;
}
)glsl";
inline constexpr const char *ground=R"glsl(
layout(location=0) in vec3 world;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 screen;
uniform vec2 origin,viewSize,renderScale;
uniform float targetSign;
out vec4 colour;
void main() {
  float a=floor(100.0*sunStrength*shadowPair(world,normal,0xffffffffu));
  colour=vec4(0,0,0,a/255.0);
  vec2 p=(screen+origin)*renderScale/viewSize;
  gl_Position=vec4(p.x*2.0-1.0,(p.y*2.0-1.0)*targetSign,0,1);
}
)glsl";
inline constexpr const char *fragment=R"glsl(#version 430 compatibility
in vec4 colour;
layout(location=0) out vec4 result;
void main(){result=colour;}
)glsl";
inline constexpr const char *surfaceVertex=R"glsl(#version 430 compatibility
flat out int faceIndex;
void main(){faceIndex=gl_VertexID;gl_Position=vec4(0,0,0,1);}
)glsl";
inline constexpr const char *surfaceGeometry=R"glsl(
layout(points) in;
layout(triangle_strip,max_vertices=24) out;
flat in int faceIndex[];
out vec4 colour;
out vec2 uv;
invariant gl_Position;
struct Face {vec4 world[4];vec4 screen[4];vec4 illumination[4];vec4 normal;vec4 ambient;vec4 bounds;vec4 maskUv;uvec4 flags;};
layout(std430,binding=4) readonly buffer SurfaceFaces {Face faces[];};
uniform vec2 origin,viewSize,renderScale;
uniform float targetSign,lamps,surfaceScale,reference;
uniform vec4 sun; // direction xyz, direct strength
struct Vertex {vec2 screen,uv;vec3 colour;float terrain;};
Vertex polygon[12],clipped[12];int count;
float distanceTo(Vertex p,int plane,vec4 bounds){
  if(plane==0)return p.terrain;
  if(plane==1)return p.screen.x-bounds.x;
  if(plane==2)return bounds.z-p.screen.x;
  if(plane==3)return p.screen.y-bounds.y;
  return bounds.w-p.screen.y;
}
void clipTo(int plane,vec4 bounds){
  if(count==0)return;int outCount=0;Vertex a=polygon[count-1];float da=distanceTo(a,plane,bounds);
  for(int k=0;k<count;++k){Vertex b=polygon[k];float db=distanceTo(b,plane,bounds);
    if((da>=0.0)!=(db>=0.0)){float t=da/(da-db);Vertex c;
      precise vec2 pos=a.screen+(b.screen-a.screen)*t;precise vec2 tex=a.uv+(b.uv-a.uv)*t;c.screen=pos;c.uv=tex;
      c.colour=floor(a.colour+(b.colour-a.colour)*t);c.terrain=a.terrain+(b.terrain-a.terrain)*t;clipped[outCount++]=c;
    }
    if(db>=0.0)clipped[outCount++]=b;a=b;da=db;
  }
  count=outCount;for(int k=0;k<count;++k)polygon[k]=clipped[k];
}
void emitPoint(int k){
  Vertex v=polygon[k];precise vec2 pos=v.screen*renderScale;
  gl_Position=gl_ModelViewProjectionMatrix*vec4(pos,0,1);
  colour=vec4(v.colour/255.0,1);uv=v.uv;EmitVertex();
}
void main(){
  Face f=faces[faceIndex[0]];count=4;
  float ratio=(.65+sun.w*max(0.0,dot(f.normal.xyz,sun.xyz)))/reference;
  precise vec4 bounds=vec4(f.bounds.xy,f.bounds.xy+f.bounds.zw)*surfaceScale;
  for(int k=0;k<4;++k){
    float occlusion=sunStrength>0.0?shadowPair(f.world[k].xyz,f.normal.xyz,f.flags.x):0.0;
    float visibility=1.0-.39*sunStrength*occlusion;
    precise vec2 xy=f.screen[k].xy+origin;
    vec2 maskUv=vec2(f.world[k].w,f.illumination[k].w);
    precise vec2 pos=xy*surfaceScale;polygon[k].screen=pos;polygon[k].uv=maskUv;polygon[k].terrain=f.screen[k].z;
    polygon[k].colour=floor(clamp(f.ambient.rgb*ratio*visibility+255.0*f.illumination[k].rgb*lamps,vec3(0),vec3(255)));
  }
  if((f.flags.w&1u)!=0u)clipTo(0,bounds);
  if((f.flags.w&2u)!=0u)for(int plane=1;plane<=4;++plane)clipTo(plane,bounds);
  for(int k=1;k+1<count;++k){emitPoint(0);emitPoint(k);emitPoint(k+1);EndPrimitive();}
}
)glsl";
inline constexpr const char *surfaceFragment=R"glsl(#version 430 compatibility
in vec4 colour;in vec2 uv;
uniform sampler2D mask;
layout(location=0) out vec4 result;
void main(){result=colour*texture(mask,uv);}
)glsl";
}
