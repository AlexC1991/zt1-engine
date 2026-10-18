#include "Embed.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <SDL2/SDL_syswm.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "SessionVolume.hpp"

namespace {
std::atomic<bool> g_active{false}, g_quit{false}, g_hidden{true}, g_snap{false};
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

// The Windows dialog the game has open, if any (a visible #32770 of this
// process: Save a zoo... / Load a zoo...)
HWND openDialog() {
  struct Find {
    HWND found = nullptr;
  } f;
  EnumWindows(
      [](HWND h, LPARAM l) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        char cls[16] = {};
        GetClassNameA(h, cls, sizeof cls);
        if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !std::strcmp(cls, "#32770")) {
          reinterpret_cast<Find *>(l)->found = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&f));
  return f.found;
}

// "dialog text <name>" fills the open file dialog's name, "dialog ok" /
// "dialog cancel" presses its button (as the original's hook does for its
// dialogs: the Lab testing Load Game in Play without anyone's mouse)
void dialogCommand(const char *arg) {
  while (*arg == ' ')
    arg++;
  HWND d = openDialog();
  if (!d)
    return;
  if (!std::strncmp(arg, "text ", 5)) {
    std::string name = arg + 5;
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
      name.pop_back();
    // (the old-style dialog's name box is control 1152; the Explorer-style
    // one's is an Edit two down inside control 1148 - ComboBoxEx32, then
    // ComboBox - so its children are searched all the way down)
    auto firstEdit = [](HWND parent) {
      struct Want {
        HWND found = nullptr;
      } w;
      EnumChildWindows(
          parent,
          [](HWND h, LPARAM l) -> BOOL {
            char cls[16] = {};
            GetClassNameA(h, cls, sizeof cls);
            if (!std::strcmp(cls, "Edit")) {
              reinterpret_cast<Want *>(l)->found = h;
              return FALSE;
            }
            return TRUE;
          },
          reinterpret_cast<LPARAM>(&w));
      return w.found;
    };
    HWND edit = GetDlgItem(d, 1152);
    if (!edit)
      if (HWND combo = GetDlgItem(d, 1148))
        edit = firstEdit(combo);
    if (!edit)
      edit = firstEdit(d);
    if (edit)
      SendMessageA(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(name.c_str()));
  } else if (!std::strncmp(arg, "ok", 2) || !std::strncmp(arg, "cancel", 6)) {
    if (HWND b = GetDlgItem(d, arg[0] == 'o' ? IDOK : IDCANCEL))
      PostMessageA(b, BM_CLICK, 0, 0);
  }
}

// A key held down for so long, as SDL events ("hold Right 800": the map
// scrolled with the arrow keys, the Lab looking round in Play without
// anyone's keyboard)
void holdKey(const char *name, int ms) {
  SDL_Scancode sc = SDL_GetScancodeFromName(name);
  if (sc == SDL_SCANCODE_UNKNOWN)
    return;
  Uint32 id = g_window ? SDL_GetWindowID(g_window) : 0;
  for (Uint32 type : {static_cast<Uint32>(SDL_KEYDOWN), static_cast<Uint32>(SDL_KEYUP)}) {
    SDL_Event e{};
    e.type = type;
    e.key.windowID = id;
    e.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    SDL_PushEvent(&e);
    if (type == SDL_KEYDOWN)
      SDL_Delay(static_cast<Uint32>(ms > 0 ? ms : 1));
  }
}

// One command from the Lab app; false: quit
bool command(const char *line) {
  int x, y, w, h;
  if (std::sscanf(line, "volume %d", &x) == 1) {
    setSessionVolume(x / 100.0f);
  } else if (bool screen = std::sscanf(line, "rect %d %d %d %d", &x, &y, &w, &h) == 4;
             screen || std::sscanf(line, "place %d %d %d %d", &x, &y, &w, &h) == 4) {
    // (place: in the Lab window's client area; rect: on the screen, as an
    // older Lab says it. On top of the Lab's page, its sibling - and posted
    // to the game's thread, not waited for)
    POINT at{x, y};
    if (screen)
      ScreenToClient(g_owner, &at);
    SetWindowPos(g_child, HWND_TOP, at.x, at.y, w > 1 ? w : 1, h > 1 ? h : 1, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
    g_hidden = false;
  } else if (std::strncmp(line, "hide", 4) == 0) {
    ShowWindowAsync(g_child, SW_HIDE);
    g_hidden = true;
  } else if (std::strncmp(line, "show", 4) == 0) {
    ShowWindowAsync(g_child, SW_SHOWNA);
    g_hidden = false;
  } else if (std::sscanf(line, "click %d %d", &x, &y) == 2) {
    clickAt(x, y, SDL_BUTTON_LEFT);
  } else if (std::sscanf(line, "rclick %d %d", &x, &y) == 2) {
    clickAt(x, y, SDL_BUTTON_RIGHT);
  } else if (std::sscanf(line, "move %d %d", &x, &y) == 2) {
    // (the mouse there and left: hovering - a tooltip, a lit piece)
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = g_window ? SDL_GetWindowID(g_window) : 0;
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
  } else if (std::strncmp(line, "snap", 4) == 0) {
    g_snap = true;
  } else if (std::strncmp(line, "dialog", 6) == 0) {
    dialogCommand(line + 6);
  } else if (char key[32] = {}; std::sscanf(line, "hold %31s %d", key, &x) == 2) {
    holdKey(key, x);
  } else if (std::strncmp(line, "focus", 5) == 0) {
    // (a child can't be the window in front: the keyboard's given it with
    // this thread's input joined to the game's for the moment)
    DWORD game = GetWindowThreadProcessId(g_child, nullptr), self = GetCurrentThreadId();
    AttachThreadInput(self, game, TRUE);
    SetFocus(g_child);
    AttachThreadInput(self, game, FALSE);
  } else if (std::strncmp(line, "quit", 4) == 0) {
    return false;
  }
  return true;
}

void listen() {
  // (stdin: one command a line, from the Lab app)
  CoInitializeEx(nullptr, COINIT_MULTITHREADED); // (for volume)
  // (the Lab's pixels as they are, on any monitor's scaling)
  using DpiFn = HANDLE(WINAPI *)(HANDLE);
  if (auto dpi = reinterpret_cast<DpiFn>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext")))
    dpi(reinterpret_cast<HANDLE>(-4)); // (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  std::string buf;
  char chunk[512];
  DWORD n;
  bool going = true;
  while (going && in && ReadFile(in, chunk, sizeof chunk, &n, nullptr) && n) {
    buf.append(chunk, n);
    // (rect/place lines in a row - the Lab being dragged or resized - are
    // only where it ended up: the last one's done, before what comes after)
    std::string rect;
    size_t nl;
    while (going && (nl = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      if (!line.compare(0, 5, "rect ") || !line.compare(0, 6, "place ")) {
        rect = line;
        continue;
      }
      if (!rect.empty())
        command(rect.c_str()), rect.clear();
      going = command(line.c_str());
    }
    if (going && !rect.empty())
      command(rect.c_str());
  }
  // (asked to, or the Lab app gone: the game goes too)
  g_quit = true;
  SDL_Event e{};
  e.type = SDL_QUIT;
  SDL_PushEvent(&e);
}
} // namespace

namespace Embed {
bool into(SDL_Window *window, const char *ownerHandle) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info))
    return false;
  g_child = info.info.win.window;
  g_window = window;
  g_owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(std::strtoull(ownerHandle, nullptr, 0)));
  if (!g_child || !g_owner || !IsWindow(g_owner))
    return false;
  // A borderless child of the Lab's window, over its Play view: Windows
  // moves it with the Lab's, in the same step (a window of its own, moved
  // after, trailed behind a fast drag). The Lab draws its page without
  // DirectComposition, or the page would cover it
  LONG style = GetWindowLong(g_child, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_POPUP);
  style |= WS_CHILD | WS_CLIPSIBLINGS;
  SetWindowLong(g_child, GWL_STYLE, style);
  LONG ex = GetWindowLong(g_child, GWL_EXSTYLE);
  // (its clicks aren't told to the Lab's page: it takes the keyboard back)
  SetWindowLong(g_child, GWL_EXSTYLE, (ex | WS_EX_NOPARENTNOTIFY) & ~WS_EX_APPWINDOW);
  SetParent(g_child, g_owner);
  SetWindowPos(g_child, HWND_TOP, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
  // (clicked: the keyboard to it, as a window of its own got it by being
  // activated)
  SDL_AddEventWatch(
      [](void *, SDL_Event *e) -> int {
        if (e->type == SDL_MOUSEBUTTONDOWN && GetWindowThreadProcessId(g_child, nullptr) == GetCurrentThreadId() && GetFocus() != g_child)
          SetFocus(g_child);
        return 0;
      },
      nullptr);
  g_active = true;
  std::thread(listen).detach();
  return true;
}
bool active() { return g_active; }
bool quitAsked() { return g_quit; }
bool hidden() { return g_active && g_hidden; }
bool snapAsked() { return g_snap.exchange(false); }
} // namespace Embed
