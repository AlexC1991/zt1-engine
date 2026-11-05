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
#include <functional>
#include <cmath>
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
#ifndef ZT1_TEST_BUILD
#define ZT1_TEST_BUILD 0
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
uint64_t g_downloaded = 0, g_total = 0;
double g_speed = 0;

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
  std::wstring w(n > 0 ? n : 0, L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.pop_back();
  }
  return w;
}

// A GET over HTTPS (redirects followed), into memory or a file
bool httpGet(const std::string &url, std::string *body, const fs::path *file, std::string &why,
             uint64_t expected = 0, const std::function<void(uint64_t)> &progress = {}) {
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
  const std::wstring server(host, u.dwHostNameLength);
  const bool localTest = u.nScheme == INTERNET_SCHEME_HTTP &&
                        (server == L"127.0.0.1" || server == L"localhost");
  if (u.nScheme != INTERNET_SCHEME_HTTPS && !localTest) {
    why = "update downloads require HTTPS";
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
  if (r) {
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
  }
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
      uint64_t bytes = 0;
      while (ok) {
        if (!WinHttpReadData(r, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
          why = "the connection ended before the download finished";
          ok = false;
          break;
        }
        if (!got) break;
        bytes += got;
        if ((expected && bytes > expected) || (!file && bytes > 1024 * 1024)) {
          why = "the response was larger than its permitted size";
          ok = false;
          break;
        }
        if (file) out.write(buf.data(), got);
        else body->append(buf.data(), got);
        ok = !file || out.good();
        if (ok && progress) progress(bytes);
      }
      ok = ok && (!file || out.good()) && (!expected || bytes == expected);
      if (!ok)
        why = why.empty() ? "couldn't save the complete download" : why;
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
    if (validSize)
      validSize = std::stoull(out.size) > 0 && std::stoull(out.size) <= 2ULL * 1024 * 1024 * 1024;
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
bool g_wakeNow = false, g_manual = false, g_started = false, g_downloadRequested = false;
Updater::State g_state = Updater::State::Off;
std::string g_status;
// The newer release downloaded and checked, waiting to be installed
bool g_ready = false, g_available = false, g_offered = false, g_installing = false;
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
  const uint64_t total = std::stoull(r.size);
  const auto began = std::chrono::steady_clock::now();
  if (!httpGet(r.url, nullptr, &part, why, total, [&](uint64_t bytes) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        std::lock_guard<std::mutex> lock(g_lock);
        g_downloaded = bytes;
        g_total = total;
        g_speed = elapsed >= 0.25 ? bytes / elapsed : 0;
      })) {
    fs::remove(part, ec);
    why = "the download failed: " + why;
    return false;
  }
  setState(Updater::State::Verifying, "Verifying download");
  if (!matches(part, r)) {
    fs::remove(part, ec);
    why = "the download didn't match its signed size and SHA-512, so it was thrown away";
    return false;
  }
  // Windows rename does not replace a stale/corrupted cached installer.
  // Remove it only after the newly downloaded part passed verification.
  fs::remove(target, ec);
  ec.clear();
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

// addition: a trusted release is offered before downloading. The player
// explicitly chooses Download/Update now; merely checking never installs.
bool lookOnce() {
  bool manual;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    manual = g_manual;
    g_manual = false;
  }
  setState(Updater::State::Checking, "Checking for Updates");
  Updater::Release release;
  if (!lookUp(release)) {
    setState(Updater::State::Failed, "Could not check for updates. Check your connection and try again.");
    return false;
  }
  if (!newer(release.version, ZT1_VERSION)) {
    setState(Updater::State::UpToDate, "All updated");
    return true;
  }
  if (!manual && skipped() == release.version) {
    setState(Updater::State::Idle, "Update skipped");
    return true;
  }
  // A cached installer is still validated before being offered as ready.
  const fs::path cached = pendingDir() / fs::path(release.file).filename();
  const bool ready = matches(cached, release);
  std::lock_guard<std::mutex> lock(g_lock);
  const bool changed = g_readyRelease.version != release.version;
  g_readyRelease = release;
  g_readyFile = ready ? cached : fs::path{};
  g_available = true;
  g_ready = ready;
  if (changed || manual) g_offered = false;
  g_downloaded = ready ? std::stoull(release.size) : 0;
  g_total = std::stoull(release.size);
  g_speed = 0;
  g_state = ready ? Updater::State::Ready : Updater::State::Available;
  g_status = ready ? "Ready to update" : "Update available";
  return true;
}

bool downloadSelected() {
  Updater::Release release;
  {
    std::lock_guard<std::mutex> lock(g_lock);
    g_downloadRequested = false;
    release = g_readyRelease;
  }
  setState(Updater::State::Downloading, "Downloading update");
  fs::path file;
  std::string why;
  if (!fetch(release, file, why)) {
    setState(Updater::State::Failed, why);
    return false;
  }
  std::lock_guard<std::mutex> lock(g_lock);
  g_ready = true;
  g_readyFile = file;
  g_downloaded = g_total;
  g_speed = 0;
  g_state = Updater::State::Ready;
  g_status = "Ready to update";
  return true;
}

// addition: a small separate updater stays visible after this app exits.
// It is copied outside the installation, so replacement cannot lock it.
std::wstring quoted(const std::wstring &value) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : value) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
    else result.append(slashes, L'\\');
    slashes = 0;
    result += c;
  }
  result.append(slashes * 2, L'\\');
  return result + L"\"";
}

bool runInstaller(const fs::path &setup, const Updater::Release &release, bool relaunch, std::string &why) {
  (void)relaunch; // explicit updates always reopen through the helper.
  if (!matches(setup, release)) {
    why = "The update no longer matches its signed size and SHA-512. It was not run.";
    return false;
  }
  const fs::path helperSource = fs::path(Paths::appDir()) / "zt1-update-helper.exe";
  const fs::path helper = pendingDir() / ("zt1-update-helper-" + std::to_string(GetCurrentProcessId()) + ".exe");
  std::error_code error;
  fs::copy_file(helperSource, helper, fs::copy_options::overwrite_existing, error);
  if (error) { why = "The update helper is missing or could not be prepared. Please reinstall the application."; return false; }
  wchar_t executable[32768]{};
  if (!GetModuleFileNameW(nullptr, executable, 32768)) { why = "Could not locate the game executable."; return false; }
  const std::wstring eventName = L"Local\\ZT1Update-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
  if (!ready) { why = "Could not create the update hand-off."; return false; }
  std::wstring command = quoted(helper.wstring()) + L" --app game --parent " + std::to_wstring(GetCurrentProcessId()) +
      L" --installer " + quoted(fs::absolute(setup).wstring()) + L" --size " + widen(release.size) +
      L" --sha512 " + quoted(widen(release.sha512)) + L" --launch " + quoted(executable) +
      L" --version " + quoted(widen(release.version)) + L" --ready-event " + quoted(eventName);
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  const bool launched = CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                      pendingDir().c_str(), &startup, &process) != FALSE;
  bool visible = false;
  if (launched) {
    HANDLE waits[]{ready, process.hProcess};
    visible = WaitForMultipleObjects(2, waits, FALSE, 10000) == WAIT_OBJECT_0;
    if (!visible) {
      // A failed hand-off must not leave a helper waiting to install when
      // the player later exits for an unrelated reason.
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 5000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  CloseHandle(ready);
  if (!visible) { why = "The updater window did not open. The game remains running; try again."; return false; }
  return true;
}

// addition: check at startup and when asked. Later means later: no
// background timer downloads or silent exit-time installation.
void loop() {
  std::unique_lock<std::mutex> lock(g_lock);
  bool first = true;
  for (;;) {
    if (!first) g_wake.wait(lock, [] { return g_wakeNow; });
    const bool download = g_downloadRequested;
    const bool check = g_manual || (first && Features::updateChecks);
    first = false;
    g_wakeNow = false;
    lock.unlock();
    if (download) downloadSelected();
    else if (check) lookOnce();
    else setState(Updater::State::Off, "Startup checks are off. Check now still works.");
    lock.lock();
  }
}
#endif

} // namespace

namespace Updater {

const char *version() { return ZT1_VERSION; }
bool official() { return ZT1_OFFICIAL_BUILD != 0; }
bool testBuild() { return ZT1_TEST_BUILD != 0; }

bool possible(std::string *why) {
  std::string w;
  // (a test release never updates: it's tried before the release is put up,
  // and the published copy is the one that updates)
  if (testBuild())
    w = "none in a test build: the published release is the one that updates";
  else if (!official())
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
  setState(Features::updateChecks ? State::Checking : State::Off,
           Features::updateChecks ? "Checking for Updates" : "Startup checks are off. Check now still works.");
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
  if (g_state == State::Downloading || g_state == State::Verifying || g_installing) return;
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
  if ((!g_available && !g_ready) || g_offered || g_installing ||
      (g_state != State::Available && g_state != State::Ready))
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
  if ((g_ready || g_available) && g_readyRelease.version == r.version) {
    g_ready = false;
    g_available = false;
    g_state = State::Idle;
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
  g_state = State::Installing;
  g_status = "Updating";
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
  // addition: never install because the player chose Later or closed a game.
}

Snapshot snapshot() {
  std::lock_guard<std::mutex> lock(g_lock);
  Snapshot value;
  value.state = g_state;
  value.release = g_readyRelease;
  value.downloadedBytes = g_downloaded;
  value.totalBytes = g_total;
  value.bytesPerSecond = g_speed;
  value.secondsRemaining = g_speed > 0 && g_total >= g_downloaded ? (g_total - g_downloaded) / g_speed : -1;
  value.status = g_status;
  return value;
}

bool downloadNow() {
  if (!possible(nullptr)) return false;
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_available || g_installing || g_downloadRequested ||
      (g_state != State::Available && g_state != State::Failed)) return false;
  g_downloadRequested = true;
  g_downloaded = 0;
  g_speed = 0;
  g_state = State::Downloading;
  g_status = "Downloading update";
  g_wakeNow = true;
  g_wake.notify_all();
  return true;
}

void acknowledgeLaunch(int argc, char **argv) {
#ifdef _WIN32
  std::string name, token;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--update-ack") name = argv[++i];
    else if (std::string(argv[i]) == "--update-token") token = argv[++i];
  }
  if (name.empty() || token.size() != 32 || !std::all_of(token.begin(), token.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) return;
  std::error_code error;
  const fs::path ack = fs::weakly_canonical(fs::u8path(name), error);
  if (error) return;
  const fs::path root = fs::weakly_canonical(pendingDir(), error);
  if (error || ack.parent_path() != root || ack.filename() != fs::path("launch-" + token + ".ack")) return;
  const fs::path temp = ack.wstring() + L".tmp";
  { std::ofstream out(temp, std::ios::binary | std::ios::trunc); out << token << "\n" << version() << "\n"; if (!out.good()) return; }
  fs::rename(temp, ack, error);
#else
  (void)argc; (void)argv;
#endif
}

int checkNowCli(bool installIt) {
#ifdef _WIN32
  // (a test release never reaches the real feeds - only a feed named for
  // trying the updater out, ZT1_UPDATE_FEED)
  if (testBuild() && !std::getenv("ZT1_UPDATE_FEED")) {
    SDL_Log("[Updater] a test release: no update feeds (ZT1_UPDATE_FEED names one to try)");
    return 3;
  }
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
