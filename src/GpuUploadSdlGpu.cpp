#include "GpuUploadSdlGpu.hpp"

// ours: see GpuUploadSdlGpu.hpp

#include <algorithm>
#include <cstring>

// (this file makes the hooks, so it calls SDL's own functions)
#undef SDL_RenderReadPixels
#undef SDL_CreateTextureFromSurface

namespace GpuUploadSdlGpu {
namespace {

SDL_GPUDevice *g_dev = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_GPUCommandBuffer *g_cmd = nullptr;
SDL_GPUCopyPass *g_copy = nullptr;
SDL_GPUTransferBuffer *g_transfer = nullptr;
Uint32 g_capacity = 0, g_used = 0;
const SDL_PixelFormat *g_formats = nullptr; // (the renderer's, ended by UNKNOWN)

constexpr Uint32 kStart = 16u << 20;   // (the upload buffer's first size)
constexpr Uint32 kPlacement = 512;     // (Direct3D 12's texture upload alignment)
constexpr Uint32 kRowPitch = 256;      // (and its row alignment)

Uint32 roundUp(Uint32 v, Uint32 to) { return (v + to - 1) / to * to; }

bool takes(SDL_PixelFormat f) {
  for (const SDL_PixelFormat *p = g_formats; p && *p != SDL_PIXELFORMAT_UNKNOWN; ++p)
    if (*p == f)
      return true;
  return false;
}

// An upload buffer of at least bytes, empty (the last one sent first)
bool room(SDL_Renderer *renderer, Uint32 bytes) {
  if (g_transfer && g_used + bytes <= g_capacity)
    return true;
  flush(renderer);
  if (g_transfer && bytes <= g_capacity)
    return true;
  if (g_transfer)
    SDL_ReleaseGPUTransferBuffer(g_dev, g_transfer);
  g_capacity = std::max(kStart, roundUp(bytes, 1u << 20));
  SDL_GPUTransferBufferCreateInfo info{};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  info.size = g_capacity;
  g_transfer = SDL_CreateGPUTransferBuffer(g_dev, &info);
  if (!g_transfer)
    g_capacity = 0;
  return g_transfer != nullptr;
}

SDL_GPUTexture *gpuOf(SDL_Texture *t) {
  return static_cast<SDL_GPUTexture *>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
}

// w x h 4-byte pixels (rows pitch apart) into gpu at x, y: written into the
// upload buffer and recorded on our copy pass
bool record(SDL_Renderer *renderer, SDL_GPUTexture *gpu, int x, int y, int w, int h, const void *pixels, int pitch) {
  const Uint32 rowPixels = roundUp(Uint32(w), kRowPitch / 4);
  const Uint32 bytes = roundUp(rowPixels * 4 * Uint32(h), kPlacement);
  if (bytes > (256u << 20) || !room(renderer, bytes))
    return false;
  auto *to = static_cast<Uint8 *>(SDL_MapGPUTransferBuffer(g_dev, g_transfer, g_used == 0));
  if (!to)
    return false;
  const Uint32 at = g_used;
  for (int row = 0; row < h; ++row)
    std::memcpy(to + at + size_t(row) * rowPixels * 4, static_cast<const Uint8 *>(pixels) + size_t(row) * pitch,
                size_t(w) * 4);
  SDL_UnmapGPUTransferBuffer(g_dev, g_transfer);
  if (!g_cmd) {
    g_cmd = SDL_AcquireGPUCommandBuffer(g_dev);
    g_copy = g_cmd ? SDL_BeginGPUCopyPass(g_cmd) : nullptr;
    if (!g_copy) {
      if (g_cmd)
        SDL_CancelGPUCommandBuffer(g_cmd);
      g_cmd = nullptr;
      return false;
    }
  }
  SDL_GPUTextureTransferInfo from{};
  from.transfer_buffer = g_transfer;
  from.offset = at;
  from.pixels_per_row = rowPixels;
  from.rows_per_layer = Uint32(h);
  SDL_GPUTextureRegion region{};
  region.texture = gpu;
  region.x = Uint32(x);
  region.y = Uint32(y);
  region.w = Uint32(w);
  region.h = Uint32(h);
  region.d = 1;
  SDL_UploadToGPUTexture(g_copy, &from, &region, false);
  g_used = at + bytes;
  return true;
}

// SDL_CreateTextureFromSurface's texture, its pixels sent on our copy pass;
// null (SDL makes it instead) for what SDL would convert or treat specially
SDL_Texture *create(SDL_Renderer *renderer, SDL_Surface *s) {
  if (!g_dev || !s || !s->pixels || s->w <= 0 || s->h <= 0 || SDL_ISPIXELFORMAT_INDEXED(s->format) ||
      SDL_BYTESPERPIXEL(s->format) != 4 || SDL_SurfaceHasColorKey(s) || SDL_MUSTLOCK(s) ||
      SDL_GetSurfaceColorspace(s) != SDL_COLORSPACE_SRGB || !takes(s->format))
    return nullptr;
  // (as SDL_CreateTextureFromSurface: the surface's own format, static, the
  // format's colour space - the surface's, sRGB)
  SDL_Texture *t = SDL_CreateTexture(renderer, s->format, SDL_TEXTUREACCESS_STATIC, s->w, s->h);
  if (!t)
    return nullptr;
  SDL_GPUTexture *gpu = gpuOf(t);
  if (!gpu || !record(renderer, gpu, 0, 0, s->w, s->h, s->pixels, s->pitch)) {
    SDL_DestroyTexture(t);
    return nullptr;
  }
  // (as SDL_UpdateTextureFromSurface: the surface's colour and alpha mods
  // and blend mode go to the texture)
  Uint8 r = 255, g = 255, b = 255, a = 255;
  SDL_GetSurfaceColorMod(s, &r, &g, &b);
  SDL_SetTextureColorMod(t, r, g, b);
  SDL_GetSurfaceAlphaMod(s, &a);
  SDL_SetTextureAlphaMod(t, a);
  SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
  SDL_GetSurfaceBlendMode(s, &mode);
  SDL_SetTextureBlendMode(t, mode);
  return t;
}

} // namespace

void flush(SDL_Renderer *) {
  if (!g_cmd)
    return;
  SDL_EndGPUCopyPass(g_copy);
  SDL_SubmitGPUCommandBuffer(g_cmd);
  g_cmd = nullptr;
  g_copy = nullptr;
  g_used = 0;
}

bool update(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch) {
  if (!g_dev || renderer != g_renderer || !texture || !pixels || SDL_BYTESPERPIXEL(texture->format) != 4)
    return false;
  SDL_GPUTexture *gpu = gpuOf(texture);
  const SDL_Rect r = rect ? *rect : SDL_Rect{0, 0, texture->w, texture->h};
  return gpu && r.w > 0 && r.h > 0 && record(renderer, gpu, r.x, r.y, r.w, r.h, pixels, pitch);
}

void install(SDL_Renderer *renderer, SDL_GPUDevice *device) {
  const char *off = SDL_getenv("ZT_OWN_UPLOADS"); // (0: SDL sends them, for comparing)
  if (off && std::strcmp(off, "0") == 0)
    return;
  g_dev = device;
  g_renderer = renderer;
  g_formats = static_cast<const SDL_PixelFormat *>(
      SDL_GetPointerProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER, nullptr));
  ZT_StandIn &s = ZT_WindowStandIn();
  s.flush = flush;
  s.create = create;
}

} // namespace GpuUploadSdlGpu
