#include "GpuFsrSdlGpu.hpp"

// ours: see GpuFsrSdlGpu.hpp. FSR 1's float path (ffx_fsr1.h, Copyright (c)
// 2021 Advanced Micro Devices, Inc., MIT license,
// https://github.com/GPUOpen-Effects/FidelityFX-FSR), as GpuFsrD3D11.cpp
// has it. SDL_GPU's targets run top-down like Direct3D 11's, so the shaders
// and their constants are the same.

#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <utility>

#include "GpuFsrSdlGpuShaders.hpp"
#include "GpuUploadSdlGpu.hpp"

// (this file runs the window's stand-in, so it calls SDL's own functions;
// standIn() is the window as the rest of the engine sees it)
#undef SDL_SetRenderTarget
#undef SDL_GetRenderTarget
#undef SDL_RenderPresent

namespace GpuFsr {
namespace SdlGpu {
namespace {

SDL_GPUDevice *g_dev = nullptr;
// The form the device takes the shaders in: DXIL (Direct3D 12) or SPIR-V (Vulkan)
SDL_GPUShaderFormat g_format = SDL_GPU_SHADERFORMAT_INVALID;
SDL_GPURenderState *g_easu = nullptr, *g_rcas = nullptr, *g_style = nullptr, *g_colour = nullptr;

struct Program {
  SDL_GPURenderState *state = nullptr;
  std::vector<std::string> params;
};
std::vector<Program> g_programs;

// The constants every pass reads (cbuffer C, as GpuFsrD3D11.cpp's)
struct Consts {
  float uSize[2];
  float uTime;
  float pad;
  float p[16];
};

// A target texture of ours, made again when its size changes
struct Tex {
  SDL_Texture *t = nullptr;
  int w = 0, h = 0;
};
Tex g_mid[2];
// The frame while it stands in for the window, and the one a screen pass
// draws the next frame into (then they swap)
Tex g_frame, g_spare;

bool g_colourOn = false;
float g_matrix[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
float g_gamma = 1.0f;
// Frames left to keep the stand-in for after a screen pass was asked for
int g_screenAsked = 0;
// The frame already copied to the window (by finishFrame) this frame
bool g_finished = false;

SDL_GPURenderState *makeState(SDL_Renderer *renderer, const Code &code, const char *what, int samplers = 1,
                              int uniforms = 1) {
  SDL_GPUShaderCreateInfo info{};
  const bool dxil = g_format == SDL_GPU_SHADERFORMAT_DXIL;
  info.code = dxil ? code.dxil : code.spv;
  info.code_size = dxil ? code.dxilSize : code.spvSize;
  info.entrypoint = "main";
  info.format = g_format;
  info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  info.num_samplers = Uint32(samplers);
  info.num_uniform_buffers = Uint32(uniforms);
  SDL_GPUShader *shader = SDL_CreateGPUShader(g_dev, &info);
  if (!shader) {
    SDL_Log("[GpuFsr] SDL GPU %s shader: %s", what, SDL_GetError());
    return nullptr;
  }
  SDL_GPURenderStateCreateInfo st{};
  st.fragment_shader = shader;
  SDL_GPURenderState *s = SDL_CreateGPURenderState(renderer, &st);
  if (!s)
    SDL_Log("[GpuFsr] SDL GPU %s render state: %s", what, SDL_GetError());
  return s;
}

bool ensure(SDL_Renderer *renderer, Tex &m, int w, int h) {
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

// The window as the engine sees it: the stand-in while there is one
SDL_Texture *standIn() { return ZT_WindowStandIn().texture; }

// A target's viewport, clip and scale: kept and set aside for a pass drawn
// in its pixels (as the other backends' raw passes are), then put back
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

// One pass: from drawn over dst on the current target through st
void pass(SDL_Renderer *renderer, SDL_GPURenderState *st, const Consts &c, SDL_Texture *from, const SDL_FRect &src,
          const SDL_FRect &dst, SDL_BlendMode blend) {
  SDL_SetGPURenderStateFragmentUniforms(st, 0, &c, sizeof(c));
  SDL_BlendMode was = SDL_BLENDMODE_NONE;
  SDL_GetTextureBlendMode(from, &was);
  SDL_SetTextureBlendMode(from, blend);
  SDL_SetGPURenderState(renderer, st);
  SDL_RenderTexture(renderer, from, &src, &dst);
  SDL_SetGPURenderState(renderer, nullptr);
  SDL_SetTextureBlendMode(from, was);
}

// The frame (the stand-in) onto the window, through the colour pass when
// colour is set; the window is the window again until the next present
void finish(SDL_Renderer *renderer, bool colour) {
  ZT_StandIn &s = ZT_WindowStandIn();
  if (g_finished || !s.texture)
    return;
  g_finished = true;
  SDL_Texture *frame = s.texture;
  s.texture = nullptr;
  SDL_SetRenderTarget(renderer, nullptr);
  const View v = keepView(renderer);
  const SDL_FRect all{0, 0, float(g_frame.w), float(g_frame.h)};
  if (colour && g_colour) {
    Consts c{};
    c.uSize[0] = float(g_frame.w);
    c.uSize[1] = float(g_frame.h);
    for (int r = 0; r < 3; r++)
      for (int k = 0; k < 3; k++)
        c.p[r * 4 + k] = g_matrix[r][k];
    c.p[12] = g_gamma;
    pass(renderer, g_colour, c, frame, all, all, SDL_BLENDMODE_NONE);
  } else {
    SDL_BlendMode was = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(frame, &was);
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
    SDL_RenderTexture(renderer, frame, &all, &all);
    SDL_SetTextureBlendMode(frame, was);
  }
  restoreView(renderer, v);
}

// The next frame: drawn into the stand-in while a pass will need to read
// it, onto the window itself otherwise
void arm(SDL_Renderer *renderer) {
  ZT_StandIn &s = ZT_WindowStandIn();
  g_finished = false;
  const bool want = g_colourOn || g_screenAsked > 0;
  if (g_screenAsked > 0)
    g_screenAsked--;
  int w = 0, h = 0;
  SDL_GetRenderOutputSize(renderer, &w, &h);
  if (!want || w <= 0 || h <= 0 || !ensure(renderer, g_frame, w, h)) {
    s.texture = nullptr;
    return;
  }
  s.texture = g_frame.t;
  SDL_SetRenderTarget(renderer, g_frame.t);
}

bool present(SDL_Renderer *renderer) {
  finish(renderer, false);
  GpuUploadSdlGpu::flush(renderer); // (new textures' pixels before SDL's frame)
  const bool ok = SDL_RenderPresent(renderer);
  arm(renderer);
  return ok;
}

unsigned long long fnv1a64(const std::string &s) {
  unsigned long long h = 0xcbf29ce484222325ull;
  for (unsigned char b : s)
    h = (h ^ b) * 0x100000001b3ull;
  return h;
}

} // namespace

SDL_GPUDevice *device() { return g_dev; }
SDL_GPUShaderFormat shaderFormat() { return g_format; }
SDL_GPURenderState *renderState(SDL_Renderer *renderer, const Code &code, int samplers, int uniforms,
                                const char *what) {
  return g_dev ? makeState(renderer, code, what, samplers, uniforms) : nullptr;
}

bool init(SDL_Renderer *renderer) {
  g_dev = static_cast<SDL_GPUDevice *>(
      SDL_GetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
  if (!g_dev)
    return false;
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(g_dev);
  g_format = (formats & SDL_GPU_SHADERFORMAT_DXIL)    ? SDL_GPU_SHADERFORMAT_DXIL
             : (formats & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
                                                      : SDL_GPU_SHADERFORMAT_INVALID;
  if (g_format == SDL_GPU_SHADERFORMAT_INVALID) {
    SDL_Log("[GpuFsr] SDL GPU on %s takes neither DXIL nor SPIR-V: no shaders", SDL_GetGPUDeviceDriver(g_dev));
    g_dev = nullptr;
    return false;
  }
  g_easu = makeState(renderer, kEasuCode, "EASU");
  g_rcas = makeState(renderer, kRcasCode, "RCAS");
  g_style = makeState(renderer, kStyleCode, "stylized");
  g_colour = makeState(renderer, kColourCode, "colour");
  const bool ok = g_easu && g_rcas;
  if (ok) {
    ZT_StandIn &s = ZT_WindowStandIn();
    s.renderer = renderer;
    s.texture = nullptr;
    s.present = present;
    GpuUploadSdlGpu::install(renderer, g_dev);
  }
  SDL_Log("[GpuFsr] SDL GPU (%s) shaders %s", SDL_GetGPUDeviceDriver(g_dev), ok ? "ready" : "unavailable");
  return ok;
}

unsigned compile(const std::string &hlsl, const std::vector<std::string> &params, std::string &log) {
  SDL_Renderer *renderer = ZT_WindowStandIn().renderer;
  if (!g_dev || !renderer)
    return log = "no SDL GPU device", 0;
  const unsigned long long h = fnv1a64(hlsl);
  for (const Effect &e : kEffects)
    if (e.hash == h) {
      Program p;
      p.state = makeState(renderer, e.code, "effect");
      if (!p.state)
        return log = "the shader couldn't be made", 0;
      p.params = params;
      g_programs.push_back(p);
      return static_cast<unsigned>(g_programs.size());
    }
  return log = "not a shader compiled ahead for SDL's GPU renderer (tools/build_sdlgpu_shaders.py)", 0;
}

bool screenPass(SDL_Renderer *renderer, unsigned prog, const std::vector<std::pair<std::string, float>> &uniforms,
                bool smooth) {
  if (!g_dev || prog == 0 || prog > g_programs.size())
    return false;
  g_screenAsked = 3;
  ZT_StandIn &s = ZT_WindowStandIn();
  if (!s.texture || renderer != s.renderer || !ensure(renderer, g_spare, g_frame.w, g_frame.h))
    return false;
  const Program &p = g_programs[prog - 1];
  SDL_Texture *was = SDL_GetRenderTarget(renderer);
  // The frame through prog into the spare, which becomes the frame (with the
  // frame's viewport, clip and scale)
  SDL_SetRenderTarget(renderer, g_frame.t);
  const View v = keepView(renderer);
  restoreView(renderer, v);
  SDL_SetRenderTarget(renderer, g_spare.t);
  keepView(renderer);
  Consts c{};
  c.uSize[0] = float(g_frame.w);
  c.uSize[1] = float(g_frame.h);
  c.uTime = SDL_GetTicks() / 1000.0f;
  for (const auto &[name, value] : uniforms)
    for (size_t i = 0; i < p.params.size() && i < 16; i++)
      if (p.params[i] == name)
        c.p[i] = value;
  const SDL_FRect all{0, 0, float(g_frame.w), float(g_frame.h)};
  SDL_SetTextureScaleMode(g_frame.t, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
  pass(renderer, p.state, c, g_frame.t, all, all, SDL_BLENDMODE_NONE);
  SDL_SetTextureScaleMode(g_frame.t, SDL_SCALEMODE_NEAREST);
  restoreView(renderer, v);
  std::swap(g_frame, g_spare);
  s.texture = g_frame.t;
  SDL_SetRenderTarget(renderer, was == g_spare.t ? g_frame.t : was);
  return true;
}

void setColour(bool on, const float matrix[3][3], float gamma) {
  g_colourOn = on;
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++)
      g_matrix[r][k] = matrix[r][k];
  g_gamma = gamma;
}

void finishFrame(SDL_Renderer *renderer) { finish(renderer, g_colourOn); }

void endLayer(SDL_Renderer *renderer, int layer, SDL_Texture *low, int lowW, int lowH, float viewW, float viewH,
              bool blend, const SDL_Rect *dstRect, bool styled, float con, float strength) {
  if (!g_easu || !g_rcas || layer < 0 || layer > 1 || !low)
    return;
  // The window (the target from here on, as GpuFsr's OpenGL endLayer leaves it)
  SDL_SetRenderTarget(renderer, standIn());
  int outW = 0, outH = 0;
  SDL_GetCurrentRenderOutputSize(renderer, &outW, &outH);
  Tex &mid = g_mid[layer];
  if (outW <= 0 || outH <= 0 || !ensure(renderer, mid, outW, outH))
    return;
  const SDL_Rect d = dstRect ? *dstRect : SDL_Rect{0, 0, outW, outH};
  const SDL_FRect dst{float(d.x), float(d.y), float(d.w), float(d.h)};
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
  SDL_SetRenderTarget(renderer, standIn());
  const View v = keepView(renderer);
  pass(renderer, style ? g_style : g_rcas, c2, mid.t, dst, dst,
       blend ? SDL_BLENDMODE_BLEND_PREMULTIPLIED : SDL_BLENDMODE_NONE);
  restoreView(renderer, v);
}

} // namespace SdlGpu
} // namespace GpuFsr
