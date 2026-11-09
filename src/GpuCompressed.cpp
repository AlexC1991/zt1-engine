#include "GpuCompressed.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_opengl.h>

#include "ArtScaler.hpp"
#include "Bc7.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL3/SDL_system.h>
#include <d3d11.h>
// (SDL's own records as SDL 2.32 lays them out - src/render/SDL_sysrender.h
// and src/render/direct3d11/SDL_render_d3d11.c in vendor/SDL - checked
// against the texture before anything's changed)
#if SDL_VERSION_ATLEAST(2, 32, 0) && !SDL_VERSION_ATLEAST(2, 33, 0)
#define ZT_D3D11_SWAP 1
#endif
#endif

namespace GpuCompressed {
namespace {

constexpr GLenum kBptcRgba = 0x8E8C; // GL_COMPRESSED_RGBA_BPTC_UNORM

struct State {
  std::string backend;
  bool ok = false;
};
std::mutex g_lock;
std::unordered_map<SDL_Renderer *, State> g_states;
std::atomic<size_t> g_made{0}, g_bytes{0};

struct Gl {
  bool loaded = false;
  void(APIENTRY *CompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *) = nullptr;
  GLenum(APIENTRY *GetError)() = nullptr;
} g_gl;

bool loadGl() {
  if (!g_gl.loaded) {
    g_gl.loaded = true;
    g_gl.CompressedTexImage2D = reinterpret_cast<decltype(g_gl.CompressedTexImage2D)>(SDL_GL_GetProcAddress("glCompressedTexImage2D"));
    g_gl.GetError = reinterpret_cast<decltype(g_gl.GetError)>(SDL_GL_GetProcAddress("glGetError"));
  }
  return g_gl.CompressedTexImage2D && g_gl.GetError;
}

#ifdef ZT_D3D11_SWAP
struct SdlTexture {
  const void *magic;
  Uint32 format;
  int access;
  int w;
  int h;
  int modMode;
  SDL_BlendMode blendMode;
  SDL_ScaleMode scaleMode;
  SDL_Color color;
  SDL_Renderer *renderer;
  SDL_Texture *native;
  void *yuv;
  void *pixels;
  int pitch;
  SDL_Rect locked_rect;
  SDL_Surface *locked_surface;
  Uint32 last_command_generation;
  void *driverdata;
  void *userdata;
  SDL_Texture *prev;
  SDL_Texture *next;
};
struct D3D11TextureStart { // (D3D11_TextureData's first two)
  ID3D11Texture2D *mainTexture;
  ID3D11ShaderResourceView *mainTextureResourceView;
};
#endif

std::string backendOf(SDL_Renderer *renderer) {
  ZT_RendererInfo info{};
  return ZT_GetRendererInfo(renderer, &info) == 0 && info.name ? info.name : "";
}

void refuse(SDL_Renderer *renderer, const char *why) {
  std::lock_guard<std::mutex> lock(g_lock);
  auto it = g_states.find(renderer);
  if (it != g_states.end() && it->second.ok) {
    it->second.ok = false;
    SDL_Log("[GpuCompressed] %s: %s - zoomed-in HD art stays uncompressed", it->second.backend.c_str(), why);
  }
}

// The storage of t (made by SDL, ARGB8888, w x h) swapped for the blocks
bool swapGl(SDL_Renderer *renderer, SDL_Texture *t, int w, int h, const std::vector<uint8_t> &blocks) {
  float tw = 0, th = 0;
  if (ZT_GL_BindTexture(t, &tw, &th) != 0)
    return false;
  // (a GL_TEXTURE_2D of its own size: SDL gives 1, 1; a rectangle texture or
  // one rounded up to a power of 2 can't be)
  if (tw != 1.0f || th != 1.0f) {
    ZT_GL_UnbindTexture(t);
    refuse(renderer, "textures here aren't plain 2D ones");
    return false;
  }
  while (g_gl.GetError() != GL_NO_ERROR) {
  }
  g_gl.CompressedTexImage2D(GL_TEXTURE_2D, 0, kBptcRgba, w, h, 0, static_cast<GLsizei>(blocks.size()), blocks.data());
  const GLenum err = g_gl.GetError();
  ZT_GL_UnbindTexture(t);
  if (err != GL_NO_ERROR) {
    refuse(renderer, "the driver didn't take the compressed texture");
    return false;
  }
  return true;
}

#ifdef ZT_D3D11_SWAP
bool swapD3d11(SDL_Renderer *renderer, SDL_Texture *t, int w, int h, const std::vector<uint8_t> &blocks) {
  // (SDL's record of it, as laid out above: checked by what's known of it)
  const auto *rec = reinterpret_cast<const SdlTexture *>(t);
  void *const mark = reinterpret_cast<void *>(intptr_t(0x5A17));
  ZT_SetTextureUserData(t, mark);
  if (rec->w != w || rec->h != h || rec->format != SDL_PIXELFORMAT_ARGB8888 || rec->renderer != renderer ||
      rec->native || rec->userdata != mark || !rec->driverdata) {
    ZT_SetTextureUserData(t, nullptr);
    refuse(renderer, "SDL's texture record isn't laid out as expected");
    return false;
  }
  ZT_SetTextureUserData(t, nullptr);
  auto *data = static_cast<D3D11TextureStart *>(rec->driverdata);
  if (!data->mainTexture || !data->mainTextureResourceView) {
    refuse(renderer, "SDL's Direct3D texture isn't laid out as expected");
    return false;
  }
  D3D11_TEXTURE2D_DESC desc{};
  data->mainTexture->GetDesc(&desc);
  ID3D11Resource *viewed = nullptr;
  data->mainTextureResourceView->GetResource(&viewed);
  const bool matches = desc.Width == UINT(w) && desc.Height == UINT(h) && desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
                       viewed == data->mainTexture;
  if (viewed)
    viewed->Release();
  if (!matches) {
    refuse(renderer, "SDL's Direct3D texture isn't laid out as expected");
    return false;
  }
  ID3D11Device *device = ZT_RenderGetD3D11Device(renderer);
  if (!device)
    return false;
  D3D11_TEXTURE2D_DESC bc{};
  bc.Width = UINT(w);
  bc.Height = UINT(h);
  bc.MipLevels = 1;
  bc.ArraySize = 1;
  bc.Format = DXGI_FORMAT_BC7_UNORM;
  bc.SampleDesc.Count = 1;
  bc.Usage = D3D11_USAGE_IMMUTABLE;
  bc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init{};
  init.pSysMem = blocks.data();
  init.SysMemPitch = UINT(w / 4 * 16);
  ID3D11Texture2D *texture = nullptr;
  ID3D11ShaderResourceView *view = nullptr;
  bool done = SUCCEEDED(device->CreateTexture2D(&bc, &init, &texture)) &&
              SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view));
  if (done) {
    data->mainTextureResourceView->Release();
    data->mainTexture->Release();
    data->mainTexture = texture;
    data->mainTextureResourceView = view;
  } else {
    if (view)
      view->Release();
    if (texture)
      texture->Release();
  }
  device->Release();
  return done;
}
#endif

} // namespace

bool supported(SDL_Renderer *renderer) {
  if (!renderer)
    return false;
  const std::string backend = backendOf(renderer);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_states.find(renderer);
    if (it != g_states.end() && it->second.backend == backend)
      return it->second.ok;
  }
  bool ok = false;
  if (backend == "opengl") {
    ok = loadGl() && SDL_GL_ExtensionSupported("GL_ARB_texture_compression_bptc");
  }
#ifdef ZT_D3D11_SWAP
  else if (backend == "direct3d11") {
    if (ID3D11Device *device = ZT_RenderGetD3D11Device(renderer)) {
      UINT support = 0;
      ok = SUCCEEDED(device->CheckFormatSupport(DXGI_FORMAT_BC7_UNORM, &support)) &&
           (support & D3D11_FORMAT_SUPPORT_TEXTURE2D) && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
      device->Release();
    }
  }
#endif
  std::lock_guard<std::mutex> lock(g_lock);
  g_states[renderer] = {backend, ok};
  SDL_Log("[GpuCompressed] %s: %s", backend.c_str(), ok ? "zoomed-in HD art held compressed (BC7)" : "no BC7 here - uncompressed");
  return ok;
}

// (blocks for a w x h texture - multiples of 4 - as a texture, or nullptr)
static SDL_Texture *fromBlocks(SDL_Renderer *renderer, int w, int h, const std::vector<uint8_t> &blocks) {
  SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!t)
    return nullptr;
  const std::string backend = backendOf(renderer);
  bool done = false;
  if (backend == "opengl")
    done = swapGl(renderer, t, w, h, blocks);
#ifdef ZT_D3D11_SWAP
  else if (backend == "direct3d11")
    done = swapD3d11(renderer, t, w, h, blocks);
#endif
  if (!done) {
    SDL_DestroyTexture(t);
    return nullptr;
  }
  return t;
}

bool encode(SDL_Surface *surface, bool best, Blocks &out, int threads) {
  if (!surface || surface->w <= 0 || surface->h <= 0)
    return false;
  SDL_Surface *rgba = surface->format == SDL_PIXELFORMAT_RGBA32
                          ? surface
                          : SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  if (!rgba)
    return false;
  const int w = surface->w, h = surface->h, w4 = (w + 3) & ~3, h4 = (h + 3) & ~3;
  // (made up to whole blocks: the last column and row repeated, so a
  // smooth draw's edge comes out as it does from the ordinary texture)
  std::vector<uint8_t> image(size_t(w4) * h4 * 4);
  if (SDL_MUSTLOCK(rgba))
    SDL_LockSurface(rgba);
  for (int y = 0; y < h4; y++) {
    const uint8_t *row = static_cast<const uint8_t *>(rgba->pixels) + size_t(std::min(y, h - 1)) * rgba->pitch;
    uint8_t *to = image.data() + size_t(y) * w4 * 4;
    std::memcpy(to, row, size_t(w) * 4);
    for (int x = w; x < w4; x++)
      std::memcpy(to + size_t(x) * 4, row + size_t(w - 1) * 4, 4);
  }
  if (SDL_MUSTLOCK(rgba))
    SDL_UnlockSurface(rgba);
  if (rgba != surface)
    SDL_DestroySurface(rgba);
  out.w = w;
  out.h = h;
  out.w4 = w4;
  out.h4 = h4;
  out.best = best;
  out.data.assign(size_t(w4 / 4) * (h4 / 4) * 16, 0);
  Bc7::encode(image.data(), w4, h4, w4 * 4, out.data.data(), threads, best);
  return true;
}

SDL_Texture *upload(SDL_Renderer *renderer, const Blocks &b, int scale) {
  if (b.data.empty() || !supported(renderer))
    return nullptr;
  SDL_Texture *t = fromBlocks(renderer, b.w4, b.h4, b.data);
  if (!t)
    return nullptr;
  ArtScaler::markPadded(t, scale, b.w4 - b.w, b.h4 - b.h, true);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
  g_made++;
  g_bytes += b.data.size();
  return t;
}

SDL_Texture *create(SDL_Renderer *renderer, SDL_Surface *surface, int scale, bool best) {
  if (!supported(renderer))
    return nullptr;
  Blocks b;
  return encode(surface, best, b) ? upload(renderer, b, scale) : nullptr;
}

std::string selfCheck(SDL_Renderer *renderer) {
  const int w = 30, h = 22, w4 = 32, h4 = 24;
  SDL_Surface *picture = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
  if (!picture)
    return "no surface";
  uint32_t seed = 12345;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      seed = seed * 1664525u + 1013904223u;
      const int noise = int(seed >> 28) - 8;
      uint8_t *p = static_cast<uint8_t *>(picture->pixels) + y * picture->pitch + x * 4;
      p[0] = uint8_t(std::clamp(x * 8 + noise, 0, 255));
      p[1] = uint8_t(std::clamp(y * 11 + noise, 0, 255));
      p[2] = uint8_t(std::clamp(200 - x * 3 - y * 2, 0, 255));
      p[3] = uint8_t(x < 4 ? 0 : x < 10 ? (x - 4) * 42 : 255); // (clear, soft, solid)
    }
  // The encoder: as create pads and encodes it, decoded on the CPU
  std::vector<uint8_t> image(size_t(w4) * h4 * 4), blocks(size_t(w4 / 4) * (h4 / 4) * 16), decoded(image.size());
  for (int y = 0; y < h4; y++)
    for (int x = 0; x < w4; x++)
      std::memcpy(&image[(size_t(y) * w4 + x) * 4],
                  static_cast<uint8_t *>(picture->pixels) + std::min(y, h - 1) * picture->pitch + std::min(x, w - 1) * 4, 4);
  Bc7::encode(image.data(), w4, h4, w4 * 4, blocks.data(), 1, true);
  for (int by = 0; by < h4 / 4; by++)
    for (int bx = 0; bx < w4 / 4; bx++) {
      uint8_t px[64];
      if (!Bc7::decodeBlock(&blocks[(size_t(by) * (w4 / 4) + bx) * 16], px)) {
        SDL_DestroySurface(picture);
        return "a block isn't mode 6";
      }
      for (int y = 0; y < 4; y++)
        std::memcpy(&decoded[(size_t(by * 4 + y) * w4 + bx * 4) * 4], px + y * 16, 16);
    }
  double sq = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      for (int c = 0; c < 4; c++) {
        const double d = double(decoded[(size_t(y) * w4 + x) * 4 + c]) - image[(size_t(y) * w4 + x) * 4 + c];
        sq += d * d;
      }
  const double psnr = 10.0 * std::log10(255.0 * 255.0 / std::max(1e-9, sq / (w * h * 4)));
  std::string fail;
  if (psnr < 32.0) // (it measures 33.7: the noise is hard on any 4 x 4 block)
    fail = "encoded at only " + std::to_string(psnr) + " dB";
  // The GPU: the texture create makes, drawn 1:1 and read back
  if (fail.empty() && supported(renderer)) {
    SDL_Texture *t = create(renderer, picture, 1, true);
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_Rect content{};
    if (!t || !target)
      fail = "no compressed texture made";
    else if (!ArtScaler::contentRect(t, &content) || content.w != w || content.h != h)
      fail = "its padding isn't marked";
    else {
      int qw = 0, qh = 0;
      ArtScaler::querySize(t, &qw, &qh);
      SDL_Texture *was = SDL_GetRenderTarget(renderer);
      SDL_Rect viewport;
      SDL_GetRenderViewport(renderer, &viewport);
      float sx = 1, sy = 1;
      SDL_GetRenderScale(renderer, &sx, &sy);
      std::vector<uint8_t> read(size_t(w) * h * 4);
      SDL_SetRenderTarget(renderer, target);
      SDL_SetRenderScale(renderer, 1, 1);
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
      SDL_Rect to{0, 0, w, h};
      ZT_RenderCopy(renderer, t, &content, &to);
      const int got = ZT_RenderReadPixels(renderer, &to, SDL_PIXELFORMAT_ABGR8888, read.data(), w * 4);
      SDL_SetRenderTarget(renderer, was);
      SDL_SetRenderViewport(renderer, &viewport);
      SDL_SetRenderScale(renderer, sx, sy);
      int worst = 0;
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
          for (int c = 0; c < 4; c++)
            worst = std::max(worst, std::abs(int(read[(size_t(y) * w + x) * 4 + c]) - decoded[(size_t(y) * w4 + x) * 4 + c]));
      if (qw != w || qh != h)
        fail = "its size isn't the picture's";
      else if (got != 0)
        fail = std::string("couldn't read it back: ") + SDL_GetError();
      else if (worst > 1)
        fail = "the GPU draws it " + std::to_string(worst) + " off the decoded picture";
    }
    if (t)
      SDL_DestroyTexture(t);
    if (target)
      SDL_DestroyTexture(target);
  }
  // The decoder (and so the encoder's choices) against the GPU's own: random
  // blocks of each mode written, the two-group ones in each of the 64
  // patterns, drawn and read back - a wrong pattern or anchor table, or a
  // field out of place, can't match
  if (fail.empty() && supported(renderer)) {
    const int modes[5] = {1, 3, 7, 5, 6}, bw = 64, bh = 5, tw = bw * 4, th = bh * 4;
    std::vector<uint8_t> random(size_t(bw) * bh * 16), want(size_t(tw) * th * 4), read(want.size());
    for (int row = 0; row < bh; row++)
      for (int k = 0; k < bw; k++) {
        uint8_t *b = &random[(size_t(row) * bw + k) * 16];
        for (int j = 0; j < 16; j++) {
          seed = seed * 1664525u + 1013904223u;
          b[j] = uint8_t(seed >> 24);
        }
        const int mode = modes[row];
        // (the mode's bits: mode zeros then a one; then, for two groups,
        // the pattern k)
        uint64_t low = 0;
        std::memcpy(&low, b, 8);
        low &= ~((uint64_t(1) << (mode + 1)) - 1);
        low |= uint64_t(1) << mode;
        if (mode != 5 && mode != 6) {
          low &= ~(uint64_t(63) << (mode + 1));
          low |= uint64_t(k) << (mode + 1);
        }
        std::memcpy(b, &low, 8);
        uint8_t px[64];
        if (!Bc7::decodeBlock(b, px)) {
          fail = "mode " + std::to_string(mode) + " doesn't decode";
          break;
        }
        for (int y = 0; y < 4; y++)
          std::memcpy(&want[(size_t(row * 4 + y) * tw + k * 4) * 4], px + y * 16, 16);
      }
    SDL_Texture *t = fail.empty() ? fromBlocks(renderer, tw, th, random) : nullptr;
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, tw, th);
    if (fail.empty() && (!t || !target))
      fail = "no texture of random blocks made";
    else if (fail.empty()) {
      SDL_Texture *was = SDL_GetRenderTarget(renderer);
      SDL_Rect viewport;
      SDL_GetRenderViewport(renderer, &viewport);
      float sx = 1, sy = 1;
      SDL_GetRenderScale(renderer, &sx, &sy);
      SDL_SetRenderTarget(renderer, target);
      SDL_SetRenderScale(renderer, 1, 1);
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
      SDL_Rect all{0, 0, tw, th};
      ZT_RenderCopy(renderer, t, &all, &all);
      const int got = ZT_RenderReadPixels(renderer, &all, SDL_PIXELFORMAT_ABGR8888, read.data(), tw * 4);
      SDL_SetRenderTarget(renderer, was);
      SDL_SetRenderViewport(renderer, &viewport);
      SDL_SetRenderScale(renderer, sx, sy);
      if (got != 0)
        fail = std::string("couldn't read the random blocks back: ") + SDL_GetError();
      for (int row = 0; row < bh && fail.empty(); row++)
        for (int k = 0; k < bw && fail.empty(); k++)
          for (int y = 0; y < 4 && fail.empty(); y++)
            for (int x = 0; x < 16; x++) {
              const size_t at = (size_t(row * 4 + y) * tw + k * 4) * 4 + x;
              if (std::abs(int(read[at]) - int(want[at])) > 1) {
                fail = "mode " + std::to_string(modes[row]) + (modes[row] == 5 || modes[row] == 6 ? "" : " pattern " + std::to_string(k)) +
                       " decodes differently on the GPU";
                break;
              }
            }
    }
    if (t)
      SDL_DestroyTexture(t);
    if (target)
      SDL_DestroyTexture(target);
  }
  SDL_DestroySurface(picture);
  return fail;
}

std::string stats() {
  return "compressed HD textures made: " + std::to_string(g_made.load()) + ", " +
         std::to_string(g_bytes.load() / (1024 * 1024)) + " MB";
}

} // namespace GpuCompressed
