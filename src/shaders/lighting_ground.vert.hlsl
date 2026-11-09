// ours: GpuSurfacesD3D11's kGroundGeometry for SDL3's GPU renderer, as a
// vertex shader (no geometry shaders there): six vertices a quad, SDL's two
// triangles a, b, c and a, c, d. Each point's shadow - from the sun maps as
// made for the ground, or asked now near a moving actor - as a byte of
// black's alpha (Uint8 of 100 x the shadow's strength). Built by
// tools/build_sdlgpu_shaders.py ("sun" below is GpuSunD3D11.cpp's functions).
// (storage buffers as byte buffers: SDL_GPU's Direct3D 12 binds raw views,
// which a StructuredBuffer can't read)
[[vk::binding(0, 0)]] Texture2D<float> sunDepth0 : register(t0, space0);
[[vk::binding(1, 0)]] Texture2D<uint> sunOwner0 : register(t1, space0);
[[vk::binding(2, 0)]] Texture2D<float> sunDepth1 : register(t2, space0);
[[vk::binding(3, 0)]] Texture2D<uint> sunOwner1 : register(t3, space0);
[[vk::binding(4, 0)]] ByteAddressBuffer sunOpacity : register(t4, space0); // float
[[vk::binding(5, 0)]] ByteAddressBuffer points : register(t5, space0);     // world, normal, screen: 48 bytes
[[vk::binding(6, 0)]] ByteAddressBuffer quads : register(t6, space0);      // uint4
[[vk::binding(7, 0)]] ByteAddressBuffer order : register(t7, space0);      // uint
[[vk::binding(8, 0)]] ByteAddressBuffer live : register(t8, space0);       // uint
[[vk::binding(9, 0)]] ByteAddressBuffer pointSun : register(t9, space0);   // float2
[[vk::binding(0, 1)]] cbuffer G : register(b0, space1) {
  float2 origin; float strength; float sunBlend;
  float2 renderScale; float2 viewSize;
};
[[vk::binding(1, 1)]] cbuffer SunMaps : register(b1, space1) {
  float4 sunMapA[2]; // x0, y0, pixel scale, size
  float4 sunMapB[2]; // dx, dy, span, soften
  uint sunOpacityCount; uint3 sunPad;
};
#include "sun"

struct VSOut { float4 colour : TEXCOORD0; float4 pos : SV_Position; };
VSOut main(uint id : SV_VertexID) {
  const uint4 q = quads.Load4(order.Load((id / 6u) * 4u) * 16u);
  const uint k = id % 6u;
  // (a table, not a chain of ?: - the chain came out wrong on Vulkan: every
  // corner the same, so nothing drawn)
  const uint corners[6] = {q.x, q.y, q.z, q.x, q.z, q.w};
  const uint pi = corners[k];
  const float3 world = asfloat(points.Load3(pi * 48u));
  const float3 normal = asfloat(points.Load3(pi * 48u + 16u));
  const float2 screen = asfloat(points.Load2(pi * 48u + 32u));
  float amount;
  if (((live.Load((pi >> 5) * 4u) >> (pi & 31u)) & 1u) != 0u) amount = sunShadowPair(world, normal, 0xffffffffu, sunBlend);
  else {
    const float2 sun = asfloat(pointSun.Load2(pi * 8u));
    amount = sun.x * (1.0 - sunBlend) + sun.y * sunBlend;
  }
  VSOut o;
  const float2 at = (screen + origin) * renderScale / viewSize;
  o.pos = float4(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0, 0.0, 1.0);
  o.colour = float4(0.0, 0.0, 0.0, floor(strength * amount) / 255.0);
  return o;
}
