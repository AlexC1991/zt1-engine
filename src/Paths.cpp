#include "Paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include <SDL2/SDL.h>

#include "Utils.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;

namespace {

bool g_portable = true;
std::string g_app, g_game, g_data;

// ours: a Zoo Tycoon folder has at least two of these (the same test
// start.bat's setup used)
bool looksLikeGame(const fs::path &dir) {
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec))
    return false;
  int found = 0;
  for (const char *marker : {"animals.ztd", "ui.ztd", "scenery.ztd"})
    found += fs::exists(dir / marker, ec) ? 1 : 0;
  return found >= 2;
}

std::string withSlash(const fs::path &p) {
  std::string s = p.string();
#ifdef _WIN32
  const char separator = '\\';
#else
  const char separator = '/';
#endif
  if (!s.empty() && s.back() != '\\' && s.back() != '/')
    s += separator;
  return s;
}

#ifdef _WIN32
std::wstring widen(const std::string &s) {
  if (s.empty())
    return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

fs::path localAppData() {
  PWSTR p = nullptr;
  fs::path out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
    out = fs::path(p);
  CoTaskMemFree(p);
  return out;
}

// Where Zoo Tycoon's own installer says it put the game (its 32-bit key:
// Microsoft Games\Zoo Tycoon\1.0, AppPath)
fs::path fromRegistry() {
  wchar_t buf[MAX_PATH * 2] = {};
  DWORD size = sizeof(buf);
  for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
    size = sizeof(buf);
    if (RegGetValueW(root, L"SOFTWARE\\Microsoft\\Microsoft Games\\Zoo Tycoon\\1.0", L"AppPath",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buf, &size) == ERROR_SUCCESS)
      return fs::path(buf);
  }
  return {};
}

// The player picks the folder (Windows' own folder picker)
fs::path askForFolder() {
  fs::path out;
  IFileOpenDialog *dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    return out;
  DWORD opts = 0;
  dialog->GetOptions(&opts);
  dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(L"Where is Zoo Tycoon installed? (the folder with animals.ztd and zoo.exe)");
  if (SUCCEEDED(dialog->Show(nullptr))) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)))
        out = fs::path(p);
      CoTaskMemFree(p);
      item->Release();
    }
  }
  dialog->Release();
  return out;
}

int say(const std::string &text, UINT flags) {
  return MessageBoxW(nullptr, widen(text).c_str(), L"ZT1-Engine", flags | MB_SETFOREGROUND);
}
#endif

// The player's Zoo Tycoon, best guess first: ZT1_GAME_DIR, the folder they
// picked before, the registry, the usual install folders
fs::path findGame(const fs::path &remembered) {
  std::vector<fs::path> tries;
  for (const char *var : {"ZT1_GAME_DIR", "ZT1_SOURCE_DIR"})
    if (const char *v = std::getenv(var))
      tries.emplace_back(v);
  if (!remembered.empty()) {
    std::ifstream in(remembered);
    std::string line;
    if (std::getline(in, line) && !line.empty())
      tries.emplace_back(line);
  }
#ifdef _WIN32
  tries.push_back(fromRegistry());
#endif
  for (const char *d : {"C:\\Program Files (x86)\\Microsoft Games\\Zoo Tycoon",
                        "C:\\Program Files\\Microsoft Games\\Zoo Tycoon",
                        "C:\\Program Files (x86)\\EA Games\\Zoo Tycoon",
                        "C:\\Program Files (x86)\\GOG Galaxy\\Games\\Zoo Tycoon",
                        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Zoo Tycoon",
                        "C:\\GOG Games\\Zoo Tycoon"})
    tries.emplace_back(d);
  for (const fs::path &t : tries)
    if (looksLikeGame(t))
      return t;
  return {};
}

// ours: a first zoo.ini for an installed engine - the original's search order
// for the game's archives (patches and expansions first, the base game
// last), and a 1280 x 720 window
const char *kDefaultZooIni =
    "[resource]\r\n"
    "path=./dlupdate;./updates;./xpack2/loc;./xpack2;./dupdate;./zupdate1;./xpack1/loc;./xpack1;./zupdate;./loc;.\r\n"
    "\r\n[ui]\r\nnoMenuMusic=0\r\nmenuMusic=sounds/mainmenu.wav\r\n"
    "progressRed=255\r\nprogressGreen=200\r\nprogressBlue=0\r\n"
    "progressLeft=229\r\nprogressTop=417\r\nprogressRight=583\r\nprogressBottom=440\r\n"
    "\r\n[Paths]\r\npath=.\r\nai=ai\r\nanim=anim\r\nmaps=maps\r\nscenarios=scenarios\r\nsaves=saves\r\nzoo=zoo\r\n"
    "\r\n[lib]\r\nres=res0.dll\r\nlang=lang0.dll\r\n"
    "\r\n[user]\r\nfullscreen=0\r\nscreenwidth=1280\r\nscreenheight=720\r\ncolourdepth=32\r\nmsaa=0\r\n";

} // namespace

namespace Paths {

bool setup() {
  std::error_code ec;
  fs::path app = fs::path(Utils::getExecutableDirectory());
  fs::path cwd = fs::current_path(ec);
  g_app = withSlash(app);

  // Portable: started in a game folder (start.bat, the Lab), or the exe sits
  // in one
  if (looksLikeGame(cwd) || looksLikeGame(app)) {
    g_portable = true;
    fs::path game = looksLikeGame(cwd) ? cwd : app;
    fs::current_path(game, ec);
    g_game = withSlash(game);
    g_data = g_app;
    SDL_Log("[Paths] portable: the game's files are in %s", g_game.c_str());
    return true;
  }

  // Installed
  g_portable = false;
#ifdef _WIN32
  fs::path data = localAppData() / "zt1-engine";
#else
  const char *home = std::getenv("HOME");
  fs::path data = fs::path(home ? home : ".") / ".zt1-engine";
#endif
  fs::create_directories(data / "saves", ec);
  fs::create_directories(data / "mods", ec);
  g_data = withSlash(data);
  fs::path remembered = data / "game-folder.txt";

  fs::path game = findGame(remembered);
#ifdef _WIN32
  if (game.empty()) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    say("ZT1-Engine plays your own copy of Zoo Tycoon 1, and it couldn't find it.\n\n"
        "Next, pick the folder Zoo Tycoon is installed in: the one with animals.ztd and zoo.exe. "
        "If you installed Dinosaur Digs and Marine Mania over the base game, pick that same folder.",
        MB_OK | MB_ICONINFORMATION);
    while (game.empty()) {
      fs::path picked = askForFolder();
      if (picked.empty())
        break;
      if (looksLikeGame(picked)) {
        game = picked;
      } else if (say("That folder doesn't look like Zoo Tycoon: it needs two of animals.ztd, ui.ztd "
                     "and scenery.ztd.\n\nPick another folder?",
                     MB_YESNO | MB_ICONWARNING) != IDYES) {
        break;
      }
    }
    if (game.empty()) {
      say("ZT1-Engine needs Zoo Tycoon 1 to play: it uses the game's own art, sounds and maps, "
          "and comes with none of them.\n\nInstall Zoo Tycoon, or start ZT1-Engine again and pick its folder.",
          MB_OK | MB_ICONERROR);
      return false;
    }
  }
#endif
  if (game.empty()) {
    SDL_Log("[Paths] no Zoo Tycoon found (set ZT1_GAME_DIR)");
    return false;
  }
  {
    std::ofstream out(remembered, std::ios::trunc);
    out << game.string() << "\n";
  }
  fs::current_path(game, ec);
  g_game = withSlash(game);

  // A first zoo.ini of our own (the game's folder is never written to)
  fs::path ini = data / "zoo.ini";
  if (!fs::exists(ini, ec)) {
    std::ofstream out(ini, std::ios::binary);
    out << kDefaultZooIni;
  }
  SDL_Log("[Paths] installed: the game in %s, our things in %s", g_game.c_str(), g_data.c_str());
  return true;
}

bool portable() { return g_portable; }
std::string appDir() { return g_app; }
std::string gameDir() { return g_game; }
std::string dataDir() { return g_data; }
std::string configFile() { return g_portable ? std::string("zoo.ini") : g_data + "zoo.ini"; }
// (portable: the source checkout's saves/, two folders above build/Release,
// where it has always been. The separators here are forward slashes on every
// platform: fs::path takes them on Windows too, and a backslash is an ordinary
// character in a file name elsewhere. ours: Linux port.)
std::string scenarioProgressFile() {
  return g_portable ? std::string("../../saves/user_scenarios.json") : g_data + "saves/user_scenarios.json";
}

} // namespace Paths
