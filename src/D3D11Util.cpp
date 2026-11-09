#include <cstring>
#include "D3D11Util.hpp"

// ours: see D3D11Util.hpp

#ifdef _WIN32
#include <windows.h>

#include <SDL3/SDL_system.h>

// ours: SDL2's SDL_RenderGetD3D11Device on SDL3 (Sdl3Compat.hpp): the
// renderer's device from its properties, referenced for the caller as SDL2's was
ID3D11Device *ZT_RenderGetD3D11Device(SDL_Renderer *renderer) {
  auto *device = static_cast<ID3D11Device *>(
      SDL_GetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
  if (device)
    device->AddRef();
  return device;
}

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
  ZT_RendererInfo info{};
  if (!renderer || ZT_GetRendererInfo(renderer, &info) != 0 || std::string(info.name) != "direct3d11")
    return false;
  g_dev = ZT_RenderGetD3D11Device(renderer);
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
  SDL_RenderPoint(renderer, 0, 0);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_SetRenderDrawBlendMode(renderer, mode);
  SDL_FlushRenderer(renderer);
  ID3D11RenderTargetView *rtv = nullptr;
  g_ctx->OMGetRenderTargets(1, &rtv, nullptr);
  return rtv;
}

// ours: SDL3's own update makes a staging texture, copies through it and
// frees it, each time - and first draws whatever it has queued that uses the
// texture: about a tenth of a millisecond for a mask of a few hundred pixels.
// UpdateSubresource takes the pixels as they are; SDL's record of the
// pipeline is untouched (no state is bound).
bool updateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch) {
  if (!texture || !g_ctx || !pixels)
    return false;
  auto *resource = static_cast<ID3D11Texture2D *>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!resource)
    return false;
  D3D11_TEXTURE2D_DESC desc{};
  resource->GetDesc(&desc);
  switch (desc.Format) { // (four bytes a pixel, as SDL copies them)
  case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: break;
  default: return false;
  }
  if (desc.Usage != D3D11_USAGE_DEFAULT || desc.MipLevels != 1 || desc.ArraySize != 1)
    return false;
  D3D11_BOX box{0, 0, 0, desc.Width, desc.Height, 1};
  if (rect) {
    if (rect->x < 0 || rect->y < 0 || rect->w <= 0 || rect->h <= 0 || UINT(rect->x + rect->w) > desc.Width || UINT(rect->y + rect->h) > desc.Height)
      return false;
    box = {UINT(rect->x), UINT(rect->y), 0, UINT(rect->x + rect->w), UINT(rect->y + rect->h), 1};
  }
  g_ctx->UpdateSubresource(resource, 0, &box, pixels, UINT(pitch), 0);
  return true;
}

// ours: SDL3's Direct3D 11 renderer makes a texture empty, then fills it
// through a staging texture it makes and frees - about a tenth of a
// millisecond each, and a busy zoo's sprites ask for a couple of hundred new
// frames at once as their animations first reach them. The same texture
// (SDL's format for the surface's own, static, sRGB) made with its pixels,
// then handed to SDL to keep; anything SDL would convert or treat specially
// is left to SDL.
namespace {
SDL_Renderer *g_maker = nullptr;
void *g_makerDevice = nullptr;
SDL_Texture *makeTexture(SDL_Renderer *renderer, SDL_Surface *s) {
  const SDL_PropertiesID rp = SDL_GetRendererProperties(renderer);
  auto *device = static_cast<ID3D11Device *>(SDL_GetPointerProperty(rp, SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
  if (!device || device != g_makerDevice || !s || !s->pixels || s->w <= 0 || s->h <= 0 || SDL_ISPIXELFORMAT_INDEXED(s->format) ||
      SDL_SurfaceHasColorKey(s) || SDL_MUSTLOCK(s) || SDL_GetSurfaceColorspace(s) != SDL_COLORSPACE_SRGB ||
      SDL_GetNumberProperty(rp, SDL_PROP_RENDERER_OUTPUT_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB) != SDL_COLORSPACE_SRGB)
    return nullptr;
  DXGI_FORMAT format; // (SDL's own choice for these, SDLPixelFormatToDXGITextureFormat)
  switch (s->format) {
  case SDL_PIXELFORMAT_ARGB8888: format = DXGI_FORMAT_B8G8R8A8_UNORM; break;
  case SDL_PIXELFORMAT_ABGR8888: format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
  case SDL_PIXELFORMAT_XRGB8888: format = DXGI_FORMAT_B8G8R8X8_UNORM; break;
  default: return nullptr;
  }
  bool takes = false; // (and the renderer's: SDL_CreateTextureFromSurface then keeps the surface's)
  for (auto *f = static_cast<const SDL_PixelFormat *>(SDL_GetPointerProperty(rp, SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER, nullptr));
       f && *f != SDL_PIXELFORMAT_UNKNOWN; ++f)
    takes |= *f == s->format;
  if (!takes)
    return nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = UINT(s->w);
  desc.Height = UINT(s->h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA pixels{s->pixels, UINT(s->pitch), 0};
  ID3D11Texture2D *made = nullptr;
  if (FAILED(device->CreateTexture2D(&desc, &pixels, &made)) || !made)
    return nullptr;
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, s->format);
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, s->w);
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, s->h);
  SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
  SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER, made);
  SDL_Texture *t = SDL_CreateTextureWithProperties(renderer, props);
  SDL_DestroyProperties(props);
  made->Release(); // (SDL keeps its own reference)
  if (!t)
    return nullptr;
  // (as SDL_CreateTextureFromSurface: the surface's colour and alpha mods and
  // blend mode go to the texture - GpuUploadSdlGpu::create's)
  Uint8 r = 255, g = 255, b = 255, a = 255;
  SDL_GetSurfaceColorMod(s, &r, &g, &b);
  SDL_SetTextureColorMod(t, r, g, b);
  SDL_GetSurfaceAlphaMod(s, &a);
  SDL_SetTextureAlphaMod(t, a);
  SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
  SDL_GetSurfaceBlendMode(s, &mode);
  SDL_SetTextureBlendMode(t, mode);
  return t;
}
} // namespace

void installTextureMaker(SDL_Renderer *renderer) {
  const char *off = SDL_getenv("ZT_D3D11_UPLOADS");
  ZT_RendererInfo info{};
  if ((off && std::strcmp(off, "0") == 0) || !renderer || ZT_GetRendererInfo(renderer, &info) != 0 ||
      std::string(info.name) != "direct3d11")
    return;
  ZT_StandIn &s = ZT_WindowStandIn();
  if (s.renderer && s.renderer != renderer)
    return; // (another renderer's stand-in: the GPU renderer's)
  g_maker = renderer;
  g_makerDevice = SDL_GetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr);
  s.renderer = renderer;
  s.create = makeTexture;
}

ID3D11ShaderResourceView *viewOf(SDL_Texture *texture) {
  // ours: SDL3 gives a texture's Direct3D 11 texture as a property (SDL2's
  // record was read before); a view of it is made once and kept with the
  // texture - released by SDL as the texture goes
  if (!texture || !g_dev)
    return nullptr;
  const SDL_PropertiesID props = SDL_GetTextureProperties(texture);
  if (auto *kept = static_cast<ID3D11ShaderResourceView *>(SDL_GetPointerProperty(props, "zt1.d3d11.view", nullptr)))
    return kept;
  auto *resource = static_cast<ID3D11Texture2D *>(SDL_GetPointerProperty(props, SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!resource)
    return nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  resource->GetDesc(&desc);
  // (SDL3 may make it typeless, to view it plain or as sRGB: plain here, as
  // SDL's own views read the game's colours)
  DXGI_FORMAT format = desc.Format;
  switch (format) {
  case DXGI_FORMAT_B8G8R8A8_TYPELESS: format = DXGI_FORMAT_B8G8R8A8_UNORM; break;
  case DXGI_FORMAT_B8G8R8X8_TYPELESS: format = DXGI_FORMAT_B8G8R8X8_UNORM; break;
  case DXGI_FORMAT_R8G8B8A8_TYPELESS: format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
  case DXGI_FORMAT_R10G10B10A2_TYPELESS: format = DXGI_FORMAT_R10G10B10A2_UNORM; break;
  case DXGI_FORMAT_R16G16B16A16_TYPELESS: format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
  default: break;
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC v{};
  v.Format = format;
  v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  v.Texture2D.MipLevels = desc.MipLevels ? desc.MipLevels : 1;
  ID3D11ShaderResourceView *view = nullptr;
  if (FAILED(g_dev->CreateShaderResourceView(resource, &v, &view)) || !view)
    return nullptr;
  SDL_SetPointerPropertyWithCleanup(props, "zt1.d3d11.view", view,
                                    [](void *, void *value) { static_cast<ID3D11ShaderResourceView *>(value)->Release(); }, nullptr);
  return view;
}

} // namespace D3D11Util
#endif
