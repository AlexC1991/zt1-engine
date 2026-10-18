#include "Updater.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
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

// Where releases are looked for, in turn: CodeRook's zt1-engine project (a
// release there offers its downloads while the files are attached before it's
// named, so its feed never runs ahead of its installer), then GitHub's latest
// release. Each one's signed feed: CodeRook writes a latest.yml of its own
// (no signature), so ours is attached there as zt1-update.yml. Either way an
// unsigned or wrongly signed feed is turned down.
struct Feed {
  const char *name, *base, *yml;
};
const Feed kFeeds[] = {
    {"CodeRook", "https://api.coderook.com/v1/public/projects/batty-251/zt1-engine/updates/", "zt1-update.yml"},
    {"GitHub", "https://github.com/AlexC1991/zt1-engine/releases/latest/download/", "latest.yml"},
};

std::mutex g_lock;

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

// Raw P-256 public key (X || Y, big-endian). The matching private key is kept
// outside the repository and is used only by the release signer.
constexpr char kUpdatePublicKeyHex[] =
    "09bdfbf256b798cd631fa24074f8e0d4f5514c68cebca1c352e29af5b9d6d46f"
    "b837e9e3f3112b998bf6cdbcfebf7e0297c8a7abf6ed7e74c6b42d243363e353";

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

bool verifyUpdateSignature(const std::string &message, const std::string &encodedSignature) {
  if (encodedSignature.size() != 88 || encodedSignature.compare(86, 2, "==") != 0 ||
      !std::all_of(encodedSignature.begin(), encodedSignature.end() - 2, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '+' || c == '/';
      }))
    return false;

  DWORD signatureSize = 0;
  if (!CryptStringToBinaryA(encodedSignature.c_str(), static_cast<DWORD>(encodedSignature.size()),
                            CRYPT_STRING_BASE64, nullptr, &signatureSize, nullptr, nullptr) ||
      signatureSize != 64)
    return false;
  std::vector<BYTE> signature(signatureSize);
  if (!CryptStringToBinaryA(encodedSignature.c_str(), static_cast<DWORD>(encodedSignature.size()),
                            CRYPT_STRING_BASE64, signature.data(), &signatureSize, nullptr, nullptr))
    return false;

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE publicKey = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0)
    return false;

  BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
  std::vector<BYTE> keyBlob(sizeof(header) + 64);
  std::memcpy(keyBlob.data(), &header, sizeof(header));
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  bool keyOk = sizeof(kUpdatePublicKeyHex) == 129;
  for (size_t i = 0; keyOk && i < 64; ++i) {
    int hi = nibble(kUpdatePublicKeyHex[i * 2]), lo = nibble(kUpdatePublicKeyHex[i * 2 + 1]);
    keyOk = hi >= 0 && lo >= 0;
    if (keyOk) keyBlob[sizeof(header) + i] = static_cast<BYTE>((hi << 4) | lo);
  }

  bool valid = false;
  if (keyOk && BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &publicKey,
                                   keyBlob.data(), static_cast<ULONG>(keyBlob.size()), 0) == 0) {
    BCRYPT_ALG_HANDLE hashAlgorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&hashAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
      DWORD hashSize = 0, returned = 0;
      if (BCryptGetProperty(hashAlgorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize),
                            sizeof(hashSize), &returned, 0) == 0 && hashSize == 32 &&
          BCryptCreateHash(hashAlgorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<BYTE> digest(hashSize);
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(message.data())),
                           static_cast<ULONG>(message.size()), 0) == 0 &&
            BCryptFinishHash(hash, digest.data(), hashSize, 0) == 0)
          valid = BCryptVerifySignature(publicKey, nullptr, digest.data(), hashSize, signature.data(),
                                        signatureSize, 0) == 0;
        BCryptDestroyHash(hash);
      }
      BCryptCloseAlgorithmProvider(hashAlgorithm, 0);
    }
    BCryptDestroyKey(publicKey);
  }
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return valid;
}
#endif

// The first feed that answers, and what it offers (ZT1_UPDATE_FEED, a folder
// URL ending in /, takes their place: for trying an update out)
bool lookUp(Updater::Release &out) {
#ifdef _WIN32
  std::vector<Feed> feeds(std::begin(kFeeds), std::end(kFeeds));
  if (const char *test = std::getenv("ZT1_UPDATE_FEED"))
    feeds = {{"ZT1_UPDATE_FEED", test, "latest.yml"}};
  for (const Feed &f : feeds) {
    std::string yml, why;
    if (!httpGet(std::string(f.base) + f.yml, &yml, nullptr, why)) {
      SDL_Log("[Updater] %s: %s", f.name, why.c_str());
      continue;
    }
    out.version = ymlValue(yml, "version");
    out.file = ymlValue(yml, "path");
    out.sha512 = ymlValue(yml, "sha512");
    out.size = ymlValue(yml, "zt1Size");
    out.signature = ymlValue(yml, "zt1Signature");
    const std::string app = ymlValue(yml, "zt1App");
    out.url = std::string(f.base) + out.file;
    out.from = f.name;
    const std::string expected = "ZT1-Engine-Setup-" + out.version + ".exe";
    const bool validVersion = out.version.size() >= 8 &&
                              std::all_of(out.version.begin(), out.version.end(), [](char c) {
                                return (c >= '0' && c <= '9') || c == '.';
                              }) && out.version.front() != '.' && out.version.back() != '.' &&
                              std::count(out.version.begin(), out.version.end(), '.') == 2 &&
                              out.version.find("..") == std::string::npos;
    const bool validHash = out.sha512.size() == 88 &&
                           std::all_of(out.sha512.begin(), out.sha512.end() - 2, [](char c) {
                             return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                    (c >= '0' && c <= '9') || c == '+' || c == '/';
                           }) && out.sha512.compare(86, 2, "==") == 0;
    bool validSize = !out.size.empty() && out.size.size() <= 12 &&
                     std::all_of(out.size.begin(), out.size.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (validSize && out.size.size() > 1 && out.size.front() == '0')
      validSize = false;
    const std::string signedFields = app + "\n" + out.version + "\n" + out.file + "\n" + out.size + "\n" + out.sha512;
    if (validVersion && app == "zt1-engine" && out.file == expected && validHash && validSize &&
        verifyUpdateSignature(signedFields, out.signature))
      return true;
    SDL_Log("[Updater] %s: latest.yml is invalid or its update signature is not trusted", f.name);
  }
#else
  (void)out;
#endif
  return false;
}


// --- Looking, downloading, offering (Updater.hpp)
// The looking thread sleeps on this between looks; Check now wakes it
std::condition_variable g_wake;
bool g_wakeNow = false, g_manual = false, g_started = false;
Updater::State g_state = Updater::State::Off;
std::string g_status;
// The newer release downloaded and checked, waiting to be installed
bool g_ready = false, g_offered = false, g_installing = false;
Updater::Release g_readyRelease;
fs::path g_readyFile;

void setState(Updater::State s, const std::string &status) {
  std::lock_guard<std::mutex> g(g_lock);
  g_state = s;
  g_status = status;
  SDL_Log("[Updater] %s", status.c_str());
}

// Where an update waits for its install (the data folder's updates\)
fs::path pendingDir() { return fs::path(Paths::dataDir()) / "updates"; }

std::string skipped() {
  std::ifstream in(skipFile());
  std::string v;
  std::getline(in, v);
  return v;
}

#ifdef _WIN32
// Its size and SHA-512 against what the signed feed says
bool matches(const fs::path &file, const Updater::Release &r) {
  std::error_code ec;
  auto size = fs::file_size(file, ec);
  return !ec && std::to_string(size) == r.size && sha512Base64(file) == r.sha512;
}

// Downloaded (or there already from an earlier look) and checked: where it is
bool fetch(const Updater::Release &r, fs::path &out, std::string &why) {
  std::error_code ec;
  fs::create_directories(pendingDir(), ec);
  fs::path target = pendingDir() / fs::path(r.file).filename();
  if (fs::exists(target, ec) && matches(target, r)) {
    out = target;
    return true;
  }
  fs::path part = target;
  part += ".part";
  if (!httpGet(r.url, nullptr, &part, why)) {
    fs::remove(part, ec);
    why = "the download failed: " + why;
    return false;
  }
  if (!matches(part, r)) {
    fs::remove(part, ec);
    why = "the download didn't match its signed size and SHA-512, so it was thrown away";
    return false;
  }
  fs::rename(part, target, ec);
  if (ec) {
    why = "couldn't keep the download";
    return false;
  }
  out = target;
  return true;
}

// Installers left from earlier updates (this version or older, or half
// downloaded) go
void tidyPending() {
  std::error_code ec;
  for (const auto &e : fs::directory_iterator(pendingDir(), ec)) {
    std::string name = e.path().filename().string();
    const std::string pre = "ZT1-Engine-Setup-";
    if (name.rfind(pre, 0) != 0)
      continue;
    bool part = name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0;
    std::string v = name.substr(pre.size());
    v = v.substr(0, v.find(".exe"));
    if (part || !newer(v, ZT1_VERSION))
      fs::remove(e.path(), ec);
  }
}

// One look: the feed, and a newer release downloaded and checked. False: it
// couldn't be done (looked again sooner)
bool lookOnce() {
  bool manual;
  {
    std::lock_guard<std::mutex> g(g_lock);
    manual = g_manual;
    g_manual = false;
  }
  setState(Updater::State::Checking, "looking...");
  Updater::Release r;
  if (!lookUp(r)) {
    setState(Updater::State::Failed, "couldn't reach the servers (again in 5 min)");
    return false;
  }
  if (!newer(r.version, ZT1_VERSION)) {
    setState(Updater::State::UpToDate, std::string(ZT1_VERSION) + " is the newest");
    return true;
  }
  if (!manual && skipped() == r.version) {
    setState(Updater::State::UpToDate, r.version + " is out (skipped)");
    return true;
  }
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (g_ready && g_readyRelease.version == r.version) {
      g_state = Updater::State::Ready;
      if (manual)
        g_offered = false; // (Check now: asked again)
      return true;
    }
  }
  setState(Updater::State::Downloading, "downloading " + r.version + "...");
  fs::path file;
  std::string why;
  if (!fetch(r, file, why)) {
    setState(Updater::State::Failed, r.version + ": " + why);
    return false;
  }
  std::lock_guard<std::mutex> g(g_lock);
  g_ready = true;
  g_offered = false;
  g_readyRelease = r;
  g_readyFile = file;
  g_state = Updater::State::Ready;
  g_status = r.version + " is ready to install";
  SDL_Log("[Updater] %s (from %s)", g_status.c_str(), r.from.c_str());
  return true;
}

// An installer, checked again first, run silently (relaunch: the game
// starts again after)
bool runInstaller(const fs::path &setup, const Updater::Release &r, bool relaunch, std::string &why) {
  if (!matches(setup, r)) {
    why = "The update on disk no longer matches its signed size and SHA-512, so it wasn't run.";
    return false;
  }
  std::wstring cmd = L"\"" + setup.wstring() + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS";
  if (relaunch)
    cmd += L" /RELAUNCH";
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    why = "Couldn't start the installer.";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

// The looking thread: 15 s after start, then every 30 minutes (5 after one
// that failed), and whenever Check now asks. With Check for updates off it
// only looks when asked
void loop() {
  std::unique_lock<std::mutex> lk(g_lock);
  g_wake.wait_for(lk, std::chrono::seconds(15), [] { return g_wakeNow; });
  for (;;) {
    g_wakeNow = false;
    bool look = Features::updateChecks || g_manual;
    lk.unlock();
    bool ok = true;
    if (look)
      ok = lookOnce();
    else
      setState(Updater::State::Off, "off (Check now still looks)");
    lk.lock();
    g_wake.wait_for(lk, ok ? std::chrono::minutes(30) : std::chrono::minutes(5), [] { return g_wakeNow; });
  }
}
#endif

} // namespace

namespace Updater {

const char *version() { return ZT1_VERSION; }
bool official() { return ZT1_OFFICIAL_BUILD != 0; }

bool possible(std::string *why) {
  std::string w;
  if (!official())
    w = "none in a build of your own";
  else if (Paths::portable())
    w = "none in a portable copy";
  if (why)
    *why = w;
  return w.empty();
}

void start() {
#ifdef _WIN32
  std::string why;
  if (!possible(&why)) {
    setState(State::Off, why);
    return;
  }
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (g_started)
      return;
    g_started = true;
  }
  tidyPending();
  setState(Features::updateChecks ? State::Idle : State::Off,
           Features::updateChecks ? "looks shortly" : "off (Check now still looks)");
  std::thread(loop).detach();
#else
  setState(State::Off, "Windows only");
#endif
}

void checkNow() {
  if (!possible(nullptr))
    return;
  start();
  std::lock_guard<std::mutex> g(g_lock);
  g_manual = true;
  g_wakeNow = true;
  g_wake.notify_all();
}

State state() {
  std::lock_guard<std::mutex> g(g_lock);
  return g_state;
}

std::string status() {
  std::lock_guard<std::mutex> g(g_lock);
  return g_status;
}

bool takeOffer(Release &out) {
  std::lock_guard<std::mutex> g(g_lock);
  if (!g_ready || g_offered || g_installing)
    return false;
  g_offered = true;
  out = g_readyRelease;
  return true;
}

void skip(const Release &r) {
  {
    std::ofstream out(skipFile(), std::ios::trunc);
    out << r.version << "\n";
  }
  std::lock_guard<std::mutex> g(g_lock);
  if (g_ready && g_readyRelease.version == r.version) {
    g_ready = false; // (not installed on quit either)
    g_state = State::UpToDate;
    g_status = r.version + " skipped";
  }
}

bool installNow(std::string &why) {
#ifdef _WIN32
  Release r;
  fs::path file;
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (!g_ready) {
      why = "No update is ready.";
      return false;
    }
    r = g_readyRelease;
    file = g_readyFile;
  }
  if (!runInstaller(file, r, true, why))
    return false;
  std::lock_guard<std::mutex> g(g_lock);
  g_installing = true;
  return true;
#else
  why = "Updates are Windows only.";
  return false;
#endif
}

bool installing() {
  std::lock_guard<std::mutex> g(g_lock);
  return g_installing;
}

void atExit() {
#ifdef _WIN32
  Release r;
  fs::path file;
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (!g_ready || g_installing || !Features::updateChecks)
      return;
    r = g_readyRelease;
    file = g_readyFile;
    g_installing = true;
  }
  std::string why;
  if (runInstaller(file, r, false, why))
    SDL_Log("[Updater] installing %s as the game closes", r.version.c_str());
  else
    SDL_Log("[Updater] %s", why.c_str());
#endif
}

int checkNowCli(bool installIt) {
#ifdef _WIN32
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
  fs::path file;
  std::string why;
  if (!fetch(r, file, why) || !runInstaller(file, r, true, why)) {
    SDL_Log("[Updater] %s", why.c_str());
    return 3;
  }
  SDL_Log("[Updater] the installer is running");
  return 0;
#else
  (void)installIt;
  return 2;
#endif
}

} // namespace Updater
