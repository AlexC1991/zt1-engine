#include "GraphicsQuality.hpp"

// addition: see GraphicsQuality.hpp

#include <SDL2/SDL.h>

#include <algorithm>
#include <cwchar>

#include "Settings.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#endif

namespace GraphicsQuality {
namespace {

Settings g_settings;
bool g_loaded = false;
uint64_t g_serial = 1;

// (each preset's lighting: shadows, range, creatures, lamps)
constexpr int kPresets[4][4] = {
    {0, 0, 1, 0}, // Low: small hard-edged shadow maps, the nearest shapes, animals' shadows only
    {1, 1, 2, 1}, // Medium
    {2, 2, 2, 2}, // High: the lighting as it always was
    {3, 2, 2, 2}, // Ultra: sharper shadows (the range as High's: Farthest cost too much for its gain)
};

void ensure() {
  if (!g_loaded)
    load();
}

int clampSetting(int v, int top) { return std::clamp(v, 0, top); }

} // namespace

const Settings &settings() {
  ensure();
  return g_settings;
}

int presetOf(const Settings &s) {
  for (int p = Low; p <= Ultra; p++)
    if (s.shadows == kPresets[p][0] && s.range == kPresets[p][1] && s.creatures == kPresets[p][2] && s.lamps == kPresets[p][3])
      return p;
  return Custom;
}

Settings preset(int which, const Settings &s) {
  Settings r = s;
  if (which < Low || which > Ultra)
    return r;
  r.shadows = kPresets[which][0];
  r.range = kPresets[which][1];
  r.creatures = kPresets[which][2];
  r.lamps = kPresets[which][3];
  r.preset = which;
  return r;
}

void set(const Settings &s) {
  ensure();
  Settings n = s;
  n.shadows = clampSetting(n.shadows, 3);
  n.range = clampSetting(n.range, 3);
  n.creatures = clampSetting(n.creatures, 2);
  n.lamps = clampSetting(n.lamps, 2);
  n.device = clampSetting(n.device, 1);
  n.preset = presetOf(n);
  const Settings &o = g_settings;
  if (n.shadows != o.shadows || n.range != o.range || n.creatures != o.creatures || n.lamps != o.lamps || n.device != o.device)
    ++g_serial;
  g_settings = n;
  ::Settings::setInt("user", "graphicspreset", n.preset);
  ::Settings::setInt("user", "shadowquality", n.shadows);
  ::Settings::setInt("user", "shadowrange", n.range);
  ::Settings::setInt("user", "creatureshadows", n.creatures);
  ::Settings::setInt("user", "lampquality", n.lamps);
  ::Settings::setInt("user", "lightingdevice", n.device);
}

void forThisRun(int which) {
  g_loaded = true;
  if (const char *pick = SDL_getenv("ZT_GRAPHICS_PRESET")) {
    const std::string p = pick;
    which = p == "low" ? Low : p == "medium" ? Medium : p == "ultra" ? Ultra : High;
  }
  g_settings = preset(which, Settings{});
  ++g_serial;
  SDL_Log("[Graphics] this run: the %s preset", which == Low ? "Low" : which == Medium ? "Medium" : which == High ? "High" : "Ultra");
}

void load() {
  g_loaded = true;
  const int saved = ::Settings::getInt("user", "graphicspreset", -1);
  if (saved < 0) {
    // (the first start on this PC: the preset for its graphics, kept)
    std::string why;
    const int pick = recommend(&why);
    SDL_Log("[Graphics] first start: %s, so the %s preset", why.c_str(),
            pick == Low ? "Low" : pick == Medium ? "Medium" : pick == High ? "High" : "Ultra");
    g_settings = preset(pick, Settings{});
    set(g_settings);
    return;
  }
  Settings s;
  s.shadows = clampSetting(::Settings::getInt("user", "shadowquality", 2), 3);
  s.range = clampSetting(::Settings::getInt("user", "shadowrange", 2), 3);
  s.creatures = clampSetting(::Settings::getInt("user", "creatureshadows", 2), 2);
  s.lamps = clampSetting(::Settings::getInt("user", "lampquality", 2), 2);
  s.device = clampSetting(::Settings::getInt("user", "lightingdevice", 0), 1);
  s.preset = presetOf(s);
  g_settings = s;
}

int recommend(std::string *why) {
#ifdef _WIN32
  // (the first graphics adapter - the one SDL's renderer uses - by its name
  // and its own memory: integrated graphics share the computer's memory and
  // report little or none of their own)
  using CreateFactory = HRESULT(WINAPI *)(REFIID, void **);
  if (HMODULE lib = LoadLibraryA("dxgi.dll")) {
    if (auto create = reinterpret_cast<CreateFactory>(GetProcAddress(lib, "CreateDXGIFactory1"))) {
      IDXGIFactory1 *factory = nullptr;
      if (SUCCEEDED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))) && factory) {
        IDXGIAdapter1 *adapter = nullptr;
        if (factory->EnumAdapters1(0, &adapter) == S_OK && adapter) {
          DXGI_ADAPTER_DESC1 d{};
          adapter->GetDesc1(&d);
          adapter->Release();
          factory->Release();
          std::wstring name(d.Description);
          auto has = [&name](const wchar_t *s) { return name.find(s) != std::wstring::npos; };
          const size_t mb = size_t(d.DedicatedVideoMemory / (1024 * 1024));
          const bool software = d.VendorId == 0x1414 || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
          const bool intel = d.VendorId == 0x8086;
          const bool integrated = !software && !has(L"Arc") &&
                                  (mb < 1024 || has(L"Radeon(TM) Graphics") || has(L"Radeon Graphics") || has(L"Vega") ||
                                   (intel && (has(L"UHD") || has(L"HD Graphics") || has(L"Iris"))));
          if (why) {
            std::string n;
            for (wchar_t c : name)
              n += c < 128 ? char(c) : '?';
            *why = n + ", " + std::to_string(mb) + " MB of its own memory" +
                   (software ? " (no graphics card driver)" : integrated ? " (integrated graphics)" : "");
          }
          if (software)
            return Low;
          if (integrated)
            return intel ? Low : Medium;
          return High;
        }
        factory->Release();
      }
    }
  }
#endif
  if (why)
    *why = "graphics not identified";
  return High;
}

uint64_t serial() {
  ensure();
  return g_serial;
}

int sunMapSize() {
  static constexpr int kSize[] = {512, 768, 1024, 1536};
  return kSize[settings().shadows];
}
float sunTexelsPerTile() {
  static constexpr float kTexels[] = {4, 6, 8, 12};
  return kTexels[settings().shadows];
}
bool softShadows() { return settings().shadows > 0; }
int shapeFaceBudget() {
  static constexpr int kFaces[] = {20000, 40000, 60000, 80000};
  return kFaces[settings().range];
}
int creatureLight() { return settings().creatures; }
float lampDetail() {
  static constexpr float kDetail[] = {.5f, .75f, 1.0f};
  return kDetail[settings().lamps];
}
int lampLimit() {
  static constexpr int kLamps[] = {32, 64, 96};
  return kLamps[settings().lamps];
}
bool lightingOnGraphicsCard() { return settings().device == 0; }

} // namespace GraphicsQuality
