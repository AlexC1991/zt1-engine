#include "Goals.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>

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
// A normal goal's section
Goals::Goal readGoal(IniReader *ini, const std::string &file, const std::string &sec) {
  Goals::Goal g;
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
  return g;
}
// The files an extragoals= entry names (goalsLoad 0x58de8a): split at its
// '.', every resource starting with the part before and ending with the
// part after (0x4b41f2), so freeform/unlock.scn brings unlock01..unlock20
std::vector<std::string> extraFiles(ResourceManager *rm, const std::string &entry) {
  std::string extra = lower(trim(entry));
  size_t dot = extra.rfind('.');
  if (dot == std::string::npos)
    return {};
  std::vector<std::string> found = rm->listResources(extra.substr(0, dot), extra.substr(dot));
  std::sort(found.begin(), found.end());
  return found;
}
} // namespace

void Goals::clear() {
  this->goals.clear();
  this->roots.clear();
  this->durationMonths = 0;
  this->durationState = 1;
  this->outcome = 0;
  this->scnPath.clear();
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
  this->scnPath = scnPath;
  // The scenario's own goal first: its time limit (ScenarioGoal::ctor
  // 0x58e218: [duration] nummonths; none, 0, is met at once)
  this->durationMonths = num(scn, "duration", "nummonths");
  this->durationState = this->durationMonths == 0 ? 1 : 0;
  // The map's own goals, then each extragoals= entry's files
  this->readGoals(scnPath, true);
  for (const std::string &extra : scn->getList("start", "extragoals"))
    for (const std::string &file : extraFiles(rm, extra))
      this->readGoals(file, lower(file) == lower(trim(extra)));
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

// named: a file the [start] list names outright (our older saves kept only
// its goals)
void Goals::readGoals(const std::string &file, bool named) {
  IniReader *ini = this->rm->getIniReader(file);
  if (!ini)
    return;
  for (const std::string &name : ini->getList("goals", "goal")) {
    const size_t first = this->goals.size();
    this->roots.push_back(this->makeNode(ini, file, trim(name), 0));
    if (named && this->roots.back().kind == 0 && num(ini, trim(name), "rtype") == 0)
      this->goals[first].legacy = true;
  }
  delete ini;
}

// makeGoal 0x58e066: rtype 1 a goal group of all its child= goals
// (goalGroupCtor 0x59364c: "optional", each "child" made the same way),
// rtype 2 the same in order (0x593793); anything else a normal goal
Goals::Node Goals::makeNode(IniReader *ini, const std::string &file, const std::string &sec, int depth) {
  Node n;
  n.file = file;
  n.name = sec;
  n.optional = num(ini, sec, "optional") != 0;
  const int rtype = num(ini, sec, "rtype");
  if (rtype == 1 || rtype == 2) {
    n.kind = rtype;
    if (depth < 16)
      for (const std::string &child : ini->getList(sec, "child"))
        n.children.push_back(this->makeNode(ini, file, trim(child), depth + 1));
    return n;
  }
  Goal g = readGoal(ini, file, sec);
  g.depth = depth;
  n.goal = static_cast<int>(this->goals.size());
  this->goals.push_back(g);
  return n;
}

// A node's update (its class's slot 4): a goal checked (goalCheck 0x4240f5);
// all of (0x41de4d): each child updated, any failed (2) fails it, else any
// not yet met (0) leaves it at 0, else met; in order (0x41ddf1): each child
// updated until one isn't met, whose state it takes (the rest wait)
void Goals::updateNode(Node &n) {
  if (n.kind == 0) {
    this->evaluate(this->goals[n.goal]);
    return;
  }
  int r = 1;
  for (Node &c : n.children) {
    this->updateNode(c);
    const int s = this->nodeState(c);
    if (n.kind == 1) {
      if (s == 2)
        r = 2;
      else if (s == 0 && r != 2)
        r = 0;
    } else {
      r = s;
      if (s != 1)
        break;
    }
  }
  n.state = r >= 0 && r < 4 ? r : 0; // (setState 0x41ddda: 0..3)
}

// A node's state (slot 8: 0x41d754, 0x41de3b): an optional one is always met
int Goals::nodeState(const Node &n) const {
  if (n.optional)
    return 1;
  return n.kind == 0 ? this->goals[n.goal].state : n.state;
}

// The time limit (ScenarioGoal::checkDeadline 0x42425d): once the months
// played pass nummonths (not -1), 3 - and 2 if any other goal, updated then,
// isn't met (1) or 3
void Goals::checkDeadline() {
  if (this->durationState != 0 || this->durationMonths < 0 || !this->hooks.months)
    return;
  if (this->durationMonths >= this->hooks.months())
    return;
  this->durationState = 3;
  for (Node &n : this->roots) {
    this->updateNode(n);
    const int s = this->nodeState(n);
    if (s != 3 && s != 1)
      this->durationState = 2;
  }
}

// The scenario's list: its time limit, then each goal= of its files
void Goals::updateTop(int i) {
  if (i == 0)
    this->checkDeadline();
  else
    this->updateNode(this->roots[i - 1]);
}
int Goals::topState(int i) const {
  return i == 0 ? this->durationState : this->nodeState(this->roots[i - 1]);
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

// goalsCheckTick 0x435a48 (not once it's over, +0x30): each of the list
// updated in turn; a failed one (2) or the time up (3) stops the round.
// Every one settled (none at 0) or stopped: scenarioComplete 0x48a408 asks
// allGoalsMet 0x48a726, once - each updated again; won (1) if every one is
// met (1) or 3, else lost (2)
void Goals::evaluateAll() {
  if (this->outcome != 0)
    return;
  const int tops = 1 + static_cast<int>(this->roots.size());
  int result = 1;
  for (int i = 0; i < tops; i++) {
    this->updateTop(i);
    const int s = this->topState(i);
    if (s == 0) {
      result = 0;
    } else if (s == 2 || s == 3) {
      result = s;
      break;
    }
  }
  if (result == 0)
    return;
  this->outcome = 1;
  for (int i = 0; i < tops; i++) {
    this->updateTop(i);
    const int s = this->topState(i);
    if (s != 3 && s != 1)
      this->outcome = 2;
  }
  if (this->hooks.ended)
    this->hooks.ended(this->outcome);
}

// zoo.exe 0x4240f5: types 0-2 met when v >= / < / == value; 3-5 failed when
// so; 6-8 failed when v < / >= / != value, else met. A change to a new
// state fires the trigger; back to 0 resets quietly. Sticky: once only.
int Goals::result(const Goal &g) const {
  if (!this->hooks.measure)
    return 0;
  int v = this->hooks.measure(g);
  switch (g.type) {
  case 0: return v >= g.value ? 1 : 0;
  case 1: return v < g.value ? 1 : 0;
  case 2: return v == g.value ? 1 : 0;
  case 3: return v >= g.value ? 2 : 0;
  case 4: return v < g.value ? 2 : 0;
  case 5: return v == g.value ? 2 : 0;
  case 6: return v < g.value ? 2 : 1;
  case 7: return v >= g.value ? 2 : 1;
  case 8: return v != g.value ? 2 : 1;
  default: return 0;
  }
}

void Goals::evaluate(Goal &g) {
  if (g.sticky && g.state != 0)
    return;
  if (!this->hooks.measure)
    return;
  int result = this->result(g);
  if (result == 0) {
    g.state = 0;
    return;
  }
  if (result != g.state) {
    g.state = result;
    this->fire(g, false);
  }
}

// (ours: a save made before composites and extragoals patterns were read
// holds only the goals of the files named outright, outside composites)
void Goals::restore(const std::vector<int> &states) {
  std::vector<Goal *> order;
  for (Goal &g : this->goals)
    order.push_back(&g);
  if (states.size() != order.size()) {
    std::vector<Goal *> legacy;
    for (Goal &g : this->goals)
      if (g.legacy)
        legacy.push_back(&g);
    if (states.size() == legacy.size())
      order = legacy;
  }
  for (size_t i = 0; i < states.size() && i < order.size(); i++) {
    order[i]->state = states[i];
    if (states[i] != 0)
      this->fire(*order[i], true);
  }
}

std::vector<Goals::Slot> Goals::originalOrder(ResourceManager *rm, const std::string &scnPath) {
  std::vector<Slot> out;
  IniReader *scn = rm ? rm->getIniReader(scnPath) : nullptr;
  if (!scn)
    return out;
  Slot scenario;
  scenario.file = scnPath;
  out.push_back(scenario);
  // makeGoal 0x58e066: rtype 1 (0x59364c) and 2 (0x593793) hold child=
  // goals, each made the same way
  std::function<void(IniReader *, const std::string &, const std::string &, int)> add =
      [&](IniReader *ini, const std::string &file, const std::string &sec, int depth) {
        Slot s;
        s.file = file;
        s.name = sec;
        s.depth = depth;
        int rtype = num(ini, sec, "rtype");
        s.goal = rtype == 0;
        s.composite = rtype == 1 || rtype == 2;
        if (s.goal)
          s.rules = readGoal(ini, file, sec);
        out.push_back(s);
        if ((rtype == 1 || rtype == 2) && depth < 16)
          for (const std::string &child : ini->getList(sec, "child"))
            add(ini, file, trim(child), depth + 1);
      };
  auto addFile = [&](IniReader *ini, const std::string &file) {
    for (const std::string &name : ini->getList("goals", "goal"))
      add(ini, file, trim(name), 0);
  };
  addFile(scn, scnPath);
  for (const std::string &entry : scn->getList("start", "extragoals")) {
    for (const std::string &file : extraFiles(rm, entry)) {
      IniReader *ini = rm->getIniReader(file);
      if (!ini)
        continue;
      addFile(ini, file);
      delete ini;
    }
  }
  delete scn;
  return out;
}

namespace {
bool sameGoal(const Goals::Goal &g, const Goals::Slot &s) {
  return lower(g.file) == lower(s.file) && lower(g.name) == lower(s.name);
}
} // namespace

namespace {
// How many states a saved zoo of that version holds of these (zoo.exe
// scenarioLoad 0x58f97d reads one goal at a time until the block's end, and
// fails the zoo unless it ends there; before version 86 a composite reads
// none, 0x5950fd, so only the goals before the first composite can be held)
size_t heldStates(const std::vector<Goals::Slot> &slots, uint32_t version) {
  if (version > 0x55)
    return slots.size();
  for (size_t i = 0; i < slots.size(); i++)
    if (slots[i].composite)
      return i;
  return slots.size();
}
} // namespace

std::vector<int> Goals::originalStates(const std::vector<int> &base, uint32_t version) const {
  std::vector<Slot> slots = originalOrder(this->rm, this->scnPath);
  slots.resize(heldStates(slots, version));
  std::vector<int> out(slots.size(), 0);
  for (size_t i = 0; i < slots.size(); i++) {
    if (i < base.size())
      out[i] = base[i];
    if (i == 0) {
      out[i] = this->durationState; // (the time limit's)
      continue;
    }
    if (slots[i].composite) {
      // (a composite's own state, as our tree has it)
      std::function<const Node *(const std::vector<Node> &)> find = [&](const std::vector<Node> &nodes) -> const Node * {
        for (const Node &n : nodes) {
          if (n.kind != 0 && lower(n.file) == lower(slots[i].file) && lower(n.name) == lower(slots[i].name))
            return &n;
          if (const Node *in = find(n.children))
            return in;
        }
        return nullptr;
      };
      if (const Node *n = find(this->roots))
        out[i] = n->state;
      continue;
    }
    if (!slots[i].goal)
      continue;
    auto own = std::find_if(this->goals.begin(), this->goals.end(),
                            [&](const Goal &g) { return sameGoal(g, slots[i]); });
    if (own != this->goals.end())
      out[i] = own->state;
    else if (out[i] == 0 && slots[i].depth == 0 && this->hooks.measures && this->hooks.measures(slots[i].rules))
      out[i] = this->result(slots[i].rules);
  }
  return out;
}

void Goals::restoreOriginal(const std::vector<int> &states, uint32_t version) {
  std::vector<Slot> slots = originalOrder(this->rm, this->scnPath);
  slots.resize(heldStates(slots, version));
  for (size_t i = 0; i < slots.size() && i < states.size(); i++) {
    if (i == 0) // (the time limit's: ScenarioGoal::setState 0x61c430)
      this->durationState = states[0];
    if (!slots[i].goal)
      continue;
    for (Goal &g : this->goals) {
      if (!sameGoal(g, slots[i]))
        continue;
      // (0x5950ab keeps 0..3)
      g.state = std::max(0, std::min(3, states[i]));
      if (g.state != 0)
        this->fire(g, true);
    }
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
  case 4: {
    // One type made available or not (0x5a0fc2): truleb even yes, odd no;
    // 0-5 the type named targa, 6-7 every animal, 8-9 every type; 10 a
    // scenario's reward kept for freeform (0x598cdc). When it changed
    // anything (not quietly): 4-5 the popup targb, 2-3 the message targb
    // with the type's name
    if (g.truleb == 10) {
      if (this->hooks.reward)
        this->hooks.reward(g.targa);
      break;
    }
    const int kind = g.truleb >= 8 ? 2 : g.truleb >= 6 ? 1 : 0;
    const bool changed = this->hooks.available && this->hooks.available(kind, {g.targa}, g.truleb % 2 == 0);
    if (changed && !quiet) {
      if (g.truleb == 4 || g.truleb == 5)
        this->showPopup(g.file, g.targb);
      else if ((g.truleb == 2 || g.truleb == 3) && this->hooks.messageAbout)
        this->hooks.messageAbout(g.targb, g.targa);
    }
    break;
  }
  case 7: {
    // A list's types made available or not (0x5a1139): section [targa]'s
    // id= each; truleb 0, 2, 4 yes, else no; when it changed anything (not
    // quietly): 4-5 the popup targb, 2-3 the message targb
    IniReader *ini = this->rm ? this->rm->getIniReader(g.file) : nullptr;
    if (!ini)
      break;
    std::vector<int> ids;
    for (const std::string &v : ini->getList(std::to_string(g.targa), "id"))
      ids.push_back(std::atoi(trim(v).c_str()));
    delete ini;
    const bool on = g.truleb == 0 || g.truleb == 2 || g.truleb == 4;
    const bool changed = !ids.empty() && this->hooks.available && this->hooks.available(0, ids, on);
    if (changed && !quiet) {
      if (g.truleb == 4 || g.truleb == 5)
        this->showPopup(g.file, g.targb);
      else if ((g.truleb == 2 || g.truleb == 3) && this->hooks.message)
        this->hooks.message(g.targb);
    }
    break;
  }
  case 6:
    if (std::find(this->awards.begin(), this->awards.end(), g.targa) == this->awards.end()) {
      this->awards.push_back(g.targa);
      if (this->hooks.award)
        this->hooks.award(g.targa);
    }
    break;
  default:
    break;
  }
}
