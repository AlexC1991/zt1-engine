#include "Embed.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

// The Lab's Play view, and the window handle it passes in, exist only on
// Windows: the Lab drives the retail 32-bit zoo.exe (src/original/). Anywhere
// else Embed::into() reports failure and the engine runs as a normal window,
// so everything below that needs a window handle is behind the platform check.
// ours: Linux port (plumbing; the original game has nothing like this).
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "SessionVolume.hpp"

namespace {
std::atomic<bool> g_active{false}, g_quit{false}, g_hidden{true};
#ifdef _WIN32
HWND g_child = nullptr, g_owner = nullptr;
SDL_Window *g_window = nullptr;

// A click at a point in the window (its pixels), as SDL events: the Lab
// testing menus in Play without anyone's mouse
void clickAt(int x, int y, int button) {
  Uint32 id = g_window ? SDL_GetWindowID(g_window) : 0;
  SDL_Event e{};
  e.type = SDL_MOUSEMOTION;
  e.motion.windowID = id;
  e.motion.x = x;
  e.motion.y = y;
  SDL_PushEvent(&e);
  SDL_Delay(60);
  for (Uint32 type : {static_cast<Uint32>(SDL_MOUSEBUTTONDOWN), static_cast<Uint32>(SDL_MOUSEBUTTONUP)}) {
    SDL_Event b{};
    b.type = type;
    b.button.windowID = id;
    b.button.button = static_cast<Uint8>(button);
    b.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    b.button.clicks = 1;
    b.button.x = x;
    b.button.y = y;
    SDL_PushEvent(&b);
    SDL_Delay(80);
  }
}

void listen() {
  // (stdin: one command a line, from the Lab app)
  CoInitializeEx(nullptr, COINIT_MULTITHREADED); // (for volume)
  char line[256];
  while (std::fgets(line, sizeof line, stdin)) {
    int x, y, w, h;
    if (std::sscanf(line, "volume %d", &x) == 1) {
      setSessionVolume(x / 100.0f);
    } else if (std::sscanf(line, "rect %d %d %d %d", &x, &y, &w, &h) == 4) {
      SetWindowPos(g_child, nullptr, x, y, w > 1 ? w : 1, h > 1 ? h : 1, SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
      g_hidden = false;
    } else if (std::strncmp(line, "hide", 4) == 0) {
      ShowWindow(g_child, SW_HIDE);
      g_hidden = true;
    } else if (std::strncmp(line, "show", 4) == 0) {
      ShowWindow(g_child, SW_SHOWNA);
      g_hidden = false;
    } else if (std::sscanf(line, "click %d %d", &x, &y) == 2) {
      clickAt(x, y, SDL_BUTTON_LEFT);
    } else if (std::sscanf(line, "rclick %d %d", &x, &y) == 2) {
      clickAt(x, y, SDL_BUTTON_RIGHT);
    } else if (std::strncmp(line, "focus", 5) == 0) {
      SetForegroundWindow(g_child);
      SetFocus(g_child);
    } else if (std::strncmp(line, "quit", 4) == 0) {
      break;
    }
  }
  // (asked to, or the Lab app gone: the game goes too)
  g_quit = true;
  SDL_Event e{};
  e.type = SDL_QUIT;
  SDL_PushEvent(&e);
}
#endif // _WIN32
} // namespace

namespace Embed {
bool into(SDL_Window *window, const char *ownerHandle) {
#ifdef _WIN32
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info))
    return false;
  g_child = info.info.win.window;
  g_window = window;
  g_owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(std::strtoull(ownerHandle, nullptr, 0)));
  if (!g_child || !g_owner || !IsWindow(g_owner))
    return false;
  // A borderless window owned by the Lab's (not a child: the Lab's page is
  // drawn over child windows): it stays over it, minimises and hides with
  // it, isn't on the taskbar, and the Lab puts it over its Play view in
  // screen pixels
  LONG style = GetWindowLong(g_child, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CHILD);
  style |= WS_POPUP;
  SetWindowLong(g_child, GWL_STYLE, style);
  LONG ex = GetWindowLong(g_child, GWL_EXSTYLE);
  SetWindowLong(g_child, GWL_EXSTYLE, (ex | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
  SetWindowLongPtr(g_child, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(g_owner));
  SetWindowPos(g_child, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
  g_active = true;
  std::thread(listen).detach();
  return true;
#else
  // There is no Lab to embed into here. Say so, and the engine keeps its own
  // window and runs normally.
  (void)window;
  (void)ownerHandle;
  return false;
#endif
}
bool active() { return g_active; }
bool quitAsked() { return g_quit; }
bool hidden() { return g_active && g_hidden; }
} // namespace Embed
