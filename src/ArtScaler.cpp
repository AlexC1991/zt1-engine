#include "ArtScaler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#include "SDL_image.h"

namespace ArtScaler {

static Mode g_mode = Mode::Off;
static int g_factor = 1;
static float g_sharpness = 0.2f;

void configure(Mode mode, int factor, float sharpness) {
  g_mode = mode;
  g_sharpness = std::max(0.0f, sharpness);
  if (factor <= 0) {
    // Enough detail for the biggest the UI gets on this desktop (the UI is
    // 800x600 scaled to fit)
    SDL_DisplayMode dm;
    int h = 1080;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.h > 0)
      h = dm.h;
    factor = static_cast<int>(std::ceil(h / 600.0));
  }
  g_factor = mode == Mode::Off ? 1 : std::clamp(factor, 2, 4);
  SDL_Log("[ArtScaler] art upscaling: %s x%d (sharpness %.2f)", modeName(mode),
          g_factor, g_sharpness);
}

Mode parseMode(const std::string &name) {
  std::string n = name;
  std::transform(n.begin(), n.end(), n.begin(), ::tolower);
  if (n == "mmpx" || n == "pixel")
    return Mode::Mmpx;
  if (n == "fsr" || n == "fsr1")
    return Mode::Fsr;
  return Mode::Off;
}

const char *modeName(Mode mode) {
  switch (mode) {
  case Mode::Mmpx:
    return "mmpx";
  case Mode::Fsr:
    return "fsr";
  default:
    return "off";
  }
}

Mode mode() { return g_mode; }
int factor() { return g_factor; }

// ----------------------------------------------------------------------------
// Texture bookkeeping: the upscale factor rides along as the texture's user
// data, so any texture can report the size of the art it stands for
// ----------------------------------------------------------------------------

int scaleOf(SDL_Texture *texture) {
  if (!texture)
    return 1;
  intptr_t s = reinterpret_cast<intptr_t>(SDL_GetTextureUserData(texture));
  return s > 1 ? static_cast<int>(s) : 1;
}

void querySize(SDL_Texture *texture, int *w, int *h) {
  int tw = 0, th = 0;
  if (texture)
    SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
  int s = scaleOf(texture);
  if (w)
    *w = tw / s;
  if (h)
    *h = th / s;
}

SDL_Rect toTexture(SDL_Texture *texture, const SDL_Rect &rect) {
  int s = scaleOf(texture);
  return {rect.x * s, rect.y * s, rect.w * s, rect.h * s};
}

static SDL_Texture *makeTexture(SDL_Renderer *renderer, SDL_Surface *surface,
                                int scale) {
  SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, surface);
  if (t && scale > 1) {
    SDL_SetTextureUserData(t, reinterpret_cast<void *>(intptr_t(scale)));
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  }
  return t;
}

// Runs fn(y) for every row, split across the CPU's cores
template <typename Fn> static void forRows(int rows, Fn fn) {
  int threads = std::clamp(
      static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
  if (rows < 64)
    threads = 1;
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; t++) {
    pool.emplace_back([=]() {
      for (int y = t; y < rows; y += threads)
        fn(y);
    });
  }
  for (std::thread &th : pool)
    th.join();
}

// ----------------------------------------------------------------------------
// MMPX 2x. Ported from the reference implementation:
//   Copyright 2020 Morgan McGuire & Mara Gagiu. Available under the MIT
//   license. https://casual-effects.com/research/McGuire2021PixelArt/
// Pixels are RGBA32 words (alpha in the top byte).
// ----------------------------------------------------------------------------

static inline uint32_t mmpxLuma(uint32_t c) {
  uint32_t a = (c & 0xFF000000u) >> 24;
  return (((c & 0x00FF0000u) >> 16) + ((c & 0x0000FF00u) >> 8) +
          (c & 0x000000FFu) + 1) *
         (256 - a);
}
static inline bool allEq2(uint32_t b, uint32_t a0, uint32_t a1) {
  return ((b ^ a0) | (b ^ a1)) == 0;
}
static inline bool allEq3(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2) {
  return ((b ^ a0) | (b ^ a1) | (b ^ a2)) == 0;
}
static inline bool allEq4(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2,
                          uint32_t a3) {
  return ((b ^ a0) | (b ^ a1) | (b ^ a2) | (b ^ a3)) == 0;
}
static inline bool anyEq3(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2) {
  return b == a0 || b == a1 || b == a2;
}
static inline bool noneEq2(uint32_t b, uint32_t a0, uint32_t a1) {
  return b != a0 && b != a1;
}
static inline bool noneEq4(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2,
                           uint32_t a3) {
  return b != a0 && b != a1 && b != a2 && b != a3;
}

static void mmpx2x(const uint32_t *srcBuf, uint32_t *dst, int w, int h) {
  auto src = [&](int x, int y) {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return srcBuf[y * w + x];
  };
  forRows(h, [&](int sy) {
    for (int sx = 0; sx < w; sx++) {
      uint32_t A = src(sx - 1, sy - 1), B = src(sx, sy - 1),
               C = src(sx + 1, sy - 1);
      uint32_t D = src(sx - 1, sy), E = src(sx, sy), F = src(sx + 1, sy);
      uint32_t G = src(sx - 1, sy + 1), H = src(sx, sy + 1),
               I = src(sx + 1, sy + 1);
      uint32_t Q = src(sx - 2, sy), R = src(sx + 2, sy);
      uint32_t J = E, K = E, L = E, M = E;

      if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) |
           (H ^ E) | (I ^ E)) != 0) {
        uint32_t P = src(sx, sy - 2), S = src(sx, sy + 2);
        uint32_t Bl = mmpxLuma(B), Dl = mmpxLuma(D), El = mmpxLuma(E),
                 Fl = mmpxLuma(F), Hl = mmpxLuma(H);

        // 1:1 slope rules
        if ((D == B && D != H && D != F) && (El >= Dl || E == A) &&
            anyEq3(E, A, C, G) && ((El < Dl) || A != D || E != P || E != Q))
          J = D;
        if ((B == F && B != D && B != H) && (El >= Bl || E == C) &&
            anyEq3(E, A, C, I) && ((El < Bl) || C != B || E != P || E != R))
          K = B;
        if ((H == D && H != F && H != B) && (El >= Hl || E == G) &&
            anyEq3(E, A, G, I) && ((El < Hl) || G != H || E != S || E != Q))
          L = H;
        if ((F == H && F != B && F != D) && (El >= Fl || E == I) &&
            anyEq3(E, C, G, I) && ((El < Fl) || I != H || E != R || E != S))
          M = F;

        // Intersection rules
        if ((E != F && allEq4(E, C, I, D, Q) && allEq2(F, B, H)) &&
            (F != src(sx + 3, sy)))
          K = M = F;
        if ((E != D && allEq4(E, A, G, F, R) && allEq2(D, B, H)) &&
            (D != src(sx - 3, sy)))
          J = L = D;
        if ((E != H && allEq4(E, G, I, B, P) && allEq2(H, D, F)) &&
            (H != src(sx, sy + 3)))
          L = M = H;
        if ((E != B && allEq4(E, A, C, H, S) && allEq2(B, D, F)) &&
            (B != src(sx, sy - 3)))
          J = K = B;
        if (Bl < El && allEq4(E, G, H, I, S) && noneEq4(E, A, D, C, F))
          J = K = B;
        if (Hl < El && allEq4(E, A, B, C, P) && noneEq4(E, D, G, I, F))
          L = M = H;
        if (Fl < El && allEq4(E, A, D, G, Q) && noneEq4(E, B, C, I, H))
          K = M = F;
        if (Dl < El && allEq4(E, C, F, I, R) && noneEq4(E, B, A, G, H))
          J = L = D;

        // 2:1 slope rules
        if (H != B) {
          if (H != A && H != E && H != C) {
            if (allEq3(H, G, F, R) && noneEq2(H, D, src(sx + 2, sy - 1)))
              L = M;
            if (allEq3(H, I, D, Q) && noneEq2(H, F, src(sx - 2, sy - 1)))
              M = L;
          }
          if (B != I && B != G && B != E) {
            if (allEq3(B, A, F, R) && noneEq2(B, D, src(sx + 2, sy + 1)))
              J = K;
            if (allEq3(B, C, D, Q) && noneEq2(B, F, src(sx - 2, sy + 1)))
              K = J;
          }
        }
        if (F != D) {
          if (D != I && D != E && D != C) {
            if (allEq3(D, A, H, S) && noneEq2(D, B, src(sx + 1, sy + 2)))
              J = L;
            if (allEq3(D, G, B, P) && noneEq2(D, H, src(sx + 1, sy - 2)))
              L = J;
          }
          if (F != E && F != A && F != G) {
            if (allEq3(F, C, H, S) && noneEq2(F, B, src(sx - 1, sy + 2)))
              K = M;
            if (allEq3(F, I, B, P) && noneEq2(F, H, src(sx - 1, sy - 2)))
              M = K;
          }
        }
      }

      uint32_t *out = dst + (2 * sy) * (2 * w) + 2 * sx;
      out[0] = J;
      out[1] = K;
      out[2 * w] = L;
      out[2 * w + 1] = M;
    }
  });
}

// ----------------------------------------------------------------------------
// FSR 1 (EASU + RCAS), the float path of ffx_fsr1.h ported to the CPU:
//   Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//   MIT license. https://github.com/GPUOpen-Effects/FidelityFX-FSR
// Works on premultiplied RGBA so sprite edges blend against transparency
// instead of picking up the colour of the transparent pixels.
// ----------------------------------------------------------------------------

struct Px {
  float r, g, b, a;
};

static inline float easuLuma(const Px &p) { return p.b * 0.5f + (p.r * 0.5f + p.g); }

static inline void easuTap(Px &aC, float &aW, float offX, float offY,
                           float dirX, float dirY, float len2X, float len2Y,
                           float lob, float clp, const Px &c) {
  // Rotate the offset by the edge direction, then stretch it
  float vx = offX * dirX + offY * dirY;
  float vy = offX * -dirY + offY * dirX;
  vx *= len2X;
  vy *= len2Y;
  float d2 = std::min(vx * vx + vy * vy, clp);
  // Lanczos-2 approximation
  float wB = 2.0f / 5.0f * d2 - 1.0f;
  float wA = lob * d2 - 1.0f;
  wB *= wB;
  wA *= wA;
  wB = 25.0f / 16.0f * wB - (25.0f / 16.0f - 1.0f);
  float w = wB * wA;
  aC.r += c.r * w;
  aC.g += c.g * w;
  aC.b += c.b * w;
  aC.a += c.a * w;
  aW += w;
}

static inline void easuSet(float &dirX, float &dirY, float &len, float w,
                           float lA, float lB, float lC, float lD, float lE) {
  //    a
  //  b c d
  //    e
  float dc = lD - lC, cb = lC - lB;
  float lenX = std::max(std::fabs(dc), std::fabs(cb));
  float dX = lD - lB;
  dirX += dX * w;
  lenX = lenX > 0.0f ? std::clamp(std::fabs(dX) / lenX, 0.0f, 1.0f) : 0.0f;
  len += lenX * lenX * w;

  float ec = lE - lC, ca = lC - lA;
  float lenY = std::max(std::fabs(ec), std::fabs(ca));
  float dY = lE - lA;
  dirY += dY * w;
  lenY = lenY > 0.0f ? std::clamp(std::fabs(dY) / lenY, 0.0f, 1.0f) : 0.0f;
  len += lenY * lenY * w;
}

static void easu(const std::vector<Px> &in, int w, int h, std::vector<Px> &out,
                 int W, int H) {
  auto at = [&](int x, int y) -> const Px & {
    return in[std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1)];
  };
  float sx = static_cast<float>(w) / W, sy = static_cast<float>(h) / H;
  forRows(H, [&](int oy) {
    for (int ox = 0; ox < W; ox++) {
      float ppx = (ox + 0.5f) * sx - 0.5f, ppy = (oy + 0.5f) * sy - 0.5f;
      int fx = static_cast<int>(std::floor(ppx)),
          fy = static_cast<int>(std::floor(ppy));
      ppx -= fx;
      ppy -= fy;
      // 12-tap kernel
      //    b c
      //  e f g h
      //  i j k l
      //    n o
      const Px &b = at(fx, fy - 1), &c = at(fx + 1, fy - 1);
      const Px &e = at(fx - 1, fy), &f = at(fx, fy), &g = at(fx + 1, fy),
               &hh = at(fx + 2, fy);
      const Px &i = at(fx - 1, fy + 1), &j = at(fx, fy + 1),
               &k = at(fx + 1, fy + 1), &l = at(fx + 2, fy + 1);
      const Px &n = at(fx, fy + 2), &o = at(fx + 1, fy + 2);
      float bL = easuLuma(b), cL = easuLuma(c), eL = easuLuma(e),
            fL = easuLuma(f), gL = easuLuma(g), hL = easuLuma(hh),
            iL = easuLuma(i), jL = easuLuma(j), kL = easuLuma(k),
            lL = easuLuma(l), nL = easuLuma(n), oL = easuLuma(o);

      float dirX = 0, dirY = 0, len = 0;
      easuSet(dirX, dirY, len, (1 - ppx) * (1 - ppy), bL, eL, fL, gL, jL);
      easuSet(dirX, dirY, len, ppx * (1 - ppy), cL, fL, gL, hL, kL);
      easuSet(dirX, dirY, len, (1 - ppx) * ppy, fL, iL, jL, kL, nL);
      easuSet(dirX, dirY, len, ppx * ppy, gL, jL, kL, lL, oL);

      float dirR = dirX * dirX + dirY * dirY;
      if (dirR < 1.0f / 32768.0f) {
        dirR = 1.0f;
        dirX = 1.0f;
      } else {
        dirR = 1.0f / std::sqrt(dirR);
      }
      dirX *= dirR;
      dirY *= dirR;
      len = len * 0.5f;
      len *= len;
      float stretch = (dirX * dirX + dirY * dirY) /
                      std::max(std::fabs(dirX), std::fabs(dirY));
      float len2X = 1.0f + (stretch - 1.0f) * len;
      float len2Y = 1.0f - 0.5f * len;
      float lob = 0.5f + ((1.0f / 4.0f - 0.04f) - 0.5f) * len;
      float clp = 1.0f / lob;

      Px aC = {0, 0, 0, 0};
      float aW = 0;
      easuTap(aC, aW, 0 - ppx, -1 - ppy, dirX, dirY, len2X, len2Y, lob, clp, b);
      easuTap(aC, aW, 1 - ppx, -1 - ppy, dirX, dirY, len2X, len2Y, lob, clp, c);
      easuTap(aC, aW, -1 - ppx, 1 - ppy, dirX, dirY, len2X, len2Y, lob, clp, i);
      easuTap(aC, aW, 0 - ppx, 1 - ppy, dirX, dirY, len2X, len2Y, lob, clp, j);
      easuTap(aC, aW, 0 - ppx, 0 - ppy, dirX, dirY, len2X, len2Y, lob, clp, f);
      easuTap(aC, aW, -1 - ppx, 0 - ppy, dirX, dirY, len2X, len2Y, lob, clp, e);
      easuTap(aC, aW, 1 - ppx, 1 - ppy, dirX, dirY, len2X, len2Y, lob, clp, k);
      easuTap(aC, aW, 2 - ppx, 1 - ppy, dirX, dirY, len2X, len2Y, lob, clp, l);
      easuTap(aC, aW, 2 - ppx, 0 - ppy, dirX, dirY, len2X, len2Y, lob, clp, hh);
      easuTap(aC, aW, 1 - ppx, 0 - ppy, dirX, dirY, len2X, len2Y, lob, clp, g);
      easuTap(aC, aW, 1 - ppx, 2 - ppy, dirX, dirY, len2X, len2Y, lob, clp, o);
      easuTap(aC, aW, 0 - ppx, 2 - ppy, dirX, dirY, len2X, len2Y, lob, clp, n);

      // Normalize and dering against the 4 nearest
      float rw = aW != 0.0f ? 1.0f / aW : 0.0f;
      auto dering = [&](float v, float p0, float p1, float p2, float p3) {
        float mn = std::min(std::min(p0, p1), std::min(p2, p3));
        float mx = std::max(std::max(p0, p1), std::max(p2, p3));
        return std::min(mx, std::max(mn, v));
      };
      Px &px = out[oy * W + ox];
      px.r = dering(aC.r * rw, f.r, g.r, j.r, k.r);
      px.g = dering(aC.g * rw, f.g, g.g, j.g, k.g);
      px.b = dering(aC.b * rw, f.b, g.b, j.b, k.b);
      px.a = dering(aC.a * rw, f.a, g.a, j.a, k.a);
    }
  });
}

static void rcas(const std::vector<Px> &in, int w, int h, std::vector<Px> &out,
                 float sharpness) {
  const float limit = 0.25f - 1.0f / 16.0f;
  const float con = std::exp2(-sharpness);
  auto at = [&](int x, int y) -> const Px & {
    return in[std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1)];
  };
  forRows(h, [&](int y) {
    for (int x = 0; x < w; x++) {
      //    b
      //  d e f
      //    h
      const Px &b = at(x, y - 1), &d = at(x - 1, y), &e = at(x, y),
               &f = at(x + 1, y), &hh = at(x, y + 1);
      auto lobeOf = [&](float bv, float dv, float ev, float fv, float hv) {
        float mn4 = std::min(std::min(bv, dv), std::min(fv, hv));
        float mx4 = std::max(std::max(bv, dv), std::max(fv, hv));
        float hitMin = mx4 > 0.0f ? std::min(mn4, ev) / (4.0f * mx4) : 0.0f;
        float denom = 4.0f * mn4 - 4.0f;
        float hitMax =
            denom < -1e-6f ? (1.0f - std::max(mx4, ev)) / denom : -1e9f;
        return std::max(-hitMin, hitMax);
      };
      float lobe = std::max(lobeOf(b.r, d.r, e.r, f.r, hh.r),
                            std::max(lobeOf(b.g, d.g, e.g, f.g, hh.g),
                                     lobeOf(b.b, d.b, e.b, f.b, hh.b)));
      lobe = std::max(-limit, std::min(lobe, 0.0f)) * con;
      float rcpL = 1.0f / (4.0f * lobe + 1.0f);
      Px &p = out[y * w + x];
      p.r = (lobe * (b.r + d.r + hh.r + f.r) + e.r) * rcpL;
      p.g = (lobe * (b.g + d.g + hh.g + f.g) + e.g) * rcpL;
      p.b = (lobe * (b.b + d.b + hh.b + f.b) + e.b) * rcpL;
      p.a = e.a; // alpha passes through
    }
  });
}

// ----------------------------------------------------------------------------

static SDL_Surface *upscale(SDL_Surface *surface, int k) {
  SDL_Surface *rgba =
      SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
  if (!rgba)
    return nullptr;
  int w = rgba->w, h = rgba->h;
  std::vector<uint32_t> px(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; y++) {
    const uint32_t *row = reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch);
    for (int x = 0; x < w; x++) {
      uint32_t c = row[x];
      // Fully transparent pixels all count as the same "nothing"
      px[static_cast<size_t>(y) * w + x] = (c >> 24) == 0 ? 0 : c;
    }
  }
  SDL_FreeSurface(rgba);

  int outScale = 1;
  std::vector<uint32_t> result;
  if (g_mode == Mode::Mmpx) {
    // MMPX doubles; repeat until at least the wanted factor
    result = px;
    int cw = w, ch = h;
    while (outScale < k) {
      std::vector<uint32_t> next(static_cast<size_t>(cw) * ch * 4);
      mmpx2x(result.data(), next.data(), cw, ch);
      result.swap(next);
      cw *= 2;
      ch *= 2;
      outScale *= 2;
    }
  } else {
    outScale = k;
    int W = w * k, H = h * k;
    std::vector<Px> in(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < in.size(); i++) {
      uint32_t c = px[i];
      float a = ((c >> 24) & 0xFF) / 255.0f;
      in[i] = {(c & 0xFF) / 255.0f * a, ((c >> 8) & 0xFF) / 255.0f * a,
               ((c >> 16) & 0xFF) / 255.0f * a, a};
    }
    std::vector<Px> big(static_cast<size_t>(W) * H), sharp(big.size());
    easu(in, w, h, big, W, H);
    rcas(big, W, H, sharp, g_sharpness);
    result.resize(sharp.size());
    for (size_t i = 0; i < sharp.size(); i++) {
      const Px &p = sharp[i];
      float a = std::clamp(p.a, 0.0f, 1.0f);
      auto ch = [&](float v) {
        float u = a > 0.0f ? v / a : 0.0f;
        return static_cast<uint32_t>(std::lround(std::clamp(u, 0.0f, 1.0f) * 255.0f));
      };
      uint32_t ai = static_cast<uint32_t>(std::lround(a * 255.0f));
      result[i] = ai == 0 ? 0 : (ch(p.r) | ch(p.g) << 8 | ch(p.b) << 16 | ai << 24);
    }
  }

  int W = w * outScale, H = h * outScale;
  SDL_Surface *out =
      SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
  if (!out)
    return nullptr;
  for (int y = 0; y < H; y++)
    std::memcpy(static_cast<uint8_t *>(out->pixels) + y * out->pitch,
                result.data() + static_cast<size_t>(y) * W, W * 4);
  return out;
}

SDL_Texture *createTexture(SDL_Renderer *renderer, SDL_Surface *surface) {
  if (!renderer || !surface)
    return nullptr;
  if (g_mode == Mode::Off || g_factor <= 1)
    return SDL_CreateTextureFromSurface(renderer, surface);
  SDL_Surface *big = upscale(surface, g_factor);
  if (!big)
    return SDL_CreateTextureFromSurface(renderer, surface);
  int scale = big->w / std::max(1, surface->w);
  SDL_Texture *t = makeTexture(renderer, big, scale);
  SDL_FreeSurface(big);
  return t;
}

SDL_Surface *loadHdSurface(const std::string &path) {
  std::string file = "hd/" + path + ".png";
  std::error_code ec;
  if (!std::filesystem::exists(file, ec))
    return nullptr;
  SDL_Surface *s = IMG_Load(file.c_str());
  if (!s)
    SDL_Log("[ArtScaler] could not load %s: %s", file.c_str(), IMG_GetError());
  return s;
}

SDL_Texture *createHdTexture(SDL_Renderer *renderer, SDL_Surface *hd, int w,
                             int h, const std::string &path) {
  if (!renderer || !hd || w <= 0 || h <= 0)
    return nullptr;
  int scale = hd->w / w;
  if (scale < 1 || hd->w != w * scale || hd->h != h * scale) {
    SDL_Log("[ArtScaler] HD art %s is %dx%d, not a whole multiple of the "
            "original %dx%d - ignored",
            path.c_str(), hd->w, hd->h, w, h);
    return nullptr;
  }
  return makeTexture(renderer, hd, scale);
}

} // namespace ArtScaler
