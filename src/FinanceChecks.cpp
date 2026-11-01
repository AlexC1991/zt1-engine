// The zoo's money, as the original's (ZT_FINANCE_CHECKS=1 with --panel-shots
// <out> <map>, a flat map - ZT_FLAT_LAB=1: ff03):
// - funding (marketing, research) is paid by the 3 s tick only while the zoo
//   has the tick's cost: it never takes the cash below zero, and none is
//   paid with the cash below zero (zoo.exe 0x41f2ae, 0x41f1ba);
// - research pays only while a program's being worked on, and makes no
//   progress on a tick it couldn't pay for (0x41f1ba);
// - nothing costing more than the cash can be bought - an object, an
//   animal, a keeper, a fence or a path dragged: refused with 10002 "This
//   purchase cannot be made at this time due to insufficient funds."
//   (0x4fb08c, 0x48b843); with the money, the same click buys it.
// <out>/finance-checks.txt is the report.
#include "World.hpp"
#include "Research.hpp"
#include "ItemCatalog.hpp"
#include "ResourceManager.hpp"
#include "ZooSim.hpp"
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
      World::ToolResult r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("a $500 bench with $10: refused, insufficient funds (message " + std::to_string(r.messageId) + ")",
            r.messageId == World::kInsufficientFunds && r.cost == 0 && world.getObjects().objects().size() == before);
      world.cash = 1000;
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
      r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("a zebra with $1: refused, insufficient funds (message " + std::to_string(r.messageId) + ")",
            r.messageId == World::kInsufficientFunds && r.animalCost == 0 && a.members().empty());
      world.cash = 1e6;
      world.mouseMove(px, py);
      r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("with the money it's adopted ($" + std::to_string(r.animalCost) + ")", r.animalCost > 0 && a.members().size() == 1);
      world.setAnimalTool(-1, false);
    }
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
      World::ToolResult r = world.mouseDown(px, py);
      world.mouseUp(px, py);
      check("a keeper with $1: not hired, insufficient funds (message " + std::to_string(r.messageId) + ")",
            r.messageId == World::kInsufficientFunds && r.wage == 0 && st.members().empty());
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

  report << "\n" << (checks - failures) << "/" << checks << " pass\n";
  SDL_Log("[Finance] %d/%d pass", checks - failures, checks);
  return failures ? 1 : 0;
}
