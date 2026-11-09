#include "GpuLighting.hpp"
#include "GpuLightingShaders.hpp"
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace {
// ours: all entry points belong to the current SDL OpenGL context.
struct Gl {
  decltype(&glGetIntegerv) GetIntegerv;decltype(&glIsEnabled) IsEnabled;
  decltype(&glEnable) Enable;decltype(&glDisable) Disable;
  decltype(&glViewport) Viewport;decltype(&glScissor) Scissor;
  decltype(&glDrawElements) DrawElements;
  decltype(&glDrawArrays) DrawArrays;decltype(&glBindTexture) BindTexture;
  PFNGLACTIVETEXTUREPROC ActiveTexture;
  PFNGLGETINTEGERI_VPROC GetIntegeri_v;
  PFNGLGENBUFFERSPROC GenBuffers;PFNGLDELETEBUFFERSPROC DeleteBuffers;
  PFNGLBINDBUFFERPROC BindBuffer;PFNGLBUFFERDATAPROC BufferData;
  PFNGLBINDBUFFERBASEPROC BindBufferBase;
  PFNGLDISPATCHCOMPUTEPROC DispatchCompute;PFNGLMEMORYBARRIERPROC MemoryBarrier;
  PFNGLGENVERTEXARRAYSPROC GenVertexArrays;PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
  PFNGLBINDVERTEXARRAYPROC BindVertexArray;PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
  PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
  PFNGLCREATESHADERPROC CreateShader;PFNGLSHADERSOURCEPROC ShaderSource;
  PFNGLCOMPILESHADERPROC CompileShader;PFNGLGETSHADERIVPROC GetShaderiv;
  PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;PFNGLDELETESHADERPROC DeleteShader;
  PFNGLCREATEPROGRAMPROC CreateProgram;PFNGLATTACHSHADERPROC AttachShader;
  PFNGLLINKPROGRAMPROC LinkProgram;PFNGLGETPROGRAMIVPROC GetProgramiv;
  PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;PFNGLDELETEPROGRAMPROC DeleteProgram;
  PFNGLUSEPROGRAMPROC UseProgram;PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
  PFNGLUNIFORM1FPROC Uniform1f;PFNGLUNIFORM2FPROC Uniform2f;PFNGLUNIFORM4FPROC Uniform4f;
  PFNGLUNIFORM1IPROC Uniform1i;
  PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate;PFNGLBLENDEQUATIONSEPARATEPROC BlendEquationSeparate;
  PFNGLGENQUERIESPROC GenQueries;PFNGLDELETEQUERIESPROC DeleteQueries;
  PFNGLQUERYCOUNTERPROC QueryCounter;PFNGLGETQUERYOBJECTIVPROC GetQueryObjectiv;
  PFNGLGETQUERYOBJECTUI64VPROC GetQueryObjectui64v;
  template<class T> bool load(T &p,const char *n){p=reinterpret_cast<T>(SDL_GL_GetProcAddress(n));return p!=nullptr;}
  bool load() {
    bool ok=true;
#define LOAD(n) ok=load(n,"gl" #n)&&ok
    LOAD(GetIntegerv);LOAD(IsEnabled);LOAD(Enable);LOAD(Disable);LOAD(Viewport);LOAD(Scissor);LOAD(DrawElements);
    LOAD(DrawArrays);LOAD(BindTexture);LOAD(ActiveTexture);LOAD(Uniform1i);
    LOAD(GetIntegeri_v);LOAD(GenBuffers);LOAD(DeleteBuffers);LOAD(BindBuffer);LOAD(BufferData);LOAD(BindBufferBase);
    LOAD(DispatchCompute);LOAD(MemoryBarrier);
    LOAD(GenVertexArrays);LOAD(DeleteVertexArrays);LOAD(BindVertexArray);LOAD(EnableVertexAttribArray);LOAD(VertexAttribPointer);
    LOAD(CreateShader);LOAD(ShaderSource);LOAD(CompileShader);LOAD(GetShaderiv);LOAD(GetShaderInfoLog);LOAD(DeleteShader);
    LOAD(CreateProgram);LOAD(AttachShader);LOAD(LinkProgram);LOAD(GetProgramiv);LOAD(GetProgramInfoLog);LOAD(DeleteProgram);
    LOAD(UseProgram);LOAD(GetUniformLocation);LOAD(Uniform1f);LOAD(Uniform2f);LOAD(Uniform4f);
    LOAD(BlendFuncSeparate);LOAD(BlendEquationSeparate);
    LOAD(GenQueries);LOAD(DeleteQueries);LOAD(QueryCounter);LOAD(GetQueryObjectiv);LOAD(GetQueryObjectui64v);
#undef LOAD
    return ok;
  }
};
// ours: restore the actual state behind SDL's cache, including the VAO that
// owns SDL's compatibility vertex arrays. No SDL cached state is modified.
struct State {
  Gl &g;GLint program,vao,array,storage,ssbo[6],viewport[4],scissor[4],active,texture;bool texturing,resetSdlTexture=false;
  GLint srcRgb,dstRgb,srcAlpha,dstAlpha,eqRgb,eqAlpha;bool blend,depth,cull,clip;
  explicit State(Gl &gl):g(gl){
    g.GetIntegerv(GL_CURRENT_PROGRAM,&program);g.GetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    g.GetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);g.GetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storage);
    for(int i=0;i<6;++i)g.GetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,i,&ssbo[i]);
    g.GetIntegerv(GL_ACTIVE_TEXTURE,&active);g.ActiveTexture(GL_TEXTURE0);g.GetIntegerv(GL_TEXTURE_BINDING_2D,&texture);texturing=g.IsEnabled(GL_TEXTURE_2D)!=0;g.ActiveTexture(active);
    g.GetIntegerv(GL_VIEWPORT,viewport);g.GetIntegerv(GL_SCISSOR_BOX,scissor);
    g.GetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);g.GetIntegerv(GL_BLEND_DST_RGB,&dstRgb);
    g.GetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);g.GetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
    g.GetIntegerv(GL_BLEND_EQUATION_RGB,&eqRgb);g.GetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqAlpha);
    blend=g.IsEnabled(GL_BLEND)!=0;depth=g.IsEnabled(GL_DEPTH_TEST)!=0;
    cull=g.IsEnabled(GL_CULL_FACE)!=0;clip=g.IsEnabled(GL_SCISSOR_TEST)!=0;
  }
  ~State(){
    g.UseProgram(program);g.BindVertexArray(vao);g.BindBuffer(GL_ARRAY_BUFFER,array);
    for(int i=0;i<6;++i)g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,i,ssbo[i]);
    g.ActiveTexture(GL_TEXTURE0);g.BindTexture(GL_TEXTURE_2D,resetSdlTexture?0:texture);if(texturing && !resetSdlTexture)g.Enable(GL_TEXTURE_2D);else g.Disable(GL_TEXTURE_2D);g.ActiveTexture(active);
    g.BindBuffer(GL_SHADER_STORAGE_BUFFER,storage);
    g.BlendFuncSeparate(srcRgb,dstRgb,srcAlpha,dstAlpha);g.BlendEquationSeparate(eqRgb,eqAlpha);
    for(const auto &v:{std::pair<GLenum,bool>{GL_BLEND,blend},{GL_DEPTH_TEST,depth},{GL_CULL_FACE,cull},{GL_SCISSOR_TEST,clip}})
      if(v.second)g.Enable(v.first);else g.Disable(v.first);
    g.Viewport(viewport[0],viewport[1],viewport[2],viewport[3]);g.Scissor(scissor[0],scissor[1],scissor[2],scissor[3]);
  }
};
// Read physical bounds without the rounding caused by SDL's scaled getters.
void setView(Gl &g,SDL_Renderer *renderer,GLuint program) {
  float sx,sy;SDL_GetRenderScale(renderer,&sx,&sy);
  SDL_Rect viewport{},clip{};
  SDL_SetRenderScale(renderer,1,1);
  SDL_GetRenderViewport(renderer,&viewport);
  SDL_GetRenderClipRect(renderer,&clip);
  SDL_SetRenderScale(renderer,sx,sy);
  int width=0,height=0;SDL_GetCurrentRenderOutputSize(renderer,&width,&height);
  const bool target=SDL_GetRenderTarget(renderer)!=nullptr;
  g.Uniform2f(g.GetUniformLocation(program,"viewSize"),float(viewport.w),float(viewport.h));
  g.Uniform2f(g.GetUniformLocation(program,"renderScale"),sx,sy);
  g.Uniform1f(g.GetUniformLocation(program,"targetSign"),target?1.0f:-1.0f);
  g.Viewport(viewport.x,target?viewport.y:height-viewport.y-viewport.h,viewport.w,viewport.h);
  if(SDL_RenderClipEnabled(renderer)){
    g.Enable(GL_SCISSOR_TEST);
    g.Scissor(viewport.x+clip.x,target?viewport.y+clip.y:height-viewport.y-clip.y-clip.h,clip.w,clip.h);
  }else g.Disable(GL_SCISSOR_TEST);
}

}
struct GpuLighting::Impl {
  struct AtlasMetadata {int size=0;float x0=0,y0=0,span=0,dx=0,dy=0;double hour=-1;};
  Gl gl{};SDL_Renderer *renderer=nullptr;SDL_GLContext context=nullptr;
  bool attempted=false,ready=false,atlasAttempted=false;std::string reason="not initialized";
  GLuint program=0,surfaceProgram=0,surfaceBuffer=0,vao=0,vertices=0,maps[4]{};
  GLuint atlasProgram=0,staticMaps[4]{},casters[2]{},ranks=0;
  int mapSize[2]{},staticSize[2]{},rankSize=0;
  uint64_t staticGeneration[2]{},casterGeneration=0;
  std::array<AtlasMetadata,2> staticMetadata;
  std::array<AtlasMetadata,2> metadata;float blend=0,strength=0;
  struct GroundRange {GLuint buffer=0;uint64_t lastUse=0;std::vector<uint32_t> indices;};
  std::array<GroundRange,8> groundRanges;
  uint64_t groundGeneration=0,rangeTick=0;size_t vertexCount=0,mapBytes=0;
  struct Timing {GLuint q[2]{};bool pending=false;};std::array<Timing,8> timing{};size_t queryAt=0;
  Stats stats;
  GLuint shader(GLenum type,const std::string &source){
    auto s=gl.CreateShader(type);const char *p=source.c_str();gl.ShaderSource(s,1,&p,nullptr);gl.CompileShader(s);
    GLint ok=0;gl.GetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096]{};gl.GetShaderInfoLog(s,sizeof log,nullptr,log);reason=log;gl.DeleteShader(s);return 0;}return s;
  }
  void destroy(){
    if(ready && context==SDL_GL_GetCurrentContext()) {
      gl.DeleteProgram(program);gl.DeleteProgram(surfaceProgram);gl.DeleteBuffers(1,&surfaceBuffer);gl.DeleteVertexArrays(1,&vao);gl.DeleteBuffers(1,&vertices);gl.DeleteBuffers(4,maps);
      for(const auto &range:groundRanges)gl.DeleteBuffers(1,&range.buffer);
      if(atlasProgram)gl.DeleteProgram(atlasProgram);
      gl.DeleteBuffers(4,staticMaps);gl.DeleteBuffers(2,casters);gl.DeleteBuffers(1,&ranks);
      for(auto &t:timing)gl.DeleteQueries(2,t.q);
    }
    ready=false;
  }
  ~Impl(){destroy();}
};
GpuLighting::GpuLighting()=default;
GpuLighting::~GpuLighting()=default;
const GpuLighting::Stats &GpuLighting::stats()const{static const Stats empty;return impl?impl->stats:empty;}
const std::string &GpuLighting::reason()const{static const std::string reference="CPU reference";return impl?impl->reason:reference;}

// addition: capability failure preserves the existing renderer; CPU reference
// selection is check-only and is never stored in player settings.
bool GpuLighting::available(SDL_Renderer *renderer){
  const char *mode=SDL_getenv("ZT_LIGHTING_GPU");
  if(!mode || (std::strcmp(mode,"1") && std::strcmp(mode,"ground") && std::strcmp(mode,"surface") && std::strcmp(mode,"atlas")))return false;
  if(!impl)impl=std::make_unique<Impl>();
  auto &p=*impl;
  if(p.attempted)return p.ready && p.renderer==renderer && p.context==SDL_GL_GetCurrentContext();
  p.attempted=true;p.renderer=renderer;p.context=SDL_GL_GetCurrentContext();
  ZT_RendererInfo info{};ZT_GetRendererInfo(renderer,&info);
  if(!p.context || !info.name || std::strcmp(info.name,"opengl")){p.reason="requires SDL OpenGL renderer";return false;}
  auto &g=p.gl;if(!g.load()){p.reason="missing GL entry points";return false;}
  GLint major=0,minor=0;g.GetIntegerv(GL_MAJOR_VERSION,&major);g.GetIntegerv(GL_MINOR_VERSION,&minor);
  if(major<4 || (major==4 && minor<3)){p.reason="requires OpenGL 4.3";return false;}
  GLint vertexBlocks=0,geometryBlocks=0,storageBindings=0;
  g.GetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS,&vertexBlocks);
  g.GetIntegerv(GL_MAX_GEOMETRY_SHADER_STORAGE_BLOCKS,&geometryBlocks);
  g.GetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,&storageBindings);
  if(vertexBlocks<4 || geometryBlocks<5 || storageBindings<6){p.reason="insufficient shader storage bindings";return false;}
  if(!SDL_FlushRenderer(renderer)){p.reason="SDL could not flush the active renderer";return false;}
  State state(g);
  const auto vs=p.shader(GL_VERTEX_SHADER,std::string("#version 430 compatibility\n")+GpuLightingShaders::shadow+GpuLightingShaders::ground);
  const auto fs=p.shader(GL_FRAGMENT_SHADER,GpuLightingShaders::fragment);
  if(!vs || !fs){if(vs)g.DeleteShader(vs);if(fs)g.DeleteShader(fs);SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"[GpuLighting] CPU fallback: %s",p.reason.c_str());return false;}
  p.program=g.CreateProgram();g.AttachShader(p.program,vs);g.AttachShader(p.program,fs);g.LinkProgram(p.program);g.DeleteShader(vs);g.DeleteShader(fs);
  GLint ok=0;g.GetProgramiv(p.program,GL_LINK_STATUS,&ok);if(!ok){char log[4096]{};g.GetProgramInfoLog(p.program,sizeof log,nullptr,log);p.reason=log;g.DeleteProgram(p.program);p.program=0;return false;}
  const auto sv=p.shader(GL_VERTEX_SHADER,GpuLightingShaders::surfaceVertex);
  const auto sg=p.shader(GL_GEOMETRY_SHADER,std::string("#version 430 compatibility\n")+GpuLightingShaders::shadow+GpuLightingShaders::surfaceGeometry);
  const auto sf=p.shader(GL_FRAGMENT_SHADER,GpuLightingShaders::surfaceFragment);
  if(!sv || !sg || !sf){if(sv)g.DeleteShader(sv);if(sg)g.DeleteShader(sg);if(sf)g.DeleteShader(sf);g.DeleteProgram(p.program);p.program=0;SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"[GpuLighting] surface CPU fallback: %s",p.reason.c_str());return false;}
  p.surfaceProgram=g.CreateProgram();for(auto s:{sv,sg,sf}){g.AttachShader(p.surfaceProgram,s);}g.LinkProgram(p.surfaceProgram);for(auto s:{sv,sg,sf})g.DeleteShader(s);
  g.GetProgramiv(p.surfaceProgram,GL_LINK_STATUS,&ok);if(!ok){char log[4096]{};g.GetProgramInfoLog(p.surfaceProgram,sizeof log,nullptr,log);p.reason=log;g.DeleteProgram(p.program);g.DeleteProgram(p.surfaceProgram);p.program=p.surfaceProgram=0;return false;}
  g.GenVertexArrays(1,&p.vao);g.GenBuffers(1,&p.vertices);g.GenBuffers(4,p.maps);
  g.GenBuffers(1,&p.surfaceBuffer);for(auto m:p.maps){g.BindBuffer(GL_SHADER_STORAGE_BUFFER,m);g.BufferData(GL_SHADER_STORAGE_BUFFER,4,nullptr,GL_STREAM_DRAW);}
  for(auto &t:p.timing)g.GenQueries(2,t.q);
  g.BindVertexArray(p.vao);g.BindBuffer(GL_ARRAY_BUFFER,p.vertices);
  for(int i=0;i<3;++i){g.EnableVertexAttribArray(i);g.VertexAttribPointer(i,i==2?2:3,GL_FLOAT,GL_FALSE,sizeof(GroundPoint),reinterpret_cast<void*>(size_t(i*3)*sizeof(float)));}
  p.ready=true;p.reason="hybrid-development: GPU ground/surface receivers and clipping, CPU sun atlases/local rays";
  SDL_Log("[GpuLighting] %s",p.reason.c_str());return true;
}

// addition: temporary stage-2 bridge. The GPU performs every receiver shadow
// lookup; stage 3 will replace these uploads with GPU-generated shadow maps.
bool GpuLighting::uploadSun(SDL_Renderer *r,const LightingAssets::SunDepth (&maps)[2],float blend,float strength){
  if(!available(r))return false;auto &p=*impl;auto &g=p.gl;SDL_FlushRenderer(r);State state(g);
  p.blend=blend;p.strength=strength;p.mapBytes=0;
  for(int k=0;k<2;++k){const auto &m=maps[k];auto &d=p.metadata[k];d.size=m.size;d.x0=m.x0;d.y0=m.y0;d.span=m.span;d.dx=m.dx;d.dy=m.dy;
    if(m.depth.size()!=size_t(m.size)*m.size || m.owner.size()!=m.depth.size())return false;
    p.mapSize[k]=m.size;
    for(int t=0;t<2;++t){g.BindBuffer(GL_SHADER_STORAGE_BUFFER,p.maps[k*2+t]);const auto bytes=m.depth.size()*4;
      g.BufferData(GL_SHADER_STORAGE_BUFFER,std::max<size_t>(4,bytes),bytes?(t?static_cast<const void*>(m.owner.data()):m.depth.data()):nullptr,GL_STREAM_DRAW);
      p.stats.uploadBytes+=bytes;p.mapBytes+=bytes;}
  }
  return true;
}

// addition: build both sun samples in GPU buffers. The CPU only submits
// geometry and cache keys. Each phase is ordered before the next GPU read.
bool GpuLighting::buildSun(SDL_Renderer *r,const LightingAssets::SunDepth (&maps)[2],float blend,float strength,
    const std::vector<CasterTriangle> &fixed,const std::vector<CasterTriangle> &actors,uint64_t generation,float terrainBase){
  if(!available(r) || fixed.size()>1048576 || actors.size()>32768)return false;
  static_assert(sizeof(CasterTriangle)==64);
  auto &p=*impl;auto &g=p.gl;SDL_FlushRenderer(r);State state(g);
  if(!p.atlasProgram){
    if(p.atlasAttempted)return false;
    p.atlasAttempted=true;
    GLint bindings=0;g.GetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS,&bindings);if(bindings<6)return false;
    const GLuint shader=p.shader(GL_COMPUTE_SHADER,GpuLightingShaders::atlasCompute);
    if(!shader){SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"[GpuLighting] GPU atlas unavailable: %s",p.reason.c_str());return false;}
    const GLuint program=g.CreateProgram();g.AttachShader(program,shader);g.LinkProgram(program);g.DeleteShader(shader);
    GLint linked=0;g.GetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked){g.DeleteProgram(program);return false;}
    p.atlasProgram=program;g.GenBuffers(4,p.staticMaps);g.GenBuffers(2,p.casters);g.GenBuffers(1,&p.ranks);
  }
  const auto program=p.atlasProgram;
  auto allocate=[&](GLuint buffer,size_t bytes,const void *data,GLenum usage){
    g.BindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);g.BufferData(GL_SHADER_STORAGE_BUFFER,std::max<size_t>(4,bytes),data,usage);
  };
  auto storeMetadata=[](Impl::AtlasMetadata &dest,const LightingAssets::SunDepth &src){
    dest.size=src.size;dest.x0=src.x0;dest.y0=src.y0;dest.span=src.span;dest.dx=src.dx;dest.dy=src.dy;dest.hour=src.hour;
  };
  if(p.casterGeneration!=generation){
    allocate(p.casters[0],fixed.size()*sizeof(CasterTriangle),fixed.data(),GL_STATIC_DRAW);
    p.stats.uploadBytes+=fixed.size()*sizeof(CasterTriangle);p.casterGeneration=generation;
  }
  allocate(p.casters[1],actors.size()*sizeof(CasterTriangle),actors.data(),GL_STREAM_DRAW);
  p.stats.uploadBytes+=actors.size()*sizeof(CasterTriangle);
  p.blend=blend;p.strength=strength;p.mapBytes=0;g.UseProgram(program);
  auto integer=[&](const char *name,int value){g.Uniform1i(g.GetUniformLocation(program,name),value);};
  g.Uniform1f(g.GetUniformLocation(program,"terrainBase"),terrainBase);
  if(maps[1].hour!=maps[0].hour && p.staticMetadata[1].hour==maps[0].hour){
    std::swap(p.staticMaps[0],p.staticMaps[2]);std::swap(p.staticMaps[1],p.staticMaps[3]);
    std::swap(p.staticSize[0],p.staticSize[1]);std::swap(p.staticGeneration[0],p.staticGeneration[1]);
    std::swap(p.staticMetadata[0],p.staticMetadata[1]);
  }
  for(int k=0;k<2;++k){
    const auto &m=maps[k];storeMetadata(p.metadata[k],m);
    const unsigned pixels=unsigned(m.size)*unsigned(m.size);const size_t bytes=size_t(pixels)*4;
    if(p.rankSize<m.size){allocate(p.ranks,bytes,nullptr,GL_DYNAMIC_DRAW);p.rankSize=m.size;}
    if(p.staticSize[k]!=m.size){for(int i=0;i<2;++i)allocate(p.staticMaps[k*2+i],bytes,nullptr,GL_DYNAMIC_DRAW);p.staticSize[k]=m.size;p.staticGeneration[k]=0;}
    if(p.mapSize[k]!=m.size){for(int i=0;i<2;++i)allocate(p.maps[k*2+i],bytes,nullptr,GL_DYNAMIC_DRAW);p.mapSize[k]=m.size;}
    integer("size",m.size);
    g.Uniform4f(g.GetUniformLocation(program,"atlasParams"),m.x0,m.y0,float(m.size)/m.span,m.span);
    g.Uniform2f(g.GetUniformLocation(program,"sunDirection"),m.dx,m.dy);
    auto render=[&](bool moving,size_t count){
      integer("fromStatic",moving);integer("triCount",int(count));
      const auto output=moving?p.maps:p.staticMaps;
      for(int i=0;i<2;++i){g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,i,output[k*2+i]);g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,2+i,moving?p.staticMaps[k*2+i]:p.maps[k*2+i]);}
      g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,4,p.casters[moving?1:0]);g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,5,p.ranks);
      for(int phase=0;phase<4;++phase){
        const bool raster=phase==1 || phase==2;if(raster && !count)continue;
        integer("phase",phase);g.DispatchCompute(GLuint(((raster?count:pixels)+63)/64),raster?4:1,1);
        g.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);++p.stats.atlasDispatches;
      }
    };
    const auto &old=p.staticMetadata[k];
    if(p.staticGeneration[k]!=generation || old.hour!=m.hour || old.x0!=m.x0 || old.y0!=m.y0 || old.span!=m.span){
      render(false,fixed.size());storeMetadata(p.staticMetadata[k],m);p.staticGeneration[k]=generation;
    }
    render(true,actors.size());p.mapBytes+=bytes*4;
  }
  p.reason="hybrid-development: GPU sun atlases and receivers, CPU local lights";
  return true;
}

// addition: native ground mesh and tank-region indices, shaded on the GPU.
bool GpuLighting::ground(SDL_Renderer *r,const std::vector<GroundPoint> &v,const std::vector<uint32_t> &indices,uint64_t generation,float ox,float oy){
  if(!available(r))return false;if(indices.empty())return true;
  auto &p=*impl;auto &g=p.gl;SDL_FlushRenderer(r);State state(g);
  for(auto &t:p.timing)if(t.pending){GLint ready=0;g.GetQueryObjectiv(t.q[1],GL_QUERY_RESULT_AVAILABLE,&ready);if(ready){GLuint64 a=0,b=0;g.GetQueryObjectui64v(t.q[0],GL_QUERY_RESULT,&a);g.GetQueryObjectui64v(t.q[1],GL_QUERY_RESULT,&b);p.stats.groundGpuMs=(b-a)/1000000.0;t.pending=false;}}
  auto &t=p.timing[p.queryAt++%p.timing.size()];const bool timed=!t.pending;if(timed)g.QueryCounter(t.q[0],GL_TIMESTAMP);
  g.BindVertexArray(p.vao);g.BindBuffer(GL_ARRAY_BUFFER,p.vertices);
  if(p.groundGeneration!=generation || p.vertexCount!=v.size()){
    g.BufferData(GL_ARRAY_BUFFER,v.size()*sizeof(GroundPoint),v.data(),GL_STATIC_DRAW);p.groundGeneration=generation;p.vertexCount=v.size();p.stats.uploadBytes+=v.size()*sizeof(GroundPoint);
  }
  // Outside ground and each tank repeat the same index ranges. Keep their
  // uploads resident until geometry or tank membership changes.
  const size_t indexBytes=indices.size()*sizeof(uint32_t);
  auto found=std::find_if(p.groundRanges.begin(),p.groundRanges.end(),[&](const auto &range){
    return range.indices.size()==indices.size() && std::memcmp(range.indices.data(),indices.data(),indexBytes)==0;
  });
  if(found==p.groundRanges.end()){
    found=std::min_element(p.groundRanges.begin(),p.groundRanges.end(),[](const auto &a,const auto &b){return a.lastUse<b.lastUse;});
    if(!found->buffer)g.GenBuffers(1,&found->buffer);
    g.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,found->buffer);
    g.BufferData(GL_ELEMENT_ARRAY_BUFFER,indexBytes,indices.data(),GL_STATIC_DRAW);
    found->indices=indices;p.stats.uploadBytes+=indexBytes;
  }else g.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,found->buffer);
  found->lastUse=++p.rangeTick;
  g.UseProgram(p.program);
  for(int k=0;k<2;++k){const auto &m=p.metadata[k];const auto suffix="["+std::to_string(k)+"]";
    g.Uniform4f(g.GetUniformLocation(p.program,("atlas"+suffix).c_str()),m.x0,m.y0,m.span,float(m.size));
    g.Uniform2f(g.GetUniformLocation(p.program,("direction"+suffix).c_str()),m.dx,m.dy);}
  for(int i=0;i<4;++i)g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,i,p.maps[i]);
  g.Uniform1f(g.GetUniformLocation(p.program,"sunBlend"),p.blend);g.Uniform1f(g.GetUniformLocation(p.program,"sunStrength"),p.strength);
  g.Uniform2f(g.GetUniformLocation(p.program,"origin"),ox,oy);
  setView(g,r,p.program);
  g.Enable(GL_BLEND);g.Disable(GL_DEPTH_TEST);g.Disable(GL_CULL_FACE);g.BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
  g.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  g.DrawElements(GL_TRIANGLES,GLsizei(indices.size()),GL_UNSIGNED_INT,nullptr);++p.stats.groundDraws;
  if(timed){g.QueryCounter(t.q[1],GL_TIMESTAMP);t.pending=true;}
  p.stats.bytes=p.mapBytes+p.vertexCount*sizeof(GroundPoint);
  for(const auto &range:p.groundRanges)p.stats.bytes+=range.indices.size()*sizeof(uint32_t);
  return true;
}

// addition: shadow tests, byte colour quantization and sequential clipping
// execute in the geometry shader. Ordered native-mask batches stay intact.
bool GpuLighting::surfaces(SDL_Renderer *r,const std::vector<SurfaceFace> &faces,const std::vector<SurfaceBatch> &batches,float scale,float ox,float oy,LightingAssets::Point sun,float direct,float reference,float lamps,float strength){
  if(!available(r))return false;if(faces.empty())return true;
  static_assert(sizeof(SurfaceFace)==272);auto &p=*impl;auto &g=p.gl;SDL_FlushRenderer(r);State state(g);
  const auto program=p.surfaceProgram;g.UseProgram(program);g.BindVertexArray(p.vao);
  g.BindBuffer(GL_SHADER_STORAGE_BUFFER,p.surfaceBuffer);g.BufferData(GL_SHADER_STORAGE_BUFFER,faces.size()*sizeof(SurfaceFace),faces.data(),GL_STREAM_DRAW);g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,4,p.surfaceBuffer);
  p.stats.uploadBytes+=faces.size()*sizeof(SurfaceFace);
  for(int i=0;i<4;++i)g.BindBufferBase(GL_SHADER_STORAGE_BUFFER,i,p.maps[i]);
  for(int k=0;k<2;++k){const auto &m=p.metadata[k];const auto suffix="["+std::to_string(k)+"]";
    g.Uniform4f(g.GetUniformLocation(program,("atlas"+suffix).c_str()),m.x0,m.y0,m.span,float(m.size));
    g.Uniform2f(g.GetUniformLocation(program,("direction"+suffix).c_str()),m.dx,m.dy);}
  g.Uniform1f(g.GetUniformLocation(program,"sunBlend"),p.blend);g.Uniform1f(g.GetUniformLocation(program,"sunStrength"),strength);
  g.Uniform1f(g.GetUniformLocation(program,"surfaceScale"),scale);g.Uniform1f(g.GetUniformLocation(program,"lamps"),lamps);g.Uniform1f(g.GetUniformLocation(program,"reference"),reference);
  g.Uniform4f(g.GetUniformLocation(program,"sun"),sun.x,sun.y,sun.z,direct);g.Uniform2f(g.GetUniformLocation(program,"origin"),ox,oy);
  setView(g,r,program);
  g.Enable(GL_BLEND);g.Disable(GL_DEPTH_TEST);g.Disable(GL_CULL_FACE);g.BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);g.BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
  g.ActiveTexture(GL_TEXTURE0);g.Uniform1i(g.GetUniformLocation(program,"mask"),0);
  // Resolve all mask names before drawing. A failed bind can then fall back
  // without painting the first few receivers twice.
  std::array<std::pair<SDL_Texture *,GLuint>,4> masks{};
  SDL_Texture *lastBound=nullptr;
  auto unbind=[&]{if(lastBound){ZT_GL_UnbindTexture(lastBound);state.resetSdlTexture=true;}};
  size_t maskCount=0;
  for(const auto &batch:batches){
    const auto end=masks.begin()+maskCount;
    const auto found=std::find_if(masks.begin(),end,[&](const auto &entry){return entry.first==batch.mask;});
    if(found!=end)continue;
    if(maskCount==masks.size() || ZT_GL_BindTexture(batch.mask,nullptr,nullptr)!=0){unbind();return false;}
    lastBound=batch.mask;
    GLint name=0;g.GetIntegerv(GL_TEXTURE_BINDING_2D,&name);masks[maskCount++]={batch.mask,GLuint(name)};
  }
  unbind();
  for(const auto &batch:batches){
    const auto mask=std::find_if(masks.begin(),masks.begin()+maskCount,[&](const auto &entry){return entry.first==batch.mask;});
    g.BindTexture(GL_TEXTURE_2D,mask->second);
    g.DrawArrays(GL_POINTS,batch.first,batch.count);
    ++p.stats.surfaceDraws;
  }
  return true;
}
