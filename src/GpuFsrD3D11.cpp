#include "GpuFsrD3D11.hpp"

// ours: see GpuFsrD3D11.hpp. The shaders are GpuFsr.cpp's - FSR 1's float
// path (ffx_fsr1.h, Copyright (c) 2021 Advanced Micro Devices, Inc., MIT
// license, https://github.com/GPUOpen-Effects/FidelityFX-FSR), Stylized and
// the colour pass - written again in HLSL. Direct3D's targets all run
// top-down (the window too), so none of the OpenGL versions' row flips.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <SDL2/SDL_system.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

namespace GpuFsr {
namespace D3D11 {
namespace {

template <typename T> void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

ID3D11Device *g_dev = nullptr;
ID3D11DeviceContext *g_ctx = nullptr;
pD3DCompile g_compile = nullptr;
ID3D11VertexShader *g_vs = nullptr;
ID3D11PixelShader *g_easu = nullptr, *g_rcas = nullptr, *g_style = nullptr, *g_colour = nullptr;
ID3D11Buffer *g_cb = nullptr;
ID3D11SamplerState *g_point = nullptr, *g_linear = nullptr;
ID3D11BlendState *g_premul = nullptr;
ID3D11RasterizerState *g_raster = nullptr;

struct Program {
  ID3D11PixelShader *ps = nullptr;
  std::vector<std::string> params;
};
std::vector<Program> g_programs;

// The constants every pass reads (cbuffer C below)
struct Consts {
  float uSize[2];
  float uTime;
  float pad;
  float p[16];
};

// A texture of ours: read by the shaders, and drawn to when rtv
struct Tex {
  ID3D11Texture2D *t = nullptr;
  ID3D11ShaderResourceView *srv = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  UINT w = 0, h = 0;
  DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN;
};
Tex g_mid[2], g_copy;
// The view made on each layer's SDL texture (it holds the texture, so the
// pointer can't be another's while it's kept)
struct LowView {
  ID3D11Resource *res = nullptr;
  ID3D11ShaderResourceView *srv = nullptr;
};
LowView g_low[2];

const char *kCommon = R"(
cbuffer C : register(b0) { float2 uSize; float uTime; float pad; float4 p0; float4 p1; float4 p2; float4 p3; };
Texture2D src : register(t0);
SamplerState samp : register(s0);
)";

// One triangle over the whole viewport
const char *kVertex = R"(
float4 main(uint id : SV_VertexID) : SV_Position {
  float2 t = float2((id << 1) & 2, id & 2);
  return float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

const char *kEasu = R"(
#define texSize p0.xy
#define inView p0.zw
#define dst p1
float4 tap(float2 p) {
  p = clamp(p, 0.0, inView - 1.0);
  return src.SampleLevel(samp, (p + 0.5) / texSize, 0);
}
float luma(float4 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }
void easuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
  float lenX = max(abs(lD - lC), abs(lC - lB));
  float dX = lD - lB;
  dir.x += dX * w;
  lenX = lenX > 0.0 ? saturate(abs(dX) / lenX) : 0.0;
  len += lenX * lenX * w;
  float lenY = max(abs(lE - lC), abs(lC - lA));
  float dY = lE - lA;
  dir.y += dY * w;
  lenY = lenY > 0.0 ? saturate(abs(dY) / lenY) : 0.0;
  len += lenY * lenY * w;
}
void easuTap(inout float4 aC, inout float aW, float2 off, float2 dir, float2 len2, float lob, float clp, float4 c) {
  float2 v = float2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len2;
  float d2 = min(dot(v, v), clp);
  float wB = 0.4 * d2 - 1.0;
  float wA = lob * d2 - 1.0;
  wB *= wB;
  wA *= wA;
  wB = 1.5625 * wB - 0.5625;
  float w = wB * wA;
  aC += c * w;
  aW += w;
}
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 pp = (pos.xy - dst.xy) * (inView / dst.zw) - 0.5;
  float2 fp = floor(pp);
  pp -= fp;
  float4 b = tap(fp + float2(0.0, -1.0)), c = tap(fp + float2(1.0, -1.0));
  float4 e = tap(fp + float2(-1.0, 0.0)), f = tap(fp);
  float4 g = tap(fp + float2(1.0, 0.0)), h = tap(fp + float2(2.0, 0.0));
  float4 i = tap(fp + float2(-1.0, 1.0)), j = tap(fp + float2(0.0, 1.0));
  float4 k = tap(fp + float2(1.0, 1.0)), l = tap(fp + float2(2.0, 1.0));
  float4 n = tap(fp + float2(0.0, 2.0)), o = tap(fp + float2(1.0, 2.0));
  float bL = luma(b), cL = luma(c), eL = luma(e), fL = luma(f), gL = luma(g), hL = luma(h);
  float iL = luma(i), jL = luma(j), kL = luma(k), lL = luma(l), nL = luma(n), oL = luma(o);
  float2 dir = 0.0;
  float len = 0.0;
  easuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
  easuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
  easuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
  easuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
  float dirR = dot(dir, dir);
  if (dirR < 1.0 / 32768.0) {
    dirR = 1.0;
    dir.x = 1.0;
  } else {
    dirR = rsqrt(dirR);
  }
  dir *= dirR;
  len = len * 0.5;
  len *= len;
  float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
  float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
  float clp = 1.0 / lob;
  float4 aC = 0.0;
  float aW = 0.0;
  easuTap(aC, aW, float2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
  easuTap(aC, aW, float2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
  easuTap(aC, aW, float2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
  easuTap(aC, aW, float2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
  easuTap(aC, aW, float2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
  easuTap(aC, aW, float2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
  easuTap(aC, aW, float2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
  easuTap(aC, aW, float2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
  easuTap(aC, aW, float2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
  easuTap(aC, aW, float2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
  easuTap(aC, aW, float2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
  easuTap(aC, aW, float2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
  float4 mn = min(min(f, g), min(j, k));
  float4 mx = max(max(f, g), max(j, k));
  float4 res = aW != 0.0 ? aC / aW : f;
  return clamp(res, mn, mx);
}
)";

const char *kRcas = R"(
#define texSize p0.xy
#define con p0.z
#define dst p1
float4 tap(float2 p) {
  p = clamp(p, 0.0, dst.zw - 1.0);
  return src.SampleLevel(samp, (dst.xy + p + 0.5) / texSize, 0);
}
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 p = floor(pos.xy - dst.xy);
  float4 b = tap(p + float2(0.0, -1.0)), d = tap(p + float2(-1.0, 0.0));
  float4 e = tap(p), f = tap(p + float2(1.0, 0.0)), h = tap(p + float2(0.0, 1.0));
  float3 mn4 = min(min(b.rgb, d.rgb), min(f.rgb, h.rgb));
  float3 mx4 = max(max(b.rgb, d.rgb), max(f.rgb, h.rgb));
  float3 hitMin = min(mn4, e.rgb) / max(4.0 * mx4, 1e-6);
  float3 hitMax = (1.0 - max(mx4, e.rgb)) / min(4.0 * mn4 - 4.0, -1e-6);
  float3 lobeRGB = max(-hitMin, hitMax);
  float lobe = max(-(0.25 - 1.0 / 16.0), min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * con;
  float4 o;
  o.rgb = (lobe * (b.rgb + d.rgb + h.rgb + f.rgb) + e.rgb) / (4.0 * lobe + 1.0);
  o.a = e.a;
  o.rgb = clamp(o.rgb, 0.0, o.a);
  return o;
}
)";

const char *kStyle = R"(
#define texSize p0.xy
#define strength p0.z
#define dst p1
float4 tap(float2 p) {
  p = clamp(p, 0.0, dst.zw - 1.0);
  return src.SampleLevel(samp, (dst.xy + p + 0.5) / texSize, 0);
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 main(float4 pos : SV_Position) : SV_Target {
  float2 p = floor(pos.xy - dst.xy);
  float4 a = tap(p + float2(-1.0, -1.0)), b = tap(p + float2(0.0, -1.0)), c = tap(p + float2(1.0, -1.0));
  float4 d = tap(p + float2(-1.0, 0.0)), e = tap(p), f = tap(p + float2(1.0, 0.0));
  float4 g = tap(p + float2(-1.0, 1.0)), h = tap(p + float2(0.0, 1.0)), i = tap(p + float2(1.0, 1.0));
  float la = luma(a.rgb), lb = luma(b.rgb), lc = luma(c.rgb), ld = luma(d.rgb), le = luma(e.rgb);
  float lf = luma(f.rgb), lg = luma(g.rgb), lh = luma(h.rgb), li = luma(i.rgb);
  float gx = (lc + 2.0 * lf + li) - (la + 2.0 * ld + lg);
  float gy = (lg + 2.0 * lh + li) - (la + 2.0 * lb + lc);
  float edge = saturate(length(float2(gx, gy)) * 2.0);
  float3 mean = (a.rgb + b.rgb + c.rgb + d.rgb + f.rgb + g.rgb + h.rgb + i.rgb) / 8.0;
  float3 mn = min(min(min(a.rgb, b.rgb), min(c.rgb, d.rgb)), min(min(f.rgb, g.rgb), min(h.rgb, i.rgb)));
  float darker = saturate((luma(mean) - le) * 10.0);
  float3 o = e.rgb;
  o = lerp(o, min(o, mn), edge * darker * strength);
  o += (e.rgb - mean) * 0.6 * strength * (1.0 - darker);
  float al = max(e.a, 1e-4);
  float3 u = saturate(o / al);
  float sat = max(u.r, max(u.g, u.b)) - min(u.r, min(u.g, u.b));
  float lu = luma(u);
  u = lerp(lu.xxx, u, 1.0 + 0.55 * strength * (1.0 - sat));
  u = saturate((u - 0.5) * (1.0 + 0.12 * strength) + 0.5);
  return float4(u * e.a, e.a);
}
)";

const char *kColour = R"(
float4 main(float4 pos : SV_Position) : SV_Target {
  float3 c = src.SampleLevel(samp, pos.xy / uSize, 0).rgb;
  float3 o = saturate(float3(dot(p0.xyz, c), dot(p1.xyz, c), dot(p2.xyz, c)));
  return float4(pow(o, 1.0 / p3.x), 1.0);
}
)";

ID3DBlob *compileBlob(const std::string &body, const char *target, std::string &log) {
  log.clear();
  if (!g_compile)
    return log = "no shader compiler (d3dcompiler_47.dll)", nullptr;
  const std::string source = std::strcmp(target, "vs_4_0") == 0 ? body : std::string(kCommon) + body;
  ID3DBlob *code = nullptr, *errors = nullptr;
  HRESULT hr = g_compile(source.data(), source.size(), "zt1", nullptr, nullptr, "main", target,
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
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

ID3D11PixelShader *pixelShader(const std::string &body, const char *what) {
  std::string log;
  ID3DBlob *code = compileBlob(body, "ps_4_0", log);
  if (!code) {
    SDL_Log("[GpuFsr] Direct3D %s shader didn't compile: %s", what, log.c_str());
    return nullptr;
  }
  ID3D11PixelShader *ps = nullptr;
  g_dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps);
  code->Release();
  return ps;
}

// A texture of ours of w x h, made again when its size or format changes
bool ensure(Tex &t, UINT w, UINT h, DXGI_FORMAT f, bool target) {
  if (t.t && t.w == w && t.h == h && t.f == f)
    return true;
  release(t.rtv);
  release(t.srv);
  release(t.t);
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w;
  d.Height = h;
  d.MipLevels = 1;
  d.ArraySize = 1;
  d.Format = f;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (target ? D3D11_BIND_RENDER_TARGET : 0);
  if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &t.t)) || FAILED(g_dev->CreateShaderResourceView(t.t, nullptr, &t.srv)) ||
      (target && FAILED(g_dev->CreateRenderTargetView(t.t, nullptr, &t.rtv)))) {
    release(t.rtv);
    release(t.srv);
    release(t.t);
    return false;
  }
  t.w = w;
  t.h = h;
  t.f = f;
  return true;
}

// What SDL has bound as its target for the renderer's current one: an
// invisible point drawn (no change to any pixel) makes SDL bind it, then
// it's read back. The caller releases it
ID3D11RenderTargetView *boundTarget(SDL_Renderer *renderer) {
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

ID3D11Texture2D *textureOf(ID3D11RenderTargetView *rtv) {
  if (!rtv)
    return nullptr;
  ID3D11Resource *res = nullptr;
  rtv->GetResource(&res);
  ID3D11Texture2D *tex = nullptr;
  if (res) {
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex));
    res->Release();
  }
  return tex;
}

// The state SDL left bound, kept across a pass and put back exactly
struct Saved {
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
  ID3D11ShaderResourceView *srv = nullptr;
  ID3D11SamplerState *sampler = nullptr;
  ID3D11PixelShader *ps = nullptr;
  ID3D11VertexShader *vs = nullptr;
  ID3D11GeometryShader *gs = nullptr;
  ID3D11Buffer *pscb = nullptr;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ID3D11Buffer *ib = nullptr;
  DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
  UINT ibOffset = 0;
  ID3D11Buffer *vb = nullptr;
  UINT stride = 0, offset = 0;
  ID3D11InputLayout *layout = nullptr;
  ID3D11RenderTargetView *rtv = nullptr;
  ID3D11DepthStencilView *dsv = nullptr;

  void save() {
    g_ctx->RSGetViewports(&views, view);
    g_ctx->RSGetScissorRects(&rects, rect);
    g_ctx->RSGetState(&rs);
    g_ctx->OMGetBlendState(&bs, factor, &mask);
    g_ctx->OMGetDepthStencilState(&ds, &ref);
    g_ctx->PSGetShaderResources(0, 1, &srv);
    g_ctx->PSGetSamplers(0, 1, &sampler);
    g_ctx->PSGetShader(&ps, nullptr, nullptr);
    g_ctx->VSGetShader(&vs, nullptr, nullptr);
    g_ctx->GSGetShader(&gs, nullptr, nullptr);
    g_ctx->PSGetConstantBuffers(0, 1, &pscb);
    g_ctx->IAGetPrimitiveTopology(&topology);
    g_ctx->IAGetIndexBuffer(&ib, &ibFormat, &ibOffset);
    g_ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    g_ctx->IAGetInputLayout(&layout);
    g_ctx->OMGetRenderTargets(1, &rtv, &dsv);
  }
  void restore() {
    g_ctx->RSSetViewports(views, view);
    g_ctx->RSSetScissorRects(rects, rect);
    g_ctx->RSSetState(rs);
    g_ctx->OMSetBlendState(bs, factor, mask);
    g_ctx->OMSetDepthStencilState(ds, ref);
    g_ctx->PSSetShaderResources(0, 1, &srv);
    g_ctx->PSSetSamplers(0, 1, &sampler);
    g_ctx->PSSetShader(ps, nullptr, 0);
    g_ctx->VSSetShader(vs, nullptr, 0);
    g_ctx->GSSetShader(gs, nullptr, 0);
    g_ctx->PSSetConstantBuffers(0, 1, &pscb);
    g_ctx->IASetPrimitiveTopology(topology);
    g_ctx->IASetIndexBuffer(ib, ibFormat, ibOffset);
    g_ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    g_ctx->IASetInputLayout(layout);
    g_ctx->OMSetRenderTargets(1, &rtv, dsv);
    release(rs);
    release(bs);
    release(ds);
    release(srv);
    release(sampler);
    release(ps);
    release(vs);
    release(gs);
    release(pscb);
    release(ib);
    release(vb);
    release(layout);
    release(rtv);
    release(dsv);
  }
};

// One pass: ps over the viewport of target, reading from (unit 0)
void draw(ID3D11RenderTargetView *target, const D3D11_VIEWPORT &vp, ID3D11PixelShader *ps,
          ID3D11ShaderResourceView *from, ID3D11SamplerState *sampler, bool blend, const Consts &c) {
  g_ctx->UpdateSubresource(g_cb, 0, nullptr, &c, 0, 0);
  ID3D11ShaderResourceView *none = nullptr;
  g_ctx->PSSetShaderResources(0, 1, &none); // (from may have been a target)
  g_ctx->OMSetRenderTargets(1, &target, nullptr);
  g_ctx->RSSetViewports(1, &vp);
  g_ctx->RSSetState(g_raster);
  const FLOAT factor[4] = {0, 0, 0, 0};
  g_ctx->OMSetBlendState(blend ? g_premul : nullptr, factor, 0xffffffff);
  g_ctx->OMSetDepthStencilState(nullptr, 0);
  g_ctx->IASetInputLayout(nullptr);
  g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
  ID3D11Buffer *noBuffer = nullptr;
  UINT zero = 0;
  g_ctx->IASetVertexBuffers(0, 1, &noBuffer, &zero, &zero);
  g_ctx->VSSetShader(g_vs, nullptr, 0);
  g_ctx->GSSetShader(nullptr, nullptr, 0);
  g_ctx->PSSetShader(ps, nullptr, 0);
  g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
  g_ctx->PSSetShaderResources(0, 1, &from);
  g_ctx->PSSetSamplers(0, 1, &sampler);
  g_ctx->Draw(3, 0);
  g_ctx->PSSetShaderResources(0, 1, &none);
}

D3D11_VIEWPORT viewport(float x, float y, float w, float h) {
  D3D11_VIEWPORT v{};
  v.TopLeftX = x;
  v.TopLeftY = y;
  v.Width = w;
  v.Height = h;
  v.MinDepth = 0.0f;
  v.MaxDepth = 1.0f;
  return v;
}

// The window's frame copied (for a pass that reads it while drawing over
// it); the window's target view returned, the caller releases it
ID3D11RenderTargetView *copyWindow(SDL_Renderer *renderer, UINT &w, UINT &h) {
  SDL_SetRenderTarget(renderer, nullptr);
  ID3D11RenderTargetView *rtv = boundTarget(renderer);
  ID3D11Texture2D *back = textureOf(rtv);
  if (!back) {
    release(rtv);
    return nullptr;
  }
  D3D11_TEXTURE2D_DESC d{};
  back->GetDesc(&d);
  if (d.SampleDesc.Count != 1 || !ensure(g_copy, d.Width, d.Height, d.Format, false)) {
    back->Release();
    release(rtv);
    return nullptr;
  }
  g_ctx->CopyResource(g_copy.t, back);
  back->Release();
  w = d.Width;
  h = d.Height;
  return rtv;
}

} // namespace

bool init(SDL_Renderer *renderer) {
  g_dev = SDL_RenderGetD3D11Device(renderer);
  if (!g_dev)
    return false;
  if (g_dev->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_0) {
    SDL_Log("[GpuFsr] Direct3D feature level below 10: no shaders");
    release(g_dev);
    return false;
  }
  g_dev->GetImmediateContext(&g_ctx);
  if (HMODULE lib = LoadLibraryA("d3dcompiler_47.dll"))
    g_compile = reinterpret_cast<pD3DCompile>(GetProcAddress(lib, "D3DCompile"));
  if (!g_compile) {
    SDL_Log("[GpuFsr] d3dcompiler_47.dll isn't here: no Direct3D shaders");
    return false;
  }
  std::string log;
  if (ID3DBlob *code = compileBlob(kVertex, "vs_4_0", log)) {
    g_dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_vs);
    code->Release();
  } else {
    SDL_Log("[GpuFsr] Direct3D vertex shader didn't compile: %s", log.c_str());
  }
  g_easu = pixelShader(kEasu, "EASU");
  g_rcas = pixelShader(kRcas, "RCAS");
  g_style = pixelShader(kStyle, "stylized");
  g_colour = pixelShader(kColour, "colour");
  D3D11_BUFFER_DESC b{};
  b.ByteWidth = sizeof(Consts);
  b.Usage = D3D11_USAGE_DEFAULT;
  b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  g_dev->CreateBuffer(&b, nullptr, &g_cb);
  D3D11_SAMPLER_DESC s{};
  s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  s.ComparisonFunc = D3D11_COMPARISON_NEVER;
  s.MaxLOD = D3D11_FLOAT32_MAX;
  s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  g_dev->CreateSamplerState(&s, &g_point);
  s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  g_dev->CreateSamplerState(&s, &g_linear);
  D3D11_BLEND_DESC bd{};
  bd.RenderTarget[0].BlendEnable = TRUE;
  bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  g_dev->CreateBlendState(&bd, &g_premul);
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  g_dev->CreateRasterizerState(&rd, &g_raster);
  const bool ok = g_vs && g_easu && g_rcas && g_cb && g_point && g_linear && g_premul && g_raster;
  SDL_Log("[GpuFsr] Direct3D 11 shaders %s", ok ? "ready" : "unavailable");
  return ok;
}

unsigned compile(const std::string &hlsl, const std::vector<std::string> &params, std::string &log) {
  if (!g_dev)
    return log = "no Direct3D device", 0;
  ID3DBlob *code = compileBlob(hlsl, "ps_4_0", log);
  if (!code)
    return 0;
  Program p;
  g_dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &p.ps);
  code->Release();
  if (!p.ps)
    return log = "the shader couldn't be made", 0;
  p.params = params;
  g_programs.push_back(p);
  return static_cast<unsigned>(g_programs.size());
}

bool screenPass(SDL_Renderer *renderer, unsigned prog, const std::vector<std::pair<std::string, float>> &uniforms,
                bool smooth) {
  if (!g_dev || prog == 0 || prog > g_programs.size())
    return false;
  const Program &p = g_programs[prog - 1];
  SDL_Texture *was = SDL_GetRenderTarget(renderer);
  UINT w = 0, h = 0;
  ID3D11RenderTargetView *window = copyWindow(renderer, w, h);
  if (!window) {
    SDL_SetRenderTarget(renderer, was);
    return false;
  }
  Consts c{};
  c.uSize[0] = float(w);
  c.uSize[1] = float(h);
  c.uTime = SDL_GetTicks() / 1000.0f;
  for (const auto &[name, v] : uniforms)
    for (size_t i = 0; i < p.params.size() && i < 16; i++)
      if (p.params[i] == name)
        c.p[i] = v;
  Saved s;
  s.save();
  draw(window, viewport(0, 0, float(w), float(h)), p.ps, g_copy.srv, smooth ? g_linear : g_point, false, c);
  s.restore();
  window->Release();
  SDL_SetRenderTarget(renderer, was);
  return true;
}

void colourPass(SDL_Renderer *renderer, const float matrix[3][3], float gamma) {
  if (!g_dev || !g_colour)
    return;
  SDL_Texture *was = SDL_GetRenderTarget(renderer);
  UINT w = 0, h = 0;
  ID3D11RenderTargetView *window = copyWindow(renderer, w, h);
  if (!window) {
    SDL_SetRenderTarget(renderer, was);
    return;
  }
  Consts c{};
  c.uSize[0] = float(w);
  c.uSize[1] = float(h);
  for (int r = 0; r < 3; r++)
    for (int k = 0; k < 3; k++)
      c.p[r * 4 + k] = matrix[r][k];
  c.p[12] = gamma;
  Saved s;
  s.save();
  draw(window, viewport(0, 0, float(w), float(h)), g_colour, g_copy.srv, g_point, false, c);
  s.restore();
  window->Release();
  SDL_SetRenderTarget(renderer, was);
}

void endLayer(SDL_Renderer *renderer, int layer, SDL_Texture *low, int lowW, int lowH, float viewW, float viewH,
              bool blend, const SDL_Rect *dstRect, bool styled, float con, float strength) {
  if (!g_dev || layer < 0 || layer > 1)
    return;
  // The layer's own texture (SDL's, the current target): a view on it
  ID3D11RenderTargetView *lowRtv = boundTarget(renderer);
  ID3D11Texture2D *lowTex = textureOf(lowRtv);
  release(lowRtv);
  LowView &lv = g_low[layer];
  if (lowTex && lowTex != lv.res) {
    release(lv.srv);
    lv.res = nullptr;
    if (SUCCEEDED(g_dev->CreateShaderResourceView(lowTex, nullptr, &lv.srv)))
      lv.res = lowTex;
  }
  if (lowTex)
    lowTex->Release();
  (void)low;
  // The window (the target from here on, as GpuFsr's OpenGL endLayer leaves it)
  SDL_SetRenderTarget(renderer, nullptr);
  ID3D11RenderTargetView *window = boundTarget(renderer);
  ID3D11Texture2D *back = textureOf(window);
  if (!lv.srv || !back) {
    if (back)
      back->Release();
    release(window);
    return;
  }
  D3D11_TEXTURE2D_DESC bd{};
  back->GetDesc(&bd);
  back->Release();
  const UINT outW = bd.Width, outH = bd.Height;
  Tex &mid = g_mid[layer];
  if (!ensure(mid, outW, outH, DXGI_FORMAT_B8G8R8A8_UNORM, true)) {
    window->Release();
    return;
  }
  const SDL_Rect dst = dstRect ? *dstRect : SDL_Rect{0, 0, int(outW), int(outH)};
  Saved s;
  s.save();
  // Pass 1: EASU, the layer -> the window-sized buffer, at dst
  Consts c{};
  c.p[0] = float(lowW);
  c.p[1] = float(lowH);
  c.p[2] = std::min(viewW, float(lowW));
  c.p[3] = std::min(viewH, float(lowH));
  c.p[4] = float(dst.x);
  c.p[5] = float(dst.y);
  c.p[6] = float(dst.w);
  c.p[7] = float(dst.h);
  const D3D11_VIEWPORT vp = viewport(float(dst.x), float(dst.y), float(dst.w), float(dst.h));
  draw(mid.rtv, vp, g_easu, lv.srv, g_point, false, c);
  // Pass 2: RCAS (or Stylized) -> the window, at dst
  Consts c2{};
  c2.p[0] = float(outW);
  c2.p[1] = float(outH);
  const bool style = styled && g_style;
  c2.p[2] = style ? strength : con;
  c2.p[4] = float(dst.x);
  c2.p[5] = float(dst.y);
  c2.p[6] = float(dst.w);
  c2.p[7] = float(dst.h);
  draw(window, vp, style ? g_style : g_rcas, mid.srv, g_point, blend, c2);
  s.restore();
  window->Release();
}

} // namespace D3D11
} // namespace GpuFsr

#else // not Windows: no Direct3D

namespace GpuFsr {
namespace D3D11 {
bool init(SDL_Renderer *) { return false; }
unsigned compile(const std::string &, const std::vector<std::string> &, std::string &log) {
  return log = "Direct3D is Windows only", 0;
}
bool screenPass(SDL_Renderer *, unsigned, const std::vector<std::pair<std::string, float>> &, bool) { return false; }
void colourPass(SDL_Renderer *, const float[3][3], float) {}
void endLayer(SDL_Renderer *, int, SDL_Texture *, int, int, float, float, bool, const SDL_Rect *, bool, float, float) {}
} // namespace D3D11
} // namespace GpuFsr

#endif
