#include "ModImport.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "Mods.hpp"
#include "ZtdFile.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ModImport {

namespace {
// (C++20's path::u8string() is a std::u8string: "p.u8string() | u8" makes it
// the std::string the rest of the engine uses, UTF-8 as it was)
struct U8 {};
constexpr U8 u8{};
std::string operator|(const std::u8string &s, U8) { return std::string(s.begin(), s.end()); }

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// A JSON string's text
std::string quoted(const std::string &s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\')
      o += '\\';
    if (c == '\n')
      o += "\\n";
    else if (static_cast<unsigned char>(c) >= 0x20)
      o += c;
  }
  return o + "\"";
}

// "asian_black_bear" -> "Asian Black Bear"
std::string niceName(std::string stem) {
  std::replace(stem.begin(), stem.end(), '_', ' ');
  std::replace(stem.begin(), stem.end(), '-', ' ');
  bool start = true;
  for (char &c : stem) {
    if (start && std::isalpha(static_cast<unsigned char>(c)))
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    start = c == ' ';
  }
  return stem;
}

// A folder name of its own in the mods folder
fs::path freeFolder(const std::string &name) {
  std::string safe;
  for (char c : name)
    safe += std::string("<>:\"/\\|?*").find(c) == std::string::npos && static_cast<unsigned char>(c) >= 0x20 ? c : '_';
  while (!safe.empty() && (safe.back() == '.' || safe.back() == ' '))
    safe.pop_back();
  if (safe.empty() || safe[0] == '.')
    safe = "Imported mod";
  fs::path base = fs::path(Mods::folder()) / fs::u8path(safe), p = base;
  std::error_code ec;
  for (int n = 2; fs::exists(p, ec); n++)
    p = base.string() + " (" + std::to_string(n) + ")";
  return p;
}

// Every file under a folder that a mod may hold (the first that isn't, if
// one isn't)
bool allAllowed(const fs::path &dir, std::string &bad) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_symlink(ec)) {
      bad = it->path().filename().string();
      return false;
    }
    if (!it->is_regular_file(ec))
      continue;
    std::string name = it->path().filename().string();
    if (!allowed(name)) {
      bad = name;
      return false;
    }
    if (lower(it->path().extension().string()) == ".ztd")
      for (const std::string &inside : ZtdFile::getFileList(it->path().string()))
        if (!allowed(inside.substr(inside.find_last_of('/') + 1))) {
          bad = name + ": " + inside;
          return false;
        }
  }
  return true;
}

void writeModJson(const fs::path &folder, const std::string &name, const std::string &about,
                  const std::vector<std::string> &adds) {
  std::ofstream out(folder / "mod.json", std::ios::binary);
  out << "{\n  \"name\": " << quoted(name) << ",\n  \"author\": \"\",\n  \"version\": \"\",\n  \"about\": "
      << quoted(about) << ",\n  \"imported\": true,\n  \"replaces\": [";
  for (size_t i = 0; i < adds.size(); i++)
    out << (i ? ", " : "") << quoted(adds[i]);
  out << "]\n}\n";
}

// Old .ztds made into one mod: their own folder, the .ztds, a mod.json
Result wrapZtds(const std::vector<fs::path> &ztds, const std::string &name, const std::string &from) {
  Result r;
  fs::path dest = freeFolder(name);
  std::error_code ec;
  fs::create_directories(dest, ec);
  std::vector<std::string> all;
  for (const fs::path &z : ztds) {
    fs::copy_file(z, dest / z.filename(), fs::copy_options::overwrite_existing, ec);
    for (const std::string &a : adds(z.string()))
      all.push_back(a);
  }
  std::sort(all.begin(), all.end());
  all.erase(std::unique(all.begin(), all.end()), all.end());
  std::string about = "An old Zoo Tycoon download (" + from + "), added here.";
  if (!all.empty())
    about += all.size() == 1 ? " It adds " + all[0] + "." : " It adds " + std::to_string(all.size()) + " things (below).";
  writeModJson(dest, name, about, all);
  r.ok = true;
  r.folder = dest.filename().u8string() | u8;
  r.name = name;
  r.message = name + " is in your mods, switched off: switch it on, then Restart.";
  return r;
}

// A mod's folder copied in
Result copyMod(const fs::path &src) {
  Result r;
  std::string bad;
  if (!allAllowed(src, bad)) {
    r.message = "Not added: a mod can hold only game data, and it has " + bad + ".";
    return r;
  }
  std::ifstream in(src / "mod.json");
  std::stringstream ss;
  ss << in.rdbuf();
  std::string json = ss.str(), name = src.filename().u8string() | u8;
  // (its own name, if it says: "name": "...")
  size_t k = json.find("\"name\"");
  if (k != std::string::npos) {
    size_t a = json.find('"', json.find(':', k) + 1), b = a == std::string::npos ? a : json.find('"', a + 1);
    if (a != std::string::npos && b != std::string::npos && b > a + 1)
      name = json.substr(a + 1, b - a - 1);
  }
  fs::path dest = freeFolder(src.filename().u8string() | u8);
  std::error_code ec;
  fs::copy(src, dest, fs::copy_options::recursive, ec);
  if (ec) {
    r.message = "Not added: it couldn't be copied (" + ec.message() + ").";
    return r;
  }
  r.ok = true;
  r.folder = dest.filename().u8string() | u8;
  r.name = name;
  r.message = name + " is in your mods, switched off: switch it on, then Restart.";
  return r;
}

// A zip unpacked (Windows' own tar, which won't write outside the folder)
bool unzip(const fs::path &zip, const fs::path &into) {
#ifdef _WIN32
  const char *root = std::getenv("SystemRoot");
  std::string cmd = "\"" + (fs::path(root ? root : "C:\\Windows") / "System32" / "tar.exe").string() + "\" -xf \"" +
                    zip.string() + "\" -C \"" + into.string() + "\"";
  STARTUPINFOA si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  std::vector<char> line(cmd.begin(), cmd.end());
  line.push_back(0);
  if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return false;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
#else
  (void)zip;
  (void)into;
  return false;
#endif
}
} // namespace

bool allowed(const std::string &fileName) {
  static const std::set<std::string> ok = {
      ".ztd", ".ai",  ".ani", ".pal", ".tga",  ".bmp", ".png", ".webp", ".jpg", ".jpeg", ".wav", ".ogg",
      ".mp3", ".json", ".ini", ".txt", ".md",  ".frag", ".lyt", ".uca", ".ucs", ".ucb", ".scn", ".zoo",
      ".zt1map", ".lng", ".cfg", ".tgs", ".rtf", ".htm", ".html"};
  std::string ext = lower(fs::u8path(fileName).extension().string());
  // (a frame of animation has no extension at all: animals/yeti/m/walk/N)
  return ext.empty() || ok.count(ext) > 0;
}

std::vector<std::string> adds(const std::string &ztd) {
  std::vector<std::string> out;
  std::set<std::string> folders;
  for (const std::string &f : ZtdFile::getFileList(ztd)) {
    std::string l = lower(f);
    if (lower(fs::u8path(f).extension().string()) == ".ai")
      out.push_back(l);
    // (its folders two deep - animals/b101b026 - for a download of art
    // alone, whose .ai came in another)
    size_t a = l.find('/'), b = a == std::string::npos ? a : l.find('/', a + 1);
    if (b != std::string::npos)
      folders.insert(l.substr(0, b));
  }
  if (out.empty())
    out.assign(folders.begin(), folders.end());
  std::sort(out.begin(), out.end());
  return out;
}

Result import(const std::string &pathText) {
  Result r;
  fs::path path = fs::u8path(pathText);
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    r.message = "Not added: there's nothing at " + pathText + ".";
    return r;
  }
  fs::create_directories(Mods::folder(), ec);
  std::string ext = lower(path.extension().string());

  // A mod's folder (or its mod.json)
  if (fs::is_directory(path, ec) || lower(path.filename().string()) == "mod.json") {
    fs::path dir = fs::is_directory(path, ec) ? path : path.parent_path();
    if (fs::exists(dir / "mod.json", ec))
      return copyMod(dir);
    std::vector<fs::path> ztds;
    for (const auto &e : fs::directory_iterator(dir, ec))
      if (lower(e.path().extension().string()) == ".ztd")
        ztds.push_back(e.path());
    if (ztds.empty()) {
      r.message = "Not added: that folder has no mod.json and no .ztd.";
      return r;
    }
    return wrapZtds(ztds, niceName(dir.filename().u8string() | u8), dir.filename().u8string() | u8);
  }

  // An old download: a .ztd
  if (ext == ".ztd") {
    std::string bad;
    for (const std::string &inside : ZtdFile::getFileList(path.string()))
      if (!allowed(inside.substr(inside.find_last_of('/') + 1))) {
        r.message = "Not added: a mod can hold only game data, and it has " + inside + ".";
        return r;
      }
    return wrapZtds({path}, niceName(path.stem().u8string() | u8), path.filename().u8string() | u8);
  }

  // A zip: a mod as it is, or old .ztds
  if (ext == ".zip") {
    fs::path work = fs::path(Mods::folder()) / ".import";
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    if (!unzip(path, work)) {
      fs::remove_all(work, ec);
      r.message = "Not added: the zip couldn't be opened.";
      return r;
    }
    std::string bad;
    if (!allAllowed(work, bad)) {
      fs::remove_all(work, ec);
      r.message = "Not added: a mod can hold only game data, and it has " + bad + ".";
      return r;
    }
    // (a mod.json at the top, or one folder down)
    fs::path mod;
    if (fs::exists(work / "mod.json", ec))
      mod = work;
    else
      for (const auto &e : fs::directory_iterator(work, ec))
        if (e.is_directory(ec) && fs::exists(e.path() / "mod.json", ec))
          mod = e.path();
    if (!mod.empty()) {
      if (mod == work) {
        fs::path named = fs::path(Mods::folder()) / ".import-named" / path.stem();
        fs::create_directories(named.parent_path(), ec);
        fs::rename(work, named, ec);
        r = copyMod(named);
        fs::remove_all(named.parent_path(), ec);
      } else {
        r = copyMod(mod);
      }
      fs::remove_all(work, ec);
      return r;
    }
    std::vector<fs::path> ztds;
    for (auto it = fs::recursive_directory_iterator(work, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
      if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".ztd")
        ztds.push_back(it->path());
    if (ztds.empty()) {
      fs::remove_all(work, ec);
      r.message = "Not added: the zip has no mod.json and no .ztd.";
      return r;
    }
    r = wrapZtds(ztds, niceName(path.stem().u8string() | u8), path.filename().u8string() | u8);
    fs::remove_all(work, ec);
    return r;
  }

  r.message = "Not added: pick a .ztd, a .zip, or a mod's mod.json.";
  return r;
}

} // namespace ModImport
