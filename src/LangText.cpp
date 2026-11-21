#include "LangText.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <vector>

#include "Utils.hpp"
#include "Paths.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

// ours: the DLLs' texts are Windows-1252 (an apostrophe is 0x92); the
// font takes UTF-8 - turned once here, for every caller (plaques, help,
// scenario popups and stories)
std::string windows1252ToUtf8(const std::string &in) {
  static const unsigned short high[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                          0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                          0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                          0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
  std::string out;
  for (unsigned char c : in) {
    unsigned cp = c < 0x80 || c >= 0xA0 ? c : high[c - 0x80];
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

std::string langText(const std::string &name) {
  static std::map<std::string, std::string> cache;
  auto it = cache.find(name);
  if (it != cache.end())
    return it->second;
  std::string text;
#ifdef _WIN32
  std::vector<std::string> dlls;
  try {
    for (const auto &e : std::filesystem::directory_iterator(Paths::gameDir())) {
      std::string f = Utils::string_to_lower(e.path().filename().string());
      if (f.rfind("lang", 0) == 0 && f.size() > 4 && f.substr(f.size() - 4) == ".dll")
        dlls.push_back(e.path().string());
    }
  } catch (...) {
  }
  std::sort(dlls.rbegin(), dlls.rend());
  for (const std::string &dll : dlls) {
    HMODULE h = LoadLibraryExA(dll.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!h)
      continue;
    if (HRSRC r = FindResourceA(h, name.c_str(), "TEXT"))
      if (HGLOBAL g = LoadResource(h, r))
        if (const char *p = static_cast<const char *>(LockResource(g))) {
          DWORD size = SizeofResource(h, r);
          text.assign(p, p + size);
          // (up to its end: some carry a trailing nul)
          size_t nul = text.find('\0');
          if (nul != std::string::npos)
            text.resize(nul);
          text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        }
    FreeLibrary(h);
    if (!text.empty())
      break;
  }
#endif
  text = windows1252ToUtf8(text);
  cache[name] = text;
  return text;
}
