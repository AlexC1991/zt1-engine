#include "Updater.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <SDL2/SDL.h>

#include "Features.hpp"
#include "Paths.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#endif

#ifndef ZT1_VERSION
#define ZT1_VERSION "dev"
#endif
#ifndef ZT1_OFFICIAL_BUILD
#define ZT1_OFFICIAL_BUILD 0
#endif

namespace fs = std::filesystem;

namespace {

// The feeds, CodeRook first: each a folder holding latest.yml and the
// installer it names
struct Feed {
  const char *name, *base;
};
const Feed kFeeds[] = {
    {"CodeRook", "https://api.coderook.com/v1/public/projects/batty-251/zt1-engine-releases/updates/"},
    {"GitHub", "https://github.com/AlexC1991/zt1-engine/releases/latest/download/"},
};

std::mutex g_lock;
bool g_have = false, g_taken = false;
Updater::Release g_offer;

// "2026.10.19" against "2026.10.5": number by number
bool newer(const std::string &a, const std::string &b) {
  auto parts = [](const std::string &v) {
    std::vector<long> out;
    std::stringstream ss(v);
    std::string p;
    while (std::getline(ss, p, '.')) {
      try {
        out.push_back(std::stol(p));
      } catch (...) {
        out.push_back(0);
      }
    }
    return out;
  };
  std::vector<long> x = parts(a), y = parts(b);
  for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
    long p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
    if (p != q)
      return p > q;
  }
  return false;
}

// latest.yml's top-level "key: 'value'" lines
std::string ymlValue(const std::string &yml, const std::string &key) {
  std::stringstream ss(yml);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.rfind(key + ":", 0) != 0)
      continue;
    std::string v = line.substr(key.size() + 1);
    size_t a = v.find_first_not_of(" \t'\"");
    size_t b = v.find_last_not_of(" \t'\"");
    return a == std::string::npos ? "" : v.substr(a, b - a + 1);
  }
  return "";
}

std::string skipFile() { return Paths::dataDir() + "update-skip.txt"; }

#ifdef _WIN32
std::wstring widen(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

// A GET over HTTPS (redirects followed), into memory or a file
bool httpGet(const std::string &url, std::string *body, const fs::path *file, std::string &why) {
  std::wstring wurl = widen(url);
  URL_COMPONENTS u = {sizeof(u)};
  wchar_t host[256] = {}, path[2048] = {};
  u.lpszHostName = host;
  u.dwHostNameLength = 256;
  u.lpszUrlPath = path;
  u.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &u)) {
    why = "a bad address";
    return false;
  }
  std::wstring agent = widen(std::string("ZT1-Engine/") + Updater::version());
  HINTERNET s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) {
    why = "no internet connection";
    return false;
  }
  WinHttpSetTimeouts(s, 5000, 8000, 8000, file ? 60000 : 8000);
  bool ok = false;
  HINTERNET c = WinHttpConnect(s, host, u.nPort, 0);
  HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                  : nullptr;
  if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(r, nullptr)) {
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status == 200) {
      std::ofstream out;
      if (file)
        out.open(*file, std::ios::binary | std::ios::trunc);
      ok = !file || out.good();
      std::vector<char> buf(64 * 1024);
      DWORD got = 0;
      while (ok && WinHttpReadData(r, buf.data(), static_cast<DWORD>(buf.size()), &got) && got > 0) {
        if (file)
          out.write(buf.data(), got);
        else if (body->size() < 1024 * 1024)
          body->append(buf.data(), got);
      }
      ok = ok && (!file || out.good());
      if (!ok)
        why = "couldn't save the download";
    } else {
      why = "the server answered " + std::to_string(status);
    }
  } else {
    why = "couldn't reach it";
  }
  if (r)
    WinHttpCloseHandle(r);
  if (c)
    WinHttpCloseHandle(c);
  WinHttpCloseHandle(s);
  return ok;
}

// A file's SHA-512, base64 (as latest.yml writes it)
std::string sha512Base64(const fs::path &file) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  std::string out;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA512_ALGORITHM, nullptr, 0) != 0)
    return out;
  if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
    std::ifstream in(file, std::ios::binary);
    std::vector<char> buf(64 * 1024);
    while (in) {
      in.read(buf.data(), buf.size());
      if (in.gcount() > 0)
        BCryptHashData(h, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(in.gcount()), 0);
    }
    unsigned char digest[64];
    if (BCryptFinishHash(h, digest, sizeof(digest), 0) == 0) {
      DWORD n = 0;
      CryptBinaryToStringA(digest, sizeof(digest), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
      std::string b64(n, '\0');
      if (CryptBinaryToStringA(digest, sizeof(digest), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &n))
        out = b64.substr(0, n);
    }
    BCryptDestroyHash(h);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}
#endif

// The first feed that answers, and what it offers (ZT1_UPDATE_FEED, a folder
// URL ending in /, takes their place: for trying an update out)
bool lookUp(Updater::Release &out) {
#ifdef _WIN32
  std::vector<Feed> feeds(std::begin(kFeeds), std::end(kFeeds));
  if (const char *test = std::getenv("ZT1_UPDATE_FEED"))
    feeds = {{"ZT1_UPDATE_FEED", test}};
  for (const Feed &f : feeds) {
    std::string yml, why;
    if (!httpGet(std::string(f.base) + "latest.yml", &yml, nullptr, why)) {
      SDL_Log("[Updater] %s: %s", f.name, why.c_str());
      continue;
    }
    out.version = ymlValue(yml, "version");
    out.file = ymlValue(yml, "path");
    out.sha512 = ymlValue(yml, "sha512");
    out.url = std::string(f.base) + out.file;
    out.from = f.name;
    if (!out.version.empty() && !out.file.empty() && !out.sha512.empty())
      return true;
    SDL_Log("[Updater] %s: its latest.yml names no installer", f.name);
  }
#else
  (void)out;
#endif
  return false;
}

} // namespace

namespace Updater {

const char *version() { return ZT1_VERSION; }
bool official() { return ZT1_OFFICIAL_BUILD != 0; }

void checkInBackground() {
  if (!official() || Paths::portable() || !Features::updateChecks) {
    SDL_Log("[Updater] not looking (%s)", !official() ? "a build of one's own: updates are off"
                                          : Paths::portable() ? "a portable copy"
                                                              : "updatechecks is off");
    return;
  }
  std::thread([] {
    Release r;
    if (!lookUp(r) || !newer(r.version, version())) {
      SDL_Log("[Updater] %s is the newest", version());
      return;
    }
    std::ifstream in(skipFile());
    std::string skipped;
    std::getline(in, skipped);
    if (skipped == r.version) {
      SDL_Log("[Updater] %s is out; the player said to skip it", r.version.c_str());
      return;
    }
    SDL_Log("[Updater] %s is out (from %s)", r.version.c_str(), r.from.c_str());
    std::lock_guard<std::mutex> g(g_lock);
    g_offer = r;
    g_have = true;
  }).detach();
}

int checkNow(bool installIt) {
  Release r;
  if (!lookUp(r)) {
    SDL_Log("[Updater] no feed answered");
    return 2;
  }
  SDL_Log("[Updater] %s offers %s (this is %s): %s", r.from.c_str(), r.version.c_str(), version(), r.url.c_str());
  if (!newer(r.version, version())) {
    SDL_Log("[Updater] nothing newer");
    return 0;
  }
  if (!installIt)
    return 0;
  std::string why;
  if (!install(r, why)) {
    SDL_Log("[Updater] %s", why.c_str());
    return 3;
  }
  SDL_Log("[Updater] the installer is running");
  return 0;
}

bool takeOffer(Release &out) {
  std::lock_guard<std::mutex> g(g_lock);
  if (!g_have || g_taken)
    return false;
  g_taken = true;
  out = g_offer;
  return true;
}

void skip(const Release &r) {
  std::ofstream out(skipFile(), std::ios::trunc);
  out << r.version << "\n";
}

bool install(const Release &r, std::string &why) {
#ifdef _WIN32
  wchar_t tmp[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, tmp);
  fs::path setup = fs::path(tmp) / fs::path(r.file).filename();
  if (!httpGet(r.url, nullptr, &setup, why)) {
    why = "The download failed: " + why;
    return false;
  }
  if (sha512Base64(setup) != r.sha512) {
    std::error_code ec;
    fs::remove(setup, ec);
    why = "The download didn't match its SHA-512, so it wasn't run.";
    return false;
  }
  // Silently, closing this engine; /RELAUNCH: start it again afterwards
  std::wstring cmd = L"\"" + setup.wstring() + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RELAUNCH";
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    why = "Couldn't start the installer.";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
#else
  (void)r;
  why = "Updates are Windows only.";
  return false;
#endif
}

} // namespace Updater
