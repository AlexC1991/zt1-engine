// ours: the themed celebrations' checks (Features::celebrations, #314), run
// by ZT_CELEBRATION_CHECKS=1 with --panel-shots <out> (ZT_FLAT_LAB=1):
// - each celebration happens on its own days and no other, news three days
//   ahead, the game's own art and sounds only;
// - switched on, the day's flyovers fly with their own sounds, guests and
//   buildings take the day's colours and give them back, nothing of them in
//   a saved file; switched off, nothing differs from the original;
// - the original's own dated events (25 Dec, 31 Oct, 15 Sep) are the same
//   with the switch on or off;
// - a year with the switch on and the same year with it off end with the same
//   books, rating, goals and guests (the calendar and what goals read are
//   never touched).
// <out>/celebration-checks.txt is the report.
#include "Celebrations.hpp"
#include "Ambient.hpp"
#include "Features.hpp"
#include "Guests.hpp"
#include "PlacedObjects.hpp"
#include "SaveGame.hpp"
#include "World.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

int runCelebrationChecks(World &world, UiGameScreen &hud, ZooSim &sim, ResourceManager *rm, const std::string &out,
                         const std::function<std::string(bool)> &yearDigest) {
  (void)rm;
  std::ofstream log(out + "/celebration-checks.txt");
  int failed = 0;
  auto check = [&](const std::string &name, bool ok) {
    log << (ok ? "PASS " : "FAIL ") << name << "\n";
    if (!ok)
      failed++;
  };
  const bool wasOn = Features::celebrations;
  Ambient &amb = world.getAmbient();
  const WorldMap &map = world.getMap();
  Guests &guests = world.getGuests();
  PlacedObjects &objects = world.objectsMutable();
  const int biplane = amb.kindOf(5082), santa = amb.kindOf(5083), witch = amb.kindOf(5084);

  // 1. The calendar: each celebration on its own days of a leap and an
  // ordinary year, the news exactly three days before its first day
  {
    int newYear = 0, valentine = 0, birthdayY1 = 0, birthdayY2 = 0, halloweenWeek = 0, halloweenDay = 0,
        christmas = 0, newsDays = 0;
    bool newsAhead = true, newsPlain = true;
    std::vector<std::string> seen;
    for (int year : {1, 2, 4}) {
      for (int month = 0; month < 12; month++)
        for (int day = 1; day <= ZooSim::daysInMonth(month, year); day++) {
          newYear += Celebrations::newYear(day, month);
          valentine += Celebrations::valentine(day, month);
          birthdayY1 += year == 1 && Celebrations::birthday(day, month, year);
          birthdayY2 += year != 1 && Celebrations::birthday(day, month, year);
          halloweenWeek += Celebrations::halloweenWeek(day, month);
          halloweenDay += Celebrations::halloweenDay(day, month);
          christmas += Celebrations::christmasWeek(day, month);
          const std::string line = Celebrations::news(day, month, year);
          if (!line.empty()) {
            newsDays++;
            seen.push_back(line);
            // (three days on, a celebration's first day)
            int d = day + 3, m = month, y = year;
            if (d > ZooSim::daysInMonth(m, y)) {
              d -= ZooSim::daysInMonth(m, y);
              if (++m == 12) {
                m = 0;
                y++;
              }
            }
            newsAhead = newsAhead && (Celebrations::newYear(d, m) || Celebrations::valentine(d, m) ||
                                      Celebrations::birthday(d, m, y) || Celebrations::halloweenWeek(d, m) ||
                                      Celebrations::christmasWeek(d, m));
            newsPlain = newsPlain && line.find('%') == std::string::npos && line.size() < 100;
          }
        }
    }
    log << "CALENDAR new year " << newYear << " valentine " << valentine << " birthday y1 " << birthdayY1
        << " y2+ " << birthdayY2 << " halloween week " << halloweenWeek << " day " << halloweenDay
        << " christmas week " << christmas << " news days " << newsDays << "\n";
    // (three years of 365 or 366 days: New Year 2 a year, the week of 8)
    check("calendar: New Year 31 Dec and 1 Jan, Valentine's 14 Feb, Halloween week 24-31 Oct (its day the 31st), "
          "Christmas week 18-25 Dec, the birthday from year 2 only",
          newYear == 6 && valentine == 3 && birthdayY1 == 0 && birthdayY2 == 2 && halloweenWeek == 24 &&
              halloweenDay == 3 && christmas == 24);
    check("news: five lines a year, each three days before a celebration's first day, plain words",
          newsDays == 15 && newsAhead && newsPlain);
  }

  // 2. Switched off: nothing at all
  {
    Features::celebrations = false;
    Celebrations::reset();
    amb.clear();
    std::vector<std::string> posted;
    auto post = [&](const std::string &line) { posted.push_back(line); };
    for (const auto &[d, m] : std::vector<std::pair<int, int>>{{28, 11}, {31, 11}, {1, 0}, {14, 1}, {24, 9}, {31, 9}, {18, 11}, {25, 11}})
      Celebrations::update(world, d, m, 2, post);
    check("switch off: no news, no flyover, no colour on any celebration day",
          posted.empty() && amb.count() == 0 && Celebrations::recolouredGuests() == 0 &&
              Celebrations::recolouredBuildings() == 0);
  }

  // 3. Switched on: each celebration's flyover, with the game's own sound
  struct Day {
    const char *what;
    int day, month, kind;
    const char *sound;
  };
  const Day days[] = {{"31 Dec", 31, 11, biplane, "sounds/biplane.wav"}, {"1 Jan", 1, 0, biplane, "sounds/biplane.wav"},
                      {"24 Oct", 24, 9, witch, ""},                       {"28 Oct", 28, 9, witch, ""},
                      {"18 Dec", 18, 11, santa, "sounds/sleigh.wav"},     {"22 Dec", 22, 11, santa, "sounds/sleigh.wav"}};
  {
    Features::celebrations = true;
    bool all = biplane >= 0 && santa >= 0 && witch >= 0;
    for (const Day &d : days) {
      Celebrations::reset();
      amb.clear();
      Celebrations::update(world, d.day, d.month, 2, nullptr);
      int kind = -1;
      float x, y, vx, vy;
      std::string path;
      int atten = 0, channel = -1;
      const bool flew = amb.count() == 1 && amb.flyer(0, kind, x, y, vx, vy) && kind == d.kind;
      amb.ownSound(0, path, atten, channel);
      const bool sound = std::string(d.sound).empty() ? path.empty() : path == d.sound;
      log << "FLYOVER " << d.what << ": flyers " << amb.count() << " kind " << kind << " sound '" << path << "'\n";
      all = all && flew && sound;
      // (the same date again: still the one, not one a frame)
      Celebrations::update(world, d.day, d.month, 2, nullptr);
      all = all && amb.count() == 1;
    }
    check("switch on: a biplane on 31 Dec and 1 Jan, a witch through Halloween week, Santa through Christmas week, "
          "each once a day, with their own sounds (biplane.wav, sleigh.wav)",
          all);
    // New Year and the birthday on the same day: both celebrated - New Year's
    // biplane, and the birthday's fireworks (owner's choice, 2026-11-21)
    Celebrations::reset();
    amb.clear();
    Celebrations::update(world, 1, 0, 2, nullptr);
    const bool planeY2 = amb.count() == 1, onY2 = Celebrations::fireworksOn();
    Celebrations::tickFireworks(1.5f);
    const int burstsY2 = Celebrations::fireworkBursts();
    // (they leave the game's own random numbers alone)
    std::srand(4321);
    const int r1 = std::rand();
    std::srand(4321);
    Celebrations::tickFireworks(2.0f);
    const int r2 = std::rand();
    // The next day: no new ones, and those in the sky finish
    Celebrations::update(world, 2, 0, 2, nullptr);
    const bool offNext = !Celebrations::fireworksOn();
    Celebrations::tickFireworks(3.0f);
    const int burstsNext = Celebrations::fireworkBursts();
    // Year 1's 1 January: New Year only, no birthday
    Celebrations::reset();
    amb.clear();
    Celebrations::update(world, 1, 0, 1, nullptr);
    const bool planeY1 = amb.count() == 1, offY1 = !Celebrations::fireworksOn();
    Celebrations::tickFireworks(1.5f);
    const int burstsY1 = Celebrations::fireworkBursts();
    log << "FIREWORKS y2 plane " << planeY2 << " on " << onY2 << " bursts " << burstsY2 << " | next day on " << !offNext
        << " bursts " << burstsNext << " | y1 plane " << planeY1 << " on " << !offY1 << " bursts " << burstsY1
        << " | rand " << r1 << "/" << r2 << "\n";
    check("1 Jan of year 2: New Year's biplane and the zoo's birthday fireworks both, over the day; the fireworks "
          "end with it and never use the game's random numbers",
          planeY2 && onY2 && burstsY2 > 0 && offNext && burstsNext == 0 && r1 == r2);
    check("1 Jan of year 1: New Year's biplane, no birthday fireworks", planeY1 && offY1 && burstsY1 == 0);
    // Switched off on the birthday: none
    Features::celebrations = false;
    Celebrations::reset();
    Celebrations::update(world, 1, 0, 2, nullptr);
    Celebrations::tickFireworks(1.5f);
    const bool noneOff = !Celebrations::fireworksOn() && Celebrations::fireworkBursts() == 0;
    Features::celebrations = true;
    check("switch off: no fireworks on the zoo's birthday", noneOff);
    // A day with none
    Celebrations::reset();
    amb.clear();
    std::vector<std::string> posted;
    Celebrations::update(world, 10, 5, 2, [&](const std::string &l) { posted.push_back(l); });
    Celebrations::update(world, 14, 1, 2, nullptr);
    check("switch on: an ordinary day sends nothing, Valentine's Day sends no flyer", posted.empty() && amb.count() == 0);
    // The news
    posted.clear();
    for (const auto &[d, m] : std::vector<std::pair<int, int>>{{28, 11}, {29, 11}, {11, 1}, {21, 9}, {15, 11}}) {
      Celebrations::update(world, d, m, 3, [&](const std::string &l) { posted.push_back(l); });
      Celebrations::update(world, d, m, 3, [&](const std::string &l) { posted.push_back(l); }); // (same day: once)
    }
    log << "NEWS";
    for (const std::string &l : posted)
      log << " | " << l;
    log << "\n";
    check("switch on: each news line posted once, on its day", posted.size() == 5 && posted[1].find("3 years old") != std::string::npos);
  }

  // 4. Colours: guests and a building in the day's colours, given back
  {
    guests.clear();
    guests.setEntrance(25, 26);
    for (int k = 0; k < 4; k++)
      guests.admit(k % 4);
    const size_t firstObj = objects.objects().size();
    const bool painted = objects.add("scenery/building/aqgftshp.ai", 14.0f, 58.0f, 0);
    if (guests.mutableGuests().size() == 4 && painted) {
      auto snapshot = [&]() {
        std::string s;
        for (size_t i = 0; i < 4 && i < guests.mutableGuests().size(); i++)
          s += std::to_string(guests.mutableGuests()[i].colours[0]) + "," +
               std::to_string(guests.mutableGuests()[i].colours[1]) + ";";
        for (int c : objects.objects()[firstObj].colours)
          s += "b" + std::to_string(c);
        return s;
      };
      const std::string before = snapshot();
      Features::celebrations = true;
      Celebrations::reset();
      Celebrations::update(world, 30, 9, 2, nullptr);
      const std::string eve = snapshot();
      Celebrations::update(world, 31, 9, 2, nullptr);
      const std::string halloween = snapshot();
      bool halloweenOk = true;
      for (const Guests::Guest &g : guests.mutableGuests())
        halloweenOk = halloweenOk && g.colours[0] == 10 && g.colours[1] == (guests.types()[g.type].female ? 11 : 2);
      const auto &c = objects.objects()[firstObj].colours;
      halloweenOk = halloweenOk && !c.empty() && c[0] == 7 && (c.size() < 2 || c[1] == 5);
      // a guest coming in during the day wears them too
      guests.admit(0);
      Celebrations::update(world, 31, 9, 2, nullptr);
      const Guests::Guest &late = guests.mutableGuests().back();
      const bool lateOk = late.colours[0] == 10;
      // a save written on the day holds the zoo's own colours
      std::string saved;
      {
        Celebrations::SaveGuard guard(world);
        saved = snapshot();
      }
      Celebrations::update(world, 1, 10, 2, nullptr);
      const std::string after = snapshot();
      log << "COLOURS before '" << before << "' halloween '" << halloween << "' after '" << after << "'\n";
      check("31 Oct: every guest 10 / 2 / 11 and a building's parts 7 / 5 (the original's Halloween colours), those that come in too",
            eve == before && halloweenOk && lateOk);
      check("the colours are put back the next day, and before a save is written - none kept in a file",
            after == before && saved == before);
      // Valentine's Day: guests only, the Mr. Pink cheat's shirt
      Celebrations::reset();
      Celebrations::update(world, 14, 1, 2, nullptr);
      bool pink = true;
      for (const Guests::Guest &g : guests.mutableGuests())
        pink = pink && g.colours[0] == 8 && g.colours[1] == 0;
      const bool buildingKept = objects.objects()[firstObj].colours.size() == 0 ||
                                objects.objects()[firstObj].colours[0] != 7;
      check("14 Feb: guests in the Mr. Pink cheat's colours (8 / 0), buildings as they are", pink && buildingKept);
      // switched off while it lasts: put back
      Features::celebrations = false;
      Celebrations::update(world, 14, 1, 2, nullptr);
      const std::string off = snapshot();
      check("switching off during a colour day gives every colour back, the original's own and a cheat's alike",
            Celebrations::recolouredGuests() == 0 && off == before);
    } else {
      check("colour fixture: four guests and a building", false);
    }
    for (size_t k = objects.objects().size(); k > firstObj; --k)
      objects.remove(k - 1);
    guests.clear();
    world.reindex();
  }

  // 5. The original's dated events are the same with the switch on or off
  {
    auto original = [&](bool on) {
      Features::celebrations = on;
      Celebrations::reset();
      amb.clear();
      guests.clear();
      amb.setAllowed({});
      std::string s;
      for (const auto &[d, m] : std::vector<std::pair<int, int>>{{25, 11}, {31, 9}, {15, 8}, {20, 5}}) {
        amb.clear();
        amb.setDate(d, m);
        for (int k = 0; k < 3; k++)
          amb.guestArrived(map);
        int witches = 0, santas = 0, planes = 0, kind;
        float x, y, vx, vy;
        for (int i = 0; amb.flyer(i, kind, x, y, vx, vy); ++i) {
          witches += kind == witch;
          santas += kind == santa;
          planes += kind == biplane;
        }
        s += std::to_string(d) + "/" + std::to_string(m) + ":" + std::to_string(witches) + "," +
             std::to_string(santas) + "," + std::to_string(planes) + ";";
      }
      amb.clear();
      return s;
    };
    const std::string off = original(false), on = original(true);
    log << "ORIGINAL EVENTS off " << off << " on " << on << "\n";
    check("the original's own events (Santa once on 25 Dec, a witch per guest on 31 Oct, the biplane on 15 Sep) are the same with the switch on or off",
          off == on && off == "25/11:0,1,0;31/9:3,0,0;15/8:0,0,1;20/5:0,0,0;");
  }

  // 6. A year, on and off: the same books, rating, goals and guests
  if (yearDigest) {
    const std::string offFull = yearDigest(false);
    const std::string onFull = yearDigest(true);
    const std::string off = offFull.substr(0, offFull.find('#')), on = onFull.substr(0, onFull.find('#'));
    log << "YEAR off " << offFull << "\nYEAR on  " << onFull << "\n";
    check("a year of play with the switch on ends with the same books, rating, goals, guests, their minute-by-minute "
          "state and the building's colours as with it off",
          !off.empty() && off == on);
    // (and the switch on did celebrate: more flyers, the building painted)
    auto number = [](const std::string &s, const std::string &key) {
      const size_t at = s.find(key);
      return at == std::string::npos ? -1 : std::atoi(s.c_str() + at + key.size());
    };
    check("... the year with the switch on did celebrate: flyovers sent and the building painted for 31 Oct, the year off none",
          number(onFull, "flyers sent ") > number(offFull, "flyers sent ") &&
              number(onFull, "most buildings recoloured ") == 1 && number(offFull, "most buildings recoloured ") == 0);
  }

  Features::celebrations = wasOn;
  Celebrations::reset();
  // (not the world or its ambient any more: the year's runs load a saved zoo,
  // which makes a new world, so the one these referred to is gone)
  if (!yearDigest)
    amb.clear();
  (void)hud;
  (void)sim;
  return failed ? 1 : 0;
}
