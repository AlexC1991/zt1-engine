#include "ModRepo.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include <SDL2/SDL.h>

#include "MiniJson.hpp"
#include "Mods.hpp"
#include "Paths.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ModRepo {

namespace {
using AbiFn = int (*)();
using CatalogueFn = int (*)(char *, int);
using ProgressFn = void (*)(void *, long long, long long);
using InstallFn = int (*)(const char *, const char *, const char *, ProgressFn, void *);
using ErrorFn = const char *(*)();

struct Client {
  bool tried = false, ok = false;
  CatalogueFn catalogue = nullptr;
  InstallFn install = nullptr;
  ErrorFn error = nullptr;
};
Client g_client;

std::mutex g_lock;
State g_state = State::Idle;
std::string g_status;
std::vector<Mod> g_mods;
std::atomic<double> g_progress{0.0};

// modrepo.dll beside the exe, once
Client &client() {
  if (g_client.tried)
    return g_client;
  g_client.tried = true;
#ifdef _WIN32
  std::string dll = (fs::path(Paths::appDir()) / "modrepo.dll").string();
  if (!fs::exists(dll))
    return g_client;
  HMODULE h = LoadLibraryA(dll.c_str());
  if (!h) {
    SDL_Log("[ModRepo] couldn't load %s", dll.c_str());
    return g_client;
  }
  auto abi = reinterpret_cast<AbiFn>(GetProcAddress(h, "zt1mr_abi"));
  g_client.catalogue = reinterpret_cast<CatalogueFn>(GetProcAddress(h, "zt1mr_catalogue"));
  g_client.install = reinterpret_cast<InstallFn>(GetProcAddress(h, "zt1mr_install"));
  g_client.error = reinterpret_cast<ErrorFn>(GetProcAddress(h, "zt1mr_error"));
  g_client.ok = abi && abi() == 1 && g_client.catalogue && g_client.install && g_client.error;
  SDL_Log("[ModRepo] %s", g_client.ok ? "the repository's client is here" : "modrepo.dll isn't one this engine knows");
#endif
  return g_client;
}

std::string lastError() {
  const char *e = client().error ? client().error() : nullptr;
  return e && *e ? e : "the repository couldn't be reached";
}

std::vector<Mod> parse(const std::string &text) {
  std::vector<Mod> out;
  Json j = Json::parse(text);
  for (size_t i = 0; i < j["mods"].size(); i++) {
    const Json &m = j["mods"][i];
    Mod mod;
    mod.id = m["id"].str();
    mod.name = m["name"].str(mod.id);
    mod.author = m["author"].str();
    mod.about = m["about"].str();
    mod.notice = m["notice"].str();
    mod.version = m["version"].str();
    mod.folder = m["folder"].str(mod.id);
    for (size_t k = 0; k < m["parts"].size(); k++) {
      const Json &p = m["parts"][k];
      Part part;
      part.id = p["id"].str();
      part.name = p["name"].str(part.id);
      part.size = static_cast<long long>(p["size"].num(0));
      part.files = static_cast<long long>(p["files"].num(0));
      if (!part.id.empty())
        mod.parts.push_back(part);
    }
    // (a folder name only: never a path out of the mods folder)
    if (!mod.id.empty() && !mod.folder.empty() && mod.folder.find_first_of("/\\:") == std::string::npos &&
        mod.folder != "." && mod.folder != "..")
      out.push_back(mod);
  }
  return out;
}
} // namespace

bool available() { return client().ok; }

State state() {
  if (!available())
    return State::Unavailable;
  std::lock_guard<std::mutex> g(g_lock);
  return g_state;
}

std::string status() {
  if (!available())
    return "The mod repository comes with the installed game (setup.exe); this copy hasn't got it.";
  std::lock_guard<std::mutex> g(g_lock);
  return g_status;
}

std::vector<Mod> mods() {
  std::lock_guard<std::mutex> g(g_lock);
  return g_mods;
}

double progress() { return g_progress.load(); }

void fetch() {
  if (!available())
    return;
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (g_state == State::Fetching || g_state == State::Installing)
      return;
    g_state = State::Fetching;
    g_status = "Asking the mod repository what it has...";
  }
  std::thread([] {
    std::string text;
    int n = client().catalogue(nullptr, 0);
    if (n < -1) {
      text.resize(static_cast<size_t>(-n));
      n = client().catalogue(text.data(), static_cast<int>(text.size()));
    }
    std::lock_guard<std::mutex> g(g_lock);
    if (n < 0) {
      g_state = State::Failed;
      g_status = lastError();
      return;
    }
    text.resize(static_cast<size_t>(n));
    g_mods = parse(text);
    g_state = State::Ready;
    g_status = g_mods.empty() ? "The repository has nothing yet." : "";
  }).detach();
}

void install(const std::string &mod, const std::string &part) {
  if (!available())
    return;
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (g_state == State::Fetching || g_state == State::Installing)
      return;
    g_state = State::Installing;
    g_status = "Downloading...";
  }
  g_progress = 0.0;
  std::string folder = Mods::folder();
  std::thread([mod, part, folder] {
    auto onProgress = [](void *, long long done, long long total) {
      if (total > 0)
        g_progress = static_cast<double>(done) / static_cast<double>(total);
    };
    int r = client().install(mod.c_str(), part.c_str(), folder.c_str(), onProgress, nullptr);
    std::lock_guard<std::mutex> g(g_lock);
    g_state = r == 0 ? State::Ready : State::Failed;
    g_status = r == 0 ? "Downloaded: it's in your mods, switched off. Switch it on in Your mods." : lastError();
    g_progress = r == 0 ? 1.0 : 0.0;
  }).detach();
}

std::string installedVersion(const Mod &mod, const Part &part) {
  std::error_code ec;
  fs::path marker = fs::path(Mods::folder()) / mod.folder / ".modrepo" / (part.id + ".json");
  if (!fs::exists(marker, ec))
    return "";
  std::ifstream in(marker);
  std::stringstream ss;
  ss << in.rdbuf();
  return Json::parse(ss.str())["version"].str("?");
}

} // namespace ModRepo
