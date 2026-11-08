#include "GpuSurfacesD3D11.hpp"

// ours: see GpuSurfacesD3D11.hpp

#include "D3D11Util.hpp"
#include "GpuSunD3D11.hpp"

#ifdef _WIN32
#include <algorithm>
#include <cstring>
#include <string>

namespace GpuSurfacesD3D11 {
namespace {

template <typename T> void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

bool g_tried = false, g_ready = false;
ID3D11VertexShader *g_vs = nullptr;
ID3D11GeometryShader *g_gs = nullptr;
ID3D11PixelShader *g_ps = nullptr;
ID3D11Buffer *g_cb = nullptr;
ID3D11SamplerState *g_point = nullptr;
ID3D11BlendState *g_blend = nullptr;
ID3D11RasterizerState *g_raster = nullptr;
ID3D11GeometryShader *g_groundGs = nullptr;
ID3D11PixelShader *g_groundPs = nullptr;
ID3D11Buffer *g_groundCb = nullptr;

// A structured buffer and its view, grown as needed
struct Structured {
  ID3D11Buffer *buffer = nullptr;
  ID3D11ShaderResourceView *srv = nullptr;
  UINT capacity = 0, stride = 0;
  bool dynamic = true;
  bool fill(const void *data, size_t count, UINT elementStride) {
    auto *dev = D3D11Util::device();
    auto *ctx = D3D11Util::context();
    const UINT need = UINT(std::max<size_t>(count, 1));
    if (!buffer || capacity < need || stride != elementStride || !dynamic) {
      release(srv);
      release(buffer);
      capacity = dynamic ? std::max(need, capacity * 2) : need;
      stride = elementStride;
      D3D11_BUFFER_DESC d{};
      d.ByteWidth = capacity * stride;
      d.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
      d.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
      d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      d.StructureByteStride = stride;
      D3D11_SUBRESOURCE_DATA init{};
      std::vector<uint8_t> zeros;
      if (!dynamic) {
        if (count) {
          init.pSysMem = data;
        } else {
          zeros.assign(stride, 0);
          init.pSysMem = zeros.data();
        }
      }
      if (FAILED(dev->CreateBuffer(&d, dynamic ? nullptr : &init, &buffer)))
        return false;
      D3D11_SHADER_RESOURCE_VIEW_DESC v{};
      v.Format = DXGI_FORMAT_UNKNOWN;
      v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
      v.Buffer.FirstElement = 0;
      v.Buffer.NumElements = capacity;
      if (FAILED(dev->CreateShaderResourceView(buffer, &v, &srv))) {
        release(buffer);
        return false;
      }
      if (!dynamic)
        return true;
    }
    if (!count)
      return true;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
      return false;
    std::memcpy(m.pData, data, count * stride);
    ctx->Unmap(buffer, 0);
    return true;
  }
};
Structured g_faces, g_actors, g_order, g_shapes, g_lights;
uint64_t g_facesKey = ~0ull, g_lightsKey = ~0ull;
Structured g_points, g_quads, g_groundOrder, g_live, g_pointSun;
uint64_t g_groundKey = ~0ull, g_pointSunKey = ~0ull;

struct GroundConstants {
  float origin[2], strength, sunBlend;
  float renderScale[2], viewSize[2];
};

struct Constants {
  float origin[2], scale, lamps;
  float sun[4]; // xyz, direct
  float reference, shadow, sunBlend, pad;
  float target[2], pad2[2];
};

const char *kVertex = R"(
struct VSOut { uint index : INDEX; };
VSOut main(uint id : SV_VertexID) { VSOut o; o.index = id; return o; }
)";

// The CPU's drawSurfaceLighting, a face at a time: its corners' colours
// (byte-rounded as SDL_Vertex's), mask coordinates, and clipping to the
// terrain (clearance below 0) and to its sprite's bounds, in that order
const char *kGeometry = R"(
#include "sun"
struct Face { float4 world[4]; float4 screen[4]; float4 normal; float4 local; uint4 info; };
struct Shape { float4 ambient; float4 bounds; float4 uv; uint4 flags; };
struct FaceLight { float4 sun0; float4 sun1; float4 local; };
StructuredBuffer<Face> statics : register(t0);
StructuredBuffer<Face> moving : register(t1);
StructuredBuffer<uint2> order : register(t2);
StructuredBuffer<Shape> shapes : register(t3);
StructuredBuffer<FaceLight> lights : register(t4);
cbuffer C : register(b0) {
  float2 origin; float scale; float lamps;
  float4 sun;
  float reference; float shadowK; float sunBlend; float pad;
  float2 target; float2 pad2;
};
struct VSOut { uint index : INDEX; };
struct GSOut { float4 pos : SV_Position; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
struct V { float2 screen; float2 uv; float3 colour; float terrain; };
float distanceTo(V p, int plane, float4 b) {
  if (plane == 0) return p.terrain;
  if (plane == 1) return p.screen.x - b.x;
  if (plane == 2) return b.z - p.screen.x;
  if (plane == 3) return p.screen.y - b.y;
  return b.w - p.screen.y;
}
GSOut emitVertex(V v) {
  GSOut o;
  o.pos = float4(v.screen.x / target.x * 2.0 - 1.0, 1.0 - v.screen.y / target.y * 2.0, 0.0, 1.0);
  o.colour = float4(v.colour / 255.0, 1.0);
  o.uv = v.uv;
  return o;
}
[maxvertexcount(30)]
void main(point VSOut input[1], inout TriangleStream<GSOut> stream) {
  const uint2 e = order[input[0].index];
  const bool isMoving = (e.x & 0x80000000u) != 0u;
  const uint fi = e.x & 0x7fffffffu;
  Face f;
  if (isMoving) f = moving[fi];
  else f = statics[fi];
  if (f.normal.w < 0.5) return;
  const Shape s = shapes[f.info.x];
  if (s.flags.x == 0u) return;
  if (f.world[0].w < 0.0 && f.world[1].w < 0.0 && f.world[2].w < 0.0 && f.world[3].w < 0.0) return;
  const bool terrainClip = f.world[0].w < 0.0 || f.world[1].w < 0.0 || f.world[2].w < 0.0 || f.world[3].w < 0.0;
  float l = f.screen[0].x, t = f.screen[0].y, r = l, b = t;
  [unroll] for (int c = 1; c < 4; ++c) { l = min(l, f.screen[c].x); r = max(r, f.screen[c].x); t = min(t, f.screen[c].y); b = max(b, f.screen[c].y); }
  const bool boundsClip = s.flags.y != 0u && (l + origin.x < s.bounds.x || t + origin.y < s.bounds.y ||
                          r + origin.x > s.bounds.x + s.bounds.z || b + origin.y > s.bounds.y + s.bounds.w);
  // (the sun maps' shadow as made for the scenery, or asked now at the
  // corners a moving actor's shadow may reach and at every actor's)
  float4 occ = float4(0.0, 0.0, 0.0, 0.0);
  float3 local = f.local.rgb;
  if (!isMoving) {
    const FaceLight light = lights[fi];
    occ = light.sun0 * (1.0 - sunBlend) + light.sun1 * sunBlend;
    local = light.local.rgb;
  }
  [unroll] for (int q = 0; q < 4; ++q)
    if ((e.y & (1u << q)) != 0u) occ[q] = sunShadowPair(f.world[q].xyz, f.normal.xyz, f.info.x + 1u, sunBlend);
  const float ratio = (0.65 + sun.w * max(0.0, f.normal.x * sun.x + f.normal.y * sun.y + f.normal.z * sun.z)) / reference;
  V poly[12];
  V clipped[12];
  [unroll] for (int z = 0; z < 12; ++z) { poly[z] = (V)0; clipped[z] = (V)0; }
  int count = 4;
  [unroll] for (int k = 0; k < 4; ++k) {
    const float visibility = 1.0 - 0.39 * shadowK * occ[k];
    const float x = f.screen[k].x + origin.x, y = f.screen[k].y + origin.y;
    float2 uv = s.uv.xy;
    if (s.flags.y != 0u) {
      float u = (x - s.bounds.x) / s.bounds.z, v = (y - s.bounds.y) / s.bounds.w;
      if (s.flags.z != 0u) u = 1.0 - u;
      uv = float2(s.uv.x + u * s.uv.z, s.uv.y + v * s.uv.w);
    }
    poly[k].screen = float2(x * scale, y * scale);
    poly[k].uv = uv;
    poly[k].terrain = f.world[k].w;
    poly[k].colour = floor(clamp(s.ambient.rgb * ratio * visibility + 255.0 * local * lamps, 0.0, 255.0));
  }
  const float4 bounds = float4(s.bounds.x * scale, s.bounds.y * scale, (s.bounds.x + s.bounds.z) * scale, (s.bounds.y + s.bounds.w) * scale);
  [loop] for (int plane = 0; plane <= 4; ++plane) {
    if (plane == 0 && !terrainClip) continue;
    if (plane > 0 && !boundsClip) break;
    if (count == 0) break;
    int outCount = 0;
    V a = poly[count - 1];
    float da = distanceTo(a, plane, bounds);
    for (int e2 = 0; e2 < count; ++e2) {
      V bb = poly[e2];
      const float db = distanceTo(bb, plane, bounds);
      if ((da >= 0.0) != (db >= 0.0) && outCount < 12) {
        const float tt = da / (da - db);
        V c;
        c.terrain = a.terrain + (bb.terrain - a.terrain) * tt;
        c.screen = a.screen + (bb.screen - a.screen) * tt;
        c.uv = a.uv + (bb.uv - a.uv) * tt;
        c.colour = floor(a.colour + (bb.colour - a.colour) * tt);
        clipped[outCount++] = c;
      }
      if (db >= 0.0 && outCount < 12) clipped[outCount++] = bb;
      a = bb;
      da = db;
    }
    count = outCount;
    for (int k2 = 0; k2 < count; ++k2) poly[k2] = clipped[k2];
  }
  for (int tri = 1; tri + 1 < count; ++tri) {
    stream.Append(emitVertex(poly[0]));
    stream.Append(emitVertex(poly[tri]));
    stream.Append(emitVertex(poly[tri + 1]));
    stream.RestartStrip();
  }
}
)";

const char *kPixel = R"(
Texture2D mask : register(t0);
SamplerState samp : register(s0);
struct GSOut { float4 pos : SV_Position; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
float4 main(GSOut i) : SV_Target { return i.colour * mask.Sample(samp, i.uv); }
)";

// The CPU's drawShadows: each point's shadow - from the sun maps as made for
// the ground, or asked now near a moving actor - as a byte of black's alpha
// (Uint8 of 100 x the shadow's strength), the quad as SDL's two triangles
const char *kGroundGeometry = R"(
#include "sun"
struct Point { float4 world; float4 normal; float4 screen; };
StructuredBuffer<Point> points : register(t0);
StructuredBuffer<uint4> quads : register(t1);
StructuredBuffer<uint> order : register(t2);
StructuredBuffer<uint> live : register(t3);
StructuredBuffer<float2> pointSun : register(t4);
cbuffer G : register(b0) {
  float2 origin; float strength; float sunBlend;
  float2 renderScale; float2 viewSize;
};
struct VSOut { uint index : INDEX; };
struct GSOut { float4 pos : SV_Position; float4 colour : COLOR0; };
GSOut corner(uint pi) {
  const Point p = points[pi];
  float amount;
  if (((live[pi >> 5] >> (pi & 31u)) & 1u) != 0u) amount = sunShadowPair(p.world.xyz, p.normal.xyz, 0xffffffffu, sunBlend);
  else amount = pointSun[pi].x * (1.0 - sunBlend) + pointSun[pi].y * sunBlend;
  GSOut o;
  const float2 at = (p.screen.xy + origin) * renderScale / viewSize;
  o.pos = float4(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0, 0.0, 1.0);
  o.colour = float4(0.0, 0.0, 0.0, floor(strength * amount) / 255.0);
  return o;
}
[maxvertexcount(6)]
void main(point VSOut input[1], inout TriangleStream<GSOut> stream) {
  const uint4 q = quads[order[input[0].index]];
  const GSOut a = corner(q.x), b = corner(q.y), c = corner(q.z), d = corner(q.w);
  stream.Append(a); stream.Append(b); stream.Append(c); stream.RestartStrip();
  stream.Append(a); stream.Append(c); stream.Append(d); stream.RestartStrip();
}
)";

const char *kGroundPixel = R"(
struct GSOut { float4 pos : SV_Position; float4 colour : COLOR0; };
float4 main(GSOut i) : SV_Target { return i.colour; }
)";

bool setup() {
  auto *dev = D3D11Util::device();
  std::string log;
  auto build = [&](const char *src, const char *target) {
    ID3DBlob *b = D3D11Util::compile(src, target, log);
    if (!b)
      SDL_Log("[GpuSurfaces] a %s shader didn't compile: %s", target, log.c_str());
    return b;
  };
  auto withSun = [](const char *source) {
    std::string s = source;
    s.replace(s.find("#include \"sun\""), std::strlen("#include \"sun\""), GpuSunD3D11::kHlsl);
    return s;
  };
  ID3DBlob *vs = build(kVertex, "vs_5_0"), *gs = build(withSun(kGeometry).c_str(), "gs_5_0"), *ps = build(kPixel, "ps_5_0");
  ID3DBlob *groundGs = build(withSun(kGroundGeometry).c_str(), "gs_5_0"), *groundPs = build(kGroundPixel, "ps_5_0");
  if (groundGs)
    dev->CreateGeometryShader(groundGs->GetBufferPointer(), groundGs->GetBufferSize(), nullptr, &g_groundGs);
  if (groundPs)
    dev->CreatePixelShader(groundPs->GetBufferPointer(), groundPs->GetBufferSize(), nullptr, &g_groundPs);
  release(groundGs);
  release(groundPs);
  if (vs)
    dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs);
  if (gs)
    dev->CreateGeometryShader(gs->GetBufferPointer(), gs->GetBufferSize(), nullptr, &g_gs);
  if (ps)
    dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps);
  release(vs);
  release(gs);
  release(ps);
  D3D11_BUFFER_DESC b{};
  b.ByteWidth = sizeof(Constants);
  b.Usage = D3D11_USAGE_DEFAULT;
  b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  dev->CreateBuffer(&b, nullptr, &g_cb);
  b.ByteWidth = sizeof(GroundConstants);
  dev->CreateBuffer(&b, nullptr, &g_groundCb);
  D3D11_SAMPLER_DESC s{};
  s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  s.ComparisonFunc = D3D11_COMPARISON_NEVER;
  s.MaxLOD = D3D11_FLOAT32_MAX;
  dev->CreateSamplerState(&s, &g_point);
  // (as SDL's SDL_BLENDMODE_BLEND)
  D3D11_BLEND_DESC bd{};
  auto &rt = bd.RenderTarget[0];
  rt.BlendEnable = TRUE;
  rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
  rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  rt.BlendOp = D3D11_BLEND_OP_ADD;
  rt.SrcBlendAlpha = D3D11_BLEND_ONE;
  rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  dev->CreateBlendState(&bd, &g_blend);
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  dev->CreateRasterizerState(&rd, &g_raster);
  g_faces.dynamic = false;
  g_points.dynamic = false;
  g_quads.dynamic = false;
  return g_vs && g_gs && g_ps && g_cb && g_point && g_blend && g_raster && g_groundGs && g_groundPs && g_groundCb;
}

} // namespace

bool available(SDL_Renderer *renderer) {
  if (g_tried)
    return g_ready;
  g_tried = true;
  const char *cpu = SDL_getenv("ZT_LIGHTING_CPU");
  if (cpu && *cpu && std::strcmp(cpu, "0") != 0)
    return false;
  g_ready = D3D11Util::ready(renderer) && setup();
  SDL_Log("[GpuSurfaces] receiving surfaces on the graphics card: %s", g_ready ? "yes" : "no (CPU)");
  return g_ready;
}

void setFaces(const std::vector<Face> &faces, uint64_t key) {
  if (!g_ready)
    return;
  g_faces.fill(faces.data(), faces.size(), sizeof(Face));
  g_facesKey = key;
}

uint64_t facesKey() { return g_facesKey; }

void setFaceLight(const std::vector<FaceLight> &light, uint64_t key) {
  if (!g_ready)
    return;
  g_lightsKey = g_lights.fill(light.data(), light.size(), sizeof(FaceLight)) ? key : ~0ull;
}

uint64_t faceLightKey() { return g_lightsKey; }

bool draw(SDL_Renderer *renderer, SDL_Texture *mask, const std::vector<Draw> &order, const std::vector<Face> &actorFaces,
          const std::vector<Shape> &shapes, const Frame &frame) {
  if (!g_ready || !g_faces.srv || !g_lights.srv || g_lightsKey == ~0ull)
    return false;
  if (order.empty())
    return true;
  ID3D11ShaderResourceView *maskView = D3D11Util::viewOf(mask);
  if (!maskView)
    return false;
  if (!g_actors.fill(actorFaces.data(), actorFaces.size(), sizeof(Face)) ||
      !g_order.fill(order.data(), order.size(), sizeof(Draw)) ||
      !g_shapes.fill(shapes.data(), shapes.size(), sizeof(Shape)))
    return false;
  ID3D11RenderTargetView *target = D3D11Util::boundTarget(renderer);
  if (!target)
    return false;
  auto *ctx = D3D11Util::context();
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
  {
    D3D11Util::Saved saved;
    ctx->UpdateSubresource(g_cb, 0, nullptr, &c, 0, 0);
    ctx->OMSetRenderTargets(1, &target, nullptr);
    D3D11_VIEWPORT vp{};
    vp.Width = float(frame.targetW);
    vp.Height = float(frame.targetH);
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(g_raster);
    const FLOAT factor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(g_blend, factor, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11Buffer *none = nullptr;
    UINT zero = 0;
    ctx->IASetVertexBuffers(0, 1, &none, &zero, &zero);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->GSSetShader(g_gs, nullptr, 0);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ID3D11ShaderResourceView *gsViews[5] = {g_faces.srv, g_actors.srv, g_order.srv, g_shapes.srv, g_lights.srv};
    ctx->GSSetShaderResources(0, 5, gsViews);
    GpuSunD3D11::bindGeometry();
    ctx->GSSetConstantBuffers(0, 1, &g_cb);
    ctx->PSSetShaderResources(0, 1, &maskView);
    ctx->PSSetSamplers(0, 1, &g_point);
    ctx->Draw(UINT(order.size()), 0);
    ID3D11ShaderResourceView *clear[5] = {};
    ctx->GSSetShaderResources(0, 5, clear);
    GpuSunD3D11::unbindGeometry();
  }
  target->Release();
  return true;
}

void setGround(const std::vector<GroundPoint> &points, const std::vector<std::array<uint32_t, 4>> &quads, uint64_t key) {
  if (!g_ready)
    return;
  g_groundKey = g_points.fill(points.data(), points.size(), sizeof(GroundPoint)) &&
                        g_quads.fill(quads.data(), quads.size(), sizeof(uint32_t) * 4)
                    ? key
                    : ~0ull;
}

uint64_t groundKey() { return g_groundKey; }

void setGroundSun(const std::vector<std::array<float, 2>> &sun, uint64_t key) {
  if (!g_ready)
    return;
  g_pointSunKey = g_pointSun.fill(sun.data(), sun.size(), sizeof(float) * 2) ? key : ~0ull;
}

uint64_t groundSunKey() { return g_pointSunKey; }

bool setGroundLive(const std::vector<uint32_t> &bits) {
  return g_ready && g_live.fill(bits.data(), bits.size(), sizeof(uint32_t));
}

bool drawGround(SDL_Renderer *renderer, const std::vector<uint32_t> &quads, const GroundFrame &frame) {
  if (!g_ready || !g_points.srv || !g_quads.srv || !g_live.srv || !g_pointSun.srv || g_groundKey == ~0ull || g_pointSunKey == ~0ull)
    return false;
  if (quads.empty())
    return true;
  if (!g_groundOrder.fill(quads.data(), quads.size(), sizeof(uint32_t)))
    return false;
  ID3D11RenderTargetView *target = D3D11Util::boundTarget(renderer);
  if (!target)
    return false;
  auto *ctx = D3D11Util::context();
  // (SDL's own view, clip and rasteriser, as it left them binding the target:
  // its vertices are the logical ones times its scale, over that view)
  D3D11_VIEWPORT vp{};
  UINT views = 1;
  ctx->RSGetViewports(&views, &vp);
  float sx = 1, sy = 1;
  SDL_RenderGetScale(renderer, &sx, &sy);
  if (!views || vp.Width <= 0 || vp.Height <= 0) {
    target->Release();
    return false;
  }
  GroundConstants c{};
  c.origin[0] = frame.originX;
  c.origin[1] = frame.originY;
  c.strength = frame.strength;
  c.sunBlend = frame.sunBlend;
  c.renderScale[0] = sx;
  c.renderScale[1] = sy;
  c.viewSize[0] = vp.Width;
  c.viewSize[1] = vp.Height;
  {
    D3D11Util::Saved saved;
    ctx->UpdateSubresource(g_groundCb, 0, nullptr, &c, 0, 0);
    ctx->OMSetRenderTargets(1, &target, nullptr);
    const FLOAT factor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(g_blend, factor, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11Buffer *none = nullptr;
    UINT zero = 0;
    ctx->IASetVertexBuffers(0, 1, &none, &zero, &zero);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->VSSetShader(g_vs, nullptr, 0);
    ctx->GSSetShader(g_groundGs, nullptr, 0);
    ctx->PSSetShader(g_groundPs, nullptr, 0);
    ID3D11ShaderResourceView *gsViews[5] = {g_points.srv, g_quads.srv, g_groundOrder.srv, g_live.srv, g_pointSun.srv};
    ctx->GSSetShaderResources(0, 5, gsViews);
    GpuSunD3D11::bindGeometry();
    ctx->GSSetConstantBuffers(0, 1, &g_groundCb);
    ctx->Draw(UINT(quads.size()), 0);
    static bool said = false;
    if (!said) {
      said = true;
      SDL_Log("[GpuSurfaces] the ground's shadow on the graphics card: %u quads", unsigned(quads.size()));
    }
    ID3D11ShaderResourceView *clear[5] = {};
    ctx->GSSetShaderResources(0, 5, clear);
    GpuSunD3D11::unbindGeometry();
  }
  target->Release();
  return true;
}

} // namespace GpuSurfacesD3D11

#else

namespace GpuSurfacesD3D11 {
bool available(SDL_Renderer *) { return false; }
void setFaces(const std::vector<Face> &, uint64_t) {}
uint64_t facesKey() { return ~0ull; }
bool draw(SDL_Renderer *, SDL_Texture *, const std::vector<Draw> &, const std::vector<Face> &, const std::vector<Shape> &,
          const Frame &) {
  return false;
}
void setGround(const std::vector<GroundPoint> &, const std::vector<std::array<uint32_t, 4>> &, uint64_t) {}
uint64_t groundKey() { return ~0ull; }
void setGroundSun(const std::vector<std::array<float, 2>> &, uint64_t) {}
uint64_t groundSunKey() { return ~0ull; }
void setFaceLight(const std::vector<FaceLight> &, uint64_t) {}
uint64_t faceLightKey() { return ~0ull; }
bool setGroundLive(const std::vector<uint32_t> &) { return false; }
bool drawGround(SDL_Renderer *, const std::vector<uint32_t> &, const GroundFrame &) { return false; }
} // namespace GpuSurfacesD3D11
#endif
