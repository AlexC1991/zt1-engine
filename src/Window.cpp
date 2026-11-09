#include "DiagnosticUtilityData.hpp"
#include "DisplaySettings.hpp"
#include "GpuTankLayer.hpp"
#include "Embed.hpp"
#include "GpuFsr.hpp"
#include "ShaderMods.hpp"
#include "Settings.hpp"
#include "Window.hpp"
#include <algorithm>

bool g_showDiagnostics = false;

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "Audio.hpp"

Window::Window(const std::string &title, int width, int height, float fps_target) {
    // Sharp on any monitor: aware of each monitor's DPI, so a 125-200%
    // Windows scaling doesn't stretch the game's pixels (it was blurred);
    // the window's size is in real pixels, and the uiscale extra makes the
    // screens bigger in whole steps instead (SDL3 is aware of each monitor's
    // DPI already: SDL2 needed SDL_HINT_WINDOWS_DPI_AWARENESS)
    // (SDL3 checks by default that every texture and renderer handed to it is
    // a live one - a locked table lookup a call, thousands a frame: the
    // world's drawing took 1.7 times as long as on SDL2. Null checks only,
    // as SDL2 made)
    SDL_SetHint(SDL_HINT_INVALID_PARAM_CHECKS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't init SDL: %s", SDL_GetError());
        exit(1);
    }

    if (!TTF_Init()) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't init SDL_ttf: %s", SDL_GetError());
        exit(2);
    }

    if (!Audio::open()) {
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't init SDL_mixer: %s", SDL_GetError());
        exit(3);
    }
    Audio::musicVolume(Audio::kMaxVolume);
    {
      int freq = 0, channels = 0;
      Audio::spec(&freq, &channels);
      SDL_Log("[Sound] audio driver %s, %d Hz, %d channels", SDL_GetCurrentAudioDriver(), freq, channels);
      int count = 0;
      if (SDL_AudioDeviceID *devices = SDL_GetAudioPlaybackDevices(&count)) {
        for (int d = 0; d < count; d++)
          SDL_Log("[Sound]   output %d: %s", d, SDL_GetAudioDeviceName(devices[d]));
        SDL_free(devices);
      }
    }

    // A click that focuses the window also counts as a click (SDL drops it
    // by default, so menu buttons needed two clicks)
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // Centred on the screen and no bigger than its usable part (left to
    // Windows, it could open stuck in a corner, partly off the screen -
    // with the original game full-screen, every time)
    {
      SDL_Rect usable;
      if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
        width = (std::min)(width, usable.w - 16);
        height = (std::min)(height, usable.h - 48);
      }
    }
    this->window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE);
    if (this->window) {
      SDL_SetWindowPosition(this->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
      SDL_Rect usable;
      int x = 0, y = 0, w = 0, h = 0;
      SDL_GetWindowPosition(this->window, &x, &y);
      SDL_GetWindowSize(this->window, &w, &h);
      if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        SDL_Log("[Window] %dx%d at %d,%d on a usable %dx%d at %d,%d", w, h, x, y, usable.w, usable.h, usable.x, usable.y);
    }
    if (this->window == nullptr) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't create window: %s", SDL_GetError());
        exit(4);
    }

    // (the renderer SDL_HINT_RENDER_DRIVER names: Display::startupRenderer)
    this->renderer = SDL_CreateRenderer(this->window, nullptr);
    if (this->renderer == nullptr) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't create renderer: %s", SDL_GetError());
        exit(5);
    }
    SDL_SetRenderVSync(this->renderer, 1);
    // (new textures scaled as SDL2's were - nearest, each pixel kept - where
    // SDL3's default is linear: the art was softened)
    SDL_SetDefaultTextureScaleMode(this->renderer, SDL_SCALEMODE_NEAREST);

    SDL_SetRenderDrawBlendMode(this->renderer, SDL_BLENDMODE_BLEND);

    this->start_frame = SDL_GetTicks();
    this->frame_delay = 1000.0 / fps_target;
}

void Window::clear() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(this->renderer);
}

// ---------------------------------------------------------- the player's window
namespace {
bool g_remembers = false;     // an ordinary run (placeSaved called)
Uint32 g_changedAt = 0;       // a move or resize waiting to be kept
SDL_Window *g_changed = nullptr;

// Where it is now, kept in zoo.ini: its size and place unless it's full
// screen or maximised (then only that, so un-maximising goes back to them)
void keep(SDL_Window *w) {
  Uint32 f = SDL_GetWindowFlags(w);
  bool full = (f & SDL_WINDOW_FULLSCREEN) != 0, max = (f & SDL_WINDOW_MAXIMIZED) != 0;
  Settings::setInt("user", "fullscreen", full ? 1 : 0);
  Settings::setInt("user", "maximized", max ? 1 : 0);
  if (full || max || (f & SDL_WINDOW_MINIMIZED))
    return;
  int x = 0, y = 0, cw = 0, ch = 0;
  SDL_GetWindowPosition(w, &x, &y);
  SDL_GetWindowSize(w, &cw, &ch);
  Settings::setInt("user", "screenwidth", cw);
  Settings::setInt("user", "screenheight", ch);
  // (the original's own keys: its window procedure 0x418f97 keeps them on
  // each move, unless minimised)
  Settings::setInt("UI", "lastWindowX", x);
  Settings::setInt("UI", "lastWindowY", y);
}
} // namespace

SDL_Rect Window::usableArea(SDL_Window *w) {
  SDL_DisplayID d = w ? SDL_GetDisplayForWindow(w) : 0;
  if (!d)
    d = SDL_GetPrimaryDisplay();
  SDL_Rect r{0, 0, 0, 0};
  if (!SDL_GetDisplayUsableBounds(d, &r))
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &r);
  return r;
}

void Window::placeSaved() {
  SDL_Window *w = this->window;
  // (the original's smallest screen, 800x600, where the monitor has room:
  // the screens are laid out for it)
  SDL_Rect u = usableArea(w);
  if (u.w >= 800 && u.h >= 600)
    SDL_SetWindowMinimumSize(w, 800, 600);
  // Where it was left, if that's still on a monitor (one unplugged since:
  // centred on the first instead)
  int x = Settings::getInt("UI", "lastWindowX", INT32_MIN), y = Settings::getInt("UI", "lastWindowY", INT32_MIN);
  if (x != INT32_MIN && y != INT32_MIN) {
    int cw = 0, ch = 0;
    SDL_GetWindowSize(w, &cw, &ch);
    int displays = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&displays);
    for (int d = 0; ids && d < displays; d++) {
      SDL_Rect b;
      // (its title bar on that monitor, so it can be grabbed)
      if (SDL_GetDisplayUsableBounds(ids[d], &b) && x + 40 < b.x + b.w && x + cw - 40 > b.x && y >= b.y - 8 &&
          y < b.y + b.h - 40) {
        SDL_SetWindowPosition(w, x, y);
        break;
      }
    }
    SDL_free(ids);
  }
  if (Settings::getInt("user", "maximized", 0))
    SDL_MaximizeWindow(w);
  // Full screen as it was left: borderless (the monitor's own size), or -
  // picked in Game Options > Display - full screen at its resolution
  if (Settings::getInt("user", "fullscreen", 0))
    Display::setMode(Settings::get("user", "windowmode", "borderless") == "fullscreen" ? Display::Mode::Fullscreen
                                                                                       : Display::Mode::Borderless);
  g_remembers = true;
}

void Window::windowChanged(SDL_Window *w) {
  if (!g_remembers || !w || Embed::active())
    return;
  g_changed = w;
  g_changedAt = SDL_GetTicks();
}

void Window::setFullscreen(SDL_Window *w, bool full) {
  if (!w || Embed::active())
    return;
  Display::setMode(full ? Display::Mode::Borderless : Display::Mode::Windowed);
}

void Window::present() {
    if (g_changed && SDL_GetTicks() - g_changedAt > 500) {
        keep(g_changed);
        g_changed = nullptr;
    }
    // (the frame rate limit: Game Options > Display, 60 unless changed)
    float delay = Display::frameDelayMs();
    int delta = SDL_GetTicks() - this->start_frame;
    if (delta < delay) {
        SDL_Delay(static_cast<Uint32>(delay - delta));
    }
    
    // Diagnostics overlay (FPS, RAM, renderer, resolution): toggled with F3
    static DiagnosticUtilityData* globalDiag = new DiagnosticUtilityData();
    if (globalDiag && g_showDiagnostics) {
        globalDiag->update();
        globalDiag->draw(this->renderer);
    }

    // Shader mods' screen shaders, then the colour-blind filter and
    // brightness, over the whole frame
    Shaders::applyScreen(this->renderer);
    GpuFsr::finishFrame(this->renderer);

    // The Lab's Play picture (shot <file>): this frame, whole, as drawn
    if (Embed::active()) {
        std::string shot = Embed::shotAsked();
        if (!shot.empty()) {
            SDL_SetRenderViewport(this->renderer, nullptr);
            SDL_SetRenderScale(this->renderer, 1, 1);
            int w = 0, h = 0;
            SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
            if (SDL_Surface *s = ZT_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_BGR24)) {
                if (ZT_RenderReadPixels(this->renderer, nullptr, SDL_PIXELFORMAT_BGR24, s->pixels, s->pitch) == 0)
                    SDL_SaveBMP(s, shot.c_str());
                SDL_DestroySurface(s);
            }
        }
    }

    SDL_RenderPresent(this->renderer);
    this->start_frame = SDL_GetTicks();
}

Window::~Window() {
    GpuTankLayer::releasePool(this->renderer);
    SDL_DestroyRenderer(this->renderer);
    SDL_DestroyWindow(this->window);
    SDL_DestroyCursor(this->default_cursor);
    Audio::close();
    TTF_Quit();
    SDL_Quit();
}
