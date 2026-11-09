#include "DisplaySettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Embed.hpp"
#include "GpuFsr.hpp"
#include "GraphicsEffects.hpp"
#include "RenderSettings.hpp"
#include "Settings.hpp"
#include "ShaderMods.hpp"
#include "Window.hpp"
#include "World.hpp"

namespace Display {

std::function<void(int size)> onCursorSize;

namespace {
SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
int g_fps = 60;
bool g_vsync = true;
float g_brightness = 1.0f;
Art g_art = Art::Fsr, g_artAtStart = Art::Fsr;
Filter g_filter = Filter::Off;
float g_strength = 1.0f;
int g_cursor = 1;

const char *kFilterNames[] = {"off", "protanopia", "deuteranopia", "tritanopia"};

void applyColour() {
  GpuFsr::setColour(static_cast<int>(g_filter), g_strength, g_brightness);
}
// (FSR on the graphics card while it's picked; otherwise the plain look)
void applyArt() {
  GpuFsr::setUpscaling(g_art == Art::Fsr || g_art == Art::Stylized);
  GpuFsr::setStyled(g_art == Art::Stylized);
  RenderSettings::artLook = g_art == Art::Sharp    ? RenderSettings::ArtLook::Sharp
                            : g_art == Art::Smooth ? RenderSettings::ArtLook::Smooth
                                                   : RenderSettings::ArtLook::Auto;
}
std::string fmt(float v) {
  char b[16];
  std::snprintf(b, sizeof b, "%.2f", v);
  return b;
}
// (SDL3's displays are ids, not numbers from 0: the window's, or the main one)
SDL_DisplayID displayOf(SDL_Window *w) {
  const SDL_DisplayID d = w ? SDL_GetDisplayForWindow(w) : 0;
  return d ? d : SDL_GetPrimaryDisplay();
}
// Full screen's display mode: the size picked, at the monitor's best refresh
// (SDL3: a mode set on the window makes its full screen exclusive)
void setFullscreenMode(Size s) {
  SDL_DisplayMode got{};
  if (SDL_GetClosestFullscreenDisplayMode(displayOf(g_window), s.w, s.h, 0.0f, false, &got))
    SDL_SetWindowFullscreenMode(g_window, &got);
}
Size desktopSize() {
  const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(displayOf(g_window));
  return dm ? Size{dm->w, dm->h} : Size{};
}
Size savedFullscreenSize() {
  const Size desk = desktopSize();
  return {Settings::getInt("user", "fullscreenwidth", desk.w), Settings::getInt("user", "fullscreenheight", desk.h)};
}
} // namespace

void init(SDL_Window *window, SDL_Renderer *renderer) {
  g_window = window;
  g_renderer = renderer;
  g_fps = std::clamp(Settings::getInt("user", "fpslimit", 60), 0, 1000);
  g_vsync = Settings::getInt("user", "vsync", 1) != 0;
  SDL_SetRenderVSync(renderer, g_vsync ? 1 : 0);
  g_brightness = std::clamp(static_cast<float>(std::atof(Settings::get("user", "brightness", "1").c_str())), 0.5f, 2.0f);
  g_art = g_artAtStart = parseArt(Settings::get("user", "artupscale", "fsr"));
  std::string f = Settings::get("user", "colorfilter", "off");
  for (int i = 0; i < 4; i++)
    if (f == kFilterNames[i])
      g_filter = static_cast<Filter>(i);
  g_strength =
      std::clamp(static_cast<float>(std::atof(Settings::get("user", "colorfilterstrength", "1").c_str())), 0.0f, 1.0f);
  g_cursor = std::clamp(Settings::getInt("user", "cursorsize", 1), 1, 3);
  applyColour();
  applyArt();
}

// ------------------------------------------------------------------ the window
Mode mode() {
  Uint32 f = g_window ? SDL_GetWindowFlags(g_window) : 0;
  // (SDL3: full screen with a display mode of its own is exclusive; with
  // none, it's the desktop's - borderless)
  if (f & SDL_WINDOW_FULLSCREEN)
    return SDL_GetWindowFullscreenMode(g_window) ? Mode::Fullscreen : Mode::Borderless;
  return Mode::Windowed;
}

const char *modeName(Mode m) {
  return m == Mode::Borderless ? "borderless" : m == Mode::Fullscreen ? "fullscreen" : "window";
}

void setMode(Mode m) {
  if (!g_window || Embed::active())
    return;
  if (m == Mode::Fullscreen) {
    setFullscreenMode(savedFullscreenSize());
    SDL_SetWindowFullscreen(g_window, true);
  } else if (m == Mode::Borderless) {
    SDL_SetWindowFullscreenMode(g_window, nullptr);
    SDL_SetWindowFullscreen(g_window, true);
  } else {
    SDL_SetWindowFullscreen(g_window, false);
  }
  Settings::set("user", "windowmode", modeName(m));
  Settings::setInt("user", "fullscreen", m == Mode::Windowed ? 0 : 1);
}

std::vector<Size> resolutions() {
  std::vector<Size> out;
  SDL_Rect usable = Window::usableArea(g_window);
  bool windowed = mode() == Mode::Windowed;
  int n = 0;
  SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(displayOf(g_window), &n);
  for (int i = 0; modes && i < n; i++) {
    const SDL_DisplayMode &dm = *modes[i];
    if (dm.w < 800 || dm.h < 600)
      continue;
    // (a window's frame needs a little room round it)
    if (windowed && (dm.w > usable.w || dm.h > usable.h - 32))
      continue;
    Size s{dm.w, dm.h};
    if (std::find(out.begin(), out.end(), s) == out.end())
      out.push_back(s);
  }
  SDL_free(modes);
  if (windowed && out.empty())
    out.push_back({800, 600});
  // (the window's own size, dragged to one of its own, among them)
  Size now = resolution();
  if (windowed && now.w > 0 && std::find(out.begin(), out.end(), now) == out.end())
    out.push_back(now);
  std::sort(out.begin(), out.end(), [](const Size &a, const Size &b) { return a.w * a.h > b.w * b.h; });
  return out;
}

Size resolution() {
  if (!g_window)
    return {};
  Mode m = mode();
  if (m == Mode::Fullscreen)
    if (const SDL_DisplayMode *dm = SDL_GetWindowFullscreenMode(g_window))
      return {dm->w, dm->h};
  if (m == Mode::Borderless)
    return desktopSize();
  Size s;
  SDL_GetWindowSize(g_window, &s.w, &s.h);
  return s;
}

bool resolutionPickable() { return g_window && !Embed::active() && mode() != Mode::Borderless; }

void setResolution(Size s) {
  if (!resolutionPickable() || s.w <= 0 || s.h <= 0)
    return;
  if (mode() == Mode::Fullscreen) {
    setFullscreenMode(s);
    // (SDL3 changes an exclusive full screen's mode as it's set)
    Settings::setInt("user", "fullscreenwidth", s.w);
    Settings::setInt("user", "fullscreenheight", s.h);
    return;
  }
  if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED)
    SDL_RestoreWindow(g_window);
  SDL_SetWindowSize(g_window, s.w, s.h);
  const SDL_DisplayID d = displayOf(g_window);
  SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
  // (kept as the window's own size and place: Window::windowChanged)
  Window::windowChanged(g_window);
}

// ------------------------------------------------------------------ frames
int frameLimit() { return g_fps; }
void setFrameLimit(int fps) {
  g_fps = std::max(0, fps);
  Settings::setInt("user", "fpslimit", g_fps);
}
float frameDelayMs() { return g_fps > 0 ? 1000.0f / g_fps : 0.0f; }

bool vsync() { return g_vsync; }
void setVsync(bool on) {
  g_vsync = on;
  if (g_renderer)
    SDL_SetRenderVSync(g_renderer, on ? 1 : 0);
  Settings::setInt("user", "vsync", on ? 1 : 0);
}

float brightness() { return g_brightness; }
void setBrightness(float v) {
  g_brightness = std::clamp(v, 0.5f, 2.0f);
  applyColour();
  Settings::set("user", "brightness", fmt(g_brightness));
}

// ------------------------------------------------------------------ the art
Art parseArt(const std::string &name) {
  if (name == "mmpx")
    return Art::Mmpx;
  if (name == "sharp" || name == "off") // (off: the art as it is - every pixel square)
    return Art::Sharp;
  if (name == "smooth")
    return Art::Smooth;
  if (name == "stylized")
    return Art::Stylized;

  return Art::Fsr;
}
const char *artName(Art a) {
  return a == Art::Mmpx       ? "mmpx"
         : a == Art::Sharp    ? "sharp"
         : a == Art::Smooth   ? "smooth"
         : a == Art::Stylized ? "stylized"
                              : "fsr";
}
Art art() { return g_art; }
Art artAtStart() { return g_artAtStart; }
void setArt(Art a) {
  g_art = a;
  // (MMPX's art is made as it loads: picked or left, it's the next start's)
  if (a != Art::Mmpx && g_artAtStart != Art::Mmpx)
    applyArt();
  Settings::set("user", "artupscale", artName(a));
}

// ------------------------------------------------------------------ accessibility
Filter filter() { return g_filter; }
void setFilter(Filter f) {
  g_filter = f;
  applyColour();
  Settings::set("user", "colorfilter", kFilterNames[static_cast<int>(f)]);
}
float filterStrength() { return g_strength; }
void setFilterStrength(float s) {
  g_strength = std::clamp(s, 0.0f, 1.0f);
  applyColour();
  Settings::set("user", "colorfilterstrength", fmt(g_strength));
}

int cursorSize() { return g_cursor; }
void setCursorSize(int size) {
  g_cursor = std::clamp(size, 1, 3);
  Settings::setInt("user", "cursorsize", g_cursor);
  if (onCursorSize)
    onCursorSize(g_cursor);
}

bool edgeScroll() { return World::kEdgeScrollOn; }
void setEdgeScroll(bool on) {
  World::kEdgeScrollOn = on;
  Settings::setInt("user", "edgescroll", on ? 1 : 0);
}

// ------------------------------------------------------------------ renderer
namespace {
bool g_autoDirect3d = false;
const char *g_atStart = "opengl";
const char *picked(const char *r) { return g_atStart = r; }
}
int rendererChoice() {
  const std::string pick = Settings::get("user", "renderer", "auto");
  return pick == "direct3d11" ? 1 : pick == "opengl" ? 2 : 0;
}
void setRendererChoice(int choice) {
  Settings::set("user", "renderer", choice == 1 ? "direct3d11" : choice == 2 ? "opengl" : "auto");
}
const char *rendererAtStart() { return g_atStart; }
const char *startupRenderer() {
#ifdef _WIN32
  // (ours: ZT_RENDERER=opengl|direct3d11 for one run - the benchmarks'
  // comparisons - over what's saved)
  const char *forced = SDL_getenv("ZT_RENDERER");
  const std::string pick = forced && *forced ? forced : Settings::get("user", "renderer", "auto");
  if (pick == "opengl" || pick == "direct3d11")
    return picked(pick == "opengl" ? "opengl" : "direct3d11");
  // (what needs OpenGL, as saved: mods' shaders, written in GLSL. FSR,
  // Stylized, brightness, the colour filter and the graphics effects run on
  // Direct3D 11 too - GpuFsrD3D11.cpp - so they no longer switch the game
  // to the slower OpenGL renderer)
  bool needs = SDL_getenv("ZT_LIGHTING_GPU") != nullptr;
  for (const Shaders::Shader &s : Shaders::all())
    needs = needs || s.on;
  if (needs)
    return picked("opengl");
  g_autoDirect3d = true;
  return picked("direct3d11");
#else
  return picked("opengl");
#endif
}
bool openGlNextStart() { return g_autoDirect3d && !GpuFsr::modShaders(); }

} // namespace Display
