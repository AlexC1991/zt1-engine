// The zoo's money, as the original's (ZT_FINANCE_CHECKS=1 with --panel-shots
// <out> <map>, a flat map - ZT_FLAT_LAB=1: ff03):
// - funding (marketing, research) is paid by the 3 s tick only while the zoo
//   has the tick's cost: it never takes the cash below zero, and none is
//   paid with the cash below zero (zoo.exe 0x41f2ae, 0x41f1ba);
// - research pays only while a program's being worked on, and makes no
//   progress on a tick it couldn't pay for (0x41f1ba);
// - nothing costing more than the cash can be bought. An object, an animal
//   or a keeper (the buy panels' items) shows its preview red with its
//   price, and a click is refused with no message (zoo.exe 0x5ce19f); a
//   fence or a path dragged is refused with 10002 "This purchase cannot be
//   made at this time due to insufficient funds." (0x4fb08c, 0x48b843).
//   With the money, the same click buys it.
// <out>/finance-checks.txt is the report.
#include "World.hpp"
#include "Research.hpp"
#include "ItemCatalog.hpp"
#include "ResourceManager.hpp"
#include "ZooSim.hpp"
#include "Goals.hpp"
#include "OriginalConstants.hpp"
#include <algorithm>
#include <set>
#include "ui/UiGameScreen.hpp"
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

int runFinanceChecks(World &world, UiGameScreen &hud, ZooSim &sim, ResourceManager *rm, const std::string &out) {
  (void)hud;
  (void)rm;
  std::ofstream report(out + "/finance-checks.txt");
  int failures = 0, checks = 0;
  auto check = [&](const std::string &name, bool okay) {
    ++checks;
    failures += !okay;
    report << (okay ? "PASS " : "FAIL ") << name << "\n";
    report.flush();
    SDL_Log("[Finance] %s %s", okay ? "PASS" : "FAIL", name.c_str());
  };
  auto money = [](double v) { return std::to_string(std::lround(v)); };

  // Marketing at $12,000 a month: $100 a tick
  {
    ZooSim books;
    books.start(250, 50);
    books.setMonthlyCost(ZooSim::Marketing, 0, 12000);
    double lowest = books.cash();
    for (int i = 0; i < 300; i++) {
      books.update(0.1);
      lowest = std::min(lowest, books.cash());
    }
    check("marketing pays its ticks while the zoo has them, then stops: $250 leaves $" + money(books.cash()),
          std::lround(books.cash()) == 50);
    check("marketing never takes the cash below zero (lowest $" + money(lowest) + ")", lowest >= 0);
    check("the two ticks paid are on the books (Marketing $" + money(-books.history().back().lines[ZooSim::Marketing]) + ")",
          std::lround(-books.history().back().lines[ZooSim::Marketing]) == 200);
  }
  {
    ZooSim books;
    books.start(-500, 50);
    books.setMonthlyCost(ZooSim::Marketing, 0, 2000);
    books.update(60);
    check("with the cash below zero no marketing's paid ($" + money(books.cash()) + ")", std::lround(books.cash()) == -500);
  }
  {
    ZooSim books;
    books.start(100000, 50);
    books.setMonthlyCost(ZooSim::Marketing, 0, 1200);
    books.update(360); // (30 days: a 120 ticks)
    check("a month's marketing costs its monthly price ($" + money(100000 - books.cash()) + " of $1,200)",
          std::abs(100000 - books.cash() - 1200) <= 1);
    check("a tick's paid only if it's no more than the cash", !books.payTick(ZooSim::Marketing, 0, books.cash() + 1) &&
                                                                  books.payTick(ZooSim::Marketing, 0, 1));
  }

  // The infinite money cheat: booked, but the cash never goes down
  {
    ZooSim books;
    books.start(100, 50);
    books.unlimited = true;
    books.spend(ZooSim::Construction, 5000);
    books.setMonthlyCost(ZooSim::Marketing, 0, 12000);
    books.update(30);
    books.earn(ZooSim::Donations, 50);
    check("infinite money: $5,000 built and marketing paid, cash still $" + money(books.cash() - 50) + " (then $50 given)",
          std::lround(books.cash()) == 150);
    check("infinite money: the spending's still on the books (Construction $" +
              money(-books.history().back().lines[ZooSim::Construction]) + ", Marketing $" +
              money(-books.history().back().lines[ZooSim::Marketing]) + ")",
          std::lround(-books.history().back().lines[ZooSim::Construction]) == 5000 &&
              std::lround(-books.history().back().lines[ZooSim::Marketing]) == 1000);
    check("infinite money: anything can be bought", books.canAfford(1e9));
    books.unlimited = false;
    check("cheat off: $1,000,000,000 can't be", !books.canAfford(1e9));
  }

  // A private foundation (zoo.exe 0x4351a9, 0x613e4a): only Tutorial 2's
  // economy file has a chance (100); 3 donations, then "No foundations..."
  {
    Original::loadEconomy(rm);
    ZooSim books;
    books.start(500, 50);
    int given = 0, news = 0;
    for (int i = 0; i < 1000 && !news; i++)
      news = books.foundationTick(given);
    check("freeform's economy: no foundation ever gives (chance " + std::to_string(Original::economy().donationChance) + ")",
          news == 0 && std::lround(books.cash()) == 500);
    Original::loadEconomy(rm, "scenario/scn02/scn02.scn");
    const Original::Economy &e = Original::economy();
    check("Tutorial 2's economy over it: chance " + std::to_string(e.donationChance) + ", $" + std::to_string(e.donationLow) +
              "-$" + std::to_string(e.donationHigh) + ", " + std::to_string(e.donationsAvail) + " of them",
          e.donationChance == 100 && e.donationLow == 15000 && e.donationHigh == 20000 && e.donationsAvail == 3);
    std::vector<int> got;
    bool inRange = true, booked = true;
    for (int i = 0; i < 6; i++) {
      books.setCash(500);
      given = 0;
      int n = books.foundationTick(given);
      got.push_back(n);
      if (n == 15002) {
        inRange = inRange && given >= 15000 && given < 20000;
        booked = booked && std::lround(books.cash()) == 500 + given;
      }
    }
    check("under $1,000: three donations, then 15003 once, then nothing",
          got == std::vector<int>{15002, 15002, 15002, 15003, 0, 0});
    check("each $15,000-$19,999, added to the cash", inRange && booked);
    {
      ZooSim rich;
      rich.start(1000, 50);
      check("$1,000 or more: none", rich.foundationTick(given) == 0);
    }
    if (rm->hasResource("scenario/scn34/scn34.scn")) {
      Original::loadEconomy(rm, "scenario/scn34/scn34.scn");
      check("a scenario with admission $0-$0: $22 held at $" + money(Original::clampAdmission(22.0f)),
            Original::clampAdmission(22.0f) == 0.0f);
    }
    Original::loadEconomy(rm);
  }

  // Research: the research branch at its most
  {
    Research &rs = Research::get();
    const auto saved = rs.branches();
    const float savedClock = rs.tickClock;
    ResearchBranch *br = rs.branch(0);
    ResearchProgram *p = br ? rs.current(*br) : nullptr;
    if (br && !p) {
      rs.pick(*br);
      p = rs.current(*br);
    }
    check("a research program to work on", br && p);
    if (br && p) {
      br->fundingLevel = static_cast<int>(br->funding.size()) - 1;
      const ResearchFunding &f = br->funding[br->fundingLevel];
      std::vector<double> asked;
      const float before = p->progress;
      rs.tickClock = 0;
      rs.advance(3.0f, [&](int branch, double amount) {
        if (branch == 0)
          asked.push_back(amount);
        return true;
      });
      check("a paid tick asks a 120th of the month's cost ($" + std::to_string(asked.empty() ? 0.0 : asked[0]) + ")",
            asked.size() == 1 && std::abs(asked[0] - f.cost / 120.0) < 1e-6);
      check("and the program gains a 120th of the month's work", std::abs(p->progress - before - f.work / 120.0f) < 1e-4f);
      const float paidTo = p->progress;
      rs.advance(3.0f, [](int, double) { return false; });
      check("a tick the zoo can't pay for: no progress", p->progress == paidTo);
      // (every program of the branch done: nothing being researched)
      for (auto &c : br->categories)
        for (auto &prog : c.programs)
          prog.done = true;
      br->currentCategory = br->currentProgram = -1;
      asked.clear();
      rs.advance(30.0f, [&](int branch, double amount) {
        if (branch == 0)
          asked.push_back(amount);
        return true;
      });
      check("nothing left to research: nothing paid (" + std::to_string(asked.size()) + " ticks asked)", asked.empty());
    }
    rs.branches() = saved;
    rs.tickClock = savedClock;
  }

  // Buying with too little: a flat corner of the map
  auto &map = world.getMap();
  auto &f = world.getFences();
  auto &a = world.getAnimals();
  auto &st = world.getStaff();
  a.clear();
  st.clear();
  world.getGuests().clear();
  const int X = 30, Y = 30;
  world.centreOn(X + 4.0f, Y + 4.0f);
  auto centre = [&](int tx, int ty, int &px, int &py) {
    int qx, qy;
    world.vertexToWindow(tx, ty, px, py);
    world.vertexToWindow(tx + 1, ty + 1, qx, qy);
    px = (px + qx) / 2;
    py = (py + qy) / 2;
  };
  int px, py;
  sim.start(10, 50);

  // An object
  {
    const CatalogItem *item = nullptr;
    for (const CatalogItem &c : ItemCatalog::get().all())
      if (c.registrySection == "building" && c.file.find("bench") != std::string::npos) {
        item = &c;
        break;
      }
    check("a bench to buy", item != nullptr);
    if (item) {
      world.setObjectTool(item->file, 500, 0);
      centre(X, Y, px, py);
      world.mouseMove(px, py);
      const size_t before = world.getObjects().objects().size();
      world.cash = 10;
      world.mouseMove(px, py);
      check("a $500 bench with $10: its preview red", world.objectPreviewPoor());
      World::ToolResult r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("a $500 bench with $10: refused, no message (message " + std::to_string(r.messageId) + ")",
            r.messageId == 0 && r.cost == 0 && world.getObjects().objects().size() == before);
      world.cash = 1000;
      world.mouseMove(px, py);
      check("with $1,000 its preview isn't red for the price", !world.objectPreviewPoor());
      r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("with $1,000 the same click buys it ($" + std::to_string(r.cost) + ")",
            r.cost == 500 && world.getObjects().objects().size() == before + 1);
      world.cash = 500;
      centre(X + 2, Y, px, py);
      world.mouseMove(px, py);
      r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("with exactly its price it can be bought", r.cost == 500);
      world.setObjectTool("", 0, 0);
    }
  }

  // A fence dragged round an exhibit, then an animal in it
  {
    const int type = f.typeIndex("chainlnk");
    check("chain-link fence", type >= 0);
    world.setFenceTool(type);
    const int fx = X + 6, fy = Y;
    int x0, y0, x1, y1;
    world.vertexToWindow(fx, fy, x0, y0);
    world.vertexToWindow(fx + 4, fy, x1, y1);
    world.cash = 1;
    world.mouseDown(x0, y0);
    world.mouseMove(x1, y1);
    World::ToolResult r = world.mouseUp(x1, y1);
    int laid = 0;
    for (int i = 0; i < 4; i++)
      laid += f.at({true, fx + i, fy}) ? 1 : 0;
    check("a fence dragged with $1: none laid, insufficient funds (" + std::to_string(laid) + " pieces)",
          laid == 0 && r.cost == 0 && r.messageId == World::kInsufficientFunds);
    world.setFenceTool(-1);
    // (the exhibit laid with money)
    int drag = 700;
    for (int i = fx; i < fx + 4; ++i) {
      f.place({true, i, fy}, type, drag, map);
      f.place({true, i, fy + 4}, type, drag + 1, map);
    }
    for (int j = fy; j < fy + 4; ++j) {
      f.place({false, fx, j}, type, drag + 2, map);
      f.place({false, fx + 4, j}, type, drag + 3, map);
    }
    for (int id : f.updateExhibits(map, 1, 0, 1))
      f.finishExhibit(id, "Test", map);
    check("a test exhibit", f.exhibitAt(fx + 1, fy + 1) >= 0);
    const int zebra = a.typeOfFile("animals/zebra.ai");
    check("zebras", zebra >= 0);
    if (zebra >= 0) {
      world.setAnimalTool(zebra, false);
      centre(fx + 2, fy + 2, px, py);
      world.mouseMove(px, py);
      world.cash = 1;
      world.mouseMove(px, py);
      check("a zebra with $1: its preview red", a.previewPoor);
      r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("a zebra with $1: refused, no message (message " + std::to_string(r.messageId) + ")",
            r.messageId == 0 && r.animalCost == 0 && a.members().empty());
      world.cash = 1e6;
      world.mouseMove(px, py);
      r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("with the money it's adopted ($" + std::to_string(r.animalCost) + ")", r.animalCost > 0 && a.members().size() == 1);
      world.setAnimalTool(-1, false);
    }
  }

  // A tank's walls dragged up by their top: only as far as the cash covers
  // the whole drag (measured in the original: $764 with $627 a step, one
  // step however far the mouse goes; $100, none), and that's charged (#72)
  {
    int tankType = -1;
    for (size_t i = 0; i < f.types().size(); i++)
      if (f.types()[i].tank) {
        tankType = static_cast<int>(i);
        break;
      }
    check("a tank wall to build with", tankType >= 0);
    const int tx = X + 14, ty = Y;
    int drag = 800;
    for (int i = tx; i < tx + 3; ++i) {
      f.place({true, i, ty}, tankType, drag, map);
      f.place({true, i, ty + 3}, tankType, drag + 1, map);
    }
    for (int j = ty; j < ty + 3; ++j) {
      f.place({false, tx, j}, tankType, drag + 2, map);
      f.place({false, tx + 3, j}, tankType, drag + 3, map);
    }
    for (int id : f.updateExhibits(map, 1, 0, 1))
      f.finishExhibit(id, "Test tank", map);
    const int tank = f.exhibitAt(tx + 1, ty + 1);
    const Fences::Exhibit *ex = tank >= 0 ? f.exhibit(tank) : nullptr;
    check("a test tank", ex && ex->tank);
    if (ex && ex->tank) {
      const double step = f.wallStepCost(tank);
      const int pressY = 400;
      auto dragUp = [&](double cash, int steps) {
        world.cash = cash;
        world.beginWallDrag(tank, pressY);
        world.mouseMove(px, pressY - steps * 200); // (far further than the steps)
        return world.mouseUp(px, pressY - steps * 200);
      };
      const int sub0 = ex->wallSub;
      World::ToolResult r = dragUp(1, 3);
      check("tank walls dragged up with $1: they don't move, nothing charged ($" + std::to_string(r.cost) + ")",
            r.cost == 0 && f.exhibit(tank)->wallSub == sub0);
      r = dragUp(step * 1.5, 3);
      check("with one step's price and a half: one step, that step charged ($" + std::to_string(r.cost) + ", a step $" +
                money(step) + ")",
            r.cost == std::lround(step) && f.exhibit(tank)->wallSub == sub0 + 1);
      const int sub1 = f.exhibit(tank)->wallSub;
      r = dragUp(1e6, 2);
      const int moved = f.exhibit(tank)->wallSub - sub1;
      check("with the money: as far as the mouse goes, each step charged (" + std::to_string(moved) + " steps, $" +
                std::to_string(r.cost) + ")",
            moved > 1 && r.cost == moved * std::lround(step));
    }
  }

  // The bulldozer, as the original's (watched 2026-11-06): a click takes
  // what's under it on letting go, not on the press; a drag over 16 pixels
  // makes a rectangle whose paths and scenery are taken together with their
  // money back - not a building among the structures
  {
    const int bx = X + 4, by = Y + 8;
    const std::string pathType = map.getPathTypes().empty() ? "" : map.getPathTypes().front();
    for (int i = 0; i < 4; i++)
      map.setPath(bx + i, by, pathType);
    const CatalogItem *scenery = nullptr, *building = nullptr;
    for (const CatalogItem &c : ItemCatalog::get().all()) {
      if (c.cost <= 0)
        continue;
      if (!c.members.count("structures") && c.file == "scenery/other/trshcan.ai" &&
          (!scenery || scenery->file.find("trash") == std::string::npos))
        scenery = &c;
      if (!building && c.file == "scenery/building/vendfood.ai")
        building = &c;
    }
    check("a trash can and a snack stand to place (" + std::string(scenery ? scenery->file : "none") + ", " +
              std::string(building ? building->file : "none") + (building && building->members.count("structures") ? ", a structure" : "") + ")",
          scenery && building && building->members.count("structures"));
    world.cash = 1e6;
    auto place = [&](const CatalogItem *c, int tx, int ty) {
      world.setObjectTool(c->file, c->cost, 0);
      centre(tx, ty, px, py);
      world.mouseMove(px, py);
      world.mouseDown(px, py);
      world.mouseUp(px, py);
      world.setObjectTool("", 0, 0);
    };
    if (scenery && building) {
      place(scenery, bx, by + 2);
      place(scenery, bx + 2, by + 2);
      place(building, bx + 1, by + 4);
    }
    const size_t objects = world.getObjects().objects().size();
    world.setBulldozer(true);
    // (a click: pressed on a path tile it stays; let go, it's gone)
    centre(bx + 3, by, px, py);
    world.mouseMove(px, py);
    world.mouseDown(px, py);
    const bool stayed = map.isPath(bx + 3, by);
    World::ToolResult r = world.mouseUp(px, py);
    check("the bulldozer: a press leaves the path tile, letting go takes it ($" + std::to_string(r.refund) + " back)",
          stayed && !map.isPath(bx + 3, by) && r.refund > 0);
    // (a drag from the paths' first tile to beyond the snack stand)
    int qx, qy;
    centre(bx, by, px, py);
    centre(bx + 3, by + 5, qx, qy);
    world.mouseMove(px, py);
    world.mouseDown(px, py);
    world.mouseMove((px + qx) / 2, (py + qy) / 2);
    world.mouseMove(qx, qy);
    const bool dragging = world.bulldozeDragging();
    const size_t lit = world.bulldozeDragCount();
    const int shown = world.hoverCost();
    r = world.mouseUp(qx, qy);
    int pathsLeft = 0;
    for (int i = 0; i < 3; i++)
      pathsLeft += map.isPath(bx + i, by) ? 1 : 0;
    check("a drag: a rectangle, its 3 paths and 2 trash cans lit (" + std::to_string(lit) + "), $" + std::to_string(shown) +
              " back shown",
          dragging && lit == 5 && shown > 0);
    check("let go: the paths and scenery taken, the snack stand left, the money shown paid back ($" +
              std::to_string(r.refund) + ")",
          pathsLeft == 0 && world.getObjects().objects().size() == objects - 2 && r.refund == shown);
    world.setBulldozer(false);
  }

  // A keeper
  {
    const int keeper = st.typeOfFile("staff/keeper.ai");
    check("keepers", keeper >= 0);
    if (keeper >= 0) {
      world.setStaffTool(keeper);
      centre(X, Y + 3, px, py);
      world.mouseMove(px, py);
      world.cash = 1;
      world.mouseMove(px, py);
      check("a keeper with $1: its preview red", st.previewPoor);
      World::ToolResult r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("a keeper with $1: not hired, no message (message " + std::to_string(r.messageId) + ")",
            r.messageId == 0 && r.wage == 0 && st.members().empty());
      world.setStaffTool(-1);
    }
  }

  // A path dragged
  {
    world.setPathTool("dirtpath", 10);
    int x0, y0, x1, y1;
    centre(X, Y + 6, x0, y0);
    centre(X + 4, Y + 6, x1, y1);
    world.cash = 20;
    world.mouseMove(x0, y0);
    world.mouseDown(x0, y0);
    for (int i = 1; i <= 4; i++) {
      centre(X + i, Y + 6, x1, y1);
      world.mouseMove(x1, y1);
    }
    World::ToolResult r = world.mouseUp(x1, y1);
    int laid = 0;
    for (int i = 0; i <= 4; i++)
      laid += map.isPath(X + i, Y + 6) ? 1 : 0;
    check("five path tiles ($50) with $20: none laid, insufficient funds (" + std::to_string(laid) + " laid)",
          laid == 0 && r.cost == 0 && r.messageId == World::kInsufficientFunds);
    world.setPathTool("", 0);
  }
  world.cash = 1e300;

  // The goal tree (zoo.exe makeGoal 0x58e066), on scripted measures: Marine
  // Mania's shark award is goals in order (rtype 2: the rule, the award, its
  // cash, its popup) whose first is all of four sharks (rtype 1)
  {
    Goals t;
    std::set<std::string> met;
    std::vector<int> awarded;
    int cash = 0, monthsNow = 0, ended = 0;
    // (each goal met by its type - 3..5, which are never met, not failed)
    auto toMeet = [](const Goals::Goal &g) {
      return g.type == 1 || g.type == 3 || g.type == 7 ? g.value - 1 : g.type == 5 ? g.value + 1 : g.value;
    };
    bool all = false;
    Goals::Hooks h;
    h.measure = [&](const Goals::Goal &g) -> int {
      if (g.rulea == 99)
        return g.arga; // (0x41d665: arga itself)
      if (g.rulea == 7 && g.ruleb == 2)
        return monthsNow;
      if (all || met.count(g.name))
        return toMeet(g);
      return g.type == 1 || g.type == 7 ? g.value : g.value - 1;
    };
    h.award = [&](int a) { awarded.push_back(a); };
    h.cash = [&](int amount, int) { cash += amount; };
    h.months = [&]() { return monthsNow; };
    h.ended = [&](int o) { ended = o; };
    t.setHooks(h);
    t.load(rm, "freeform/unlock02.scn");
    const Goals::Node *shark = nullptr;
    for (const Goals::Node &n : t.tree())
      if (n.name == "shark_conservation")
        shark = &n;
    check("Marine Mania's shark award: in order (rtype 2) of 4, its first all of 4 sharks (rtype 1)",
          shark && shark->kind == 2 && shark->optional && shark->children.size() == 4 &&
              shark->children[0].kind == 1 && shark->children[0].children.size() == 4);
    met = {"HHEAD", "GWHITE", "TIGERSH"};
    t.evaluateAll();
    const bool none = std::find(awarded.begin(), awarded.end(), 19) == awarded.end() && cash == 0;
    met.insert("MAKO");
    t.evaluateAll();
    const bool given = std::find(awarded.begin(), awarded.end(), 19) != awarded.end() && cash == 10000;
    t.evaluateAll();
    check("three sharks of four: no award; all four: award 19 and $10,000 (the children after the rule wait for it)",
          none && given);
    check("and only once ($" + std::to_string(cash) + ")", cash == 10000);
    check("a free-form file's goals never end it (outcome " + std::to_string(t.outcome) + ")", t.outcome == 0 && ended == 0);

    // Scenario 3's time limit: 6 months (months from 0, the original's)
    Goals s;
    s.setHooks(h);
    met.clear();
    monthsNow = 0;
    s.load(rm, "scenario/scn03/scn03.scn");
    check("scenario 3: a 6-month limit, not met at the start (" + std::to_string(s.durationMonths) + ", state " +
              std::to_string(s.durationState) + ")",
          s.durationMonths == 6 && s.durationState == 0);
    monthsNow = 6;
    s.evaluateAll();
    const bool running = s.outcome == 0 && ended == 0;
    monthsNow = 7;
    s.evaluateAll();
    check("its goals not met: still on at 6 months played; lost after (outcome " + std::to_string(s.outcome) + ")",
          running && s.outcome == 2 && ended == 2);
    ended = 0;
    all = true;
    monthsNow = 3;
    s.load(rm, "scenario/scn03/scn03.scn");
    s.evaluateAll();
    const bool early = s.outcome == 0;
    monthsNow = 7;
    s.evaluateAll();
    check("every goal met: not over before the limit; won once it passes (outcome " + std::to_string(s.outcome) + ")",
          early && s.outcome == 1 && ended == 1);
  }

  report << "\n" << (checks - failures) << "/" << checks << " pass\n";
  SDL_Log("[Finance] %d/%d pass", checks - failures, checks);
  return failures ? 1 : 0;
}
