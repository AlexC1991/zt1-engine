#include "Mods.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "Features.hpp"
#include "MiniJson.hpp"
#include "Paths.hpp"
#include "Utils.hpp"

namespace fs = std::filesystem;

namespace {
// mods.cfg, by the game: "folder=<path>" and an "enabled=<mod folder>" line
// for each mod switched on
// (in the engine's data folder: beside the exe when portable,
// %LocalAppData%\zt1-engine when installed - Paths.hpp)
std::string cfgPath() { return (fs::path(Paths::dataDir()) / "mods.cfg").string(); }

struct Cfg {
  std::string folder;
  std::set<std::string> enabled;
};

Cfg readCfg() {
  Cfg c;
  std::ifstream in(cfgPath());
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "folder")
      c.folder = v;
    else if (k == "enabled" && !v.empty())
      c.enabled.insert(v);
  }
  return c;
}

void writeCfg(const Cfg &c) {
  std::ofstream out(cfgPath(), std::ios::trunc);
  if (!c.folder.empty())
    out << "folder=" << c.folder << "\n";
  for (const std::string &e : c.enabled)
    out << "enabled=" << e << "\n";
}
} // namespace

namespace Mods {
std::string folder() {
  Cfg c = readCfg();
  if (!c.folder.empty())
    return c.folder;
  return (fs::path(Paths::dataDir()) / "mods").string();
}

void setFolder(const std::string &path) {
  Cfg c = readCfg();
  c.folder = path;
  writeCfg(c);
}

std::vector<Mod> list() {
  std::vector<Mod> out;
  Cfg c = readCfg();
  std::error_code ec;
  fs::path root = folder();
  if (!fs::is_directory(root, ec))
    return out;
  for (const fs::directory_entry &d : fs::directory_iterator(root, ec)) {
    if (!d.is_directory(ec))
      continue;
    Mod m;
    m.folder = d.path().filename().string();
    m.path = d.path().string();
    m.name = m.folder;
    std::ifstream in(d.path() / "mod.json");
    if (in) {
      std::stringstream ss;
      ss << in.rdbuf();
      Json j = Json::parse(ss.str());
      m.name = j["name"].str(m.folder);
      m.author = j["author"].str();
      m.version = j["version"].str();
      m.about = j["about"].str();
      m.preview = j["preview"].str();
      m.palette = j["palette"].str();
      m.originalPalette = j["originalPalette"].str();
      for (size_t i = 0; i < j["replaces"].size(); i++)
        m.replaces.push_back(j["replaces"][i].str());
    }
    m.enabled = c.enabled.count(m.folder) > 0;
    out.push_back(m);
  }
  std::sort(out.begin(), out.end(), [](const Mod &a, const Mod &b) { return a.name < b.name; });
  return out;
}

void setEnabled(const std::string &mod, bool on) {
  Cfg c = readCfg();
  if (on)
    c.enabled.insert(mod);
  else
    c.enabled.erase(mod);
  writeCfg(c);
}

std::vector<std::string> enabledPaths() {
  std::vector<std::string> out;
  if (!Features::mods)
    return out;
  for (const Mod &m : list())
    if (m.enabled)
      out.push_back(m.path);
  return out;
}
} // namespace Mods
