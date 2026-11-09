// ours: GpuSurfacesD3D11's kGroundPixel for SDL3's GPU renderer. Built by
// tools/build_sdlgpu_shaders.py.
struct PSIn { float4 colour : TEXCOORD0; };
float4 main(PSIn i) : SV_Target { return i.colour; }
