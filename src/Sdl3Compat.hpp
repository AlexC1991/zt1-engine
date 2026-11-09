#ifndef SDL3_COMPAT_HPP
#define SDL3_COMPAT_HPP

// ours: the SDL2 calls whose meaning changed in SDL3, kept as the engine
// used them while it moves to SDL3 (one by one, each checked against its
// pictures, they become SDL3's own). The same arguments and the same
// answers as SDL2's - 0 for done, the integer rectangles, the 0-255 vertex
// colours - over SDL3. Included before every source file (CMakeLists:
// /FI), so the names are everywhere SDL2's were.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
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

// ---------------------------------------------------------------------------
// SDL2's render scale, viewport and clip. SDL2 turned a viewport or clip into
// the target's pixels the moment it was set (times the scale then), and a
// later scale changed only what was drawn after; switching to a texture
// target gave it scale 1, all of it as the viewport and no clip, and coming
// back to the window brought the window's back. SDL3 keeps them in the scale's
// units and works them out again when the scale changes, and each texture
// keeps its own - so the engine's device-pixel viewports and clips (the
// world's offset view, its strips of buildings, the fences' layers) moved
// with the zoom. Here SDL3's own scale stays 1: the viewport and clip go to
// SDL3 as SDL2's backends had them (each value floored), and every drawing
// call below puts the scale on its coordinates as SDL2 did.
struct ZT_View2 {
  bool whole = true;              // (the viewport: all of the target)
  double vx = 0, vy = 0, vw = 0, vh = 0;
  bool clip = false;
  double cx = 0, cy = 0, cw = 0, ch = 0;
  float sx = 1.0f, sy = 1.0f;
};
struct ZT_Render2 {
  ZT_View2 cur, backup;          // (backup: the window's, while on a texture)
  SDL_Texture *target = nullptr; // (as the engine chose it: null is the window)
};
inline std::unordered_map<SDL_Renderer *, ZT_Render2> &ZT_Renders2() {
  static std::unordered_map<SDL_Renderer *, ZT_Render2> all;
  return all;
}
inline SDL_Renderer *&ZT_LastRenderer2() {
  static SDL_Renderer *r = nullptr;
  return r;
}
inline ZT_Render2 *&ZT_LastRender2() {
  static ZT_Render2 *s = nullptr;
  return s;
}
// (asked for every drawing call: the last renderer's kept to hand)
inline ZT_Render2 &ZT_R2(SDL_Renderer *r) {
  if (r == ZT_LastRenderer2() && ZT_LastRender2())
    return *ZT_LastRender2();
  ZT_Render2 &s = ZT_Renders2()[r];
  ZT_LastRenderer2() = r;
  ZT_LastRender2() = &s;
  return s;
}
inline bool ZT_Scaled2(const ZT_View2 &v) { return v.sx != 1.0f || v.sy != 1.0f; }

// (SDL's GPU renderer passes the clip on as the graphics card's scissor as it
// is; one reaching past the target - left of it or above it, or past its far
// side - is invalid on Vulkan and trips SDL's own checks: there it's cut at
// the target's edges - the same pixels, those beyond don't exist)
inline SDL_Rect ZT_GpuClip2(SDL_Renderer *r, const ZT_View2 &v, SDL_Rect c) {
  const char *name = SDL_GetRendererName(r);
  if (!name || std::strcmp(name, "gpu") != 0)
    return c;
  int outW = 0, outH = 0;
  SDL_GetCurrentRenderOutputSize(r, &outW, &outH);
  const int px = v.whole ? 0 : int(std::floor(v.vx)), py = v.whole ? 0 : int(std::floor(v.vy));
  const int x0 = std::max(px + c.x, 0), y0 = std::max(py + c.y, 0);
  const int x1 = std::min(px + c.x + c.w, outW), y1 = std::min(py + c.y + c.h, outH);
  if (x1 <= x0 || y1 <= y0) // (empty: draws nothing, wherever it is - put inside)
    return SDL_Rect{std::clamp(-px, std::min(0, outW - px), std::max(0, outW - px)),
                    std::clamp(-py, std::min(0, outH - py), std::max(0, outH - py)), 0, 0};
  return SDL_Rect{x0 - px, y0 - py, x1 - x0, y1 - y0};
}
// The engine's view onto SDL3's current target, in its pixels
inline void ZT_PushViewport2(SDL_Renderer *r, const ZT_View2 &v) {
  if (v.whole) {
    SDL_SetRenderViewport(r, nullptr);
  } else {
    const SDL_Rect p{int(std::floor(v.vx)), int(std::floor(v.vy)), int(std::floor(v.vw)), int(std::floor(v.vh))};
    SDL_SetRenderViewport(r, &p);
  }
}
inline void ZT_PushClip2(SDL_Renderer *r, const ZT_View2 &v) {
  if (!v.clip) {
    SDL_SetRenderClipRect(r, nullptr);
    return;
  }
  const SDL_Rect c = ZT_GpuClip2(r, v, SDL_Rect{int(std::floor(v.cx)), int(std::floor(v.cy)), int(std::floor(v.cw)),
                                                int(std::floor(v.ch))});
  SDL_SetRenderClipRect(r, &c);
}
inline void ZT_PushView2(SDL_Renderer *r) {
  const ZT_View2 &v = ZT_R2(r).cur;
  SDL_SetRenderScale(r, 1.0f, 1.0f);
  ZT_PushViewport2(r, v);
  ZT_PushClip2(r, v);
}

// The engine's view kept and put back whole (no rounding through SDL2's
// getters), and a plain one - the target's pixels, all of it - for a pass
inline ZT_View2 ZT_SaveView2(SDL_Renderer *r) { return ZT_R2(r).cur; }
inline void ZT_RestoreView2(SDL_Renderer *r, const ZT_View2 &v) {
  ZT_R2(r).cur = v;
  ZT_PushView2(r);
}
inline void ZT_PixelView2(SDL_Renderer *r) {
  ZT_R2(r).cur = ZT_View2{};
  ZT_PushView2(r);
}
// The viewport and clip in the target's pixels, as drawn
inline SDL_Rect ZT_PixelViewport2(SDL_Renderer *r) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (v.whole) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    return SDL_Rect{0, 0, w, h};
  }
  return SDL_Rect{int(std::floor(v.vx)), int(std::floor(v.vy)), int(std::floor(v.vw)), int(std::floor(v.vh))};
}
inline bool ZT_PixelClip2(SDL_Renderer *r, SDL_Rect *clip) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (clip)
    *clip = SDL_Rect{int(std::floor(v.cx)), int(std::floor(v.cy)), int(std::floor(v.cw)), int(std::floor(v.ch))};
  return v.clip;
}

// SDL2's setters and getters
inline bool ZT_SetRenderScale(SDL_Renderer *r, float sx, float sy) {
  ZT_View2 &v = ZT_R2(r).cur;
  v.sx = sx;
  v.sy = sy;
  return true;
}
inline bool ZT_GetRenderScale(SDL_Renderer *r, float *sx, float *sy) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (sx)
    *sx = v.sx;
  if (sy)
    *sy = v.sy;
  return true;
}
inline bool ZT_SetRenderViewport(SDL_Renderer *r, const SDL_Rect *rect) {
  ZT_View2 &v = ZT_R2(r).cur;
  v.whole = rect == nullptr;
  if (rect) {
    v.vx = double(rect->x) * v.sx;
    v.vy = double(rect->y) * v.sy;
    v.vw = double(rect->w) * v.sx;
    v.vh = double(rect->h) * v.sy;
  }
  ZT_PushViewport2(r, v);
  if (v.clip)
    ZT_PushClip2(r, v); // (its cut at the target's edges moves with the viewport)
  return true;
}
inline bool ZT_GetRenderViewport(SDL_Renderer *r, SDL_Rect *rect) {
  if (!rect)
    return true;
  const ZT_View2 &v = ZT_R2(r).cur;
  double w = v.vw, h = v.vh, x = v.vx, y = v.vy;
  if (v.whole) {
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(r, &ow, &oh);
    x = y = 0;
    w = ow;
    h = oh;
  }
  *rect = SDL_Rect{int(std::floor(x / v.sx)), int(std::floor(y / v.sy)), int(std::floor(w / v.sx)), int(std::floor(h / v.sy))};
  return true;
}
inline bool ZT_SetRenderClipRect(SDL_Renderer *r, const SDL_Rect *rect) {
  ZT_View2 &v = ZT_R2(r).cur;
  if (rect && rect->w >= 0 && rect->h >= 0) {
    v.clip = true;
    v.cx = double(rect->x) * v.sx;
    v.cy = double(rect->y) * v.sy;
    v.cw = double(rect->w) * v.sx;
    v.ch = double(rect->h) * v.sy;
  } else {
    v.clip = false;
    v.cx = v.cy = v.cw = v.ch = 0;
  }
  ZT_PushClip2(r, v);
  return true;
}
inline bool ZT_GetRenderClipRect(SDL_Renderer *r, SDL_Rect *rect) {
  if (!rect)
    return true;
  const ZT_View2 &v = ZT_R2(r).cur;
  *rect = SDL_Rect{int(std::floor(v.cx / v.sx)), int(std::floor(v.cy / v.sy)), int(std::floor(v.cw / v.sx)),
                   int(std::floor(v.ch / v.sy))};
  return true;
}
inline bool ZT_RenderClipEnabled(SDL_Renderer *r) { return ZT_R2(r).cur.clip; }
inline bool ZT_DestroyRenderer(SDL_Renderer *r) {
  ZT_Renders2().erase(r);
  ZT_LastRenderer2() = nullptr;
  ZT_LastRender2() = nullptr;
  SDL_DestroyRenderer(r);
  return true;
}

// Drawing, the scale on its coordinates as SDL2's
inline SDL_FRect ZT_ScaleRect2(const ZT_View2 &v, const SDL_FRect &d) { return SDL_FRect{d.x * v.sx, d.y * v.sy, d.w * v.sx, d.h * v.sy}; }
inline bool ZT_RenderTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (!dst || !ZT_Scaled2(v))
    return SDL_RenderTexture(r, t, src, dst);
  const SDL_FRect d = ZT_ScaleRect2(v, *dst);
  return SDL_RenderTexture(r, t, src, &d);
}
inline bool ZT_RenderTextureRotated(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *src, const SDL_FRect *dst, double angle,
                                    const SDL_FPoint *centre, SDL_FlipMode flip) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (!dst || !ZT_Scaled2(v))
    return SDL_RenderTextureRotated(r, t, src, dst, angle, centre, flip);
  const SDL_FRect d = ZT_ScaleRect2(v, *dst);
  SDL_FPoint c;
  if (centre)
    c = SDL_FPoint{centre->x * v.sx, centre->y * v.sy};
  return SDL_RenderTextureRotated(r, t, src, &d, angle, centre ? &c : nullptr, flip);
}
inline bool ZT_RenderGeometryRaw(SDL_Renderer *r, SDL_Texture *t, const float *xy, int xyStride, const SDL_FColor *colour,
                                 int colourStride, const float *uv, int uvStride, int n, const void *indices, int ni,
                                 int indexSize) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (!ZT_Scaled2(v) || !xy || n <= 0)
    return SDL_RenderGeometryRaw(r, t, xy, xyStride, colour, colourStride, uv, uvStride, n, indices, ni, indexSize);
  thread_local std::vector<float> scaled;
  scaled.resize(size_t(n) * 2);
  for (int i = 0; i < n; i++) {
    const float *p = reinterpret_cast<const float *>(reinterpret_cast<const Uint8 *>(xy) + size_t(i) * xyStride);
    scaled[size_t(i) * 2] = p[0] * v.sx;
    scaled[size_t(i) * 2 + 1] = p[1] * v.sy;
  }
  return SDL_RenderGeometryRaw(r, t, scaled.data(), int(sizeof(float) * 2), colour, colourStride, uv, uvStride, n, indices, ni,
                               indexSize);
}
inline bool ZT_RenderFillRects2(SDL_Renderer *r, const SDL_FRect *rects, int n) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (!ZT_Scaled2(v))
    return SDL_RenderFillRects(r, rects, n);
  thread_local std::vector<SDL_FRect> scaled;
  scaled.resize(size_t(std::max(n, 0)));
  for (int i = 0; i < n; i++)
    scaled[size_t(i)] = ZT_ScaleRect2(v, rects[i]);
  return SDL_RenderFillRects(r, scaled.data(), n);
}
inline bool ZT_RenderFillRect2(SDL_Renderer *r, const SDL_FRect *rect) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (!rect || !ZT_Scaled2(v))
    return SDL_RenderFillRect(r, rect);
  const SDL_FRect d = ZT_ScaleRect2(v, *rect);
  return SDL_RenderFillRect(r, &d);
}
// (scaled points and lines as SDL2 drew them: each point a rectangle the
// scale's size, a line's straight runs one rectangle, its slopes SDL2's
// Bresenham points - RenderDrawPointsWithRectsF, RenderDrawLinesWithRectsF,
// RenderDrawLineBresenham)
inline bool ZT_ScaledPoints2(SDL_Renderer *r, const ZT_View2 &v, const SDL_FPoint *points, int n) {
  thread_local std::vector<SDL_FRect> rects;
  rects.resize(size_t(std::max(n, 0)));
  for (int i = 0; i < n; i++)
    rects[size_t(i)] = SDL_FRect{points[i].x * v.sx, points[i].y * v.sy, v.sx, v.sy};
  return n <= 0 || SDL_RenderFillRects(r, rects.data(), n);
}
inline void ZT_Bresenham2(SDL_Renderer *r, const ZT_View2 &v, int x1, int y1, int x2, int y2, bool last) {
  int vw = 0, vh = 0;
  {
    const SDL_Rect pv = ZT_PixelViewport2(r);
    vw = pv.w;
    vh = pv.h;
  }
  const SDL_Rect bounds{0, 0, vw, vh};
  if (!SDL_GetRectAndLineIntersection(&bounds, &x1, &y1, &x2, &y2))
    return;
  const int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
  int count, d, dinc1, dinc2, xinc1, xinc2, yinc1, yinc2;
  if (dx >= dy) {
    count = dx + 1;
    d = 2 * dy - dx;
    dinc1 = dy * 2;
    dinc2 = (dy - dx) * 2;
    xinc1 = 1;
    xinc2 = 1;
    yinc1 = 0;
    yinc2 = 1;
  } else {
    count = dy + 1;
    d = 2 * dx - dy;
    dinc1 = dx * 2;
    dinc2 = (dx - dy) * 2;
    xinc1 = 0;
    xinc2 = 1;
    yinc1 = 1;
    yinc2 = 1;
  }
  if (x1 > x2) {
    xinc1 = -xinc1;
    xinc2 = -xinc2;
  }
  if (y1 > y2) {
    yinc1 = -yinc1;
    yinc2 = -yinc2;
  }
  if (!last)
    --count;
  std::vector<SDL_FPoint> points(size_t(std::max(count, 0)));
  int x = x1, y = y1;
  for (int i = 0; i < count; ++i) {
    points[size_t(i)] = SDL_FPoint{float(x), float(y)};
    if (d < 0) {
      d += dinc1;
      x += xinc1;
      y += yinc1;
    } else {
      d += dinc2;
      x += xinc2;
      y += yinc2;
    }
  }
  ZT_ScaledPoints2(r, v, points.data(), count);
}
inline bool ZT_ScaledLines2(SDL_Renderer *r, const ZT_View2 &v, const SDL_FPoint *p, int n) {
  if (n < 2)
    return true;
  std::vector<SDL_FRect> rects;
  bool drew = false, last = false;
  for (int i = 0; i < n - 1; ++i) {
    const bool sameX = p[i].x == p[i + 1].x, sameY = p[i].y == p[i + 1].y;
    if (i == n - 2) {
      if (!drew || p[i + 1].x != p[0].x || p[i + 1].y != p[0].y)
        last = true;
    } else if (sameX && sameY) {
      continue;
    }
    if (sameX) {
      const float minY = std::min(p[i].y, p[i + 1].y), maxY = std::max(p[i].y, p[i + 1].y);
      SDL_FRect f{p[i].x * v.sx, minY * v.sy, v.sx, (maxY - minY + (last ? 1 : 0)) * v.sy};
      if (!last && p[i + 1].y < p[i].y)
        f.y += v.sy;
      rects.push_back(f);
    } else if (sameY) {
      const float minX = std::min(p[i].x, p[i + 1].x), maxX = std::max(p[i].x, p[i + 1].x);
      SDL_FRect f{minX * v.sx, p[i].y * v.sy, (maxX - minX + (last ? 1 : 0)) * v.sx, v.sy};
      if (!last && p[i + 1].x < p[i].x)
        f.x += v.sx;
      rects.push_back(f);
    } else {
      ZT_Bresenham2(r, v, int(std::lround(p[i].x)), int(std::lround(p[i].y)), int(std::lround(p[i + 1].x)),
                    int(std::lround(p[i + 1].y)), last);
    }
    drew = true;
  }
  return rects.empty() || SDL_RenderFillRects(r, rects.data(), int(rects.size()));
}
inline bool ZT_RenderPoints(SDL_Renderer *r, const SDL_FPoint *points, int n) {
  const ZT_View2 &v = ZT_R2(r).cur;
  return ZT_Scaled2(v) ? ZT_ScaledPoints2(r, v, points, n) : SDL_RenderPoints(r, points, n);
}
inline bool ZT_RenderPoint(SDL_Renderer *r, float x, float y) {
  const SDL_FPoint p{x, y};
  return ZT_RenderPoints(r, &p, 1);
}
inline bool ZT_RenderLines(SDL_Renderer *r, const SDL_FPoint *points, int n) {
  const ZT_View2 &v = ZT_R2(r).cur;
  return ZT_Scaled2(v) ? ZT_ScaledLines2(r, v, points, n) : SDL_RenderLines(r, points, n);
}
inline bool ZT_RenderLine(SDL_Renderer *r, float x1, float y1, float x2, float y2) {
  const SDL_FPoint p[2] = {{x1, y1}, {x2, y2}};
  return ZT_RenderLines(r, p, 2);
}
inline bool ZT_RenderRect(SDL_Renderer *r, const SDL_FRect *rect) {
  const ZT_View2 &v = ZT_R2(r).cur;
  if (!ZT_Scaled2(v))
    return SDL_RenderRect(r, rect);
  SDL_FRect f;
  if (rect) {
    f = *rect;
  } else {
    SDL_Rect whole{};
    ZT_GetRenderViewport(r, &whole);
    f = SDL_FRect{0, 0, float(whole.w), float(whole.h)};
  }
  // (SDL2's outline: its four corners as a closed line)
  const SDL_FPoint p[5] = {{f.x, f.y}, {f.x + f.w - 1, f.y}, {f.x + f.w - 1, f.y + f.h - 1}, {f.x, f.y + f.h - 1}, {f.x, f.y}};
  return ZT_ScaledLines2(r, v, p, 5);
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
  const bool ok = ZT_RenderGeometryRaw(r, t, &v[0].position.x, int(sizeof(ZT_Vertex)), colours.data(), int(sizeof(SDL_FColor)),
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
  return ZT_RenderTexture(r, t, src ? &s : nullptr, dst ? &d : nullptr) ? 0 : -1;
}
inline int ZT_RenderCopyF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst) {
  SDL_FRect s;
  if (src)
    s = ZT_FRect(*src);
  return ZT_RenderTexture(r, t, src ? &s : nullptr, dst) ? 0 : -1;
}
inline int ZT_RenderCopyExF(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_FRect *dst, double angle,
                            const SDL_FPoint *centre, SDL_FlipMode flip) {
  SDL_FRect s;
  if (src)
    s = ZT_FRect(*src);
  return ZT_RenderTextureRotated(r, t, src ? &s : nullptr, dst, angle, centre, flip) ? 0 : -1;
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
  return ZT_RenderFillRect2(r, rect ? &f : nullptr) ? 0 : -1;
}
inline int ZT_RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect) {
  SDL_FRect f;
  if (rect)
    f = ZT_FRect(*rect);
  return ZT_RenderRect(r, rect ? &f : nullptr) ? 0 : -1;
}
// ours: SDL3's GPU renderer's hooks (GpuFsrSdlGpu.cpp, GpuUploadSdlGpu.cpp),
// set for that renderer only; nothing changes while they're unset.
// - The window's stand-in: the renderer keeps the window's frame to itself,
//   so while GpuFsr's screen passes need to read it the frame is drawn into
//   a texture instead: choosing the window as the target chooses the
//   texture, asking for the target answers the window for it, and presenting
//   goes through GpuFsr, which copies the texture to the window first.
// - New textures' pixels sent on the engine's own command buffer (SDL's
//   makes and frees an upload buffer for each one), flushed before SDL sends
//   its own commands: at present and before reading pixels back.
struct ZT_StandIn {
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;                                 // (while set, the frame goes here)
  bool (*present)(SDL_Renderer *) = nullptr;                     // (presents renderer's frames)
  void (*flush)(SDL_Renderer *) = nullptr;                       // (sends what SDL's frame needs of ours)
  SDL_Texture *(*create)(SDL_Renderer *, SDL_Surface *) = nullptr; // (a texture of a surface; null: SDL's)
};
inline ZT_StandIn &ZT_WindowStandIn() {
  static ZT_StandIn s;
  return s;
}

// SDL2's: the pixels into the caller's memory, in the format asked for
inline int ZT_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch) {
  if (ZT_WindowStandIn().flush && r == ZT_WindowStandIn().renderer)
    ZT_WindowStandIn().flush(r);
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

// The target as SDL2's: the stand-in for the window when there is one, and
// SDL2's view rules - a texture starts with scale 1, all of it and no clip,
// the window gets its own back - with the engine's view put on whatever
// SDL3 now draws into
inline bool ZT_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) {
  const ZT_StandIn &s = ZT_WindowStandIn();
  if (!SDL_SetRenderTarget(r, !t && r == s.renderer ? s.texture : t))
    return false;
  ZT_Render2 &st = ZT_R2(r);
  if (t != st.target) {
    if (t && !st.target)
      st.backup = st.cur;
    st.cur = t ? ZT_View2{} : st.backup;
    st.target = t;
  }
  ZT_PushView2(r);
  return true;
}
inline SDL_Texture *ZT_GetRenderTarget(SDL_Renderer *r) {
  SDL_Texture *t = SDL_GetRenderTarget(r);
  const ZT_StandIn &s = ZT_WindowStandIn();
  return t && r == s.renderer && t == s.texture ? nullptr : t;
}
inline bool ZT_RenderPresent(SDL_Renderer *r) {
  const ZT_StandIn &s = ZT_WindowStandIn();
  return s.present && r == s.renderer ? s.present(r) : SDL_RenderPresent(r);
}
inline SDL_Surface *ZT_RenderReadPixelsSurface(SDL_Renderer *r, const SDL_Rect *rect) {
  const ZT_StandIn &s = ZT_WindowStandIn();
  if (s.flush && r == s.renderer)
    s.flush(r);
  return SDL_RenderReadPixels(r, rect);
}
inline SDL_Texture *ZT_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *surface) {
  const ZT_StandIn &s = ZT_WindowStandIn();
  if (s.create && r == s.renderer)
    if (SDL_Texture *t = s.create(r, surface))
      return t;
  return SDL_CreateTextureFromSurface(r, surface);
}
#define SDL_SetRenderTarget ZT_SetRenderTarget
#define SDL_GetRenderTarget ZT_GetRenderTarget
#define SDL_RenderPresent ZT_RenderPresent
#define SDL_RenderReadPixels ZT_RenderReadPixelsSurface
#define SDL_CreateTextureFromSurface ZT_CreateTextureFromSurface
#define SDL_SetRenderScale ZT_SetRenderScale
#define SDL_GetRenderScale ZT_GetRenderScale
#define SDL_SetRenderViewport ZT_SetRenderViewport
#define SDL_GetRenderViewport ZT_GetRenderViewport
#define SDL_SetRenderClipRect ZT_SetRenderClipRect
#define SDL_GetRenderClipRect ZT_GetRenderClipRect
#define SDL_RenderClipEnabled ZT_RenderClipEnabled
#define SDL_DestroyRenderer ZT_DestroyRenderer
#define SDL_RenderTexture ZT_RenderTexture
#define SDL_RenderTextureRotated ZT_RenderTextureRotated
#define SDL_RenderGeometryRaw ZT_RenderGeometryRaw
#define SDL_RenderFillRect ZT_RenderFillRect2
#define SDL_RenderFillRects ZT_RenderFillRects2
#define SDL_RenderPoint ZT_RenderPoint
#define SDL_RenderPoints ZT_RenderPoints
#define SDL_RenderLine ZT_RenderLine
#define SDL_RenderLines ZT_RenderLines
#define SDL_RenderRect ZT_RenderRect

#endif // SDL3_COMPAT_HPP
