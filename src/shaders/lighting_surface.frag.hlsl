// ours: GpuSurfacesD3D11's kPixel for SDL3's GPU renderer: the colour times
// the shape's mask (point-sampled). Built by tools/build_sdlgpu_shaders.py.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D mask : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState samp : register(s0, space2);
struct PSIn { float4 colour : TEXCOORD0; float2 uv : TEXCOORD1; };
// (the mask read a 64th of a texel on, as GpuSurfacesD3D11's kPixel: no
// pixel exactly between two texels, where each card and API picked its own)
float4 main(PSIn i) : SV_Target {
  float w, h;
  mask.GetDimensions(w, h);
  return i.colour * mask.Sample(samp, i.uv + (1.0 / 64.0) / float2(w, h));
}
