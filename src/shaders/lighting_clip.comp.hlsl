// ours: GpuSurfacesD3D11's geometry shader (kGeometry) for SDL3's GPU
// renderer, which has no geometry shaders: the CPU's drawSurfaceLighting, a
// face at a time - its corners' colours (byte-rounded as ZT_Vertex's), mask
// coordinates, and clipping to the terrain (clearance below 0) and to its
// sprite's bounds, in that order - written as triangles into the face's own
// 21 vertex slots (a quad clipped by five planes keeps at most 9 corners: 7
// triangles), the slots it doesn't need left as points, so the draw keeps
// the faces' order. Built by tools/build_sdlgpu_shaders.py ("sun" below is
// GpuSunD3D11.cpp's functions).
struct Face { float4 world[4]; float4 screen[4]; float4 normal; float4 local; uint4 info; };
struct Shape { float4 ambient; float4 bounds; float4 uv; uint4 flags; };
struct FaceLight { float4 sun0; float4 sun1; float4 local; };
// (storage buffers as byte buffers: SDL_GPU's Direct3D 12 binds raw views,
// which a StructuredBuffer can't read)
[[vk::binding(0, 0)]] Texture2D<float> sunDepth0 : register(t0, space0);
[[vk::binding(1, 0)]] Texture2D<uint> sunOwner0 : register(t1, space0);
[[vk::binding(2, 0)]] Texture2D<float> sunDepth1 : register(t2, space0);
[[vk::binding(3, 0)]] Texture2D<uint> sunOwner1 : register(t3, space0);
[[vk::binding(4, 0)]] ByteAddressBuffer sunOpacity : register(t4, space0); // float
[[vk::binding(5, 0)]] ByteAddressBuffer statics : register(t5, space0);    // Face, 176 bytes
[[vk::binding(6, 0)]] ByteAddressBuffer moving : register(t6, space0);     // Face
[[vk::binding(7, 0)]] ByteAddressBuffer order : register(t7, space0);      // uint2
[[vk::binding(8, 0)]] ByteAddressBuffer shapes : register(t8, space0);     // Shape, 64 bytes
[[vk::binding(9, 0)]] ByteAddressBuffer lights : register(t9, space0);     // FaceLight, 48 bytes
[[vk::binding(0, 1)]] RWByteAddressBuffer verts : register(u0, space1);    // pos, uv, colour, 0: 24 bytes
Face loadFace(ByteAddressBuffer b, uint i) {
  const uint at = i * 176u;
  Face f;
  [unroll] for (uint k = 0u; k < 4u; ++k) {
    f.world[k] = asfloat(b.Load4(at + k * 16u));
    f.screen[k] = asfloat(b.Load4(at + 64u + k * 16u));
  }
  f.normal = asfloat(b.Load4(at + 128u));
  f.local = asfloat(b.Load4(at + 144u));
  f.info = b.Load4(at + 160u);
  return f;
}
Shape loadShape(uint i) {
  const uint at = i * 64u;
  Shape s;
  s.ambient = asfloat(shapes.Load4(at));
  s.bounds = asfloat(shapes.Load4(at + 16u));
  s.uv = asfloat(shapes.Load4(at + 32u));
  s.flags = shapes.Load4(at + 48u);
  return s;
}
FaceLight loadLight(uint i) {
  const uint at = i * 48u;
  FaceLight l;
  l.sun0 = asfloat(lights.Load4(at));
  l.sun1 = asfloat(lights.Load4(at + 16u));
  l.local = asfloat(lights.Load4(at + 32u));
  return l;
}
[[vk::binding(0, 2)]] cbuffer C : register(b0, space2) {
  float2 origin; float scale; float lamps;
  float4 sun;
  float reference; float shadowK; float sunBlend; float pad;
  float2 target; uint drawCount; uint pad2;
};
[[vk::binding(1, 2)]] cbuffer SunMaps : register(b1, space2) {
  float4 sunMapA[2]; // x0, y0, pixel scale, size
  float4 sunMapB[2]; // dx, dy, span, soften
  uint sunOpacityCount; uint3 sunPad;
};
#include "sun"

static const uint kSlots = 21u;
struct V { float2 screen; float2 uv; float3 colour; float terrain; };
float distanceTo(V p, int plane, float4 b) {
  if (plane == 0) return p.terrain;
  if (plane == 1) return p.screen.x - b.x;
  if (plane == 2) return b.z - p.screen.x;
  if (plane == 3) return p.screen.y - b.y;
  return b.w - p.screen.y;
}
void put(uint at, V v) {
  const float2 pos = float2(v.screen.x / target.x * 2.0 - 1.0, 1.0 - v.screen.y / target.y * 2.0);
  const uint3 c = (uint3)v.colour;
  verts.Store4(at * 24u, asuint(float4(pos, v.uv)));
  verts.Store2(at * 24u + 16u, uint2(c.r | (c.g << 8) | (c.b << 16) | (255u << 24), 0u));
}
// The face's polygon after clipping (kGeometry's arithmetic); 0 corners when
// it isn't drawn
int build(uint index, out V poly[12]) {
  [unroll] for (int z = 0; z < 12; ++z) poly[z] = (V)0;
  const uint2 e = order.Load2(index * 8u);
  const bool isMoving = (e.x & 0x80000000u) != 0u;
  const uint fi = e.x & 0x7fffffffu;
  Face f;
  if (isMoving) f = loadFace(moving, fi);
  else f = loadFace(statics, fi);
  if (f.normal.w < 0.5) return 0;
  const Shape s = loadShape(f.info.x);
  if (s.flags.x == 0u) return 0;
  if (f.world[0].w < 0.0 && f.world[1].w < 0.0 && f.world[2].w < 0.0 && f.world[3].w < 0.0) return 0;
  const bool terrainClip = f.world[0].w < 0.0 || f.world[1].w < 0.0 || f.world[2].w < 0.0 || f.world[3].w < 0.0;
  float l = f.screen[0].x, t = f.screen[0].y, r = l, b = t;
  [unroll] for (int c = 1; c < 4; ++c) { l = min(l, f.screen[c].x); r = max(r, f.screen[c].x); t = min(t, f.screen[c].y); b = max(b, f.screen[c].y); }
  const bool boundsClip = s.flags.y != 0u && (l + origin.x < s.bounds.x || t + origin.y < s.bounds.y ||
                          r + origin.x > s.bounds.x + s.bounds.z || b + origin.y > s.bounds.y + s.bounds.w);
  float4 occ = float4(0.0, 0.0, 0.0, 0.0);
  float3 local = f.local.rgb;
  if (!isMoving) {
    const FaceLight light = loadLight(fi);
    occ = light.sun0 * (1.0 - sunBlend) + light.sun1 * sunBlend;
    local = light.local.rgb;
  }
  [unroll] for (int q = 0; q < 4; ++q)
    if ((e.y & (1u << q)) != 0u) occ[q] = sunShadowPair(f.world[q].xyz, f.normal.xyz, f.info.x + 1u, sunBlend);
  const float ratio = (0.65 + sun.w * max(0.0, f.normal.x * sun.x + f.normal.y * sun.y + f.normal.z * sun.z)) / reference;
  V clipped[12];
  [unroll] for (int z2 = 0; z2 < 12; ++z2) clipped[z2] = (V)0;
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
  return count;
}
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  const uint index = tid.x;
  if (index >= drawCount) return;
  V poly[12];
  const int count = build(index, poly);
  const uint base = index * kSlots;
  V none = (V)0;
  [loop] for (int tri = 0; tri < 7; ++tri) {
    if (tri + 2 < count) {
      put(base + tri * 3, poly[0]);
      put(base + tri * 3 + 1, poly[tri + 1]);
      put(base + tri * 3 + 2, poly[tri + 2]);
    } else {
      put(base + tri * 3, none);
      put(base + tri * 3 + 1, none);
      put(base + tri * 3 + 2, none);
    }
  }
}
