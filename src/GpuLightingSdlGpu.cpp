#include "GpuLightingSdlGpu.hpp"

// ours: see GpuLightingSdlGpu.hpp. GpuSurfacesD3D11.cpp and GpuSunD3D11.cpp
// step for step, on SDL_GPU.

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <cstring>

#include "GpuFsrSdlGpu.hpp"
#include "GpuFsrSdlGpuShaders.hpp"
#include "GpuUploadSdlGpu.hpp"

namespace GpuSurfacesSdlGpu {
namespace {

namespace Fsr = GpuFsr::SdlGpu;

bool g_tried = false, g_ready = false;
SDL_GPUDevice *g_dev = nullptr;
SDL_GPUComputePipeline *g_clip = nullptr;
SDL_GPUGraphicsPipeline *g_surface = nullptr, *g_ground = nullptr;
SDL_GPUSampler *g_point = nullptr;
SDL_GPURenderState *g_composite = nullptr;
// The layers' form: half floats when the card draws into them, else bytes
SDL_PixelFormat g_layerPixels = SDL_PIXELFORMAT_RGBA64_FLOAT;
SDL_GPUTextureFormat g_layerFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

constexpr Uint32 kSlots = 21; // (lighting_clip.comp.hlsl's vertices a face)
constexpr Uint32 kVertexSize = 24;

// A buffer on the graphics card and the transfer buffer that fills it
struct Buffer {
  SDL_GPUBuffer *buffer = nullptr;
  SDL_GPUTransferBuffer *transfer = nullptr;
  Uint32 capacity = 0, transferCapacity = 0;
  SDL_GPUBufferUsageFlags usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
  // At least bytes long (what it held is gone when it grows)
  bool ensure(Uint32 bytes) {
    bytes = std::max<Uint32>(bytes, 16);
    if (buffer && capacity >= bytes)
      return true;
    if (buffer)
      SDL_ReleaseGPUBuffer(g_dev, buffer);
    capacity = std::max(bytes, capacity * 2);
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = capacity;
    buffer = SDL_CreateGPUBuffer(g_dev, &info);
    if (!buffer)
      capacity = 0;
    return buffer != nullptr;
  }
  // data copied in on copy (a pass of a command buffer of ours)
  bool upload(SDL_GPUCopyPass *copy, const void *data, Uint32 bytes) {
    if (!ensure(bytes))
      return false;
    if (!bytes)
      return true;
    if (!transfer || transferCapacity < bytes) {
      if (transfer)
        SDL_ReleaseGPUTransferBuffer(g_dev, transfer);
      transferCapacity = std::max(bytes, transferCapacity * 2);
      SDL_GPUTransferBufferCreateInfo info{};
      info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      info.size = transferCapacity;
      transfer = SDL_CreateGPUTransferBuffer(g_dev, &info);
      if (!transfer) {
        transferCapacity = 0;
        return false;
      }
    }
    void *to = SDL_MapGPUTransferBuffer(g_dev, transfer, true);
    if (!to)
      return false;
    std::memcpy(to, data, bytes);
    SDL_UnmapGPUTransferBuffer(g_dev, transfer);
    SDL_GPUTransferBufferLocation from{};
    from.transfer_buffer = transfer;
    SDL_GPUBufferRegion region{};
    region.buffer = buffer;
    region.size = bytes;
    SDL_UploadToGPUBuffer(copy, &from, &region, true);
    return true;
  }
};

Buffer g_faces, g_actors, g_order, g_shapes, g_lights, g_verts, g_opacity;
uint64_t g_facesKey = ~0ull, g_lightsKey = ~0ull;
Buffer g_points, g_quads, g_groundOrder, g_live, g_pointSun;
uint64_t g_groundKey = ~0ull, g_pointSunKey = ~0ull;

// GpuSurfacesD3D11.cpp's constants (lighting_clip's cbuffer C: drawCount in
// the padding)
struct Constants {
  float origin[2], scale, lamps;
  float sun[4];
  float reference, shadow, sunBlend, pad;
  float target[2];
  uint32_t count, pad2;
};
struct GroundConstants {
  float origin[2], strength, sunBlend;
  float renderScale[2], viewSize[2];
};

// The sun maps (GpuSunD3D11.cpp's Slot and Constants)
constexpr int kTile = 32;
struct SunConstants {
  float a[2][4];
  float b[2][4];
  uint32_t opacityCount, pad[3];
};
struct Slot {
  SDL_GPUTexture *depth = nullptr, *owner = nullptr;
  int size = 0;
  uint64_t stamp = 0;
  std::vector<SDL_Rect> last;
  void drop() {
    if (depth)
      SDL_ReleaseGPUTexture(g_dev, depth);
    if (owner)
      SDL_ReleaseGPUTexture(g_dev, owner);
    depth = owner = nullptr;
    size = 0;
    stamp = 0;
    last.clear();
  }
  bool make(int s) {
    drop();
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
    info.width = info.height = Uint32(s);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    depth = SDL_CreateGPUTexture(g_dev, &info);
    info.format = SDL_GPU_TEXTUREFORMAT_R32_UINT;
    owner = SDL_CreateGPUTexture(g_dev, &info);
    if (!depth || !owner)
      return drop(), false;
    size = s;
    return true;
  }
};
Slot g_slot[2];
int g_bound[2] = {0, 1};
SunConstants g_sun{};
uint64_t g_sunFrame = ~0ull;
bool g_sunSent = false;
SDL_GPUTransferBuffer *g_sunTransfer = nullptr;
Uint32 g_sunTransferCapacity = 0;

// A layer a pass draws into, laid onto SDL's target afterwards
struct Layer {
  SDL_Texture *t = nullptr;
  int w = 0, h = 0;
  SDL_GPUTexture *gpu() const {
    return t ? static_cast<SDL_GPUTexture *>(
                   SDL_GetPointerProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr))
             : nullptr;
  }
  bool ensure(SDL_Renderer *renderer, int width, int height) {
    if (t && w == width && h == height)
      return true;
    if (t)
      SDL_DestroyTexture(t);
    t = SDL_CreateTexture(renderer, g_layerPixels, SDL_TEXTUREACCESS_TARGET, width, height);
    w = width;
    h = height;
    if (t) {
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    }
    return t != nullptr;
  }
};
Layer g_surfaceLayer, g_groundLayer;

SDL_GPUShader *shader(const GpuFsr::SdlGpu::Code &code, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 storageTextures,
                      Uint32 storageBuffers, Uint32 uniforms, const char *what) {
  const bool dxil = Fsr::shaderFormat() == SDL_GPU_SHADERFORMAT_DXIL;
  SDL_GPUShaderCreateInfo info{};
  info.code = dxil ? code.dxil : code.spv;
  info.code_size = dxil ? code.dxilSize : code.spvSize;
  info.entrypoint = "main";
  info.format = Fsr::shaderFormat();
  info.stage = stage;
  info.num_samplers = samplers;
  info.num_storage_textures = storageTextures;
  info.num_storage_buffers = storageBuffers;
  info.num_uniform_buffers = uniforms;
  SDL_GPUShader *s = SDL_CreateGPUShader(g_dev, &info);
  if (!s)
    SDL_Log("[GpuSurfaces] SDL GPU %s shader: %s", what, SDL_GetError());
  return s;
}

// (as SDL's SDL_BLENDMODE_BLEND, GpuSurfacesD3D11's g_blend)
SDL_GPUColorTargetDescription blendTarget() {
  SDL_GPUColorTargetDescription d{};
  d.format = g_layerFormat;
  d.blend_state.enable_blend = true;
  d.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  d.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  d.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  d.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  d.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  d.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  return d;
}

SDL_GPUGraphicsPipeline *pipeline(SDL_GPUShader *vs, SDL_GPUShader *fs, bool vertices) {
  if (!vs || !fs)
    return nullptr;
  SDL_GPUGraphicsPipelineCreateInfo info{};
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  SDL_GPUVertexBufferDescription vb{};
  SDL_GPUVertexAttribute attrs[3]{};
  if (vertices) {
    vb.slot = 0;
    vb.pitch = kVertexSize;
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0};
    attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8};
    attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16};
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 3;
  }
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  info.rasterizer_state.enable_depth_clip = true;
  const SDL_GPUColorTargetDescription target = blendTarget();
  info.target_info.color_target_descriptions = &target;
  info.target_info.num_color_targets = 1;
  SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(g_dev, &info);
  if (!p)
    SDL_Log("[GpuSurfaces] SDL GPU pipeline: %s", SDL_GetError());
  return p;
}

bool setup(SDL_Renderer *renderer) {
  g_dev = Fsr::device();
  if (!g_dev)
    return false;
  // (the layers: half floats if the card blends into them, as it should)
  g_layerFormat = SDL_GetGPUTextureFormatFromPixelFormat(SDL_PIXELFORMAT_RGBA64_FLOAT);
  if (!SDL_GPUTextureSupportsFormat(g_dev, g_layerFormat, SDL_GPU_TEXTURETYPE_2D,
                                    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
    g_layerPixels = SDL_PIXELFORMAT_ARGB8888;
    g_layerFormat = SDL_GetGPUTextureFormatFromPixelFormat(g_layerPixels);
  }
  const auto storage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
  if (!SDL_GPUTextureSupportsFormat(g_dev, SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTURETYPE_2D, storage) ||
      !SDL_GPUTextureSupportsFormat(g_dev, SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTURETYPE_2D, storage)) {
    SDL_Log("[GpuSurfaces] the card can't read the sun maps as storage textures");
    return false;
  }
  const bool dxil = Fsr::shaderFormat() == SDL_GPU_SHADERFORMAT_DXIL;
  const GpuFsr::SdlGpu::Code &clip = GpuFsr::SdlGpu::kLightingClipCode;
  SDL_GPUComputePipelineCreateInfo ci{};
  ci.code = dxil ? clip.dxil : clip.spv;
  ci.code_size = dxil ? clip.dxilSize : clip.spvSize;
  ci.entrypoint = "main";
  ci.format = Fsr::shaderFormat();
  ci.num_readonly_storage_textures = 4;
  ci.num_readonly_storage_buffers = 6;
  ci.num_readwrite_storage_buffers = 1;
  ci.num_uniform_buffers = 2;
  ci.threadcount_x = 64;
  ci.threadcount_y = 1;
  ci.threadcount_z = 1;
  g_clip = SDL_CreateGPUComputePipeline(g_dev, &ci);
  if (!g_clip)
    SDL_Log("[GpuSurfaces] SDL GPU clip pass: %s", SDL_GetError());
  SDL_GPUShader *surfaceVs = shader(GpuFsr::SdlGpu::kLightingSurfaceVertCode, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, 0, 0,
                                    "surface vertex");
  SDL_GPUShader *surfaceFs = shader(GpuFsr::SdlGpu::kLightingSurfaceFragCode, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, 0, 0,
                                    "surface pixel");
  SDL_GPUShader *groundVs = shader(GpuFsr::SdlGpu::kLightingGroundVertCode, SDL_GPU_SHADERSTAGE_VERTEX, 0, 4, 6, 2,
                                   "ground vertex");
  SDL_GPUShader *groundFs = shader(GpuFsr::SdlGpu::kLightingGroundFragCode, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0,
                                   "ground pixel");
  g_surface = pipeline(surfaceVs, surfaceFs, true);
  g_ground = pipeline(groundVs, groundFs, false);
  for (SDL_GPUShader *s : {surfaceVs, surfaceFs, groundVs, groundFs})
    if (s)
      SDL_ReleaseGPUShader(g_dev, s);
  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  g_point = SDL_CreateGPUSampler(g_dev, &si);
  g_composite = Fsr::renderState(renderer, GpuFsr::SdlGpu::kLightingCompositeCode, 1, 0, "composite");
  g_verts.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
  // (something bound in every slot from the start - before any sun map, or
  // with the sun down, the maps are 1 x 1 and size 0, so never read)
  const bool sunReady = g_slot[0].make(1) && g_slot[1].make(1) && g_opacity.ensure(16);
  return sunReady && g_clip && g_surface && g_ground && g_point && g_composite;
}

// A target's viewport, clip and scale set aside for drawing in its pixels
struct View {
  SDL_Rect view{}, clip{};
  bool clipped = false;
  float sx = 1.0f, sy = 1.0f;
};
View keepView(SDL_Renderer *renderer) {
  View v;
  SDL_GetRenderViewport(renderer, &v.view);
  v.clipped = SDL_RenderClipEnabled(renderer);
  SDL_GetRenderClipRect(renderer, &v.clip);
  SDL_GetRenderScale(renderer, &v.sx, &v.sy);
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
  SDL_SetRenderViewport(renderer, nullptr);
  SDL_SetRenderClipRect(renderer, nullptr);
  return v;
}
void restoreView(SDL_Renderer *renderer, const View &v) {
  SDL_SetRenderViewport(renderer, &v.view);
  SDL_SetRenderClipRect(renderer, v.clipped ? &v.clip : nullptr);
  SDL_SetRenderScale(renderer, v.sx, v.sy);
}

// The layer onto SDL's current target, texel for texel (premultiplied); the
// draw recorded at once, so the layer can be drawn into again
void composite(SDL_Renderer *renderer, const Layer &layer) {
  const View v = keepView(renderer);
  const SDL_FRect all{0, 0, float(layer.w), float(layer.h)};
  SDL_SetGPURenderState(renderer, g_composite);
  SDL_RenderTexture(renderer, layer.t, &all, &all);
  SDL_SetGPURenderState(renderer, nullptr);
  restoreView(renderer, v);
  SDL_FlushRenderer(renderer);
}

// A copy pass of its own, submitted at once (the faces, points and sun maps
// sent when they change)
template <typename Fn> bool sendNow(Fn fn) {
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_dev);
  if (!cmd)
    return false;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  const bool ok = fn(copy);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  return ok;
}

void bindSun(SDL_GPUTexture *(&textures)[4]) {
  textures[0] = g_slot[g_bound[0]].depth;
  textures[1] = g_slot[g_bound[0]].owner;
  textures[2] = g_slot[g_bound[1]].depth;
  textures[3] = g_slot[g_bound[1]].owner;
}

SDL_GPUTexture *gpuTextureOf(SDL_Texture *t) {
  return t ? static_cast<SDL_GPUTexture *>(
                 SDL_GetPointerProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr))
           : nullptr;
}

} // namespace

bool available(SDL_Renderer *renderer) {
  if (g_tried)
    return g_ready;
  g_tried = true;
  const char *cpu = SDL_getenv("ZT_LIGHTING_CPU");
  if (cpu && *cpu && std::strcmp(cpu, "0") != 0)
    return false;
  ZT_RendererInfo info{};
  if (!renderer || ZT_GetRendererInfo(renderer, &info) != 0 || std::strcmp(info.name, "gpu") != 0)
    return false;
  g_ready = setup(renderer);
  SDL_Log("[GpuSurfaces] receiving surfaces on the graphics card (SDL GPU, %s layers): %s",
          g_layerPixels == SDL_PIXELFORMAT_RGBA64_FLOAT ? "float" : "byte", g_ready ? "yes" : "no (CPU)");
  return g_ready;
}

void setFaces(const std::vector<Face> &faces, uint64_t key) {
  if (!g_ready)
    return;
  const bool ok = sendNow([&](SDL_GPUCopyPass *copy) {
    return g_faces.upload(copy, faces.data(), Uint32(faces.size() * sizeof(Face)));
  });
  g_facesKey = ok ? key : ~0ull;
}

uint64_t facesKey() { return g_facesKey; }

void setFaceLight(const std::vector<FaceLight> &light, uint64_t key) {
  if (!g_ready)
    return;
  const bool ok = sendNow([&](SDL_GPUCopyPass *copy) {
    return g_lights.upload(copy, light.data(), Uint32(light.size() * sizeof(FaceLight)));
  });
  g_lightsKey = ok ? key : ~0ull;
}

uint64_t faceLightKey() { return g_lightsKey; }

bool draw(SDL_Renderer *renderer, SDL_Texture *mask, const std::vector<Draw> &order, const std::vector<Face> &actorFaces,
          const std::vector<Shape> &shapes, const Frame &frame) {
  if (!g_ready || !g_faces.buffer || !g_lights.buffer || g_lightsKey == ~0ull)
    return false;
  if (order.empty())
    return true;
  SDL_GPUTexture *maskTexture = gpuTextureOf(mask);
  // (the mask atlas's new pixels, sent with the engine's uploads: ahead of
  // this pass - GpuSurfaces::updateMask)
  GpuUploadSdlGpu::flush(renderer);
  if (!maskTexture || !g_surfaceLayer.ensure(renderer, frame.targetW, frame.targetH))
    return false;
  SDL_GPUTexture *layer = g_surfaceLayer.gpu();
  const Uint32 count = Uint32(order.size());
  if (!layer || !g_verts.ensure(count * kSlots * kVertexSize))
    return false;
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_dev);
  if (!cmd)
    return false;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  const bool sent = g_actors.upload(copy, actorFaces.data(), Uint32(actorFaces.size() * sizeof(Face))) &&
                    g_order.upload(copy, order.data(), Uint32(order.size() * sizeof(Draw))) &&
                    g_shapes.upload(copy, shapes.data(), Uint32(shapes.size() * sizeof(Shape)));
  SDL_EndGPUCopyPass(copy);
  if (!sent) {
    SDL_CancelGPUCommandBuffer(cmd);
    return false;
  }
  Constants c{};
  c.origin[0] = frame.originX;
  c.origin[1] = frame.originY;
  c.scale = frame.scale;
  c.lamps = frame.lamps;
  c.sun[0] = frame.sun[0];
  c.sun[1] = frame.sun[1];
  c.sun[2] = frame.sun[2];
  c.sun[3] = frame.direct;
  c.reference = frame.reference;
  c.shadow = frame.shadow;
  c.sunBlend = frame.sunBlend;
  c.target[0] = float(frame.targetW);
  c.target[1] = float(frame.targetH);
  c.count = count;
  // The faces clipped into their vertex slots
  SDL_GPUStorageBufferReadWriteBinding out{};
  out.buffer = g_verts.buffer;
  out.cycle = true;
  SDL_GPUComputePass *compute = SDL_BeginGPUComputePass(cmd, nullptr, 0, &out, 1);
  SDL_BindGPUComputePipeline(compute, g_clip);
  SDL_GPUTexture *sun[4];
  bindSun(sun);
  SDL_BindGPUComputeStorageTextures(compute, 0, sun, 4);
  SDL_GPUBuffer *buffers[6] = {g_opacity.buffer, g_faces.buffer, g_actors.buffer,
                               g_order.buffer,   g_shapes.buffer, g_lights.buffer};
  SDL_BindGPUComputeStorageBuffers(compute, 0, buffers, 6);
  SDL_PushGPUComputeUniformData(cmd, 0, &c, sizeof(c));
  SDL_PushGPUComputeUniformData(cmd, 1, &g_sun, sizeof(g_sun));
  SDL_DispatchGPUCompute(compute, (count + 63) / 64, 1, 1);
  SDL_EndGPUComputePass(compute);
  // Then drawn, in order, into the layer
  SDL_GPUColorTargetInfo target{};
  target.texture = layer;
  target.clear_color = SDL_FColor{0, 0, 0, 0};
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(pass, g_surface);
  const SDL_GPUViewport vp{0, 0, float(frame.targetW), float(frame.targetH), 0.0f, 1.0f};
  SDL_SetGPUViewport(pass, &vp);
  SDL_GPUBufferBinding vb{g_verts.buffer, 0};
  SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
  SDL_GPUTextureSamplerBinding sampler{maskTexture, g_point};
  SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
  SDL_DrawGPUPrimitives(pass, count * kSlots, 1, 0, 0);
  SDL_EndGPURenderPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  composite(renderer, g_surfaceLayer);
  return true;
}

void setGround(const std::vector<GroundPoint> &points, const std::vector<std::array<uint32_t, 4>> &quads, uint64_t key) {
  if (!g_ready)
    return;
  const bool ok = sendNow([&](SDL_GPUCopyPass *copy) {
    return g_points.upload(copy, points.data(), Uint32(points.size() * sizeof(GroundPoint))) &&
           g_quads.upload(copy, quads.data(), Uint32(quads.size() * sizeof(uint32_t) * 4));
  });
  g_groundKey = ok ? key : ~0ull;
}

uint64_t groundKey() { return g_groundKey; }

void setGroundSun(const std::vector<std::array<float, 2>> &sun, uint64_t key) {
  if (!g_ready)
    return;
  const bool ok = sendNow([&](SDL_GPUCopyPass *copy) {
    return g_pointSun.upload(copy, sun.data(), Uint32(sun.size() * sizeof(float) * 2));
  });
  g_pointSunKey = ok ? key : ~0ull;
}

uint64_t groundSunKey() { return g_pointSunKey; }

bool setGroundLive(const std::vector<uint32_t> &bits) {
  return g_ready && sendNow([&](SDL_GPUCopyPass *copy) {
           return g_live.upload(copy, bits.data(), Uint32(bits.size() * sizeof(uint32_t)));
         });
}

bool drawGround(SDL_Renderer *renderer, const std::vector<uint32_t> &quads, const GroundFrame &frame) {
  if (!g_ready || !g_points.buffer || !g_quads.buffer || !g_live.buffer || !g_pointSun.buffer ||
      g_groundKey == ~0ull || g_pointSunKey == ~0ull)
    return false;
  if (quads.empty())
    return true;
  int outW = 0, outH = 0;
  SDL_GetCurrentRenderOutputSize(renderer, &outW, &outH);
  if (outW <= 0 || outH <= 0 || !g_groundLayer.ensure(renderer, outW, outH))
    return false;
  SDL_GPUTexture *layer = g_groundLayer.gpu();
  if (!layer)
    return false;
  // (SDL's own view and clip in its target's pixels, as SDL works them out:
  // the Direct3D 11 pass draws under the view and scissor SDL left bound)
  SDL_Rect view{}, clip{};
  SDL_GetRenderViewport(renderer, &view);
  float sx = 1, sy = 1;
  SDL_GetRenderScale(renderer, &sx, &sy);
  const SDL_Rect px{int(SDL_floorf(view.x * sx)), int(SDL_floorf(view.y * sy)), int(SDL_ceilf(view.w * sx)),
                    int(SDL_ceilf(view.h * sy))};
  if (px.w <= 0 || px.h <= 0)
    return false;
  const bool clipped = SDL_RenderClipEnabled(renderer);
  SDL_GetRenderClipRect(renderer, &clip);
  SDL_Rect scissor{0, 0, outW, outH};
  if (clipped) {
    const SDL_Rect pc{px.x + int(SDL_floorf(clip.x * sx)), px.y + int(SDL_floorf(clip.y * sy)), int(SDL_ceilf(clip.w * sx)),
                      int(SDL_ceilf(clip.h * sy))};
    const SDL_Rect whole{0, 0, outW, outH};
    if (!SDL_GetRectIntersection(&pc, &whole, &scissor))
      return true;
  }
  GroundConstants c{};
  c.origin[0] = frame.originX;
  c.origin[1] = frame.originY;
  c.strength = frame.strength;
  c.sunBlend = frame.sunBlend;
  c.renderScale[0] = sx;
  c.renderScale[1] = sy;
  c.viewSize[0] = float(px.w);
  c.viewSize[1] = float(px.h);
  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_dev);
  if (!cmd)
    return false;
  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
  const bool sent = g_groundOrder.upload(copy, quads.data(), Uint32(quads.size() * sizeof(uint32_t)));
  SDL_EndGPUCopyPass(copy);
  if (!sent) {
    SDL_CancelGPUCommandBuffer(cmd);
    return false;
  }
  SDL_GPUColorTargetInfo target{};
  target.texture = layer;
  target.clear_color = SDL_FColor{0, 0, 0, 0};
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.cycle = true;
  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(pass, g_ground);
  const SDL_GPUViewport vp{float(px.x), float(px.y), float(px.w), float(px.h), 0.0f, 1.0f};
  SDL_SetGPUViewport(pass, &vp);
  SDL_SetGPUScissor(pass, &scissor);
  SDL_GPUTexture *sun[4];
  bindSun(sun);
  SDL_BindGPUVertexStorageTextures(pass, 0, sun, 4);
  SDL_GPUBuffer *buffers[6] = {g_opacity.buffer,     g_points.buffer, g_quads.buffer,
                               g_groundOrder.buffer, g_live.buffer,   g_pointSun.buffer};
  SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, 6);
  SDL_PushGPUVertexUniformData(cmd, 0, &c, sizeof(c));
  SDL_PushGPUVertexUniformData(cmd, 1, &g_sun, sizeof(g_sun));
  SDL_DrawGPUPrimitives(pass, Uint32(quads.size()) * 6, 1, 0, 0);
  SDL_EndGPURenderPass(pass);
  SDL_SubmitGPUCommandBuffer(cmd);
  composite(renderer, g_groundLayer);
  static bool said = false;
  if (!said) {
    said = true;
    SDL_Log("[GpuSurfaces] the ground's shadow on the graphics card (SDL GPU): %u quads", unsigned(quads.size()));
  }
  return true;
}

bool uploadSun(const LightingAssets::SunDepth (&maps)[2], const std::vector<float> *opacity, uint64_t frame) {
  if (!g_ready)
    return false;
  if (frame == g_sunFrame)
    return g_sunSent;
  // (a frame went by unsent: what the card holds no longer matches the maps)
  if (frame != g_sunFrame + 1)
    for (auto &s : g_slot)
      s.stamp = 0;
  g_sunFrame = frame;
  g_sunSent = false;
  // (each map in the slot already holding it, when one does - as GpuSunD3D11)
  int at[2] = {-1, -1};
  for (int k = 0; k < 2; ++k)
    for (int s = 0; s < 2; ++s)
      if (maps[k].stamp && g_slot[s].stamp == maps[k].stamp && g_slot[s].size == maps[k].size && at[1 - k] != s)
        at[k] = s;
  for (int k = 0; k < 2; ++k)
    if (at[k] < 0)
      at[k] = at[1 - k] >= 0 ? 1 - at[1 - k] : k;
  // The regions to send (slot, map, x, y, w, h), worked out as GpuSunD3D11's
  struct Region {
    int slot, map, x, y, w, h;
  };
  std::vector<Region> regions;
  for (int k = 0; k < 2; ++k) {
    const auto &m = maps[k];
    auto &slot = g_slot[at[k]];
    g_bound[k] = at[k];
    auto &a = g_sun.a[k];
    auto &b = g_sun.b[k];
    const bool usable = m.size > 0 && m.depth.size() == size_t(m.size) * m.size && m.owner.size() == m.depth.size();
    a[0] = m.x0;
    a[1] = m.y0;
    a[2] = m.pixelScale;
    a[3] = usable ? float(m.size) : 0.0f;
    b[0] = m.dx;
    b[1] = m.dy;
    b[2] = m.span;
    b[3] = m.soften;
    if (!usable) {
      if (!slot.depth && !slot.make(1))
        return false;
      slot.stamp = 0;
      continue;
    }
    if (slot.size != m.size && !slot.make(m.size))
      return false;
    if (slot.stamp != m.stamp) {
      regions.push_back({at[k], k, 0, 0, m.size, m.size});
      slot.stamp = m.stamp;
    } else {
      const int tiles = (m.size + kTile - 1) / kTile;
      std::vector<uint8_t> dirty(size_t(tiles) * tiles, 0);
      for (const std::vector<SDL_Rect> *list : {static_cast<const std::vector<SDL_Rect> *>(&slot.last), &m.actorRegions})
        for (const auto &r : *list) {
          const int x0 = std::max(0, r.x / kTile), y0 = std::max(0, r.y / kTile);
          const int x1 = std::min(tiles - 1, (r.x + r.w - 1) / kTile), y1 = std::min(tiles - 1, (r.y + r.h - 1) / kTile);
          for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
              dirty[size_t(y) * tiles + x] = 1;
        }
      for (int y = 0; y < tiles; ++y)
        for (int x = 0; x < tiles;) {
          if (!dirty[size_t(y) * tiles + x]) {
            ++x;
            continue;
          }
          int end = x;
          while (end < tiles && dirty[size_t(y) * tiles + end])
            ++end;
          const int px = x * kTile, py = y * kTile;
          regions.push_back({at[k], k, px, py, std::min(m.size, end * kTile) - px, std::min(m.size, py + kTile) - py});
          x = end;
        }
    }
    slot.last = m.actorRegions;
  }
  g_sun.opacityCount = opacity ? uint32_t(opacity->size()) : 0;
  // Everything in one transfer buffer: each region's depth rows, then its owners
  size_t bytes = 0;
  for (const Region &r : regions)
    bytes += size_t(r.w) * r.h * 8;
  if (bytes && (!g_sunTransfer || g_sunTransferCapacity < bytes)) {
    if (g_sunTransfer)
      SDL_ReleaseGPUTransferBuffer(g_dev, g_sunTransfer);
    g_sunTransferCapacity = Uint32(std::max(bytes, size_t(g_sunTransferCapacity) * 2));
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = g_sunTransferCapacity;
    g_sunTransfer = SDL_CreateGPUTransferBuffer(g_dev, &info);
    if (!g_sunTransfer) {
      g_sunTransferCapacity = 0;
      return false;
    }
  }
  const bool ok = sendNow([&](SDL_GPUCopyPass *copy) {
    if (bytes) {
      auto *to = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(g_dev, g_sunTransfer, true));
      if (!to)
        return false;
      size_t offset = 0;
      std::vector<size_t> starts;
      for (const Region &r : regions) {
        const auto &m = maps[r.map];
        starts.push_back(offset);
        for (int row = 0; row < r.h; ++row) {
          std::memcpy(to + offset, m.depth.data() + size_t(r.y + row) * m.size + r.x, size_t(r.w) * 4);
          offset += size_t(r.w) * 4;
        }
        for (int row = 0; row < r.h; ++row) {
          std::memcpy(to + offset, m.owner.data() + size_t(r.y + row) * m.size + r.x, size_t(r.w) * 4);
          offset += size_t(r.w) * 4;
        }
      }
      SDL_UnmapGPUTransferBuffer(g_dev, g_sunTransfer);
      for (size_t i = 0; i < regions.size(); ++i) {
        const Region &r = regions[i];
        const auto &m = maps[r.map];
        const bool whole = r.x == 0 && r.y == 0 && r.w == m.size && r.h == m.size;
        for (int plane = 0; plane < 2; ++plane) {
          SDL_GPUTextureTransferInfo from{};
          from.transfer_buffer = g_sunTransfer;
          from.offset = Uint32(starts[i] + size_t(plane) * r.w * r.h * 4);
          from.pixels_per_row = Uint32(r.w);
          from.rows_per_layer = Uint32(r.h);
          SDL_GPUTextureRegion to2{};
          to2.texture = plane ? g_slot[r.slot].owner : g_slot[r.slot].depth;
          to2.x = Uint32(r.x);
          to2.y = Uint32(r.y);
          to2.w = Uint32(r.w);
          to2.h = Uint32(r.h);
          to2.d = 1;
          SDL_UploadToGPUTexture(copy, &from, &to2, whole);
        }
      }
    }
    return g_opacity.upload(copy, opacity ? opacity->data() : nullptr,
                            opacity ? Uint32(opacity->size() * sizeof(float)) : 0);
  });
  return g_sunSent = ok;
}

} // namespace GpuSurfacesSdlGpu
