#include "ShaderMods.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "ArtScaler.hpp"
#include "GpuFsr.hpp"
#include "MiniJson.hpp"
#include "Mods.hpp"
#include "Settings.hpp"

namespace fs = std::filesystem;

namespace Shaders {

namespace {
std::vector<Shader> g_shaders;
int g_generation = 0;
bool g_read = false;

void ensure() {
  if (!g_read)
    refresh();
}
std::string readFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
std::string fmt(float v) {
  char b[32];
  std::snprintf(b, sizeof b, "%g", v);
  return b;
}
// (compiled the first time it's used: needs the graphics card's context)
bool ready(Shader &s) {
  if (s.program || s.failed)
    return s.program != 0;
  s.program = GpuFsr::compileShader(readFile(s.file), s.log);
  if (!s.program) {
    s.failed = true;
    SDL_Log("[Shaders] %s didn't compile: %s", s.key.c_str(), s.log.c_str());
  } else {
    SDL_Log("[Shaders] %s ready (%s)", s.key.c_str(), s.stage.c_str());
  }
  return s.program != 0;
}
std::vector<std::pair<std::string, float>> uniformsOf(const Shader &s) {
  std::vector<std::pair<std::string, float>> u;
  for (const Setting &v : s.settings)
    u.push_back({v.name, v.value});
  for (const auto &mv : s.modValues)
    if (std::none_of(s.settings.begin(), s.settings.end(), [&](const Setting &v) { return v.name == mv.first; }))
      u.push_back(mv);
  return u;
}
} // namespace

void refresh() {
  g_read = true;
  g_shaders.clear();
  std::error_code ec;
  // (each mod's own settings, for its shaders' uniforms)
  std::map<std::string, std::vector<std::pair<std::string, float>>> modValues;
  for (const Mods::Mod &m : Mods::list())
    for (const Mods::Setting &t : m.settings)
      modValues[m.folder].push_back({t.name, t.value});
  for (const std::string &modPath : Mods::enabledPaths()) {
    fs::path dir = fs::path(modPath) / "shaders";
    if (!fs::is_directory(dir, ec))
      continue;
    for (const fs::directory_entry &e : fs::directory_iterator(dir, ec)) {
      if (e.path().extension() != ".json")
        continue;
      Json j = Json::parse(readFile(e.path()));
      Shader s;
      s.mod = fs::path(modPath).filename().string();
      s.key = s.mod + "/" + e.path().stem().string();
      s.modValues = modValues[s.mod];
      s.name = j["name"].str(e.path().stem().string());
      s.about = j["about"].str();
      s.stage = j["stage"].str("art");
      s.file = (dir / j["shader"].str(e.path().stem().string() + ".frag")).string();
      // (on unless its file says "on": false - then as the player left it)
      const bool standardOn = j["on"].kind == Json::Kind::Null || j["on"].truthy();
      s.on = Settings::get("Shaders", s.key, standardOn ? "1" : "0") != "0";
      for (size_t i = 0; i < j["settings"].size(); i++) {
        const Json &v = j["settings"][i];
        Setting t;
        t.name = v["name"].str();
        if (t.name.empty())
          continue;
        t.label = v["label"].str(t.name);
        t.about = v["about"].str();
        t.min = static_cast<float>(v["min"].num(0));
        t.max = static_cast<float>(v["max"].num(1));
        t.standard = static_cast<float>(v["value"].num(t.min));
        std::string kept = Settings::get("Shaders", s.key + "." + t.name, "");
        t.value = kept.empty() ? t.standard : std::clamp(static_cast<float>(std::atof(kept.c_str())), t.min, t.max);
        s.settings.push_back(t);
      }
      g_shaders.push_back(s);
    }
  }
  // (art shaders first - what most change - then by name)
  std::sort(g_shaders.begin(), g_shaders.end(), [](const Shader &a, const Shader &b) {
    return a.stage != b.stage ? a.stage == "art" : a.key < b.key;
  });
  g_generation++;
  SDL_Log("[Shaders] %d shader(s) in the mods switched on", static_cast<int>(g_shaders.size()));
}

std::vector<Shader> &all() {
  ensure();
  return g_shaders;
}

bool artActive() {
  ensure();
  if (!GpuFsr::available())
    return false;
  for (const Shader &s : g_shaders)
    if (s.on && s.stage == "art" && !s.failed)
      return true;
  return false;
}

int generation() {
  ensure();
  return g_generation;
}

void setOn(const std::string &key, bool on) {
  for (Shader &s : all())
    if (s.key == key && s.on != on) {
      s.on = on;
      Settings::set("Shaders", key, on ? "1" : "0");
      g_generation++;
    }
}

void setValue(const std::string &key, const std::string &setting, float value) {
  for (Shader &s : all())
    if (s.key == key)
      for (Setting &t : s.settings)
        if (t.name == setting) {
          value = std::clamp(value, t.min, t.max);
          if (value == t.value)
            return;
          t.value = value;
          Settings::set("Shaders", key + "." + setting, fmt(value));
          if (s.stage == "art")
            g_generation++;
        }
}

void setModValue(const std::string &mod, const std::string &setting, float value) {
  for (Shader &s : all())
    if (s.mod == mod)
      for (auto &mv : s.modValues)
        if (mv.first == setting && mv.second != value) {
          mv.second = value;
          if (s.stage == "art")
            g_generation++;
        }
}

SDL_Surface *normals(SDL_Surface *frame) {
  if (!frame || frame->format->format != SDL_PIXELFORMAT_RGBA32)
    return nullptr;
  const int w = frame->w, h = frame->h;
  auto px = [&](int x, int y) {
    return static_cast<const Uint8 *>(frame->pixels) + std::clamp(y, 0, h - 1) * frame->pitch + std::clamp(x, 0, w - 1) * 4;
  };
  // How far each pixel is inside the outline (two passes of a 3x3 chamfer:
  // 1 along, 1.414 across)
  const float big = 1e6f;
  std::vector<float> d(static_cast<size_t>(w) * h);
  // (the body only: clear pixels and its shadow on the ground - near black,
  // solid in the art - are outside it)
  auto body = [&](int x, int y) {
    const Uint8 *p = px(x, y);
    return p[3] >= 128 && p[0] + p[1] + p[2] > 3 * 16;
  };
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      d[static_cast<size_t>(y) * w + x] = body(x, y) ? big : 0.0f;
  auto at = [&](int x, int y) -> float { return x < 0 || y < 0 || x >= w || y >= h ? 0.0f : d[static_cast<size_t>(y) * w + x]; };
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      float &v = d[static_cast<size_t>(y) * w + x];
      if (v == 0)
        continue;
      v = std::min({v, at(x - 1, y) + 1, at(x, y - 1) + 1, at(x - 1, y - 1) + 1.414f, at(x + 1, y - 1) + 1.414f});
    }
  float deepest = 0;
  for (int y = h - 1; y >= 0; y--)
    for (int x = w - 1; x >= 0; x--) {
      float &v = d[static_cast<size_t>(y) * w + x];
      if (v == 0)
        continue;
      v = std::min({v, at(x + 1, y) + 1, at(x, y + 1) + 1, at(x + 1, y + 1) + 1.414f, at(x - 1, y + 1) + 1.414f});
      deepest = std::max(deepest, v);
    }
  // Its height: a rounded shape - a quarter circle up from the outline to
  // its thickest - and its shading's detail on top (lighter, nearer)
  const float R = std::max(3.0f, deepest);
  std::vector<float> hgt(d.size());
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      size_t i = static_cast<size_t>(y) * w + x;
      if (d[i] == 0) {
        hgt[i] = 0;
        continue;
      }
      float t = std::min(d[i], R) / R;
      const Uint8 *p = px(x, y);
      float lum = (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f) / 255.0f;
      hgt[i] = std::sqrt(1.0f - (1.0f - t) * (1.0f - t)) * R * 0.75f + lum * R * 0.12f;
    }
  SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
  if (!out)
    return nullptr;
  auto hAt = [&](int x, int y) { return hgt[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
  for (int y = 0; y < h; y++) {
    Uint8 *row = static_cast<Uint8 *>(out->pixels) + y * out->pitch;
    for (int x = 0; x < w; x++) {
      // (its slope over two pixels each way: smoother than one)
      float gx = (hAt(x + 2, y) - hAt(x - 2, y)) * 0.25f, gy = (hAt(x, y + 2) - hAt(x, y - 2)) * 0.25f;
      // (the picture's rows run down: up is -y)
      float nx = -gx, ny = gy, nz = 1.0f, l = std::sqrt(nx * nx + ny * ny + nz * nz);
      row[x * 4] = static_cast<Uint8>(std::lround((nx / l * 0.5f + 0.5f) * 255));
      row[x * 4 + 1] = static_cast<Uint8>(std::lround((ny / l * 0.5f + 0.5f) * 255));
      row[x * 4 + 2] = static_cast<Uint8>(std::lround((nz / l * 0.5f + 0.5f) * 255));
      size_t i = static_cast<size_t>(y) * w + x;
      row[x * 4 + 3] = static_cast<Uint8>(std::lround(std::clamp(std::min(d[i], R) / R, 0.0f, 1.0f) * 255));
    }
  }
  return out;
}

SDL_Texture *applyArt(SDL_Renderer *renderer, SDL_Surface *frame, int kind, int w, int h) {
  ensure();
  if (!renderer || !frame || !artActive())
    return nullptr;
  SDL_Surface *n = normals(frame);
  if (!n)
    return nullptr;
  SDL_Texture *color = SDL_CreateTextureFromSurface(renderer, frame);
  SDL_Texture *aux = SDL_CreateTextureFromSurface(renderer, n);
  SDL_FreeSurface(n);
  SDL_Texture *result = nullptr;
  for (Shader &s : g_shaders) {
    if (!s.on || s.stage != "art" || !ready(s) || !color || !aux)
      continue;
    SDL_Texture *out =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, frame->w, frame->h);
    if (!out)
      break;
    auto u = uniformsOf(s);
    u.push_back({"uKind", static_cast<float>(kind)});
    if (!GpuFsr::artPass(renderer, s.program, result ? result : color, aux, out, frame->w, frame->h, u)) {
      SDL_DestroyTexture(out);
      break;
    }
    // (the next shader works on this one's picture)
    if (result)
      SDL_DestroyTexture(result);
    result = out;
  }
  if (color)
    SDL_DestroyTexture(color);
  if (aux)
    SDL_DestroyTexture(aux);
  if (result) {
    SDL_SetTextureBlendMode(result, SDL_BLENDMODE_BLEND);
    ArtScaler::markScale(result, w > 0 ? frame->w / w : 1);
  }
  (void)h;
  return result;
}

void applyScreen(SDL_Renderer *renderer) {
  ensure();
  for (Shader &s : g_shaders)
    if (s.on && s.stage == "screen" && ready(s))
      GpuFsr::screenPass(renderer, s.program, uniformsOf(s));
}

} // namespace Shaders
