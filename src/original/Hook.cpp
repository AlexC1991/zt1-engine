// zt1-hook.dll: loaded into the original game, zoo.exe, by zt1-original.exe
// (Launch.cpp) before the game starts. It replaces a few of the Windows
// calls zoo.exe makes (its import table) with ours:
// - GetPrivateProfileStringA: zoo.ini's [user] fullscreen reads 0, so the
//   game takes its own windowed path (GXVidMan::setupWindowed, zoo.exe
//   0x52f1d0) - and WritePrivateProfileStringA doesn't save that back, so
//   zoo.ini is left as it was
//   ([user] screenwidth/screenheight too, when the Lab gives it a size)
// - In the Lab (ZT1_HOOK_EMBED, the Lab's window): CreateWindowExA,
//   SetWindowPos, SetWindowLongA and AdjustWindowRectEx keep the game's
//   window borderless, owned by the Lab's and where the Lab puts it - over
//   its Play view, like our engine's (src/Embed.cpp) - taking the same
//   lines on stdin: rect <x> <y> <w> <h> | show | hide | focus | quit |
//   volume <0-100>, rearm (the armed functions that ran, armed again), shot <file.bmp> (a picture of what it draws), key <vk>
//   (a key to it), and dialog [text <name> | ok | cancel] (the Windows file
//   dialogs it opens - Save a zoo... - which aren't on its surface). It
//   keeps running when it isn't the window in front: its window's told it's
//   still active, and GetForegroundWindow/GetActiveWindow answer its own
// - The Lab's recorder: arm <file> / disarm - which of zoo.exe's functions
//   run (a one-time breakpoint on each), reported as "hit" lines
// - Its memory: scan/rescan (where it keeps a value), peek, each (a list of
//   objects' fields at once), strs (the text an object points at - its
//   name), watch/unwatch (a
//   hardware watchpoint: which instructions write it - "wrote" lines)
// - The Lab's mouse: click/rclick/move <x> <y>, drag <x0> <y0> <x1> <y1> in
//   the game's pixels (GetCursorPos, GetAsyncKeyState, GetKeyState answer it
//   for the moment it takes)
// - GetProcAddress (DirectDrawCreateEx - zoo.exe loads DDRAW.DLL itself):
//   the surface it draws the game on (its windowed back surface, Blt to
//   the screen's), for shot
// - CoCreateInstance, LoadLibraryA: DirectPlay's objects aren't made (they
//   make Windows ask to install DirectPlay); the rest noted
// Everything goes in a log (ZT1_HOOK_LOG): each setting read, each window,
// and any crash with its zoo.exe address and the calls that led to it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <tlhelp32.h>
#include <shlwapi.h>

#include "../SessionVolume.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
// The Lab's window (embedded) and the game's, and the game's size
HWND g_owner = nullptr, g_game = nullptr;
int g_gw = 0, g_gh = 0;
// Where the Lab's Play view is (screen pixels), once it's said
int g_rx = 0, g_ry = 0, g_rw = 0, g_rh = 0;
bool g_hidden = true;
// (its window procedure, behind ours - "it keeps running", below)
WNDPROC g_gameProc = nullptr;
LRESULT CALLBACK gameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
FILE *g_log = nullptr;
CRITICAL_SECTION g_logLock;

void say(const char *fmt, ...) {
  if (!g_log)
    return;
  EnterCriticalSection(&g_logLock);
  std::fprintf(g_log, "[%7lu] ", GetTickCount() % 10000000);
  va_list a;
  va_start(a, fmt);
  std::vfprintf(g_log, fmt, a);
  va_end(a);
  std::fputc('\n', g_log);
  std::fflush(g_log);
  LeaveCriticalSection(&g_logLock);
}

// Every import-table entry of zoo.exe that points at dll!fn now points at
// hook; the real function comes back in *real. False if zoo.exe doesn't
// import it
bool patchImport(const char *dll, const char *fn, void *hook, void **real) {
  HMODULE lib = LoadLibraryA(dll);
  void *target = lib ? reinterpret_cast<void *>(GetProcAddress(lib, fn)) : nullptr;
  if (!target)
    return false;
  auto *base = reinterpret_cast<BYTE *>(GetModuleHandleA(nullptr));
  auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
  const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  bool found = false;
  for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; d++) {
    for (auto *t = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->FirstThunk); t->u1.Function; t++) {
      if (reinterpret_cast<void *>(t->u1.Function) != target)
        continue;
      DWORD old;
      VirtualProtect(&t->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
      t->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
      VirtualProtect(&t->u1.Function, sizeof(void *), old, &old);
      found = true;
    }
  }
  if (found)
    *real = target;
  say("hook %s!%s: %s", dll, fn, found ? "on" : "not imported");
  return found;
}

// ---------------------------------------------------------------- zoo.ini
// [user] settings the Lab decides for the game, never written back
bool ours(LPCSTR app, LPCSTR key) {
  if (!app || !key || _stricmp(app, "user"))
    return false;
  return !_stricmp(key, "fullscreen") || (g_gw && (!_stricmp(key, "screenwidth") || !_stricmp(key, "screenheight")));
}
// What the Lab says for one of those
const char *oursValue(LPCSTR key, char *buf, size_t n) {
  if (!_stricmp(key, "fullscreen"))
    return "0";
  std::snprintf(buf, n, "%d", !_stricmp(key, "screenwidth") ? g_gw : g_gh);
  return buf;
}

using GetProfileFn = DWORD(WINAPI *)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
GetProfileFn realGetProfile = nullptr;
int g_reads = 0;
DWORD WINAPI getProfile(LPCSTR app, LPCSTR key, LPCSTR def, LPSTR out, DWORD size, LPCSTR file) {
  if (ours(app, key) && out && size > 1) {
    char buf[16];
    const char *v = oursValue(key, buf, sizeof buf);
    lstrcpynA(out, v, size);
    say("ini [%s] %s = %s (the Lab's)", app, key, v);
    return static_cast<DWORD>(std::strlen(out));
  }
  DWORD n = realGetProfile(app, key, def, out, size, file);
  if (g_reads++ < 400)
    say("ini [%s] %s = %s", app ? app : "-", key ? key : "-", out && key ? out : "(list)");
  return n;
}

using WriteProfileFn = BOOL(WINAPI *)(LPCSTR, LPCSTR, LPCSTR, LPCSTR);
WriteProfileFn realWriteProfile = nullptr;
BOOL WINAPI writeProfile(LPCSTR app, LPCSTR key, LPCSTR value, LPCSTR file) {
  if (ours(app, key)) {
    say("ini write [%s] %s = %s: kept out of zoo.ini", app, key, value ? value : "-");
    return TRUE;
  }
  return realWriteProfile(app, key, value, file);
}

// ---------------------------------------------------------------- windows
using CreateWindowFn = HWND(WINAPI *)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
CreateWindowFn realCreateWindow = nullptr;
// In the Lab, its window's look: a borderless popup (the Lab's page is drawn
// over child windows), not on the taskbar, never topmost
DWORD embedStyle(DWORD style) { return (style & (WS_VISIBLE | WS_DISABLED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)) | WS_POPUP; }
DWORD embedExStyle(DWORD ex) { return (ex | WS_EX_TOOLWINDOW) & ~(WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME); }
// Where it goes: the middle of the Play view (its size is the game's)
POINT placed() {
  POINT p{g_rx, g_ry};
  if (g_rw > g_gw)
    p.x += (g_rw - g_gw) / 2;
  if (g_rh > g_gh)
    p.y += (g_rh - g_gh) / 2;
  return p;
}
HWND WINAPI createWindow(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
  char name[64] = "#atom";
  if (cls && HIWORD(reinterpret_cast<ULONG_PTR>(cls)))
    lstrcpynA(name, cls, sizeof name);
  // (the game's own window: class Zoo, made by GXVidMan at start-up)
  bool game = !g_game && !parent && !std::strcmp(name, "Zoo");
  if (game && g_owner) {
    say("window: the game's, %d,%d %dx%d style %08lx ex %08lx - into the Lab", x, y, w, h, style, ex);
    g_gw = w;
    g_gh = h;
    POINT p = placed();
    style = embedStyle(style) & ~WS_VISIBLE;
    ex = embedExStyle(ex);
    x = p.x;
    y = p.y;
    parent = g_owner; // (a popup's parent is its owner)
  }
  HWND hwnd = realCreateWindow(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
  if (game) {
    g_game = hwnd;
    // (ours in front of its window procedure: it keeps running in the Lab)
    if (g_owner && hwnd) {
      g_gameProc = reinterpret_cast<WNDPROC>(GetWindowLongA(hwnd, GWLP_WNDPROC));
      SetWindowLongA(hwnd, GWLP_WNDPROC, static_cast<LONG>(reinterpret_cast<LONG_PTR>(gameProc)));
    }
  }
  say("window %p: class %s, title \"%s\", style %08lx ex %08lx, %d,%d %dx%d, parent %p", hwnd, name, title ? title : "", style, ex, x, y, w, h, parent);
  return hwnd;
}

// It may size, move and restyle its window itself (going windowed does):
// in the Lab, the size stays the game's, the place the Lab's, the look ours
using SetWindowPosFn = BOOL(WINAPI *)(HWND, HWND, int, int, int, int, UINT);
SetWindowPosFn realSetWindowPos = nullptr;
BOOL WINAPI setWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags) {
  if (g_owner && hwnd && hwnd == g_game) {
    say("game SetWindowPos %d,%d %dx%d flags %x: kept in the Lab", x, y, w, h, flags);
    POINT p = placed();
    flags |= SWP_NOZORDER | SWP_NOACTIVATE;
    if (g_hidden)
      flags = (flags & ~SWP_SHOWWINDOW);
    return realSetWindowPos(hwnd, after, p.x, p.y, g_gw, g_gh, flags & ~(SWP_NOMOVE | SWP_NOSIZE));
  }
  return realSetWindowPos(hwnd, after, x, y, w, h, flags);
}
// In the Lab it keeps running when it isn't the window in front (the Lab
// is, mostly): left to itself it pauses when it's put in the background - a
// fullscreen game's habit - so its window's told it's still active
// (WM_ACTIVATEAPP, WM_ACTIVATE, WM_NCACTIVATE; WM_KILLFOCUS isn't passed on)
// and GetForegroundWindow / GetActiveWindow answer its own window
LRESULT CALLBACK gameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (g_owner) {
    if (msg == WM_ACTIVATEAPP)
      wp = TRUE;
    else if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE)
      wp = MAKEWPARAM(WA_ACTIVE, HIWORD(wp));
    else if (msg == WM_NCACTIVATE)
      wp = TRUE;
    else if (msg == WM_KILLFOCUS)
      return 0;
  }
  return CallWindowProcA(g_gameProc, hwnd, msg, wp, lp);
}
using HwndFn = HWND(WINAPI *)();
HwndFn realGetForeground = nullptr, realGetActive = nullptr;
HWND WINAPI getForeground() { return g_owner && g_game ? g_game : realGetForeground(); }
HWND WINAPI getActive() { return g_owner && g_game ? g_game : realGetActive(); }

using SetWindowLongFn = LONG(WINAPI *)(HWND, int, LONG);
SetWindowLongFn realSetWindowLong = nullptr;
LONG WINAPI setWindowLong(HWND hwnd, int index, LONG value) {
  if (g_owner && hwnd && hwnd == g_game && index == GWLP_WNDPROC) {
    // (its own window procedure, behind ours: the one it had back - not
    // ours, so it never calls itself through us)
    WNDPROC before = g_gameProc;
    g_gameProc = reinterpret_cast<WNDPROC>(value);
    realSetWindowLong(hwnd, index, static_cast<LONG>(reinterpret_cast<LONG_PTR>(gameProc)));
    say("game's window procedure %08lx (ours in front)", value);
    return static_cast<LONG>(reinterpret_cast<LONG_PTR>(before));
  }
  if (g_owner && hwnd && hwnd == g_game) {
    if (index == GWL_STYLE)
      value = static_cast<LONG>(embedStyle(static_cast<DWORD>(value)));
    else if (index == GWL_EXSTYLE)
      value = static_cast<LONG>(embedExStyle(static_cast<DWORD>(value)));
    else if (index == GWLP_HWNDPARENT)
      value = static_cast<LONG>(reinterpret_cast<LONG_PTR>(g_owner));
    if (index == GWL_STYLE || index == GWL_EXSTYLE || index == GWLP_HWNDPARENT)
      say("game SetWindowLong %d = %08lx (kept ours)", index, value);
  }
  return realSetWindowLong(hwnd, index, value);
}
// (borderless: its window is exactly the size it draws)
using AdjustRectFn = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD);
AdjustRectFn realAdjustRect = nullptr;
BOOL WINAPI adjustRect(LPRECT r, DWORD style, BOOL menu, DWORD ex) {
  if (g_owner)
    return TRUE;
  return realAdjustRect(r, style, menu, ex);
}

// ------------------------------------------------------------- DirectDraw
// The surface the game draws on: in windowed, what it Blts to the screen's
// (the primary) each frame
// (IDirectDrawSurface4 and 7 are the same as far as Blt, GetSurfaceDesc,
// Lock and Unlock go: both are used as 7s here)
IDirectDrawSurface7 *g_primary = nullptr, *g_back = nullptr;
DWORD g_descSize = sizeof(DDSURFACEDESC2);
int g_blts = 0;
void *patchVtable(void *object, int index, void *hook);
// The game's DirectDraw is an IDirectDraw4 or 7: each version has its own
// table of functions (vtable), so its own hooks and its own originals
template <int V> struct Ddraw {
  using BltFn = HRESULT(STDMETHODCALLTYPE *)(IDirectDrawSurface7 *, LPRECT, IDirectDrawSurface7 *, LPRECT, DWORD, LPDDBLTFX);
  using CreateSurfaceFn = HRESULT(STDMETHODCALLTYPE *)(IDirectDraw7 *, LPDDSURFACEDESC2, LPDIRECTDRAWSURFACE7 *, IUnknown *);
  static inline BltFn realBlt = nullptr;
  static inline CreateSurfaceFn realCreateSurface = nullptr;

  // A frame to the screen (the primary): what it's from is what it draws on
  static HRESULT STDMETHODCALLTYPE blt(IDirectDrawSurface7 *self, LPRECT dst, IDirectDrawSurface7 *src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx) {
    if (self == g_primary && src) {
      g_back = src;
      if (g_blts++ < 3)
        say("frame to the screen: %ld,%ld-%ld,%ld from %p %ld,%ld-%ld,%ld flags %lx", dst ? dst->left : -1, dst ? dst->top : -1, dst ? dst->right : -1,
            dst ? dst->bottom : -1, src, srcRect ? srcRect->left : -1, srcRect ? srcRect->top : -1, srcRect ? srcRect->right : -1,
            srcRect ? srcRect->bottom : -1, flags);
    }
    return realBlt(self, dst, src, srcRect, flags, fx);
  }
  static HRESULT STDMETHODCALLTYPE createSurface(IDirectDraw7 *self, LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE7 *out, IUnknown *outer) {
    HRESULT hr = realCreateSurface(self, desc, out, outer);
    if (SUCCEEDED(hr) && out && *out && desc) {
      DDSURFACEDESC2 got{};
      got.dwSize = desc->dwSize <= sizeof got ? desc->dwSize : sizeof got;
      g_descSize = got.dwSize;
      (*out)->GetSurfaceDesc(&got);
      say("surface %p (DirectDraw %d): caps %lx, %lux%lu, %lu bits (r %lx g %lx b %lx)", *out, V, desc->ddsCaps.dwCaps, got.dwWidth, got.dwHeight,
          got.ddpfPixelFormat.dwRGBBitCount, got.ddpfPixelFormat.dwRBitMask, got.ddpfPixelFormat.dwGBitMask, got.ddpfPixelFormat.dwBBitMask);
      if (desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) {
        g_primary = *out;
        if (void *old = patchVtable(*out, 5, reinterpret_cast<void *>(blt))) // (Blt)
          realBlt = reinterpret_cast<BltFn>(old);
      }
    }
    return hr;
  }
  // Its CreateSurface (the 7th entry, in every version) hooked
  static void hook(void *dd) {
    if (void *old = patchVtable(dd, 6, reinterpret_cast<void *>(createSurface)))
      realCreateSurface = reinterpret_cast<CreateSurfaceFn>(old);
  }
  // One made with CoCreateInstance has a stand-in table of functions until
  // its Initialize (the 19th entry) is called: the real one's hooked then
  using InitFn = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, GUID *);
  static inline InitFn realInit = nullptr;
  static HRESULT STDMETHODCALLTYPE init(IUnknown *self, GUID *guid) {
    HRESULT hr = realInit(self, guid);
    say("its IDirectDraw%d Initialize: %08lx", V, hr);
    if (SUCCEEDED(hr))
      hook(self);
    return hr;
  }
  static void hookInit(void *dd) {
    if (void *old = patchVtable(dd, 18, reinterpret_cast<void *>(init)))
      realInit = reinterpret_cast<InitFn>(old);
  }
};
// A vtable entry of a COM object (shared by every object of its kind) now
// ours; the old one back
void *patchVtable(void *object, int index, void *hook) {
  void **vt = *reinterpret_cast<void ***>(object);
  void *old = vt[index];
  if (old == hook)
    return nullptr;
  DWORD prot;
  VirtualProtect(&vt[index], sizeof(void *), PAGE_READWRITE, &prot);
  vt[index] = hook;
  VirtualProtect(&vt[index], sizeof(void *), prot, &prot);
  return old;
}
using DDCreateExFn = HRESULT(WINAPI *)(GUID *, LPVOID *, REFIID, IUnknown *);
DDCreateExFn realDDCreateEx = nullptr;
HRESULT WINAPI ddCreateEx(GUID *guid, LPVOID *dd, REFIID iid, IUnknown *outer) {
  HRESULT hr = realDDCreateEx(guid, dd, iid, outer);
  say("DirectDrawCreateEx: %08lx", hr);
  if (SUCCEEDED(hr) && dd && *dd && IsEqualIID(iid, IID_IDirectDraw7))
    Ddraw<7>::hook(*dd);
  return hr;
}
// zoo.exe loads DDRAW.DLL itself and asks it for DirectDrawCreateEx
// (GetProcAddress): that's where ours goes in
using GetProcFn = FARPROC(WINAPI *)(HMODULE, LPCSTR);
GetProcFn realGetProc = nullptr;
FARPROC WINAPI getProc(HMODULE m, LPCSTR name) {
  FARPROC p = realGetProc(m, name);
  if (!name || !HIWORD(reinterpret_cast<ULONG_PTR>(name)))
    return p;
  if (p && !std::strcmp(name, "DirectDrawCreateEx")) {
    say("GetProcAddress DirectDrawCreateEx: ours");
    realDDCreateEx = reinterpret_cast<DDCreateExFn>(p);
    return reinterpret_cast<FARPROC>(ddCreateEx);
  }
  return p;
}

// ------------------------------------------------------------- DirectPlay
// Windows asks to install DirectPlay (a "Windows Features" prompt) when a
// program first makes one of its objects. zoo.exe makes them through COM:
// any class served by a DirectPlay DLL (dplayx, dpnet, dpvoice...) is
// answered "not there", and the game goes on without
bool directPlayClass(REFCLSID id, char *server, DWORD n) {
  wchar_t g[64];
  StringFromGUID2(id, g, 64);
  char key[128];
  std::snprintf(key, sizeof key, "CLSID\\%ls\\InprocServer32", g);
  server[0] = 0;
  LONG len = static_cast<LONG>(n);
  if (RegQueryValueA(HKEY_CLASSES_ROOT, key, server, &len) != ERROR_SUCCESS)
    return false;
  const char *slash = std::strrchr(server, '\\');
  const char *base = slash ? slash + 1 : server;
  return !_strnicmp(base, "dplay", 5) || !_strnicmp(base, "dpn", 3) || !_strnicmp(base, "dpv", 3) || !_strnicmp(base, "dpwsock", 7);
}
using CoCreateFn = HRESULT(WINAPI *)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
CoCreateFn realCoCreate = nullptr;
HRESULT WINAPI coCreate(REFCLSID id, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *out) {
  char server[MAX_PATH];
  bool dp = directPlayClass(id, server, sizeof server);
  wchar_t g[64];
  StringFromGUID2(id, g, 64);
  if (dp) {
    say("CoCreateInstance %ls (%s): DirectPlay - not made", g, server);
    if (out)
      *out = nullptr;
    return REGDB_E_CLASSNOTREG;
  }
  HRESULT hr = realCoCreate(id, outer, ctx, iid, out);
  say("CoCreateInstance %ls (%s): %08lx", g, server[0] ? server : "?", hr);
  // (the game's DirectDraw, the one it draws with: GXVidMan makes it this
  // way - CoCreateInstance, then Initialize on the same interface (zoo.exe
  // 0x533d0a) - not with DirectDrawCreateEx. Its CreateSurface is hooked
  // once it's initialized - its surfaces, for shot)
  if (SUCCEEDED(hr) && out && *out && (IsEqualCLSID(id, CLSID_DirectDraw) || IsEqualCLSID(id, CLSID_DirectDraw7))) {
    if (IsEqualIID(iid, IID_IDirectDraw7))
      Ddraw<7>::hookInit(*out);
    else if (IsEqualIID(iid, IID_IDirectDraw4))
      Ddraw<4>::hookInit(*out);
    StringFromGUID2(iid, g, 64);
    say("its DirectDraw: interface %ls", g);
  }
  return hr;
}
// (and each DLL it loads by name, for the log - DirectPlay's not loaded.
// zoo.exe's DirectX version check loads dpnhpast.dll, DirectPlay's NAT
// helper, only to tell 8.1 from 8.0 - that's what made Windows ask: not
// there, it's 8.0, as when the prompt's skipped)
using LoadLibFn = HMODULE(WINAPI *)(LPCSTR);
LoadLibFn realLoadLib = nullptr;
HMODULE WINAPI loadLib(LPCSTR name) {
  const char *slash = name ? std::strrchr(name, '\\') : nullptr;
  const char *base = slash ? slash + 1 : name ? name : "";
  if (!_strnicmp(base, "dplay", 5) || !_strnicmp(base, "dpn", 3) || !_strnicmp(base, "dpv", 3)) {
    say("LoadLibrary %s: DirectPlay - not loaded", name);
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
  }
  HMODULE m = realLoadLib(name);
  say("LoadLibrary %s: %p", name ? name : "-", m);
  return m;
}

// A picture of the game's surface as a .bmp (24-bit): whatever its pixel
// format, from its masks
bool shot(const char *file) {
  IDirectDrawSurface7 *s = g_back;
  if (!s)
    return false;
  DDSURFACEDESC2 d{};
  d.dwSize = g_descSize;
  if (FAILED(s->Lock(nullptr, &d, DDLOCK_WAIT | DDLOCK_READONLY | DDLOCK_SURFACEMEMORYPTR, nullptr)))
    return false;
  const int w = static_cast<int>(d.dwWidth), h = static_cast<int>(d.dwHeight), bpp = static_cast<int>(d.ddpfPixelFormat.dwRGBBitCount);
  const DWORD masks[3] = {d.ddpfPixelFormat.dwRBitMask, d.ddpfPixelFormat.dwGBitMask, d.ddpfPixelFormat.dwBBitMask};
  const int row = (w * 3 + 3) & ~3;
  std::vector<BYTE> px(static_cast<size_t>(row) * h);
  for (int y = 0; y < h; y++) {
    const BYTE *line = static_cast<const BYTE *>(d.lpSurface) + static_cast<size_t>(y) * d.lPitch;
    BYTE *outRow = px.data() + static_cast<size_t>(h - 1 - y) * row;
    for (int x = 0; x < w; x++) {
      DWORD v = bpp == 16 ? reinterpret_cast<const WORD *>(line)[x] : bpp == 32 ? reinterpret_cast<const DWORD *>(line)[x] : 0;
      for (int c = 0; c < 3; c++) {
        DWORD m = masks[c];
        if (!m)
          continue;
        unsigned long lo = 0, hi = 0;
        _BitScanForward(&lo, m);
        _BitScanReverse(&hi, m);
        DWORD val = (v & m) >> lo, top = m >> lo;
        outRow[x * 3 + (2 - c)] = static_cast<BYTE>(val * 255 / (top ? top : 1));
        (void)hi;
      }
    }
  }
  s->Unlock(nullptr);
  FILE *f = std::fopen(file, "wb");
  if (!f)
    return false;
  BITMAPFILEHEADER fh{};
  BITMAPINFOHEADER ih{};
  fh.bfType = 0x4d42;
  fh.bfOffBits = sizeof fh + sizeof ih;
  fh.bfSize = fh.bfOffBits + static_cast<DWORD>(px.size());
  ih.biSize = sizeof ih;
  ih.biWidth = w;
  ih.biHeight = h;
  ih.biPlanes = 1;
  ih.biBitCount = 24;
  std::fwrite(&fh, sizeof fh, 1, f);
  std::fwrite(&ih, sizeof ih, 1, f);
  std::fwrite(px.data(), 1, px.size(), f);
  std::fclose(f);
  return true;
}

// ------------------------------------------------------------ what runs
// The Lab's recorder: which of zoo.exe's functions run while something's
// done in the game. "arm <file>" (an address a line, the functions' entries
// from Ghidra) puts a breakpoint (int3) on each one's first byte; the first
// time one runs, its byte goes back, the game carries on from it as if
// nothing happened, and "hit <function> <called from> <ms>" goes to the Lab.
// One-shot: once warmed up the game runs at full speed. "disarm" puts every
// byte still armed back
CRITICAL_SECTION g_armLock;
std::vector<std::pair<DWORD, BYTE>> g_armed; // (address, its own byte), sorted
size_t g_armedLeft = 0;
DWORD g_codeLo = 0, g_codeHi = 0;
std::string g_hits;
CRITICAL_SECTION g_hitsLock;

// zoo.exe's code (its executable sections), made writable for the int3s
void findCode() {
  auto *base = reinterpret_cast<BYTE *>(GetModuleHandleA(nullptr));
  auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
  auto *sec = IMAGE_FIRST_SECTION(nt);
  for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
    if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
      continue;
    DWORD lo = reinterpret_cast<DWORD>(base) + sec->VirtualAddress, hi = lo + sec->Misc.VirtualSize;
    if (!g_codeLo || lo < g_codeLo)
      g_codeLo = lo;
    if (hi > g_codeHi)
      g_codeHi = hi;
    DWORD old;
    VirtualProtect(reinterpret_cast<void *>(lo), sec->Misc.VirtualSize, PAGE_EXECUTE_READWRITE, &old);
  }
  say("code %08lx-%08lx", g_codeLo, g_codeHi);
}

void disarm() {
  EnterCriticalSection(&g_armLock);
  for (auto &[at, byte] : g_armed)
    if (*reinterpret_cast<BYTE *>(at) == 0xCC)
      *reinterpret_cast<BYTE *>(at) = byte;
  g_armed.clear();
  g_armedLeft = 0;
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  LeaveCriticalSection(&g_armLock);
}

int arm(const char *file) {
  if (!g_codeLo)
    findCode();
  FILE *f = std::fopen(file, "r");
  if (!f)
    return -1;
  std::vector<DWORD> want;
  char line[64];
  while (std::fgets(line, sizeof line, f)) {
    DWORD a = std::strtoul(line, nullptr, 16);
    if (a >= g_codeLo && a < g_codeHi)
      want.push_back(a);
  }
  std::fclose(f);
  disarm();
  EnterCriticalSection(&g_armLock);
  std::sort(want.begin(), want.end());
  want.erase(std::unique(want.begin(), want.end()), want.end());
  for (DWORD a : want) {
    BYTE b = *reinterpret_cast<BYTE *>(a);
    if (b == 0xCC) // (padding, or not code: left alone)
      continue;
    g_armed.emplace_back(a, b);
    *reinterpret_cast<BYTE *>(a) = 0xCC;
  }
  g_armedLeft = g_armed.size();
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  LeaveCriticalSection(&g_armLock);
  return static_cast<int>(g_armed.size());
}

// The ones that have run armed again (their int3 back): the Lab's live steps
// call it every second, so each function shows each time it runs - at most
// once a second - not just the first time
int rearm() {
  EnterCriticalSection(&g_armLock);
  int n = 0;
  for (const auto &x : g_armed) {
    BYTE *at = reinterpret_cast<BYTE *>(x.first);
    if (*at != 0xCC) {
      *at = 0xCC;
      n++;
    }
  }
  g_armedLeft += n;
  if (n)
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  LeaveCriticalSection(&g_armLock);
  return n;
}

// The int3 of one that's run: its byte back, noted, and on from its start
LONG CALLBACK onHit(EXCEPTION_POINTERS *e) {
  if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
    return EXCEPTION_CONTINUE_SEARCH;
  DWORD at = reinterpret_cast<DWORD>(e->ExceptionRecord->ExceptionAddress);
  if (at < g_codeLo || at >= g_codeHi)
    return EXCEPTION_CONTINUE_SEARCH;
  EnterCriticalSection(&g_armLock);
  auto it = std::lower_bound(g_armed.begin(), g_armed.end(), std::make_pair(at, BYTE{0}),
                             [](const auto &x, const auto &y) { return x.first < y.first; });
  bool ours = it != g_armed.end() && it->first == at;
  bool first = ours && *reinterpret_cast<BYTE *>(at) == 0xCC;
  if (first) {
    *reinterpret_cast<BYTE *>(at) = it->second;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(at), 1);
    g_armedLeft--;
  }
  LeaveCriticalSection(&g_armLock);
  if (!ours)
    return EXCEPTION_CONTINUE_SEARCH;
  if (first) {
    // (at a function's first byte, the top of the stack is where it was
    // called from)
    DWORD from = 0;
    if (!IsBadReadPtr(reinterpret_cast<void *>(e->ContextRecord->Esp), 4))
      from = *reinterpret_cast<DWORD *>(e->ContextRecord->Esp);
    char line[64];
    std::snprintf(line, sizeof line, "hit %06lx %06lx %lu\n", at, from, GetTickCount());
    EnterCriticalSection(&g_hitsLock);
    g_hits += line;
    LeaveCriticalSection(&g_hitsLock);
  }
  // (another thread's hit it first: its byte's back already - on as well)
  e->ContextRecord->Eip = at;
  return EXCEPTION_CONTINUE_EXECUTION;
}

// ------------------------------------------------------------ its memory
// Where the game keeps a value - its cash, a count - found the way a cheat
// finder does: "scan <int|float|double> <value>" lists every place in its
// memory holding it, "rescan <value>" keeps those that hold the new one (spend
// some, rescan), "peek <address> <bytes>" reads, and "watch <address>" puts a
// hardware watchpoint on it: each instruction that writes it is reported once,
// with the registers (the object it writes through is in one: [esi+0x...]),
// "unwatch" takes it off
char g_scanType = 'i';
std::vector<DWORD> g_found;
void reply(const char *fmt, ...);

bool sameValue(DWORD at, char type, double v) {
  switch (type) {
  case 'f':
    return *reinterpret_cast<const float *>(at) == static_cast<float>(v);
  case 'd':
    return *reinterpret_cast<const double *>(at) == v;
  default:
    return *reinterpret_cast<const int *>(at) == static_cast<int>(v);
  }
}
// (one region: a page can go while it's read - the game freeing it - and
// that's just skipped)
void scanRegion(DWORD lo, DWORD hi, char type, double v, std::vector<DWORD> &out) {
  __try {
    const DWORD step = type == 'd' ? 8 : 4, size = step;
    for (DWORD at = lo; at + size <= hi; at += 4)
      if (sameValue(at, type, v))
        out.push_back(at);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}
size_t scan(char type, double v) {
  g_scanType = type;
  g_found.clear();
  MEMORY_BASIC_INFORMATION mi;
  HMODULE self = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&scan), &self);
  for (DWORD at = 0x10000; at < 0x7ffe0000 && VirtualQuery(reinterpret_cast<void *>(at), &mi, sizeof mi); at += static_cast<DWORD>(mi.RegionSize)) {
    const DWORD rw = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mi.State != MEM_COMMIT || !(mi.Protect & rw) || (mi.Protect & PAGE_GUARD) || mi.AllocationBase == self)
      continue;
    scanRegion(reinterpret_cast<DWORD>(mi.BaseAddress), reinterpret_cast<DWORD>(mi.BaseAddress) + static_cast<DWORD>(mi.RegionSize), type, v, g_found);
  }
  return g_found.size();
}
bool stillValue(DWORD at, double v) {
  __try {
    return sameValue(at, g_scanType, v);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
size_t rescan(double v) {
  std::vector<DWORD> keep;
  for (DWORD at : g_found)
    if (stillValue(at, v))
      keep.push_back(at);
  g_found.swap(keep);
  return g_found.size();
}
// "scan" and "rescan"'s answer: how many, and the first 40 (with what
// they're in: zoo.exe's own data, or the heap)
void replyFound(const char *what) {
  std::string line = std::string(what) + " " + std::to_string(g_found.size());
  char buf[48];
  for (size_t i = 0; i < g_found.size() && i < 40; i++) {
    std::snprintf(buf, sizeof buf, " %06lx", g_found[i]);
    line += buf;
  }
  reply("%s", line.c_str());
}
bool peek(DWORD at, DWORD n, std::string &hex) {
  __try {
    char b[4];
    for (DWORD i = 0; i < n; i++) {
      std::snprintf(b, sizeof b, "%02x", reinterpret_cast<const BYTE *>(at)[i]);
      hex += b;
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// A list of objects at once: "each <list> <offset>..." - list is where a
// begin/end pair of pointers sits (a std::vector of object pointers, as the
// world's entities at [0x638040]+0x80) - each object's address and the 4
// bytes at each offset in it: "each <n> <object>:<v1>,<v2>... ..." (at most
// 2000 objects)
// (read: each object, then its values - no strings made in here, where a
// fault is caught)
bool eachRead(DWORD list, const std::vector<DWORD> &offs, std::vector<DWORD> &vals) {
  __try {
    DWORD *p = *reinterpret_cast<DWORD **>(list), *end = *reinterpret_cast<DWORD **>(list + 4);
    if (end < p || end - p > 2000)
      return false;
    for (; p != end; p++) {
      DWORD obj = *p;
      vals.push_back(obj);
      for (DWORD off : offs)
        vals.push_back(obj && !IsBadReadPtr(reinterpret_cast<void *>(obj + off), 4) ? *reinterpret_cast<DWORD *>(obj + off) : 0);
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
bool eachObject(DWORD list, const std::vector<DWORD> &offs, std::string &out) {
  std::vector<DWORD> vals;
  if (!eachRead(list, offs, vals))
    return false;
  const size_t per = offs.size() + 1;
  out = "each " + std::to_string(vals.size() / per);
  char b[16];
  for (size_t i = 0; i < vals.size(); i++) {
    size_t k = i % per;
    std::snprintf(b, sizeof b, k == 0 ? " %lx:" : k == 1 ? "%lx" : ",%lx", vals[i]);
    out += b;
  }
  return true;
}

// The text an object points at: "strs <address> <bytes>" - each 4-byte field
// in it that's a pointer to readable text (3+ printable characters), and the
// text: how an object's name is found ("+0x8 lion"). Answer "strs <off>=<text>
// ..." (spaces in the text as _)
bool textAt(DWORD p, char *out, size_t n) {
  __try {
    const char *s = reinterpret_cast<const char *>(p);
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) {
      unsigned char c = static_cast<unsigned char>(s[i]);
      if (c < 0x20 || c > 0x7e)
        return false;
      out[i] = c == ' ' ? '_' : static_cast<char>(c);
    }
    out[i] = 0;
    return i >= 3;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
bool fieldAt(DWORD at, DWORD &v) {
  __try {
    v = *reinterpret_cast<const DWORD *>(at);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
std::string textsOf(DWORD at, DWORD bytes) {
  std::string out = "strs";
  char text[64], b[96];
  for (DWORD off = 0; off + 4 <= bytes && off < 0x4000; off += 4) {
    DWORD v;
    if (!fieldAt(at + off, v) || v < 0x10000 || IsBadReadPtr(reinterpret_cast<void *>(v), 4))
      continue;
    if (textAt(v, text, sizeof text)) {
      std::snprintf(b, sizeof b, " %lx=%s", off, text);
      out += b;
    }
  }
  return out;
}

// The watchpoint: debug register 0 on every thread of the game (those
// running now), on a write of 4 bytes
DWORD g_watchAt = 0;
std::vector<DWORD> g_watchSeen;
CRITICAL_SECTION g_watchLock;
void setWatch(DWORD at) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 te{};
  te.dwSize = sizeof te;
  for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
    if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId())
      continue;
    HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
    if (!t)
      continue;
    SuspendThread(t);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(t, &c)) {
      c.Dr0 = at;
      // (L0 on, RW0 = 01 writes, LEN0 = 11 four bytes) - or all off
      c.Dr7 = at ? (c.Dr7 & ~0xF0003u) | 0x1u | (0x1u << 16) | (0x3u << 18) : c.Dr7 & ~0xF0003u;
      c.Dr6 = 0;
      SetThreadContext(t, &c);
    }
    ResumeThread(t);
    CloseHandle(t);
  }
  CloseHandle(snap);
  g_watchAt = at;
}
LONG CALLBACK onWatch(EXCEPTION_POINTERS *e) {
  if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1) || !g_watchAt)
    return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT *c = e->ContextRecord;
  c->Dr6 = 0;
  // (once per instruction: the one after the write is where it stopped)
  EnterCriticalSection(&g_watchLock);
  bool fresh = std::find(g_watchSeen.begin(), g_watchSeen.end(), c->Eip) == g_watchSeen.end() && g_watchSeen.size() < 64;
  if (fresh)
    g_watchSeen.push_back(c->Eip);
  LeaveCriticalSection(&g_watchLock);
  if (fresh) {
    DWORD v = 0;
    if (!IsBadReadPtr(reinterpret_cast<void *>(g_watchAt), 4))
      v = *reinterpret_cast<DWORD *>(g_watchAt);
    DWORD from = 0;
    if (!IsBadReadPtr(reinterpret_cast<void *>(c->Ebp + 4), 4))
      from = *reinterpret_cast<DWORD *>(c->Ebp + 4);
    char line[256];
    std::snprintf(line, sizeof line, "wrote %06lx after %06lx now %08lx eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx from %06lx\n",
                  g_watchAt, c->Eip, v, c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp, from);
    EnterCriticalSection(&g_hitsLock);
    g_hits += line;
    LeaveCriticalSection(&g_hitsLock);
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

// ------------------------------------------------------------ the Lab's mouse
// The Lab clicking in the game (click/rclick/drag/move, the game's own
// pixels), without anyone's mouse: zoo.exe takes where the mouse is from
// GetCursorPos, clicks as window messages, and a held button from
// GetAsyncKeyState/GetKeyState - for a moment each answers with the Lab's
// mouse (then yours again)
POINT g_fakeAt{};
DWORD g_fakeUntil = 0;
bool g_fakeLeft = false, g_fakeRight = false;
bool faking() { return g_fakeUntil && GetTickCount() < g_fakeUntil; }

using GetCursorPosFn = BOOL(WINAPI *)(LPPOINT);
GetCursorPosFn realGetCursorPos = nullptr;
BOOL WINAPI getCursorPos(LPPOINT p) {
  if (faking() && p && g_game) {
    *p = g_fakeAt;
    ClientToScreen(g_game, p);
    return TRUE;
  }
  return realGetCursorPos(p);
}
using KeyStateFn = SHORT(WINAPI *)(int);
KeyStateFn realGetAsyncKeyState = nullptr, realGetKeyState = nullptr;
SHORT fakeButton(int vk) {
  if (vk == VK_LBUTTON)
    return g_fakeLeft ? static_cast<SHORT>(0x8000) : 0;
  if (vk == VK_RBUTTON)
    return g_fakeRight ? static_cast<SHORT>(0x8000) : 0;
  return -1;
}
SHORT WINAPI getAsyncKeyState(int vk) {
  SHORT f = faking() ? fakeButton(vk) : -1;
  return f != -1 ? f : realGetAsyncKeyState(vk);
}
SHORT WINAPI getKeyState(int vk) {
  SHORT f = faking() ? fakeButton(vk) : -1;
  return f != -1 ? f : realGetKeyState(vk);
}

void fakeMouse(int x, int y, UINT msg, WPARAM held) {
  g_fakeAt = {x, y};
  g_fakeUntil = GetTickCount() + 600;
  PostMessageA(g_game, msg, held, MAKELPARAM(x, y));
}
void labClick(int x, int y, bool right) {
  fakeMouse(x, y, WM_MOUSEMOVE, 0);
  Sleep(80);
  (right ? g_fakeRight : g_fakeLeft) = true;
  fakeMouse(x, y, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON);
  Sleep(90);
  (right ? g_fakeRight : g_fakeLeft) = false;
  fakeMouse(x, y, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0);
  Sleep(80);
}
void labDrag(int x0, int y0, int x1, int y1) {
  fakeMouse(x0, y0, WM_MOUSEMOVE, 0);
  Sleep(80);
  g_fakeLeft = true;
  fakeMouse(x0, y0, WM_LBUTTONDOWN, MK_LBUTTON);
  Sleep(90);
  const int steps = 16;
  for (int i = 1; i <= steps; i++) {
    fakeMouse(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, WM_MOUSEMOVE, MK_LBUTTON);
    Sleep(40);
  }
  Sleep(60);
  g_fakeLeft = false;
  fakeMouse(x1, y1, WM_LBUTTONUP, 0);
  Sleep(80);
}

// ------------------------------------------------------------ the Lab's lines
void reply(const char *fmt, ...) {
  char buf[512];
  va_list a;
  va_start(a, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, a);
  va_end(a);
  std::string line = std::string(buf) + "\n";
  DWORD n;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &n, nullptr);
}
// The Windows dialog the game has open, if any (a visible #32770 of this
// process: its file dialogs)
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
void command(const char *line) {
  int x, y, w, h;
  if (std::sscanf(line, "volume %d", &x) == 1) {
    setSessionVolume(x / 100.0f);
  } else if (std::sscanf(line, "rect %d %d %d %d", &x, &y, &w, &h) == 4) {
    g_rx = x, g_ry = y, g_rw = w, g_rh = h;
    g_hidden = false;
    if (g_game) {
      POINT p = placed();
      realSetWindowPos(g_game, nullptr, p.x, p.y, g_gw, g_gh, SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }
  } else if (!std::strncmp(line, "hide", 4)) {
    g_hidden = true;
    if (g_game)
      ShowWindow(g_game, SW_HIDE);
  } else if (!std::strncmp(line, "show", 4)) {
    g_hidden = false;
    if (g_game)
      ShowWindow(g_game, SW_SHOWNA);
  } else if (!std::strncmp(line, "focus", 5)) {
    if (g_game)
      SetForegroundWindow(g_game);
  } else if (!std::strncmp(line, "shot ", 5)) {
    reply(shot(line + 5) ? "shot ok %s" : "shot failed %s", line + 5);
  } else if (!std::strncmp(line, "dialog", 6)) {
    // Windows' own dialogs the game opens ("Save a zoo...", "Open a zoo..."):
    // they aren't drawn on its surface, and the game waits on them.
    // "dialog" names the one open, "dialog text <name>" fills its file name,
    // "dialog ok" / "dialog cancel" presses its button
    HWND d = openDialog();
    const char *arg = line + 6;
    while (*arg == ' ')
      arg++;
    if (!d) {
      reply("dialog none");
    } else if (!std::strncmp(arg, "text ", 5)) {
      HWND edit = GetDlgItem(d, 1152); // (the old-style file dialog's name box)
      if (!edit)
        edit = FindWindowExA(d, nullptr, "Edit", nullptr);
      if (edit)
        SendMessageA(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(arg + 5));
      reply(edit ? "dialog text %s" : "dialog no-text-box", arg + 5);
    } else if (!std::strncmp(arg, "press ", 6)) {
      // (a button by its text - "press Yes" on "Confirm Save As", whose
      // buttons have no fixed ids: the first whose text has it, & ignored)
      struct Want {
        const char *text;
        HWND found;
      } w{arg + 6, nullptr};
      EnumChildWindows(
          d,
          [](HWND h, LPARAM l) -> BOOL {
            auto *w = reinterpret_cast<Want *>(l);
            char cls[16] = {}, text[64] = {}, plain[64] = {};
            GetClassNameA(h, cls, sizeof cls);
            GetWindowTextA(h, text, sizeof text);
            for (char *s = text, *p = plain; *s; s++)
              if (*s != '&')
                *p++ = *s;
            if (!_stricmp(cls, "Button") && StrStrIA(plain, w->text)) {
              w->found = h;
              return FALSE;
            }
            return TRUE;
          },
          reinterpret_cast<LPARAM>(&w));
      if (w.found)
        PostMessageA(w.found, BM_CLICK, 0, 0);
      reply(w.found ? "dialog pressed %s" : "dialog no-button %s", arg + 6);
    } else if (!std::strcmp(arg, "ok") || !std::strcmp(arg, "cancel")) {
      HWND b = GetDlgItem(d, arg[0] == 'o' ? IDOK : IDCANCEL);
      if (b)
        PostMessageA(b, BM_CLICK, 0, 0);
      reply(b ? "dialog pressed %s" : "dialog no-button %s", arg);
    } else {
      char title[256] = {};
      GetWindowTextA(d, title, sizeof title);
      reply("dialog open %s", title);
    }
  } else if (std::sscanf(line, "key %d", &x) == 1) {
    // A key to the game (a virtual-key code: 27 Esc, 13 Enter...)
    if (g_game) {
      PostMessageA(g_game, WM_KEYDOWN, x, 1);
      PostMessageA(g_game, WM_KEYUP, x, 0xC0000001);
    }
    reply("done key %d", x);
  } else if (int x1, y1; std::sscanf(line, "drag %d %d %d %d", &x, &y, &x1, &y1) == 4) {
    if (g_game)
      labDrag(x, y, x1, y1);
    reply("done drag");
  } else if (std::sscanf(line, "click %d %d", &x, &y) == 2) {
    if (g_game)
      labClick(x, y, false);
    reply("done click");
  } else if (std::sscanf(line, "rclick %d %d", &x, &y) == 2) {
    if (g_game)
      labClick(x, y, true);
    reply("done rclick");
  } else if (std::sscanf(line, "move %d %d", &x, &y) == 2) {
    if (g_game)
      fakeMouse(x, y, WM_MOUSEMOVE, 0);
    reply("done move");
  } else if (char type[16], value[64]; std::sscanf(line, "scan %15s %63s", type, value) == 2) {
    scan(type[0] == 'f' ? 'f' : type[0] == 'd' ? 'd' : 'i', std::atof(value));
    replyFound("found");
  } else if (std::sscanf(line, "rescan %63s", value) == 1) {
    rescan(std::atof(value));
    replyFound("found");
  } else if (unsigned long at, n; std::sscanf(line, "peek %lx %lu", &at, &n) == 2) {
    std::string hex;
    bool ok = n <= 4096 && peek(at, n, hex);
    reply("peek %06lx %s", at, ok ? hex.c_str() : "-");
  } else if (std::sscanf(line, "strs %lx %lu", &at, &n) == 2) {
    std::string out = textsOf(at, n) + "\n";
    DWORD wrote;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out.data(), static_cast<DWORD>(out.size()), &wrote, nullptr);
  } else if (!std::strncmp(line, "each ", 5)) {
    // (each <list> <offset>...: hex)
    char *rest = nullptr;
    DWORD list = std::strtoul(line + 5, &rest, 16);
    std::vector<DWORD> offs;
    while (rest && *rest) {
      char *next = nullptr;
      DWORD o = std::strtoul(rest, &next, 16);
      if (next == rest)
        break;
      offs.push_back(o);
      rest = next;
    }
    std::string out;
    if (!eachObject(list, offs, out))
      out = "each -";
    out += "\n";
    DWORD n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out.data(), static_cast<DWORD>(out.size()), &n, nullptr);
  } else if (std::sscanf(line, "watch %lx", &at) == 1) {
    EnterCriticalSection(&g_watchLock);
    g_watchSeen.clear();
    LeaveCriticalSection(&g_watchLock);
    setWatch(at);
    reply("watching %06lx", at);
  } else if (!std::strncmp(line, "unwatch", 7)) {
    setWatch(0);
    reply("unwatched");
  } else if (!std::strncmp(line, "arm ", 4)) {
    int n = arm(line + 4);
    say("armed %d functions", n);
    reply("armed %d", n);
  } else if (!std::strncmp(line, "rearm", 5)) {
    reply("rearmed %d", rearm());
  } else if (!std::strncmp(line, "disarm", 6)) {
    disarm();
    reply("disarmed");
  } else if (!std::strncmp(line, "quit", 4)) {
    if (g_game)
      PostMessageA(g_game, WM_CLOSE, 0, 0);
  }
}
// The hits so far to the Lab, every 150 ms (not from the game's threads:
// they only add to the list)
DWORD WINAPI sendHits(LPVOID) {
  for (;;) {
    Sleep(150);
    std::string out;
    EnterCriticalSection(&g_hitsLock);
    out.swap(g_hits);
    LeaveCriticalSection(&g_hitsLock);
    if (!out.empty()) {
      DWORD n;
      WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out.data(), static_cast<DWORD>(out.size()), &n, nullptr);
    }
  }
}
DWORD WINAPI listen(LPVOID) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED); // (for volume)
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  std::string buf;
  char chunk[512];
  DWORD n;
  while (in && ReadFile(in, chunk, sizeof chunk, &n, nullptr) && n) {
    buf.append(chunk, n);
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
      if (!line.empty())
        command(line.c_str());
    }
  }
  // (the Lab gone: so is the game)
  say("the Lab's gone: closing");
  TerminateProcess(GetCurrentProcess(), 0);
  return 0;
}

// ---------------------------------------------------------------- crashes
// A fault, logged where it happened (zoo.exe's own addresses, as in the
// Lab's Original Game) and the return addresses up the stack. Only noted:
// the game's own handlers still get it
int g_faults = 0;
void where(char *out, size_t n, DWORD addr) {
  HMODULE m = nullptr;
  char file[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(addr), &m) && m) {
    GetModuleFileNameA(m, file, MAX_PATH);
    const char *slash = std::strrchr(file, '\\');
    std::snprintf(out, n, "%08lx %s+%lx", addr, slash ? slash + 1 : file, addr - reinterpret_cast<DWORD>(m));
  } else
    std::snprintf(out, n, "%08lx", addr);
}
LONG CALLBACK onFault(EXCEPTION_POINTERS *e) {
  DWORD code = e->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
      code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_PRIV_INSTRUCTION)
    return EXCEPTION_CONTINUE_SEARCH;
  if (g_faults++ >= 20)
    return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT *c = e->ContextRecord;
  char at[160];
  where(at, sizeof at, c->Eip);
  if (code == EXCEPTION_ACCESS_VIOLATION)
    say("FAULT %08lx at %s: %s %08lx", code, at, e->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
        static_cast<DWORD>(e->ExceptionRecord->ExceptionInformation[1]));
  else
    say("FAULT %08lx at %s", code, at);
  say("  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi,
      c->Ebp, c->Esp);
  // (the return addresses on the stack that point into code: what called it)
  auto *sp = reinterpret_cast<DWORD *>(c->Esp);
  int shown = 0;
  for (int i = 0; i < 256 && shown < 12; i++) {
    DWORD v;
    if (IsBadReadPtr(sp + i, 4))
      break;
    v = sp[i];
    if (v >= 0x401000 && v < 0x640000) {
      say("  called from %08lx", v);
      shown++;
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void start() {
  InitializeCriticalSection(&g_logLock);
  char path[MAX_PATH];
  if (!GetEnvironmentVariableA("ZT1_HOOK_LOG", path, MAX_PATH)) {
    GetTempPathA(MAX_PATH, path);
    lstrcatA(path, "zt1-hook.log");
  }
  g_log = std::fopen(path, "w");
  say("zt1-hook in zoo.exe (pid %lu)", GetCurrentProcessId());
  AddVectoredExceptionHandler(1, onFault);
  patchImport("kernel32.dll", "GetPrivateProfileStringA", reinterpret_cast<void *>(getProfile), reinterpret_cast<void **>(&realGetProfile));
  patchImport("kernel32.dll", "WritePrivateProfileStringA", reinterpret_cast<void *>(writeProfile), reinterpret_cast<void **>(&realWriteProfile));
  patchImport("user32.dll", "CreateWindowExA", reinterpret_cast<void *>(createWindow), reinterpret_cast<void **>(&realCreateWindow));
  patchImport("user32.dll", "SetWindowPos", reinterpret_cast<void *>(setWindowPos), reinterpret_cast<void **>(&realSetWindowPos));
  patchImport("user32.dll", "SetWindowLongA", reinterpret_cast<void *>(setWindowLong), reinterpret_cast<void **>(&realSetWindowLong));
  patchImport("user32.dll", "AdjustWindowRectEx", reinterpret_cast<void *>(adjustRect), reinterpret_cast<void **>(&realAdjustRect));
  patchImport("kernel32.dll", "GetProcAddress", reinterpret_cast<void *>(getProc), reinterpret_cast<void **>(&realGetProc));
  patchImport("kernel32.dll", "LoadLibraryA", reinterpret_cast<void *>(loadLib), reinterpret_cast<void **>(&realLoadLib));
  patchImport("ole32.dll", "CoCreateInstance", reinterpret_cast<void *>(coCreate), reinterpret_cast<void **>(&realCoCreate));
  patchImport("user32.dll", "GetCursorPos", reinterpret_cast<void *>(getCursorPos), reinterpret_cast<void **>(&realGetCursorPos));
  patchImport("user32.dll", "GetForegroundWindow", reinterpret_cast<void *>(getForeground), reinterpret_cast<void **>(&realGetForeground));
  patchImport("user32.dll", "GetActiveWindow", reinterpret_cast<void *>(getActive), reinterpret_cast<void **>(&realGetActive));
  patchImport("user32.dll", "GetAsyncKeyState", reinterpret_cast<void *>(getAsyncKeyState), reinterpret_cast<void **>(&realGetAsyncKeyState));
  patchImport("user32.dll", "GetKeyState", reinterpret_cast<void *>(getKeyState), reinterpret_cast<void **>(&realGetKeyState));
  if (!realSetWindowPos)
    realSetWindowPos = SetWindowPos;
  // In the Lab: its window, and the size it's to draw at (the Play view's)
  char v[64];
  if (GetEnvironmentVariableA("ZT1_HOOK_EMBED", v, sizeof v)) {
    g_owner = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(std::strtoull(v, nullptr, 0)));
    say("in the Lab: its window %p", g_owner);
    if (GetEnvironmentVariableA("ZT1_HOOK_SIZE", v, sizeof v) && std::sscanf(v, "%dx%d", &g_gw, &g_gh) == 2)
      say("drawing at %dx%d", g_gw, g_gh);
    else
      g_gw = g_gh = 0;
    InitializeCriticalSection(&g_armLock);
    InitializeCriticalSection(&g_hitsLock);
    InitializeCriticalSection(&g_watchLock);
    AddVectoredExceptionHandler(1, onHit);
    AddVectoredExceptionHandler(1, onWatch);
    CreateThread(nullptr, 0, sendHits, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, listen, nullptr, 0, nullptr);
  }
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE self, DWORD why, LPVOID) {
  if (why == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(self);
    start();
  }
  return TRUE;
}
