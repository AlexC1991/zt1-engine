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
#include <fstream>
#include <set>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include "LightingAssets.hpp"

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

// addition: palette-reference animations are owned by this cache. Release
// them while their ResourceManager and SDL renderer are still alive, before
// the resource inventory is reset or destroyed.
void setResources(ResourceManager *rm) {
  g_ownPalette.clear();g_ownGeneration=-1;g_rm=rm;
}

// addition: a baked material belongs to the same enabled root as its colour
// frame. A lower-priority pack must not light a different replacement's art.
SDL_Surface *loadEmission(const std::string &path,const std::string &material) {
  ensure();std::error_code ec;
  for(const auto &root:g_roots)if(fs::exists(root+path+".png",ec)) {
    const auto file=root+material+".png";return fs::exists(file,ec)?IMG_Load(file.c_str()):nullptr;
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
      auto *image=SDL_CreateRGBSurfaceWithFormat(0,source->w,source->h,32,SDL_PIXELFORMAT_RGBA32);if(!target || !image){SDL_DestroyTexture(target);SDL_FreeSurface(image);throw std::runtime_error("material seed allocation failed");}
      auto *previous=SDL_GetRenderTarget(exportRenderer);SDL_SetRenderTarget(exportRenderer,target);SDL_RenderSetViewport(exportRenderer,nullptr);SDL_RenderSetClipRect(exportRenderer,nullptr);SDL_RenderSetScale(exportRenderer,1,1);SDL_SetRenderDrawColor(exportRenderer,0,0,0,0);SDL_RenderClear(exportRenderer);
      for(const auto &p:patches){SDL_BlendMode blend;SDL_GetTextureBlendMode(p.texture,&blend);SDL_SetTextureBlendMode(p.texture,SDL_BLENDMODE_NONE);SDL_RenderCopy(exportRenderer,p.texture,nullptr,&p.rect);SDL_SetTextureBlendMode(p.texture,blend);}
      const std::string seed=std::to_string(id)+"@"+std::to_string(side)+"@"+flat;
      fs::create_directories(fs::path(out)/"material-seeds");const bool ok=SDL_RenderReadPixels(exportRenderer,nullptr,SDL_PIXELFORMAT_RGBA32,image->pixels,image->pitch)==0 && IMG_SavePNG(image,(fs::path(out)/"material-seeds"/seed).string().c_str())==0;
      SDL_SetRenderTarget(exportRenderer,previous);SDL_DestroyTexture(target);SDL_FreeSurface(image);if(!ok)throw std::runtime_error("could not write material seed");
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
        if (IMG_SavePNG(c, file.c_str()) == 0){
          exportMaterialSeeds(*a,direction,frame,s,fs::path(file).filename().string(),outDir);
          written++;
        }else {SDL_FreeSurface(c);delete a;throw std::runtime_error("could not export HD frame");}
        SDL_FreeSurface(c);
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
