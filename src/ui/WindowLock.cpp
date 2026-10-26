#include "WindowLock.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../Animation.hpp"
#include "../ResourceManager.hpp"

namespace WindowLock {

namespace {

// The padlock, 7 x 8, centred where the X crossed: '#' in the X's colour,
// '.' left as the button's face
// (shut: the shackle down into the body; open: lifted, its left leg free)
const char *const kShut[8] = {"..###..", ".#...#.", ".#...#.", "#######", "###.###", "###.###", "#######", "......."};
const char *const kOpen[8] = {"...###.", "..#...#", "......#", "#######", "###.###", "###.###", "#######", "......."};

std::map<std::pair<SDL_Renderer *, int>, SDL_Texture *> g_made; // (renderer, look) -> texture

// A look: one of the close button's frames - "N" normal, "H" lit, "S" its
// gold-rimmed one - with a shut or open padlock
SDL_Texture *make(SDL_Renderer *renderer, ResourceManager *rm, bool locked, char frame) {
  const int key = (locked ? 1 : 0) | (frame << 1);
  auto it = g_made.find({renderer, key});
  if (it != g_made.end())
    return it->second;
  SDL_Texture *t = nullptr;
  Animation *close = rm ? rm->getAnimation("ui/sharedui/close/close") : nullptr;
  SDL_Surface *src = close ? close->frameSurface(std::string(1, frame), 0) : nullptr;
  if (src) {
    SDL_Surface *s = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (s) {
      Uint32 *px = static_cast<Uint32 *>(s->pixels);
      const int pitch = s->pitch / 4, w = s->w, h = s->h;
      auto rgb = [&](int x, int y, Uint8 &r, Uint8 &g, Uint8 &b, Uint8 &a) {
        SDL_GetRGBA(px[y * pitch + x], s->format, &r, &g, &b, &a);
      };
      // The X's colour: where its strokes cross, the middle
      const int cx = w / 2, cy = h / 2;
      Uint8 xr, xg, xb, xa;
      rgb(cx, cy, xr, xg, xb, xa);
      auto isX = [&](int x, int y) {
        Uint8 r, g, b, a;
        rgb(x, y, r, g, b, a);
        return a > 0 && std::abs(r - xr) + std::abs(g - xg) + std::abs(b - xb) < 90;
      };
      // The X out: each of its pixels inside the frame takes the face's
      // colour beside it on its row (the nearest that isn't X)
      const int x0 = 4, x1 = w - 5, y0 = 3, y1 = h - 4;
      std::vector<Uint32> out(px, px + pitch * h);
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
          if (!isX(x, y))
            continue;
          for (int d = 1; d < w; d++) {
            if (x - d >= x0 && !isX(x - d, y)) {
              out[y * pitch + x] = px[y * pitch + x - d];
              break;
            }
            if (x + d <= x1 && !isX(x + d, y)) {
              out[y * pitch + x] = px[y * pitch + x + d];
              break;
            }
          }
        }
      // The padlock in the X's colour
      const Uint32 ink = SDL_MapRGBA(s->format, xr, xg, xb, 255);
      const char *const *glyph = locked ? kShut : kOpen;
      for (int gy = 0; gy < 8; gy++)
        for (int gx = 0; gx < 7; gx++)
          if (glyph[gy][gx] == '#') {
            int x = cx - 3 + gx, y = cy - 4 + gy;
            if (x >= 0 && x < w && y >= 0 && y < h)
              out[y * pitch + x] = ink;
          }
      std::memcpy(px, out.data(), out.size() * 4);
      t = SDL_CreateTextureFromSurface(renderer, s);
      if (t)
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
      SDL_FreeSurface(s);
    }
  }
  g_made[{renderer, key}] = t;
  return t;
}

} // namespace

SDL_Rect beside(const SDL_Rect &closeRect) {
  return {closeRect.x - closeRect.w - 2, closeRect.y, closeRect.w, closeRect.h};
}

void draw(SDL_Renderer *renderer, ResourceManager *rm, const SDL_Rect &at, bool locked, bool lit, bool pressed) {
  // (as a toggle button: held down or locked, the gold rim; under the
  // mouse, lit)
  const char frame = pressed ? 'S' : lit ? 'H' : locked ? 'S' : 'N';
  if (SDL_Texture *t = make(renderer, rm, locked, frame))
    SDL_RenderCopy(renderer, t, nullptr, &at);
}

} // namespace WindowLock
