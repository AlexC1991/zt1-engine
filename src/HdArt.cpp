#include "HdArt.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "SDL_image.h"

#include "Animation.hpp"
#include "Mods.hpp"
#include "ResourceManager.hpp"

namespace fs = std::filesystem;

namespace HdArt {

namespace {
std::vector<std::string> g_roots;
int g_generation = 0;
bool g_read = false;

void ensure() {
  if (!g_read)
    refresh();
}

// A frame as RGBA32 (a new surface the caller frees)
SDL_Surface *rgba(SDL_Surface *s) { return SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0); }

// The clear pixels given their opaque neighbours' colour, a few pixels out
// (still clear): an upscaler blends across the outline, and black there
// left a dark fringe round every sprite
void bleed(SDL_Surface *s, int passes = 4) {
  const int w = s->w, h = s->h;
  std::vector<uint32_t> px(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; y++)
    std::memcpy(&px[static_cast<size_t>(y) * w], static_cast<uint8_t *>(s->pixels) + y * s->pitch, w * 4);
  std::vector<uint8_t> known(px.size());
  for (size_t i = 0; i < px.size(); i++)
    known[i] = (px[i] >> 24) != 0;
  for (int pass = 0; pass < passes; pass++) {
    std::vector<uint32_t> next = px;
    std::vector<uint8_t> nextKnown = known;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        size_t i = static_cast<size_t>(y) * w + x;
        if (known[i])
          continue;
        int r = 0, g = 0, b = 0, n = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if ((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h && known[static_cast<size_t>(ny) * w + nx]) {
              uint32_t c = px[static_cast<size_t>(ny) * w + nx];
              r += c & 0xFF;
              g += (c >> 8) & 0xFF;
              b += (c >> 16) & 0xFF;
              n++;
            }
          }
        if (n) {
          next[i] = static_cast<uint32_t>(r / n) | static_cast<uint32_t>(g / n) << 8 | static_cast<uint32_t>(b / n) << 16;
          nextKnown[i] = 1;
        }
      }
    px.swap(next);
    known.swap(nextKnown);
  }
  for (int y = 0; y < h; y++)
    std::memcpy(static_cast<uint8_t *>(s->pixels) + y * s->pitch, &px[static_cast<size_t>(y) * w], w * 4);
}
} // namespace

void refresh() {
  g_read = true;
  g_roots.clear();
  std::error_code ec;
  for (const std::string &m : Mods::enabledPaths()) {
    fs::path hd = fs::path(m) / "hd";
    if (fs::is_directory(hd, ec))
      g_roots.push_back(hd.string() + "/");
  }
  if (fs::is_directory("hd", ec))
    g_roots.push_back("hd/");
  g_generation++;
  SDL_Log("[HdArt] %d HD art folder(s) at work", static_cast<int>(g_roots.size()));
}

bool active() {
  ensure();
  return !g_roots.empty();
}

int generation() {
  ensure();
  return g_generation;
}

namespace {
int g_found = 0, g_missed = 0;
std::string g_lastMissed;
} // namespace

SDL_Surface *load(const std::string &path) {
  ensure();
  std::error_code ec;
  for (const std::string &root : g_roots) {
    std::string file = root + path + ".png";
    if (!fs::exists(file, ec))
      continue;
    SDL_Surface *s = IMG_Load(file.c_str());
    if (!s)
      SDL_Log("[HdArt] could not load %s: %s", file.c_str(), IMG_GetError());
    g_found++;
    return s;
  }
  if (!g_roots.empty()) {
    g_missed++;
    g_lastMissed = path;
  }
  return nullptr;
}

std::string stats() {
  ensure();
  std::string s = std::to_string(g_found) + " HD frames in use, " + std::to_string(g_missed) + " looked for and not in the mod";
  if (!g_lastMissed.empty())
    s += " (last: " + g_lastMissed + ")";
  for (const std::string &r : g_roots)
    s += "\n  from " + r;
  return s;
}

namespace {
ResourceManager *g_rm = nullptr;
// Recoloured art in its own palette, by name (dropped when the roots change)
std::map<std::string, std::unique_ptr<Animation>> g_ownPalette;
int g_ownGeneration = -1;
} // namespace

void setResources(ResourceManager *rm) { g_rm = rm; }

SDL_Surface *recolour(SDL_Surface *hd, SDL_Surface *painted, const std::string &name, const std::string &direction,
                      int frame) {
  if (!hd || !painted || !g_rm || painted->w <= 0)
    return hd;
  if (g_ownGeneration != g_generation) {
    g_ownPalette.clear();
    g_ownGeneration = g_generation;
  }
  auto it = g_ownPalette.find(name);
  if (it == g_ownPalette.end())
    it = g_ownPalette
             .emplace(name, std::unique_ptr<Animation>(g_rm->hasResource(name + ".ani") ? g_rm->getAnimation(name)
                                                                                         : nullptr))
             .first;
  SDL_Surface *base = it->second ? it->second->frameSurface(direction, frame) : nullptr;
  const int k = hd->w / painted->w;
  if (!base || base->w != painted->w || base->h != painted->h || k < 1 || hd->w != painted->w * k ||
      hd->h != painted->h * k)
    return hd;
  SDL_Surface *o = rgba(base), *p = rgba(painted), *out = rgba(hd);
  if (!o || !p || !out) {
    for (SDL_Surface *s : {o, p, out})
      if (s)
        SDL_FreeSurface(s);
    return hd;
  }
  // Each original pixel's change of colour (none: the frame as it is)
  const int w = p->w, h = p->h;
  auto at = [](SDL_Surface *s, int x, int y) { return static_cast<uint8_t *>(s->pixels) + y * s->pitch + x * 4; };
  std::vector<int> delta(static_cast<size_t>(w) * h * 3);
  bool any = false;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint8_t *a = at(o, x, y), *b = at(p, x, y);
      for (int c = 0; c < 3; c++) {
        int d = a[3] ? b[c] - a[c] : 0;
        delta[(static_cast<size_t>(y) * w + x) * 3 + c] = d;
        any = any || d;
      }
    }
  if (any)
    for (int Y = 0; Y < out->h; Y++) {
      int y0 = static_cast<int>(std::floor((Y + 0.5f) / k - 0.5f));
      for (int X = 0; X < out->w; X++) {
        uint8_t *px = at(out, X, Y);
        if (!px[3])
          continue;
        int x0 = static_cast<int>(std::floor((X + 0.5f) / k - 0.5f));
        // (of the four original pixels round it, the one whose own colour
        // it's nearest)
        int best = -1, bestD = 1 << 30;
        for (int dy = 0; dy <= 1; dy++)
          for (int dx = 0; dx <= 1; dx++) {
            int cx = std::clamp(x0 + dx, 0, w - 1), cy = std::clamp(y0 + dy, 0, h - 1);
            const uint8_t *c0 = at(o, cx, cy);
            if (!c0[3])
              continue;
            int d = std::abs(px[0] - c0[0]) + std::abs(px[1] - c0[1]) + std::abs(px[2] - c0[2]);
            if (d < bestD) {
              bestD = d;
              best = cy * w + cx;
            }
          }
        if (best >= 0)
          for (int c = 0; c < 3; c++)
            px[c] = static_cast<uint8_t>(std::clamp(px[c] + delta[static_cast<size_t>(best) * 3 + c], 0, 255));
      }
    }
  SDL_FreeSurface(o);
  SDL_FreeSurface(p);
  if (!any) {
    SDL_FreeSurface(out);
    return hd;
  }
  SDL_FreeSurface(hd);
  return out;
}

int exportFrames(ResourceManager *rm, const std::string &outDir, const std::vector<std::string> &prefixes) {
  int written = 0;
  std::error_code ec;
  for (const std::string &key : rm->listResources("", ".ani")) {
    bool wanted = prefixes.empty() ? key.rfind("ui/", 0) != 0 : false;
    for (const std::string &p : prefixes)
      wanted = wanted || key.rfind(p, 0) == 0;
    if (!wanted)
      continue;
    std::string name = key.substr(0, key.size() - 4);
    Animation *a = rm->getAnimation(name);
    if (!a)
      continue;
    size_t slash = key.find_last_of('/');
    std::string dir = slash == std::string::npos ? "" : key.substr(0, slash);
    // (one folder of files, the path in each name - "animals~lion~m~walk~N_3"
    // - as an upscaler reads a folder, not a tree)
    std::string flat = dir;
    std::replace(flat.begin(), flat.end(), '/', '~');
    fs::create_directories(outDir, ec);
    a->forEachFrame([&](const std::string &direction, int frame, SDL_Surface *s) {
      std::string file = (fs::path(outDir) / (flat + "~" + direction + "_" + std::to_string(frame) + ".png")).string();
      if (fs::exists(file, ec))
        return;
      if (SDL_Surface *c = rgba(s)) {
        bleed(c);
        if (IMG_SavePNG(c, file.c_str()) == 0)
          written++;
        SDL_FreeSurface(c);
      }
    });
  }
  SDL_Log("[HdArt] exported %d frames to %s", written, outDir.c_str());
  return written;
}

void survey(ResourceManager *rm, const std::vector<std::string> &prefixes) {
  // Per top folder (animals, staff, guests, fences, objects...): animations,
  // frames and pixels - what an HD pack of it comes to
  struct Sum {
    long long anims = 0, frames = 0, pixels = 0, biggest = 0;
  };
  std::map<std::string, Sum> sums;
  for (const std::string &key : rm->listResources("", ".ani")) {
    bool wanted = prefixes.empty() ? key.rfind("ui/", 0) != 0 : false;
    for (const std::string &p : prefixes)
      wanted = wanted || key.rfind(p, 0) == 0;
    if (!wanted)
      continue;
    Animation *a = rm->getAnimation(key.substr(0, key.size() - 4));
    if (!a)
      continue;
    std::string top = key.substr(0, key.find('/'));
    Sum &s = sums[top];
    s.anims++;
    a->forEachFrame([&](const std::string &, int, SDL_Surface *f) {
      long long px = static_cast<long long>(f->w) * f->h;
      s.frames++;
      s.pixels += px;
      s.biggest = std::max(s.biggest, px);
    });
    delete a;
  }
  Sum all;
  for (const auto &[top, s] : sums) {
    SDL_Log("[HdArt] survey %-10s %6lld animations %8lld frames %11lld pixels (biggest frame %lld)", top.c_str(),
            s.anims, s.frames, s.pixels, s.biggest);
    all.anims += s.anims;
    all.frames += s.frames;
    all.pixels += s.pixels;
  }
  SDL_Log("[HdArt] survey all        %6lld animations %8lld frames %11lld pixels", all.anims, all.frames, all.pixels);
}

int finishFrames(const std::string &framesDir, const std::string &upDir, const std::string &outDir) {
  std::error_code ec;
  std::vector<fs::path> files;
  for (const fs::directory_entry &e : fs::directory_iterator(upDir, ec))
    if (e.is_regular_file() && e.path().extension() == ".png")
      files.push_back(e.path());
  // (each frame on its own: one per CPU core at a time - reading, fixing and
  // writing tens of thousands of big PNGs one by one took many minutes)
  std::atomic<size_t> next{0};
  std::atomic<int> done{0};
  std::mutex dirs;
  auto work = [&] {
    for (size_t i; (i = next++) < files.size();) {
      std::error_code wec;
      const fs::path &up = files[i];
      // (exportFrames' flat name, back to its path)
    fs::path orig = fs::path(framesDir) / up.filename();
    std::string relName = up.filename().string();
    std::replace(relName.begin(), relName.end(), '~', '/');
    fs::path rel = relName;
    SDL_Surface *o = IMG_Load(orig.string().c_str());
    SDL_Surface *u = IMG_Load(up.string().c_str());
    SDL_Surface *oc = o ? rgba(o) : nullptr, *uc = u ? rgba(u) : nullptr;
    if (o)
      SDL_FreeSurface(o);
    if (u)
      SDL_FreeSurface(u);
    int k = oc && uc && oc->w > 0 ? uc->w / oc->w : 0;
    if (k >= 2 && uc->w == oc->w * k && uc->h == oc->h * k) {
      // The outline: the original's alpha, scaled up smoothly (bilinear,
      // then sharpened to a soft step a pixel wide at the new size) - the
      // upscaler's own alpha wandered and haloed
      const int W = uc->w, H = uc->h, w = oc->w, h = oc->h;
      auto alphaAt = [&](int x, int y) {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return static_cast<float>(static_cast<uint8_t *>(oc->pixels)[y * oc->pitch + x * 4 + 3]);
      };
      for (int y = 0; y < H; y++) {
        uint8_t *row = static_cast<uint8_t *>(uc->pixels) + y * uc->pitch;
        float fy = (y + 0.5f) / k - 0.5f;
        int y0 = static_cast<int>(std::floor(fy));
        float ty = fy - y0;
        for (int x = 0; x < W; x++) {
          float fx = (x + 0.5f) / k - 0.5f;
          int x0 = static_cast<int>(std::floor(fx));
          float tx = fx - x0;
          float a = (alphaAt(x0, y0) * (1 - tx) + alphaAt(x0 + 1, y0) * tx) * (1 - ty) +
                    (alphaAt(x0, y0 + 1) * (1 - tx) + alphaAt(x0 + 1, y0 + 1) * tx) * ty;
          // (a step centred on half: crisp at the new size, not a blur)
          float v = std::clamp((a / 255.0f - 0.5f) * 2.5f + 0.5f, 0.0f, 1.0f);
          row[x * 4 + 3] = static_cast<uint8_t>(std::lround(v * 255.0f));
        }
      }
      // The texture: the original's own pixels put back (back-projection).
      // The upscaler smooths away the fine speckle and dots that make the
      // fur and markings; shrunk back down, the HD frame should come out
      // as the original, pixel for pixel. So it's shrunk (each k x k block
      // averaged), compared with the original, and the difference spread
      // back over the block's pixels (smoothly, bilinear) - a few times
      // over: the upscaler's clean shapes and edges stay, every texture
      // pixel's detail comes back
      {
        std::vector<float> hd(static_cast<size_t>(W) * H * 3), err(static_cast<size_t>(w) * h * 3);
        for (int y = 0; y < H; y++) {
          const uint8_t *row = static_cast<uint8_t *>(uc->pixels) + y * uc->pitch;
          for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++)
              hd[(static_cast<size_t>(y) * W + x) * 3 + c] = row[x * 4 + c];
        }
        auto origAt = [&](int x, int y, int c) {
          return static_cast<float>(static_cast<uint8_t *>(oc->pixels)[y * oc->pitch + x * 4 + c]);
        };
        auto errAt = [&](int x, int y, int c) {
          x = std::clamp(x, 0, w - 1);
          y = std::clamp(y, 0, h - 1);
          return err[(static_cast<size_t>(y) * w + x) * 3 + c];
        };
        for (int pass = 0; pass < 5; pass++) {
          for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
              bool solid = alphaAt(x, y) >= 128;
              for (int c = 0; c < 3; c++) {
                float sum = 0;
                for (int dy = 0; dy < k; dy++)
                  for (int dx = 0; dx < k; dx++)
                    sum += hd[(static_cast<size_t>(y * k + dy) * W + (x * k + dx)) * 3 + c];
                err[(static_cast<size_t>(y) * w + x) * 3 + c] = solid ? origAt(x, y, c) - sum / (k * k) : 0.0f;
              }
            }
          for (int y = 0; y < H; y++) {
            float fy = (y + 0.5f) / k - 0.5f;
            int y0 = static_cast<int>(std::floor(fy));
            float ty = fy - y0;
            for (int x = 0; x < W; x++) {
              float fx = (x + 0.5f) / k - 0.5f;
              int x0 = static_cast<int>(std::floor(fx));
              float tx = fx - x0;
              for (int c = 0; c < 3; c++) {
                float e = (errAt(x0, y0, c) * (1 - tx) + errAt(x0 + 1, y0, c) * tx) * (1 - ty) +
                          (errAt(x0, y0 + 1, c) * (1 - tx) + errAt(x0 + 1, y0 + 1, c) * tx) * ty;
                float &v = hd[(static_cast<size_t>(y) * W + x) * 3 + c];
                v = std::clamp(v + e, 0.0f, 255.0f);
              }
            }
          }
        }
        for (int y = 0; y < H; y++) {
          uint8_t *row = static_cast<uint8_t *>(uc->pixels) + y * uc->pitch;
          for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++)
              row[x * 4 + c] = static_cast<uint8_t>(std::lround(hd[(static_cast<size_t>(y) * W + x) * 3 + c]));
        }
      }
      fs::path out = fs::path(outDir) / rel;
      {
        std::lock_guard<std::mutex> lock(dirs);
        fs::create_directories(out.parent_path(), wec);
      }
      if (IMG_SavePNG(uc, out.string().c_str()) == 0)
        done++;
    } else if (oc && uc) {
      SDL_Log("[HdArt] %s: %dx%d is not a whole multiple of %dx%d - left out", rel.string().c_str(), uc->w, uc->h,
              oc->w, oc->h);
    }
    if (oc)
      SDL_FreeSurface(oc);
    if (uc)
      SDL_FreeSurface(uc);
    }
  };
  std::vector<std::thread> workers;
  unsigned n = std::max(1u, std::thread::hardware_concurrency());
  for (unsigned t = 0; t < n; t++)
    workers.emplace_back(work);
  for (std::thread &t : workers)
    t.join();
  SDL_Log("[HdArt] finished %d frames into %s", done.load(), outDir.c_str());
  return done.load();
}

} // namespace HdArt
