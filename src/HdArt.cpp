#include "HdArt.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <list>
#include <shared_mutex>
#include <tuple>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <fstream>
#include <set>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include "LightingAssets.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <SDL3_image/SDL_image.h>

#include "Animation.hpp"
#include "ArtScaler.hpp"
#include "GpuCompressed.hpp"
#include "Mods.hpp"
#include "Settings.hpp"
#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "ZtdFile.hpp"

namespace fs = std::filesystem;

namespace HdArt {
// (a frame's blocks for the graphics card, kept with it: a surface property)
constexpr const char *kBlocks = "zt1.hdart.blocks";

namespace {
std::vector<std::string> g_roots;
std::vector<std::string> g_hdMods; // (the mods among them, by folder: their setting)
// (Graphics > Art Sharpness, as RCAS stops - 0 the sharpest, 2 light - read
// at each refresh, used by the loader's threads)
std::atomic<float> g_sharpStops{0.2f};
// (the first HD art mod's graphics card setting, read at each refresh: a
// setting read is a read of zoo.ini from the disk - asked on every draw,
// it was thousands a frame)
std::atomic<int> g_compression{1};
// ours: each root's packed art folders ("animals/lion/") and their archives
std::vector<std::map<std::string, std::string>> g_packed;
int g_generation = 0;
std::atomic<bool> g_read{false};
// ours: the roots and their archives - read by load on the loader's
// threads, changed by refresh on the main thread
std::shared_mutex g_rootsLock;

// The art folder a frame or light map belongs to: its first two levels, or
// for a light map those after _lighting/<fingerprint>/<profile>/<view>/
std::string folderOf(const std::string &path) {
  std::vector<std::string> parts;
  std::stringstream in(path);
  for (std::string part; std::getline(in, part, '/');)
    parts.push_back(part);
  const size_t at = !parts.empty() && parts[0] == "_lighting" ? 4 : 0;
  return parts.size() >= at + 3 ? Utils::string_to_lower(parts[at] + "/" + parts[at + 1] + "/") : std::string();
}
// The archive in root i holding this file, if it's packed there
const std::string *packedIn(size_t i, const std::string &file) {
  if (i >= g_packed.size() || g_packed[i].empty())
    return nullptr;
  auto at = g_packed[i].find(folderOf(file));
  return at != g_packed[i].end() && ZtdFile::hasFile(at->second, file) ? &at->second : nullptr;
}
SDL_Surface *packedImage(const std::string &archive, const std::string &file) {
  int size = 0;
  void *data = ZtdFile::tryFileContent(archive, file, &size);
  if (!data)
    return nullptr;
  SDL_Surface *s = IMG_Load_IO(SDL_IOFromConstMem(data, size), 1);
  free(data);
  return s;
}

void ensure() {
  if (!g_read)
    refresh();
}

// A frame as RGBA32 (a new surface the caller frees)
SDL_Surface *rgba(SDL_Surface *s) { return ZT_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0); }

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

void dropLoaderWork();
void refresh() {
  dropLoaderWork(); // (frames from the old roots: not wanted)
  std::unique_lock<std::shared_mutex> lock(g_rootsLock);
  g_read = true;
  g_roots.clear();
  g_hdMods.clear();
  std::error_code ec;
  for (const std::string &m : Mods::enabledPaths()) {
    fs::path hd = fs::path(m) / "hd";
    if (fs::is_directory(hd, ec)) {
      g_roots.push_back(hd.string() + "/");
      g_hdMods.push_back(fs::path(m).filename().string());
    }
  }
  if (fs::is_directory("hd", ec))
    g_roots.push_back("hd/");
  g_packed.assign(g_roots.size(), {});
  for (size_t i = 0; i < g_roots.size(); i++)
    for (const fs::directory_entry &top : fs::directory_iterator(g_roots[i], ec))
      if (top.is_directory(ec))
        for (const fs::directory_entry &z : fs::directory_iterator(top.path(), ec)) {
          std::string ext = Utils::string_to_lower(z.path().extension().string());
          if (ext == ".ztd" && z.is_regular_file(ec))
            g_packed[i][Utils::string_to_lower(top.path().filename().string() + "/" + z.path().stem().string() + "/")] =
                z.path().string();
        }
  g_sharpStops = std::clamp(static_cast<float>(std::atof(Settings::get("user", "artsharpness", "0.2").c_str())), 0.0f, 2.0f);
  if (std::getenv("ZT_HD_NO_SHARPEN")) // (the checks: compared without it)
    g_sharpStops = -1.0f;
  g_compression = g_hdMods.empty() ? 1 : Mods::hdCompression(g_hdMods.front());
  g_generation++;
  SDL_Log("[HdArt] %d HD art folder(s) at work", static_cast<int>(g_roots.size()));
}

bool active() {
  ensure();
  return !g_roots.empty();
}

int compression() {
  ensure();
  return g_compression;
}

int generation() {
  ensure();
  return g_generation;
}

namespace {
std::atomic<int> g_found{0}, g_missed{0};
std::mutex g_missedMutex; // (loadMany's threads)
std::string g_lastMissed;
} // namespace

SDL_Surface *load(const std::string &path) {
  ensure();
  std::shared_lock<std::shared_mutex> lock(g_rootsLock);
  std::error_code ec;
  for (size_t i = 0; i < g_roots.size(); i++) {
    std::string file = g_roots[i] + path + ".png";
    if (!fs::exists(file, ec)) {
      if (const std::string *archive = packedIn(i, path + ".png")) {
        SDL_Surface *s = packedImage(*archive, path + ".png");
        if (!s)
          SDL_Log("[HdArt] could not load %s from %s", (path + ".png").c_str(), archive->c_str());
        g_found++;
        return s;
      }
      continue;
    }
    SDL_Surface *s = IMG_Load(file.c_str());
    if (!s)
      SDL_Log("[HdArt] could not load %s: %s", file.c_str(), SDL_GetError());
    g_found++;
    return s;
  }
  if (!g_roots.empty()) {
    g_missed++;
    std::lock_guard<std::mutex> lock(g_missedMutex);
    g_lastMissed = path;
  }
  return nullptr;
}

namespace {
// One frame for the loader: read, and halved for the 2x level
SDL_Surface *loadAt(const std::string &path, int level) {
  SDL_Surface *s = load(path);
  if (s && level == 2) {
    SDL_Surface *half = halve(s);
    SDL_DestroySurface(s);
    s = half;
  }
  // ours: Graphics > Art Sharpness on HD art too, at the level it's drawn
  // at (after halving: where the softness is) - FSR's RCAS, as the game's
  // own art gets it
  if (const float stops = g_sharpStops; s && stops >= 0.0f) {
    if (s->format != SDL_PIXELFORMAT_RGBA32) {
      SDL_Surface *c = ZT_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
      SDL_DestroySurface(s);
      s = c;
    }
    ArtScaler::sharpen(s, stops);
  }
  return s;
}
} // namespace

std::vector<SDL_Surface *> loadMany(const std::vector<std::string> &paths, int level) {
  std::vector<SDL_Surface *> out(paths.size(), nullptr);
  ensure(); // (the roots read here, not on the threads)
  if (g_roots.empty() || paths.empty())
    return out;
  // (SDL3_image needs no setting up before its decoders are used)
  // (the archives' reads take turns - ZtdFile's lock - the decoding doesn't)
  const unsigned n = std::min<unsigned>(std::max(1u, std::thread::hardware_concurrency()), unsigned(paths.size()));
  if (n <= 1) {
    for (size_t i = 0; i < paths.size(); i++)
      out[i] = loadAt(paths[i], level);
    return out;
  }
  std::atomic<size_t> next{0};
  std::vector<std::thread> workers;
  for (unsigned t = 0; t < n; t++)
    workers.emplace_back([&] {
      for (size_t i; (i = next++) < paths.size();)
        out[i] = loadAt(paths[i], level);
    });
  for (std::thread &w : workers)
    w.join();
  return out;
}

SDL_Surface *halve(SDL_Surface *s) {
  SDL_Surface *in = s->format == SDL_PIXELFORMAT_RGBA32 ? s : rgba(s);
  if (!in)
    return nullptr;
  const int w = in->w / 2, h = in->h / 2;
  SDL_Surface *out = w > 0 && h > 0 ? ZT_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32) : nullptr;
  if (out) {
    SDL_LockSurface(in);
    for (int y = 0; y < h; y++) {
      const uint8_t *r0 = static_cast<const uint8_t *>(in->pixels) + static_cast<size_t>(2 * y) * in->pitch;
      const uint8_t *r1 = r0 + in->pitch;
      uint8_t *o = static_cast<uint8_t *>(out->pixels) + static_cast<size_t>(y) * out->pitch;
      for (int x = 0; x < w; x++, o += 4) {
        const uint8_t *q[4] = {r0 + 8 * x, r0 + 8 * x + 4, r1 + 8 * x, r1 + 8 * x + 4};
        // (by alpha: a clear pixel's colour doesn't darken the edge; all
        // four clear, their plain average, for the smooth scaling's edge)
        int a = 0, c[3] = {0, 0, 0}, plain[3] = {0, 0, 0};
        for (const uint8_t *p : q) {
          a += p[3];
          for (int k = 0; k < 3; k++) {
            c[k] += p[k] * p[3];
            plain[k] += p[k];
          }
        }
        for (int k = 0; k < 3; k++)
          o[k] = static_cast<uint8_t>(a ? (c[k] + a / 2) / a : plain[k] / 4);
        o[3] = static_cast<uint8_t>((a + 2) / 4);
      }
    }
    SDL_UnlockSurface(in);
  }
  if (in != s)
    SDL_DestroySurface(in);
  return out;
}

namespace {
// ours: the background loader (requestFrames)
using LoadKey = std::tuple<uint64_t, std::string, int>; // owner, its key, the level
// (the loader used yet, and gone: an animation freed after it, at exit,
// doesn't reach for it)
std::atomic<bool> g_loaderUsed{false}, g_loaderGone{false};
struct LoadJob {
  LoadKey key;
  std::vector<std::string> paths;
  uint32_t wanted = 0;
  int encode = 0; // (requestFrames)
};
struct Loader {
  std::mutex m;
  std::condition_variable cv;
  // (the jobs waiting, in the order they were last asked for - the latest
  // at the back, where the workers take from - and each one's place in it:
  // asked again, a job moves to the back at once. Scanned on every ask, a
  // long queue - a slow loader in a busy zoo - cost the game's thread more
  // than drawing)
  std::list<LoadJob> queue;
  std::map<LoadKey, std::list<LoadJob>::iterator> queued;
  std::map<LoadKey, uint32_t> pending;                                        // queued or being made
  std::map<LoadKey, std::pair<std::vector<SDL_Surface *>, uint32_t>> ready; // and when made
  std::vector<std::thread> workers;
  bool stop = false;
  uint32_t now = 0; // (frames: loaderTick)
  static void free(std::vector<SDL_Surface *> &frames) {
    for (SDL_Surface *f : frames)
      freeFrame(f);
    frames.clear();
  }
  void start() { // (with m held)
    if (!workers.empty())
      return;
    // (one a physical core but one - the game's thread keeps a core of its
    // own: on a 4-core CPU with 8 threads, 6 workers made it share one)
    const unsigned cores = std::thread::hardware_concurrency();
    const unsigned n = std::clamp(cores / 2 > 1 ? cores / 2 - 1 : 1u, 1u, 6u);
    for (unsigned i = 0; i < n; i++)
      workers.emplace_back([this] {
        // (below the game's own thread: decoding, sharpening and encoding
        // for the graphics card only use what it leaves)
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        run();
      });
  }
  void run() {
    for (;;) {
      LoadJob job;
      {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return stop || !queue.empty(); });
        if (stop)
          return;
        job = std::move(queue.back());
        queue.pop_back();
        queued.erase(job.key);
      }
      std::vector<SDL_Surface *> frames;
      frames.reserve(job.paths.size());
      for (const std::string &path : job.paths)
        frames.push_back(loadAt(path, std::get<2>(job.key)));
      // (encoded for the graphics card here too, off the game's thread: the
      // game only hands the blocks over - GpuCompressed::upload)
      if (job.encode > 0)
        for (SDL_Surface *f : frames)
          if (f) {
            auto *b = new GpuCompressed::Blocks;
            if (GpuCompressed::encode(f, job.encode == 2, *b, 1))
              SDL_SetPointerProperty(SDL_GetSurfaceProperties(f), kBlocks, b); // (SDL3: a surface's own property)
            else
              delete b;
          }
      std::lock_guard<std::mutex> lock(m);
      auto it = pending.find(job.key);
      if (it == pending.end()) { // (cancelled meanwhile)
        free(frames);
        continue;
      }
      pending.erase(it);
      ready[job.key] = {std::move(frames), now};
    }
  }
  ~Loader() {
    g_loaderGone = true;
    {
      std::lock_guard<std::mutex> lock(m);
      stop = true;
    }
    cv.notify_all();
    for (std::thread &t : workers)
      t.join();
    for (auto &[key, made] : ready)
      free(made.first);
  }
};
Loader &loader() {
  static Loader l;
  return l;
}
} // namespace

void dropLoaderWork() {
  if (!g_loaderUsed || g_loaderGone)
    return;
  Loader &l = loader();
  std::lock_guard<std::mutex> lock(l.m);
  l.queue.clear();
  l.queued.clear();
  l.pending.clear(); // (what's being made now is freed when it's done)
  for (auto &[key, made] : l.ready)
    Loader::free(made.first);
  l.ready.clear();
}

const GpuCompressed::Blocks *encodedOf(SDL_Surface *frame) {
  return frame ? static_cast<const GpuCompressed::Blocks *>(SDL_GetPointerProperty(SDL_GetSurfaceProperties(frame), kBlocks, nullptr))
               : nullptr;
}

void freeFrame(SDL_Surface *frame) {
  if (!frame)
    return;
  const SDL_PropertiesID props = SDL_GetSurfaceProperties(frame);
  delete static_cast<GpuCompressed::Blocks *>(SDL_GetPointerProperty(props, kBlocks, nullptr));
  SDL_ClearProperty(props, kBlocks);
  SDL_DestroySurface(frame);
}

void requestFrames(uint64_t owner, const std::string &key, int level, const std::vector<std::string> &paths,
                   int encode) {
  ensure(); // (the roots read here, on the main thread)
  if (g_loaderGone)
    return;
  g_loaderUsed = true;
  Loader &l = loader();
  std::lock_guard<std::mutex> lock(l.m);
  l.start();
  const LoadKey k{owner, key, level};
  if (l.ready.count(k))
    return;
  if (auto it = l.pending.find(k); it != l.pending.end()) {
    it->second = l.now;
    // (still waiting: to the back, the latest asked for)
    if (auto q = l.queued.find(k); q != l.queued.end()) {
      q->second->wanted = l.now;
      l.queue.splice(l.queue.end(), l.queue, q->second);
    }
    return;
  }
  l.pending[k] = l.now;
  l.queue.push_back({k, paths, l.now, encode});
  l.queued[k] = std::prev(l.queue.end());
  l.cv.notify_one();
}

bool framesReady(uint64_t owner, const std::string &key, int level) {
  if (!g_loaderUsed || g_loaderGone)
    return false;
  Loader &l = loader();
  std::lock_guard<std::mutex> lock(l.m);
  return l.ready.count({owner, key, level}) > 0;
}

bool takeFrames(uint64_t owner, const std::string &key, int level, std::vector<SDL_Surface *> &out) {
  if (!g_loaderUsed || g_loaderGone)
    return false;
  Loader &l = loader();
  std::lock_guard<std::mutex> lock(l.m);
  auto it = l.ready.find({owner, key, level});
  if (it == l.ready.end())
    return false;
  out = std::move(it->second.first);
  l.ready.erase(it);
  return true;
}

void cancelFrames(uint64_t owner) {
  if (!g_loaderUsed || g_loaderGone)
    return;
  Loader &l = loader();
  std::lock_guard<std::mutex> lock(l.m);
  l.queue.remove_if([&](const LoadJob &j) {
    if (std::get<0>(j.key) != owner)
      return false;
    l.queued.erase(j.key);
    return true;
  });
  for (auto it = l.pending.begin(); it != l.pending.end();)
    it = std::get<0>(it->first) == owner ? l.pending.erase(it) : std::next(it);
  for (auto it = l.ready.begin(); it != l.ready.end();) {
    if (std::get<0>(it->first) == owner) {
      Loader::free(it->second.first);
      it = l.ready.erase(it);
    } else {
      ++it;
    }
  }
}

void loaderTick() {
  if (!g_loaderUsed || g_loaderGone)
    return;
  Loader &l = loader();
  std::lock_guard<std::mutex> lock(l.m);
  l.now++;
  // (not asked for again in 2 seconds: off the screen, not made - the
  // oldest asked for are at the front)
  while (!l.queue.empty() && l.queue.front().wanted + 120 < l.now) {
    l.pending.erase(l.queue.front().key);
    l.queued.erase(l.queue.front().key);
    l.queue.pop_front();
  }
  // (made but not taken in 10 seconds: freed)
  for (auto it = l.ready.begin(); it != l.ready.end();) {
    if (it->second.second + 600 < l.now) {
      Loader::free(it->second.first);
      it = l.ready.erase(it);
    } else {
      ++it;
    }
  }
}

std::string stats() {
  ensure();
  std::string s = std::to_string(g_found.load()) + " HD frames in use, " + std::to_string(g_missed.load()) + " looked for and not in the mod";
  if (!g_lastMissed.empty())
    s += " (last: " + g_lastMissed + ")";
  for (size_t i = 0; i < g_roots.size(); i++)
    s += "\n  from " + g_roots[i] + (i < g_packed.size() && !g_packed[i].empty() ? " (" + std::to_string(g_packed[i].size()) + " packed folders)" : "");
  return s;
}

namespace {
ResourceManager *g_rm = nullptr;
// Recoloured art in its own palette, by name (dropped when the roots change)
std::map<std::string, std::unique_ptr<Animation>> g_ownPalette;
int g_ownGeneration = -1;
} // namespace

// addition: palette-reference animations are owned by this cache. Release
// them while their ResourceManager and SDL renderer are still alive, before
// the resource inventory is reset or destroyed.
void setResources(ResourceManager *rm) {
  g_ownPalette.clear();g_ownGeneration=-1;g_rm=rm;
}

// addition: a mod item's light map (SpriteLighting): a PNG among the
// resources, read where the mod's other files are (null if there's none)
SDL_Surface *loadModLight(const std::string &resource) {
  if (!g_rm || !g_rm->hasResource(resource + ".png"))
    return nullptr;
  int size = 0;
  void *bytes = g_rm->getFileBytes(resource + ".png", &size);
  if (!bytes)
    return nullptr;
  SDL_Surface *s = size > 0 ? IMG_Load_IO(SDL_IOFromConstMem(bytes, size), 1) : nullptr;
  free(bytes);
  return s;
}

// addition: a baked material belongs to the same enabled root as its colour
// frame. A lower-priority pack must not light a different replacement's art.
SDL_Surface *loadEmission(const std::string &path,const std::string &material) {
  ensure();std::error_code ec;
  for(size_t i=0;i<g_roots.size();i++) {
    const auto &root=g_roots[i];
    if(fs::exists(root+path+".png",ec)) {
      const auto file=root+material+".png";
      if(fs::exists(file,ec))return IMG_Load(file.c_str());
      const std::string *archive=packedIn(i,material+".png");return archive?packedImage(*archive,material+".png"):nullptr;
    }
    // (packed: its light map from the same root's archives)
    if(packedIn(i,path+".png")){const std::string *archive=packedIn(i,material+".png");return archive?packedImage(*archive,material+".png"):nullptr;}
  }return nullptr;
}


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
        SDL_DestroySurface(s);
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
  SDL_DestroySurface(o);
  SDL_DestroySurface(p);
  if (!any) {
    SDL_DestroySurface(out);
    return hd;
  }
  freeFrame(hd); // (its blocks, if any, were of the frame before recolouring)
  return out;
}


// addition: these tool-only layers reuse the reviewed runtime material stencils.
// Colour frames remain independent of time, sunshine and neighbouring lamps.
SDL_Renderer *exportRenderer=nullptr;
std::string materialKey(int kind) {
  const auto *a=LightingAssets::profile(kind);if(!a)return "unknown";
  std::ostringstream data;data<<std::setprecision(9)<<a->id<<a->animated;
  for(const auto &view:a->views){data<<'|';for(const auto &r:view){data<<r.name<<int(r.material)<<int(r.pixels)<<r.stroke<<r.animated<<r.shadowCut;
    for(int c:{r.colour.r,r.colour.g,r.colour.b,r.colour.a})data<<':'<<c;
    for(auto p:r.outline)data<<':'<<p.x<<','<<p.y;}}
  uint64_t hash=1469598103934665603ull;for(unsigned char c:data.str()){hash^=c;hash*=1099511628211ull;}
  std::ostringstream hex;hex<<std::hex<<hash;return hex.str();
}
int writeIndex(ResourceManager *rm,const std::string &out) {
  std::ofstream f(out);auto keys=rm->listResources("",".ani");std::sort(keys.begin(),keys.end());
  for(const auto &key:keys)if(!key.starts_with("ui/"))f<<key<<'\n';return f?0:1;
}
void exportMaterialSeeds(Animation &art,const std::string &direction,int frame,SDL_Surface *source,const std::string &flat,const std::string &out) {
  using namespace LightingAssets;if(!exportRenderer)return;
  const std::string key=art.lightingKey();const auto first=key.find('/'),second=key.find('/',first+1);
  if(first==std::string::npos || second==std::string::npos)return;
  const auto top=key.substr(0,first),item=key.substr(first+1,second-first-1),state=fs::path(key).parent_path().filename().string();
  std::set<int> profiles;if(const auto *a=find(item))profiles.insert(a->id);
  if(top=="animals"){if(int id=fluorescenceProfile("animals/"+item+".ai"))profiles.insert(id);if(int id=actorEffect("animals/"+item+".ai",state,frame))profiles.insert(id);}
  if(top=="guests"){if(int id=actorEffect("guests/"+item,state,frame))profiles.insert(id);}
  if(item=="heli")for(int layer=0;layer<2;++layer)if(int id=aircraftProfile(state=="tiltspin"?"fly":state=="levspin"?"level":state,layer))profiles.insert(id);
  // Native cinema/arcade fixture channels have their own animation profiles.
  if(item=="cinema")profiles.insert(3214);if(item=="dkarcade")profiles.insert(3224);
  std::ofstream manifest(fs::path(out)/"material-seeds.tsv",std::ios::app);
  for(int id:profiles){const auto *a=profile(id);if(!a || (!a->animated && frame!=0))continue;
    for(int side=0;side<8;++side){const auto dir=CompassDirection(side);auto native=art.lightingFrame(dir,frame);if(native.surface!=source)continue;
      const auto &patches=art.emissionPatches(exportRenderer,dir,frame,id);if(patches.empty())continue;
      auto *target=SDL_CreateTexture(exportRenderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,source->w,source->h);
      auto *image=ZT_CreateRGBSurfaceWithFormat(0,source->w,source->h,32,SDL_PIXELFORMAT_RGBA32);if(!target || !image){SDL_DestroyTexture(target);SDL_DestroySurface(image);throw std::runtime_error("material seed allocation failed");}
      auto *previous=SDL_GetRenderTarget(exportRenderer);SDL_SetRenderTarget(exportRenderer,target);SDL_SetRenderViewport(exportRenderer,nullptr);SDL_SetRenderClipRect(exportRenderer,nullptr);SDL_SetRenderScale(exportRenderer,1,1);SDL_SetRenderDrawColor(exportRenderer,0,0,0,0);SDL_RenderClear(exportRenderer);
      for(const auto &p:patches){SDL_BlendMode blend;SDL_GetTextureBlendMode(p.texture,&blend);SDL_SetTextureBlendMode(p.texture,SDL_BLENDMODE_NONE);ZT_RenderCopy(exportRenderer,p.texture,nullptr,&p.rect);SDL_SetTextureBlendMode(p.texture,blend);}
      const std::string seed=std::to_string(id)+"@"+std::to_string(side)+"@"+flat;
      fs::create_directories(fs::path(out)/"material-seeds");const bool ok=ZT_RenderReadPixels(exportRenderer,nullptr,SDL_PIXELFORMAT_RGBA32,image->pixels,image->pitch)==0 && IMG_SavePNG(image,(fs::path(out)/"material-seeds"/seed).string().c_str());
      SDL_SetRenderTarget(exportRenderer,previous);SDL_DestroyTexture(target);SDL_DestroySurface(image);if(!ok)throw std::runtime_error("could not write material seed");
      std::string path=key+"/"+direction+"_"+std::to_string(frame)+".png";
      manifest<<seed<<'\t'<<"_lighting/"<<materialKey(id)<<'/'<<id<<'/'<<side<<'/'<<path<<'\t'<<flat<<'\n';
    }
  }
}

int exportFrames(ResourceManager *rm, const std::string &outDir, const std::vector<std::string> &prefixes) {
  int written = 0;
  const int limit=std::getenv("ZT_HD_EXPORT_LIMIT")?std::max(1,std::atoi(std::getenv("ZT_HD_EXPORT_LIMIT"))):2147483647;
  std::set<std::string> skipped;if(const char *path=std::getenv("ZT_HD_SKIP_LIST")){std::ifstream f(path);std::string line;while(std::getline(f,line)){if(!line.empty() && line.back()=='\r')line.pop_back();skipped.insert(line);}}
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
      if (written>=limit || skipped.count(fs::path(file).filename().string()) || fs::exists(file, ec))
        return;
      if (SDL_Surface *c = rgba(s)) {
        bleed(c);
        if (IMG_SavePNG(c, file.c_str())){
          exportMaterialSeeds(*a,direction,frame,s,fs::path(file).filename().string(),outDir);
          written++;
        }else {SDL_DestroySurface(c);delete a;throw std::runtime_error("could not export HD frame");}
        SDL_DestroySurface(c);
      }
    });
    delete a; // addition: a million-frame batch must release each decoded animation.
    if(written>=limit)break;
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
      SDL_DestroySurface(o);
    if (u)
      SDL_DestroySurface(u);
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
      if (IMG_SavePNG(uc, out.string().c_str()))
        done++;
    } else if (oc && uc) {
      SDL_Log("[HdArt] %s: %dx%d is not a whole multiple of %dx%d - left out", rel.string().c_str(), uc->w, uc->h,
              oc->w, oc->h);
    }
    if (oc)
      SDL_DestroySurface(oc);
    if (uc)
      SDL_DestroySurface(uc);
    }
  };
  std::vector<std::thread> workers;
  unsigned n = std::max(1u, std::thread::hardware_concurrency());
  if(const char *workers=std::getenv("ZT_HD_WORKERS"))n=std::clamp(unsigned(std::max(1,std::atoi(workers))),1u,n);
  for (unsigned t = 0; t < n; t++)
    workers.emplace_back(work);
  for (std::thread &t : workers)
    t.join();
  SDL_Log("[HdArt] finished %d frames into %s", done.load(), outDir.c_str());
  return done.load();
}

} // namespace HdArt

// addition: tool entry points without changing the runtime HD-art interface.
int hdWriteIndex(ResourceManager *rm,const std::string &out){return HdArt::writeIndex(rm,out);}
void hdSetExportRenderer(SDL_Renderer *renderer){HdArt::exportRenderer=renderer;}
