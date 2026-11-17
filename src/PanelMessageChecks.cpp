// Tank Adjustment's prices and limits (#238: zoo.exe tankmod_Buttons_click
// 0x469128, its refresh 0x5ae509, the wall price 0x4ef687, the walls' least
// 0x5af226) and the animals' messages (#239: unitMessageText 0x49cfeb - a
// female's text from the number + 30000 - and what postUnitMessage 0x49d18e
// posts: 10029, 10030, 10031). Run from runParityChecks (the flat Basic
// Grass test zoo); its fixture taken down after
#include "World.hpp"
#include "Animation.hpp"
#include "Features.hpp"
#include "OriginalConstants.hpp"
#include "ResourceManager.hpp"
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

std::string runPanelMessageChecks(World &world, ResourceManager *rm, int &passed, int &total) {
  auto &map = world.getMap();
  auto &f = world.getFences();
  auto &a = world.getAnimals();
  std::ostringstream out;
  auto check = [&](const std::string &name, bool ok) {
    ++total;
    passed += ok;
    out << (ok ? "PASS " : "FAIL ") << name << "\n";
  };
  // Room: 10 x 6 tiles in the zoo, no exhibit, nothing on them
  int bx = -1, by = -1;
  for (int y = 2; y < map.getHeight() - 8 && bx < 0; ++y)
    for (int x = 2; x < map.getWidth() - 12 && bx < 0; ++x) {
      bool ok = true;
      for (int yy = y - 1; yy <= y + 6 && ok; ++yy)
        for (int xx = x - 1; xx <= x + 10 && ok; ++xx)
          ok = f.exhibitAt(xx, yy) < 0 && f.insideZoo(xx, yy) && !world.getIndex().blocked(xx, yy) && !map.isPath(xx, yy);
      if (ok) {
        bx = x;
        by = y;
      }
    }
  out << "PANELS room at " << bx << "," << by << "\n";
  check("#238/#239: room for the tank and paddock fixture", bx >= 0);
  if (bx < 0)
    return out.str();
  // A 2 x 2 tank (atltank) and a 3 x 3 chain-link paddock beside it
  std::vector<Fences::Edge> placed;
  int drag = 950;
  auto rectangle = [&](int x, int y, int w, int h, int type, const std::string &name) {
    for (int k = 0; k < w; ++k) {
      placed.push_back({true, x + k, y});
      placed.push_back({true, x + k, y + h});
      f.place(placed[placed.size() - 2], type, drag, map);
      f.place(placed.back(), type, drag + 1, map);
    }
    for (int k = 0; k < h; ++k) {
      placed.push_back({false, x, y + k});
      placed.push_back({false, x + w, y + k});
      f.place(placed[placed.size() - 2], type, drag + 2, map);
      f.place(placed.back(), type, drag + 3, map);
    }
    drag += 4;
    for (int id : f.updateExhibits(map, 1, 0, 1))
      f.finishExhibit(id, name, map);
    return f.exhibitAt(x, y);
  };
  const int tankId = rectangle(bx, by, 2, 2, f.typeIndex("atltank"), "Panel tank");
  const int padId = rectangle(bx + 5, by, 3, 3, f.typeIndex("chainlnk"), "Panel paddock");
  world.reindex();
  Fences::Exhibit *tank = f.exhibit(tankId);
  check("#238: the fixture's 2 x 2 tank and paddock", tank && tank->tank && f.exhibit(padId));
  if (tank) {
    const double step = f.wallStepCost(tankId);
    out << "PANELS tank: walls " << tank->wallSub << ", a step $" << step << ", base step $" << f.baseStepCost(tankId)
        << ", least " << f.minWallHeight(tankId) << "\n";
    // zoo.exe 0x4ef687: a step up from initialHeight (5) a step's price;
    // under it free both ways; down from above it the step's price back
    check("#238: walls 5 -> 6 cost a step (0x4ef687)", std::abs(f.wallChangeCost(tankId, 5, 6) - step) < 0.001);
    check("#238: walls 6 -> 5 give a step back (0x4ef687, tankModBuy 0x4691cd)",
          std::abs(f.wallChangeCost(tankId, 6, 5) + step) < 0.001);
    check("#238: walls under initialHeight free both ways (5 -> 4, 4 -> 5, 3 -> 2)",
          f.wallChangeCost(tankId, 5, 4) == 0 && f.wallChangeCost(tankId, 4, 5) == 0 && f.wallChangeCost(tankId, 3, 2) == 0);
    check("#238: walls 4 -> 7 cost two steps (from initialHeight up)",
          std::abs(f.wallChangeCost(tankId, 4, 7) - 2 * step) < 0.001);
    check("#238: a base step is the tiles x 8 either way ($32, 0x459b19 +0x1a4)", f.baseStepCost(tankId) == 32);
    // zoo.exe 0x5af226: an empty tank's walls go down to 2, no further
    while (f.canAdjustWall(tankId, -1))
      f.adjustWall(tankId, -1);
    check("#238: an empty tank's walls go down to 2, not 1 (0x5af226)", tank->wallSub == 2);
    // ... and with an orca in it (cDepth 3) to 4: its depth less waterOffset
    const int orca = a.typeOfFile("animals/orca.ai");
    const int swimmer = orca >= 0 ? a.adopt(orca, false, bx + 1.0f, by + 1.0f, map, f, true) : -1;
    check("#238: with an orca in it (cDepth 3) the least is 4 (0x5af226, 0x5af19b)",
          swimmer >= 0 && f.minWallHeight(tankId) == 4);
    // (walls under the least come up to it on the next step: 0x5af39e)
    f.adjustWall(tankId, 1);
    const int raised = tank->wallSub;
    f.adjustWall(tankId, 1);
    while (f.canAdjustWall(tankId, -1))
      f.adjustWall(tankId, -1);
    check("#238: the walls stop at 4 over the orca, and come up to it from under (0x5af39e)",
          raised == 4 && tank->wallSub == 4);
    a.clear();
    while (f.canAdjustWall(tankId, 1) && tank->wallSub < 5)
      f.adjustWall(tankId, 1);
  }
  // #239: a female's message from the number + 30000 (zoo.exe 0x49cfeb).
  // (English has the same words for most; the first number whose + 30000
  // text differs, if any, shows the pick)
  const int zebra = a.typeOfFile("animals/zebra.ai");
  const int z = zebra >= 0 ? a.adopt(zebra, true, bx + 6.5f, by + 1.5f, map, f, true) : -1;
  Animals::Member *zm = a.member(z);
  if (zm)
    zm->exhibit = padId;
  check("#239: a female zebra in the paddock", zm && zm->female);
  if (zm && rm) {
    auto fill = [&](int id) {
      std::string s = rm->getString(id);
      if (size_t at = s.find("%s"); at != std::string::npos)
        s.replace(at, 2, zm->name);
      return s;
    };
    int differing = 0;
    for (int id = 10000; id < 20000 && !differing; ++id) {
      const std::string base = rm->getString(id), her = rm->getString(id + 30000);
      if (!her.empty() && !base.empty() && her != base && base.find("%s") != std::string::npos)
        differing = id;
    }
    out << "PANELS first text whose female form differs: " << differing << "\n";
    check("#239: a female's text is the number + 30000's (0x49cfeb)",
          a.unitText(*zm, 10007) == fill(40007) && (!differing || a.unitText(*zm, differing) == fill(differing + 30000)));
    zm->female = false;
    check("#239: a male's text is the number's own", !differing || a.unitText(*zm, differing) == fill(differing));
    zm->female = true;
    check("#239: no + 30000 text (40322 hatched): the number's own (ours)",
          rm->getString(40322).empty() && a.unitText(*zm, 10322) == fill(10322));
    // The messages through postUnitMessage 0x49d18e, in its status tick
    const Animals::Type &t = a.types()[zm->type];
    auto tickWith = [&](auto set) {
      zm = a.member(z);
      zm->habitatCheck = 100;
      zm->hungerCheck = 100;
      zm->healthCheck = 100;
      zm->sick = false;
      zm->starve = 0;
      zm->takenAway = false;
      zm->noFoodSeen = false;
      zm->hunger = 0;
      zm->escaped = false;
      zm->exhibit = padId;
      set(*zm);
      a.takeNotices();
      a.tick(*zm, map, f);
      std::vector<std::string> said;
      for (const auto &n : a.takeNotices())
        said.push_back(n.text);
      return said;
    };
    auto has = [](const std::vector<std::string> &said, const std::string &s) {
      for (const auto &x : said)
        if (x == s)
          return true;
      return false;
    };
    auto first = tickWith([&](Animals::Member &m) {
      m.hunger = 100;
      m.hungerCheck = 0;
    });
    auto again = tickWith([&](Animals::Member &m) {
      m.hunger = 100;
      m.noFoodSeen = true;
      m.hungerCheck = 0;
    });
    check("#239: hungry with no food: nothing the first check, then \"" + fill(40030) + "\" (0x49c069)",
          !has(first, fill(40030)) && has(again, fill(40030)));
    auto sick = tickWith([&](Animals::Member &m) {
      m.sick = true;
      m.healthCheck = 0;
    });
    auto longSick = tickWith([&](Animals::Member &m) {
      m.sick = true;
      m.health = 0;
      m.hunger = 100;
      m.starve = t.sickTime;
      m.healthCheck = 0;
    });
    check("#239: sick: \"" + fill(40029) + "\" each health check, under cSickTime (0x4389aa)",
          has(sick, fill(40029)) && !has(longSick, fill(40029)));
    // #247: starving and sick for cSickTime health checks in a row, with no
    // keeper at its duties in its exhibit, it's taken away: 10010 (her
    // 40010), gone at the sweep (0x4389aa -> 0x4808ac -> 0x4fa78e(1),
    // 0x423797, 0x434df7); a keeper at work there, it stays. The check
    // before cSickTime counts on; a fed one's count goes back to 0
    const bool marked = zm->takenAway;
    const auto keepWas = a.keepersAtWork;
    a.keepersAtWork = [](int) { return 1; };
    auto tended = tickWith([&](Animals::Member &m) {
      m.sick = true;
      m.health = 0;
      m.hunger = 100;
      m.starve = t.sickTime;
      m.healthCheck = 0;
    });
    const bool tendedStays = !zm->takenAway;
    a.keepersAtWork = keepWas;
    auto counting = tickWith([&](Animals::Member &m) {
      m.sick = true;
      m.health = 0;
      m.hunger = 100;
      m.starve = t.sickTime - 2;
      m.healthCheck = 0;
    });
    const int counted = zm->starve;
    const bool notYet = !zm->takenAway;
    tickWith([&](Animals::Member &m) {
      m.sick = true;
      m.health = 0;
      m.hunger = 50;
      m.starve = t.sickTime - 1;
      m.healthCheck = 0;
    });
    const bool fedReset = zm->starve == 0 && !zm->takenAway && zm->health == float(t.minHits);
    out << "PANELS #247 taken away: marked " << marked << " says 40010 " << has(longSick, fill(40010))
        << " tended stays " << tendedStays << " counted " << counted << " fed reset " << fedReset << "\n";
    check("#247: sick and starving cSickTime checks running, no keeper at work there: \"" + fill(40010) +
              "\", taken away (0x4808ac, 0x4fa78e)",
          marked && has(longSick, fill(40010)) && !has(longSick, fill(40029)));
    check("#247: with a keeper at its duties there (habitat +0xf0) it stays; under cSickTime it counts on, "
          "\"very sick\"; fed, the count's 0 again and its health cMinHits (0x4808ac, 0x4806ea)",
          tendedStays && !has(tended, fill(40010)) && counted == t.sickTime - 1 && notYet &&
              has(counting, fill(40029)) && fedReset);
    auto &types = a.typeList;
    const int prefWas = types[zm->type].habitatPreference, pctWas = types[zm->type].pctHabitat;
    types[zm->type].habitatPreference = 1000;
    types[zm->type].pctHabitat = 100;
    auto dislikes = tickWith([&](Animals::Member &m) { m.habitatCheck = 0; });
    types[zm->type].habitatPreference = prefWas;
    types[zm->type].pctHabitat = pctWas;
    check("#239: far from what it wants: \"" + fill(40031) + "\" (0x438c47)", has(dislikes, fill(40031)));
    // (#247, last: taken away, it's gone at the sweep)
    tickWith([&](Animals::Member &m) {
      m.sick = true;
      m.health = 0;
      m.hunger = 100;
      m.starve = t.sickTime;
      m.healthCheck = 0;
    });
    const bool markedAgain = zm->takenAway;
    a.takeNotices();
    a.lifeEvents(map, f);
    const bool gone = a.member(z) == nullptr;
    out << "PANELS #247 the sweep: marked " << markedAgain << " gone " << gone << "\n";
    check("#247: taken away, it's gone at the sweep (0x434df7, 0x5bdda8)", markedAgain && gone);
  }
  // #242: fMove's time (its argument x 1000 ms, 0x425533) in plays of the
  // move's animation (0x43646e, 0x42514c): ceil(time / length), at least
  // one; a length of 0 with a time, two (0x5aa022). A mako's bEat
  // fMove(level, 1, submerged_eat): its eat's frames x frame time
  {
    using Original::timedMovePlays;
    const bool plays = timedMovePlays(1000, 800) == 2 && timedMovePlays(1000, 1000) == 1 &&
                       timedMovePlays(3000, 999) == 4 && timedMovePlays(0, 500) == 1 &&
                       timedMovePlays(1000, 0) == 2 && timedMovePlays(0, 0) == 1;
    bool lengthOk = true;
    std::string mako;
    if (const int mk = a.typeOfFile("animals/mako.ai"); mk >= 0) {
      Animals::Member mm;
      mm.type = mk;
      if (Animation *eat = a.art(mm, "submerged_eat")) {
        const float want = std::max(1, eat->frameCount()) * (eat->frameTimeMs() ? eat->frameTimeMs() : 100) / 1000.0f;
        lengthOk = std::abs(a.animSeconds(mm, "submerged_eat") - want) < 1e-4f;
        mako = std::to_string(eat->frameCount()) + " frames x " + std::to_string(eat->frameTimeMs()) + " ms, " +
               std::to_string(timedMovePlays(1000, static_cast<int>(std::lround(want * 1000)))) + " plays for 1 s";
      }
    }
    out << "PANELS #242 timed fMove: plays " << plays << " mako eat " << mako << "\n";
    check("#242: fMove's time as plays: ceil(time / its length), at least one, two for a 0 length (0x43646e, "
          "0x42514c, 0x5aa022)",
          plays && lengthOk);
  }
  // The name boxes (zoo.exe setName 0x449215, exhibits 0x5aedbd): a name
  // with nothing to see in it is not taken
  check("names: blank, spaces or a tab are not taken; one visible character is (0x449215, 0x4491ea)",
        !Original::nameTaken("") && !Original::nameTaken("   ") && !Original::nameTaken("\t ") &&
            Original::nameTaken("Zoo 1") && Original::nameTaken("  . ") && Original::nameTaken("\xe9"));
  // (the fixture taken up: the animals, every wall piece, the tank's ground)
  a.clear();
  {
    const bool groundWas = Features::tankGroundBack;
    Features::tankGroundBack = true;
    for (const Fences::Edge &e : placed)
      f.remove(e, map);
    Features::tankGroundBack = groundWas;
    f.updateExhibits(map, 1, 0, 1);
    f.takeLostTanks();
  }
  world.reindex();
  check("#238/#239: the fixture taken down (no exhibit left)", f.exhibits().empty());
  return out.str();
}
