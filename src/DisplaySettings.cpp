#include "DisplaySettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Embed.hpp"
#include "GpuFsr.hpp"
#include "RenderSettings.hpp"
#include "Settings.hpp"
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
int displayOf(SDL_Window *w) {
  int d = w ? SDL_GetWindowDisplayIndex(w) : 0;
  return d < 0 ? 0 : d;
}
// Full screen's display mode: the size picked, at the monitor's best refresh
void setFullscreenMode(Size s) {
  SDL_DisplayMode want{}, got{};
  want.w = s.w;
  want.h = s.h;
  if (SDL_GetClosestDisplayMode(displayOf(g_window), &want, &got))
    SDL_SetWindowDisplayMode(g_window, &got);
}
Size savedFullscreenSize() {
  SDL_DisplayMode dm{};
  SDL_GetDesktopDisplayMode(displayOf(g_window), &dm);
  return {Settings::getInt("user", "fullscreenwidth", dm.w), Settings::getInt("user", "fullscreenheight", dm.h)};
}
} // namespace

void init(SDL_Window *window, SDL_Renderer *renderer) {
  g_window = window;
  g_renderer = renderer;
  g_fps = std::clamp(Settings::getInt("user", "fpslimit", 60), 0, 1000);
  g_vsync = Settings::getInt("user", "vsync", 1) != 0;
  SDL_RenderSetVSync(renderer, g_vsync ? 1 : 0);
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
  if ((f & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP)
    return Mode::Borderless;
  if (f & SDL_WINDOW_FULLSCREEN)
    return Mode::Fullscreen;
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
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
  } else {
    SDL_SetWindowFullscreen(g_window, m == Mode::Borderless ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  }
  Settings::set("user", "windowmode", modeName(m));
  Settings::setInt("user", "fullscreen", m == Mode::Windowed ? 0 : 1);
}

std::vector<Size> resolutions() {
  std::vector<Size> out;
  int d = displayOf(g_window);
  SDL_Rect usable = Window::usableArea(g_window);
  bool windowed = mode() == Mode::Windowed;
  for (int i = 0, n = SDL_GetNumDisplayModes(d); i < n; i++) {
    SDL_DisplayMode dm;
    if (SDL_GetDisplayMode(d, i, &dm) != 0 || dm.w < 800 || dm.h < 600)
      continue;
    // (a window's frame needs a little room round it)
    if (windowed && (dm.w > usable.w || dm.h > usable.h - 32))
      continue;
    Size s{dm.w, dm.h};
    if (std::find(out.begin(), out.end(), s) == out.end())
      out.push_back(s);
  }
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
  if (m == Mode::Fullscreen) {
    SDL_DisplayMode dm{};
    if (SDL_GetWindowDisplayMode(g_window, &dm) == 0)
      return {dm.w, dm.h};
  }
  if (m == Mode::Borderless) {
    SDL_DisplayMode dm{};
    SDL_GetDesktopDisplayMode(displayOf(g_window), &dm);
    return {dm.w, dm.h};
  }
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
    // (the new mode takes hold as full screen is entered again)
    SDL_SetWindowFullscreen(g_window, 0);
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
    Settings::setInt("user", "fullscreenwidth", s.w);
    Settings::setInt("user", "fullscreenheight", s.h);
    return;
  }
  if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED)
    SDL_RestoreWindow(g_window);
  SDL_SetWindowSize(g_window, s.w, s.h);
  int d = displayOf(g_window);
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
    SDL_RenderSetVSync(g_renderer, on ? 1 : 0);
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

} // namespace Display
