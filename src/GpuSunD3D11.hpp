#ifndef GPU_SUN_D3D11_HPP
#define GPU_SUN_D3D11_HPP

#include <vector>

#include "LightingSun.hpp"

// ours: the day and night lighting's two sun maps (LightingAssets::SunDepth)
// kept on the graphics card for the engine's Direct3D 11 passes
// (GpuSurfacesD3D11), so a shader asks the sun's shadow at a point as the
// CPU's SunDepth::shadow does. A map is sent whole only when it is a new one
// (its stamp); after that, a frame sends only the tiles where the zoo's
// moving actors were and are now (SunDepth::actorRegions).
namespace GpuSunD3D11 {

// HLSL: the maps at t5-t9 and their settings at b1, and
// float sunShadowPair(float3 p, float3 normal, uint receiver, float blend)
extern const char *kHlsl;
// Both maps (and how much each caster stops) brought up to date for this
// frame (DayNightLighting's receiverFrame, one more each updateSun): sent once
// a frame however many passes ask; the changes only when the last frame's
// were sent, else whole. False when they can't be (the CPU's pass then)
bool upload(const LightingAssets::SunDepth (&maps)[2], const std::vector<float> *opacity, uint64_t frame);
// Binds and unbinds kHlsl's resources on the geometry shader stage
void bindGeometry();
void unbindGeometry();

} // namespace GpuSunD3D11

#endif // GPU_SUN_D3D11_HPP
