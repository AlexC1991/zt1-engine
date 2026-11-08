#ifndef D3D11_UTIL_HPP
#define D3D11_UTIL_HPP

// ours: what the engine's own Direct3D 11 passes share (GpuSurfacesD3D11):
// SDL's device and immediate context, the shader compiler, keeping and
// putting back the state SDL left bound around a pass, the view SDL has bound
// as its target, and SDL's own shader view of one of its textures. Windows
// only; everything answers "not available" elsewhere or on older hardware.

#include <string>

#include <SDL2/SDL.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>

namespace D3D11Util {

// SDL's device and context, the compiler (d3dcompiler_47.dll) and feature
// level 11 (shader storage in every stage) - false otherwise, once decided
bool ready(SDL_Renderer *renderer);
ID3D11Device *device();
ID3D11DeviceContext *context();
// HLSL compiled (entry point main); 0 and log when it doesn't
ID3DBlob *compile(const std::string &source, const char *target, std::string &log);

// The state SDL left bound (every stage this engine's passes touch), kept
// across a pass and put back exactly, so SDL's own record of it stays true
struct Saved {
  Saved();
  ~Saved();
  Saved(const Saved &) = delete;
  Saved &operator=(const Saved &) = delete;
private:
  struct Impl;
  Impl *impl;
};

// What SDL has bound as the target of the renderer's current one (an
// invisible point drawn makes SDL bind it); the caller releases it
ID3D11RenderTargetView *boundTarget(SDL_Renderer *renderer);
// SDL's own shader view of one of its textures (SDL 2.32's record, checked
// against the texture first), not referenced - 0 when it can't be had
ID3D11ShaderResourceView *viewOf(SDL_Texture *texture);

} // namespace D3D11Util
#endif

#endif // D3D11_UTIL_HPP
