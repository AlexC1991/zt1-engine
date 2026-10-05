#include "Goals.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "IniReader.hpp"
#include "LangText.hpp"
#include "ResourceManager.hpp"

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
// (a key left empty, as "targb=", reads as 0)
int num(IniReader *ini, const std::string &sec, const std::string &key) {
  std::string v = trim(ini->get(sec, key));
  return v.empty() ? 0 : std::atoi(v.c_str());
}
} // namespace

void Goals::clear() {
  this->goals.clear();
  this->awards.clear();
  this->clock = 0;
}

void Goals::load(ResourceManager *rm, const std::string &scnPath) {
  this->clear();
  this->rm = rm;
  if (!rm)
    return;
  IniReader *scn = rm->getIniReader(scnPath);
  if (!scn)
    return;
  // The map's own goals, then each extra file's
  this->readGoals(scnPath, true);
  for (const std::string &extra : scn->getList("start", "extragoals"))
    this->readGoals(trim(extra), true);
  // [start] triggers=: each section's trigger fired once, at once (not kept)
  for (const std::string &t : scn->getList("start", "triggers")) {
    Goal g;
    g.file = scnPath;
    std::string sec = trim(t);
    g.trulea = num(scn, sec, "trulea");
    g.truleb = num(scn, sec, "truleb");
    g.targa = num(scn, sec, "targa");
    g.targb = num(scn, sec, "targb");
    this->fire(g, false);
  }
  delete scn;
}

void Goals::readGoals(const std::string &file, bool fromList) {
  IniReader *ini = this->rm->getIniReader(file);
  if (!ini)
    return;
  (void)fromList;
  for (const std::string &name : ini->getList("goals", "goal")) {
    std::string sec = trim(name);
    // (only the normal goal class, rtype 0)
    if (num(ini, sec, "rtype") != 0)
      continue;
    Goal g;
    g.name = sec;
    g.file = file;
    g.rulea = num(ini, sec, "rulea");
    g.ruleb = num(ini, sec, "ruleb");
    g.type = num(ini, sec, "type");
    g.value = num(ini, sec, "value");
    g.arga = num(ini, sec, "arga");
    g.argb = num(ini, sec, "argb");
    g.sticky = num(ini, sec, "sticky") != 0;
    g.optional = num(ini, sec, "optional") != 0;
    g.text = num(ini, sec, "text");
    g.hidden = num(ini, sec, "hidden") != 0 || !g.text;
    g.trulea = num(ini, sec, "trulea");
    g.truleb = num(ini, sec, "truleb");
    g.targa = num(ini, sec, "targa");
    g.targb = num(ini, sec, "targb");
    this->goals.push_back(g);
  }
  delete ini;
}

// Checked every 1500 ms of the world's ticks (zoo.exe 0x435a48; not while
// paused)
void Goals::update(float seconds) {
  this->clock += seconds * 1000.0f;
  if (this->clock < 1500.0f)
    return;
  this->clock = 0;
  this->evaluateAll();
}

void Goals::evaluateAll() {
  for (Goal &g : this->goals)
    this->evaluate(g);
}

// zoo.exe 0x4240f5: types 0-2 met when v >= / < / == value; 3-5 failed when
// so; 6-8 failed when v < / >= / != value, else met. A change to a new
// state fires the trigger; back to 0 resets quietly. Sticky: once only.
void Goals::evaluate(Goal &g) {
  if (g.sticky && g.state != 0)
    return;
  if (!this->hooks.measure)
    return;
  int v = this->hooks.measure(g);
  int result = 0;
  switch (g.type) {
  case 0: result = v >= g.value ? 1 : 0; break;
  case 1: result = v < g.value ? 1 : 0; break;
  case 2: result = v == g.value ? 1 : 0; break;
  case 3: result = v >= g.value ? 2 : 0; break;
  case 4: result = v < g.value ? 2 : 0; break;
  case 5: result = v == g.value ? 2 : 0; break;
  case 6: result = v < g.value ? 2 : 1; break;
  case 7: result = v >= g.value ? 2 : 1; break;
  case 8: result = v != g.value ? 2 : 1; break;
  default: break;
  }
  if (result == 0) {
    g.state = 0;
    return;
  }
  if (result != g.state) {
    g.state = result;
    this->fire(g, false);
  }
}

void Goals::restore(const std::vector<int> &states) {
  for (size_t i = 0; i < states.size() && i < this->goals.size(); i++) {
    this->goals[i].state = states[i];
    if (states[i] != 0)
      this->fire(this->goals[i], true);
  }
}

// A popup section of the goal's file, read over its [default]: the picture
// (image1) and text (longText1: a string id, or a lang0.dll TEXT name such
// as AWARD_AWARD1, whose words the map folder's award1.txt holds)
void Goals::showPopup(const std::string &file, int section) {
  if (!this->hooks.popup || !this->rm)
    return;
  IniReader *ini = this->rm->getIniReader(file);
  if (!ini)
    return;
  std::string sec = std::to_string(section);
  auto key = [&](const std::string &k) {
    std::string v = trim(ini->get(sec, k));
    return v.empty() ? trim(ini->get("default", k)) : v;
  };
  std::string image = key("image1"), longText = key("longtext1");
  bool pause = std::atoi(key("pause").c_str()) != 0;
  delete ini;
  std::string text;
  if (!longText.empty() && std::all_of(longText.begin(), longText.end(), ::isdigit)) {
    text = this->rm->getString(static_cast<uint32_t>(std::atoi(longText.c_str())));
  } else if (!longText.empty() && !(text = langText(longText)).empty()) {
    // (lang0.dll's TEXT resource of that name)
  } else if (!longText.empty()) {
    std::string dir = file.substr(0, file.find_last_of('/') + 1);
    size_t us = longText.find('_');
    std::string name = lower(us == std::string::npos ? longText : longText.substr(us + 1));
    int size = 0;
    if (void *bytes = this->rm->getFileBytes(dir + name + ".txt", &size)) {
      text.assign(static_cast<const char *>(bytes), static_cast<size_t>(size));
      free(bytes);
      text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
      text = trim(text);
    }
  }
  if (!text.empty())
    this->hooks.popup(image, text, pause);
}

// zoo.exe 0x4fc783. quiet: a saved game's states put back (no popups, no
// cash, no messages)
void Goals::fire(const Goal &g, bool quiet) {
  switch (g.trulea) {
  case 1:
    if (!quiet && this->hooks.message && g.targa > 0)
      this->hooks.message(g.targa);
    break;
  case 2:
    switch (g.truleb) {
    case 0:
      if (!quiet)
        this->showPopup(g.file, g.targa);
      break;
    case 3:
    case 4:
      if (this->hooks.disable && this->hooks.disable(g.targa, true) && !quiet && g.targb > 0)
        this->showPopup(g.file, g.targb);
      break;
    case 5:
    case 6:
      if (this->hooks.disable && this->hooks.disable(g.targa, false) && !quiet && g.targb > 0)
        this->showPopup(g.file, g.targb);
      break;
    case 7:
    case 8:
      if (this->hooks.hide)
        this->hooks.hide(g.targa, true);
      break;
    case 9:
    case 10:
      if (this->hooks.hide)
        this->hooks.hide(g.targa, false);
      break;
    case 12: // the same for each id= of section [targa]: disabled,
    case 13: // enabled, hidden, shown, or 16 a mix (d=, e=, h=, s=)
    case 14:
    case 15:
    case 16: {
      IniReader *ini = this->rm ? this->rm->getIniReader(g.file) : nullptr;
      if (!ini)
        break;
      std::string sec = std::to_string(g.targa);
      bool changed = false;
      auto apply = [&](const std::string &key, int action) {
        for (const std::string &v : ini->getList(sec, key)) {
          int id = std::atoi(trim(v).c_str());
          if (action <= 1 && this->hooks.disable)
            changed = this->hooks.disable(id, action == 0) || changed;
          else if (this->hooks.hide)
            this->hooks.hide(id, action == 2);
        }
      };
      if (g.truleb == 16) {
        apply("d", 0);
        apply("e", 1);
        apply("h", 2);
        apply("s", 3);
      } else {
        apply("id", g.truleb - 12);
      }
      delete ini;
      if (changed && !quiet && g.targb > 0 && g.truleb <= 13)
        this->showPopup(g.file, g.targb);
      break;
    }
    default:
      break;
    }
    break;
  case 3:
    if (!quiet && this->hooks.cash) {
      this->hooks.cash(g.targa, g.truleb);
      if (g.targb > 0 && this->hooks.message)
        this->hooks.message(g.targb);
    }
    break;
  case 6:
    if (std::find(this->awards.begin(), this->awards.end(), g.targa) == this->awards.end()) {
      this->awards.push_back(g.targa);
      if (this->hooks.award)
        this->hooks.award(g.targa);
    }
    break;
  // (4, 7: what can be bought - the catalogue's timed unlocks, ItemCatalog)
  default:
    break;
  }
}
