// Every show (ZT_SHOW_ALL=1 with --panel-shots <out> <map>, a flat map -
// ZT_FLAT_LAB=1 - with room: ff03): a home tank and a show tank sharing a
// wall, every show toy the registry lists in the show tank, the tricks'
// research done; then, for every species with tricks, a pair performs a show
// of every trick it has - each must be available, each performed by both,
// none skipped, both home after it. While a toy is in use its user stands
// exactly on it, facing as it does (the tricks' art draws the toy round the
// user), and it is hidden just when its cHideBuilding says; none is left
// hidden. Then every schedule: Never starts none, Continuous, Frequent and
// Infrequent start the next show their interval after the last.
// <out>/show-all-checks.txt is the report.
#include "World.hpp"
#include "Shows.hpp"
#include "Staff.hpp"
#include "Research.hpp"
#include "ItemCatalog.hpp"
#include "ResourceManager.hpp"
#include "IniReader.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

int runShowAllChecks(World &world, UiGameScreen &hud, ZooSim &sim, ResourceManager *rm, const std::string &out) {
  (void)hud;
  (void)sim;
  std::ofstream report(out + "/show-all-checks.txt");
  int failures = 0, checks = 0;
  auto check = [&](const std::string &name, bool okay) {
    ++checks;
    failures += !okay;
    report << (okay ? "PASS " : "FAIL ") << name << "\n";
    report.flush();
    SDL_Log("[ShowAll] %s %s", okay ? "PASS" : "FAIL", name.c_str());
  };
  auto note = [&](const std::string &text) { report << "     " << text << "\n"; report.flush(); };
  auto &f = world.getFences();
  auto &map = world.getMap();
  auto &a = world.getAnimals();
  auto &st = world.getStaff();
  auto &shows = world.getShows();
  shows.clear();
  a.clear();
  st.clear();
  world.getGuests().clear();
  auto run = [&](float seconds) {
    for (int k = 0; k < static_cast<int>(seconds * 20); ++k) world.update(SDL_GetKeyboardState(nullptr), 0.05f);
  };

  // Every trick's research done: a trick needing it is tested, not refused
  for (auto &branch : Research::get().branches())
    for (auto &category : branch.categories)
      for (auto &program : category.programs)
        if (program.effect == 6) program.done = true;

  // The tanks: a home tank and a show tank sharing a wall (its portal)
  int drag = 900;
  auto rectangle = [&](int x, int y, int w, int h, int type, const char *name) {
    const int base = drag;
    drag += 4;
    for (int i = x; i < x + w; ++i) {
      f.place({true, i, y}, type, base, map);
      f.place({true, i, y + h}, type, base + 1, map);
    }
    for (int j = y; j < y + h; ++j) {
      f.place({false, x, j}, type, base + 2, map);
      f.place({false, x + w, j}, type, base + 3, map);
    }
    for (int id : f.updateExhibits(map, 1, 0, 1)) f.finishExhibit(id, name, map);
    return f.exhibitAt(x, y);
  };
  const int HX = 40, SX = 56, Y = 30, HW = 16, SW = 24, H = 18;
  const int home = rectangle(HX, Y, HW, H, f.typeIndex("tankwal2"), "Show home");
  const int tank = rectangle(SX, Y, SW, H, f.typeIndex("tankwal3"), "Show tank");
  check("home and show tanks built", home >= 0 && tank >= 0 && home != tank);
  if (home < 0 || tank < 0) return 1;
  for (int id : {tank, home}) {
    while (f.canAdjustWall(id, 1)) f.adjustWall(id, 1);
    f.fillExhibit(id, true);
  }
  run(5);
  shows.refresh();
  check("the show tank is a show", shows.show(tank) != nullptr);
  Shows::Show *s = shows.show(tank);
  if (!s) return 1;

  // Every show toy (cShow: placeable in a show tank) in the show tank
  struct Toy { std::string file; bool hide = false; int id = -1; };
  std::vector<Toy> toys;
  {
    std::vector<char> used(SW * H, 0);
    auto take = [&](int w, int h, int &ox, int &oy) {
      for (int j = 0; j + h <= H; ++j)
        for (int i = 0; i + w <= SW; ++i) {
          bool free = true;
          for (int b = 0; b < h && free; ++b)
            for (int c = 0; c < w && free; ++c)
              if (used[(j + b) * SW + i + c]) free = false;
          if (!free) continue;
          for (int b = 0; b < h; ++b)
            for (int c = 0; c < w; ++c) used[(j + b) * SW + i + c] = 1;
          ox = SX + i;
          oy = Y + j;
          return true;
        }
      return false;
    };
    for (const auto &item : ItemCatalog::get().all()) {
      if (item.file.rfind("scenery/", 0) != 0) continue;
      IniReader *ai = rm->getIniReader(item.file);
      if (!ai) continue;
      const std::string ints = "characteristics/integers";
      const bool show = ai->getInt(ints, "cshow", 0) != 0;
      const int fx = std::max(ai->getInt(ints, "cfootprintx", 2), ai->getInt(ints, "cplacementfootprintx", 0));
      const int fy = std::max(ai->getInt(ints, "cfootprinty", 2), ai->getInt(ints, "cplacementfootprinty", 0));
      const bool hide = ai->getInt(ints, "chidebuilding", 0) != 0;
      delete ai;
      if (!show) continue;
      const int w = std::max(1, (fx + 1) / 2), h = std::max(1, (fy + 1) / 2);
      int x = 0, y = 0;
      if (!take(w, h, x, y)) { check("room in the show tank for " + item.file, false); continue; }
      const bool placed = world.objectsMutable().add(item.file, x + w / 2.0f, y + h / 2.0f, 0);
      check("show toy placed: " + item.file, placed);
      if (placed) toys.push_back({item.file, hide, world.getObjects().objects().back().id});
    }
  }
  world.reindex();
  note(std::to_string(toys.size()) + " show toys in the show tank");
  auto toyOf = [&](int id) -> const Toy * {
    for (const auto &t : toys) if (t.id == id) return &t;
    return nullptr;
  };

  const int trainerType = st.typeOfFile("staff/trainer.ai");
  const int trainer = st.hire(trainerType, SX + SW + 3.5f, Y + 2.5f, map, f);
  check("marine specialist hired", trainer >= 0);
  if (trainer < 0) return 1;
  run(1);

  // Every species with tricks
  std::string why;
  std::vector<std::string> performers;
  for (const auto &type : a.types()) {
    if (!type.showAnimal) continue;
    const std::string file = type.file, name = file.substr(file.find('/') + 1);
    while (shows.remove(tank, 0)) {}
    a.clear();
    st.moveTo(trainer, SX + SW + 3.5f, Y + 2.5f);
    const int typeIndex = a.typeOfFile(file);
    const int one = a.adopt(typeIndex, false, HX + 4.5f, Y + 6.5f, map, f);
    const int two = a.adopt(typeIndex, true, HX + 10.5f, Y + 10.5f, map, f);
    run(1);
    for (auto &m : const_cast<std::vector<Animals::Member> &>(a.members())) m.happiness = 100;
    const bool selected = shows.selectSpecies(tank, file);
    check(name + ": a pair adopted and chosen for the show", one >= 0 && two >= 0 && selected);
    if (one < 0 || two < 0 || !selected) continue;
    performers.push_back(file);
    int added = 0, offered = 0;
    std::vector<std::string> keys;
    for (const auto &t : shows.tricks(file)) {
      if (!t.visible) continue;
      ++offered;
      const std::string refusal = shows.unavailable(tank, file, t);
      const bool ok = refusal.empty() && shows.add(tank, t.key);
      check(name + " trick " + t.key + (t.kind == 1 ? " (toy " + std::to_string(t.building) + ")" : "") + " available" +
                (refusal.empty() ? "" : ": " + refusal),
            ok);
      if (ok) {
        ++added;
        keys.push_back(t.key);
      }
    }
    if (!added) continue;
    const int before = s->completed, doneBefore = s->tricksDone, skippedBefore = s->tricksSkipped;
    const bool started = shows.start(tank, why);
    check(name + ": show of " + std::to_string(added) + " tricks starts" + (started ? "" : " (" + why + ")"), started);
    if (!started) continue;
    bool offToy = false, wrongHide = false;
    std::string offWhat, hideWhat;
    std::set<std::string> anims;
    std::map<int, int> holding; // actor -> the toy it held a second ago
    for (int i = 0; i < 60 * added + 240; ++i) {
      run(1);
      for (auto &m : const_cast<std::vector<Animals::Member> &>(a.members())) m.happiness = 100;
      for (const auto &actor : s->actors) {
        const auto *m = a.member(actor.id);
        if (!m) continue;
        if (m->showTrick) anims.insert(m->anim);
        const bool justTaken = actor.object >= 0 && holding[actor.id] != actor.object;
        holding[actor.id] = actor.object;
        if (actor.object < 0) continue;
        const auto *o = world.objectsMutable().byId(actor.object);
        const Toy *toy = toyOf(actor.object);
        if (!o || !toy) continue;
        const float angle = static_cast<float>(o->facing) * 3.14159265f / 4.0f;
        if (std::hypot(m->x - o->x, m->y - o->y) > 0.01f || std::hypot(m->fx - std::sin(angle), m->fy + std::cos(angle)) > 0.01f) {
          offToy = true;
          offWhat = toy->file;
        }
        // (a cHideBuilding toy's art is in the trick's from its first frame:
        // hidden as the trick takes it - shown again at its fExitBuilding;
        // one without, like the stage, never hidden)
        if ((toy->hide && justTaken && !o->showHidden) || (!toy->hide && o->showHidden)) {
          wrongHide = true;
          hideWhat = toy->file;
        }
      }
      if (s->completed > before && s->phase == Shows::Phase::Waiting) break;
    }
    const int actors = 2;
    check(name + ": the show completes", s->completed == before + 1 && s->phase == Shows::Phase::Waiting);
    check(name + ": every trick performed by both (" + std::to_string(s->tricksDone - doneBefore) + " of " +
              std::to_string(added * actors) + ", " + std::to_string(s->tricksSkipped - skippedBefore) + " skipped)",
          s->tricksDone - doneBefore == added * actors && s->tricksSkipped == skippedBefore);
    check(name + ": on its toy, facing as it does, while using it" + (offToy ? " (" + offWhat + ")" : ""), !offToy);
    check(name + ": a toy hidden as the trick takes it if cHideBuilding, never if not" + (wrongHide ? " (" + hideWhat + ")" : ""),
          !wrongHide);
    bool leftHidden = false;
    for (const auto &t : toys)
      if (const auto *o = world.objectsMutable().byId(t.id))
        if (o->showHidden) leftHidden = true;
    check(name + ": no toy left hidden after", !leftHidden);
    const auto *m1 = a.member(one), *m2 = a.member(two);
    check(name + ": both back home", m1 && m2 && m1->exhibit == home && m2->exhibit == home && !m1->boxed && !m2->boxed);
    {
      std::string list;
      for (const std::string &n : anims) list += " " + n;
      note(name + ": " + std::to_string(anims.size()) + " trick animations seen:" + list);
    }
  }
  note(std::to_string(performers.size()) + " show species");

  // Every schedule: the next show the interval after the last (or never)
  if (!performers.empty()) {
    const std::string file = performers.front();
    while (shows.remove(tank, 0)) {}
    a.clear();
    st.moveTo(trainer, SX + SW + 3.5f, Y + 2.5f);
    a.adopt(a.typeOfFile(file), false, HX + 4.5f, Y + 6.5f, map, f);
    a.adopt(a.typeOfFile(file), true, HX + 10.5f, Y + 10.5f, map, f);
    run(1);
    shows.selectSpecies(tank, file);
    for (const auto &t : shows.tricks(file))
      if (t.visible && t.kind == 0 && shows.unavailable(tank, file, t).empty()) {
        shows.add(tank, t.key);
        break;
      }
    // (to the end of any show going, then a clean wait)
    auto settle = [&]() {
      for (int i = 0; i < 600 && s->phase != Shows::Phase::Waiting; ++i) run(1);
    };
    shows.setFrequency(tank, -1);
    settle();
    const int beforeNever = s->completed;
    bool startedNever = false;
    for (int i = 0; i < 200 && !startedNever; ++i) {
      run(1);
      startedNever = s->phase != Shows::Phase::Waiting || s->completed != beforeNever;
    }
    check("Never: no show starts in 200 seconds", !startedNever);
    const char *names[3] = {"Continuous", "Frequent", "Infrequent"};
    for (int freq = 0; freq <= 2; ++freq) {
      shows.setFrequency(tank, freq);
      settle();
      // When the show falls due (it stops waiting for the next show) is the
      // schedule's; a show then starts once a specialist is free - one busy
      // feeding rightly holds it back (the first run: 82 s after Never)
      int waited = 0, dueAt = -1;
      bool started = false;
      const int interval = shows.settings.interval[freq];
      std::string statuses, last;
      for (; waited < interval + 300 && !started; ++waited) {
        run(1);
        started = s->phase != Shows::Phase::Waiting;
        if (dueAt < 0 && (started || s->status.rfind("Waiting for next show", 0) != 0)) dueAt = waited + 1;
        if (s->status != last) {
          last = s->status;
          statuses += " | " + std::to_string(waited) + "s " + last;
        }
      }
      note(std::string(names[freq]) + " waiting:" + statuses);
      for (auto &m : const_cast<std::vector<Animals::Member> &>(a.members())) m.happiness = 100;
      check(std::string(names[freq]) + ": the next show falls due " + std::to_string(interval) + " seconds after the last (" +
                (dueAt >= 0 ? std::to_string(dueAt) + " s" : "never") + ")",
            dueAt >= 0 && std::abs(dueAt - interval) <= 6);
      check(std::string(names[freq]) + ": and a show starts" + (started ? " (" + std::to_string(waited) + " s)" : ""), started);
      settle();
    }
  }
  report << "\n" << checks << " checks, " << failures << " failures\n";
  SDL_Log("[ShowAll] %d checks, %d failures", checks, failures);
  return failures ? 1 : 0;
}
