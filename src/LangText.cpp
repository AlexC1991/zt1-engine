#include "LangText.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <vector>

#include "Utils.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

std::string langText(const std::string &name) {
  static std::map<std::string, std::string> cache;
  auto it = cache.find(name);
  if (it != cache.end())
    return it->second;
  std::string text;
#ifdef _WIN32
  std::vector<std::string> dlls;
  try {
    for (const auto &e : std::filesystem::directory_iterator(Utils::getExecutableDirectory())) {
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
  cache[name] = text;
  return text;
}
