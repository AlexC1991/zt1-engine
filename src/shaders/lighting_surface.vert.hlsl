// ours: the receiving surfaces' triangles as lighting_clip.comp.hlsl wrote
// them (clip-space position, mask coordinates, the byte colour), for SDL3's
// GPU renderer. Built by tools/build_sdlgpu_shaders.py.
struct VSIn { float2 pos : TEXCOORD0; float2 uv : TEXCOORD1; float4 colour : TEXCOORD2; };
struct VSOut { float4 colour : TEXCOORD0; float2 uv : TEXCOORD1; float4 pos : SV_Position; };
VSOut main(VSIn i) {
  VSOut o;
  o.pos = float4(i.pos, 0.0, 1.0);
  o.colour = i.colour;
  o.uv = i.uv;
  return o;
}
