#include "Updater.hpp"

#include <algorithm>
#include <atomic>
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

// GitHub is the canonical published release, and the only feed for now.
// CodeRook mirrors each release, but its update feed isn't reliable yet (its
// publisher promotes a version before every file is attached), so the game
// doesn't update from it; put it back here once it is.
struct Feed {
  const char *name, *base;
};
const Feed kFeeds[] = {
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
  std::error_code sizeError;
  const auto actualSize = fs::file_size(setup, sizeError);
  if (sizeError || std::to_string(actualSize) != r.size || sha512Base64(setup) != r.sha512) {
    std::error_code ec;
    fs::remove(setup, ec);
    why = "The download didn't match its signed size and SHA-512, so it wasn't run.";
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
