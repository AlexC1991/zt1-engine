#include "Lab.hpp"

#include <array>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <set>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

#include "SDL_image.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include "Animals.hpp"
#include "Animation.hpp"
#include "Paths.hpp"
#include "SaveGame.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"
#include "EngineScripts.hpp"
#include "Fences.hpp"
#include "Goals.hpp"
#include "HdArt.hpp"
#include "IniReader.hpp"
#include "Mods.hpp"
#include "ShaderMods.hpp"
#include "ItemCatalog.hpp"
#include "LabTrace.hpp"
#include "OriginalConstants.hpp"
#include "MiniJson.hpp"
#include "Pathfinder.hpp"
#include "PlacedObjects.hpp"
#include "Research.hpp"
#include "ResourceManager.hpp"
#include "ScenarioManager.hpp"
#include "World.hpp"
#include "lab_api.h"

// (after the engine's headers: windows.h defines near, far, min, max)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <SDL2/SDL_syswm.h>
#undef near

extern float g_ZoomLevel;

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- the repo
fs::path findRepo() {
  std::vector<fs::path> starts = {fs::current_path()};
  if (char *base = SDL_GetBasePath()) {
    starts.push_back(fs::path(base));
    SDL_free(base);
  }
  for (fs::path p : starts)
    for (int i = 0; i < 8 && !p.empty(); i++, p = p.parent_path())
      if (fs::exists(p / "re" / "lab" / "lab_api.h"))
        return p;
  return {};
}

std::string readFile(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

unsigned parseAddr(const std::string &s) {
  return static_cast<unsigned>(std::strtoul(s.c_str(), nullptr, 16));
}

std::string hex(unsigned v) {
  char b[16];
  snprintf(b, sizeof b, "0x%06x", v);
  return b;
}

// ---------------------------------------------------------- zoo.exe itself
struct Exe {
  std::vector<unsigned char> data;
  struct Section {
    unsigned va, size, raw, rawSize;
  };
  std::vector<Section> sections;
  unsigned base = 0x400000;

  bool load(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
      return false;
    data.assign(std::istreambuf_iterator<char>(f), {});
    if (data.size() < 0x400)
      return false;
    unsigned pe = u32file(0x3c);
    unsigned nsec = data[pe + 6] | (data[pe + 7] << 8);
    unsigned optSize = data[pe + 20] | (data[pe + 21] << 8);
    base = u32file(pe + 24 + 28);
    unsigned sec = pe + 24 + optSize;
    for (unsigned i = 0; i < nsec; i++, sec += 40)
      sections.push_back({u32file(sec + 12), u32file(sec + 8), u32file(sec + 20), u32file(sec + 16)});
    return true;
  }
  unsigned u32file(unsigned off) const {
    return off + 4 <= data.size() ? data[off] | (data[off + 1] << 8) | (data[off + 2] << 16) | (data[off + 3] << 24)
                                  : 0;
  }
  const unsigned char *at(unsigned va, unsigned n) const {
    unsigned rva = va - base;
    for (const Section &s : sections)
      if (rva >= s.va && rva + n <= s.va + std::max(s.size, s.rawSize)) {
        unsigned off = s.raw + (rva - s.va);
        return off + n <= data.size() ? &data[off] : nullptr;
      }
    return nullptr;
  }
};

// -------------------------------------------------- the knowledge base
struct KFunction {
  unsigned address;
  std::string name, cls, summary, comment, by, confidence;
  bool provisional = false;
  std::vector<std::string> engine;
};
struct KField {
  unsigned offset;
  std::string name, type, summary, by;
};
struct KStruct {
  std::string name, summary;
  std::vector<KField> fields;
};
struct Knowledge {
  std::vector<KFunction> functions;
  std::vector<KStruct> structs;
  std::vector<std::pair<int, std::string>> constants;
  // engine_refs.csv: address -> (file:line, comment)
  std::multimap<unsigned, std::pair<std::string, std::string>> refs;

  void load(const fs::path &repo) {
    functions.clear();
    structs.clear();
    constants.clear();
    refs.clear();
    Json db = Json::parse(readFile(repo / "re" / "knowledge.json"));
    for (const auto &[key, e] : db["functions"].fields) {
      KFunction f;
      f.address = parseAddr(key);
      f.name = e["name"].str();
      f.cls = e["class"].str();
      f.summary = e["summary"].str();
      f.comment = e["comment"].str();
      f.by = e["by"].str();
      f.confidence = e["confidence"].str();
      f.provisional = e["provisional"].truthy();
      for (const Json &x : e["engine"].items)
        f.engine.push_back(x.str());
      functions.push_back(f);
    }
    for (const auto &[name, s] : db["structs"].fields) {
      KStruct st{name, s["summary"].str(), {}};
      for (const auto &[off, fl] : s["fields"].fields)
        st.fields.push_back({parseAddr(off), fl["name"].str(), fl["type"].str(), fl["summary"].str(), fl["by"].str()});
      std::sort(st.fields.begin(), st.fields.end(), [](const KField &a, const KField &b) { return a.offset < b.offset; });
      structs.push_back(st);
    }
    for (const auto &[v, c] : db["constants"].fields)
      constants.push_back({std::atoi(v.c_str()), c["meaning"].str()});
    std::sort(constants.begin(), constants.end());
    // (engine_refs.csv: address,engine,"comment")
    std::istringstream csv(readFile(repo / "re" / "engine_refs.csv"));
    std::string line;
    std::getline(csv, line);
    while (std::getline(csv, line)) {
      size_t a = line.find(','), b = a == std::string::npos ? a : line.find(',', a + 1);
      if (b == std::string::npos)
        continue;
      std::string comment = line.substr(b + 1);
      if (comment.size() >= 2 && comment.front() == '"')
        comment = comment.substr(1, comment.size() - 2);
      refs.insert({parseAddr(line.substr(0, a)), {line.substr(a + 1, b - a - 1), comment}});
    }
  }
  const KStruct *structNamed(const std::string &n) const {
    for (const KStruct &s : structs)
      if (s.name == n)
        return &s;
    return nullptr;
  }
};

// ---------------------------------------------------------------- scripts
struct ScriptFile {
  fs::path path;
  std::string stem, about;
  std::vector<unsigned> addresses; // the zoo.exe functions it says it's about
};

std::vector<ScriptFile> findScripts(const fs::path &repo) {
  std::vector<ScriptFile> out;
  fs::path dir = repo / "re" / "lab" / "scripts";
  if (!fs::exists(dir))
    return out;
  static const std::regex addr(R"(zoo\.exe\s+(0x[0-9a-fA-F]{6}))"), about(R"(lab_about\(\)\s*\{\s*return\s*"([^"]*)\")");
  for (const auto &e : fs::directory_iterator(dir)) {
    if (e.path().extension() != ".cpp")
      continue;
    ScriptFile s{e.path(), e.path().stem().string(), "", {}};
    std::string text = readFile(e.path());
    for (std::sregex_iterator it(text.begin(), text.end(), addr), end; it != end; ++it)
      s.addresses.push_back(parseAddr((*it)[1].str()));
    std::smatch m;
    if (std::regex_search(text, m, about))
      s.about = m[1].str();
    out.push_back(s);
  }
  std::sort(out.begin(), out.end(), [](const ScriptFile &a, const ScriptFile &b) { return a.stem < b.stem; });
  return out;
}

using AboutFn = const char *(*)();
using StartFn = void (*)(Lab *);
using StepFn = bool (*)(Lab *, int);

// (a script that crashes stops, not the Lab: plain functions so __try can
// guard them)
int guardedStart(StartFn f, Lab *lab) {
  __try {
    f(lab);
    return 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return static_cast<int>(GetExceptionCode());
  }
}
int guardedStep(StepFn f, Lab *lab, int step, bool *more) {
  __try {
    *more = f(lab, step);
    return 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return static_cast<int>(GetExceptionCode());
  }
}

std::string findVcvars() {
  if (const char *v = std::getenv("ZT1_VCVARS"))
    return v;
  // (a script's DLL is built for the engine it loads into: the Lab runs the
  // 64-bit build when there is one, and a 32-bit DLL wouldn't load in it)
  const char *bat = sizeof(void *) == 8 ? "vcvars64.bat" : "vcvars32.bat";
  for (const char *dir : {"C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/",
                          "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Auxiliary/Build/",
                          "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/",
                          "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/",
                          "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/"})
    if (fs::exists(std::string(dir) + bat))
      return std::string(dir) + bat;
  return "";
}

// --------------------------------------------------- Ghidra (the bridge)
// ZT1_LabBridge.py, run in Ghidra, answers on 127.0.0.1:18489: every
// function, a function's decompiled C, assembly, callers and callees, the
// strings and numbers it uses. Asked in the background; every answer is kept
// in re/private/ghidra/ so what's been seen shows with Ghidra closed.
class GhidraLink {
public:
  explicit GhidraLink(fs::path cacheDir) : cache(std::move(cacheDir)) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    fs::create_directories(cache);
    worker = std::thread([this] { loop(); });
  }
  ~GhidraLink() {
    {
      std::lock_guard<std::mutex> g(lock);
      quit = true;
    }
    wake.notify_all();
    worker.join();
    WSACleanup();
  }
  bool connected() const { return online; }
  std::string program() const {
    std::lock_guard<std::mutex> g(lock);
    return programName;
  }
  // The answer to a request ("/decompile?addr=4378df"): parsed, or null while
  // it's on its way (asked for now); fromCache: it's a saved copy
  const Json *get(const std::string &path, bool *fromCache = nullptr) {
    std::lock_guard<std::mutex> g(lock);
    auto it = answers.find(path);
    if (it != answers.end()) {
      if (fromCache)
        *fromCache = it->second.cached;
      // (a saved copy, Ghidra now there: asked again for the fresh one)
      if (it->second.cached && online && !asked.count(path)) {
        asked.insert(path);
        queue.push_back(path);
        wake.notify_all();
      }
      return &it->second.json;
    }
    if (!asked.count(path)) {
      asked.insert(path);
      queue.push_back(path);
      wake.notify_all();
    }
    return nullptr;
  }
  void forget(const std::string &path) {
    std::lock_guard<std::mutex> g(lock);
    answers.erase(path);
    asked.erase(path);
  }
  // Fire and forget (moving Ghidra's cursor)
  void post(const std::string &path) {
    std::thread([path] {
      std::string body;
      request(path, body);
    }).detach();
  }

private:
  struct Answer {
    Json json;
    bool cached;
  };
  fs::path cache;
  mutable std::mutex lock;
  std::condition_variable wake;
  std::deque<std::string> queue;
  std::set<std::string> asked;
  std::map<std::string, Answer> answers;
  std::string programName;
  std::atomic<bool> online{false};
  bool quit = false;
  std::thread worker;

  fs::path cacheFile(const std::string &path) const {
    std::string n;
    for (char c : path)
      n += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return cache / (n + ".json");
  }
  static bool request(const std::string &path, std::string &body) {
    SOCKET sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sk == INVALID_SOCKET)
      return false;
    DWORD timeout = 90000;
    setsockopt(sk, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof timeout);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(18489);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // (a quick connect: nothing listening is the usual case when Ghidra's shut)
    u_long nb = 1;
    ioctlsocket(sk, FIONBIO, &nb);
    connect(sk, reinterpret_cast<sockaddr *>(&a), sizeof a);
    fd_set w;
    FD_ZERO(&w);
    FD_SET(sk, &w);
    timeval tv{0, 300000};
    if (select(0, nullptr, &w, nullptr, &tv) <= 0) {
      closesocket(sk);
      return false;
    }
    nb = 0;
    ioctlsocket(sk, FIONBIO, &nb);
    std::string req = "GET " + path + " HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    ::send(sk, req.c_str(), static_cast<int>(req.size()), 0);
    std::string all;
    char buf[16384];
    int n;
    // (until the body's all there: Java's server keeps the connection open)
    size_t want = std::string::npos;
    while ((n = recv(sk, buf, sizeof buf, 0)) > 0) {
      all.append(buf, n);
      if (want == std::string::npos) {
        size_t h = all.find("\r\n\r\n");
        if (h != std::string::npos) {
          std::string head = all.substr(0, h);
          for (char &c : head)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          size_t cl = head.find("content-length:");
          want = cl == std::string::npos ? std::string::npos - 1 : h + 4 + std::strtoul(head.c_str() + cl + 15, nullptr, 10);
        }
      }
      if (want != std::string::npos && all.size() >= want)
        break;
    }
    closesocket(sk);
    size_t at = all.find("\r\n\r\n");
    if (at == std::string::npos)
      return false;
    body = all.substr(at + 4);
    return all.compare(0, 12, "HTTP/1.1 200") == 0 || all.compare(0, 12, "HTTP/1.0 200") == 0;
  }
  void loop() {
    Uint32 lastStatus = 0;
    for (;;) {
      std::string path;
      {
        std::unique_lock<std::mutex> g(lock);
        wake.wait_for(g, std::chrono::milliseconds(500), [this] { return quit || !queue.empty(); });
        if (quit)
          return;
        if (!queue.empty()) {
          path = queue.front();
          queue.pop_front();
        }
      }
      // (is Ghidra there? every 3 s)
      if (SDL_GetTicks() - lastStatus > 3000 || lastStatus == 0) {
        lastStatus = SDL_GetTicks();
        std::string b;
        bool was = online;
        online = request("/status", b);
        if (online) {
          Json st = Json::parse(b);
          std::lock_guard<std::mutex> g(lock);
          programName = st["program"].str() + " (" + std::to_string(static_cast<int>(st["functions"].num())) + " functions)";
        }
        if (online && !was) {
          // (back: what was only a saved copy may be asked for again)
          std::lock_guard<std::mutex> g(lock);
          for (auto &[p, a] : answers)
            if (a.cached)
              asked.erase(p);
        }
      }
      if (path.empty())
        continue;
      std::string body;
      bool fresh = online && request(path, body);
      if (!fresh && online && !body.empty() && body[0] == '{') {
        // (Ghidra answered, with an error: shown, not kept)
        Json j = Json::parse(body);
        std::lock_guard<std::mutex> g(lock);
        answers[path] = {std::move(j), false};
        continue;
      }
      if (!fresh) {
        std::ifstream f(cacheFile(path), std::ios::binary);
        if (!f) {
          std::lock_guard<std::mutex> g(lock);
          asked.erase(path); // (asked again later, when Ghidra's there)
          continue;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        body = ss.str();
      } else {
        std::ofstream(cacheFile(path), std::ios::binary) << body;
      }
      Json j = Json::parse(body);
      std::lock_guard<std::mutex> g(lock);
      answers[path] = {std::move(j), !fresh};
    }
  }
};

// ------------------------------------------------ the Lab app's connection
// lab-app/ (Electron) drives the engine over 127.0.0.1:<port>: a line of
// JSON a message each way; a frame is a line {"t":"frame",...,"n":N} then N
// bytes of JPEG.
std::string jq(const std::string &s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\')
      o += '\\', o += static_cast<char>(c);
    else if (c == '\n')
      o += "\\n";
    else if (c == '\r')
      o += "\\r";
    else if (c == '\t')
      o += "\\t";
    else if (c < 0x20) {
      char b[8];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else
      o += static_cast<char>(c);
  }
  return o + "\"";
}

std::string jnum(double v) {
  if (!std::isfinite(v))
    return "0";
  char b[32];
  snprintf(b, sizeof b, "%.6g", v);
  return b;
}

// A JSON object's fields, one by one
struct JObj {
  std::string s;
  JObj &add(const std::string &k, const std::string &rawValue) {
    s += (s.empty() ? "" : ",") + jq(k) + ":" + rawValue;
    return *this;
  }
  JObj &str(const std::string &k, const std::string &v) { return add(k, jq(v)); }
  JObj &num(const std::string &k, double v) { return add(k, jnum(v)); }
  JObj &flag(const std::string &k, bool v) { return add(k, v ? "true" : "false"); }
  std::string done() const { return "{" + s + "}"; }
};


class HostLink {
public:
  explicit HostLink(int port) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof yes);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<u_short>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok = bind(listener, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0 && listen(listener, 1) == 0;
    if (ok)
      reader = std::thread([this] { loop(); });
  }
  ~HostLink() {
    quit = true;
    closesocket(listener);
    {
      std::lock_guard<std::mutex> g(sendLock);
      if (client != INVALID_SOCKET)
        closesocket(client);
    }
    if (reader.joinable())
      reader.join();
    WSACleanup();
  }
  bool listening() const { return ok; }
  bool connected() const { return client != INVALID_SOCKET; }
  bool takeConnected() { return justConnected.exchange(false); }
  // The commands come in so far
  std::vector<Json> take() {
    std::lock_guard<std::mutex> g(inLock);
    std::vector<Json> out;
    out.swap(inbox);
    return out;
  }
  void send(const std::string &line) {
    std::lock_guard<std::mutex> g(sendLock);
    if (client == INVALID_SOCKET)
      return;
    std::string l = line + "\n";
    sendAll(l.data(), l.size());
  }
  void sendFrame(const std::string &head, const std::vector<unsigned char> &bytes) {
    std::lock_guard<std::mutex> g(sendLock);
    if (client == INVALID_SOCKET)
      return;
    std::string l = head + "\n";
    sendAll(l.data(), l.size());
    sendAll(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  }

private:
  SOCKET listener = INVALID_SOCKET;
  std::atomic<SOCKET> client{INVALID_SOCKET};
  std::atomic<bool> quit{false}, justConnected{false};
  bool ok = false;
  std::thread reader;
  std::mutex inLock, sendLock;
  std::vector<Json> inbox;

  void sendAll(const char *p, size_t n) {
    while (n > 0) {
      int k = ::send(client, p, static_cast<int>(std::min<size_t>(n, 1 << 20)), 0);
      if (k <= 0)
        return;
      p += k;
      n -= static_cast<size_t>(k);
    }
  }
  void loop() {
    while (!quit) {
      SOCKET c = accept(listener, nullptr, nullptr);
      if (c == INVALID_SOCKET)
        return;
      BOOL nodelay = TRUE;
      setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay), sizeof nodelay);
      client = c;
      justConnected = true;
      std::string buf;
      char chunk[8192];
      int n;
      while ((n = recv(c, chunk, sizeof chunk, 0)) > 0) {
        buf.append(chunk, n);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
          std::string line = buf.substr(0, nl);
          buf.erase(0, nl + 1);
          if (line.empty())
            continue;
          std::lock_guard<std::mutex> g(inLock);
          inbox.push_back(Json::parse(line));
        }
      }
      {
        std::lock_guard<std::mutex> g(sendLock);
        closesocket(c);
        client = INVALID_SOCKET;
      }
    }
  }
};

// ------------------------------------------------------------------- Lab
class LabApp final : public Lab {
public:
  LabApp(SDL_Window *w, SDL_Renderer *r, ResourceManager *rm, ScenarioManager *sm, World *world)
      : win(w), ren(r), rm(rm), sm(sm), world(world) {}

  int run(const std::string &mapStem);
  int runHost(const std::string &mapStem, int port);

  // ---- Lab (the scripts' side)
  void log(const char *fmt, ...) override {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    addLog(std::string("[") + std::to_string(stepNo) + "] " + buf);
  }
  void watch(const char *label, double value, const char *where) override {
    for (Watch &w : watches)
      if (w.label == label) {
        w.value = value;
        w.where = where ? where : "";
        return;
      }
    watches.push_back({label, value, where ? where : ""});
  }
  void plot(const char *series, double value) override {
    std::vector<float> &v = plots[series];
    v.push_back(static_cast<float>(value));
    if (v.size() > 2000)
      v.erase(v.begin());
  }
  void expect(const char *what, bool ok) override {
    for (Check &c : checks)
      if (c.what == what) {
        c.ok = ok;
        c.times++;
        c.fails += !ok;
        return;
      }
    checks.push_back({what, ok, 1, ok ? 0 : 1});
  }
  void view(const char *mode, const char *what) override {
    viewMode = mode ? mode : "scene";
    viewWhat = what ? what : "";
    snprintf(viewBuf, sizeof viewBuf, "%s", viewWhat.c_str());
    viewArt = nullptr;
    if (viewMode == "sprite")
      viewArt = rm->getAnimation(viewWhat);
    else if (viewMode == "animal") {
      Animals &an = world->getAnimals();
      int t = an.typeOfFile("animals/" + viewWhat + ".ai");
      viewArt = t >= 0 ? an.preview(t, false) : nullptr;
      sendViewAnims(viewWhat);
    } else if (viewMode == "anim") {
      // One of a species' animations by name ("orca|surface_jump_spin|1"),
      // and with layers on, the ones its art's name gives beside it -
      // "shadow" + art, "under" + art - drawn together at one anchor, to see
      // whether they make one picture (Marine Mania's tank art)
      std::string species = viewWhat.substr(0, viewWhat.find('|'));
      std::string rest = viewWhat.find('|') == std::string::npos ? "" : viewWhat.substr(viewWhat.find('|') + 1);
      std::string anim = rest.substr(0, rest.find('|'));
      bool layered = rest.find('|') != std::string::npos && rest.substr(rest.find('|') + 1) == "1";
      viewLayers.clear();
      viewLayerNames.clear();
      Animals &an = world->getAnimals();
      int t = an.typeOfFile("animals/" + species + ".ai");
      if (t >= 0) {
        const auto &anims = an.types()[t].anims[0];
        // (the viewer's own: freed when the next is picked)
        viewOwned.clear();
        auto load = [&](const std::string &name) -> Animation * {
          auto it = anims.find(name);
          if (it == anims.end() || it->second.empty() || !rm->hasResource(it->second + ".ani"))
            return nullptr;
          viewOwned.emplace_back(rm->getAnimation(it->second));
          return viewOwned.back().get();
        };
        viewArt = load(anim);
        if (viewArt && layered) {
          std::string f = anims.at(anim);
          f = f.substr(f.find_last_of('/') + 1);
          for (const std::string &l : {"shadow" + f, "under" + f})
            if (Animation *a = load(l)) {
              viewLayers.push_back(a);
              viewLayerNames.push_back(l);
            }
          viewLayers.push_back(viewArt);
          viewLayerNames.push_back(anim);
        }
        if (species != viewSpecies)
          sendViewAnims(species);
      }
      addLog("view: " + anim + (viewLayers.empty() ? "" : " in layers:") + [&] {
        std::string s;
        for (const std::string &n : viewLayerNames)
          s += " " + n;
        return s;
      }());
    }
    if ((viewMode == "sprite" || viewMode == "animal" || viewMode == "anim") && !viewArt)
      addLog("view: no art for " + viewWhat);
  }
  // A species' animations, by name, to the app (its Animal view's list)
  void sendViewAnims(const std::string &species) {
    viewSpecies = species;
    if (!host)
      return;
    Animals &an = world->getAnimals();
    int t = an.typeOfFile("animals/" + species + ".ai");
    std::string list;
    if (t >= 0)
      for (const auto &kv : an.types()[t].anims[0])
        list += (list.empty() ? "" : ",") + jq(kv.first);
    host->send("{\"t\":\"viewAnims\",\"species\":" + jq(species) + ",\"anims\":[" + list + "]}");
  }
  void focusAnimal(int id) override {
    if (world->getAnimals().member(id))
      world->centreOnAnimal(id);
  }
  void focusTile(float x, float y) override { world->centreOn(x, y); }
  int exhibit(int w, int h, const char *fence) override;
  int animal(const char *species, bool female) override {
    Animals &an = world->getAnimals();
    int t = an.typeOfFile(std::string("animals/") + species + ".ai");
    if (t < 0) {
      addLog(std::string("animal: no species ") + species);
      return -1;
    }
    // (spread along the exhibit's middle)
    float x = labX + 1.0f + static_cast<float>(labAnimals.size() % std::max(1, labW - 2)) + 0.5f;
    float y = labY + labH / 2.0f;
    int id = an.adopt(t, female, x, y, world->getMap(), world->getFences());
    lastMsg = id < 0 ? an.placeMessage : 0;
    if (id >= 0)
      labAnimals.push_back(id);
    return id;
  }
  int object(const char *aiFile, float dx, float dy) override {
    float x = labX + dx, y = labY + dy;
    Fences::Fit f = world->objectFit(aiFile, x, y, 4);
    lastMsg = world->fitMessage;
    if (f != Fences::Fit::Ok || !world->objectsMutable().add(aiFile, x, y, 4))
      return -1;
    world->reindex();
    labObjects.push_back({aiFile, x, y});
    return static_cast<int>(world->getObjects().objects().size()) - 1;
  }
  int lastMessage() override { return lastMsg; }
  void runEngine(float seconds) override {
    static Uint8 none[SDL_NUM_SCANCODES] = {};
    world->setPaused(false);
    for (float t = 0; t < seconds - 1e-4f; t += 0.25f)
      world->update(none, std::min(0.25f, seconds - t));
    world->setPaused(!playing);
  }
  double get(int id, const char *field) override {
    const Animals::Member *m = world->getAnimals().member(id);
    if (!m)
      return 0;
    std::string f = field;
    if (f == "happiness") return m->happiness;
    if (f == "pending") return m->pending;
    if (f == "hunger") return m->hunger;
    if (f == "food") return m->food;
    if (f == "health") return m->health;
    if (f == "energy") return m->energy;
    if (f == "dirty") return m->dirty;
    if (f == "x") return m->x;
    if (f == "y") return m->y;
    if (f == "exhibit") return m->exhibit;
    if (f == "type") return m->type;
    if (f == "female") return m->female;
    if (f == "baby") return m->baby;
    if (f == "sick") return m->sick;
    if (f == "sleeping") return m->sleeping;
    if (f == "escaped") return m->escaped;
    return 0;
  }
  void set(int id, const char *field, double v) override {
    Animals::Member *m = world->getAnimals().member(id);
    if (!m)
      return;
    std::string f = field;
    float fv = static_cast<float>(v);
    if (f == "happiness") m->happiness = fv;
    else if (f == "pending") m->pending = fv;
    else if (f == "hunger") m->hunger = fv;
    else if (f == "food") m->food = fv;
    else if (f == "health") m->health = fv;
    else if (f == "energy") m->energy = fv;
    else if (f == "dirty") m->dirty = fv;
    else if (f == "x") m->x = fv;
    else if (f == "y") m->y = fv;
    else if (f == "sick") m->sick = v != 0;
    else addLog(std::string("set: no field ") + field);
  }
  int suitability(int id) override {
    return world->getAnimals().suitability(id, world->getMap(), world->getFences());
  }
  int speciesInt(const char *species, const char *subtype, const char *key, int *found) override {
    if (found)
      *found = 0;
    IniReader *ai = rm->getIniReader(std::string("animals/") + species + ".ai");
    if (!ai)
      return 0;
    std::string def = ai->get("Global", "DefaultSubtype", "m");
    std::string sub = subtype && *subtype ? subtype : def;
    for (const std::string &s : {sub, def}) {
      std::string v = ai->get(s + "/Characteristics/Integers", key, "\x01");
      if (v != "\x01") {
        if (found)
          *found = 1;
        return std::atoi(v.c_str());
      }
    }
    return 0;
  }
  unsigned exeU32(unsigned a) override {
    const unsigned char *p = exe.at(a, 4);
    return p ? p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24) : 0;
  }
  float exeF32(unsigned a) override {
    unsigned u = exeU32(a);
    float f;
    memcpy(&f, &u, 4);
    return f;
  }
  double exeF64(unsigned a) override {
    const unsigned char *p = exe.at(a, 8);
    double d = 0;
    if (p)
      memcpy(&d, p, 8);
    return d;
  }
  const char *text(int id) override {
    texts.push_back(rm->getString(static_cast<uint32_t>(id)));
    if (texts.size() > 64)
      texts.pop_front();
    return texts.back().c_str();
  }
  int offsetOf(const char *structName, const char *field) override {
    if (const KStruct *s = kb.structNamed(structName))
      for (const KField &f : s->fields)
        if (f.name == field)
          return static_cast<int>(f.offset);
    return -1;
  }

  // ---- Making things: a program run in the background, files, a mod
  int start(const char *program, const char *args, const char *cwd) override {
    auto job = std::make_unique<Job>();
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
      log("Couldn't start %s (no pipe)", program ? program : "");
      return -1;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = writeEnd;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::string line = std::string("\"") + (program ? program : "") + "\" " + (args ? args : "");
    std::vector<char> cmd(line.begin(), line.end());
    cmd.push_back(0);
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             cwd && *cwd ? cwd : nullptr, &si, &pi);
    CloseHandle(writeEnd);
    if (!ok) {
      CloseHandle(readEnd);
      log("Couldn't start %s (error %lu)", program ? program : "", GetLastError());
      return -1;
    }
    CloseHandle(pi.hThread);
    job->process = pi.hProcess;
    job->name = fs::path(program ? program : "").filename().string();
    Job *j = job.get();
    // (its output read as it comes, a line at a time, for the log)
    job->reader = std::thread([j, readEnd] {
      char buf[4096];
      DWORD n = 0;
      std::string part;
      while (ReadFile(readEnd, buf, sizeof buf, &n, nullptr) && n) {
        part.append(buf, n);
        size_t nl;
        while ((nl = part.find_first_of("\r\n")) != std::string::npos) {
          std::string l = part.substr(0, nl);
          part.erase(0, nl + 1);
          if (!l.empty()) {
            std::lock_guard<std::mutex> g(j->lock);
            j->lines.push_back(l);
          }
        }
      }
      if (!part.empty()) {
        std::lock_guard<std::mutex> g(j->lock);
        j->lines.push_back(part);
      }
      CloseHandle(readEnd);
    });
    jobs.push_back(std::move(job));
    log("Started %s %s", jobs.back()->name.c_str(), args ? args : "");
    return static_cast<int>(jobs.size()) - 1;
  }
  bool running(int id) override {
    Job *j = id >= 0 && id < static_cast<int>(jobs.size()) ? jobs[id].get() : nullptr;
    if (!j)
      return false;
    // (its new output to the log: not lines that are only a percentage - an
    // upscaler's progress - and at most 200 lines a program)
    std::deque<std::string> lines;
    {
      std::lock_guard<std::mutex> g(j->lock);
      lines.swap(j->lines);
    }
    for (const std::string &l : lines) {
      bool percent = !l.empty() && l.back() == '%' && l.find_first_not_of("0123456789.% ") == std::string::npos;
      if (percent)
        continue;
      if (j->logged++ < 200)
        addLog("  " + j->name + ": " + l);
      else if (j->logged == 201)
        addLog("  " + j->name + ": ... (more not shown)");
    }
    if (j->process && WaitForSingleObject(j->process, 0) == WAIT_OBJECT_0) {
      DWORD code = 0;
      GetExitCodeProcess(j->process, &code);
      j->code = static_cast<int>(code);
      CloseHandle(j->process);
      j->process = nullptr;
      if (j->reader.joinable())
        j->reader.join();
      log("%s finished (%d)", j->name.c_str(), j->code);
      running(id); // (its last lines)
    }
    return j->process != nullptr;
  }
  int exitCode(int id) override {
    return id >= 0 && id < static_cast<int>(jobs.size()) && !jobs[id]->process ? jobs[id]->code : -1;
  }
  void stop(int id) override {
    if (id >= 0 && id < static_cast<int>(jobs.size()) && jobs[id]->process)
      TerminateProcess(jobs[id]->process, 1);
  }
  const char *engineExe() override {
    char b[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, b, MAX_PATH);
    paths.push_back(b);
    return paths.back().c_str();
  }
  bool makeDir(const char *path) override {
    std::error_code ec;
    fs::create_directories(path ? path : "", ec);
    return fs::is_directory(path ? path : "", ec);
  }
  bool removeDir(const char *path) override {
    // (never a drive or a folder near the top of one)
    fs::path p = fs::absolute(path ? path : "");
    int depth = 0;
    for (const auto &part : p.relative_path())
      depth += !part.empty();
    std::error_code ec;
    if (depth < 2 || !fs::is_directory(p, ec)) {
      log("Not removed: %s", p.string().c_str());
      return false;
    }
    fs::remove_all(p, ec);
    return !ec;
  }
  bool writeFile(const char *path, const char *text) override {
    std::ofstream out(path ? path : "", std::ios::binary | std::ios::trunc);
    out << (text ? text : "");
    return static_cast<bool>(out);
  }
  bool exists(const char *path) override {
    std::error_code ec;
    return fs::exists(path ? path : "", ec);
  }
  int countFiles(const char *dir, const char *extension) override {
    std::error_code ec;
    int n = 0;
    for (const auto &e : fs::directory_iterator(dir ? dir : "", ec))
      if (e.is_regular_file(ec) && (!extension || !*extension || e.path().extension() == extension))
        n++;
    return n;
  }
  bool writeMod(const char *folder, const char *name, const char *author, const char *about) override {
    // (a mod.json already there - one you've edited - is kept)
    fs::path json = fs::path(folder ? folder : "") / "mod.json";
    std::error_code ec;
    if (fs::exists(json, ec))
      return true;
    fs::create_directories(json.parent_path(), ec);
    auto quoted = [](const char *s) {
      std::string o = "\"";
      for (const char *c = s ? s : ""; *c; c++)
        o += *c == '"' || *c == '\\' ? std::string("\\") + *c : std::string(1, *c);
      return o + "\"";
    };
    std::string text = "{\n  \"name\": " + quoted(name) + ",\n  \"author\": " + quoted(author) +
                       ",\n  \"version\": \"1.0\",\n  \"about\": " + quoted(about) + ",\n  \"replaces\": []\n}\n";
    return writeFile(json.string().c_str(), text.c_str());
  }
  void useModsFolder(const char *folder) override {
    Mods::setFolder(folder ? folder : "");
    HdArt::refresh();
    Shaders::refresh();
    log("The game's mods folder: %s", folder ? folder : "");
  }
  const char *modsFolder() override {
    paths.push_back(Mods::folder());
    return paths.back().c_str();
  }

private:
  // A program a script started (start), its output waiting for the log
  struct Job {
    HANDLE process = nullptr;
    std::thread reader;
    std::mutex lock;
    std::deque<std::string> lines;
    std::string name;
    int code = -1, logged = 0;
    ~Job() {
      if (process) {
        TerminateProcess(process, 1);
        CloseHandle(process);
      }
      if (reader.joinable())
        reader.join();
    }
  };
  std::vector<std::unique_ptr<Job>> jobs;
  std::deque<std::string> paths;
  struct Watch {
    std::string label;
    double value;
    std::string where;
  };
  struct Check {
    std::string what;
    bool ok;
    int times, fails;
  };
  struct LabObject {
    std::string file;
    float x, y;
  };

  SDL_Window *win;
  SDL_Renderer *ren;
  ResourceManager *rm;
  ScenarioManager *sm;
  World *world;
  fs::path repo;
  Exe exe;
  Knowledge kb;
  std::vector<ScriptFile> scripts;
  int selScript = -1;
  int selFunction = -1, selStruct = -1;
  unsigned selAddr = 0; // the function picked (any of zoo.exe's, from Ghidra or the knowledge base)
  std::unique_ptr<GhidraLink> ghidra;
  char gfilter[64] = "";
  char filter[64] = "";

  // the log (scripts and the build both write to it)
  std::mutex logLock;
  std::deque<std::string> logLines;
  bool logToBottom = true;
  // the Lab app (host mode)
  std::unique_ptr<HostLink> host;
  std::mutex buildOutLock;
  std::vector<std::string> buildOutput;
  bool frameWanted = false;
  int hostW = 0, hostH = 0;
  bool hostQuit = false;
  void hostCommand(const Json &c);
  void sendState();
  void sendScripts();
  void sendFrame();
  int animalAt(int x, int y);
  void addLog(const std::string &s) {
    SDL_Log("[Lab] %s", s.c_str());
    if (host)
      host->send("{\"t\":\"log\",\"text\":" + jq(s) + "}");
    std::lock_guard<std::mutex> g(logLock);
    logLines.push_back(s);
    if (logLines.size() > 5000)
      logLines.pop_front();
    logToBottom = true;
  }

  // the script running
  HMODULE dll = nullptr;
  StartFn startFn = nullptr;
  StepFn stepFn = nullptr;
  bool scriptLive = false, autoStep = false, playing = false;
  int stepNo = 0;
  float stepsPerSecond = 4.0f;
  Uint32 lastStepAt = 0;
  std::string runningName;
  std::vector<Watch> watches;
  std::vector<Check> checks;
  std::map<std::string, std::vector<float>> plots;
  std::deque<std::string> texts;
  int buildCount = 0;

  // building
  std::thread buildThread;
  std::atomic<int> buildState{0}; // 0 idle, 1 building, 2 built ok, 3 failed
  bool compilerReady = false;
  std::string builtDll;

  // the lab zoo
  int labX = 0, labY = 0, labW = 0, labH = 0, labFence = -1, lastMsg = 0;
  int sceneLevel = 4;
  std::vector<Fences::Edge> labEdges;
  std::vector<int> labAnimals;
  std::vector<LabObject> labObjects;

  // the view
  std::string viewMode = "scene", viewWhat;
  Animation *viewArt = nullptr;
  std::vector<Animation *> viewLayers; // (anim with layers: shadow, under, the animation; drawn in that order)
  int viewFrame = -1;                  // (the art viewer held on a frame; -1 plays)
  std::vector<std::unique_ptr<Animation>> viewOwned; // (the art viewer's animations, its own)
  // A species check under way: what was asked, the species left, how many
  // in all, the findings so far
  std::string checkWant, checkOut;
  std::vector<std::string> checkQueue;
  size_t checkOf = 0;
  std::vector<std::string> viewLayerNames;
  std::string viewSpecies;
  int viewDir = 1; // SE
  char viewBuf[128] = "";
  float artZoom = 1.5f;
  SDL_Texture *artLayer = nullptr, *heightTex = nullptr;
  SDL_Rect viewRect{0, 0, 0, 0};
  bool dragging = false;
  int dragX = 0, dragY = 0;
  int watchAnimal = -1;

  // ---- the app's Scene view: a map, edited with the game's own tools, what
  // the cursor's over, what's picked, overlays, the clock
public:
  LabConsole console;
private:
  std::string mapStem, mapName;
  std::string mapScn; // (the map's .scn: Save as a map makes a design over it)
  bool toolHeld = false; // (a tool pressed and not let go: its drag kept for the rebuild)
  int heldX = -1, heldY = -1;
  int pressPX = 0, pressPY = 0; // (where mouseTile last pressed, on the screen)
  bool mapFlat = false;
  int loads = 0; // (maps loaded: the app waits for the next)
  // ---- the Watch log: the animals you follow, a row each time one starts
  // doing something else or has gone a tile (game time) - for the app's
  // Watch panel: what a hunt, a chase, a stand-off did, in order
  struct WatchRow {
    double t;
    int id;
    std::string name, doing;
    float x, y;
  };
  std::set<int> watching;
  std::vector<WatchRow> watchRows;
  std::map<int, WatchRow> watchLast;
  bool watchDirty = false;
  Uint32 watchSentAt = 0;
  void watchTick();
  void sendWatch();
  struct Tool {
    // select bulldoze fence path object animal staff terrain gate filter
    std::string kind = "select";
    std::string item; // its catalogue file (a fence, path, object, animal, staff)
    bool female = false;
    int facing = 0;
    bool painting = true; // terrain: its types, else its heights
    int terrainType = 0, size = 2, mode = 0;
  } tool;
  struct Sel {
    std::string kind; // animal guest staff object exhibit tile ("": nothing)
    int id = -1, x = 0, y = 0;
  } sel;
  bool hoverOn = false;
  int hoverX = 0, hoverY = 0;
  float speed = 1.0f;
  double simClock = 0;
  Uint32 lastUpdate = 0;
  bool showGrid = false, showExhibits = true;
  std::string asking; // "remove" (a fence with animals, two exhibits merging) or "drain": yes or no
  struct Terrain {
    std::string name, icon;
    int cost = 0, water = 0;
  };
  std::map<int, Terrain> terrains;
  void applyTool();
  void sceneMouse(const std::string &ev, int x, int y);
  void toolResult(const World::ToolResult &r, bool selecting, int x, int y);
  int objectAt(int x, int y);
  std::string selJson();
  std::string hoverJson();
  std::string routeJson(const char *kind, int id, const std::vector<std::pair<float, float>> &path, size_t at);
  std::string countsJson();
  void setSel(const std::string &field, const Json &v);
  void removeSel();
  bool selPoint(float &x, float &y);
  void sendOutline();
  void sendMaps();
  void sendCatalog();
  void sendIcon(const std::string &path);
  void sendFileView(const std::string &name, const std::string &dirWanted);
  void sendFilesIndex();
  void sendFilesGrep(const std::string &text);
  void sendAiKeys();
  // Write Code's scripts (EngineScripts.hpp): told what's picked and where
  // their words go, once; their list, with which are switched on
  void hookScripts() {
    if (EngineScripts::fillPicked)
      return;
    EngineScripts::fillPicked = [this](Zoo &z) {
      if (sel.kind == "animal")
        z.animal = z.animals.member(sel.id);
      else if (sel.kind == "guest")
        z.guest = sel.id;
      else if (sel.kind == "staff")
        z.staffMember = sel.id;
      else if (sel.kind == "exhibit")
        z.exhibit = sel.id;
      if (sel.kind == "tile")
        z.tileX = sel.x, z.tileY = sel.y;
      else if (hoverOn)
        z.tileX = hoverX, z.tileY = hoverY;
    };
    EngineScripts::log = [this](const std::string &s) { addLog(s); };
  }
  // A click or drag made by code (mouseTile, mouseWorld - the rebuild list's
  // replay, an MCP agent's place) while the Test Zoo isn't on screen - its
  // view 64x64 - is done in a full-size view, so a drag across the map fits;
  // the view as it was after
  template <class F> void inWorkView(F f) {
    SDL_Rect was = viewRect;
    bool small = viewRect.w < 640 || viewRect.h < 400;
    if (small) {
      viewRect = SDL_Rect{0, 0, 1600, 900};
      world->setView(viewRect, 1.0f);
    }
    f();
    if (small) {
      viewRect = was;
      world->setView(viewRect, 1.0f);
    }
  }
  void sendEngineScripts() {
    std::string list;
    for (const EngineScripts::Script &s : EngineScripts::all()) {
      const char *when = s.when == EngineScripts::Once ? "once" : s.when == EngineScripts::EveryTick ? "tick" : "animal";
      list += (list.empty() ? "" : ",") +
              JObj().str("name", s.name).str("about", s.about).str("when", when).flag("on", EngineScripts::switchedOn().count(s.name) > 0).done();
    }
    host->send("{\"t\":\"engineScripts\",\"list\":[" + list + "]}");
  }
  bool loadMap(const std::string &stem, bool flat);
  // The map's goals, as in the game (its .scn's: freeform's donations,
  // awards, unlocks - and the adoption ban when its animals are unhappy):
  // measured from the lab zoo, on its clock. Popups are notes - and pause
  // the zoo when they say pause=1, as the game does; UI elements they'd
  // disable (the adopt button's 2075, 2080) are kept as a set
  Goals goals;
  std::set<int> goalsDisabled;
  bool goalPaused = false;
  void setupGoals();
  void flattenZoo();
  void drawOverlays();
  void stepWorld(float seconds);
  void note(const std::string &text);
  std::string terrainName(int type);
  std::string sceneJson();

  // ---- stepping: the engine's trace points (LabTrace.hpp), sent to the
  // app as they're reached; at a breakpoint (or stepping) the engine stops
  // there - the view still drawn, the world as it is at that line - until
  // the app says go on
  std::string traceMode = "selected"; // off, selected (and what's in it, and the tools), all
  bool stepEach = false;              // stop at every step traced
  bool breakNext = false;             // stop at the next one (Step)
  int stepOverDepth = -1;             // stop at the next at this depth or less (Step over / out)
  bool inBreak = false;
  std::set<std::string> breakpoints; // "src/Animals.cpp:2065"
  int stepSeq = 0, stepsThisSecond = 0, stepsDropped = 0;
  // The lab zoo's set-up, to be made again after a build (rebuildGet/Run)
  std::vector<Json> rebuild;
  // (skipping time: the latest steps, sent when the skip's done)
  bool skipping = false;
  std::deque<std::string> skipSteps;
  std::string stepJson(const LabTrace::Step &s, const std::string &file, const LabTrace::Who &w, int depth, bool pause,
                       bool bp);
  Uint32 stepSecond = 0;
  LabTrace::Who stepWho;              // whose step it's stopped at
  std::string stepAt;                 // and where ("src/Animals.cpp:2065")
  std::vector<Json> deferred;         // commands that wait until it goes on

  // ---- routes: each watched walker's last search (Pathfinder::Search),
  // everyone's routes drawn (the Routes overlay), and the Route tool's own
  std::map<std::pair<std::string, int>, Pathfinder::Search> searches;
  Pathfinder::Search testSearch;
  bool hasTest = false, routeTesting = false, routeStartSet = false;
  int routeSX = 0, routeSY = 0;
  bool showRoutes = false;
  // Footprints: every object's, the quarter tiles it blocks; none found, red
  bool showFootprints = false;
  void drawFootprints();

  // ---- testing: a value pinned (held at what it's set to, every update),
  // and actions on demand (an animation, a behaviour, a need, the console's)
  struct Pin {
    std::string kind;
    int id;
    std::string field;
    double value;
  };
  std::vector<Pin> pins;
  void applyPins();
  void act(const std::string &action, const std::string &arg);
  void routeClick(int tx, int ty);
  void drawSearch(const Pathfinder::Search &s);
  void drawRoutes();
  const Pathfinder::Search *searchOf(const std::string &kind, int id) const;
  std::string searchJson(const Pathfinder::Search &s);
  void onStep(const LabTrace::Step &s);
  void breakLoop(int depth);
  bool traced(const LabTrace::Who &w);
  std::string whoName(const LabTrace::Who &w);
  void updateTraceOn();

  std::string forceTab; // (ZT_LAB_TAB: a bottom tab opened at the start)
  // Ghidra's "Show in Lab"
  fs::file_time_type selectStamp{};

  bool setupScene(const std::string &mapStem);
  void flatten(int x0, int y0, int w, int h);
  void clearLab();
  void startBuild(int script);
  void finishBuild();
  void unloadScript();
  void doStep();
  void selectAddress(unsigned addr);
  void pollGhidra();
  void openInEditor(const fs::path &file, int line = 0);
  void newScriptFor(const KFunction &f);
  void drawView();
  void drawHeightmap();
  void drawArt();
  void ui();
  void uiToolbar(float h);
  void uiLeft(const ImVec2 &pos, const ImVec2 &size);
  void uiRight(const ImVec2 &pos, const ImVec2 &size);
  void uiBottom(const ImVec2 &pos, const ImVec2 &size);
  void uiGhidraCode(bool assembly);
  void uiGhidraCalls();
  void uiNotes(const std::string &code);
  std::string fnLabel(unsigned addr, const std::string &ghidraName) const;
};

// ------------------------------------------------------------- the scene
bool LabApp::setupScene(const std::string &mapStem) {
  ItemCatalog::get().load(rm);
  Research::get().load(rm);
  world->edgeScroll = false;
  world->getFences().setEditing(false);
  // (blank:<w>x<h> - a blank map, flat grass with nothing on it, to make a
  // map from: the map editor - editing, so walls, entrances and scenery go
  // anywhere; its goals, research and ambience the Basic Grass map's)
  if (mapStem.rfind("blank:", 0) == 0) {
    int w = 0, h = 0;
    if (std::sscanf(mapStem.c_str() + 6, "%dx%d", &w, &h) != 2 || !world->loadBlank(w, h)) {
      addLog("Couldn't make a blank map " + mapStem.substr(6) + " (16 to 512 tiles a side)");
      return false;
    }
    addLog("Lab zoo: a blank map, " + std::to_string(w) + " x " + std::to_string(h));
    this->mapStem = mapStem;
    this->mapName = "A blank map (" + std::to_string(w) + " x " + std::to_string(h) + ")";
    mapScn = "freeform/ff01.scn";
    simClock = 0;
    setupGoals();
    goalsDisabled.clear();
    goals.load(rm, mapScn);
    world->getFences().setEditing(true);
    return true;
  }
  // Native player saves can be inspected in the same Test Zoo tools.
  if (mapStem.rfind("native:", 0) == 0) {
    const std::string path=mapStem.substr(7),base=SaveGame::mapOf(path);
    if(base.empty() || !world->loadFreeform(base)) {
      addLog("Couldn't load the native saved zoo " + path);return false;
    }
    ZooSim books;
    UiGameScreen hud(rm,"ui/main.lyt","ui/gamescrn.lyt");
    if(!SaveGame::load(path,*world,books,hud)) {
      addLog("Couldn't restore the native saved zoo " + path);return false;
    }
    this->mapStem=mapStem;this->mapName=fs::path(path).filename().string();
    mapScn=base;simClock=0;setupGoals();goalsDisabled.clear();
    addLog("Lab zoo: native save " + path);
    return true;
  }
  // (save:<name> - one of the original game's own saved zoos, its whole world
  // put back as the game does it: SaveGame::loadOriginalWorld)
  if (mapStem.rfind("save:", 0) == 0) {
    const std::string dir = Paths::originalSavesDir();
    const std::string path = dir + mapStem.substr(5) + ".zoo";
    if (dir.empty() || !world->loadFreeform(path) || !SaveGame::loadOriginalWorld(*world)) {
      addLog("Couldn't load the original's saved zoo " + path);
      return false;
    }
    addLog("Lab zoo: the original's saved zoo " + path);
    this->mapStem = mapStem;
    this->mapName = mapStem.substr(5) + " (the original's save)";
    simClock = 0;
    setupGoals();
    goalsDisabled.clear();
    return true;
  }
  const FreeformMap *chosen = nullptr;
  for (int i = 0; const FreeformMap *m = sm->getFreeformMap(i); i++)
    if (fs::path(m->path).stem().string() == mapStem)
      chosen = m;
  if (!chosen && sm->getFreeformMap(0)) {
    chosen = sm->getFreeformMap(0);
    addLog("No map called " + mapStem + " (a mod's map is listed once the mod is on and the engine started again): " +
           chosen->name + " instead");
  }
  if (!chosen || !world->loadFreeform(chosen->path)) {
    addLog("Couldn't load a map for the lab zoo");
    return false;
  }
  addLog("Lab zoo: " + chosen->path);
  mapScn = chosen->path;
  this->mapStem = fs::path(chosen->path).stem().string();
  this->mapName = chosen->name;
  simClock = 0;
  // (its goals, as the game starts them - main.cpp's startGoals)
  setupGoals();
  goalsDisabled.clear();
  goals.load(rm, chosen->path);
  // (what it can't measure is left out - its cash and zoo rating: the lab
  // zoo keeps no books - not answered with a made-up number)
  auto &all = goals.mutableAll();
  size_t before = all.size();
  all.erase(std::remove_if(all.begin(), all.end(), [](const Goals::Goal &g) { return g.rulea == 1 && (g.ruleb == 2 || g.ruleb == 7); }),
            all.end());
  addLog("Lab zoo goals: " + std::to_string(all.size()) + " (" + std::to_string(before - all.size()) +
         " on cash or the zoo rating left out: no books here)");
  // The lab spot: a clear stretch inside the zoo near its gate
  WorldMap &map = world->getMap();
  Fences &fn = world->getFences();
  float gx = map.getWidth() / 2.0f, gy = map.getHeight() / 2.0f;
  for (const PlacedObjects::Object &o : world->getObjects().objects())
    if (o.className == "building" && o.typeName.size() >= 4 &&
        o.typeName.compare(o.typeName.size() - 4, 4, "gate") == 0) {
      gx = o.x;
      gy = o.y;
    }
  if (const MapTile *t = map.getTile(static_cast<int>(gx), static_cast<int>(gy)))
    sceneLevel = t->cornerHeight[0];
  const int need = 16;
  int best = -1;
  for (int r = 4; r < 40 && best < 0; r++)
    for (int dy = -r; dy <= r && best < 0; dy++)
      for (int dx = -r; dx <= r && best < 0; dx++) {
        int x0 = static_cast<int>(gx) + dx, y0 = static_cast<int>(gy) + dy;
        bool ok = true;
        for (int y = y0; y < y0 + need && ok; y++)
          for (int x = x0; x < x0 + need && ok; x++)
            ok = fn.insideZoo(x, y);
        if (ok) {
          labX = x0 + 4;
          labY = y0 + 4;
          best = 1;
        }
      }
  if (best < 0) {
    labX = static_cast<int>(gx) + 6;
    labY = static_cast<int>(gy) + 6;
  }
  // (the lab spot's made flat when a mod builds its exhibit there; the
  // ImGui Lab shows it flat from the start)
  if (!host)
    flatten(labX - 4, labY - 4, need, need);
  world->centreOn(labX + 4.0f, labY + 4.0f);
  return true;
}

// The whole zoo one level of plain grass, its trees, rocks, paths and water
// gone (the entrance and the path by it kept): a flat place to test on (as
// ZT_FLAT_LAB)
void LabApp::flattenZoo() {
  WorldMap &map = world->getMap();
  Fences &fn = world->getFences();
  const PlacedObjects::Object *gate = nullptr;
  for (const PlacedObjects::Object &o : world->getObjects().objects())
    if (o.className == "building" && o.typeName.size() >= 4 && o.typeName.compare(o.typeName.size() - 4, 4, "gate") == 0)
      gate = &o;
  float gx = gate ? gate->x : map.getWidth() / 2.0f, gy = gate ? gate->y : map.getHeight() / 2.0f;
  auto nearGate = [&](float x, float y) { return std::fabs(x - gx) < 4.0f && std::fabs(y - gy) < 4.0f; };
  // The height the ground by the entrance (kept as it is) mostly has: the
  // rest made that, so the two meet level. (One corner of the gate's tile
  // was used: a unit below the rest on Death Mountain, a cliff all round the
  // entrance that no path or guest could cross - the Route tool showed it)
  std::map<int, int> heights;
  for (int y = static_cast<int>(gy) - 4; y <= static_cast<int>(gy) + 4; y++)
    for (int x = static_cast<int>(gx) - 4; x <= static_cast<int>(gx) + 4; x++)
      if (const MapTile *t = map.getTile(x, y); t && fn.insideZoo(x, y) && nearGate(x + 0.5f, y + 0.5f))
        for (int c = 0; c < 4; c++)
          heights[t->cornerHeight[c]]++;
  int level = 4;
  for (int best = -1; const auto &[h, n] : heights)
    if (n > best)
      best = n, level = h;
  // A grid point the entrance's kept ground shares keeps that height: the
  // flat ground meets it in a ramp, not a cliff
  auto kept = [&](int x, int y) { return fn.insideZoo(x, y) && nearGate(x + 0.5f, y + 0.5f); };
  static const int cdx[4] = {0, 1, 1, 0}, cdy[4] = {0, 0, 1, 1}; // (CORNER_X0Y0, X1Y0, X1Y1, X0Y1)
  auto pointHeight = [&](int vx, int vy) {
    for (int c = 0; c < 4; c++) {
      int tx = vx - cdx[c], ty = vy - cdy[c]; // the tile with this point as its corner c
      if (kept(tx, ty))
        if (const MapTile *k = map.getTile(tx, ty))
          return static_cast<int>(k->cornerHeight[c]);
    }
    return level;
  };
  std::vector<std::array<int, 4>> want(static_cast<size_t>(map.getWidth()) * map.getHeight());
  for (int y = 0; y < map.getHeight(); y++)
    for (int x = 0; x < map.getWidth(); x++)
      if (fn.insideZoo(x, y) && !kept(x, y))
        for (int c = 0; c < 4; c++)
          want[static_cast<size_t>(y) * map.getWidth() + x][c] = pointHeight(x + cdx[c], y + cdy[c]);
  for (int y = 0; y < map.getHeight(); y++)
    for (int x = 0; x < map.getWidth(); x++) {
      if (!fn.insideZoo(x, y) || kept(x, y))
        continue;
      MapTile *t = map.getTileMutable(x, y);
      for (int c = 0; c < 4; c++)
        t->cornerHeight[c] = want[static_cast<size_t>(y) * map.getWidth() + x][c];
      t->height = t->cornerHeight[0];
      t->terrainType = 0;
      map.setPath(x, y, "");
    }
  map.touch();
  auto &objs = world->objectsMutable();
  for (int i = static_cast<int>(world->getObjects().objects().size()) - 1; i >= 0; i--) {
    const PlacedObjects::Object &o = world->getObjects().objects()[i];
    if (!o.fence && !nearGate(o.x, o.y) && fn.insideZoo(static_cast<int>(o.x), static_cast<int>(o.y)))
      objs.remove(i);
  }
  world->reindex();
  sceneLevel = level;
}

void LabApp::flatten(int x0, int y0, int w, int h) {
  WorldMap &map = world->getMap();
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++)
      if (MapTile *t = map.getTileMutable(x, y)) {
        for (int c = 0; c < 4; c++)
          t->cornerHeight[c] = sceneLevel;
        t->height = sceneLevel;
        t->terrainType = 0;
        map.setPath(x, y, "");
      }
  map.touch();
  auto &objs = world->objectsMutable();
  for (int i = static_cast<int>(world->getObjects().objects().size()) - 1; i >= 0; i--) {
    const PlacedObjects::Object &o = world->getObjects().objects()[i];
    if (!o.fence && o.x >= x0 && o.x < x0 + w && o.y >= y0 && o.y < y0 + h)
      objs.remove(i);
  }
  world->reindex();
}

void LabApp::clearLab() {
  Animals &an = world->getAnimals();
  for (int id : labAnimals)
    if (an.member(id))
      an.remove(id);
  labAnimals.clear();
  for (const Fences::Edge &e : labEdges)
    if (world->getFences().at(e))
      world->getFences().remove(e, world->getMap());
  labEdges.clear();
  labObjects.clear();
  flatten(labX - 4, labY - 4, 16, 16);
  world->getFences().updateExhibits(world->getMap(), 1, 0, 1);
  world->getFences().update(1.0f);
}

int LabApp::exhibit(int w, int h, const char *fence) {
  clearLab();
  w = std::clamp(w, 2, 8);
  h = std::clamp(h, 2, 8);
  labW = w;
  labH = h;
  Fences &fn = world->getFences();
  labFence = -1;
  for (size_t i = 0; i < fn.types().size(); i++)
    if (fn.types()[i].key == (fence ? fence : "chainlnk"))
      labFence = static_cast<int>(i);
  if (labFence < 0)
    for (size_t i = 0; i < fn.types().size(); i++)
      if (!fn.types()[i].tank && !fn.types()[i].zooWall) {
        labFence = static_cast<int>(i);
        break;
      }
  static int drag = 9000;
  drag++;
  auto lay = [&](bool alongX, int x, int y) {
    Fences::Edge e{alongX, x, y};
    if (fn.canPlace(e, world->getMap())) {
      fn.place(e, labFence, drag, world->getMap());
      labEdges.push_back(e);
    }
  };
  for (int x = labX; x < labX + w; x++) {
    lay(true, x, labY);
    lay(true, x, labY + h);
  }
  for (int y = labY; y < labY + h; y++) {
    lay(false, labX, y);
    lay(false, labX + w, y);
  }
  // (the fence tool's last step: what's now shut off from the map's edge
  // becomes an exhibit)
  for (int id : fn.updateExhibits(world->getMap(), 1, 0, 1))
    if (fn.exhibit(id))
      fn.finishExhibit(id, "Lab Exhibit", world->getMap()); // (named, with its gate: as the fence tool does)
  for (int i = 0; i < 3; i++)
    fn.update(1.0f);
  world->reindex();
  world->centreOn(labX + w / 2.0f, labY + h / 2.0f);
  int ex = fn.exhibitAt(labX, labY);
  addLog("exhibit " + std::to_string(w) + "x" + std::to_string(h) + " at " + std::to_string(labX) + "," +
         std::to_string(labY) + ": id " + std::to_string(ex) + " (" + std::to_string(labEdges.size()) + " fence pieces)");
  return ex;
}

// ------------------------------------------------------------- scripts
void LabApp::startBuild(int s) {
  if (s < 0 || s >= static_cast<int>(scripts.size()) || buildState == 1)
    return;
  if (buildThread.joinable())
    buildThread.join();
  std::string vcvars = findVcvars();
  if (vcvars.empty()) {
    addLog(std::string("Build: no Visual Studio (") + (sizeof(void *) == 8 ? "vcvars64" : "vcvars32") +
           ".bat) found. Set ZT1_VCVARS to its path.");
    return;
  }
  fs::path out = repo / "build" / "lab";
  fs::create_directories(out / "obj");
  builtDll = (out / (scripts[s].stem + "_" + std::to_string(++buildCount) + ".dll")).string();
  // (a batch file: cmd's quoting of a one-line command is too fragile)
  fs::path bat = out / "build_script.cmd";
  {
    std::ofstream b(bat);
    b << "@echo off\r\n"
      << "cl /nologo /LD /EHsc /std:c++20 /MD /O2 /utf-8 /I\"" << (repo / "re" / "lab").string() << "\" \""
      << scripts[s].path.string() << "\" /Fe\"" << builtDll << "\" /Fo\"" << (out / "obj").string()
      << "\\\\\" /link /NOLOGO\r\n";
  }
  std::string cmd = "\"\"" + bat.string() + "\" 2>&1\"";
  addLog("Build: " + scripts[s].stem + ".cpp ...");
  buildState = 1;
  runningName = scripts[s].stem;
  buildThread = std::thread([this, cmd, vcvars, out] {
    Uint32 t0 = SDL_GetTicks();
    // The compiler's environment (vcvars32.bat takes half a minute): worked
    // out once, kept in build/lab/vcenv.txt, put into the Lab's own
    // environment for every build after
    if (!compilerReady) {
      fs::path envFile = out / "vcenv.txt";
      std::string text = readFile(envFile);
      if (text.compare(0, vcvars.size() + 1, vcvars + "\n") != 0) {
        addLog("Build: setting up the compiler (once: about 30 s)...");
        fs::path envBat = out / "vcenv.cmd";
        std::ofstream(envBat) << "@echo off\r\ncall \"" << fs::path(vcvars).make_preferred().string()
                              << "\" >nul 2>&1\r\nset\r\n";
        text = vcvars + "\n";
        if (FILE *p = _popen(("\"\"" + envBat.string() + "\"\"").c_str(), "r")) {
          char line[8192];
          while (fgets(line, sizeof line, p))
            text += line;
          _pclose(p);
        }
        std::ofstream(envFile, std::ios::binary) << text;
      }
      std::istringstream in(text);
      std::string l;
      std::getline(in, l);
      while (std::getline(in, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n'))
          l.pop_back();
        size_t eq = l.find('=');
        if (eq != std::string::npos && eq > 0)
          _putenv_s(l.substr(0, eq).c_str(), l.substr(eq + 1).c_str());
      }
      compilerReady = true;
    }
    FILE *p = _popen(cmd.c_str(), "r");
    if (!p) {
      addLog("Build: couldn't start the compiler");
      buildState = 3;
      return;
    }
    char line[1024];
    while (fgets(line, sizeof line, p)) {
      std::string s = line;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
      // (cl prints the file's name, and the import library lines: noise)
      if (s.empty() || s.find("Creating library") != std::string::npos ||
          (s.find(".cpp") != std::string::npos && s.find('(') == std::string::npos))
        continue;
      addLog("  " + s);
      std::lock_guard<std::mutex> g(buildOutLock);
      buildOutput.push_back(s);
    }
    int rc = _pclose(p);
    addLog("Build: " + std::string(rc == 0 ? "done" : "FAILED") + " in " + std::to_string((SDL_GetTicks() - t0) / 1000.0f).substr(0, 4) + " s");
    buildState = rc == 0 ? 2 : 3;
  });
}

void LabApp::unloadScript() {
  scriptLive = false;
  autoStep = false;
  startFn = nullptr;
  stepFn = nullptr;
  if (dll) {
    FreeLibrary(dll);
    dll = nullptr;
  }
}

void LabApp::finishBuild() {
  if (buildThread.joinable())
    buildThread.join();
  int state = buildState.exchange(0);
  if (host) {
    // (to the Lab app: the outcome and each problem, by line)
    static const std::regex problem(R"(\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning)\s+(\w+)\s*:\s*(.*))");
    std::string list;
    std::lock_guard<std::mutex> g(buildOutLock);
    for (const std::string &l : buildOutput) {
      std::smatch m;
      if (!std::regex_search(l, m, problem))
        continue;
      if (!list.empty())
        list += ",";
      list += "{\"line\":" + m[1].str() + ",\"col\":" + (m[2].matched ? m[2].str() : "0") + ",\"kind\":" + jq(m[3].str()) +
              ",\"code\":" + jq(m[4].str()) + ",\"message\":" + jq(m[5].str()) + "}";
    }
    host->send("{\"t\":\"build\",\"script\":" + jq(runningName) + ",\"ok\":" + (state == 2 ? "true" : "false") +
               ",\"problems\":[" + list + "]}");
    buildOutput.clear();
  } else {
    std::lock_guard<std::mutex> g(buildOutLock);
    buildOutput.clear();
  }
  if (state != 2)
    return;
  unloadScript();
  dll = LoadLibraryA(builtDll.c_str());
  if (!dll) {
    addLog("Couldn't load " + builtDll);
    return;
  }
  startFn = reinterpret_cast<StartFn>(GetProcAddress(dll, "lab_start"));
  stepFn = reinterpret_cast<StepFn>(GetProcAddress(dll, "lab_step"));
  if (!startFn) {
    addLog("The script has no lab_start (LAB_EXPORT void lab_start(Lab *lab))");
    unloadScript();
    return;
  }
  watches.clear();
  checks.clear();
  plots.clear();
  stepNo = 0;
  view("scene", "");
  addLog("Run: " + runningName);
  if (int code = guardedStart(startFn, this)) {
    char b[64];
    snprintf(b, sizeof b, "lab_start crashed (exception 0x%08x)", code);
    addLog(b);
    unloadScript();
    return;
  }
  scriptLive = stepFn != nullptr;
  autoStep = scriptLive;
  lastStepAt = SDL_GetTicks();
}

void LabApp::doStep() {
  if (!scriptLive || !stepFn)
    return;
  bool more = false;
  stepNo++;
  if (int code = guardedStep(stepFn, this, stepNo, &more)) {
    char b[64];
    snprintf(b, sizeof b, "lab_step crashed at step %d (exception 0x%08x)", stepNo, code);
    addLog(b);
    more = false;
  }
  if (!more) {
    scriptLive = false;
    autoStep = false;
    int fails = 0;
    for (const Check &c : checks)
      fails += c.fails > 0;
    addLog("Finished after " + std::to_string(stepNo) + " steps: " + std::to_string(checks.size() - fails) + " checks passed, " +
           std::to_string(fails) + " failed");
  }
}

void LabApp::selectAddress(unsigned addr) {
  selFunction = -1;
  selAddr = addr;
  for (size_t i = 0; i < kb.functions.size(); i++)
    if (kb.functions[i].address == addr)
      selFunction = static_cast<int>(i);
  for (size_t i = 0; i < scripts.size(); i++)
    for (unsigned a : scripts[i].addresses)
      if (a == addr)
        selScript = static_cast<int>(i);
  addLog("Selected " + hex(addr) + (selFunction >= 0 ? " " + kb.functions[selFunction].name : " (not in the knowledge base)"));
}

void LabApp::pollGhidra() {
  fs::path f = repo / "re" / "private" / "lab_select.txt";
  std::error_code ec;
  auto t = fs::last_write_time(f, ec);
  if (ec || t == selectStamp)
    return;
  bool first = selectStamp == fs::file_time_type{};
  selectStamp = t;
  if (first)
    return; // (an old one, from before the Lab started)
  std::string s = readFile(f);
  if (!s.empty()) {
    selectAddress(parseAddr(s));
    SDL_RaiseWindow(win);
  }
}

void LabApp::openInEditor(const fs::path &file, int line) {
  // (VS Code to the line if it's there, else whatever opens .cpp files)
  std::string arg = "-g \"" + file.string() + ":" + std::to_string(line) + "\"";
  if (reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", "code", arg.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
    ShellExecuteA(nullptr, "open", file.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void LabApp::newScriptFor(const KFunction &f) {
  fs::path p = repo / "re" / "lab" / "scripts" / (f.name.empty() ? "fn_" + hex(f.address).substr(2) : f.name);
  p += ".cpp";
  if (!fs::exists(p)) {
    std::ofstream o(p);
    o << "#include \"lab_api.h\"\n\n"
      << "// zoo.exe " << hex(f.address) << " " << f.name << ": " << f.summary << "\n"
      << "// What does zoo.exe do here? Write it below and check it against the engine.\n\n"
      << "LAB_EXPORT const char *lab_about() { return \"" << f.name << "\"; }\n\n"
      << "static int animal = -1;\n\n"
      << "LAB_EXPORT void lab_start(Lab *lab) {\n"
      << "  lab->exhibit(5, 5, \"chainlnk\");\n"
      << "  animal = lab->animal(\"zebra\", false);\n"
      << "  lab->focusAnimal(animal);\n"
      << "  lab->log(\"zebra %d in the lab exhibit\", animal);\n"
      << "}\n\n"
      << "LAB_EXPORT bool lab_step(Lab *lab, int step) {\n"
      << "  lab->runEngine(1.0f); // one second of the engine (one tick of the animal's checks)\n"
      << "  double h = lab->get(animal, \"happiness\");\n"
      << "  lab->watch(\"happiness\", h, \"ZTAnimal+0x2a8\");\n"
      << "  lab->plot(\"happiness\", h);\n"
      << "  return step < 120;\n"
      << "}\n";
  }
  scripts = findScripts(repo);
  for (size_t i = 0; i < scripts.size(); i++)
    if (scripts[i].path == p)
      selScript = static_cast<int>(i);
  openInEditor(p, 1);
}

// ------------------------------------------------------------ drawing
void LabApp::drawHeightmap() {
  WorldMap &map = world->getMap();
  int w = map.getWidth(), h = map.getHeight();
  if (w <= 0 || h <= 0)
    return;
  if (!heightTex) {
    heightTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureScaleMode(heightTex, SDL_ScaleModeNearest);
  }
  int lo = 1 << 30, hi = -(1 << 30);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (const MapTile *t = map.getTile(x, y)) {
        lo = std::min(lo, t->cornerHeight[0]);
        hi = std::max(hi, t->cornerHeight[0]);
      }
  // (each tile its ground's colour - grass green, savannah gold, water
  // blue... - lit by its height, paths pale: the whole map at once, for
  // designing one; the lab exhibit tinted green)
  static const Uint8 ground[18][3] = {
      {86, 128, 48},   {196, 160, 60},  {220, 200, 140}, {140, 100, 60},  {70, 95, 40},    {130, 95, 70},
      {128, 128, 128}, {170, 165, 150}, {240, 240, 245}, {60, 120, 200},  {40, 90, 170},   {115, 105, 55},
      {150, 190, 230}, {80, 85, 50},    {190, 190, 185}, {70, 70, 75},    {150, 130, 80},  {150, 170, 180}};
  void *pixels;
  int pitch;
  if (SDL_LockTexture(heightTex, nullptr, &pixels, &pitch) == 0) {
    for (int y = 0; y < h; y++) {
      Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(pixels) + y * pitch);
      for (int x = 0; x < w; x++) {
        const MapTile *t = map.getTile(x, y);
        int v = t ? (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) / 4 : lo;
        float lit = hi > lo ? 0.6f + 0.6f * (v - lo) / static_cast<float>(hi - lo) : 1.0f;
        const Uint8 *c = ground[t ? std::min<int>(t->terrainType, 17) : 0];
        int r = c[0], g = c[1], b = c[2];
        if (map.isPath(x, y))
          r = 222, g = 212, b = 186;
        r = std::min(255, static_cast<int>(r * lit));
        g = std::min(255, static_cast<int>(g * lit));
        b = std::min(255, static_cast<int>(b * lit));
        if (x >= labX && x < labX + labW && y >= labY && y < labY + labH)
          r /= 2, b /= 2;
        row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
    }
    SDL_UnlockTexture(heightTex);
  }
  float s = std::min(viewRect.w / static_cast<float>(w), viewRect.h / static_cast<float>(h));
  SDL_Rect dst{viewRect.x + static_cast<int>((viewRect.w - w * s) / 2), viewRect.y + static_cast<int>((viewRect.h - h * s) / 2),
               static_cast<int>(w * s), static_cast<int>(h * s)};
  SDL_RenderCopy(ren, heightTex, nullptr, &dst);
  // (its fences as lines along the tiles' sides: the zoo wall red, the
  // rest dark - the map's outline, for drawing one by hand)
  const Fences &fn = world->getFences();
  for (const auto &[e, p] : fn.pieces()) {
    const bool wall = fn.types()[static_cast<size_t>(p.type)].zooWall;
    SDL_SetRenderDrawColor(ren, wall ? 230 : 40, wall ? 60 : 40, wall ? 50 : 40, 255);
    const float x0 = dst.x + e.x * s, y0 = dst.y + e.y * s;
    const int th = std::max(1, static_cast<int>(s / 3));
    SDL_Rect r = e.alongX ? SDL_Rect{static_cast<int>(x0), static_cast<int>(y0) - th / 2, std::max(1, static_cast<int>(s + 0.5f)), th}
                          : SDL_Rect{static_cast<int>(x0) - th / 2, static_cast<int>(y0), th, std::max(1, static_cast<int>(s + 0.5f))};
    SDL_RenderFillRect(ren, &r);
  }
  // (the map's objects as dots: plants green, buildings and the rest brown)
  for (const PlacedObjects::Object &o : world->getObjects().objects()) {
    if (o.fence)
      continue;
    const bool plant = o.subClass == "foliage";
    SDL_SetRenderDrawColor(ren, plant ? 30 : 110, plant ? 70 : 70, plant ? 25 : 45, 255);
    const int d = std::max(2, static_cast<int>(s * (o.className == "building" ? 2.0f : 0.6f)));
    SDL_Rect r{dst.x + static_cast<int>(o.x * s) - d / 2, dst.y + static_cast<int>(o.y * s) - d / 2, d, d};
    SDL_RenderFillRect(ren, &r);
  }
  // (the lab animals as dots)
  SDL_SetRenderDrawColor(ren, 255, 80, 40, 255);
  for (int id : labAnimals)
    if (const Animals::Member *m = world->getAnimals().member(id)) {
      SDL_Rect d{dst.x + static_cast<int>(m->x * s) - 2, dst.y + static_cast<int>(m->y * s) - 2, 5, 5};
      SDL_RenderFillRect(ren, &d);
    }
}

void LabApp::drawArt() {
  if (!viewArt)
    return;
  const int L = 512; // (room for a whale's leap: 256 clipped the big marine art)
  if (!artLayer) {
    artLayer = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, L, L);
    SDL_SetTextureBlendMode(artLayer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(artLayer, SDL_ScaleModeNearest);
  }
  SDL_Texture *old = SDL_GetRenderTarget(ren);
  SDL_SetRenderTarget(ren, artLayer);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
  SDL_RenderClear(ren);
  // (with layers, each over the last at the one anchor, on its own frame)
  // (held on one frame, viewFrame >= 0: every layer on that frame, to compare)
  std::vector<Animation *> draw = viewLayers.empty() ? std::vector<Animation *>{viewArt} : viewLayers;
  for (Animation *a : draw) {
    int frames = std::max(1, a->frameCount());
    int frame = viewFrame >= 0 ? viewFrame % frames
                               : static_cast<int>(SDL_GetTicks() / std::max<Uint32>(1, a->frameTimeMs())) % frames;
    a->drawAnchored(ren, L / 2.0f, L / 2.0f, static_cast<CompassDirection>(viewDir), nullptr, frame);
  }
  // (the anchor: where the animal stands, the point every layer shares)
  SDL_SetRenderDrawColor(ren, 255, 80, 40, 200);
  SDL_RenderDrawLine(ren, L / 2 - 4, L / 2, L / 2 + 4, L / 2);
  SDL_RenderDrawLine(ren, L / 2, L / 2 - 4, L / 2, L / 2 + 4);
  SDL_SetRenderTarget(ren, old);
  int sz = static_cast<int>(L * artZoom);
  SDL_Rect dst{viewRect.x + (viewRect.w - sz) / 2, viewRect.y + (viewRect.h - sz) / 2, sz, sz};
  SDL_SetRenderDrawColor(ren, 60, 90, 60, 255);
  SDL_RenderFillRect(ren, &viewRect);
  SDL_RenderCopy(ren, artLayer, nullptr, &dst);
}

void LabApp::drawView() {
  if (viewMode == "scene") {
    world->setView(viewRect, 1.0f);
    // What the inspector has picked gets the game's own arrow over it
    // (ui/select/selsmall, loaded at 0x51efcd), as the game's info panels
    // set it (UiGameScreen)
    world->getAnimals().selected = sel.kind == "animal" ? sel.id : -1;
    world->getGuests().selected = sel.kind == "guest" ? sel.id : -1;
    world->getStaff().selected = sel.kind == "staff" ? sel.id : -1;
    world->draw(ren);
    SDL_RenderSetViewport(ren, nullptr);
    SDL_RenderSetClipRect(ren, nullptr);
    SDL_RenderSetScale(ren, 1.0f, 1.0f);
    return;
  }
  SDL_SetRenderDrawColor(ren, 24, 24, 28, 255);
  SDL_RenderClear(ren);
  if (viewMode == "heightmap")
    drawHeightmap();
  else
    drawArt();
}

// ----------------------------------------------------------------- UI
void LabApp::uiToolbar(float h) {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, h));
  ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  bool canBuild = selScript >= 0 && buildState != 1;
  ImGui::BeginDisabled(!canBuild);
  if (ImGui::Button(buildState == 1 ? "Building..." : "Build & Run"))
    startBuild(selScript);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!scriptLive);
  if (ImGui::Button("Step"))
    doStep();
  ImGui::SameLine();
  if (ImGui::Button(autoStep ? "Pause" : "Auto"))
    autoStep = !autoStep;
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!dll);
  if (ImGui::Button("Stop"))
    unloadScript();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  ImGui::SliderFloat("steps/s", &stepsPerSecond, 0.5f, 60.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  if (ImGui::Button(playing ? "Pause world" : "Play world")) {
    playing = !playing;
    world->setPaused(!playing);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("|  View:");
  for (const char *m : {"scene", "heightmap", "sprite", "animal"}) {
    ImGui::SameLine();
    if (ImGui::RadioButton(m, viewMode == m)) {
      if (std::string(m) == "animal" && viewWhat.empty())
        view(m, "zebra");
      else if (std::string(m) == "sprite" && viewWhat.empty())
        view(m, "animals/zebra/m/walk/walk");
      else
        view(m, viewWhat.c_str());
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::Text("%s  step %d%s", runningName.empty() ? "(no script)" : runningName.c_str(), stepNo,
              scriptLive ? (autoStep ? "  running" : "  paused") : (dll ? "  finished" : ""));
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  if (ghidra->connected())
    ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1), "Ghidra: %s", ghidra->program().c_str());
  else {
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.35f, 1), "Ghidra: not connected");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("In Ghidra, with zoo.exe open: Tools > zt1-engine > Start Lab bridge.\n"
                        "Until then the Lab shows saved copies of what it's fetched before.");
  }
  ImGui::End();
}

void LabApp::uiLeft(const ImVec2 &pos, const ImVec2 &size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::Begin("Project", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
  if (ImGui::BeginTabBar("left")) {
    if (ImGui::BeginTabItem("Scripts")) {
      if (ImGui::Button("Refresh"))
        scripts = findScripts(repo);
      ImGui::SameLine();
      ImGui::BeginDisabled(selScript < 0);
      if (ImGui::Button("Edit"))
        openInEditor(scripts[selScript].path, 1);
      ImGui::EndDisabled();
      ImGui::Separator();
      for (size_t i = 0; i < scripts.size(); i++) {
        const ScriptFile &s = scripts[i];
        if (ImGui::Selectable(s.stem.c_str(), selScript == static_cast<int>(i), ImGuiSelectableFlags_AllowDoubleClick)) {
          selScript = static_cast<int>(i);
          if (!s.addresses.empty())
            selectAddress(s.addresses[0]);
          if (ImGui::IsMouseDoubleClicked(0))
            startBuild(selScript);
        }
        if (!s.about.empty())
          ImGui::TextDisabled("   %s", s.about.c_str());
      }
      if (scripts.empty())
        ImGui::TextWrapped("No scripts yet. Pick a function in Binary and press \"New script for it\".");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Binary")) {
      if (ImGui::Button("Reload knowledge base")) {
        unsigned keep = selAddr;
        kb.load(repo);
        if (keep)
          selectAddress(keep);
        addLog("Knowledge base reloaded: " + std::to_string(kb.functions.size()) + " functions");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("re/knowledge.json again: what you've mapped in Ghidra since (Ctrl+Alt+M, or ZT1_Sync)");
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##filter", "filter (name, address, field)", filter, sizeof filter);
      std::string flt = filter;
      std::transform(flt.begin(), flt.end(), flt.begin(), ::tolower);
      auto match = [&](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return flt.empty() || s.find(flt) != std::string::npos;
      };
      if (ImGui::TreeNodeEx("Known functions (knowledge base)", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < kb.functions.size(); i++) {
          const KFunction &f = kb.functions[i];
          std::string label = hex(f.address) + "  " + (f.cls.empty() ? "" : f.cls + "::") + f.name;
          if (!match(label + f.summary))
            continue;
          bool hasScript = false;
          for (const ScriptFile &s : scripts)
            for (unsigned a : s.addresses)
              hasScript |= a == f.address;
          if (hasScript)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
          if (ImGui::Selectable(label.c_str(), selAddr == f.address))
            selectAddress(f.address), selStruct = -1;
          if (hasScript)
            ImGui::PopStyleColor();
          if (ImGui::IsItemHovered() && !f.summary.empty())
            ImGui::SetTooltip("%s", f.summary.c_str());
        }
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("All of zoo.exe's functions (Ghidra)")) {
        const Json *all = ghidra->get("/functions");
        if (!all)
          ImGui::TextDisabled(ghidra->connected() ? "Asking Ghidra..." : "Start the Lab bridge in Ghidra to list them.");
        else {
          int shown = 0;
          ImGuiListClipper clip;
          std::vector<const Json *> rows;
          for (const Json &f : (*all)["functions"].items) {
            std::string label = f["address"].str() + "  " + fnLabel(parseAddr(f["address"].str()), f["name"].str());
            if (match(label))
              rows.push_back(&f);
          }
          ImGui::TextDisabled("%d of %d (* a provisional name of ours)", static_cast<int>(rows.size()),
                              static_cast<int>((*all)["functions"].items.size()));
          clip.Begin(static_cast<int>(rows.size()));
          while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
              const Json &f = *rows[r];
              unsigned fa = parseAddr(f["address"].str());
              std::string label = f["address"].str() + "  " + fnLabel(fa, f["name"].str());
              if (ImGui::Selectable(label.c_str(), selAddr == fa))
                selectAddress(fa), selStruct = -1;
            }
          (void)shown;
        }
        ImGui::TreePop();
      }
      if (ImGui::TreeNodeEx("Structs", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < kb.structs.size(); i++)
          if (match(kb.structs[i].name) || flt.empty() || [&] {
                for (const KField &f : kb.structs[i].fields)
                  if (match(f.name))
                    return true;
                return false;
              }())
            if (ImGui::Selectable((kb.structs[i].name + "  (" + std::to_string(kb.structs[i].fields.size()) + ")").c_str(),
                                  selStruct == static_cast<int>(i)))
              selStruct = static_cast<int>(i), selFunction = -1;
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("Numbers")) {
        for (const auto &[v, m] : kb.constants)
          if (match(std::to_string(v) + m))
            ImGui::TextWrapped("%d  %s", v, m.c_str());
        ImGui::TreePop();
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void LabApp::uiRight(const ImVec2 &pos, const ImVec2 &size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
  if (selAddr && selFunction < 0) {
    ImGui::SeparatorText("Selected function");
    ImGui::Text("%s  (not in the knowledge base yet)", hex(selAddr).c_str());
    ImGui::TextWrapped("Name it in Ghidra (Ctrl+Alt+M) and it comes in here.");
  }
  if (selFunction >= 0) {
    const KFunction &f = kb.functions[selFunction];
    ImGui::SeparatorText("Selected function");
    ImGui::Text("%s  %s%s", hex(f.address).c_str(), f.cls.empty() ? "" : (f.cls + "::").c_str(), f.name.c_str());
    if (f.provisional)
      ImGui::TextDisabled("provisional name");
    ImGui::TextWrapped("%s", f.summary.c_str());
    if (!f.comment.empty())
      ImGui::TextWrapped("%s", f.comment.c_str());
    ImGui::TextDisabled("by %s, %s", f.by.c_str(), f.confidence.c_str());
    bool hasScript = false;
    for (const ScriptFile &s : scripts)
      for (unsigned a : s.addresses)
        hasScript |= a == f.address;
    if (!hasScript && ImGui::Button("New script for it"))
      newScriptFor(f);
  }
  if (selStruct >= 0) {
    const KStruct &s = kb.structs[selStruct];
    ImGui::SeparatorText(s.name.c_str());
    ImGui::TextWrapped("%s", s.summary.c_str());
    if (ImGui::BeginTable("fields", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0, 260))) {
      ImGui::TableSetupColumn("offset");
      ImGui::TableSetupColumn("field");
      ImGui::TableSetupColumn("type");
      ImGui::TableHeadersRow();
      for (const KField &f : s.fields) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("+0x%03x", f.offset);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(f.name.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s\nby %s", f.summary.c_str(), f.by.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(f.type.c_str());
      }
      ImGui::EndTable();
    }
  }
  ImGui::SeparatorText("Watched");
  if (watches.empty())
    ImGui::TextDisabled("(a script's lab->watch values)");
  if (!watches.empty() && ImGui::BeginTable("watch", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    for (const Watch &w : watches) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(w.label.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.4g", w.value);
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", w.where.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::SeparatorText("Checks");
  if (checks.empty())
    ImGui::TextDisabled("(a script's lab->expect checks)");
  for (const Check &c : checks)
    ImGui::TextColored(c.fails ? ImVec4(1, 0.4f, 0.35f, 1) : ImVec4(0.4f, 0.9f, 0.45f, 1), "%s %s  (%d/%d)",
                       c.fails ? "FAIL" : "ok  ", c.what.c_str(), c.times - c.fails, c.times);
  ImGui::SeparatorText("Graphs");
  for (auto &[name, v] : plots) {
    float lo = *std::min_element(v.begin(), v.end()), hi = *std::max_element(v.begin(), v.end());
    if (hi - lo < 1e-3f)
      hi = lo + 1;
    char over[96];
    snprintf(over, sizeof over, "%s  %.4g", name.c_str(), v.empty() ? 0.0f : v.back());
    ImGui::PlotLines("##p", v.data(), static_cast<int>(v.size()), 0, over, lo, hi, ImVec2(-1, 70));
  }
  ImGui::SeparatorText("Lab animals (the engine's state)");
  for (int id : labAnimals)
    if (const Animals::Member *m = world->getAnimals().member(id)) {
      const char *sp = m->type >= 0 && m->type < static_cast<int>(world->getAnimals().types().size())
                           ? world->getAnimals().types()[m->type].key.c_str()
                           : "?";
      if (ImGui::TreeNode(reinterpret_cast<void *>(static_cast<intptr_t>(id)), "#%d %s", id, sp)) {
        auto row = [&](const char *n, double v) {
          int off = offsetOf("ZTAnimal", n);
          ImGui::Text("%-10s %8.2f", n, v);
          if (off >= 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("ZTAnimal+0x%03x", off);
          }
        };
        row("happiness", m->happiness);
        row("hunger", m->hunger);
        row("health", m->health);
        row("energy", m->energy);
        row("dirty", m->dirty);
        ImGui::Text("at %.1f, %.1f  exhibit %d", m->x, m->y, m->exhibit);
        if (ImGui::SmallButton("focus"))
          focusAnimal(id);
        ImGui::TreePop();
      }
    }
  ImGui::End();
}

void LabApp::uiBottom(const ImVec2 &pos, const ImVec2 &size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::Begin("Output", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
  if (ImGui::BeginTabBar("bottom")) {
    if (ImGui::BeginTabItem("Log", nullptr, forceTab == "Log" ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> g(logLock);
        logLines.clear();
      }
      ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
      {
        std::lock_guard<std::mutex> g(logLock);
        for (const std::string &l : logLines) {
          bool bad = l.find("FAIL") != std::string::npos || l.find("error") != std::string::npos ||
                     l.find("crashed") != std::string::npos;
          if (bad)
            ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", l.c_str());
          else
            ImGui::TextUnformatted(l.c_str());
        }
        if (logToBottom) {
          ImGui::SetScrollHereY(1.0f);
          logToBottom = false;
        }
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Decompiled (Ghidra)", nullptr, forceTab == "Decompiled (Ghidra)" ? ImGuiTabItemFlags_SetSelected : 0)) {
      uiGhidraCode(false);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Assembly (Ghidra)", nullptr, forceTab == "Assembly (Ghidra)" ? ImGuiTabItemFlags_SetSelected : 0)) {
      uiGhidraCode(true);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Calls & uses (Ghidra)", nullptr, forceTab == "Calls & uses (Ghidra)" ? ImGuiTabItemFlags_SetSelected : 0)) {
      uiGhidraCalls();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Engine code", nullptr, forceTab == "Engine code" ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (!selAddr) {
        ImGui::TextDisabled("Pick a function (Binary tab, a script, or Show in Lab from Ghidra) to see the engine code "
                            "that implements it.");
      } else {
        auto range = kb.refs.equal_range(selAddr);
        if (range.first == range.second)
          ImGui::TextDisabled("No engine code cites %s yet: when you implement it, cite it as // ... (zoo.exe %s): ...",
                              hex(selAddr).c_str(), hex(selAddr).c_str());
        int n = 0;
        for (auto it = range.first; it != range.second; ++it, ++n) {
          const std::string &where = it->second.first;
          size_t colon = where.rfind(':');
          fs::path file = repo / where.substr(0, colon);
          int line = std::atoi(where.substr(colon + 1).c_str());
          ImGui::PushID(n);
          ImGui::SeparatorText(where.c_str());
          if (ImGui::SmallButton("Open in code"))
            openInEditor(file, line);
          std::istringstream src(readFile(file));
          std::string l;
          int k = 0;
          while (std::getline(src, l)) {
            k++;
            if (k >= line - 3 && k <= line + 30) {
              if (k == line)
                ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%5d  %s", k, l.c_str());
              else
                ImGui::Text("%5d  %s", k, l.c_str());
            }
          }
          ImGui::PopID();
        }
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
  if (ImGui::GetFrameCount() > 5)
    forceTab.clear();
}

std::string LabApp::fnLabel(unsigned addr, const std::string &ghidraName) const {
  // (our name for it, if the knowledge base has one, over Ghidra's)
  for (const KFunction &f : kb.functions)
    if (f.address == addr)
      return (f.cls.empty() ? "" : f.cls + "::") + f.name + (f.provisional ? "*" : "");
  return ghidraName;
}

// What the code uses that we know: offsets that are mapped fields, numbers
// that are known
void LabApp::uiNotes(const std::string &code) {
  static const std::regex off(R"(\+ (0x[0-9a-f]+)\))"), idx(R"(\[(0x[0-9a-f]+)\])"), num(R"(\b(\d{3,6})\b)");
  std::set<unsigned> offs;
  for (std::sregex_iterator it(code.begin(), code.end(), off), end; it != end; ++it)
    offs.insert(parseAddr((*it)[1].str()));
  std::set<int> nums;
  for (std::sregex_iterator it(code.begin(), code.end(), num), end; it != end; ++it)
    nums.insert(std::atoi((*it)[1].str().c_str()));
  bool any = false;
  for (unsigned o : offs)
    for (const KStruct &s : kb.structs)
      for (const KField &f : s.fields)
        if (f.offset == o) {
          if (!any)
            ImGui::SeparatorText("Known in this code");
          any = true;
          ImGui::BulletText("+0x%x  could be %s.%s (%s)", o, s.name.c_str(), f.name.c_str(), f.type.c_str());
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nby %s", f.summary.c_str(), f.by.c_str());
        }
  for (int n : nums)
    for (const auto &[v, m] : kb.constants)
      if (v == n) {
        if (!any)
          ImGui::SeparatorText("Known in this code");
        any = true;
        ImGui::BulletText("%d  %s", n, m.c_str());
      }
  (void)idx;
}

void LabApp::uiGhidraCode(bool assembly) {
  if (!selAddr) {
    ImGui::TextDisabled("Pick a function: Binary tab, a script, or Show in Lab (Ctrl+Alt+L) in Ghidra.");
    return;
  }
  char a[16];
  snprintf(a, sizeof a, "%x", selAddr);
  std::string path = std::string(assembly ? "/disasm" : "/decompile") + "?addr=" + a;
  bool cached = false;
  const Json *j = ghidra->get(path, &cached);
  if (!j) {
    ImGui::TextDisabled(ghidra->connected() ? "Asking Ghidra..." : "Ghidra isn't connected and there's no saved copy of this one. "
                                                                    "In Ghidra: Tools > zt1-engine > Start Lab bridge.");
    return;
  }
  if (!(*j)["error"].str().empty()) {
    ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Ghidra: %s", (*j)["error"].str().c_str());
    return;
  }
  if (cached)
    ImGui::TextDisabled("(a saved copy: Ghidra isn't connected)");
  if (ImGui::SmallButton("Refresh")) {
    ghidra->forget(path);
    return;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Show in Ghidra"))
    ghidra->post("/goto?addr=" + std::string(a));
  ImGui::BeginChild("code", ImVec2(assembly ? 0 : ImGui::GetContentRegionAvail().x * 0.68f, 0), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  std::string text;
  if (assembly) {
    for (const Json &l : (*j)["lines"].items) {
      std::string c = l["comment"].str();
      ImGui::TextDisabled("%s", l["at"].str().c_str());
      ImGui::SameLine();
      ImGui::TextUnformatted(l["text"].str().c_str());
      if (!c.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "; %s", c.c_str());
      }
      text += l["text"].str() + "\n";
    }
  } else {
    text = (*j)["c"].str();
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
      if (!l.empty() && l.back() == '\r')
        l.pop_back();
      bool comment = l.find("/*") != std::string::npos || l.find("[zt1]") != std::string::npos;
      if (comment)
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "%s", l.c_str());
      else
        ImGui::TextUnformatted(l.c_str());
    }
  }
  ImGui::EndChild();
  if (!assembly) {
    ImGui::SameLine();
    ImGui::BeginChild("notes", ImVec2(0, 0), ImGuiChildFlags_Borders);
    uiNotes(text);
    ImGui::EndChild();
  }
}

void LabApp::uiGhidraCalls() {
  if (!selAddr) {
    ImGui::TextDisabled("Pick a function.");
    return;
  }
  char a[16];
  snprintf(a, sizeof a, "%x", selAddr);
  const Json *j = ghidra->get(std::string("/function?addr=") + a);
  if (!j) {
    ImGui::TextDisabled(ghidra->connected() ? "Asking Ghidra..." : "Ghidra isn't connected (no saved copy).");
    return;
  }
  ImGui::Text("%s  %s", (*j)["address"].str().c_str(), (*j)["signature"].str().c_str());
  std::string tags;
  for (const Json &t : (*j)["tags"].items)
    tags += t.str() + " ";
  ImGui::TextDisabled("%d bytes  %s", static_cast<int>((*j)["size"].num()), tags.c_str());
  auto list = [&](const char *title, const Json &items) {
    ImGui::SeparatorText((std::string(title) + " (" + std::to_string(items.items.size()) + ")").c_str());
    for (const Json &f : items.items) {
      unsigned fa = parseAddr(f["address"].str());
      std::string lbl = f["address"].str() + "  " + fnLabel(fa, f["name"].str());
      if (ImGui::Selectable(lbl.c_str()))
        selectAddress(fa);
    }
  };
  if (ImGui::BeginTable("calls", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableNextColumn();
    ImGui::BeginChild("callers", ImVec2(0, 0));
    list("Called by", (*j)["callers"]);
    ImGui::EndChild();
    ImGui::TableNextColumn();
    ImGui::BeginChild("callees", ImVec2(0, 0));
    list("Calls", (*j)["callees"]);
    ImGui::EndChild();
    ImGui::TableNextColumn();
    ImGui::BeginChild("uses", ImVec2(0, 0));
    ImGui::SeparatorText("Strings it uses");
    for (const Json &s : (*j)["strings"].items)
      ImGui::TextWrapped("%s  \"%s\"", s["at"].str().c_str(), s["text"].str().c_str());
    ImGui::SeparatorText("Numbers it uses");
    for (const Json &s : (*j)["scalars"].items) {
      int v = static_cast<int>(s["value"].num());
      std::string meaning;
      for (const auto &[k, m] : kb.constants)
        if (k == v)
          meaning = m;
      if (meaning.empty())
        ImGui::TextDisabled("%s  %d (0x%x)", s["at"].str().c_str(), v, v);
      else
        ImGui::Text("%s  %d  %s", s["at"].str().c_str(), v, meaning.c_str());
    }
    ImGui::EndChild();
    ImGui::EndTable();
  }
}

void LabApp::ui() {
  ImVec2 ds = ImGui::GetIO().DisplaySize;
  const float top = 34, left = 300, right = 360, bottom = 240;
  uiToolbar(top);
  uiLeft(ImVec2(0, top), ImVec2(left, ds.y - top));
  uiRight(ImVec2(ds.x - right, top), ImVec2(right, ds.y - top));
  uiBottom(ImVec2(left, ds.y - bottom), ImVec2(ds.x - left - right, bottom));
  viewRect = SDL_Rect{static_cast<int>(left), static_cast<int>(top), static_cast<int>(ds.x - left - right),
                      static_cast<int>(ds.y - top - bottom)};
  // The view's own controls, over its corner
  ImGui::SetNextWindowPos(ImVec2(left + 8, top + 8));
  ImGui::SetNextWindowBgAlpha(0.6f);
  ImGui::Begin("##viewctl", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
  if (viewMode == "scene") {
    ImGui::Text("Scene  (drag to pan, wheel to zoom)");
    if (ImGui::SmallButton("Rotate"))
      world->rotateView(1);
    ImGui::SameLine();
    if (ImGui::SmallButton("To the lab exhibit"))
      world->centreOn(labX + labW / 2.0f, labY + labH / 2.0f);
  } else if (viewMode == "heightmap") {
    ImGui::Text("Terrain: the whole map from above - each tile its ground, lit by its height; paths, objects (lab exhibit green, its animals red)");
  } else {
    ImGui::Text("%s: %s", viewMode.c_str(), viewWhat.c_str());
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputTextWithHint("##what", viewMode == "animal" ? "a species: zebra" : "an animation: animals/zebra/m/walk/walk",
                                 viewBuf, sizeof viewBuf, ImGuiInputTextFlags_EnterReturnsTrue))
      view(viewMode.c_str(), std::string(viewBuf).c_str());
    if (ImGui::SmallButton("Turn"))
      viewDir = (viewDir + 1) % 4;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("zoom", &artZoom, 1.0f, 6.0f, "%.1f");
  }
  ImGui::End();
}

int LabApp::run(const std::string &mapStem) {
  repo = findRepo();
  if (repo.empty()) {
    SDL_Log("[Lab] can't find the repo (re/lab/lab_api.h) from here");
    return 1;
  }
  SDL_SetWindowTitle(win, "ZT1 Lab");
  SDL_SetWindowSize(win, 1600, 950);
  SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_RenderSetLogicalSize(ren, 0, 0);
  SDL_RenderSetScale(ren, 1.0f, 1.0f);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
  ImGui_ImplSDLRenderer2_Init(ren);

  // (last session's script builds: the numbered DLLs, not in use now)
  std::error_code ec;
  for (const auto &e : fs::directory_iterator(repo / "build" / "lab", ec))
    if (std::regex_match(e.path().filename().string(), std::regex(R"(.+_\d+\.(dll|exp|lib))")))
      fs::remove(e.path(), ec);
  kb.load(repo);
  scripts = findScripts(repo);
  ghidra = std::make_unique<GhidraLink>(repo / "re" / "private" / "ghidra");
  std::string exePath = std::getenv("ZT1_EXE") ? std::getenv("ZT1_EXE")
                                                : "C:/Program Files (x86)/Microsoft Games/Zoo Tycoon/zoo.exe";
  if (!exe.load(exePath))
    addLog("zoo.exe not found at " + exePath + " (set ZT1_EXE): exeU32 etc. read 0");
  addLog("ZT1 Lab: " + std::to_string(kb.functions.size()) + " functions, " + std::to_string(kb.structs.size()) +
         " structs from re/knowledge.json; " + std::to_string(scripts.size()) + " scripts");
  setupScene(mapStem);
  world->setPaused(true);
  addLog("Pick a script and press Build & Run (or double-click it).");

  // Testing without a mouse: ZT_LAB_RUN=<script> builds and runs it,
  // ZT_LAB_SELECT=<address> picks a function, ZT_LAB_TAB=<bottom tab>,
  // ZT_LAB_SHOTS=<dir> saves the window every 4 s and closes after
  // ZT_LAB_SECONDS (40)
  if (const char *r = std::getenv("ZT_LAB_RUN"))
    for (size_t i = 0; i < scripts.size(); i++)
      if (scripts[i].stem == r) {
        selScript = static_cast<int>(i);
        startBuild(selScript);
      }
  if (const char *a = std::getenv("ZT_LAB_SELECT"))
    selectAddress(parseAddr(a));
  const char *shots = std::getenv("ZT_LAB_SHOTS");
  const Uint32 startedAt = SDL_GetTicks();
  const Uint32 lastFor = std::getenv("ZT_LAB_SECONDS") ? std::atoi(std::getenv("ZT_LAB_SECONDS")) * 1000u : 40000u;
  Uint32 lastShot = startedAt;
  int shotNo = 0;
  if (const char *t = std::getenv("ZT_LAB_TAB"))
    forceTab = t;
  if (const char *sps = std::getenv("ZT_LAB_SPS"))
    stepsPerSecond = static_cast<float>(std::atof(sps));

  bool running = true;
  Uint32 lastPoll = 0;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      if (e.type == SDL_QUIT)
        running = false;
      bool overUi = ImGui::GetIO().WantCaptureMouse;
      if (viewMode == "scene" && !overUi) {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
          dragging = true;
          dragX = e.button.x;
          dragY = e.button.y;
        } else if (e.type == SDL_MOUSEWHEEL) {
          g_ZoomLevel = std::clamp(g_ZoomLevel * (e.wheel.y > 0 ? 1.25f : 0.8f), 0.25f, 4.0f);
        }
      }
      if (e.type == SDL_MOUSEBUTTONUP)
        dragging = false;
      if (e.type == SDL_MOUSEMOTION && dragging) {
        world->getRenderer().getCamera().x -= static_cast<int>((e.motion.x - dragX) / std::max(0.1f, g_ZoomLevel));
        world->getRenderer().getCamera().y -= static_cast<int>((e.motion.y - dragY) / std::max(0.1f, g_ZoomLevel));
        dragX = e.motion.x;
        dragY = e.motion.y;
      }
    }
    if (buildState == 2 || buildState == 3)
      finishBuild();
    Uint32 now = SDL_GetTicks();
    if (scriptLive && autoStep && now - lastStepAt >= 1000.0f / stepsPerSecond) {
      lastStepAt = now;
      doStep();
    }
    if (playing) {
      static Uint8 none[SDL_NUM_SCANCODES] = {};
      static Uint32 last = now;
      world->update(none, std::min(0.1f, (now - last) / 1000.0f));
      last = now;
    } else {
      world->getRenderer().getCamera().zoom = g_ZoomLevel;
    }
    if (now - lastPoll > 400) {
      lastPoll = now;
      pollGhidra();
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ui();
    ImGui::Render();
    drawView();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
    if (shots && now - lastShot >= 4000) {
      lastShot = now;
      int w, h;
      SDL_GetRendererOutputSize(ren, &w, &h);
      if (SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)) {
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
        SDL_SaveBMP(sf, (std::string(shots) + "/lab" + std::to_string(++shotNo) + ".bmp").c_str());
        SDL_FreeSurface(sf);
      }
    }
    SDL_RenderPresent(ren);
    if (shots && now - startedAt > lastFor)
      running = false;
  }
  unloadScript();
  if (buildThread.joinable())
    buildThread.join();
  ghidra.reset();
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  return 0;
}

// ------------------------------------------------------------- host mode
int LabApp::animalAt(int x, int y) {
  // (as World::pick: window pixels to logical, at the drawing zoom)
  Camera &c = world->getRenderer().getCamera();
  float zoom = g_ZoomLevel > 0 ? g_ZoomLevel : 1.0f;
  float lx = (x - viewRect.x) / zoom, ly = (y - viewRect.y) / zoom;
  float saved = c.zoom;
  c.zoom = zoom;
  int id = world->getAnimals().pick(lx, ly, world->getRenderer(), world->getMap());
  c.zoom = saved;
  return id;
}

void LabApp::sendScripts() {
  std::string list;
  for (const ScriptFile &s : scripts) {
    std::string addrs;
    for (unsigned a : s.addresses)
      addrs += (addrs.empty() ? "" : ",") + jq(hex(a));
    list += (list.empty() ? "" : ",") + std::string("{\"stem\":") + jq(s.stem) + ",\"about\":" + jq(s.about) +
            ",\"path\":" + jq(s.path.string()) + ",\"addresses\":[" + addrs + "]}";
  }
  host->send("{\"t\":\"scripts\",\"list\":[" + list + "]}");
}

void LabApp::sendState() {
  LabTrace::Mute quiet; // (the Lab reading the world: not the game's steps)
  std::string w, c, p, a;
  for (const Watch &x : watches)
    w += (w.empty() ? "" : ",") + std::string("{\"label\":") + jq(x.label) + ",\"value\":" + jnum(x.value) +
         ",\"where\":" + jq(x.where) + "}";
  for (const Check &x : checks)
    c += (c.empty() ? "" : ",") + std::string("{\"what\":") + jq(x.what) + ",\"ok\":" + (x.fails ? "false" : "true") +
         ",\"times\":" + std::to_string(x.times) + ",\"fails\":" + std::to_string(x.fails) + "}";
  for (const auto &[name, v] : plots) {
    std::string pts;
    size_t from = v.size() > 200 ? v.size() - 200 : 0;
    for (size_t i = from; i < v.size(); i++)
      pts += (i > from ? "," : "") + jnum(v[i]);
    p += (p.empty() ? "" : ",") + jq(name) + ":[" + pts + "]";
  }
  const Animals &an = world->getAnimals();
  for (const Animals::Member &m : an.members()) {
    int sx = 0, sy = 0;
    if (viewMode == "scene") {
      const MapTile *t = world->getMap().getTile(static_cast<int>(m.x), static_cast<int>(m.y));
      world->pointToWindow(m.x, m.y, t ? static_cast<float>(t->cornerHeight[0]) : 0.0f, sx, sy);
    }
    std::string sp = m.type >= 0 && m.type < static_cast<int>(an.types().size()) ? an.types()[m.type].key : "?";
    bool lab = std::find(labAnimals.begin(), labAnimals.end(), m.id) != labAnimals.end();
    a += (a.empty() ? "" : ",") + std::string("{\"id\":") + std::to_string(m.id) + ",\"species\":" + jq(sp) +
         ",\"nameId\":" + std::to_string(m.type >= 0 && m.type < static_cast<int>(an.types().size()) ? an.types()[m.type].nameId : 0) +
         ",\"female\":" + (m.female ? "true" : "false") + ",\"baby\":" + (m.baby ? "true" : "false") +
         ",\"lab\":" + (lab ? "true" : "false") + ",\"exhibit\":" + std::to_string(m.exhibit) +
         ",\"x\":" + jnum(m.x) + ",\"y\":" + jnum(m.y) + ",\"sx\":" + std::to_string(sx) + ",\"sy\":" + std::to_string(sy) +
         ",\"happiness\":" + jnum(m.happiness) + ",\"pending\":" + jnum(m.pending) + ",\"hunger\":" + jnum(m.hunger) +
         ",\"health\":" + jnum(m.health) + ",\"energy\":" + jnum(m.energy) + ",\"dirty\":" + jnum(m.dirty) +
         ",\"sick\":" + (m.sick ? "true" : "false") + "}";
  }
  const char *build = buildState == 1 ? "building" : "idle";
  host->send(std::string("{\"t\":\"state\",\"step\":") + std::to_string(stepNo) + ",\"live\":" + (scriptLive ? "true" : "false") +
             ",\"auto\":" + (autoStep ? "true" : "false") + ",\"loaded\":" + (dll ? "true" : "false") +
             ",\"sps\":" + jnum(stepsPerSecond) + ",\"playing\":" + (playing ? "true" : "false") + ",\"script\":" +
             jq(runningName) + ",\"build\":" + jq(build) + ",\"view\":" + jq(viewMode) + ",\"what\":" + jq(viewWhat) +
             ",\"zoom\":" + jnum(g_ZoomLevel) + ",\"exhibit\":{\"x\":" + std::to_string(labX) + ",\"y\":" +
             std::to_string(labY) + ",\"w\":" + std::to_string(labW) + ",\"h\":" + std::to_string(labH) +
             "},\"watches\":[" + w + "],\"checks\":[" + c + "],\"plots\":{" + p + "},\"animals\":[" + a + "]" +
             ",\"scene\":" + sceneJson() + "}");
}

// The Scene view's editor: the map, the clock, the tool, the cursor, what's
// picked, how much there is, the exhibits' labels
std::string LabApp::sceneJson() {
  std::string labels;
  if (showExhibits && viewMode == "scene")
    for (const Fences::Exhibit &e : world->getFences().exhibits())
      if (e.named && !e.tiles.empty()) {
        float sx = 0, sy = 0;
        for (const auto &t : e.tiles)
          sx += t.first + 0.5f, sy += t.second + 0.5f;
        sx /= e.tiles.size(), sy /= e.tiles.size();
        const MapTile *t = world->getMap().getTile(static_cast<int>(sx), static_cast<int>(sy));
        int wx = 0, wy = 0;
        world->pointToWindow(sx, sy, t ? static_cast<float>(t->cornerHeight[0]) : 0.0f, wx, wy);
        if (wx > -200 && wy > -200 && wx < hostW + 200 && wy < hostH + 200)
          labels += (labels.empty() ? "" : ",") + JObj().num("id", e.id).str("name", e.name).flag("tank", e.tank).num("sx", wx).num("sy", wy).done();
      }
  return JObj()
      .str("map", mapStem)
      .str("mapName", mapName)
      .flag("flat", mapFlat)
      .flag("editing", world->getFences().isEditing())
      .num("loads", loads)
      .num("clock", simClock)
      .num("speed", speed)
      .add("tool", JObj()
                       .str("kind", tool.kind)
                       .str("item", tool.item)
                       .flag("female", tool.female)
                       .num("facing", tool.facing)
                       .flag("painting", tool.painting)
                       .num("terrainType", tool.terrainType)
                       .num("size", tool.size)
                       .num("mode", tool.mode)
                       .num("walkway", world->getBuildHeight())
                       .str("lineMode", World::fenceModeName(tool.kind == "path" ? world->getPathMode() : world->getFenceMode()))
                       .done())
      .add("hover", hoverJson())
      .add("sel", selJson())
      .add("counts", countsJson())
      .add("overlays", JObj().flag("grid", showGrid).flag("exhibits", showExhibits).flag("routes", showRoutes).flag("paths", world->debugPaths).flag("footprints", showFootprints).done())
      .add("routeTest", tool.kind != "route" ? std::string("null")
                        : JObj().flag("startSet", routeStartSet).num("startX", routeSX).num("startY", routeSY)
                              .add("search", hasTest ? searchJson(testSearch) : std::string("null")).done())
      .add("labels", "[" + labels + "]")
      .str("asking", asking)
      .flag("canUndo", world->canUndo())
      .add("pins", [&] {
        std::string out;
        for (const Pin &p : pins)
          out += (out.empty() ? "" : ",") + JObj().str("kind", p.kind).num("id", p.id).str("field", p.field).num("value", p.value).done();
        return "[" + out + "]";
      }())
      .add("trace", JObj()
                        .str("mode", traceMode)
                        .flag("stepEach", stepEach)
                        .flag("paused", inBreak)
                        .str("at", stepAt)
                        .str("who", stepWho.kind ? stepWho.kind : "")
                        .num("whoId", stepWho.id)
                        .done())
      .done();
}

void LabApp::sendFrame() {
  // The window is the view's size (off the screen): drawn, read back, sent
  int ow = 0, oh = 0;
  SDL_GetWindowSize(win, &ow, &oh);
  if (ow != hostW || oh != hostH)
    SDL_SetWindowSize(win, hostW, hostH);
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(ren, &w, &h);
  if (w <= 0 || h <= 0)
    return;
  viewRect = SDL_Rect{0, 0, w, h};
  LabTrace::Mute quiet;
  // (how long each part takes: in the log every 5 s, to find what's slow)
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  Uint64 t0 = SDL_GetPerformanceCounter();
  drawView();
  drawOverlays();
  Uint64 t1 = SDL_GetPerformanceCounter();
  // The pixels as they are, in the order the app's canvas takes them (R, G,
  // B, A: ABGR8888 in memory): no JPEG - encoding took ten times as long as
  // drawing, and over a local connection the bytes are cheap
  struct Mem {
    std::vector<unsigned char> bytes;
  } mem;
  mem.bytes.resize(static_cast<size_t>(w) * h * 4);
  if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, mem.bytes.data(), w * 4) != 0)
    return;
  SDL_RenderPresent(ren);
  Uint64 t2 = SDL_GetPerformanceCounter();
  // (what's read back from the window can carry no alpha: opaque)
  for (size_t i = 3; i < mem.bytes.size(); i += 4)
    mem.bytes[i] = 255;
  Uint64 t3 = SDL_GetPerformanceCounter();
  host->sendFrame("{\"t\":\"frame\",\"fmt\":\"rgba\",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(h) +
                      ",\"n\":" + std::to_string(mem.bytes.size()) + "}",
                  mem.bytes);
  Uint64 t4 = SDL_GetPerformanceCounter();
  static double sDraw = 0, sRead = 0, sEnc = 0, sSend = 0, sBytes = 0;
  static int frames = 0;
  static Uint32 since = SDL_GetTicks();
  sDraw += (t1 - t0) / freq, sRead += (t2 - t1) / freq, sEnc += (t3 - t2) / freq, sSend += (t4 - t3) / freq;
  sBytes += static_cast<double>(mem.bytes.size());
  frames++;
  if (SDL_GetTicks() - since >= 5000) {
    double secs = (SDL_GetTicks() - since) / 1000.0;
    char b[256];
    snprintf(b, sizeof b, "[frames] %.1f a second at %dx%d: draw %.1f ms, read back %.1f, alpha %.1f, send %.1f (%.0f KB)",
             frames / secs, w, h, 1000 * sDraw / frames, 1000 * sRead / frames, 1000 * sEnc / frames, 1000 * sSend / frames,
             sBytes / frames / 1024);
    addLog(b);
    sDraw = sRead = sEnc = sSend = sBytes = 0;
    frames = 0;
    since = SDL_GetTicks();
  }
}

// --------------------------------------------- the Scene view's editor
// An image as PNG or JPEG bytes
std::vector<unsigned char> encodeImage(SDL_Surface *sf, bool png) {
  struct Mem {
    std::vector<unsigned char> bytes;
  } mem;
  SDL_RWops *rw = SDL_AllocRW();
  rw->hidden.unknown.data1 = &mem;
  rw->size = [](SDL_RWops *) -> Sint64 { return -1; };
  rw->seek = [](SDL_RWops *, Sint64, int) -> Sint64 { return -1; };
  rw->read = [](SDL_RWops *, void *, size_t, size_t) -> size_t { return 0; };
  rw->write = [](SDL_RWops *ctx, const void *p, size_t size, size_t n) -> size_t {
    auto *m = static_cast<Mem *>(ctx->hidden.unknown.data1);
    const unsigned char *b = static_cast<const unsigned char *>(p);
    m->bytes.insert(m->bytes.end(), b, b + size * n);
    return n;
  };
  rw->close = [](SDL_RWops *ctx) -> int {
    SDL_FreeRW(ctx);
    return 0;
  };
  if (png)
    IMG_SavePNG_RW(sf, rw, 1);
  else
    IMG_SaveJPG_RW(sf, rw, 1, 82);
  return mem.bytes;
}

std::string base64(const std::vector<unsigned char> &b) {
  static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((b.size() + 2) / 3 * 4);
  for (size_t i = 0; i < b.size(); i += 3) {
    unsigned v = b[i] << 16 | (i + 1 < b.size() ? b[i + 1] << 8 : 0) | (i + 2 < b.size() ? b[i + 2] : 0);
    out += A[v >> 18 & 63];
    out += A[v >> 12 & 63];
    out += i + 1 < b.size() ? A[v >> 6 & 63] : '=';
    out += i + 2 < b.size() ? A[v & 63] : '=';
  }
  return out;
}

// A placed object's catalogue file (as PlacedObjects reads it back:
// scenery/<subclass>/<type>.ai, or a downloaded entity's .ucs)
std::string objectFile(const PlacedObjects::Object &o) { return PlacedObjects::fileOf(o); }

// "ttConiferousForestFloor" -> "Coniferous Forest Floor"
std::string spaced(const std::string &key) {
  std::string s = key.rfind("tt", 0) == 0 ? key.substr(2) : key, out;
  for (size_t i = 0; i < s.size(); i++) {
    if (i > 0 && std::isupper(static_cast<unsigned char>(s[i])) && !std::isupper(static_cast<unsigned char>(s[i - 1])))
      out += ' ';
    out += s[i];
  }
  return out;
}

std::string LabApp::terrainName(int type) {
  if (terrains.empty())
    for (const std::string &cfg : rm->listResources("terrain/tiletex", ".cfg"))
      if (IniReader *ini = rm->getIniReader(cfg)) {
        for (const std::string &section : ini->getSections()) {
          int t = ini->getInt(section, "type", -1);
          if (t >= 0)
            terrains[t] = {spaced(section), ini->get(section, "icon", ""), ini->getInt(section, "cost", 0),
                           ini->getInt(section, "water", 0)};
        }
        delete ini;
      }
  auto it = terrains.find(type);
  return it == terrains.end() ? "type " + std::to_string(type) : it->second.name;
}

void LabApp::note(const std::string &text) {
  addLog(text);
  host->send("{\"t\":\"note\",\"text\":" + jq(text) + "}");
}

// The tool picked in the app, given to the World (as the game's panels do)
void LabApp::applyTool() {
  TerrainTool &tt = world->getTerrainTool();
  if (tt.isActive() && tool.kind != "terrain")
    tt.accept(); // (no money in the lab: the changes are kept)
  world->setFenceTool(-1);
  world->setPathTool("", 0);
  world->setObjectTool("", 0, 0);
  world->setAnimalTool(-1, false);
  world->setStaffTool(-1);
  world->setGateTool(false);
  world->setFilterTool(false);
  world->setBulldozer(false);
  if (tool.kind != "terrain")
    world->setTerrainTool(false, true, 0, 2, 0);
  int cost = 0;
  if (const CatalogItem *ci = ItemCatalog::get().find(tool.item))
    cost = ci->cost;
  const std::string &k = tool.kind;
  if (k == "bulldoze")
    world->setBulldozer(true);
  else if (k == "gate")
    world->setGateTool(true);
  else if (k == "filter")
    world->setFilterTool(world->getFences().selectFilter(tool.item));
  else if (k == "fence") {
    const auto &types = world->getFences().types();
    for (size_t i = 0; i < types.size(); i++)
      if (types[i].file == tool.item)
        world->setFenceTool(static_cast<int>(i));
  } else if (k == "path") {
    size_t slash = tool.item.find_last_of('/'), dot = tool.item.find_last_of('.');
    world->setPathTool(tool.item.substr(slash + 1, dot - slash - 1), cost);
  } else if (k == "object")
    world->setObjectTool(tool.item, cost, tool.facing);
  else if (k == "animal")
    // (any species the game's files have, the expansions' too: the lab is
    // where what isn't done yet is tried)
    world->setAnimalTool(world->getAnimals().typeOfFile(tool.item), tool.female);
  else if (k == "staff")
    world->setStaffTool(world->getStaff().typeOfFile(tool.item));
  else if (k == "terrain")
    world->setTerrainTool(true, tool.painting, tool.terrainType, std::clamp(tool.size, 1, 5), tool.mode);
  else if (k == "route")
    ; // (the Lab's own: a start, then where to)
  else
    tool.kind = "select";
  if (tool.kind != "route")
    routeStartSet = hasTest = false;
}

// The scenery under window pixels (in front first; -1 none)
int LabApp::objectAt(int x, int y) {
  Camera &c = world->getRenderer().getCamera();
  float zoom = c.zoom > 0 ? c.zoom : 1.0f;
  float lx = (x - viewRect.x) / zoom, ly = (y - viewRect.y) / zoom;
  return world->getObjects().pick(lx, ly, world->getRenderer(), world->getMap());
}

// The mouse over the view (the app's canvas pixels, which are the engine's
// window pixels): a tool's press, drag and release go to the World as the
// game's do; with no tool a click picks what's there
void LabApp::sceneMouse(const std::string &ev, int x, int y) {
  if (viewMode != "scene")
    return;
  ZT_WHO("tool", 0); // (what the engine does now is for the tool used)
  if (ev == "move") {
    world->mouseMove(x, y);
    float wx, wy;
    hoverOn = world->groundAt(x, y, wx, wy);
    if (hoverOn) {
      hoverX = static_cast<int>(std::floor(wx));
      hoverY = static_cast<int>(std::floor(wy));
    }
    // (a drag with a tool held - painting the ground, raising it - kept in
    // the rebuild list a tile at a time too: its press and release alone
    // made only its two ends again)
    if (toolHeld && hoverOn && (hoverX != heldX || hoverY != heldY) && rebuild.size() < 4000) {
      heldX = hoverX;
      heldY = hoverY;
      char b[160];
      snprintf(b, sizeof b, "{\"cmd\":\"mouseWorld\",\"wx\":%.4f,\"wy\":%.4f,\"ev\":\"move\"}", wx, wy);
      rebuild.push_back(Json::parse(b));
    }
  } else if (ev == "leave") {
    world->hoverOff();
    hoverOn = false;
  } else if (ev == "down" || ev == "up" || ev == "click") {
    // (a tool used: kept in the rebuild list at its world place)
    float rx = 0, ry = 0;
    if (tool.kind != "select" && tool.kind != "route" && world->groundAt(x, y, rx, ry) && rebuild.size() < 4000) {
      char b[160];
      snprintf(b, sizeof b, "{\"cmd\":\"mouseWorld\",\"wx\":%.4f,\"wy\":%.4f,\"ev\":\"%s\"}", rx, ry, ev.c_str());
      rebuild.push_back(Json::parse(b));
    }
    toolHeld = ev == "down" && tool.kind != "select" && tool.kind != "route";
    heldX = static_cast<int>(std::floor(rx));
    heldY = static_cast<int>(std::floor(ry));
  }
  if (ev == "down") {
    float wx, wy;
    if (tool.kind == "route") {
      if (world->groundAt(x, y, wx, wy))
        routeClick(static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wy)));
    } else if (tool.kind != "select")
      toolResult(world->mouseDown(x, y), false, x, y);
  } else if (ev == "up") {
    if (tool.kind != "select" && tool.kind != "route")
      toolResult(world->mouseUp(x, y), false, x, y);
  } else if (ev == "click") {
    World::ToolResult r = world->mouseDown(x, y);
    World::ToolResult up = world->mouseUp(x, y);
    if (r.exhibit < 0)
      r.exhibit = up.exhibit;
    toolResult(r, true, x, y);
  } else if (ev == "cancel") {
    routeStartSet = hasTest = false;
    world->cancelTool();
    tool = Tool{};
    applyTool();
  }
}

void LabApp::toolResult(const World::ToolResult &r, bool selecting, int x, int y) {
  Fences &fn = world->getFences();
  // A new exhibit: named as the game names it ("Exhibit 3"), with its gate
  for (int id : r.newExhibits)
    if (const Fences::Exhibit *ex = fn.exhibit(id)) {
      std::string name = rm->getString(ex->tank ? 6209 : 6208) + " " + std::to_string(id);
      fn.finishExhibit(id, name, world->getMap());
      note("A new exhibit: " + name);
    }
  std::string ask;
  if (!r.askMerge.empty())
    asking = "remove", ask = r.askMerge;
  if (r.askEscape)
    asking = "remove", ask = rm->getString(157);
  if (r.askDrainTank)
    asking = "drain", ask = rm->getString(160);
  if (!ask.empty())
    host->send("{\"t\":\"ask\",\"text\":" + jq(ask) + "}");
  if (r.outsideZoo)
    note(rm->getString(10311));
  if (r.messageId)
    note(rm->getString(r.messageId));
  if (!selecting)
    return;
  // What was clicked: someone, then a building or scenery, then an
  // exhibit, else the ground
  Sel s;
  if (r.animal >= 0)
    s = {"animal", r.animal};
  else if (r.guest >= 0)
    s = {"guest", r.guest};
  else if (r.staff >= 0)
    s = {"staff", r.staff};
  else if (r.building >= 0)
    s = {"object", world->objectsMutable().indexOf(r.building)};
  else if (int o = objectAt(x, y); o >= 0 && !world->getObjects().objects()[o].fence)
    s = {"object", o};
  else if (r.exhibit >= 0)
    s = {"exhibit", r.exhibit};
  else {
    float wx, wy;
    if (world->groundAt(x, y, wx, wy)) {
      int tx = static_cast<int>(std::floor(wx)), ty = static_cast<int>(std::floor(wy));
      int ex = fn.exhibitAt(tx, ty);
      const Fences::Exhibit *e = ex >= 0 ? fn.exhibit(ex) : nullptr;
      s = e && e->named ? Sel{"exhibit", ex} : Sel{"tile", -1, tx, ty};
    }
  }
  sel = s;
}

// Where what's picked is (tiles)
bool LabApp::selPoint(float &x, float &y) {
  const std::string &k = sel.kind;
  if (k == "animal")
    if (const Animals::Member *m = world->getAnimals().member(sel.id))
      return x = m->x, y = m->y, true;
  if (k == "guest")
    if (const Guests::Guest *g = world->getGuests().guest(sel.id))
      return x = g->x, y = g->y, true;
  if (k == "staff")
    if (const Staff::Member *m = world->getStaff().member(sel.id))
      return x = m->x, y = m->y, true;
  if (k == "object" && sel.id >= 0 && sel.id < static_cast<int>(world->getObjects().objects().size()))
    return x = world->getObjects().objects()[sel.id].x, y = world->getObjects().objects()[sel.id].y, true;
  if (k == "exhibit")
    if (const Fences::Exhibit *e = world->getFences().exhibit(sel.id); e && !e->tiles.empty()) {
      float sx = 0, sy = 0;
      for (const auto &t : e->tiles)
        sx += t.first + 0.5f, sy += t.second + 0.5f;
      return x = sx / e->tiles.size(), y = sy / e->tiles.size(), true;
    }
  if (k == "tile")
    return x = sel.x + 0.5f, y = sel.y + 0.5f, true;
  return false;
}

std::string LabApp::selJson() {
  Fences &fn = world->getFences();
  auto exName = [&](int id) {
    const Fences::Exhibit *e = id >= 0 ? fn.exhibit(id) : nullptr;
    return e ? e->name : std::string();
  };
  JObj o;
  o.str("kind", sel.kind).num("id", sel.id);
  const std::string &k = sel.kind;
  if (k == "animal") {
    Animals &an = world->getAnimals();
    const Animals::Member *m = an.member(sel.id);
    if (!m)
      return sel = {}, "null";
    const Animals::Type &t = an.types()[m->type];
    o.str("name", m->name).str("species", t.key).str("speciesName", t.displayName.empty() ? rm->getString(t.nameId) : t.displayName).str("file", t.file);
    o.flag("female", m->female).flag("baby", m->baby).num("exhibit", m->exhibit).str("exhibitName", exName(m->exhibit));
    o.flag("egg", m->egg).num("hatchIn", m->hatchIn);
    o.num("x", m->x).num("y", m->y).str("doing", m->anim);
    o.num("happiness", m->happiness).num("pending", m->pending).num("hunger", m->hunger).num("food", m->food);
    o.num("health", m->health).num("energy", m->energy).num("dirty", m->dirty);
    o.flag("sick", m->sick).flag("sleeping", m->sleeping).flag("escaped", m->escaped);
    // (not while stopped at a step: rating the exhibit again could change
    // what the stopped code is using)
    o.num("suitability", m->exhibit >= 0 && !inBreak ? an.suitability(m->id, world->getMap(), fn) : -1);
    // Its carer's recommendations, as the panel lists them (red: "!")
    if (!inBreak) {
      static const char *const carers[3] = {"zookeeper", "scientist", "marine specialist"};
      std::string recs;
      for (const auto &[text, red] : an.advice(m->id, world->getMap(), fn))
        recs += (recs.empty() ? "" : ",") + jq((red ? "! " : "") + text);
      o.str("carer", carers[an.carerIndex(*m)]).add("recommendations", "[" + recs + "]");
    }
    o.add("route", routeJson("animal", m->id, m->path, m->pathAt));
    {
      std::string anims, sets;
      for (const std::string &a : an.labAnims(m->id))
        anims += (anims.empty() ? "" : ",") + jq(a);
      for (const std::string &b : an.labSets(m->id))
        sets += (sets.empty() ? "" : ",") + jq(b);
      o.add("anims", "[" + anims + "]").add("sets", "[" + sets + "]");
    }
  } else if (k == "guest") {
    const Guests::Guest *g = world->getGuests().guest(sel.id);
    if (!g)
      return sel = {}, "null";
    static const char *states[] = {"arriving", "walking", "gawking", "leaving", "fleeing", "using a building", "caught"};
    const Guests::Type &t = world->getGuests().types()[g->type];
    std::string thoughts;
    for (size_t i = 0; i < g->thoughts.size() && i < 6; i++)
      thoughts += (i ? "," : "") + jq(g->thoughts[i]);
    o.str("name", g->name).str("type", t.key).str("state", states[std::clamp(static_cast<int>(g->state), 0, 6)]);
    o.str("need", g->need).str("watching", exName(g->target)).num("x", g->x).num("y", g->y).str("doing", g->anim);
    o.num("happiness", g->happiness).num("hunger", g->hunger).num("thirst", g->thirst).num("bathroom", g->bathroom);
    o.num("tired", g->tired).flag("souvenir", g->souvenir).flag("trash", g->trash).flag("leaving", g->leaving);
    o.num("seen", static_cast<double>(g->seen.size())).add("thoughts", "[" + thoughts + "]");
    o.num("guide", g->guide).flag("tourHeard", g->tourHeard);
    o.add("route", routeJson("guest", g->id, g->path, g->pathAt));
    {
      std::string anims;
      for (const std::string &a : world->getGuests().labAnims(g->id))
        anims += (anims.empty() ? "" : ",") + jq(a);
      o.add("anims", "[" + anims + "]");
    }
  } else if (k == "staff") {
    const Staff &st = world->getStaff();
    const Staff::Member *m = st.member(sel.id);
    if (!m)
      return sel = {}, "null";
    static const char *jobs[] = {"nothing", "fixing a fence", "cleaning a filter", "sweeping litter", "visiting an exhibit",
                                 "catching an animal", "giving a tour", "emptying a bin", "raking dung"};
    const Staff::Type &t = st.types()[m->type];
    std::string duty = m->dutyId ? rm->getString(m->dutyId) : std::string();
    if (size_t p = duty.find("%s"); p != std::string::npos)
      duty.replace(p, 2, m->dutyArg);
    std::string exs;
    for (int e : m->exhibits)
      exs += (exs.empty() ? "" : ",") + jq(exName(e));
    const CatalogItem *ci = ItemCatalog::get().find(t.file);
    o.str("name", m->name).str("type", ci ? ci->name : rm->getString(t.nameId)).str("file", t.file).flag("female", m->female);
    o.str("duty", duty).str("job", jobs[std::clamp(static_cast<int>(m->job), 0, 8)]).add("exhibits", "[" + exs + "]");
    o.num("x", m->x).num("y", m->y).str("doing", m->anim);
    if (t.kind == Staff::Kind::Guide)
      o.num("followers", static_cast<double>(m->followers.size())).flag("speaking", m->speaking);
    o.add("route", routeJson("staff", m->id, m->path, m->pathAt));
    {
      std::string anims;
      for (const std::string &a : world->getStaff().labAnims(m->id))
        anims += (anims.empty() ? "" : ",") + jq(a);
      o.add("anims", "[" + anims + "]");
    }
  } else if (k == "object") {
    const auto &list = world->getObjects().objects();
    if (sel.id < 0 || sel.id >= static_cast<int>(list.size()))
      return sel = {}, "null";
    const PlacedObjects::Object &b = list[sel.id];
    std::string file = objectFile(b);
    const CatalogItem *ci = ItemCatalog::get().find(file);
    o.str("name", !b.label.empty() ? b.label : ci ? ci->name : b.typeName).str("file", file).str("class", b.className);
    o.str("type", b.typeName).num("x", b.x).num("y", b.y).num("z", b.z).num("facing", b.facing).flag("bought", b.bought);
    o.num("cost", ci ? ci->cost : 0).str("exhibitName", exName(fn.exhibitAt(static_cast<int>(b.x), static_cast<int>(b.y))));
    if (!b.label.empty())
      o.num("price", b.price).num("income", b.income).num("upkeep", b.upkeep).num("visitors", b.visitorsTotal);
    {
      auto [fx, fy] = world->getIndex().footprintOf(b.subClass, b.typeName, rm);
      std::string from = "none";
      if (IniReader *ai = rm->getIniReader(file)) {
        if (ai->getInt("characteristics/integers", "cfootprintx", -1) >= 0)
          from = file;
        delete ai;
      }
      if (from == "none" && fx > 0)
        from = "its catalogue entry";
      o.add("footprint", JObj()
                             .num("x", fx)
                             .num("y", fy)
                             .str("from", from)
                             .flag("turned", (b.facing & 6) == 2 || (b.facing & 6) == 6)
                             .done());
    }
  } else if (k == "exhibit") {
    const Fences::Exhibit *e = fn.exhibit(sel.id);
    if (!e)
      return sel = {}, "null";
    Animals &an = world->getAnimals();
    std::string animals;
    for (const Animals::Member &m : an.members())
      if (m.exhibit == e->id)
        animals += (animals.empty() ? "" : ",") +
                   JObj()
                       .num("id", m.id)
                       .str("species", an.types()[m.type].displayName.empty() ? rm->getString(an.types()[m.type].nameId) : an.types()[m.type].displayName)
                       .num("happiness", m.happiness)
                       .num("suitability", inBreak ? -1 : an.suitability(m.id, world->getMap(), fn))
                       .done();
    o.str("name", e->name).flag("tank", e->tank).num("tiles", static_cast<double>(e->tiles.size()));
    o.add("animals", "[" + animals + "]").num("fakeAnimals", e->testAnimals);
    o.num("food", world->getItems().foodIn(e->id));
    o.num("dung", static_cast<double>(world->getItems().ofKind(ZooItems::Kind::Dung, e->id).size()));
    if (e->tank)
      o.num("water", e->water).num("purity", e->purity).flag("salt", e->salt);
  } else if (k == "tile") {
    const MapTile *t = world->getMap().getTile(sel.x, sel.y);
    if (!t)
      return sel = {}, "null";
    int objs = 0;
    for (const PlacedObjects::Object &b : world->getObjects().objects())
      objs += static_cast<int>(std::floor(b.x)) == sel.x && static_cast<int>(std::floor(b.y)) == sel.y && !b.fence;
    o.num("x", sel.x).num("y", sel.y).num("terrain", t->terrainType).str("terrainName", terrainName(t->terrainType));
    o.add("corners", "[" + std::to_string(t->cornerHeight[0]) + "," + std::to_string(t->cornerHeight[1]) + "," +
                         std::to_string(t->cornerHeight[2]) + "," + std::to_string(t->cornerHeight[3]) + "]");
    o.str("path", world->pathTypeAt(sel.x, sel.y)).str("exhibitName", exName(fn.exhibitAt(sel.x, sel.y)));
    o.flag("inZoo", fn.insideZoo(sel.x, sel.y)).num("objects", objs);
  } else
    return "null";
  return o.done();
}

// A walker's route: where it's going, what's left of it, and its last
// search (if it was being watched when it searched)
std::string LabApp::routeJson(const char *kind, int id, const std::vector<std::pair<float, float>> &path, size_t at) {
  JObj o;
  bool going = at < path.size();
  o.flag("going", going);
  if (going)
    o.num("goalX", path.back().first).num("goalY", path.back().second).num("legsLeft", static_cast<double>(path.size() - at));
  if (const Pathfinder::Search *s = searchOf(kind, id))
    o.add("search", searchJson(*s));
  return o.done();
}

std::string LabApp::hoverJson() {
  const MapTile *t = hoverOn ? world->getMap().getTile(hoverX, hoverY) : nullptr;
  if (!t)
    return "null";
  Fences &fn = world->getFences();
  int ex = fn.exhibitAt(hoverX, hoverY);
  const Fences::Exhibit *e = ex >= 0 ? fn.exhibit(ex) : nullptr;
  return JObj()
      .num("x", hoverX)
      .num("y", hoverY)
      .num("h", (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) / 4.0)
      .str("terrain", terrainName(t->terrainType))
      .str("path", world->pathTypeAt(hoverX, hoverY))
      .str("exhibit", e && e->named ? e->name : "")
      .flag("inZoo", fn.insideZoo(hoverX, hoverY))
      .done();
}

std::string LabApp::countsJson() {
  int exhibits = 0;
  for (const Fences::Exhibit &e : world->getFences().exhibits())
    exhibits += e.named;
  // (the guests in the zoo - gone ones wait at x -999 - and their average
  // happiness, -100..100, as the game's goals and rating count them)
  int inZoo = 0;
  double happy = 0;
  for (const Guests::Guest &g : world->getGuests().guests())
    if (g.x > -999) {
      inZoo++;
      happy += g.happiness;
    }
  return JObj()
      .num("exhibits", exhibits)
      .num("animals", static_cast<double>(world->getAnimals().members().size()))
      .num("guests", inZoo)
      .num("guestHappiness", inZoo ? std::round(happy / inZoo) : 0)
      .num("rating", world->getZooRating())
      .num("staff", static_cast<double>(world->getStaff().members().size()))
      .num("objects", static_cast<double>(world->getObjects().objects().size()))
      .done();
}

void LabApp::setSel(const std::string &field, const Json &v) {
  if (sel.kind == "animal")
    set(sel.id, field.c_str(), v.num());
  else if (sel.kind == "guest") {
    auto *g = const_cast<Guests::Guest *>(world->getGuests().guest(sel.id));
    float f = static_cast<float>(v.num());
    if (!g)
      return;
    if (field == "happiness") g->happiness = f;
    else if (field == "hunger") g->hunger = f;
    else if (field == "thirst") g->thirst = f;
    else if (field == "bathroom") g->bathroom = f;
    else if (field == "tired") g->tired = f;
  } else if (sel.kind == "exhibit" && field == "name") {
    if (Fences::Exhibit *e = world->getFences().exhibit(sel.id))
      e->name = v.str(e->name);
  } else if (sel.kind == "object" && field == "facing") {
    auto &list = world->objectsMutable().all();
    if (sel.id >= 0 && sel.id < static_cast<int>(list.size()))
      list[sel.id].facing = (static_cast<int>(v.num()) % 8 + 8) % 8;
  }
}

void LabApp::removeSel() {
  if (sel.kind == "animal")
    world->sellAnimal(sel.id);
  else if (sel.kind == "staff")
    world->getStaff().fire(sel.id);
  else if (sel.kind == "object" && sel.id >= 0 && sel.id < static_cast<int>(world->getObjects().objects().size())) {
    world->objectsMutable().remove(sel.id);
    world->reindex();
  } else
    return;
  sel = {};
}

// Everything in the map, for the app's outline
void LabApp::sendOutline() {
  LabTrace::Mute quiet;
  Fences &fn = world->getFences();
  Animals &an = world->getAnimals();
  std::string exhibits, animals, staff, guests, buildings, scenery;
  for (const Fences::Exhibit &e : fn.exhibits())
    if (e.named) {
      int n = 0;
      for (const Animals::Member &m : an.members())
        n += m.exhibit == e.id;
      exhibits += (exhibits.empty() ? "" : ",") +
                  JObj().num("id", e.id).str("name", e.name).flag("tank", e.tank).num("animals", n).done();
    }
  for (const Animals::Member &m : an.members())
    animals += (animals.empty() ? "" : ",") + JObj()
                                                  .num("id", m.id)
                                                  .str("name", m.name)
                                                  .str("species", an.types()[m.type].displayName.empty() ? rm->getString(an.types()[m.type].nameId) : an.types()[m.type].displayName)
                                                  .num("exhibit", m.exhibit)
                                                  .done();
  for (const Staff::Member &m : world->getStaff().members())
    staff += (staff.empty() ? "" : ",") +
             JObj().num("id", m.id).str("name", m.name).str("type", rm->getString(world->getStaff().types()[m.type].nameId)).done();
  int ng = 0;
  for (const Guests::Guest &g : world->getGuests().guests())
    if (++ng <= 400)
      guests += (guests.empty() ? "" : ",") + JObj().num("id", g.id).str("name", g.name).done();
  // Buildings one by one; scenery by its kind (a map has thousands)
  std::map<std::string, std::vector<int>> kinds;
  const auto &list = world->getObjects().objects();
  for (size_t i = 0; i < list.size(); i++) {
    const PlacedObjects::Object &o = list[i];
    if (o.fence)
      continue;
    if (!o.label.empty() || o.className == "building") {
      const CatalogItem *ci = ItemCatalog::get().find(objectFile(o));
      buildings += (buildings.empty() ? "" : ",") +
                   JObj().num("i", static_cast<double>(i)).str("name", !o.label.empty() ? o.label : ci ? ci->name : o.typeName).done();
    } else
      kinds[objectFile(o)].push_back(static_cast<int>(i));
  }
  for (const auto &[file, idx] : kinds) {
    const CatalogItem *ci = ItemCatalog::get().find(file);
    std::string is;
    for (size_t j = 0; j < idx.size() && j < 200; j++)
      is += (j ? "," : "") + std::to_string(idx[j]);
    scenery += (scenery.empty() ? "" : ",") +
               JObj().str("file", file).str("name", ci ? ci->name : fs::path(file).stem().string()).add("i", "[" + is + "]").done();
  }
  host->send("{\"t\":\"outline\",\"exhibits\":[" + exhibits + "],\"animals\":[" + animals + "],\"staff\":[" + staff +
             "],\"guests\":[" + guests + "],\"guestCount\":" + std::to_string(ng) + ",\"buildings\":[" + buildings +
             "],\"scenery\":[" + scenery + "]}");
}

void LabApp::sendMaps() {
  std::string list;
  for (int i = 0; const FreeformMap *m = sm->getFreeformMap(i); i++)
    list += (list.empty() ? "" : ",") +
            JObj().str("stem", fs::path(m->path).stem().string()).str("name", m->name).str("size", m->size).done();
  // (and the original game's own saved zoos: save:<name>)
  std::error_code ec;
  if (const std::string dir = Paths::originalSavesDir(); !dir.empty())
    for (const auto &e : fs::directory_iterator(dir, ec))
      if (e.path().extension() == ".zoo")
        list += (list.empty() ? "" : ",") + JObj()
                                                .str("stem", "save:" + e.path().stem().string())
                                                .str("name", e.path().stem().string() + " (the original's save)")
                                                .str("size", "saved zoo")
                                                .done();
  host->send("{\"t\":\"maps\",\"list\":[" + list + "]}");
}

// Everything that can be put down: the buy panels' items (their categories,
// as the panels' tabs ask for them), and the terrain types
void LabApp::sendCatalog() {
  ItemCatalog &cat = ItemCatalog::get();
  cat.load(rm);
  std::string items;
  for (const CatalogItem &ci : cat.all()) {
    std::string members;
    for (const std::string &m : ci.members)
      members += (members.empty() ? "" : ",") + jq(m);
    items += (items.empty() ? "" : ",") + JObj()
                                              .str("file", ci.file)
                                              .str("name", ci.name)
                                              .num("cost", ci.cost)
                                              .num("nameId", ci.nameId)
                                              .str("icon", ci.icon)
                                              .num("facings", static_cast<double>(ci.icons.size()))
                                              .str("section", ci.registrySection)
                                              .str("type", ci.type)
                                              .flag("fromMod", ci.fromMod)
                                              .num("pack", ci.expansion)
                                              .add("members", "[" + members + "]")
                                              .done();
  }
  terrainName(0);
  std::string ts;
  for (const auto &[t, info] : terrains)
    if (info.icon.find("bogus") == std::string::npos)
      ts += (ts.empty() ? "" : ",") +
            JObj().num("type", t).str("name", info.name).str("icon", info.icon).num("cost", info.cost).flag("water", info.water).done();
  std::string filter = world->getFences().filterType().file;
  host->send("{\"t\":\"catalog\",\"items\":[" + items + "],\"terrains\":[" + ts + "],\"filter\":" + jq(filter) + "}");
}

// An item's icon (the buy panels' art) as a PNG
void LabApp::sendIcon(const std::string &path) {
  std::string png;
  Animation *a = !path.empty() && rm->hasResource(path + ".ani") ? rm->getAnimation(path) : nullptr;
  int w = 0, h = 0;
  if (a)
    a->queryTexture(CompassDirection::N, &w, &h);
  if (a && w > 0 && h > 0 && w <= 512 && h <= 512)
    if (SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h)) {
      SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
      SDL_Texture *old = SDL_GetRenderTarget(ren);
      SDL_SetRenderTarget(ren, tex);
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
      SDL_RenderClear(ren);
      SDL_Rect r{0, 0, w, h};
      a->draw(ren, &r, CompassDirection::N);
      if (SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)) {
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
        png = base64(encodeImage(sf, true));
        SDL_FreeSurface(sf);
      }
      SDL_SetRenderTarget(ren, old);
      SDL_DestroyTexture(tex);
    }
  host->send("{\"t\":\"icon\",\"path\":" + jq(path) + ",\"png\":" + jq(png) + "}");
}

// A game file for the Lab's viewers, by its kind: an animation (one
// direction's frames as PNGs, which directions it has, its frame time), a
// picture, a palette's colours, a sound's bytes, a text's text, or the start
// of anything else as bytes
void LabApp::sendFileView(const std::string &name, const std::string &dirWanted) {
  std::string ext = name.substr(name.find_last_of('.') + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  JObj o;
  o.str("name", name).str("from", rm->archiveOf(name));
  int size = 0;
  if (ext == "ani") {
    // (animals/orca/m/jumpspin/jumpspin.ani: the engine names it by its
    // folder, animals/orca/m/jumpspin)
    std::string p = name.substr(0, name.size() - 4);
    size_t slash = p.find_last_of('/');
    if (slash != std::string::npos) {
      std::string last = p.substr(slash + 1), folder = p.substr(0, slash);
      size_t s2 = folder.find_last_of('/');
      if (folder.substr(s2 == std::string::npos ? 0 : s2 + 1) == last)
        p = folder;
    }
    std::unique_ptr<Animation> a(rm->getAnimation(p));
    static const std::pair<CompassDirection, const char *> dirs[8] = {
        {CompassDirection::N, "N"},   {CompassDirection::NE, "NE"}, {CompassDirection::E, "E"},
        {CompassDirection::SE, "SE"}, {CompassDirection::S, "S"},   {CompassDirection::SW, "SW"},
        {CompassDirection::W, "W"},   {CompassDirection::NW, "NW"}};
    std::string has, frames;
    int pick = -1;
    for (int i = 0; a && i < 8; i++)
      if (a->hasFrames(dirs[i].first)) {
        has += (has.empty() ? "" : ",") + jq(dirs[i].second);
        if (pick < 0 || dirWanted == dirs[i].second)
          pick = i;
      }
    float l = 0, t = 0, r = 0, b = 0;
    int W = 0, H = 0, n = a ? std::max(1, a->frameCount()) : 0;
    if (pick >= 0 && a->anchoredBounds(0, 0, dirs[pick].first, l, t, r, b)) {
      W = std::clamp(static_cast<int>(std::ceil(r - l)), 1, 2048);
      H = std::clamp(static_cast<int>(std::ceil(b - t)), 1, 2048);
      if (SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H)) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_Texture *old = SDL_GetRenderTarget(ren);
        SDL_SetRenderTarget(ren, tex);
        for (int f = 0; f < std::min(n, 200); f++) {
          SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
          SDL_RenderClear(ren);
          a->drawAnchored(ren, -l, -t, dirs[pick].first, nullptr, f);
          if (SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888)) {
            SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
            frames += (frames.empty() ? "" : ",") + jq(base64(encodeImage(sf, true)));
            SDL_FreeSurface(sf);
          }
        }
        SDL_SetRenderTarget(ren, old);
        SDL_DestroyTexture(tex);
      }
    }
    o.str("kind", a ? "anim" : "missing")
        .num("frameMs", a ? a->frameTimeMs() : 0)
        .num("w", W)
        .num("h", H)
        .num("frames", n)
        .num("anchorX", -l)
        .num("anchorY", -t)
        .str("dir", pick >= 0 ? dirs[pick].second : "")
        .add("dirs", "[" + has + "]")
        .add("png", "[" + frames + "]");
  } else if (ext == "tga" || ext == "bmp" || ext == "png" || ext == "jpg" || ext == "pcx" || ext == "gif") {
    void *data = rm->getFileBytes(name, &size);
    // (told its type by its extension: a TGA has no signature to guess from)
    std::string type = ext;
    std::transform(type.begin(), type.end(), type.begin(), ::toupper);
    SDL_Surface *s = data ? IMG_LoadTyped_RW(SDL_RWFromConstMem(data, size), 1, type.c_str()) : nullptr;
    free(data);
    if (s) {
      o.str("kind", "image").num("w", s->w).num("h", s->h).num("size", size).str("png", base64(encodeImage(s, true)));
      SDL_FreeSurface(s);
    } else {
      o.str("kind", "binary").num("size", size).str("error", IMG_GetError());
    }
  } else if (ext == "pal") {
    std::string palName = name; // (getPallet takes a string it may change)
    Pallet *p = rm->getPalletManager()->getPallet(palName);
    std::string cols;
    char hex[8];
    for (uint32_t i = 0; p && i < std::min<uint32_t>(p->color_count, 256); i++) {
      uint32_t c = p->colors[i]; // (as the art's pixels: 0xAABBGGRR)
      snprintf(hex, sizeof hex, "#%02x%02x%02x", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
      cols += (cols.empty() ? "" : ",") + jq(hex);
    }
    o.str("kind", p ? "palette" : "missing").add("colors", "[" + cols + "]");
  } else {
    void *data = rm->getFileBytes(name, &size);
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    if (!data || size < 0) {
      o.str("kind", "missing");
    } else if (ext == "wav" || ext == "mp3" || ext == "ogg") {
      o.str("kind", "sound").num("size", size).str("type", ext == "wav" ? "audio/wav" : ext == "mp3" ? "audio/mpeg" : "audio/ogg");
      o.str("data", size <= 32 * 1024 * 1024 ? base64(std::vector<unsigned char>(bytes, bytes + size)) : "");
    } else {
      int odd = 0;
      for (int i = 0; i < std::min(size, 4096); i++)
        odd += bytes[i] == 0 || bytes[i] < 9 || (bytes[i] > 13 && bytes[i] < 32);
      if (odd * 50 <= std::min(size, 4096)) {
        o.str("kind", "text").num("size", size).str("text", std::string(static_cast<const char *>(data), static_cast<size_t>(std::min(size, 2000000))));
      } else {
        std::string hx;
        char b3[4];
        for (int i = 0; i < std::min(size, 2048); i++) {
          snprintf(b3, sizeof b3, "%02x", bytes[i]);
          hx += b3;
        }
        o.str("kind", "binary").num("size", size).str("hex", hx);
      }
    }
    free(data);
  }
  host->send("{\"t\":\"fileView\"," + o.s + "}");
}

// Every game file (the archives' and the loose ones, as the engine reads
// them: an expansion's or update's copy over the base game's), with where
// each comes from - the Lab's smart filter searches it
void LabApp::sendFilesIndex() {
  // (straight from the engine's map of them: archiveOf, a name at a time,
  // asks the disk for loose copies and took minutes for them all)
  std::vector<std::pair<std::string, std::string>> names(rm->allResources().begin(), rm->allResources().end());
  std::sort(names.begin(), names.end());
  std::string list;
  std::map<std::string, std::string> shortFrom;
  for (const auto &[n, from] : names) {
    if (n.empty() || n.back() == '/')
      continue;
    auto it = shortFrom.find(from);
    if (it == shortFrom.end()) {
      size_t cut = from.find_last_of("/\\");
      std::string dir = cut == std::string::npos ? "" : from.substr(0, cut);
      size_t cut2 = dir.find_last_of("/\\");
      it = shortFrom.emplace(from, (cut2 == std::string::npos ? "" : dir.substr(cut2 + 1) + "/") +
                                       (cut == std::string::npos ? from : from.substr(cut + 1)))
               .first;
    }
    list += (list.empty() ? "" : ",") + std::string("[") + jq(n) + "," + jq(it->second) + "]";
  }
  host->send("{\"t\":\"filesIndex\",\"list\":[" + list + "]}");
}

// The game's text files (.ai, .cfg, .lyt...) with a line holding the text -
// spaces and case don't count, so "cPreattack=1" finds "cPreattack = 1" -
// and those lines (5 a file at most): the Lab's has: filter
void LabApp::sendFilesGrep(const std::string &text) {
  auto squash = [](const std::string &s) {
    std::string o;
    for (char c : s)
      if (c != ' ' && c != '\t' && c != '\r')
        o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
  };
  static const std::set<std::string> kText = {"ai", "cfg", "ini", "lyt", "uca", "ucb", "ucs", "txt"};
  std::string want = squash(text), hits;
  int files = 0;
  std::vector<std::pair<std::string, std::string>> names(rm->allResources().begin(), rm->allResources().end());
  std::sort(names.begin(), names.end());
  for (const auto &[n, from] : names) {
    if (want.empty() || files >= 400)
      break;
    size_t dot = n.rfind('.');
    if (dot == std::string::npos || !kText.count(squash(n.substr(dot + 1))))
      continue;
    int size = 0;
    void *data = rm->getFileBytes(n, &size);
    if (!data)
      continue;
    std::string s(static_cast<const char *>(data), static_cast<size_t>(std::max(size, 0)));
    free(data);
    std::string lines;
    int count = 0;
    for (size_t at = 0; at < s.size() && count < 5;) {
      size_t nl = s.find('\n', at);
      std::string line = s.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (squash(line).find(want) != std::string::npos) {
        lines += (lines.empty() ? "" : ",") + jq(line);
        count++;
      }
      if (nl == std::string::npos)
        break;
      at = nl + 1;
    }
    if (count) {
      hits += (hits.empty() ? "" : ",") + std::string("[") + jq(n) + ",[" + lines + "]]";
      files++;
    }
  }
  host->send("{\"t\":\"filesGrep\",\"q\":" + jq(text) + ",\"hits\":[" + hits + "]}");
}

// Every key the game's .ai files set (cPreattack, cPrey...) and every
// behaviour set they name (bPreattack...), with how many files have each and
// one that does: the Engine code editor's suggestions inside num("...") and
// set(m, "...") - the names the game really reads, not remembered ones
void LabApp::sendAiKeys() {
  std::map<std::string, std::pair<int, std::string>> keys, sets;
  std::vector<std::pair<std::string, std::string>> names(rm->allResources().begin(), rm->allResources().end());
  for (const auto &[n, from] : names) {
    if (n.size() < 4 || n.compare(n.size() - 3, 3, ".ai") != 0)
      continue;
    int size = 0;
    void *data = rm->getFileBytes(n, &size);
    if (!data)
      continue;
    std::string s(static_cast<const char *>(data), static_cast<size_t>(std::max(size, 0)));
    free(data);
    std::set<std::string> seenK, seenS;
    for (size_t at = 0; at < s.size();) {
      size_t nl = s.find('\n', at);
      std::string line = s.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      at = nl == std::string::npos ? s.size() : nl + 1;
      size_t a = line.find_first_not_of(" \t");
      if (a == std::string::npos)
        continue;
      // (a key: "cName = ...")
      if (line[a] == 'c' && a + 1 < line.size() && std::isupper(static_cast<unsigned char>(line[a + 1]))) {
        size_t b = a;
        while (b < line.size() && std::isalnum(static_cast<unsigned char>(line[b])))
          b++;
        if (line.find('=', b) != std::string::npos)
          seenK.insert(line.substr(a, b - a));
      }
      // (a set: "[m\BehaviorSet\bName]")
      size_t bs = line.find("BehaviorSet\\");
      if (line[a] == '[' && bs != std::string::npos) {
        size_t b = bs + 12, e = b;
        while (e < line.size() && std::isalnum(static_cast<unsigned char>(line[e])))
          e++;
        if (e > b)
          seenS.insert(line.substr(b, e - b));
      }
    }
    for (const std::string &k : seenK) {
      auto &v = keys[k];
      if (!v.first++)
        v.second = n;
    }
    for (const std::string &k : seenS) {
      auto &v = sets[k];
      if (!v.first++)
        v.second = n;
    }
  }
  auto list = [](const std::map<std::string, std::pair<int, std::string>> &m) {
    std::string o;
    for (const auto &[k, v] : m)
      o += (o.empty() ? "" : ",") + std::string("[") + jq(k) + "," + std::to_string(v.first) + "," + jq(v.second) + "]";
    return o;
  };
  host->send("{\"t\":\"aiKeys\",\"keys\":[" + list(keys) + "],\"sets\":[" + list(sets) + "]}");
}

bool LabApp::loadMap(const std::string &stem, bool flat) {
  unloadScript();
  tool = Tool{};
  applyTool();
  sel = {};
  hoverOn = false;
  asking.clear();
  labAnimals.clear();
  labEdges.clear();
  labObjects.clear();
  labW = labH = 0;
  // (a new zoo: its animals aren't the ones followed, and their ids - 1, 2...
  // again - aren't the last zoo's routes)
  watching.clear();
  searches.clear();
  watchRows.clear();
  watchLast.clear();
  // (and the Watch panel told: it kept the last zoo's rows)
  if (host)
    sendWatch();
  if (!setupScene(stem))
    return false;
  if (flat)
    flattenZoo();
  mapFlat = flat;
  loads++;
  world->setPaused(!playing);
  note("Loaded " + mapName + (flat ? " (made flat)" : ""));
  return true;
}

// The goals' hooks: their rules measured from the lab zoo as the game
// measures them (main.cpp's setupGoals, zoo.exe 0x41d665) - what it has: its
// animals, guests, exhibits and clock (no books: goals on cash or the zoo
// rating are left out when the map's loaded)
void LabApp::setupGoals() {
  Goals::Hooks h;
  h.measure = [this](const Goals::Goal &g) -> int {
    // (months begun: a month a 30 days of 12 s)
    int months = 1 + static_cast<int>(simClock / (12.0 * 30.0));
    Animals &an = world->getAnimals();
    if (g.rulea == 1) {
      if (months < g.arga)
        return 0;
      switch (g.ruleb) {
      case 0: { // animals' average happiness, 0-100 (the game's: (h + 100) / 2)
        double sum = 0;
        int n = 0;
        for (const Animals::Member &m : an.members()) {
          sum += m.happiness;
          n++;
        }
        int raw = n ? static_cast<int>(sum / n) : 0;
        return (raw + 100) * 100 / 200;
      }
      case 1: { // guests' average happiness, 0-100
        double sum = 0;
        int n = 0;
        for (const Guests::Guest &gu : world->getGuests().guests())
          if (gu.x > -999) {
            sum += gu.happiness;
            n++;
          }
        int raw = n ? static_cast<int>(sum / n) : 0;
        return (raw + 100) * 100 / 200;
      }
      case 3: {
        int n = 0;
        for (const Guests::Guest &gu : world->getGuests().guests())
          n += gu.x > -999 ? 1 : 0;
        return n;
      }
      case 4: return static_cast<int>(an.members().size());
      case 5: {
        int n = 0;
        for (const Animals::Member &m : an.members())
          n += m.sick ? 1 : 0;
        return n;
      }
      case 6: {
        std::set<int> kinds;
        for (const Animals::Member &m : an.members())
          if (m.exhibit >= 0)
            kinds.insert(m.type);
        return static_cast<int>(kinds.size());
      }
      case 11: return static_cast<int>(goals.awards.size());
      default: return 0;
      }
    }
    if (g.rulea == 2 && g.ruleb == 2) { // animals of a species (arga) and kind (argb)
      int n = 0;
      for (const Animals::Member &m : an.members()) {
        if (an.types()[m.type].nameId != g.arga)
          continue;
        int kind = m.baby ? 3 : m.female ? 1 : 0;
        n += kind == g.argb ? 1 : 0;
      }
      return n;
    }
    if (g.rulea == 6 && g.ruleb == 0) {
      int count = 0;
      for (const Fences::Exhibit &ex : world->getFences().exhibits())
        if (ex.animals > 0)
          count += an.exhibitSuitability(ex.id) >= g.value ? 1 : 0;
      return count >= std::max(1, g.arga) ? g.value : 0;
    }
    if (g.rulea == 7 && g.ruleb == 2)
      return months;
    return 0;
  };
  h.popup = [this](const std::string &, const std::string &text, bool pause) {
    note("A goal's popup: " + text);
    // (pause=1: the game waits on it - so does the lab zoo)
    if (pause) {
      goalPaused = true;
      if (playing) {
        playing = false;
        world->setPaused(true);
      }
      note("The zoo's paused on it, as the game is (run it again to go on)");
    }
  };
  h.disable = [this](int element, bool off) {
    bool had = goalsDisabled.count(element) != 0;
    if (had == off)
      return false;
    if (off)
      goalsDisabled.insert(element);
    else
      goalsDisabled.erase(element);
    note(std::string("A goal ") + (off ? "disabled" : "enabled") + " UI element " + std::to_string(element) +
         (element == 2075 || element == 2080 ? " (adopting animals)" : ""));
    return true;
  };
  h.hide = [](int, bool) {};
  h.cash = [this](int amount, int) { note("A goal's cash: $" + std::to_string(amount) + " (the lab zoo keeps no books)"); };
  h.message = [this](int id) { note("A goal's message: " + rm->getString(static_cast<uint32_t>(id))); };
  h.award = [this](int a) { note("A goal's award: " + std::to_string(a)); };
  goals.setHooks(h);
}

// The engine's own simulation, for so many seconds of game time
void LabApp::stepWorld(float seconds) {
  static Uint8 none[SDL_NUM_SCANCODES] = {};
  world->setPaused(false);
  skipping = true;
  while (seconds > 1e-4f) {
    float s = std::min(0.1f, seconds);
    world->update(none, s);
    simClock += s;
    goals.update(s);
    seconds -= s;
    applyPins();
    watchTick();
    // (a goal's popup paused it: the skip stops there, as the game would)
    if (goalPaused)
      break;
  }
  goalPaused = false;
  world->setPaused(!playing);
  // (the skip's latest steps, in order; how many more there were)
  skipping = false;
  for (const std::string &m : skipSteps)
    host->send(m);
  skipSteps.clear();
  if (stepsDropped) {
    host->send("{\"t\":\"stepsDropped\",\"n\":" + std::to_string(stepsDropped) + "}");
    stepsDropped = 0;
  }
  if (watchDirty)
    sendWatch();
  // (the app told the skip's done: the clock where it ended)
  host->send("{\"t\":\"skipDone\",\"clock\":" + std::to_string(simClock) + "}");
}

// The Watch log's next rows: each watched animal that's doing something
// new, or has gone a tile since its last row (or is gone)
void LabApp::watchTick() {
  for (auto it = watching.begin(); it != watching.end();) {
    const Animals::Member *m = world->getAnimals().member(*it);
    auto last = watchLast.find(*it);
    if (!m) {
      std::string name = last != watchLast.end() ? last->second.name : "#" + std::to_string(*it);
      watchRows.push_back({simClock, *it, name, "(gone)", 0, 0});
      watchLast.erase(*it);
      it = watching.erase(it);
      watchDirty = true;
      continue;
    }
    WatchRow now{simClock, m->id, m->name, m->anim, m->x, m->y};
    if (last == watchLast.end() || last->second.doing != now.doing ||
        std::hypot(last->second.x - now.x, last->second.y - now.y) >= 1.0f) {
      watchRows.push_back(now);
      watchLast[m->id] = now;
      watchDirty = true;
    }
    ++it;
  }
  if (watchRows.size() > 3000)
    watchRows.erase(watchRows.begin(), watchRows.begin() + 1000);
}

void LabApp::sendWatch() {
  watchDirty = false;
  watchSentAt = SDL_GetTicks();
  std::string rows, who;
  size_t from = watchRows.size() > 500 ? watchRows.size() - 500 : 0;
  for (size_t i = from; i < watchRows.size(); i++) {
    const WatchRow &r = watchRows[i];
    rows += (rows.empty() ? "" : ",") +
            JObj().num("t", r.t).num("id", r.id).str("name", r.name).str("doing", r.doing).num("x", r.x).num("y", r.y).done();
  }
  for (int id : watching)
    if (const Animals::Member *m = world->getAnimals().member(id))
      who += (who.empty() ? "" : ",") + JObj().num("id", id).str("name", m->name).done();
  host->send("{\"t\":\"watch\",\"rows\":[" + rows + "],\"watching\":[" + who + "]}");
}

// Over the view: the tile grid, the exhibits' outlines, the tile the
// cursor's over and what's picked
void LabApp::drawOverlays() {
  if (viewMode != "scene")
    return;
  Fences &fn = world->getFences();
  auto vtx = [&](int x, int y, SDL_Point &p) { return world->vertexToWindow(x, y, p.x, p.y); };
  auto edge = [&](int x0, int y0, int x1, int y1, int thick) {
    SDL_Point a, b;
    if (!vtx(x0, y0, a) || !vtx(x1, y1, b))
      return;
    for (int d = 0; d < thick; d++)
      SDL_RenderDrawLine(ren, a.x, a.y - d, b.x, b.y - d);
  };
  auto tile = [&](int x, int y, int thick) {
    edge(x, y, x + 1, y, thick);
    edge(x + 1, y, x + 1, y + 1, thick);
    edge(x + 1, y + 1, x, y + 1, thick);
    edge(x, y + 1, x, y, thick);
  };
  // an exhibit's outline: its tiles' sides that aren't shared
  auto outline = [&](const Fences::Exhibit &e, int thick) {
    for (const auto &[x, y] : e.tiles) {
      if (!e.tiles.count({x, y - 1}))
        edge(x, y, x + 1, y, thick);
      if (!e.tiles.count({x, y + 1}))
        edge(x, y + 1, x + 1, y + 1, thick);
      if (!e.tiles.count({x - 1, y}))
        edge(x, y, x, y + 1, thick);
      if (!e.tiles.count({x + 1, y}))
        edge(x + 1, y, x + 1, y + 1, thick);
    }
  };
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  if (showGrid) {
    float cx, cy;
    if (!world->viewCentre(cx, cy))
      cx = world->getMap().getWidth() / 2.0f, cy = world->getMap().getHeight() / 2.0f;
    int r = static_cast<int>(28 / std::max(0.25f, g_ZoomLevel));
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 46);
    for (int y = std::max(0, static_cast<int>(cy) - r); y < std::min(world->getMap().getHeight(), static_cast<int>(cy) + r); y++)
      for (int x = std::max(0, static_cast<int>(cx) - r); x < std::min(world->getMap().getWidth(), static_cast<int>(cx) + r); x++) {
        edge(x, y, x + 1, y, 1);
        edge(x, y, x, y + 1, 1);
      }
  }
  if (showExhibits)
    for (const Fences::Exhibit &e : fn.exhibits())
      if (e.named && !(sel.kind == "exhibit" && sel.id == e.id)) {
        SDL_SetRenderDrawColor(ren, e.tank ? 106 : 92, e.tank ? 168 : 196, e.tank ? 239 : 137, 150);
        outline(e, 2);
      }
  SDL_SetRenderDrawColor(ren, 232, 180, 90, 255);
  if (sel.kind == "exhibit") {
    if (const Fences::Exhibit *e = fn.exhibit(sel.id))
      outline(*e, 3);
  } else {
    float x, y;
    if (selPoint(x, y))
      tile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), 2);
  }
  // Routes: everyone's (the Routes overlay); the search of the one picked
  // (or stopped on), or the Route tool's
  if (showFootprints)
    drawFootprints();
  if (showRoutes)
    drawRoutes();
  {
    const Pathfinder::Search *s = nullptr;
    if (tool.kind == "route" && hasTest)
      s = &testSearch;
    else if (inBreak && stepWho.kind && (showRoutes || stepAt.rfind("src/Pathfinder.cpp", 0) == 0))
      s = searchOf(stepWho.kind, stepWho.id);
    else if (showRoutes && (sel.kind == "animal" || sel.kind == "guest" || sel.kind == "staff"))
      s = searchOf(sel.kind, sel.id);
    if (s)
      drawSearch(*s);
    if (tool.kind == "route" && routeStartSet && !hasTest) {
      SDL_SetRenderDrawColor(ren, 90, 150, 255, 255);
      tile(routeSX, routeSY, 3);
    }
  }
  // Stopped at a step: whose it is, in cyan
  if (inBreak && stepWho.kind) {
    SDL_SetRenderDrawColor(ren, 80, 220, 230, 255);
    std::string k = stepWho.kind;
    float x = -1, y = -1;
    if (k == "exhibit") {
      if (const Fences::Exhibit *e = fn.exhibit(stepWho.id))
        outline(*e, 3);
    } else if (k == "animal") {
      if (const Animals::Member *m = world->getAnimals().member(stepWho.id))
        x = m->x, y = m->y;
    } else if (k == "guest") {
      if (const Guests::Guest *g = world->getGuests().guest(stepWho.id))
        x = g->x, y = g->y;
    } else if (k == "staff") {
      if (const Staff::Member *m = world->getStaff().member(stepWho.id))
        x = m->x, y = m->y;
    }
    if (x >= 0) {
      int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
      tile(tx, ty, 3);
      edge(tx - 1, ty - 1, tx + 2, ty - 1, 1);
      edge(tx + 2, ty - 1, tx + 2, ty + 2, 1);
      edge(tx + 2, ty + 2, tx - 1, ty + 2, 1);
      edge(tx - 1, ty + 2, tx - 1, ty - 1, 1);
    }
  }
  if (hoverOn && tool.kind == "select") {
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 170);
    tile(hoverX, hoverY, 1);
  }
}

// ------------------------------------------------------------- testing
// Pinned values put back after each update: held where they're set
void LabApp::applyPins() {
  if (pins.empty())
    return;
  LabTrace::Mute quiet;
  for (auto it = pins.begin(); it != pins.end();) {
    bool alive = it->kind == "animal" ? world->getAnimals().member(it->id) != nullptr
                 : it->kind == "guest" ? world->getGuests().guest(it->id) != nullptr
                                       : false;
    if (!alive) {
      it = pins.erase(it);
      continue;
    }
    if (it->kind == "animal")
      set(it->id, it->field.c_str(), it->value);
    else
      world->getGuests().labSet(it->id, it->field, static_cast<float>(it->value));
    ++it;
  }
}

// An action on what's picked (the inspector's buttons)
void LabApp::act(const std::string &action, const std::string &arg) {
  ZT_WHO("tool", 0);
  Animals &an = world->getAnimals();
  const std::string &k = sel.kind;
  if (k == "animal") {
    Animals::Member *m = an.member(sel.id);
    if (!m)
      return;
    const Animals::Type &t = an.types()[m->type];
    if (action == "anim")
      an.labPlay(sel.id, arg, 0);
    else if (action == "behave")
      an.labBehave(sel.id, arg);
    else if (action == "hungry")
      m->hunger = 100;
    else if (action == "tired")
      m->energy = static_cast<float>(t.energyThreshold);
    else if (action == "sick")
      m->sick = true, m->health = 0;
    else if (action == "heal")
      an.heal(sel.id);
    else if (action == "dart")
      an.tranquilise(sel.id);
    else if (action == "crate")
      an.box(sel.id);
    else if (action == "dung")
      world->getItems().add(ZooItems::Kind::Dung, m->exhibit, m->x, m->y);
    else if (action == "unhappy")
      m->happiness = -100;
    else if (action == "happy")
      m->happiness = 100;
    ZT_STEP("", "The Lab: an action on an animal", {"action", action}, {"arg", arg}, {"animal", m->name});
  } else if (k == "guest") {
    Guests &gs = world->getGuests();
    if (action == "anim")
      gs.labPlay(sel.id, arg, 0);
    else if (action == "leave")
      gs.labLeave(sel.id);
    else if (action == "hungry" || action == "thirst" || action == "bathroom" || action == "tired")
      gs.labSet(sel.id, action == "hungry" ? "hunger" : action, 100);
    ZT_STEP("", "The Lab: an action on a guest", {"action", action}, {"arg", arg});
  } else if (k == "staff") {
    if (action == "anim")
      world->getStaff().labPlay(sel.id, arg, 2);
    ZT_STEP("", "The Lab: an action on staff", {"action", action}, {"arg", arg});
  } else if (k == "exhibit") {
    // (the game's dev console, with this exhibit's number in its list)
    int n = 0, at = 0;
    for (const Fences::Exhibit &e : world->getFences().exhibits())
      if (e.named) {
        n++;
        if (e.id == sel.id)
          at = n;
      }
    if (!at || !console)
      return;
    std::vector<std::string> words{action, std::to_string(at)};
    if (!arg.empty())
      words.push_back(arg);
    std::string out = console(words);
    note(out.empty() ? action + ": done" : out);
  }
}

// --------------------------------------------------------------- routes
const Pathfinder::Search *LabApp::searchOf(const std::string &kind, int id) const {
  auto it = searches.find({kind, id});
  return it == searches.end() ? nullptr : &it->second;
}

std::string LabApp::searchJson(const Pathfinder::Search &s) {
  return JObj()
      .flag("found", s.found)
      .flag("gaveUp", s.gaveUp)
      .str("why", s.why)
      .num("fromX", s.sx)
      .num("fromY", s.sy)
      .num("toX", s.gx)
      .num("toY", s.gy)
      .num("looked", static_cast<double>(s.closed.size()))
      .num("limit", s.maxNodes)
      .num("open", static_cast<double>(s.open.size()))
      .num("blocked", static_cast<double>(s.blocked.size()))
      .num("walls", static_cast<double>(s.walls.size()))
      .num("routeTiles", static_cast<double>(s.route.size()))
      .num("ms", s.ms)
      .done();
}

// The Route tool: a click is where from, each click after where to (an
// animal: from where it stands); the search is the walker kind's own
void LabApp::routeClick(int tx, int ty) {
  std::string k = tool.item.empty() ? "guest" : tool.item;
  if (k == "animal") {
    if (sel.kind != "animal" || !world->getAnimals().member(sel.id)) {
      note("Pick an animal first: its route goes from where it stands.");
      return;
    }
  } else if (!routeStartSet) {
    routeStartSet = true;
    routeSX = tx;
    routeSY = ty;
    hasTest = false;
    note("From " + std::to_string(tx) + ", " + std::to_string(ty) + ": now click where to.");
    return;
  }
  std::vector<std::pair<int, int>> out;
  routeTesting = true;
  bool wasOn = LabTrace::on;
  LabTrace::on = true;
  bool ok = k == "animal" ? world->getAnimals().testRoute(sel.id, tx, ty, world->getMap(), world->getFences(), out)
            : k == "staff" ? world->getStaff().testRoute(routeSX, routeSY, tx, ty, world->getMap(), world->getFences(), out)
                           : world->getGuests().testRoute(routeSX, routeSY, tx, ty, out);
  LabTrace::on = wasOn;
  routeTesting = false;
  updateTraceOn();
  note(ok ? "A " + k + " can get there: " + std::to_string(out.size()) + " tiles" : "No route for a " + k + ": " + testSearch.why);
}

// A search drawn over the ground: what it looked at (fainter first, brighter
// later), what was left open (yellow), tiles it couldn't go onto (red),
// steps refused (red edges), the route (green), start (blue) and goal
void LabApp::drawSearch(const Pathfinder::Search &s) {
  if (s.width <= 0)
    return;
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  auto corner = [&](int x, int y, SDL_FPoint &p) {
    int wx, wy;
    if (!world->vertexToWindow(x, y, wx, wy))
      return false;
    p = {static_cast<float>(wx), static_cast<float>(wy)};
    return true;
  };
  auto quad = [&](int x, int y, SDL_Color c) {
    SDL_FPoint p[4];
    if (!corner(x, y, p[0]) || !corner(x + 1, y, p[1]) || !corner(x + 1, y + 1, p[2]) || !corner(x, y + 1, p[3]))
      return;
    if (p[1].x < -64 || p[3].x > hostW + 64 || p[2].y < -64 || p[0].y > hostH + 64)
      return; // (off the view)
    int b = static_cast<int>(v.size());
    for (const SDL_FPoint &q : p)
      v.push_back({q, c, {0, 0}});
    idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
  };
  const int w = s.width;
  const int n = static_cast<int>(s.closed.size());
  for (int i = 0; i < n; i++)
    quad(s.closed[i] % w, s.closed[i] / w, {70, 190, 230, static_cast<Uint8>(35 + 110 * i / std::max(1, n - 1))});
  for (int t : s.open)
    quad(t % w, t / w, {240, 200, 60, 110});
  for (int t : s.blocked)
    quad(t % w, t / w, {235, 70, 60, 150});
  for (auto [x, y] : s.route)
    quad(x, y, {90, 220, 120, 175});
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  if (!idx.empty())
    SDL_RenderGeometry(ren, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
  auto line = [&](int x0, int y0, int x1, int y1, int thick) {
    SDL_FPoint a, b;
    if (!corner(x0, y0, a) || !corner(x1, y1, b))
      return;
    for (int d = 0; d < thick; d++)
      SDL_RenderDrawLineF(ren, a.x, a.y - d, b.x, b.y - d);
  };
  auto outline = [&](int x, int y, int thick) {
    line(x, y, x + 1, y, thick);
    line(x + 1, y, x + 1, y + 1, thick);
    line(x + 1, y + 1, x, y + 1, thick);
    line(x, y + 1, x, y, thick);
  };
  // the steps it couldn't take: the edge between the two tiles
  SDL_SetRenderDrawColor(ren, 255, 60, 50, 255);
  for (auto [a, b] : s.walls) {
    int ax = a % w, ay = a / w, bx = b % w, by = b / w;
    if (ax != bx)
      line(std::max(ax, bx), ay, std::max(ax, bx), ay + 1, 3);
    else
      line(ax, std::max(ay, by), ax + 1, std::max(ay, by), 3);
  }
  SDL_SetRenderDrawColor(ren, 90, 150, 255, 255);
  outline(s.sx, s.sy, 3);
  if (s.found)
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
  else
    SDL_SetRenderDrawColor(ren, 255, 60, 50, 255);
  outline(s.gx, s.gy, 3);
}

// Every object's footprint, as blocking uses it (WorldIndex: zoo.exe keeps
// four object slots a tile, and a quarter is taken when its middle is under
// the footprint): the quarters filled, the footprint outlined. One with no
// footprint found (no cFootprintX/Y) is a red cross: it blocks nothing
void LabApp::drawFootprints() {
  WorldIndex &ix = world->getIndex();
  auto at = [&](float x, float y, SDL_FPoint &p) {
    const MapTile *t = world->getMap().getTile(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    float h = t ? (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) / 4.0f : 0.0f;
    int wx, wy;
    world->pointToWindow(x, y, h, wx, wy);
    p = {static_cast<float>(wx), static_cast<float>(wy)};
  };
  std::vector<SDL_Vertex> v;
  std::vector<int> idx;
  std::vector<std::array<SDL_FPoint, 5>> boxes;
  std::vector<SDL_FPoint> missing;
  for (const PlacedObjects::Object &o : world->getObjects().objects()) {
    if (o.fence)
      continue;
    SDL_FPoint c;
    at(o.x, o.y, c);
    if (c.x < -200 || c.y < -200 || c.x > hostW + 200 || c.y > hostH + 200)
      continue; // (off the view)
    auto [fx, fy] = ix.footprintOf(o.subClass, o.typeName, rm);
    if (fx <= 0 || fy <= 0) {
      missing.push_back(c);
      continue;
    }
    if ((o.facing & 6) == 2 || (o.facing & 6) == 6)
      std::swap(fx, fy);
    float hx = fx / 4.0f, hy = fy / 4.0f;
    SDL_Color fill = o.className == "building" ? SDL_Color{240, 150, 60, 95} : SDL_Color{220, 80, 220, 90};
    for (int qy = static_cast<int>(std::floor((o.y - hy) * 2)); qy <= static_cast<int>(std::floor((o.y + hy) * 2)); qy++)
      for (int qx = static_cast<int>(std::floor((o.x - hx) * 2)); qx <= static_cast<int>(std::floor((o.x + hx) * 2)); qx++) {
        float mx = qx * 0.5f + 0.25f, my = qy * 0.5f + 0.25f;
        if (!(mx > o.x - hx && mx < o.x + hx && my > o.y - hy && my < o.y + hy))
          continue;
        SDL_FPoint q[4];
        at(qx * 0.5f, qy * 0.5f, q[0]);
        at(qx * 0.5f + 0.5f, qy * 0.5f, q[1]);
        at(qx * 0.5f + 0.5f, qy * 0.5f + 0.5f, q[2]);
        at(qx * 0.5f, qy * 0.5f + 0.5f, q[3]);
        int b = static_cast<int>(v.size());
        for (const SDL_FPoint &pq : q)
          v.push_back({pq, fill, {0, 0}});
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
      }
    std::array<SDL_FPoint, 5> box;
    at(o.x - hx, o.y - hy, box[0]);
    at(o.x + hx, o.y - hy, box[1]);
    at(o.x + hx, o.y + hy, box[2]);
    at(o.x - hx, o.y + hy, box[3]);
    box[4] = box[0];
    boxes.push_back(box);
  }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  if (!idx.empty())
    SDL_RenderGeometry(ren, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
  SDL_SetRenderDrawColor(ren, 240, 110, 240, 230);
  for (auto &box : boxes)
    SDL_RenderDrawLinesF(ren, box.data(), 5);
  // Each door (entrpos): green on a path a guest can stand on, yellow off one
  for (const PlacedObjects::Object &o : world->getObjects().objects()) {
    if (o.fence)
      continue;
    for (const auto &[dx, dy] : world->getGuests().doorTiles(o)) {
      SDL_FPoint d[5];
      at(dx + 0.15f, dy + 0.15f, d[0]);
      at(dx + 0.85f, dy + 0.15f, d[1]);
      at(dx + 0.85f, dy + 0.85f, d[2]);
      at(dx + 0.15f, dy + 0.85f, d[3]);
      d[4] = d[0];
      bool onPath = world->getGuests().canWalk(dx, dy);
      SDL_SetRenderDrawColor(ren, onPath ? 60 : 255, onPath ? 230 : 220, onPath ? 90 : 40, 255);
      for (int k = -1; k <= 1; k++) {
        SDL_FPoint s[5];
        for (int j = 0; j < 5; j++)
          s[j] = {d[j].x, d[j].y + k};
        SDL_RenderDrawLinesF(ren, s, 5);
      }
    }
  }
  SDL_SetRenderDrawColor(ren, 255, 60, 50, 255);
  for (const SDL_FPoint &c : missing)
    for (int d = -1; d <= 1; d++) {
      SDL_RenderDrawLineF(ren, c.x - 7, c.y - 7 + d, c.x + 7, c.y + 7 + d);
      SDL_RenderDrawLineF(ren, c.x - 7, c.y + 7 + d, c.x + 7, c.y - 7 + d);
    }
}

// Everyone's route: where each is walking, from where it is to its goal
// (animals orange, guests blue, staff green)
void LabApp::drawRoutes() {
  auto at = [&](float x, float y, SDL_FPoint &p) {
    const MapTile *t = world->getMap().getTile(static_cast<int>(x), static_cast<int>(y));
    float h = t ? (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) / 4.0f : 0.0f;
    int wx, wy;
    world->pointToWindow(x, y, h, wx, wy);
    p = {static_cast<float>(wx), static_cast<float>(wy)};
  };
  auto route = [&](float x, float y, const std::vector<std::pair<float, float>> &path, size_t from, SDL_Color c) {
    if (from >= path.size())
      return;
    std::vector<SDL_FPoint> pts(1);
    at(x, y, pts[0]);
    for (size_t i = from; i < path.size(); i++) {
      pts.emplace_back();
      at(path[i].first, path[i].second, pts.back());
    }
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 230);
    SDL_RenderDrawLinesF(ren, pts.data(), static_cast<int>(pts.size()));
    for (SDL_FPoint &p : pts)
      p.y += 1;
    SDL_RenderDrawLinesF(ren, pts.data(), static_cast<int>(pts.size()));
    // (its goal: a small diamond)
    SDL_FPoint g = pts.back();
    SDL_FPoint d[5] = {{g.x, g.y - 5}, {g.x + 8, g.y}, {g.x, g.y + 5}, {g.x - 8, g.y}, {g.x, g.y - 5}};
    SDL_RenderDrawLinesF(ren, d, 5);
  };
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  for (const Animals::Member &m : world->getAnimals().members())
    route(m.x, m.y, m.path, m.pathAt, {240, 150, 70, 255});
  for (const Guests::Guest &g : world->getGuests().guests())
    route(g.x, g.y, g.path, g.pathAt, {110, 170, 245, 255});
  for (const Staff::Member &m : world->getStaff().members())
    route(m.x, m.y, m.path, m.pathAt, {110, 225, 140, 255});
}

// ------------------------------------------------------------- stepping
// A source file's path from the repo (__FILE__ is the full path)
std::string relSrc(const char *file) {
  std::string f = file ? file : "";
  std::replace(f.begin(), f.end(), '\\', '/');
  size_t at = f.rfind("/src/");
  return at == std::string::npos ? f : f.substr(at + 1);
}

void LabApp::updateTraceOn() {
  LabTrace::on = host && host->connected() && (traceMode != "off" || !breakpoints.empty());
}

// Whose steps are shown: what's picked (an exhibit: and its animals; an
// animal: and the rating of its exhibit), the tools used, a mod's animals;
// or everyone's
bool LabApp::traced(const LabTrace::Who &w) {
  // (a game file's reader, run from Engine code: always, it's asked for)
  if (w.kind && std::string(w.kind) == "reader")
    return true;
  if (traceMode == "off")
    return false;
  if (traceMode == "all")
    return true;
  if (!w.kind)
    return false;
  std::string k = w.kind;
  if (k == "tool")
    return true;
  if (sel.kind == k && sel.id == w.id)
    return true;
  Animals &an = world->getAnimals();
  if (k == "animal") {
    if (sel.kind == "exhibit")
      if (const Animals::Member *m = an.member(w.id))
        return m->exhibit == sel.id;
    return std::find(labAnimals.begin(), labAnimals.end(), w.id) != labAnimals.end();
  }
  if (k == "exhibit" && sel.kind == "animal")
    if (const Animals::Member *m = an.member(sel.id))
      return m->exhibit == w.id;
  return false;
}

std::string LabApp::whoName(const LabTrace::Who &w) {
  if (!w.kind)
    return "the world";
  std::string k = w.kind;
  if (k == "reader")
    return "a game file's reader";
  if (k == "animal")
    if (const Animals::Member *m = world->getAnimals().member(w.id))
      return m->name;
  if (k == "guest")
    if (const Guests::Guest *g = world->getGuests().guest(w.id))
      return g->name;
  if (k == "staff")
    if (const Staff::Member *m = world->getStaff().member(w.id))
      return m->name;
  if (k == "exhibit")
    if (const Fences::Exhibit *e = world->getFences().exhibit(w.id))
      return e->named ? e->name : "exhibit " + std::to_string(w.id);
  if (k == "tool")
    return "you (a tool)";
  return k + " " + std::to_string(w.id);
}

void LabApp::onStep(const LabTrace::Step &s) {
  if (inBreak || !host || !host->connected())
    return;
  std::string file = relSrc(s.file);
  std::string at = file + ":" + std::to_string(s.line);
  // (a step's call over several lines says the last one: a breakpoint on any
  // of its lines - the few above - stops there too)
  bool bp = false;
  for (int l = s.line; l >= std::max(1, s.line - 4) && !bp; l--)
    bp = breakpoints.count(file + ":" + std::to_string(l)) > 0;
  LabTrace::Who w = LabTrace::who();
  bool watched = traced(w);
  if (!watched && !bp)
    return;
  int depth = static_cast<int>(LabTrace::stack().size());
  bool pause = bp || (watched && (stepEach || breakNext || (stepOverDepth >= 0 && depth <= stepOverDepth)));
  // (running: 300 a second at most - a whole zoo at 8x is thousands)
  Uint32 now = SDL_GetTicks();
  if (now - stepSecond >= 1000) {
    if (stepsDropped)
      host->send("{\"t\":\"stepsDropped\",\"n\":" + std::to_string(stepsDropped) + "}");
    stepSecond = now;
    stepsThisSecond = stepsDropped = 0;
  }
  // (skipping time - minutes of the zoo in a moment: the latest steps kept,
  // sent when it's done, so what the zoo did last is what the list shows)
  if (!pause && skipping) {
    skipSteps.push_back(stepJson(s, file, w, depth, pause, bp));
    if (skipSteps.size() > 1500) {
      skipSteps.pop_front();
      stepsDropped++;
    }
    return;
  }
  if (!pause && ++stepsThisSecond > 300) {
    stepsDropped++;
    return;
  }
  host->send(stepJson(s, file, w, depth, pause, bp));
  if (pause) {
    stepWho = w;
    stepAt = at;
    breakLoop(depth);
  }
}

// A step as the app's message (its values and the stack read now, while
// they're there)
std::string LabApp::stepJson(const LabTrace::Step &s, const std::string &file, const LabTrace::Who &w, int depth,
                             bool pause, bool bp) {
  std::string vals, stack;
  for (const LabTrace::Val &v : s.vals) {
    JObj o;
    o.str("n", v.name);
    if (v.isText)
      o.str("s", v.text);
    else if (std::isfinite(v.to))
      o.num("v", v.to);
    if (!v.isText && std::isfinite(v.from))
      o.num("f", v.from);
    vals += (vals.empty() ? "" : ",") + o.done();
  }
  for (const LabTrace::Frame &f : LabTrace::stack())
    stack += (stack.empty() ? "" : ",") + JObj()
                                              .str("fn", f.fn ? f.fn : "")
                                              .str("zoo", f.zoo ? f.zoo : "")
                                              .str("what", f.what ? f.what : "")
                                              .str("file", relSrc(f.file))
                                              .num("line", f.line)
                                              .done();
  return JObj()
                 .str("t", "step")
                 .num("n", ++stepSeq)
                 .num("time", simClock)
                 .str("file", file)
                 .num("line", s.line)
                 .str("fn", s.fn ? s.fn : "")
                 .str("zoo", s.zoo ? s.zoo : "")
                 .str("what", s.what ? s.what : "")
                 .flag("enter", s.enter)
                 .str("who", w.kind ? w.kind : "")
                 .num("whoId", w.id)
                 .str("whoName", whoName(w))
                 .num("depth", depth)
                 .flag("paused", pause)
                 .flag("breakpoint", bp)
                 .add("vals", "[" + vals + "]")
                 .add("stack", "[" + stack + "]")
                 .done();
}

// Stopped at a step: the engine waits here (the world mid-update), still
// drawing and answering what doesn't change the world (the view, picking,
// the stepping itself); the rest waits until it goes on
void LabApp::breakLoop(int depth) {
  inBreak = true;
  breakNext = false;
  stepOverDepth = -1;
  frameWanted = true;
  static const std::set<std::string> ok = {"frame",   "viewport", "pan",      "zoom",    "rotate", "select",
                                           "focusSel", "overlay", "trace",    "stepEach", "breakpoint",
                                           "breakpoints", "catalog", "icon",  "outline", "maps",   "view",
                                           "turn",    "artZoom",  "artFrame", "files",    "file",    "reloadKb", "scripts",
                                           "focus",   "focusLab", "quit",     "speciesValues"};
  Uint32 lastState = 0, lastFrame = 0;
  int how = 0; // 1 step, 2 step over, 3 continue, 4 step out
  while (!how && !hostQuit && host->connected()) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_QUIT)
        hostQuit = true;
    for (const Json &c : host->take()) {
      std::string cmd = c["cmd"].str();
      if (cmd == "stepNext")
        how = 1;
      else if (cmd == "stepOver")
        how = 2;
      else if (cmd == "continue")
        how = 3;
      else if (cmd == "stepOut")
        how = 4;
      else if (ok.count(cmd))
        hostCommand(c);
      else if (cmd == "mouse" && (c["ev"].str() == "move" || c["ev"].str() == "leave"))
        ; // (the cursor's place: not while stopped)
      else
        deferred.push_back(c);
    }
    Uint32 now = SDL_GetTicks();
    if (now - lastState >= 150) {
      lastState = now;
      sendState();
    }
    if (frameWanted && hostW > 0 && now - lastFrame >= 16) {
      frameWanted = false;
      lastFrame = now;
      sendFrame();
    } else {
      SDL_Delay(4);
    }
  }
  if (how == 1)
    breakNext = true;
  else if (how == 2)
    stepOverDepth = depth;
  else if (how == 4)
    stepOverDepth = std::max(0, depth - 1);
  stepWho = {};
  stepAt.clear();
  inBreak = false;
  host->send("{\"t\":\"resumed\"}");
}

// A command back as text (the rebuild list, sent to the app)
static std::string jsonText(const Json &j) {
  switch (j.kind) {
  case Json::Kind::Null:
    return "null";
  case Json::Kind::Bool:
    return j.b ? "true" : "false";
  case Json::Kind::Number: {
    char b[40];
    snprintf(b, sizeof b, "%.9g", j.n);
    return b;
  }
  case Json::Kind::String:
    return jq(j.s);
  case Json::Kind::Array: {
    std::string o = "[";
    for (size_t i = 0; i < j.items.size(); i++)
      o += (i ? "," : "") + jsonText(j.items[i]);
    return o + "]";
  }
  case Json::Kind::Object: {
    std::string o = "{";
    bool first = true;
    for (const auto &[k, v] : j.fields) {
      o += (first ? "" : ",") + jq(k) + ":" + jsonText(v);
      first = false;
    }
    return o + "}";
  }
  }
  return "null";
}

void LabApp::hostCommand(const Json &c) {
  std::string cmd = c["cmd"].str();
  // (the lab zoo's set-up, kept to be made again after a build: the map, the
  // tools used - in world places, recorded as they're used - what's picked,
  // pinned, followed, changed. A new map starts the list again)
  static const std::set<std::string> kSetup = {"load", "tool", "lineMode", "walkway", "pin", "watch", "select",
                                               "set", "remove", "undo", "confirm", "act", "rotate", "scriptOn", "mapEdit"};
  if (kSetup.count(cmd)) {
    if (cmd == "load")
      rebuild.clear();
    if (rebuild.size() < 4000)
      rebuild.push_back(c);
  }
  if (cmd == "frame") {
    frameWanted = true;
  } else if (cmd == "viewport") {
    hostW = std::clamp(static_cast<int>(c["w"].num()), 64, 4096);
    hostH = std::clamp(static_cast<int>(c["h"].num()), 64, 4096);
  } else if (cmd == "build") {
    std::string stem = c["script"].str();
    scripts = findScripts(repo);
    for (size_t i = 0; i < scripts.size(); i++)
      if (scripts[i].stem == stem) {
        selScript = static_cast<int>(i);
        startBuild(selScript);
      }
  } else if (cmd == "step") {
    doStep();
  } else if (cmd == "auto") {
    autoStep = c["on"].truthy() && scriptLive;
    lastStepAt = SDL_GetTicks();
  } else if (cmd == "sps") {
    stepsPerSecond = std::clamp(static_cast<float>(c["value"].num(4)), 0.25f, 120.0f);
  } else if (cmd == "stop") {
    unloadScript();
  } else if (cmd == "view") {
    view(c["mode"].str("scene").c_str(), c["what"].str().c_str());
  } else if (cmd == "turn") {
    viewDir = (viewDir + 1) % 4;
  } else if (cmd == "artZoom") {
    artZoom = std::clamp(static_cast<float>(c["value"].num(3)), 0.5f, 8.0f);
  } else if (cmd == "artFrame") {
    // -1 plays it; a frame holds it there; step moves the held frame on/back
    if (c["step"].num(0) != 0) {
      int n = 1;
      for (Animation *a : viewLayers.empty() ? std::vector<Animation *>{viewArt} : viewLayers)
        if (a)
          n = std::max(n, a->frameCount());
      viewFrame = ((std::max(0, viewFrame) + static_cast<int>(c["step"].num(0))) % n + n) % n;
    } else {
      viewFrame = static_cast<int>(c["value"].num(-1));
    }
    addLog(viewFrame < 0 ? std::string("view: playing") : "view: frame " + std::to_string(viewFrame));
  } else if (cmd == "play") {
    playing = c["on"].truthy();
    world->setPaused(!playing);
  } else if (cmd == "pan") {
    float z = std::max(0.1f, g_ZoomLevel);
    world->getRenderer().getCamera().x -= static_cast<int>(c["dx"].num() / z);
    world->getRenderer().getCamera().y -= static_cast<int>(c["dy"].num() / z);
  } else if (cmd == "zoom") {
    g_ZoomLevel = std::clamp(g_ZoomLevel * (c["dir"].num() > 0 ? 1.25f : 0.8f), 0.25f, 4.0f);
  } else if (cmd == "rotate") {
    world->rotateView(1);
  } else if (cmd == "focus") {
    focusAnimal(static_cast<int>(c["animal"].num(-1)));
  } else if (cmd == "focusLab") {
    world->centreOn(labX + labW / 2.0f, labY + labH / 2.0f);
  } else if (cmd == "pick") {
    int id = animalAt(static_cast<int>(c["x"].num()), static_cast<int>(c["y"].num()));
    host->send("{\"t\":\"picked\",\"animal\":" + std::to_string(id) + "}");
  } else if (cmd == "scripts") {
    scripts = findScripts(repo);
    sendScripts();
  } else if (cmd == "files") {
    // The game's files (its archives, the expansions' and updates' over
    // the base game's): those whose path starts with prefix
    std::string prefix = c["prefix"].str();
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
    std::vector<std::string> names = rm->listResources(prefix, "");
    std::sort(names.begin(), names.end());
    std::string list;
    int n = 0;
    for (const std::string &name : names) {
      if (!name.empty() && name.back() == '/')
        continue;
      if (++n > 3000)
        break;
      std::string from = rm->archiveOf(name);
      size_t cut = from.find_last_of("/\\");
      std::string dir = cut == std::string::npos ? "" : from.substr(0, cut);
      size_t cut2 = dir.find_last_of("/\\");
      std::string shortFrom = (cut2 == std::string::npos ? "" : dir.substr(cut2 + 1) + "/") +
                              (cut == std::string::npos ? from : from.substr(cut + 1));
      list += (list.empty() ? "" : ",") + std::string("{\"name\":") + jq(name) + ",\"from\":" + jq(shortFrom) + "}";
    }
    host->send("{\"t\":\"files\",\"prefix\":" + jq(prefix) + ",\"list\":[" + list + "]}");
  } else if (cmd == "file") {
    // One file: its text (a config, a layout), or that it's binary
    std::string name = c["name"].str();
    int size = 0;
    void *data = rm->getFileBytes(name, &size);
    std::string text, kind = "missing";
    if (data && size >= 0) {
      const unsigned char *b = static_cast<const unsigned char *>(data);
      int odd = 0;
      for (int i = 0; i < std::min(size, 4096); i++)
        odd += b[i] == 0 || (b[i] < 9) || (b[i] > 13 && b[i] < 32);
      kind = odd * 50 > std::min(size, 4096) ? "binary" : "text";
      if (kind == "text")
        text.assign(static_cast<const char *>(data), static_cast<size_t>(std::min(size, 400000)));
    }
    free(data);
    host->send("{\"t\":\"file\",\"name\":" + jq(name) + ",\"from\":" + jq(rm->archiveOf(name)) + ",\"kind\":" + jq(kind) +
               ",\"size\":" + std::to_string(size) + ",\"text\":" + jq(text) + "}");
  } else if (cmd == "reloadKb") {
    kb.load(repo);
    // ---- the Scene view's editor
  } else if (cmd == "maps") {
    sendMaps();
  } else if (cmd == "load") {
    const bool ok = loadMap(c["map"].str(mapStem), c["flat"].truthy());
    if (ok) sendOutline();
    if (!c["request"].str().empty())
      host->send(JObj().str("t", "mapLoaded").str("request", c["request"].str())
                 .flag("ok", ok).add("scene", sceneJson()).done());
  } else if (cmd == "mapEdit") {
    // Editing the map (the map editor): walls, entrances and scenery go
    // anywhere; off, the zoo's inside worked out again from its wall
    world->getFences().setEditing(c["on"].truthy());
    if (!c["on"].truthy())
      world->refindInside();
    note(c["on"].truthy() ? "Editing the map: walls, entrances and scenery go anywhere."
                          : "Editing off: the zoo's inside is what its wall and entrance close in.");
  } else if (cmd == "catalog") {
    sendCatalog();
  } else if (cmd == "icon") {
    sendIcon(c["path"].str());
  } else if (cmd == "fileView") {
    sendFileView(c["name"].str(), c["dir"].str());
  } else if (cmd == "filesGrep") {
    sendFilesGrep(c["text"].str());
  } else if (cmd == "formula") {
    // Our engine's own formulas on given inputs, for the Lab's formula
    // checks (re/lab/formulas.json): each case's numbers in, our answer out -
    // the very functions the game calls
    std::string name = c["name"].str(), items;
    for (size_t i = 0; i < c["cases"].size() && i < 2000; i++) {
      const auto &k = c["cases"][i];
      auto n = [&](size_t j) { return k[j].num(); };
      double v = std::nan("");
      if (name == "animalSellValue")
        v = Animals::sellValueOf(static_cast<int>(n(0)), static_cast<int>(n(1)), static_cast<int>(n(2)),
                                 static_cast<int>(n(3)), static_cast<int>(n(4)), n(5) != 0);
      else if (name == "setAdmissionPrice") {
        Original::Economy keep = Original::economy();
        Original::economy().minAdmission = static_cast<float>(n(1));
        Original::economy().maxAdmission = static_cast<float>(n(2));
        v = Original::clampAdmission(static_cast<float>(n(0)));
        Original::economy() = keep;
      }
      char buf[64];
      snprintf(buf, sizeof(buf), std::isnan(v) ? "null" : "%.9g", v);
      items += (items.empty() ? "" : ",") + std::string(buf);
    }
    host->send("{\"t\":\"formula\",\"name\":" + jq(name) + ",\"values\":[" + items + "]}");
  } else if (cmd == "texts") {
    // The game's text by number (its lang dlls, the expansions' over the
    // base game's): what a message id in zoo.exe's code says
    std::string items;
    for (size_t i = 0; i < c["ids"].size() && i < 500; i++) {
      uint32_t id = static_cast<uint32_t>(c["ids"][i].num());
      items += (items.empty() ? "" : ",") + std::string("[") + std::to_string(id) + "," + jq(rm->getString(id)) + "]";
    }
    host->send("{\"t\":\"texts\",\"items\":[" + items + "]}");
  } else if (cmd == "mouseWorld") {
    // (a tool used at a world place: the rebuild list's way to click)
    float wx = static_cast<float>(c["wx"].num()), wy = static_cast<float>(c["wy"].num());
    const MapTile *t = world->getMap().getTile(static_cast<int>(wx), static_cast<int>(wy));
    float h = t ? (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) / 4.0f : 0.0f;
    inWorkView([&] {
      int x = 0, y = 0;
      world->pointToWindow(wx, wy, h, x, y);
      if (x < viewRect.x + 24 || y < viewRect.y + 24 || x > viewRect.x + viewRect.w - 24 || y > viewRect.y + viewRect.h - 24) {
        world->centreOn(wx, wy);
        world->pointToWindow(wx, wy, h, x, y);
      }
      sceneMouse("move", x, y);
      sceneMouse(c["ev"].str(), x, y);
    });
  } else if (cmd == "rebuildGet") {
    // (the set-up, before a build: the app keeps it for the new engine)
    std::string list;
    for (const Json &r : rebuild)
      list += (list.empty() ? "" : ",") + jsonText(r);
    host->send("{\"t\":\"rebuild\",\"clock\":" + std::to_string(simClock) + ",\"cmds\":[" + list + "]}");
  } else if (cmd == "rebuildRun") {
    // (the set-up made again on this engine, then the zoo skipped to the
    // time it had got to)
    // (scripts switched on after the zoo's run back to its time, not
    // before: one holding an animal - Pose - kept an egg from hatching all
    // the way)
    const Json &list = c["cmds"];
    std::vector<size_t> later;
    for (size_t i = 0; i < list.size(); i++)
      if (list[i]["cmd"].str() == "scriptOn")
        later.push_back(i);
      else
        hostCommand(list[i]);
    float to = static_cast<float>(c["clock"].num());
    if (to > simClock)
      stepWorld(to - simClock);
    for (size_t i : later)
      hostCommand(list[i]);
    note("The lab zoo was made again after the build: " + std::to_string(list.size()) + " steps of set-up, back to " +
         std::to_string(static_cast<int>(to)) + " s.");
    sendOutline();
  } else if (cmd == "aiKeys") {
    sendAiKeys();
  } else if (cmd == "watch") {
    // (an animal followed or let go; clear: the log emptied)
    if (c["clear"].truthy()) {
      watchRows.clear();
      watchLast.clear();
    }
    int id = static_cast<int>(c["id"].num(-1));
    if (id >= 0 && c["on"].truthy())
      watching.insert(id);
    else if (id >= 0)
      watching.erase(id), watchLast.erase(id);
    watchTick();
    sendWatch();
  } else if (cmd == "filesIndex") {
    sendFilesIndex();
  } else if (cmd == "readers") {
    // The game files our engine has a reader for (Engine code's Run)
    std::string list;
    for (const GameFileReaders::Reader &r : GameFileReaders::all())
      list += (list.empty() ? "" : ",") + JObj().str("file", r.file).str("name", r.name).done();
    host->send("{\"t\":\"readers\",\"list\":[" + list + "]}");
  } else if (cmd == "readerRun") {
    // A game file's readers run now, their steps shown whatever the Trace
    // setting is, and what each says it read
    std::string file = c["file"].str(), res;
    bool was = LabTrace::on;
    LabTrace::on = host->connected();
    for (const GameFileReaders::Reader &r : GameFileReaders::all())
      if (r.file == file) {
        ZT_WHO("reader", 0);
        std::string said = r.run(rm);
        res += (res.empty() ? "" : ",") + JObj().str("name", r.name).str("summary", said).done();
      }
    LabTrace::on = was;
    updateTraceOn();
    host->send("{\"t\":\"readerRan\",\"file\":" + jq(file) + ",\"results\":[" + res + "]}");
  } else if (cmd == "engineScripts") {
    // The scripts written in Write Code (src/scripts/, EngineScripts.hpp)
    hookScripts();
    sendEngineScripts();
  } else if (cmd == "scriptRun") {
    // A script run now on the Test Zoo, its steps shown whatever the Trace
    // setting is
    hookScripts();
    std::string name = c["name"].str();
    bool was = LabTrace::on;
    LabTrace::on = host->connected();
    bool ok = world && EngineScripts::runOnce(*world, name);
    LabTrace::on = was;
    updateTraceOn();
    if (!ok)
      addLog("No script called " + name + " (built in? Build engine first)");
    host->send("{\"t\":\"scriptRan\",\"name\":" + jq(name) + ",\"ok\":" + (ok ? "true" : "false") + "}");
    sendOutline();
  } else if (cmd == "scriptOn") {
    // A script switched on (each tick of the zoo) or off
    hookScripts();
    std::string name = c["name"].str();
    if (c["on"].truthy())
      EngineScripts::switchedOn().insert(name);
    else
      EngineScripts::switchedOn().erase(name);
    addLog(std::string("Script ") + name + (c["on"].truthy() ? " switched on" : " switched off"));
    sendEngineScripts();
  } else if (cmd == "outline") {
    sendOutline();
  } else if (cmd == "tool") {
    Tool t;
    t.kind = c["kind"].str("select");
    t.item = c["item"].str();
    t.female = c["female"].truthy();
    t.facing = static_cast<int>(c["facing"].num(0)) & 3;
    t.painting = c["painting"].kind == Json::Kind::Null ? true : c["painting"].truthy();
    t.terrainType = static_cast<int>(c["terrainType"].num(0));
    t.size = static_cast<int>(c["size"].num(2));
    t.mode = static_cast<int>(c["mode"].num(0));
    tool = t;
    routeStartSet = hasTest = false; // (the Route tool picked again: a fresh start)
    applyTool();
  } else if (cmd == "act") {
    act(c["action"].str(), c["arg"].str());
  } else if (cmd == "pin") {
    std::string field = c["field"].str();
    pins.erase(std::remove_if(pins.begin(), pins.end(),
                              [&](const Pin &p) { return p.kind == sel.kind && p.id == sel.id && p.field == field; }),
               pins.end());
    if (c["on"].truthy() && (sel.kind == "animal" || sel.kind == "guest"))
      pins.push_back({sel.kind, sel.id, field, c["value"].num()});
    applyPins();
  } else if (cmd == "speciesValues") {
    // A species' .ai values by their keys (cHungerIncrement...: its
    // subtype's over its default's), for zoo.exe's code run on it
    std::string sp = c["species"].str(), sub = c["subtype"].str(), vals;
    for (size_t i = 0; i < c["keys"].size(); i++) {
      std::string k = c["keys"][i].str();
      int found = 0;
      int v = speciesInt(sp.c_str(), sub.c_str(), k.c_str(), &found);
      if (found)
        vals += (vals.empty() ? "" : ",") + jq(k) + ":" + std::to_string(v);
    }
    host->send("{\"t\":\"speciesValues\",\"species\":" + jq(sp) + ",\"subtype\":" + jq(sub) + ",\"values\":{" + vals + "}}");
  } else if (cmd == "speciesCheck") {
    // A species in bulk ("*marine": every tank animal, "*": all): its art
    // (missing, won't load, its views), its Marine Mania layers ("under" /
    // "shadow" + its art's name: there, loading, the same frames), and its
    // behaviour sets against what the engine does - steps it skips (not yet
    // done), moves to places it doesn't know, animations and sets named but
    // not there
    Animals &an = world->getAnimals();
    if (!c["step"].truthy()) {
      // (asked: the species queued, one checked each update - see the host
      // loop - so the Lab keeps drawing and says how far it's got)
      checkWant = c["species"].str();
      checkQueue.clear();
      checkOut.clear();
      for (const Animals::Type &t : an.types()) {
        std::string st = t.file.substr(t.file.find_last_of('/') + 1);
        st = st.substr(0, st.find('.'));
        if (checkWant == "*" || (checkWant == "*marine" && t.underwater) || st == checkWant)
          checkQueue.push_back(st);
      }
      checkOf = checkQueue.size();
      if (checkQueue.empty())
        host->send("{\"t\":\"speciesCheck\",\"want\":" + jq(checkWant) + ",\"species\":[]}");
      return;
    }
    std::string want = checkQueue.front();
    checkQueue.erase(checkQueue.begin());
    // (said before it's done: if one never finishes, this names it)
    host->send("{\"t\":\"speciesCheckProgress\",\"species\":" + jq(want) + ",\"starting\":true,\"done\":" +
               std::to_string(checkOf - checkQueue.size() - 1) + ",\"of\":" + std::to_string(checkOf) + "}");
    // Command recognition is the same table used by Animals::nextStep.
    static const std::set<std::string> playFns = {"fplay", "fplaywithsound", "fplaytime", "fplaypingpong",
                                                  "fplayreverse"};
    // (prey: the chase is the engine's own - Animals::hunt, with its set's
    // move - not the step)
    static const std::set<std::string> doneTargets = {"keeperfood", "otherfood", "0", "", "level",
                                                      "floor", "surface", "dive", "rise", "prey"};
    static const CompassDirection dirs[8] = {CompassDirection::N, CompassDirection::NE, CompassDirection::E,
                                             CompassDirection::SE, CompassDirection::S, CompassDirection::SW,
                                             CompassDirection::W, CompassDirection::NW};
    std::string out;
    for (size_t ti = 0; ti < an.types().size(); ti++) {
      const Animals::Type &t = an.types()[ti];
      std::string stem = t.file.substr(t.file.find_last_of('/') + 1);
      stem = stem.substr(0, stem.find('.'));
      if (!(want == "*" || (want == "*marine" && t.underwater) || stem == want))
        continue;
      std::string missing, layers, refs, ambient;
      std::map<std::string, int> fns, targets;
      int nAnims = 0, nSets = 0, nLayered = 0, badLayers = 0, unreachable = 0;
      // The sets the game can start: from its code (names zoo.exe holds:
      // bSick, bCaughtPrey...), the engine's (bAfterCaught, bIdle1...), the
      // species' moods (its [Ambient] lists), an object's ("b" + the object's
      // file name + what it satisfies + n: bicefloeanimalrest0,
      // botthoophoopjump0), and every set those call, on and on
      std::set<std::string> reachable;
      {
        static std::string exeText;
        static bool exeRead = false;
        if (!exeRead) {
          exeRead = true;
          if (FILE *f = fopen((Paths::gameDir() + "zoo.exe").c_str(), "rb")) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0)
              exeText.append(buf, n);
            fclose(f);
            for (char &ch : exeText)
              ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          }
        }
        static std::vector<std::string> objectStems;
        if (objectStems.empty())
          for (const CatalogItem &it : ItemCatalog::get().all()) {
            std::string f = it.file;
            for (char &ch : f)
              ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            std::string stem = f.substr(f.find_last_of('/') + 1);
            stem = stem.substr(0, stem.find('.'));
            if (stem.size() >= 3 && f.rfind("animals/", 0) != 0)
              objectStems.push_back("b" + stem);
          }
        static const std::set<std::string> engineRoots = {"baftercaught", "bidle1", "beat", "bfindkeeperfood",
                                                          "bsleep", "bsick", "bdie", "bhatch", "bescaped",
                                                          "btestfence", "bcaughtprey"};
        std::vector<std::string> todo;
        auto add = [&](const std::string &name) {
          if (reachable.insert(name).second)
            todo.push_back(name);
        };
        for (const auto &sets : t.sets)
          for (const auto &[name, steps] : sets) {
            bool root = engineRoots.count(name) ||
                        (!exeText.empty() && exeText.find(std::string(name) + '\0') != std::string::npos);
            for (const std::string &o : objectStems)
              root = root || name.rfind(o, 0) == 0;
            // (a show's: "b" + a trick's name= - bTrickBreach, bfoodtrick)
            root = root || name.rfind("btrick", 0) == 0 ||
                   (name.size() > 5 && name.compare(name.size() - 5, 5, "trick") == 0);
            if (root)
              add(name);
          }
        for (const auto *list : {&t.ambient, &t.ambientWater, &t.ambientUnder, &t.ambientSurface})
          for (const Animals::Ambient &am : *list)
            add(am.set);
        while (!todo.empty()) {
          std::string name = todo.back();
          todo.pop_back();
          for (const auto &sets : t.sets) {
            auto it = sets.find(name);
            if (it == sets.end())
              continue;
            for (const Animals::Step &st : it->second)
              for (const std::string &a : st.args) {
                std::string l = a;
                for (char &ch : l)
                  ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                for (const auto &other : t.sets)
                  if (other.count(l))
                    add(l);
              }
          }
        }
      }
      // (each art's name written down before it's loaded, flushed: if one
      // never loads, the last line of species_check_trail.txt names it)
      FILE *trail = fopen("species_check_trail.txt", "w");
      // (each load is a new Animation, all its frames as pictures: freed as
      // soon as it's measured - kept, a whale's few hundred filled the Lab's
      // 32-bit engine and it fell over)
      std::vector<std::unique_ptr<Animation>> loaded;
      auto load = [&](const std::string &path) -> Animation * {
        if (trail) {
          fprintf(trail, "%s %s\n", stem.c_str(), path.c_str());
          fflush(trail);
        }
        if (path.empty() || !rm->hasResource(path + ".ani"))
          return nullptr;
        loaded.emplace_back(rm->getAnimation(path));
        return loaded.back().get();
      };
      for (int s = 0; s < 3; s++) {
        const char *subName = s == 0 ? "m" : s == 1 ? "f" : "y";
        for (const auto &[name, path] : t.anims[s]) {
          nAnims++;
          loaded.clear(); // (the last one's, done with)
          Animation *a = load(path);
          if (!a) {
            missing += (missing.empty() ? "" : ",") + JObj().str("sub", subName).str("anim", name).str("path", path).done();
            continue;
          }
          // (layers: only for the animation itself, not for a layer; and only
          // for the animal's own art - a crate's "idle" isn't its idle)
          if (name.rfind("under", 0) == 0 || name.rfind("shadow", 0) == 0 || path.rfind("animals/", 0) != 0)
            continue;
          std::string f = path.substr(path.find_last_of("/\\") + 1);
          auto u = t.anims[s].find("under" + f), sh = t.anims[s].find("shadow" + f);
          if (u == t.anims[s].end() && sh == t.anims[s].end())
            continue;
          nLayered++;
          Animation *ua = u != t.anims[s].end() ? load(u->second) : nullptr;
          Animation *sa = sh != t.anims[s].end() ? load(sh->second) : nullptr;
          int views = 0;
          for (CompassDirection d : dirs)
            views += a->hasFrames(d) ? 1 : 0;
          bool bad = (u != t.anims[s].end() && (!ua || ua->frameCount() != a->frameCount())) ||
                     (sh != t.anims[s].end() && (!sa || sa->frameCount() != a->frameCount()));
          badLayers += bad ? 1 : 0;
          JObj l;
          l.str("sub", subName).str("anim", name).num("frames", a->frameCount()).num("views", views);
          if (u != t.anims[s].end())
            l.num("under", ua ? ua->frameCount() : -1);
          if (sh != t.anims[s].end())
            l.num("shadow", sa ? sa->frameCount() : -1);
          if (bad)
            l.num("bad", 1);
          layers += (layers.empty() ? "" : ",") + l.done();
        }
        for (const auto &[setName, steps] : t.sets[s]) {
          nSets++;
          // (a set the game can never start - none of its own sets call it,
          // nor its code: the leftover bBabyActions and the bBaby1 / bBaby2
          // it picks from, in 75 species - is left out of the counts)
          if (!reachable.count(setName)) {
            unreachable++;
            continue;
          }
          for (const Animals::Step &st : steps) {
            std::string fn = st.fn;
            for (char &ch : fn)
              ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (Animals::behaviorCommand(fn) == Animals::Command::Unknown)
              fns[fn]++;
            std::string a0 = st.args.empty() ? "" : st.args[0];
            auto has = [&](const std::string &anim) {
              return t.anims[s].count(anim) || t.anims[0].count(anim);
            };
            auto hasSet = [&](const std::string &set) { return t.sets[s].count(set) || t.sets[0].count(set); };
            if (playFns.count(fn) && !a0.empty() && !has(a0))
              refs += (refs.empty() ? "" : ",") + JObj().str("sub", subName).str("set", setName).str("anim", a0).done();
            if ((fn == "fwalk" || fn == "fmove" || fn == "frun") && !doneTargets.count(a0))
              targets[a0]++;
            if (fn == "fmove" && st.args.size() > 2 && !st.args[2].empty() && !has(st.args[2]))
              refs += (refs.empty() ? "" : ",") + JObj().str("sub", subName).str("set", setName).str("anim", st.args[2]).done();
            if (fn == "fplayset" || fn == "fplaysettank" || fn == "fplaysetprob")
              for (size_t i = 0; i < st.args.size(); i += fn == "fplaysetprob" ? 2 : 1)
                if (!st.args[i].empty() && !hasSet(st.args[i]))
                  refs += (refs.empty() ? "" : ",") +
                          JObj().str("sub", subName).str("set", setName).str("missingSet", st.args[i]).done();
          }
        }
      }
      for (const auto *list : {&t.ambient, &t.ambientWater, &t.ambientUnder, &t.ambientSurface})
        for (const Animals::Ambient &am : *list)
          // (in any of its kinds: the mermaid's are all "f", she has no "m")
          if (!t.sets[0].count(am.set) && !t.sets[1].count(am.set) && !t.sets[2].count(am.set))
            ambient += (ambient.empty() ? "" : ",") + jq(am.set);
      auto counts = [](const std::map<std::string, int> &m) {
        std::string s;
        for (const auto &[k, v] : m)
          s += (s.empty() ? "" : ",") + jq(k) + ":" + std::to_string(v);
        return "{" + s + "}";
      };
      if (trail)
        fclose(trail);
      out += (out.empty() ? "" : ",") + std::string("{\"species\":") + jq(stem) + ",\"underwater\":" +
             (t.underwater ? "true" : "false") + ",\"depth\":" + std::to_string(t.needDepth) +
             ",\"anims\":" + std::to_string(nAnims) + ",\"sets\":" + std::to_string(nSets) +
             ",\"setsNeverStarted\":" + std::to_string(unreachable) +
             ",\"layered\":" + std::to_string(nLayered) + ",\"badLayers\":" + std::to_string(badLayers) +
             ",\"missingArt\":[" + missing + "],\"layers\":[" + layers + "],\"stepsNotDone\":" + counts(fns) +
             ",\"movesNotKnown\":" + counts(targets) + ",\"namedNotThere\":[" + refs +
             "],\"ambientNotThere\":[" + ambient + "]}";
    }
    checkOut += (checkOut.empty() || out.empty() ? "" : ",") + out;
    host->send("{\"t\":\"speciesCheckProgress\",\"species\":" + jq(want) + ",\"done\":" +
               std::to_string(checkOf - checkQueue.size()) + ",\"of\":" + std::to_string(checkOf) + "}");
    if (checkQueue.empty())
      host->send("{\"t\":\"speciesCheck\",\"want\":" + jq(checkWant) + ",\"species\":[" + checkOut + "]}");
  } else if (cmd == "trace") {
    std::string m = c["mode"].str("selected");
    traceMode = m == "off" || m == "all" ? m : "selected";
    updateTraceOn();
  } else if (cmd == "stepEach") {
    stepEach = c["on"].truthy();
  } else if (cmd == "stepNext") {
    breakNext = true; // (running: stop at the next step traced)
  } else if (cmd == "stepOver" || cmd == "stepOut" || cmd == "continue") {
    breakNext = false; // (not stopped: nothing to go on from)
    stepOverDepth = -1;
  } else if (cmd == "breakpoint") {
    std::string at = c["file"].str() + ":" + std::to_string(static_cast<int>(c["line"].num()));
    if (c["on"].truthy())
      breakpoints.insert(at);
    else
      breakpoints.erase(at);
    updateTraceOn();
  } else if (cmd == "breakpoints") {
    breakpoints.clear();
    for (size_t i = 0; i < c["list"].size(); i++)
      breakpoints.insert(c["list"][i].str());
    updateTraceOn();
  } else if (cmd == "lineMode") {
    // (how a fence or path drag lays out: Tab in the game; set: to a mode by
    // its name)
    bool path = tool.kind == "path";
    auto now = [&] { return std::string(World::fenceModeName(path ? world->getPathMode() : world->getFenceMode())); };
    std::string want = c["set"].str();
    if (!want.empty()) {
      for (int i = 0; i < 4 && now() != want; i++)
        path ? world->cyclePathMode(1) : world->cycleFenceMode(1);
    } else {
      int step = c["step"].num(1) < 0 ? -1 : 1;
      path ? world->cyclePathMode(step) : world->cycleFenceMode(step);
    }
    // (kept for the rebuild as the mode it's now, not a step from whatever
    // a new engine starts on)
    if (!rebuild.empty() && rebuild.back()["cmd"].str() == "lineMode")
      rebuild.back() = Json::parse("{\"cmd\":\"lineMode\",\"set\":" + jq(now()) + "}");
  } else if (cmd == "walkway") {
    world->adjustBuildHeight(static_cast<int>(c["by"].num(1)));
  } else if (cmd == "mouse") {
    sceneMouse(c["ev"].str(), static_cast<int>(c["x"].num()), static_cast<int>(c["y"].num()));
  } else if (cmd == "mouseTile") {
    // (the same at a tile's middle: for driving the editor from code)
    // (exact: a grid point - fences go from corner to corner - else the
    // tile's middle)
    float half = c["exact"].truthy() ? 0.0f : 0.5f;
    float tx = static_cast<float>(c["tx"].num()) + half, ty = static_cast<float>(c["ty"].num()) + half;
    const MapTile *t = world->getMap().getTile(static_cast<int>(tx), static_cast<int>(ty));
    float h = t ? (t->cornerHeight[0] + t->cornerHeight[1] + t->cornerHeight[2] + t->cornerHeight[3]) / 4.0f : 0.0f;
    inWorkView([&] {
      int x = 0, y = 0;
      world->pointToWindow(tx, ty, h, x, y);
      // (a tile out of the view: brought into it first, else the click lands
      // off the map and does nothing)
      const int m = 24;
      if (x < viewRect.x + m || y < viewRect.y + m || x > viewRect.x + viewRect.w - m || y > viewRect.y + viewRect.h - m) {
        world->centreOn(tx, ty);
        world->pointToWindow(tx, ty, h, x, y);
        addLog("mouseTile: brought " + std::to_string(static_cast<int>(tx)) + "," + std::to_string(static_cast<int>(ty)) + " into view");
      }
      // (dy: that many pixels up or down the screen from where it was
      // pressed, at 1x - a hills tool's drag: 16 a step, as the original's.
      // From the press, not the tile: the ground it raises moves the tile
      // up the screen, and the drag would raise it more and more)
      std::string ev = c["ev"].str();
      if (ev == "down") {
        pressPX = x;
        pressPY = y;
      } else if (c["dy"].kind != Json::Kind::Null) {
        const float zoom = world->getRenderer().getCamera().zoom;
        x = pressPX;
        y = pressPY + static_cast<int>(c["dy"].num() * (zoom > 0 ? zoom : 1.0f));
      }
      sceneMouse("move", x, y);
      if (ev != "move")
        sceneMouse(ev, x, y);
    });
  } else if (cmd == "select") {
    sel = {c["kind"].str(), static_cast<int>(c["id"].num(-1)), static_cast<int>(c["x"].num()), static_cast<int>(c["y"].num())};
    float x, y;
    if (c["focus"].truthy() && selPoint(x, y))
      world->centreOn(x, y);
  } else if (cmd == "focusSel") {
    float x, y;
    if (selPoint(x, y))
      world->centreOn(x, y);
  } else if (cmd == "set") {
    setSel(c["field"].str(), c["value"]);
  } else if (cmd == "remove") {
    removeSel();
  } else if (cmd == "undo") {
    if (world->canUndo())
      world->undo();
  } else if (cmd == "confirm") {
    if (c["yes"].truthy()) {
      if (asking == "drain")
        world->confirmDrain();
      else if (asking == "remove")
        world->confirmFenceRemoval();
    }
    asking.clear();
  } else if (cmd == "economy") {
    // The zoo's rating and admission, as guests come by them (the lab zoo
    // keeps no books, so its rating's set: 50 till told - the side by side
    // gives it the original's). Either left out: unchanged. Answers both
    double r = c["rating"].num(-1), a = c["admission"].num(-1);
    int rating = r < 0 ? world->getZooRating() : static_cast<int>(r);
    double admission = a < 0 ? world->getZooAdmission() : a;
    world->setEconomy(std::clamp(rating, 0, 100), admission);
    host->send("{\"t\":\"economy\",\"rating\":" + std::to_string(world->getZooRating()) + ",\"admission\":" +
               std::to_string(world->getZooAdmission()) + "}");
  } else if (cmd == "speed") {
    speed = std::clamp(static_cast<float>(c["value"].num(1)), 0.25f, 16.0f);
  } else if (cmd == "advance") {
    stepWorld(std::clamp(static_cast<float>(c["seconds"].num(1)), 0.0f, 3600.0f));
  } else if (cmd == "overlay") {
    bool on = c["on"].truthy();
    std::string n = c["name"].str();
    if (n == "grid") showGrid = on;
    else if (n == "exhibits") showExhibits = on;
    else if (n == "paths") world->debugPaths = on;
    else if (n == "routes") showRoutes = on;
    else if (n == "footprints") showFootprints = on;
  } else if (cmd == "modReason") {
    host->send("{\"t\":\"modReason\",\"id\":" + std::to_string(static_cast<int>(c["id"].num())) +
               ",\"reason\":" + jq(Mods::offReason(c["folder"].str())) + "}");
  } else if (cmd == "console") {
    std::string line = c["line"].str(), text;
    std::vector<std::string> words;
    std::istringstream in(line);
    for (std::string w; in >> w;)
      words.push_back(w);
    if (words.empty())
      text = "";
    else if (!console)
      text = "The game's console isn't in this engine.";
    else
      text = console(words);
    host->send("{\"t\":\"console\",\"request\":" + jq(c["request"].str()) + ",\"line\":" + jq(line) + ",\"text\":" + jq(text) + "}");
  } else if (cmd == "saveMap") {
    // The lab zoo as a map design (Test Zoo > Save as a map): over the map
    // it was loaded from - its .scn's [start] savegame= (a map from a mod:
    // the map that one's over), written to the path given; the Lab puts it
    // in a mod with its .scn
    std::string over;
    // (a blank map's: over nothing - its .scn says how big a blank map it's
    // drawn on, [start] blank)
    bool blank = mapStem.rfind("blank:", 0) == 0;
    if (IniReader *scn = rm->getIniReader(mapScn)) {
      blank = blank || !scn->get("start", "blank").empty();
      over = scn->get("start", "savegame");
      delete scn;
    }
    if (blank) over = "blank";
    // (the inside as the wall and entrance now close it in, for the game)
    world->refindInside();
    const std::string path = c["path"].str();
    const bool ok = !over.empty() && !path.empty() && SaveGame::saveMap(path, *world, over);
    if (!ok)
      note(over.empty() ? "This zoo isn't from one of the game's freeform maps: load one first." : "The map couldn't be saved.");
    const WorldMap &map = world->getMap();
    host->send("{\"t\":\"mapSaved\",\"ok\":" + std::string(ok ? "true" : "false") + ",\"path\":" + jq(path) +
               ",\"over\":" + jq(over) + ",\"scn\":" + jq(mapScn) + ",\"w\":" + std::to_string(map.getWidth()) +
               ",\"h\":" + std::to_string(map.getHeight()) + ",\"blank\":" + (blank ? "true" : "false") + "}");
  } else if (cmd == "quit") {
    hostQuit = true;
  }
}

int LabApp::runHost(const std::string &mapStem, int port) {
  repo = findRepo();
  if (repo.empty()) {
    SDL_Log("[Lab] can't find the repo (re/lab/lab_api.h) from here");
    return 1;
  }
  host = std::make_unique<HostLink>(port);
  if (!host->listening()) {
    SDL_Log("[Lab] port %d is taken: is another Lab engine running?", port);
    return 1;
  }
  SDL_RenderSetLogicalSize(ren, 0, 0);
  SDL_RenderSetScale(ren, 1.0f, 1.0f);
  // (the app gone - its end of stdin closed - and this goes too)
  std::atomic<bool> parentGone{false};
  std::thread([&parentGone] {
    char b[256];
    while (std::fgets(b, sizeof b, stdin)) {
    }
    parentGone = true;
  }).detach();
  std::error_code ec;
  for (const auto &e : fs::directory_iterator(repo / "build" / "lab", ec))
    if (std::regex_match(e.path().filename().string(), std::regex(R"(.+_\d+\.(dll|exp|lib))")))
      fs::remove(e.path(), ec);
  kb.load(repo);
  scripts = findScripts(repo);
  std::string exePath = std::getenv("ZT1_EXE") ? std::getenv("ZT1_EXE")
                                                : "C:/Program Files (x86)/Microsoft Games/Zoo Tycoon/zoo.exe";
  exe.load(exePath);
  setupScene(mapStem);
  world->setPaused(true);
  LabTrace::sink = [this](const LabTrace::Step &st) { onStep(st); };
  Pathfinder::wantSearch = [this] { return routeTesting || traced(LabTrace::who()); };
  Pathfinder::searched = [this](const Pathfinder::Search &found) {
    if (routeTesting) {
      testSearch = found;
      hasTest = true;
      return;
    }
    LabTrace::Who w = LabTrace::who();
    if (searches.size() > 300)
      searches.clear();
    searches[{w.kind ? w.kind : "world", w.id}] = found;
  };
  SDL_Log("[Lab] host on 127.0.0.1:%d", port);
  std::printf("ZT1LAB READY %d\n", port);
  std::fflush(stdout);

  Uint32 lastState = 0, lastFrame = 0;
  while (!hostQuit && !parentGone) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_QUIT)
        hostQuit = true;
    if (host->takeConnected()) {
      addLog("Lab app connected");
      sendScripts();
      frameWanted = true;
    }
    updateTraceOn();
    // (what came while stopped at a step, first)
    if (!deferred.empty()) {
      std::vector<Json> later;
      later.swap(deferred);
      for (const Json &c : later)
        hostCommand(c);
    }
    for (const Json &c : host->take())
      hostCommand(c);
    if (buildState == 2 || buildState == 3)
      finishBuild();
    Uint32 now = SDL_GetTicks();
    if (scriptLive && autoStep && now - lastStepAt >= 1000.0f / stepsPerSecond) {
      lastStepAt = now;
      doStep();
    }
    // (the zoo running, at its speed: 0.1 s of game time an update at most)
    float real = lastUpdate ? std::min(0.1f, (now - lastUpdate) / 1000.0f) : 0.0f;
    lastUpdate = now;
    if (playing) {
      static Uint8 none[SDL_NUM_SCANCODES] = {};
      for (float left = real * speed; left > 1e-4f && playing; left -= 0.1f) {
        world->update(none, std::min(0.1f, left));
        simClock += std::min(0.1f, left);
        goals.update(std::min(0.1f, left));
        applyPins();
        watchTick();
      }
      // (the Watch panel kept up: a few times a second)
      if (watchDirty && now - watchSentAt > 300)
        sendWatch();
    } else {
      world->getRenderer().getCamera().zoom = g_ZoomLevel;
    }
    // (a species check under way: the next species, one an update)
    if (!checkQueue.empty() && host->connected()) {
      static const Json step = Json::parse("{\"cmd\":\"speciesCheck\",\"step\":true}");
      hostCommand(step);
    }
    if (now - lastState >= 150 && host->connected()) {
      lastState = now;
      sendState();
    }
    // (a frame when the app has taken the last one: 60 a second at most)
    if (frameWanted && hostW > 0 && host->connected() && now - lastFrame >= 16) {
      frameWanted = false;
      lastFrame = now;
      sendFrame();
    } else {
      SDL_Delay(4);
    }
  }
  LabTrace::on = false;
  LabTrace::sink = nullptr;
  Pathfinder::wantSearch = nullptr;
  Pathfinder::searched = nullptr;
  unloadScript();
  if (buildThread.joinable())
    buildThread.join();
  host.reset();
  return 0;
}

} // namespace

void labHostHide(SDL_Window *window) {
  // Off the screen, off the taskbar: the app shows what it draws
  SDL_SetWindowPosition(window, -20000, -20000);
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (SDL_GetWindowWMInfo(window, &info)) {
    HWND h = info.info.win.window;
    SetWindowLong(h, GWL_EXSTYLE, (GetWindowLong(h, GWL_EXSTYLE) | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
  }
}

int runLab(SDL_Window *window, SDL_Renderer *renderer, ResourceManager *rm, ScenarioManager *sm, World *world,
           const std::string &mapStem, int hostPort, LabConsole console) {
  LabApp app(window, renderer, rm, sm, world);
  app.console = std::move(console);
  return hostPort > 0 ? app.runHost(mapStem, hostPort) : app.run(mapStem);
}
