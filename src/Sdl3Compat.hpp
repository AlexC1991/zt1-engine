#ifndef SDL3_COMPAT_HPP
#define SDL3_COMPAT_HPP

// ours: the SDL2 calls whose meaning changed in SDL3, kept as the engine
// used them while it moves to SDL3 (one by one, each checked against its
// pictures, they become SDL3's own). The same arguments and the same
// answers as SDL2's - 0 for done, the integer rectangles, the 0-255 vertex
// colours - over SDL3. Included before every source file (CMakeLists:
// /FI), so the names are everywhere SDL2's were.

#include <SDL3/SDL.h>

#include <cstring>
#include <unordered_map>
#include <vector>

// (SDL2's renderer flags, for the code that still reads them: ZT_GetRendererInfo)
enum : Uint32 {
  ZT_RENDERER_SOFTWARE = 0x1,
  ZT_RENDERER_ACCELERATED = 0x2,
  ZT_RENDERER_PRESENTVSYNC = 0x4,
  ZT_RENDERER_TARGETTEXTURE = 0x8,
};
struct ZT_RendererInfo {
  const char *name = "";
  Uint32 flags = 0;
  int max_texture_width = 0, max_texture_height = 0;
};
inline int ZT_GetRendererInfo(SDL_Renderer *r, ZT_RendererInfo *info) {
  if (!r || !info)
    return -1;
  info->name = SDL_GetRendererName(r);
  if (!info->name)
    info->name = "";
  int vsync = 0;
  SDL_GetRenderVSync(r, &vsync);
  info->flags = ZT_RENDERER_TARGETTEXTURE | (std::strcmp(info->name, "software") ? ZT_RENDERER_ACCELERATED : ZT_RENDERER_SOFTWARE) |
                (vsync ? ZT_RENDERER_PRESENTVSYNC : 0);
  const int max = int(SDL_GetNumberProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
  info->max_texture_width = info->max_texture_height = max;
  return 0;
}

// A vertex as SDL2's: its colour 0-255 a channel
struct ZT_Vertex {
  SDL_FPoint position;
  SDL_Color color;
  SDL_FPoint tex_coord;
};
inline int ZT_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const ZT_Vertex *v, int n, const int *indices, int ni) {
  if (!r || !v || n <= 0)
    return n == 0 ? 0 : -1;
  // (each colour as SDL3's 0-1 floats: the same /255 SDL2's renderers made)
  thread_local std::vector<SDL_FColor> colours;
  colours.resize(size_t(n));
  for (int i = 0; i < n; i++)
    colours[i] = SDL_FColor{v[i].color.r / 255.0f, v[i].color.g / 255.0f, v[i].color.b / 255.0f, v[i].color.a / 255.0f};
  const bool ok = SDL_RenderGeometryRaw(r, t, &v[0].position.x, int(sizeof(ZT_Vertex)), colours.data(), int(sizeof(SDL_FColor)),
                                        &v[0].tex_coord.x, int(sizeof(ZT_Vertex)), n, indices, ni, indices ? 4 : 0);
  return ok ? 0 : -1;
}

inline SDL_FRect ZT_FRect(const SDL_Rect &r) { return SDL_FRect{float(r.x), float(r.y), float(r.w), float(r.h)}; }
inline int ZT_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst) {
  SDL_FRect s, d;
  if (src)
    s = ZT_FRect(*src);
  if (dst)
    d = ZT_FRect(*dst);
  return SDL_RenderTexture(r, t, src ? &s : nullptr, dst ? &d : nullptr) ? 0 : -1;
}
inline int ZT_RenderCopyF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst) {
  SDL_FRect s;
  if (src)
    s = ZT_FRect(*src);
  return SDL_RenderTexture(r, t, src ? &s : nullptr, dst) ? 0 : -1;
}
inline int ZT_RenderCopyExF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst, double angle,
                            const SDL_FPoint *centre, SDL_FlipMode flip) {
  SDL_FRect s;
  if (src)
    s = ZT_FRect(*src);
  return SDL_RenderTextureRotated(r, t, src ? &s : nullptr, dst, angle, centre, flip) ? 0 : -1;
}
inline int ZT_RenderCopyEx(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst, double angle,
                           const SDL_Point *centre, SDL_FlipMode flip) {
  SDL_FRect d;
  SDL_FPoint c;
  if (dst)
    d = ZT_FRect(*dst);
  if (centre)
    c = SDL_FPoint{float(centre->x), float(centre->y)};
  return ZT_RenderCopyExF(r, t, src, dst ? &d : nullptr, angle, centre ? &c : nullptr, flip);
}
inline int ZT_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect) {
  SDL_FRect f;
  if (rect)
    f = ZT_FRect(*rect);
  return SDL_RenderFillRect(r, rect ? &f : nullptr) ? 0 : -1;
}
inline int ZT_RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect) {
  SDL_FRect f;
  if (rect)
    f = ZT_FRect(*rect);
  return SDL_RenderRect(r, rect ? &f : nullptr) ? 0 : -1;
}
// SDL2's: the pixels into the caller's memory, in the format asked for
inline int ZT_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch) {
  SDL_Surface *got = SDL_RenderReadPixels(r, rect);
  if (!got)
    return -1;
  const SDL_PixelFormat want = format ? SDL_PixelFormat(format) : got->format;
  const bool ok = SDL_ConvertPixels(got->w, got->h, got->format, got->pixels, got->pitch, want, pixels, pitch);
  SDL_DestroySurface(got);
  return ok ? 0 : -1;
}
// (asked for every sprite drawn: SDL3's texture holds its format and size
// itself; only its access is a property - a locked lookup)
inline int ZT_QueryTexture(SDL_Texture *t, Uint32 *format, int *access, int *w, int *h) {
  if (!t)
    return -1;
  if (format)
    *format = Uint32(t->format);
  if (access)
    *access = int(SDL_GetNumberProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_ACCESS_NUMBER, 0));
  if (w)
    *w = t->w;
  if (h)
    *h = t->h;
  return 0;
}

// (a surface's format is SDL3's enum: its details looked up for the colour)
inline void ZT_GetRGBA(Uint32 pixel, SDL_PixelFormat format, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
  SDL_GetRGBA(pixel, SDL_GetPixelFormatDetails(format), nullptr, r, g, b, a);
}
inline void ZT_GetRGB(Uint32 pixel, SDL_PixelFormat format, Uint8 *r, Uint8 *g, Uint8 *b) {
  SDL_GetRGB(pixel, SDL_GetPixelFormatDetails(format), nullptr, r, g, b);
}
inline Uint32 ZT_MapRGBA(SDL_PixelFormat format, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
  return SDL_MapRGBA(SDL_GetPixelFormatDetails(format), nullptr, r, g, b, a);
}

inline SDL_Surface *ZT_CreateRGBSurfaceWithFormat(Uint32, int w, int h, int, Uint32 format) {
  return SDL_CreateSurface(w, h, SDL_PixelFormat(format));
}
inline SDL_Surface *ZT_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h, int, int pitch, Uint32 format) {
  return SDL_CreateSurfaceFrom(w, h, SDL_PixelFormat(format), pixels, pitch);
}
inline SDL_Surface *ZT_ConvertSurfaceFormat(SDL_Surface *s, Uint32 format, Uint32) {
  return SDL_ConvertSurface(s, SDL_PixelFormat(format));
}

// A texture's own pointer, as SDL2's user data: read for every sprite drawn
// (ArtScaler::scaleOf), so kept in a table of the engine's - a property is
// a locked lookup - and the texture told to forget it as SDL destroys it (a
// texture made later at the same address can't find an old one)
inline std::unordered_map<SDL_Texture *, void *> &ZT_TextureData() {
  static std::unordered_map<SDL_Texture *, void *> data;
  return data;
}
inline int ZT_SetTextureUserData(SDL_Texture *t, void *data) {
  if (!t)
    return -1;
  auto &table = ZT_TextureData();
  const bool known = table.count(t) != 0;
  table[t] = data;
  if (!known)
    SDL_SetPointerPropertyWithCleanup(SDL_GetTextureProperties(t), "zt1.userdata", t,
                                      [](void *, void *texture) { ZT_TextureData().erase(static_cast<SDL_Texture *>(texture)); },
                                      nullptr);
  return 0;
}
inline void *ZT_GetTextureUserData(SDL_Texture *t) {
  const auto &table = ZT_TextureData();
  const auto found = table.find(t);
  return found == table.end() ? nullptr : found->second;
}

inline int ZT_RenderSetLogicalSize(SDL_Renderer *r, int w, int h) {
  return SDL_SetRenderLogicalPresentation(r, w, h, w && h ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED)
             ? 0
             : -1;
}

// SDL2's GL texture binding: the texture's own GL name bound on its target
// (SDL3 gives it as a property; the renderer's batch sent first)
inline int ZT_GL_BindTexture(SDL_Texture *t, float *texw, float *texh) {
  if (!t)
    return -1;
  const SDL_PropertiesID p = SDL_GetTextureProperties(t);
  const Sint64 name = SDL_GetNumberProperty(p, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
  if (!name)
    return -1;
  const Sint64 target = SDL_GetNumberProperty(p, SDL_PROP_TEXTURE_OPENGL_TEXTURE_TARGET_NUMBER, 0x0DE1 /* GL_TEXTURE_2D */);
  SDL_FlushRenderer(SDL_GetRendererFromTexture(t));
  using Bind = void(SDLCALL *)(unsigned, unsigned);
  static Bind bind = reinterpret_cast<Bind>(SDL_GL_GetProcAddress("glBindTexture"));
  if (!bind)
    return -1;
  bind(unsigned(target), unsigned(name));
  if (texw)
    *texw = SDL_GetFloatProperty(p, SDL_PROP_TEXTURE_OPENGL_TEX_W_FLOAT, 1.0f);
  if (texh)
    *texh = SDL_GetFloatProperty(p, SDL_PROP_TEXTURE_OPENGL_TEX_H_FLOAT, 1.0f);
  return 0;
}
inline int ZT_GL_UnbindTexture(SDL_Texture *t) {
  if (!t)
    return -1;
  const Sint64 target =
      SDL_GetNumberProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_OPENGL_TEXTURE_TARGET_NUMBER, 0x0DE1);
  using Bind = void(SDLCALL *)(unsigned, unsigned);
  static Bind bind = reinterpret_cast<Bind>(SDL_GL_GetProcAddress("glBindTexture"));
  if (bind)
    bind(unsigned(target), 0);
  return 0;
}

// SDL2's readers: the value (0 past the end), and SDL_RWread's whole objects
inline Uint32 ZT_ReadLE32(SDL_IOStream *io) {
  Uint32 v = 0;
  SDL_ReadU32LE(io, &v);
  return v;
}
inline Uint16 ZT_ReadLE16(SDL_IOStream *io) {
  Uint16 v = 0;
  SDL_ReadU16LE(io, &v);
  return v;
}
inline Uint8 ZT_ReadU8(SDL_IOStream *io) {
  Uint8 v = 0;
  SDL_ReadU8(io, &v);
  return v;
}
inline size_t ZT_RWread(SDL_IOStream *io, void *ptr, size_t size, size_t count) {
  return size && count ? SDL_ReadIO(io, ptr, size * count) / size : 0;
}

#ifdef _WIN32
// SDL2's: the renderer's Direct3D 11 device, referenced for the caller
struct ID3D11Device;
ID3D11Device *ZT_RenderGetD3D11Device(SDL_Renderer *r); // (D3D11Util.cpp)
#endif

#endif // SDL3_COMPAT_HPP
