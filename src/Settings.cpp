#include "Settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

#include "Features.hpp"
#include "IniReader.hpp"
#include "Paths.hpp"
#include "Sound.hpp"
#include "World.hpp"

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r");
  size_t b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

} // namespace

namespace Settings {

std::string get(const std::string &section, const std::string &key, const std::string &fallback) {
  IniReader ini(Paths::configFile());
  return ini.get(section, key, fallback);
}

int getInt(const std::string &section, const std::string &key, int fallback) {
  std::string v = get(section, key, "");
  if (v.empty())
    return fallback;
  try {
    return std::stoi(v);
  } catch (...) {
    return fallback;
  }
}

// ours: the key's line replaced (or added at the end of its section, or the
// section added), every other line kept as it was
void set(const std::string &section, const std::string &key, const std::string &value) {
  std::vector<std::string> lines;
  {
    std::ifstream in(Paths::configFile(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      lines.push_back(line);
    }
  }
  const std::string sec = lower(section), k = lower(key);
  int sectionAt = -1, insertAt = -1;
  bool done = false;
  for (size_t i = 0; i < lines.size() && !done; i++) {
    std::string t = trim(lines[i]);
    if (!t.empty() && t.front() == '[') {
      if (sectionAt >= 0) {
        insertAt = static_cast<int>(i);
        break;
      }
      if (lower(t) == "[" + sec + "]")
        sectionAt = static_cast<int>(i);
      continue;
    }
    if (sectionAt < 0)
      continue;
    size_t eq = t.find('=');
    if (eq != std::string::npos && lower(trim(t.substr(0, eq))) == k) {
      lines[i] = key + "=" + value;
      done = true;
    }
  }
  if (!done) {
    if (sectionAt < 0) {
      if (!lines.empty() && !trim(lines.back()).empty())
        lines.push_back("");
      lines.push_back("[" + section + "]");
      lines.push_back(key + "=" + value);
    } else {
      // (after the section's last line that isn't blank)
      int at = insertAt < 0 ? static_cast<int>(lines.size()) : insertAt;
      while (at > sectionAt + 1 && trim(lines[at - 1]).empty())
        at--;
      lines.insert(lines.begin() + at, key + "=" + value);
    }
  }
  std::ofstream out(Paths::configFile(), std::ios::binary | std::ios::trunc);
  for (const std::string &l : lines)
    out << l << "\r\n";
}

void setInt(const std::string &section, const std::string &key, int value) { set(section, key, std::to_string(value)); }

void loadExtras() {
  IniReader ini(Paths::configFile());
  for (Features::Switch &f : Features::all()) {
    std::string v = lower(ini.get("Extras", f.name, ""));
    if (v == "1" || v == "on" || v == "true")
      *f.value = true;
    else if (v == "0" || v == "off" || v == "false")
      *f.value = false;
  }
}

void saveExtra(const std::string &name, bool on) { set("Extras", name, on ? "1" : "0"); }

// The Game Options' settings at work, as the original sets them up
// (zoo.exe 0x518375, the panel's refresh, reads them all with these
// defaults)
void apply() {
  // Main volume: [UI] userAttenuation, hundredths of a decibel down (0 by
  // default; 5000, silence, with the slider at 0 - 0x58b98d)
  Sound::get().master = std::clamp(getInt("UI", "userAttenuation", 0), 0, 5000);
  // The menu's music: [UI] menuMusicAttenuation, -1000 by default, its
  // slider's top menuMusicMaxAttenuation (-1000: createGameMgr 0x527dc7,
  // gameMgr+0x11a8), 50 more for each step down (0x47ff4d). measured: read
  // as hundredths of a decibel down, as the main volume - the top (-1000,
  // and below) full volume, the bottom 40 dB down
  int music = std::clamp(getInt("UI", "menuMusicAttenuation", -1000), 0, 10000);
  Mix_VolumeMusic(std::clamp(static_cast<int>(std::lround(std::pow(10.0, -music / 2000.0) * MIX_MAX_VOLUME)), 0,
                             MIX_MAX_VOLUME));
  // Scrolling at the window's edge: [UI] mouseScrollX, 32 by default
  // (0x51a33c)
  World::kEdgeScrollPixels = static_cast<float>(std::clamp(getInt("UI", "mouseScrollX", 32), 8, 128));
}

} // namespace Settings
