#ifndef GPU_UPLOAD_SDLGPU_HPP
#define GPU_UPLOAD_SDLGPU_HPP

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

// ours: new textures' pixels on SDL3's GPU renderer sent on the engine's own
// command buffer. SDL_CreateTextureFromSurface there makes and frees an
// upload buffer for every texture (about half a millisecond each on
// Direct3D 12, a third on Vulkan - Direct3D 11 takes 0.04), and the map art
// is made a frame at a time as it's first drawn, so new animals and objects
// coming into view stalled the game. Here each new texture's pixels go into
// one large upload buffer, recorded at once into a copy pass of ours (so SDL
// keeps the texture until it's done), and the batch is sent before SDL sends
// its frame (Sdl3Compat.hpp's hooks: at present, before reading pixels back,
// or when the buffer is full). The same texture as SDL's in every way; what
// this can't do the same (palettes, colour keys, other formats) SDL does.
namespace GpuUploadSdlGpu {

// Sets the hooks on SDL's GPU renderer (device: its own)
void install(SDL_Renderer *renderer, SDL_GPUDevice *device);
// Sends what's been recorded (the hooks' flush)
void flush(SDL_Renderer *renderer);
// SDL_UpdateTexture of a 4-byte texture's rect (all of it: null) sent this
// way; false when it can't be (not this renderer: then SDL's). Only for a
// texture the engine's passes read and SDL draws no earlier in the frame
// (the lighting's mask atlas): SDL's frame is sent after it
bool update(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);

} // namespace GpuUploadSdlGpu

#endif // GPU_UPLOAD_SDLGPU_HPP
