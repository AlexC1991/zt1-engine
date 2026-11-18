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
#include "SaveGame.hpp"
#include "ZooReader.hpp"
#include "MemoryManager.hpp"
#include "Research.hpp"
#include "ItemCatalog.hpp"
#include "ResourceManager.hpp"
#include "ZooSim.hpp"
#include "Goals.hpp"
#include "OriginalConstants.hpp"
#include <algorithm>
#include <set>
#include "ui/UiGameScreen.hpp"
#include "ui/UiGraph.hpp"
#include "ui/UiText.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The game's own goal hooks (main.cpp's setupGoals), set before the checks run
Goals::Hooks g_financeGoalHooks;

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
  // A tick's share is spent as it is, cents and all (zoo.exe 0x41f2ae,
  // 0x41f1ba: a float to spendCash; only the cash shown is rounded)
  {
    ZooSim books;
    books.start(1000, 50);
    books.setMonthlyCost(ZooSim::Marketing, 0, 1000);
    books.update(3.0);
    const double spent = 1000 - books.cash();
    check("one tick of $1,000 a month: $" + std::to_string(spent) + " (a 120th, not rounded to whole dollars)",
          std::abs(spent - 1000.0 / 120.0) < 1e-6);
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

  // economy.cfg's numbers for the zoo rating's kinds (zooRatingTerms
  // 0x41fcc1: cSpeciesAvailable) and the members (monthlyUpkeep 0x483f11,
  // 0x4fa85d), read as ZTGameMgr::loadConfig 0x591c0f reads them
  {
    Original::loadEconomy(rm);
    const Original::Economy &e = Original::economy();
    check("economy.cfg: species available " + std::to_string(e.speciesAvailable) + "; members join over " +
              std::to_string(e.membershipJoinHappiness) + " happy, " + std::to_string(e.membershipJoinChance) +
              "%, lapse factor " + std::to_string(e.membershipJoinFactor) + ", give $" +
              std::to_string(e.endowmentGiftLow) + "-$" + std::to_string(e.endowmentGiftHigh),
          e.speciesAvailable == 20 && e.membershipJoinHappiness == 80 && e.membershipJoinChance == 15 &&
              e.membershipJoinFactor == 10 && e.endowmentGiftLow == 100 && e.endowmentGiftHigh == 300);
  }

  // Guest arrivals (zoo.exe guestArrivalCheck 0x424375, #207): economy.cfg's
  // cAdultAdmission bands over the exe's 50/20/10/5 (loadConfig 0x591a1f)
  // and its cCreateGuestChance* - measured live in a new freeform zoo:
  // 49, 29, 19, 9 and 3, 5, 25, 30, 40
  {
    const Original::Economy &e = Original::economy();
    std::string bands, chances;
    for (float v : e.adultAdmission) bands += " " + std::to_string(static_cast<int>(v));
    for (int v : e.createGuestChance) chances += " " + std::to_string(v);
    check("economy.cfg: admission bands" + bands + "; arrival chances" + chances,
          e.adultAdmission[0] == 49 && e.adultAdmission[1] == 29 && e.adultAdmission[2] == 19 &&
              e.adultAdmission[3] == 9 && e.adultAdmission[4] == 0 && e.createGuestChance[0] == 3 &&
              e.createGuestChance[1] == 5 && e.createGuestChance[2] == 25 && e.createGuestChance[3] == 30 &&
              e.createGuestChance[4] == 40);
    // (the decoded table: a $60 ticket at rating 85 rolls VeryLow, $22 at
    // 85 High, $5 at 85 VeryHigh; nothing over $49 under 81; a $22 ticket
    // under 30 VeryLow)
    check("arrival table cells (0x42456c, 0x42457c, 0x42458c, 0x42452e)",
          Original::guestArrivalChance[0][0] == 0 && Original::guestArrivalChance[0][2] == 3 &&
              Original::guestArrivalChance[0][4] == 4 && Original::guestArrivalChance[1][0] < 0 &&
              Original::guestArrivalChance[2][0] < 0 && Original::guestArrivalChance[3][2] == 0);
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
      check("a paid tick asks a 120th of the month's cost ($" + std::to_string(asked.empty() ? 0.0 : asked[0]) + " of $" +
                std::to_string(f.cost) + ", " + std::to_string(asked.size()) + " asked)",
            asked.size() == 1 && std::abs(asked[0] - f.cost / 120.0) < 1e-6);
      check("and the program gains a 120th of the month's work (" + std::to_string(p->progress - before) + " of " + std::to_string(f.work) + ")",
            std::abs(p->progress - before - f.work / 120.0f) < 1e-4f);
      const float paidTo = p->progress;
      rs.advance(3.0f, [](int, double) { return false; });
      check("a tick the zoo can't pay for: no progress", p->progress == paidTo);
      // Unchecking the category being researched (resrch2_list_click
      // 0x6173f3 sets only its flag): research stays on it until the next
      // tick, which pays nothing and picks from the checked categories
      // (0x41f1ba -> 0x5b09f6); a program already begun is taken up again
      // when picked
      {
        const int c0 = br->currentCategory;
        const bool begun = p->progress > 0;
        br->enabled[c0] = false;
        check("unchecking the category being researched: research stays on it until the tick",
              br->currentCategory == c0 && rs.current(*br) == p);
        std::vector<double> asked2;
        rs.tickClock = 0;
        rs.advance(3.0f, [&](int branch, double amount) {
          if (branch == 0)
            asked2.push_back(amount);
          return true;
        });
        ResearchProgram *q = rs.current(*br);
        check("the tick pays nothing and moves it to a checked category (" + std::to_string(asked2.size()) + " asked)",
              asked2.empty() && br->currentCategory != c0 && (!q || (br->currentCategory >= 0 && br->enabled[br->currentCategory])));
        br->enabled[c0] = true;
        rs.pick(*br);
        check("checked again, a program already begun is picked up again", !begun || rs.current(*br) == p);
        p = rs.current(*br) ? rs.current(*br) : p;
      }
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
      // (the walls' price on tankWalls: zoo.exe 0x489f53, 0x4ef687 - from
      // initialHeight up, a step each; the tank starts at it)
      check("tank walls dragged up with $1: they don't move, nothing charged ($" + money(r.tankWalls) + ")",
            r.tankWalls == 0 && f.exhibit(tank)->wallSub == sub0);
      r = dragUp(step * 1.5, 3);
      check("with one step's price and a half: one step, that step charged ($" + money(r.tankWalls) + ", a step $" +
                money(step) + ")",
            std::abs(r.tankWalls - step) < 0.001 && f.exhibit(tank)->wallSub == sub0 + 1);
      const int sub1 = f.exhibit(tank)->wallSub;
      r = dragUp(1e6, 2);
      const int moved = f.exhibit(tank)->wallSub - sub1;
      check("with the money: as far as the mouse goes, each step charged (" + std::to_string(moved) + " steps, $" +
                money(r.tankWalls) + ")",
            moved > 1 && std::abs(r.tankWalls - moved * step) < 0.001);
      // Down again from above initialHeight: a step's price back each
      // (0x4ef687's negative price, spent as it is - tankModBuy 0x4691cd)
      world.cash = 1;
      world.beginWallDrag(tank, pressY);
      world.mouseMove(px, pressY + 200);
      r = world.mouseUp(px, pressY + 200);
      const int from = sub1 + moved, to = f.exhibit(tank)->wallSub;
      const int backSteps = std::max(from, 5) - std::max(to, 5); // (initialHeight 5: under it free)
      check("dragged back down with $1: a step's price back for each above initialHeight ($" + money(r.tankWalls) +
                ", " + std::to_string(from) + " -> " + std::to_string(to) + ")",
            to < from && backSteps > 0 && std::abs(r.tankWalls + backSteps * step) < 0.001);
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
          pathsLeft == 0 && world.getObjects().objects().size() == objects - 2 && std::lround(r.refund) == shown);
    // zoo.exe bulldozeMouse 0x509071: a few pixels across into the next
    // tile is a drag (ours had only the 16 pixels); back on the pressed
    // tile near the press, a click again. A right click mid drag (0x509870)
    // takes the rectangle back, the tool kept, the release taking nothing
    {
      for (int i = 0; i < 3; i++)
        map.setPath(bx + i, by, "dirtpath");
      int c0x, c0y, c1x, c1y;
      centre(bx, by, c0x, c0y);
      centre(bx + 1, by, c1x, c1y);
      const int mx = (c0x + c1x) / 2, my = (c0y + c1y) / 2;
      const int ax = mx + (c0x - mx) / 4, ay = my + (c0y - my) / 4; // (bx's side of the edge)
      const int qx2 = mx + (c1x - mx) / 4, qy2 = my + (c1y - my) / 4; // (bx + 1's)
      world.mouseMove(ax, ay);
      world.mouseDown(ax, ay);
      world.mouseMove(qx2, qy2);
      const bool tileDrag = world.bulldozeDragging();
      world.mouseMove(ax, ay);
      const bool backClick = !world.bulldozeDragging() && world.bulldozeDragCount() == 0;
      r = world.mouseUp(ax, ay);
      check("the bulldozer: " + std::to_string(std::abs(qx2 - ax)) + "," + std::to_string(std::abs(qy2 - ay)) +
                " px into the next tile is a drag; back by the press, a click (0x509071)",
            tileDrag && backClick && std::abs(qx2 - ax) <= Original::bulldozeClickPixels &&
                std::abs(qy2 - ay) <= Original::bulldozeClickPixels);
      check("let go there: the pressed tile's path alone taken", !map.isPath(bx, by) && map.isPath(bx + 1, by) &&
                                                                    map.isPath(bx + 2, by) && r.refund > 0);
      int ex, ey;
      centre(bx + 1, by, px, py);
      centre(bx + 3, by + 3, ex, ey);
      world.mouseMove(px, py);
      world.mouseDown(px, py);
      world.mouseMove(ex, ey);
      const bool was = world.bulldozeDragging();
      const bool took = world.cancelTool();
      world.mouseMove(ex + 20, ey + 20);
      const bool stillOff = !world.bulldozeDragging() && world.bulldozeDragCount() == 0;
      r = world.mouseUp(ex + 20, ey + 20);
      check("a right click mid bulldozer drag takes it back, the tool kept; the release takes nothing (0x509870)",
            was && took && stillOff && world.isBulldozer() && r.refund == 0 && map.isPath(bx + 1, by) &&
                map.isPath(bx + 2, by));
      map.setPath(bx + 1, by, "");
      map.setPath(bx + 2, by, "");
    }
    // A click on the snack stand: a building (cNeedsConfirm 1) is asked
    // about first, "Are you sure you want to sell Snack Stand 1? Refund:
    // $N" (zoo.exe deleteHovered 0x509176 -> sellObject 0x50a432 ->
    // removeObject 0x50a0b0 -> sellConfirmDialog 0x50bf8d); Yes, gone with
    // its 80%
    if (building) {
      const size_t before = world.getObjects().objects().size();
      centre(bx + 1, by + 4, px, py);
      world.mouseMove(px, py);
      world.mouseDown(px, py);
      r = world.mouseUp(px, py);
      const bool asked = !r.askSell.empty() && r.refund == 0 && world.getObjects().objects().size() == before;
      const std::string expect = rm->getString(154).substr(0, rm->getString(154).find("%s"));
      check("a building bulldozed is asked about first (\"" + r.askSell + "\")",
            asked && r.askSell.rfind(expect, 0) == 0 &&
                r.askSell.find("Refund: $" + std::to_string(static_cast<int>(building->cost * Original::sellFactor))) != std::string::npos);
      const float back = world.confirmSell();
      check("Yes: the building gone, $" + std::to_string(back) + " back (80% of $" + std::to_string(building->cost) + ")",
            world.getObjects().objects().size() == before - 1 && std::abs(back - building->cost * Original::sellFactor) < 1e-3f);
    }
    check("buildings and the tank filter ask (cNeedsConfirm 1), a trash can doesn't",
          Original::sellNeedsConfirm(rm, "scenery/building/vendfood.ai") == Original::confirmSell() &&
              Original::sellNeedsConfirm(rm, "scenery/other/filter1.ai") == Original::confirmSell() &&
              !Original::sellNeedsConfirm(rm, "scenery/other/trshcan.ai"));
    check("the question's refund cut toward zero and grouped (\"" + Original::sellQuestion(rm, "X", 1234.9f) + "\")",
          Original::sellQuestion(rm, "X", 1234.9f).find("Refund: $1,234") != std::string::npos);
    world.setBulldozer(false);
  }

  // Money back is 80% of the price as it is (ZTEntity::objectSellValue
  // 0x41ec7f; removeObject 0x50a0b0 pays the float): an $18 African path
  // tile gives $14.40 back, not $14
  {
    const int tx = X + 8, ty = Y + 2;
    map.setPath(tx, ty, "afpath");
    world.setBulldozer(true);
    int qx, qy;
    centre(tx, ty, qx, qy);
    world.mouseMove(qx, qy);
    world.mouseDown(qx, qy);
    World::ToolResult r = world.mouseUp(qx, qy);
    world.setBulldozer(false);
    check("an $18 path tile bulldozed: $" + std::to_string(r.refund) + " back (80% as it is)",
          !map.isPath(tx, ty) && std::abs(r.refund - 18 * Original::sellFactor) < 1e-4f);
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

  // A path across a lake tile (zoo.exe placeObject 0x4d9088, #264): the
  // tile painted dirt (terrain 3) first, priced the path's and the dirt's
  // ($10 + $20, placementCheck 0x4f885e); Undo puts the lake back
  {
    const int ly = Y + 8;
    MapTile *lake = map.getTileMutable(X + 2, ly);
    const int was = lake ? lake->terrainType : -1;
    if (lake)
      lake->terrainType = 9; // (fresh water)
    world.setPathTool("dirtpath", 10);
    int x0, y0, x1 = 0, y1 = 0, hx, hy;
    centre(X + 2, ly, hx, hy);
    world.mouseMove(hx, hy);
    const int hover = world.hoverCost();
    centre(X, ly, x0, y0);
    world.mouseMove(x0, y0);
    world.mouseDown(x0, y0);
    for (int i = 1; i <= 4; i++) {
      centre(X + i, ly, x1, y1);
      world.mouseMove(x1, y1);
    }
    World::ToolResult r = world.mouseUp(x1, y1);
    int laid = 0;
    for (int i = 0; i <= 4; i++)
      laid += map.isPath(X + i, ly) ? 1 : 0;
    const int now = lake ? lake->terrainType : -1;
    check("a path over a lake tile: hover $" + std::to_string(hover) + ", five laid (" + std::to_string(laid) +
              "), the lake painted dirt (terrain " + std::to_string(now) + "), $" + std::to_string(r.cost) +
              " ($50 and the dirt's $20)",
          hover == 30 && laid == 5 && now == Original::pathOnLakeTerrain && r.cost == 70);
    const World::UndoMoney back = world.undo();
    int left = 0;
    for (int i = 0; i <= 4; i++)
      left += map.isPath(X + i, ly) ? 1 : 0;
    check("Undo: the paths gone, the lake back (terrain " + std::to_string(lake ? lake->terrainType : -1) + "), $" +
              std::to_string(back.construction) + " back",
          left == 0 && lake && lake->terrainType == 9 && std::lround(back.construction) == 70);
    if (lake)
      lake->terrainType = static_cast<uint8_t>(was);
    map.touch();
    world.setPathTool("", 0);
  }

  // Freeform's awards, donations and unlocks (awards.scn, donation.scn,
  // unlock.scn, as extragoals): every rule they use is one the game measures
  // (zoo.exe goalRuleMeasure 0x41d665) - Dinosaur Digs' dinosaur awards
  // (13, 14, 15, 18) are rule 6/1 and 6/5, by cFamily and cEra (0x41da81)
  if (g_financeGoalHooks.measures) {
    for (const char *file : {"freeform/awards.scn", "freeform/donation.scn", "freeform/unlock.scn"}) {
      Goals f;
      f.load(rm, file);
      std::set<std::string> unknown;
      for (const Goals::Goal &g : f.all())
        if (!g_financeGoalHooks.measures(g))
          unknown.insert(std::to_string(g.rulea) + "/" + std::to_string(g.ruleb));
      std::string list;
      for (const std::string &u : unknown)
        list += " " + u;
      check(std::string(file) + ": " + std::to_string(f.all().size()) + " goals, every rule measured" +
                (list.empty() ? "" : " - not:" + list),
            !f.all().empty() && unknown.empty());
    }
  } else
    check("the game's goal hooks were handed to the checks", false);

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

  // Staff wages (zoo.exe ZTStaff::wage 0x5ad1e8: the type's cMonthlyCost,
  // its default the cPurchaseCost - ZTStaffType::setDefaults 0x4c3081): a
  // keeper is paid its cPurchaseCost a month, the DRT helicopter its
  // cMonthlyCost ($2,000)
  {
    const auto &types = world.getStaff().types();
    int keeper = -1, heli = -1;
    for (size_t i = 0; i < types.size(); i++) {
      if (types[i].key == "keeper") keeper = static_cast<int>(i);
      if (types[i].key == "helicopt") heli = static_cast<int>(i);
    }
    check("a keeper's wage is its cPurchaseCost ($" + std::to_string(keeper >= 0 ? types[keeper].wage : -1) + ")",
          keeper >= 0 && types[keeper].wage == types[keeper].salary && types[keeper].wage > 0);
    check("the helicopter's is its cMonthlyCost ($" + std::to_string(heli >= 0 ? types[heli].wage : -1) + ")",
          heli < 0 || types[heli].wage == 2000);
  }

  // A building's months open (#122; zoo.exe buildingInfoNumbers 0x488446,
  // the building list 0x5317c5): whole days since it opened, + 29.9 cut,
  // / 30, at least 1 - days 0-30 one month, 31-60 two. The opening day is
  // kept in our save and in the original's part (ZTBuilding +0x180, a
  // FILETIME after +0x198: ZTBuilding::save 0x477147)
  {
    using Original::monthsOpen;
    check("months open: day 0 and day 30 are 1, day 31 is 2, day 60 is 2, day 61 is 3 (" +
              std::to_string(monthsOpen(0, 0)) + " " + std::to_string(monthsOpen(0, 30)) + " " +
              std::to_string(monthsOpen(0, 31)) + " " + std::to_string(monthsOpen(0, 60)) + " " +
              std::to_string(monthsOpen(0, 61)) + ")",
          monthsOpen(0, 0) == 1 && monthsOpen(0, 30) == 1 && monthsOpen(0, 30.99) == 1 &&
              monthsOpen(0, 31) == 2 && monthsOpen(0, 60) == 2 && monthsOpen(0, 61) == 3);
    check("whole days only: opened at 10.5, on 41.4 is 30 days, 1 month (" + std::to_string(monthsOpen(10.5, 41.4)) + ")",
          monthsOpen(10.5, 41.4) == 1 && monthsOpen(10.5, 41.6) == 2);
    PlacedObjects &po = world.objectsMutable();
    const double before = po.today;
    po.today = 45.25;
    const bool added = po.add("scenery/building/bgrstnd.ai", 30.5f, 30.5f, 4);
    po.today = before;
    PlacedObjects::Object *b = added ? po.last() : nullptr;
    const int id = b ? b->id : -1;
    check("a building put down on day 45.25 opens that day", b && b->openedDay == 45.25);
    if (b) {
      b->visitorsNow = 7;
      b->visitorsLast = 3;
    }
    world.reindex();
    const std::string file = out + "/opened-day.zoo";
    const bool written = b && SaveGame::save(file, world, sim, hud, "freeform/ff03.scn");
    double inOriginal = -1;
    uint32_t lastInOriginal = 0, nowInOriginal = 0;
    if (written) {
      std::ifstream in(file, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      AssetBuffer buffer;
      buffer.data = bytes.data();
      buffer.size = bytes.size();
      ZooReader z;
      if (z.load(buffer))
        for (const ZooReader::ZooObject &o : z.getObjects())
          if (o.className == "building" && o.typeName == "bgrstnd" && o.commonEnd &&
              o.commonEnd + 20 <= o.payload.size()) {
            uint32_t users = 0;
            std::memcpy(&users, o.payload.data() + o.commonEnd + 16, 4);
            const size_t at = o.commonEnd + 20 + size_t(users) * 8 + 20;
            uint64_t ft = 0;
            if (at + 8 <= o.payload.size())
              std::memcpy(&ft, o.payload.data() + at, 8);
            inOriginal = (double(ft) - double(Original::fileTime2001)) / double(Original::fileTimeDay);
            if (at + 8 <= o.payload.size()) {
              std::memcpy(&lastInOriginal, o.payload.data() + at - 8, 4);
              std::memcpy(&nowInOriginal, o.payload.data() + at - 4, 4);
            }
          }
      buffer.data = nullptr;
    }
    check("saved, the original's part holds it as a FILETIME (day " + std::to_string(inOriginal) + ")",
          written && std::abs(inOriginal - 45.25) < 1e-6);
    // (its visitors: +0x194 last month, +0x198 this month - a guest going in
    // adds to +0x198, 0x42ce0a; rollMonthlyCounters 0x4842b7 moves it on)
    check("and its visitors, last month's (+0x194) then this month's (+0x198): " + std::to_string(lastInOriginal) + ", " +
              std::to_string(nowInOriginal),
          written && lastInOriginal == 3 && nowInOriginal == 7);
    if (!written)
      report << "SAVE ERROR " << SaveGame::lastError() << "\n";
    if (b)
      b->openedDay = 0;
    const bool loaded = written && SaveGame::load(file, world, sim, hud);
    const PlacedObjects::Object *again = loaded ? world.objectsMutable().byId(id) : nullptr;
    check("loaded, 7 visitors this month and 3 last (" + std::to_string(again ? again->visitorsNow : -1) + ", " +
              std::to_string(again ? again->visitorsLast : -1) + ")",
          again && again->visitorsNow == 7 && again->visitorsLast == 3);
    check("loaded, it opened on day 45.25 still (" + std::to_string(again ? again->openedDay : -1.0) + ")",
          again && again->openedDay == 45.25);
    if (written && !loaded)
      report << "LOAD ERROR " << SaveGame::lastError() << "\n";
    std::error_code ec;
    std::filesystem::remove(file, ec);
  }

  // A tank filter's months (#188): built (+0x178) and last serviced (+0x180)
  // as days, its months in operation (+0x188) counted every 3 s from the
  // built day - whole days + 29.9, cut, / 30 (ZTTankFilter::update
  // 0x497e0f) - and when they move on its upkeep rolls; serviced, the day's
  // today (serviceAndDate 0x595cad); kept in our save and in the original's
  // part (ZTTankFilter::save 0x615b31)
  {
    using Original::monthsSince;
    check("filter months: day 0 is 0, days 1 and 30 are 1, day 31 is 2 (" + std::to_string(monthsSince(0, 0)) + " " +
              std::to_string(monthsSince(0, 1)) + " " + std::to_string(monthsSince(0, 30)) + " " +
              std::to_string(monthsSince(0, 31)) + ")",
          monthsSince(0, 0) == 0 && monthsSince(0, 1) == 1 && monthsSince(0, 30) == 1 && monthsSince(0, 30.9) == 1 &&
              monthsSince(0, 31) == 2 && monthsSince(0, 61) == 3);
    const double before = f.today;
    f.today = 100.0;
    f.selectFilter("scenery/other/filter1.ai");
    bool placed = false;
    for (int y = Y - 3; y < Y + 8 && !placed; ++y)
      for (int x = X + 10; x < X + 22 && !placed; ++x)
        if (f.filterFit(x, y, map) == Fences::Fit::Ok)
          placed = f.placeFilter(x, y, map);
    const int index = static_cast<int>(f.filters().size()) - 1;
    Fences::Filter *pump = placed ? f.filter(index) : nullptr;
    check("a filter put down on day 100: built and serviced that day, 1 month in operation",
          pump && pump->builtDay == 100.0 && pump->servicedDay == 100.0 && pump->monthsRunning == 1);
    if (pump) {
      pump->filterClock = 0;
      pump->upkeepCurrent = 40;
      pump->upkeepLast = 0;
      f.today = 130.9;
      f.update(Original::filterMonthCheckSeconds + 0.1f);
      pump = f.filter(index);
      check("day 130.9 (30 whole days): still 1 month, its upkeep where it was ($" + money(pump->upkeepCurrent) + ")",
            pump->monthsRunning == 1 && pump->upkeepCurrent == 40);
      pump->filterClock = 0;
      f.today = 131.0;
      f.update(1.0f);
      pump = f.filter(index);
      check("day 131 a second later: not counted yet (every 3 s)", pump->monthsRunning == 1);
      pump->filterClock = 0;
      f.update(Original::filterMonthCheckSeconds);
      pump = f.filter(index);
      check("3 s on: 2 months, this month's $" + money(pump->upkeepLast) + " now last month's, this month's $" +
                money(pump->upkeepCurrent),
            pump->monthsRunning == 2 && pump->upkeepLast == 40 && pump->upkeepCurrent == 0);
      f.today = 140.5;
      f.serviceFilter(index);
      pump = f.filter(index);
      check("serviced on day 140.5: its last service that day (" + std::to_string(pump->servicedDay) + ")",
            pump->servicedDay == 140.5);
    }
    f.today = before;
    world.reindex();
    const std::string file = out + "/filter-days.zoo";
    const bool written = pump && SaveGame::save(file, world, sim, hud, "freeform/ff03.scn");
    double builtInOriginal = -1, servicedInOriginal = -1;
    uint32_t periodInOriginal = 0;
    if (written) {
      std::ifstream in(file, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      AssetBuffer buffer;
      buffer.data = bytes.data();
      buffer.size = bytes.size();
      ZooReader z;
      if (z.load(buffer))
        for (const ZooReader::ZooObject &o : z.getObjects())
          if (o.className == "filter") {
            builtInOriginal = (double(o.filterBuilt) - double(Original::fileTime2001)) / double(Original::fileTimeDay);
            servicedInOriginal = (double(o.filterServiced) - double(Original::fileTime2001)) / double(Original::fileTimeDay);
            periodInOriginal = o.filterPeriod;
          }
      buffer.data = nullptr;
    }
    check("saved, the original's part holds built day " + std::to_string(builtInOriginal) + ", serviced " +
              std::to_string(servicedInOriginal) + ", " + std::to_string(periodInOriginal) + " months in operation",
          written && std::abs(builtInOriginal - 100.0) < 1e-6 && std::abs(servicedInOriginal - 140.5) < 1e-6 &&
              periodInOriginal == 2);
    if (pump) {
      pump->builtDay = pump->servicedDay = 0;
      pump->monthsRunning = 9;
    }
    const bool loaded = written && SaveGame::load(file, world, sim, hud);
    const Fences::Filter *again = loaded && !world.getFences().filters().empty() ? &world.getFences().filters().back() : nullptr;
    check("loaded, built day 100, serviced 140.5 and 2 months still (" +
              (again ? std::to_string(again->builtDay) + ", " + std::to_string(again->servicedDay) + ", " +
                           std::to_string(again->monthsRunning)
                     : std::string("none")) + ")",
          again && again->builtDay == 100.0 && again->servicedDay == 140.5 && again->monthsRunning == 2);
    if (written && !loaded)
      report << "LOAD ERROR " << SaveGame::lastError() << "\n";
    std::error_code ec;
    std::filesystem::remove(file, ec);
  }

  // Zoo Status' graphs (zoo.exe zooGraphsRefresh 0x52ff4d): this month and
  // the 11 before it, never more - after 15 months, 12 points each; after
  // 3, 3
  {
    auto pointsAfter = [&](int months) {
      ZooSim books;
      books.start(1000, 50);
      while (static_cast<int>(books.history().size()) < months)
        books.update(ZooSim::kSecondsPerDay);
      hud.setSim(&books);
      hud.setZooInfo(hud.zooInfo());
      size_t least = 99, most = 0;
      for (int id : {4150, 13978, 13967, 13965})
        if (const UiGraph *g = dynamic_cast<const UiGraph *>(hud.getElementById(id))) {
          least = std::min(least, g->pointCount());
          most = std::max(most, g->pointCount());
        }
      hud.setSim(&sim);
      hud.setZooInfo(hud.zooInfo());
      return least == most ? static_cast<int>(least) : -1;
    };
    const int fifteen = pointsAfter(15), three = pointsAfter(3);
    check("Zoo Status' graphs show the last 12 months at most (" + std::to_string(fifteen) + " after 15 months, " +
              std::to_string(three) + " after 3)",
          fifteen == 12 && three == 3);
  }

  // Zoo Status' attendance graph (13965, graphSeriesD 0x530bf2) is books
  // row 25, the guests in the zoo at the month's last rating update
  // (zooRatingUpdate 0x41f881) - not the month's admissions (row 4); a
  // month's count is kept in a save (v19) (#209)
  {
    ZooSim books;
    books.start(1000, 50);
    books.count(ZooSim::Admissions, 40);
    books.countGuests(12);
    books.countGuests(9); // (set, not added: the last update's count)
    while (books.history().size() < 2)
      books.update(ZooSim::kSecondsPerDay);
    books.count(ZooSim::Admissions, 5);
    books.countGuests(21);
    hud.setSim(&books);
    hud.setZooInfo(hud.zooInfo());
    std::string seen;
    bool plotted = false;
    if (const UiGraph *g = dynamic_cast<const UiGraph *>(hud.getElementById(13965))) {
      const auto &p = g->pointList();
      for (const auto &point : p)
        seen += std::to_string(std::lround(point.value)) + " ";
      plotted = p.size() == 2 && p[0].value == 9 && p[1].value == 21;
    }
    hud.setSim(&sim);
    hud.setZooInfo(hud.zooInfo());
    check("Zoo Status' attendance graph plots the guests in the zoo, not those admitted (" + seen + "; want 9 21)",
          plotted);
    // (saved: our months keep it, and the original's own row 25 has it)
    sim.countGuests(17);
    const int month = sim.month();
    const std::string file = out + "/guests-row25.zoo";
    const bool written = SaveGame::save(file, world, sim, hud, "freeform/ff03.scn");
    double inOriginal = -1;
    if (written) {
      std::ifstream in(file, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      AssetBuffer buffer;
      buffer.data = bytes.data();
      buffer.size = bytes.size();
      ZooReader z;
      if (z.load(buffer) && z.getBooks().monthly.size() > 25)
        inOriginal = z.getBooks().monthly[25][month];
      buffer.data = nullptr;
    }
    sim.countGuests(0);
    const bool loaded = written && SaveGame::load(file, world, sim, hud);
    const double back = loaded && !sim.history().empty() ? sim.history().back().guests : -1;
    check("a month's guests in the zoo are saved (ours " + std::to_string(back) + ", the original's row 25 " +
              std::to_string(inOriginal) + "; want 17)",
          back == 17 && inOriginal == 17);
    if (written && !loaded)
      report << "LOAD ERROR " << SaveGame::lastError() << "\n";
    std::error_code ec;
    std::filesystem::remove(file, ec);
  }

  // Zoo Status' admission arrows (zoo.exe 0x530329): the up arrow greyed at
  // cMaxAdultAdmissionPrice, the down arrow at cMinAdultAdmissionPrice, both
  // on in between
  {
    const UiGameScreen::ZooInfo before = hud.zooInfo();
    const Original::Economy &e = Original::economy();
    auto arrows = [&](double price, bool upOff, bool downOff) {
      UiGameScreen::ZooInfo info = before;
      info.admission = price;
      hud.setZooInfo(info);
      const UiElement *up = hud.getElementById(4195), *down = hud.getElementById(4191);
      return up && down && up->isDisabled() == upOff && down->isDisabled() == downOff;
    };
    const bool top = arrows(e.maxAdmission, true, false), bottom = arrows(e.minAdmission, false, true),
               middle = arrows((e.minAdmission + e.maxAdmission) / 2, false, false);
    hud.setZooInfo(before);
    // Zoo Status open: its counts redone every 10 s, not while paused, at
    // once for a building placed (zoo.exe 0x401644, 0x41739a)
    {
      const bool wasOpen = hud.isPanelOpen(14);
      hud.setPanelOpen(14, true);
      auto shown = [&]() {
        const UiText *t = dynamic_cast<const UiText *>(hud.getElementById(4121));
        return t ? t->getText() : std::string("?");
      };
      UiGameScreen::ZooInfo info = before;
      const uint64_t t0 = 1000000;
      hud.zooStatusTick(t0, false); // (due: shown now, next at +10 s)
      info.guestCount = before.guestCount + 7;
      hud.setZooInfo(info);
      const std::string waiting = shown();
      hud.zooStatusTick(t0 + 5000, false);
      const std::string at5 = shown();
      hud.zooStatusTick(t0 + 10000, true);
      const std::string paused = shown();
      hud.zooStatusTick(t0 + 10000, false);
      const std::string at10 = shown();
      info.guestCount = before.guestCount + 9;
      hud.setZooInfo(info);
      hud.forceZooStatus();
      const std::string forced = shown();
      const std::string was = std::to_string(before.guestCount), now7 = std::to_string(before.guestCount + 7);
      check("Zoo Status open: a new count waits (" + waiting + ", " + at5 + " at 5 s, " + paused + " paused), shown at 10 s (" +
                at10 + "), at once for a building placed (" + forced + ") (0x401644)",
            waiting == was && at5 == was && paused == was && at10 == now7 &&
                forced == std::to_string(before.guestCount + 9));
      hud.setPanelOpen(14, wasOpen);
      hud.setZooInfo(before);
    }
    check("the admission arrows grey out at the highest and lowest price (top " + std::to_string(top) + ", bottom " +
              std::to_string(bottom) + ", between " + std::to_string(middle) + ")",
          top && bottom && middle);
  }

  // The message bar's repeats and its 25 (zoo.exe 0x49c6d7, the bar's
  // constructor 0x520a08): a kind 0-2 message already kept from less than
  // [UI] minimumMessageInterval seconds ago is dropped, kind 3 and up isn't,
  // and only the newest 25 are kept
  {
    const size_t start = hud.messageCount();
    hud.postMessage("finance check: advice", 2);
    hud.postMessage("finance check: advice", 2);
    check("a kind-2 message posted twice within " + std::to_string(UiGameScreen::minimumMessageInterval) + " s: kept once (" +
              std::to_string(hud.messageCount() - start) + ")",
          hud.messageCount() - start == 1);
    hud.postMessage("finance check: news", 0);
    hud.postMessage("finance check: news", 0);
    check("a kind-0 one too (" + std::to_string(hud.messageCount() - start - 1) + ")", hud.messageCount() - start == 2);
    hud.postMessage("finance check: kind 3", 3);
    hud.postMessage("finance check: kind 3", 3);
    check("a kind-3 message posted twice: kept twice (" + std::to_string(hud.messageCount() - start - 2) + ")",
          hud.messageCount() - start == 4);
    const int interval = UiGameScreen::minimumMessageInterval;
    UiGameScreen::minimumMessageInterval = 0;
    hud.postMessage("finance check: advice", 2);
    UiGameScreen::minimumMessageInterval = interval;
    check("with minimumMessageInterval 0 the kind-2 one is taken again", hud.messageCount() - start == 5);
    for (int i = 0; i < 30; i++)
      hud.postMessage("finance check: message " + std::to_string(i), 1);
    check("30 more posted: " + std::to_string(hud.messageCount()) + " kept, the newest first (" + hud.messageText(0) + ")",
          hud.messageCount() == static_cast<size_t>(Original::messagesKept) &&
              hud.messageText(0) == "finance check: message 29");
  }

  report << "\n" << (checks - failures) << "/" << checks << " pass\n";
  SDL_Log("[Finance] %d/%d pass", checks - failures, checks);
  return failures ? 1 : 0;
}
