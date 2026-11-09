// ours: a lighting pass's layer (GpuLightingSdlGpu) laid onto SDL's target
// texel for texel, as an SDL_GPURenderState on an SDL_RenderTexture draw: no
// colour conversion of the float layer, which SDL's own shaders would do.
// The inputs are SDL's vertex shader's, as its DXIL names them. Built by
// tools/build_sdlgpu_shaders.py.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D src : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState samp : register(s0, space2);
struct PSInput { float4 v_color : TEXCOORD0; float2 v_uv : TEXCOORD1; float4 pos : SV_Position; };
float4 main(PSInput input) : SV_Target { return src.Load(int3(input.pos.xy, 0)); }
