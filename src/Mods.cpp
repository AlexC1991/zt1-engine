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
#include "Settings.hpp"
#include "Utils.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

// (zoo.ini's section for a mod's settings)
std::string section(const std::string &folder) { return "Mod:" + folder; }

// A mod.json's "settings", each with the value kept for it
std::vector<Mods::Setting> settingsOf(const std::string &folder, const Json &list) {
  std::vector<Mods::Setting> out;
  for (size_t i = 0; i < list.size(); i++) {
    const Json &v = list[i];
    Mods::Setting t;
    t.name = v["name"].str();
    if (t.name.empty())
      continue;
    t.label = v["label"].str(t.name);
    t.about = v["about"].str();
    t.type = v["type"].str("slider");
    if (t.type == "toggle") {
      t.min = 0;
      t.max = 1;
      t.standard = v["value"].kind == Json::Kind::Number ? (v["value"].num() != 0 ? 1.0f : 0.0f)
                                                         : (v["value"].truthy() ? 1.0f : 0.0f);
    } else if (t.type == "choice") {
      for (size_t c = 0; c < v["choices"].size(); c++)
        t.choices.push_back(v["choices"][c].str());
      if (t.choices.empty())
        continue;
      t.min = 0;
      t.max = static_cast<float>(t.choices.size() - 1);
      std::string pick = v["value"].str();
      t.standard = static_cast<float>(v["value"].num(0));
      for (size_t c = 0; c < t.choices.size(); c++)
        if (!pick.empty() && t.choices[c] == pick)
          t.standard = static_cast<float>(c);
    } else {
      t.type = "slider";
      t.min = static_cast<float>(v["min"].num(0));
      t.max = static_cast<float>(v["max"].num(1));
      t.standard = static_cast<float>(v["value"].num(t.min));
    }
    t.standard = std::clamp(t.standard, std::min(t.min, t.max), std::max(t.min, t.max));
    std::string kept = Settings::get(section(folder), t.name, "");
    t.value = kept.empty() ? t.standard
                           : std::clamp(static_cast<float>(std::atof(kept.c_str())), std::min(t.min, t.max),
                                        std::max(t.min, t.max));
    if (t.type != "slider")
      t.value = std::round(t.value);
    out.push_back(t);
  }
  return out;
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
  // The player's mods folder, then the mods that come with the game (a mods
  // folder beside the exe: the toon shaders) - one of the player's own by the
  // same folder name wins
  std::vector<fs::path> roots = {fs::path(folder())};
  fs::path bundled = fs::path(Paths::appDir()) / "mods";
  if (!fs::equivalent(bundled, roots[0], ec))
    roots.push_back(bundled);
  std::set<std::string> seen;
  for (const fs::path &root : roots) {
  if (!fs::is_directory(root, ec))
    continue;
  for (const fs::directory_entry &d : fs::directory_iterator(root, ec)) {
    std::string key = d.path().filename().string();
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    // (a hidden folder - ".work", the Lab's HD art scratch - isn't a mod)
    if (!d.is_directory(ec) || key.empty() || key[0] == '.' || !seen.insert(key).second)
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
      // (a picture named outside its folder isn't its own: left out)
      std::string pic = j["picture"].str();
      if (!pic.empty() && pic.find("..") == std::string::npos && std::filesystem::is_regular_file(d.path() / pic))
        m.picture = (d.path() / pic).string();
      for (size_t i = 0; i < j["replaces"].size(); i++)
        m.replaces.push_back(j["replaces"][i].str());
      m.settings = settingsOf(m.folder, j["settings"]);
    }
    m.enabled = c.enabled.count(m.folder) > 0;
    out.push_back(m);
  }
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

void setSetting(const std::string &mod, const std::string &name, float value) {
  char b[32];
  std::snprintf(b, sizeof b, "%g", value);
  Settings::set(section(mod), name, b);
}

float setting(const std::string &mod, const std::string &name, float fallback) {
  for (const Mod &m : list())
    if (m.folder == mod)
      for (const Setting &t : m.settings)
        if (t.name == name)
          return t.value;
  return fallback;
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
