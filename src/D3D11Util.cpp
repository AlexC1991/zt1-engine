#include "D3D11Util.hpp"

// ours: see D3D11Util.hpp

#ifdef _WIN32
#include <windows.h>

#include <SDL2/SDL_system.h>

namespace D3D11Util {
namespace {

template <typename T> void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

bool g_tried = false, g_ready = false;
ID3D11Device *g_dev = nullptr;
ID3D11DeviceContext *g_ctx = nullptr;
pD3DCompile g_compile = nullptr;

// (SDL's own records as SDL 2.32 lays them out - src/render/SDL_sysrender.h
// and src/render/direct3d11/SDL_render_d3d11.c in vendor/SDL; the same
// mirror GpuCompressed.cpp checks)
#if SDL_VERSION_ATLEAST(2, 32, 0) && !SDL_VERSION_ATLEAST(2, 33, 0)
#define ZT_D3D11_RECORD 1
struct SdlTexture {
  const void *magic;
  Uint32 format;
  int access;
  int w;
  int h;
  int modMode;
  SDL_BlendMode blendMode;
  SDL_ScaleMode scaleMode;
  SDL_Color color;
  SDL_Renderer *renderer;
  SDL_Texture *native;
  void *yuv;
  void *pixels;
  int pitch;
  SDL_Rect locked_rect;
  SDL_Surface *locked_surface;
  Uint32 last_command_generation;
  void *driverdata;
  void *userdata;
  SDL_Texture *prev;
  SDL_Texture *next;
};
struct D3D11TextureStart { // (D3D11_TextureData's first two)
  ID3D11Texture2D *mainTexture;
  ID3D11ShaderResourceView *mainTextureResourceView;
};
#endif

} // namespace

bool ready(SDL_Renderer *renderer) {
  if (g_tried)
    return g_ready;
  g_tried = true;
  SDL_RendererInfo info{};
  if (!renderer || SDL_GetRendererInfo(renderer, &info) != 0 || std::string(info.name) != "direct3d11")
    return false;
  g_dev = SDL_RenderGetD3D11Device(renderer);
  if (!g_dev)
    return false;
  if (g_dev->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
    SDL_Log("[D3D11] feature level below 11: the lighting stays on the CPU");
    release(g_dev);
    return false;
  }
  g_dev->GetImmediateContext(&g_ctx);
  if (HMODULE lib = LoadLibraryA("d3dcompiler_47.dll"))
    g_compile = reinterpret_cast<pD3DCompile>(GetProcAddress(lib, "D3DCompile"));
  g_ready = g_ctx && g_compile;
  return g_ready;
}

ID3D11Device *device() { return g_dev; }
ID3D11DeviceContext *context() { return g_ctx; }

ID3DBlob *compile(const std::string &source, const char *target, std::string &log) {
  log.clear();
  if (!g_compile)
    return log = "no shader compiler", nullptr;
  ID3DBlob *code = nullptr, *errors = nullptr;
  const HRESULT hr = g_compile(source.data(), source.size(), "zt1", nullptr, nullptr, "main", target,
                               D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &errors);
  if (errors) {
    log.assign(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
    errors->Release();
  }
  if (FAILED(hr)) {
    release(code);
    if (log.empty())
      log = "the shader didn't compile";
    return nullptr;
  }
  return code;
}

struct Saved::Impl {
  static constexpr UINT kSrv = 6;
  UINT views = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  UINT rects = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  D3D11_VIEWPORT view[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
  D3D11_RECT rect[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
  ID3D11RasterizerState *rs = nullptr;
  ID3D11BlendState *bs = nullptr;
  FLOAT factor[4];
  UINT mask = 0;
  ID3D11DepthStencilState *ds = nullptr;
  UINT ref = 0;
  ID3D11ShaderResourceView *vsSrv[kSrv]{}, *gsSrv[kSrv]{}, *psSrv[kSrv]{};
  ID3D11SamplerState *psSampler = nullptr;
  ID3D11VertexShader *vs = nullptr;
  ID3D11GeometryShader *gs = nullptr;
  ID3D11PixelShader *ps = nullptr;
  ID3D11Buffer *vsCb = nullptr, *gsCb = nullptr, *psCb = nullptr;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ID3D11Buffer *ib = nullptr;
  DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
  UINT ibOffset = 0;
  ID3D11Buffer *vb = nullptr;
  UINT stride = 0, offset = 0;
  ID3D11InputLayout *layout = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  ID3D11DepthStencilView *dsv = nullptr;
};

Saved::Saved() : impl(new Impl) {
  auto &s = *impl;
  ID3D11DeviceContext *c = g_ctx;
  if (!c)
    return;
  c->RSGetViewports(&s.views, s.view);
  c->RSGetScissorRects(&s.rects, s.rect);
  c->RSGetState(&s.rs);
  c->OMGetBlendState(&s.bs, s.factor, &s.mask);
  c->OMGetDepthStencilState(&s.ds, &s.ref);
  c->VSGetShaderResources(0, Impl::kSrv, s.vsSrv);
  c->GSGetShaderResources(0, Impl::kSrv, s.gsSrv);
  c->PSGetShaderResources(0, Impl::kSrv, s.psSrv);
  c->PSGetSamplers(0, 1, &s.psSampler);
  c->VSGetShader(&s.vs, nullptr, nullptr);
  c->GSGetShader(&s.gs, nullptr, nullptr);
  c->PSGetShader(&s.ps, nullptr, nullptr);
  c->VSGetConstantBuffers(0, 1, &s.vsCb);
  c->GSGetConstantBuffers(0, 1, &s.gsCb);
  c->PSGetConstantBuffers(0, 1, &s.psCb);
  c->IAGetPrimitiveTopology(&s.topology);
  c->IAGetIndexBuffer(&s.ib, &s.ibFormat, &s.ibOffset);
  c->IAGetVertexBuffers(0, 1, &s.vb, &s.stride, &s.offset);
  c->IAGetInputLayout(&s.layout);
  c->OMGetRenderTargets(1, &s.rtv, &s.dsv);
}

Saved::~Saved() {
  auto &s = *impl;
  if (ID3D11DeviceContext *c = g_ctx) {
    c->RSSetViewports(s.views, s.view);
    c->RSSetScissorRects(s.rects, s.rect);
    c->RSSetState(s.rs);
    c->OMSetBlendState(s.bs, s.factor, s.mask);
    c->OMSetDepthStencilState(s.ds, s.ref);
    c->VSSetShaderResources(0, Impl::kSrv, s.vsSrv);
    c->GSSetShaderResources(0, Impl::kSrv, s.gsSrv);
    c->PSSetShaderResources(0, Impl::kSrv, s.psSrv);
    c->PSSetSamplers(0, 1, &s.psSampler);
    c->VSSetShader(s.vs, nullptr, 0);
    c->GSSetShader(s.gs, nullptr, 0);
    c->PSSetShader(s.ps, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, &s.vsCb);
    c->GSSetConstantBuffers(0, 1, &s.gsCb);
    c->PSSetConstantBuffers(0, 1, &s.psCb);
    c->IASetPrimitiveTopology(s.topology);
    c->IASetIndexBuffer(s.ib, s.ibFormat, s.ibOffset);
    c->IASetVertexBuffers(0, 1, &s.vb, &s.stride, &s.offset);
    c->IASetInputLayout(s.layout);
    c->OMSetRenderTargets(1, &s.rtv, s.dsv);
  }
  release(s.rs);
  release(s.bs);
  release(s.ds);
  for (UINT i = 0; i < Impl::kSrv; i++) {
    release(s.vsSrv[i]);
    release(s.gsSrv[i]);
    release(s.psSrv[i]);
  }
  release(s.psSampler);
  release(s.vs);
  release(s.gs);
  release(s.ps);
  release(s.vsCb);
  release(s.gsCb);
  release(s.psCb);
  release(s.ib);
  release(s.vb);
  release(s.layout);
  release(s.rtv);
  release(s.dsv);
  delete impl;
}

ID3D11RenderTargetView *boundTarget(SDL_Renderer *renderer) {
  if (!g_ctx)
    return nullptr;
  Uint8 r, g, b, a;
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
  SDL_BlendMode mode;
  SDL_GetRenderDrawBlendMode(renderer, &mode);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderDrawPoint(renderer, 0, 0);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_SetRenderDrawBlendMode(renderer, mode);
  SDL_RenderFlush(renderer);
  ID3D11RenderTargetView *rtv = nullptr;
  g_ctx->OMGetRenderTargets(1, &rtv, nullptr);
  return rtv;
}

ID3D11ShaderResourceView *viewOf(SDL_Texture *texture) {
#ifdef ZT_D3D11_RECORD
  if (!texture)
    return nullptr;
  int w = 0, h = 0;
  Uint32 format = 0;
  if (SDL_QueryTexture(texture, &format, nullptr, &w, &h) != 0)
    return nullptr;
  const auto *record = reinterpret_cast<const SdlTexture *>(texture);
  if (record->w != w || record->h != h || record->format != format || !record->driverdata)
    return nullptr;
  const auto *data = static_cast<const D3D11TextureStart *>(record->driverdata);
  if (!data->mainTexture || !data->mainTextureResourceView)
    return nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  data->mainTexture->GetDesc(&desc);
  if (int(desc.Width) != w || int(desc.Height) != h)
    return nullptr;
  return data->mainTextureResourceView;
#else
  (void)texture;
  return nullptr;
#endif
}

} // namespace D3D11Util
#endif
