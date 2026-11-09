#include "GpuFsrSdlGpu.hpp"

// ours: see GpuFsrSdlGpu.hpp. FSR 1's float path (ffx_fsr1.h, Copyright (c)
// 2021 Advanced Micro Devices, Inc., MIT license,
// https://github.com/GPUOpen-Effects/FidelityFX-FSR), as GpuFsrD3D11.cpp
// has it. SDL_GPU's targets run top-down like Direct3D 11's, so the shaders
// and their constants are the same.

#include <SDL3/SDL_gpu.h>

#include <algorithm>

#include "GpuFsrSdlGpuShaders.hpp"

namespace GpuFsr {
namespace SdlGpu {
namespace {

SDL_GPUDevice *g_dev = nullptr;
SDL_GPUShader *g_easuShader = nullptr, *g_rcasShader = nullptr, *g_styleShader = nullptr;
SDL_GPURenderState *g_easu = nullptr, *g_rcas = nullptr, *g_style = nullptr;

// The constants every pass reads (cbuffer C, as GpuFsrD3D11.cpp's)
struct Consts {
  float uSize[2];
  float uTime;
  float pad;
  float p[16];
};

// Each layer's window-sized buffer
struct Mid {
  SDL_Texture *t = nullptr;
  int w = 0, h = 0;
};
Mid g_mid[2];

SDL_GPUShader *shader(const unsigned char *code, size_t size, const char *what) {
  SDL_GPUShaderCreateInfo info{};
  info.code = code;
  info.code_size = size;
  info.entrypoint = "main";
  info.format = SDL_GPU_SHADERFORMAT_DXIL;
  info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  info.num_samplers = 1;
  info.num_uniform_buffers = 1;
  SDL_GPUShader *s = SDL_CreateGPUShader(g_dev, &info);
  if (!s)
    SDL_Log("[GpuFsr] SDL GPU %s shader: %s", what, SDL_GetError());
  return s;
}

SDL_GPURenderState *state(SDL_Renderer *renderer, SDL_GPUShader *frag) {
  if (!frag)
    return nullptr;
  SDL_GPURenderStateCreateInfo info{};
  info.fragment_shader = frag;
  SDL_GPURenderState *s = SDL_CreateGPURenderState(renderer, &info);
  if (!s)
    SDL_Log("[GpuFsr] SDL GPU render state: %s", SDL_GetError());
  return s;
}

bool ensure(SDL_Renderer *renderer, Mid &m, int w, int h) {
  if (m.t && m.w == w && m.h == h)
    return true;
  if (m.t)
    SDL_DestroyTexture(m.t);
  m.t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
  m.w = w;
  m.h = h;
  if (m.t)
    SDL_SetTextureScaleMode(m.t, SDL_SCALEMODE_NEAREST);
  return m.t != nullptr;
}

// One pass: from drawn over dst on the current target through st
void pass(SDL_Renderer *renderer, SDL_GPURenderState *st, const Consts &c, SDL_Texture *from, const SDL_FRect &src,
          const SDL_FRect &dst, SDL_BlendMode blend) {
  SDL_SetGPURenderStateFragmentUniforms(st, 0, &c, sizeof(c));
  SDL_SetTextureBlendMode(from, blend);
  SDL_SetGPURenderState(renderer, st);
  SDL_RenderTexture(renderer, from, &src, &dst);
  SDL_SetGPURenderState(renderer, nullptr);
}

} // namespace

bool init(SDL_Renderer *renderer) {
  g_dev = static_cast<SDL_GPUDevice *>(
      SDL_GetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
  if (!g_dev)
    return false;
  if (!(SDL_GetGPUShaderFormats(g_dev) & SDL_GPU_SHADERFORMAT_DXIL)) {
    SDL_Log("[GpuFsr] SDL GPU on %s takes no DXIL: no shaders yet", SDL_GetGPUDeviceDriver(g_dev));
    g_dev = nullptr;
    return false;
  }
  g_easuShader = shader(kEasuDxil, sizeof(kEasuDxil), "EASU");
  g_rcasShader = shader(kRcasDxil, sizeof(kRcasDxil), "RCAS");
  g_styleShader = shader(kStyleDxil, sizeof(kStyleDxil), "stylized");
  g_easu = state(renderer, g_easuShader);
  g_rcas = state(renderer, g_rcasShader);
  g_style = state(renderer, g_styleShader);
  const bool ok = g_easu && g_rcas;
  SDL_Log("[GpuFsr] SDL GPU (%s) shaders %s", SDL_GetGPUDeviceDriver(g_dev), ok ? "ready" : "unavailable");
  return ok;
}

void endLayer(SDL_Renderer *renderer, int layer, SDL_Texture *low, int lowW, int lowH, float viewW, float viewH,
              bool blend, const SDL_Rect *dstRect, bool styled, float con, float strength) {
  if (!g_easu || !g_rcas || layer < 0 || layer > 1 || !low)
    return;
  // The window (the target from here on, as GpuFsr's OpenGL endLayer leaves it)
  SDL_SetRenderTarget(renderer, nullptr);
  int outW = 0, outH = 0;
  SDL_GetCurrentRenderOutputSize(renderer, &outW, &outH);
  Mid &mid = g_mid[layer];
  if (outW <= 0 || outH <= 0 || !ensure(renderer, mid, outW, outH))
    return;
  const SDL_Rect d = dstRect ? *dstRect : SDL_Rect{0, 0, outW, outH};
  const SDL_FRect dst{float(d.x), float(d.y), float(d.w), float(d.h)};
  SDL_BlendMode lowBlend = SDL_BLENDMODE_NONE, midBlend = SDL_BLENDMODE_NONE;
  SDL_GetTextureBlendMode(low, &lowBlend);
  SDL_GetTextureBlendMode(mid.t, &midBlend);
  // Pass 1: EASU, the layer -> the window-sized buffer, at dst
  Consts c{};
  c.p[0] = float(lowW);
  c.p[1] = float(lowH);
  c.p[2] = std::min(viewW, float(lowW));
  c.p[3] = std::min(viewH, float(lowH));
  c.p[4] = dst.x;
  c.p[5] = dst.y;
  c.p[6] = dst.w;
  c.p[7] = dst.h;
  SDL_SetRenderTarget(renderer, mid.t);
  pass(renderer, g_easu, c, low, SDL_FRect{0, 0, float(lowW), float(lowH)}, dst, SDL_BLENDMODE_NONE);
  // Pass 2: RCAS (or Stylized) -> the window, at dst
  Consts c2{};
  c2.p[0] = float(outW);
  c2.p[1] = float(outH);
  const bool style = styled && g_style;
  c2.p[2] = style ? strength : con;
  c2.p[4] = dst.x;
  c2.p[5] = dst.y;
  c2.p[6] = dst.w;
  c2.p[7] = dst.h;
  SDL_SetRenderTarget(renderer, nullptr);
  // (in window pixels, as the other backends' raw passes are: the window's
  // viewport, clip and scale set aside for it)
  SDL_Rect view, clip;
  SDL_GetRenderViewport(renderer, &view);
  const bool clipped = SDL_RenderClipEnabled(renderer);
  SDL_GetRenderClipRect(renderer, &clip);
  float sx = 1.0f, sy = 1.0f;
  SDL_GetRenderScale(renderer, &sx, &sy);
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
  SDL_SetRenderViewport(renderer, nullptr);
  SDL_SetRenderClipRect(renderer, nullptr);
  pass(renderer, style ? g_style : g_rcas, c2, mid.t, dst, dst,
       blend ? SDL_BLENDMODE_BLEND_PREMULTIPLIED : SDL_BLENDMODE_NONE);
  SDL_SetRenderViewport(renderer, &view);
  SDL_SetRenderClipRect(renderer, clipped ? &clip : nullptr);
  SDL_SetRenderScale(renderer, sx, sy);
  SDL_SetTextureBlendMode(low, lowBlend);
  SDL_SetTextureBlendMode(mid.t, midBlend);
}

} // namespace SdlGpu
} // namespace GpuFsr
