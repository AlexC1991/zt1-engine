#include "DiagnosticUtilityData.hpp"
#include "Window.hpp"
#include <algorithm>

bool g_showDiagnostics = false;

#include "SDL_image.h"
#include "SDL_mixer.h"
#include "SDL_ttf.h"

Window::Window(const std::string &title, int width, int height, float fps_target) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't init SDL: %s", SDL_GetError());
        exit(1);
    }

    if (TTF_Init() == -1) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't init SDL_ttf: %s", TTF_GetError());
        exit(2);
    }

    if( Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == -1 ) {
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't init SDL_mixer: %s", Mix_GetError());
        exit(3);
    }
    Mix_VolumeMusic(MIX_MAX_VOLUME);
    {
      int freq = 0, channels = 0;
      Uint16 format = 0;
      Mix_QuerySpec(&freq, &format, &channels);
      SDL_Log("[Sound] audio driver %s, %d Hz, %d channels", SDL_GetCurrentAudioDriver(), freq, channels);
      for (int d = 0; d < SDL_GetNumAudioDevices(0); d++)
        SDL_Log("[Sound]   output %d: %s", d, SDL_GetAudioDeviceName(d, 0));
      char *def = nullptr;
      SDL_AudioSpec spec;
      if (SDL_GetDefaultAudioInfo(&def, &spec, 0) == 0) {
        SDL_Log("[Sound]   Windows default: %s", def ? def : "?");
        SDL_free(def);
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
      if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0) {
        width = (std::min)(width, usable.w - 16);
        height = (std::min)(height, usable.h - 48);
      }
    }
    this->window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_RESIZABLE);
    if (this->window) {
      SDL_Rect usable;
      int x = 0, y = 0, w = 0, h = 0;
      SDL_GetWindowPosition(this->window, &x, &y);
      SDL_GetWindowSize(this->window, &w, &h);
      if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
        SDL_Log("[Window] %dx%d at %d,%d on a usable %dx%d at %d,%d", w, h, x, y, usable.w, usable.h, usable.x, usable.y);
    }
    if (this->window == nullptr) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't create window: %s", SDL_GetError());
        exit(4);
    }

    this->renderer = SDL_CreateRenderer(this->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (this->renderer == nullptr) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "couldn't create renderer: %s", SDL_GetError());
        exit(5);
    }

    SDL_SetRenderDrawBlendMode(this->renderer, SDL_BLENDMODE_BLEND);

    this->start_frame = SDL_GetTicks();
    this->frame_delay = 1000.0 / fps_target;
}

void Window::clear() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(this->renderer);
}

void Window::present() {
    int delta = SDL_GetTicks() - this->start_frame;
    if (delta < this->frame_delay) {
        SDL_Delay(this->frame_delay - delta);
    }
    
    // Diagnostics overlay (FPS, RAM, renderer, resolution): toggled with F3
    static DiagnosticUtilityData* globalDiag = new DiagnosticUtilityData();
    if (globalDiag && g_showDiagnostics) {
        globalDiag->update();
        globalDiag->draw(this->renderer);
    }
    
    SDL_RenderPresent(this->renderer);
    this->start_frame = SDL_GetTicks();
}

Window::~Window() {
    SDL_DestroyRenderer(this->renderer);
    SDL_DestroyWindow(this->window);
    SDL_FreeCursor(this->default_cursor);
    Mix_Quit();
    Mix_CloseAudio();
    IMG_Quit();
    SDL_Quit();
}
