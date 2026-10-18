#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <SDL2/SDL.h>
#include <string>

// Diagnostics overlay visibility (F3)
extern bool g_showDiagnostics;

class Window {
public:
    SDL_Window *window;
    SDL_Renderer *renderer;

    Window(const std::string &title, int width, int height, float fps_target);
    ~Window();

    unsigned int start_frame;
    float frame_delay;

    void set_cursor(SDL_Cursor * cursor) {this->default_cursor = cursor; SDL_SetCursor(this->default_cursor);};

    void clear();
    void present();

    // The player's window, as the original keeps it in zoo.ini [user]
    // (fullscreen, screenwidth, screenheight; [UI] lastWindowX/Y where it
    // was) and - ours - maximized. placeSaved() puts it back at start
    // (full screen, maximised) and turns remembering on: an ordinary run
    // only, never the Lab's or a test's. Each move or resize after that is
    // kept (half a second after it settles, not every step of a drag)
    void placeSaved();
    static void windowChanged(SDL_Window *w);
    // Full screen on or off (Alt+Enter, F11, the Game Options), kept
    static void setFullscreen(SDL_Window *w, bool full);
    // The part of the window's own monitor a window may use (taskbar left out)
    static SDL_Rect usableArea(SDL_Window *w);
private:
    SDL_Cursor * default_cursor = nullptr;
};

#endif // WINDOW_HPP