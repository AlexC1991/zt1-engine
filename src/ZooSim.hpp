#ifndef ZOO_SIM_HPP
#define ZOO_SIM_HPP

#include <array>
#include <string>
#include <vector>

// ============================================================================
// ZOO SIMULATION: the game clock and the zoo's books
// ============================================================================
// Measured against the original (freeform, normal speed):
// - A day takes 12 seconds of play (Jun 24 -> Jul 9 in 181 s); nothing
//   passes while paused.
// - The calendar is a real one starting on Monday, January 1 2001: months
//   have their real lengths and the date tooltip reads "Date: Fri. Jun 22"
//   (lang strings 1030 "Date: %s", 22013 "ddd'.' MMM d"). The HUD shows
//   "Jun, Year 1" (22012 "MMM',' 'Year' y"), months from 22101-22112.
// - Funding (research, conservation, marketing) is a monthly cost paid by
//   the tick: every 3 s of play a 120th of it (zoo.exe researchTick
//   0x41f1ba, the marketing tick 0x41f2ae), each tick's share spent as it
//   is, cents and all (the original's cash is a float; only its display is
//   rounded, 0x40ee3d). A tick's paid only if the zoo has it - with no
//   money nothing's paid, so funding never takes the cash below zero.
// - The Income / Expenses page shows the last four months, oldest on the
//   left, this month on the right; money spent shows in red with a minus.
//   Net income is this month's total. The graphs show one point a month.
// ============================================================================
class ZooSim {
  friend class SaveGame; // (saving and loading a game)
public:
  // The Income / Expenses rows (zoofin1.lyt's order)
  enum Line {
    Admissions = 0,     // a count, not money
    AdmissionsIncome,
    Donations,
    Concessions,
    ShowIncome,
    Recycling,
    Construction,
    AnimalPurchase,
    Upkeep,
    Wages,
    Research,
    Marketing,
    LineCount
  };
  struct Month {
    int month = 0; // 0 = January
    int year = 1;
    std::array<double, LineCount> lines{};
    double total() const;
    double rating = 0;
    // The guests in the zoo (zoo.exe's books row 25: zooRatingUpdate
    // 0x41f881 clears this month's at 0x41f8ad and adds 1 a guest it
    // counts, 0x41f669) - the count at the month's last rating update,
    // which Zoo Status' attendance graph shows (graphSeriesD 0x530bf2)
    double guests = 0;
  };

  void start(double cash, double rating);
  // Advances the clock by real seconds of play (not while paused)
  void update(double seconds);
  // Spending and earning: positive amounts; costs are recorded negative
  void spend(Line line, double amount);
  void earn(Line line, double amount);
  // A count kept on the books (Admissions), not money
  void count(Line line, int n) {
    if (!this->months.empty()) {
      this->months.back().lines[line] += n;
      this->dirty = true;
    }
  }
  // The rating update's guest count, this month's books row 25 (zoo.exe
  // zooRatingUpdate 0x41f881: cleared, then 1 a guest)
  void countGuests(int n) {
    if (!this->months.empty()) {
      this->months.back().guests = n;
      this->dirty = true;
    }
  }
  // A monthly cost paid by the tick as the clock runs (marketing funding)
  void setMonthlyCost(Line line, int key, double perMonth);
  // A funding tick's cost paid, if the zoo has it (zoo.exe 0x41f1ba,
  // 0x41f2ae: spendCash only when the amount's no more than the cash);
  // false: not paid
  bool payTick(Line line, int key, double amount);
  // A private foundation's donation, tried each tick (zoo.exe 0x4351a9,
  // 0x613e4a): 0 none; 15002 given (amount, on the books); 15003 no more
  int foundationTick(int &amount);

  double cash() const { return this->money; }
  // The infinite money cheat (Features::infiniteMoney): costs are booked as
  // ever but the cash isn't taken down, and anything can be bought
  bool unlimited = false;
  // A purchase the zoo can afford (zoo.exe 0x4fb08c): free, or no more than
  // the cash
  bool canAfford(double cost) const { return this->unlimited || cost <= 0 || cost <= this->money; }
  // (dev console) the money set outright, off the books
  void setCash(double amount) {
    this->money = amount;
    this->dirty = true;
  }
  double rating() const { return this->zooRating; }
  void setRating(double r) { this->zooRating = r; }
  int month() const { return this->months.back().month; }
  int year() const { return this->months.back().year; }
  int dayOfMonth() const { return static_cast<int>(this->day) + 1; } // 1-based
  // 0 = Sunday .. 6 = Saturday
  int weekday() const;
  static int daysInMonth(int month, int year);
  // The date now as days since January 1 of year 1, with the time of day
  // as a fraction (the original's zoo FILETIME, 0x40e7e0, counted in days)
  double today() const { return static_cast<double>(this->dayCount) + (this->day - static_cast<long>(this->day)); }
  // The day the given month began (months since January of year 1)
  static double monthStart(int monthIndex);
  // A building's opening day: its own, or for one saved before that was
  // kept, the first day of the month it opened (its openedMonth on the
  // game's month count, monthCount now)
  double openedDayOf(double openedDay, int openedMonth, int monthCount) const {
    if (openedDay > -1e299) // (PlacedObjects::Object::kUnknownDay: not known)
      return openedDay;
    const int now = this->month() + (this->year() - 1) * 12;
    return monthStart(now - (monthCount - openedMonth) < 0 ? 0 : now - (monthCount - openedMonth));
  }
  // The months so far, oldest first (the last is this month)
  const std::vector<Month> &history() const { return this->months; }
  // Something changed that the HUD and panels should show
  bool changed() {
    bool c = this->dirty;
    this->dirty = false;
    return c;
  }

  static constexpr double kSecondsPerDay = 12.0;

private:
  std::vector<Month> months;
  // Original annual rows/totals have no monthly detail before its rolling
  // year. Retain them separately so a portable save does not discard them.
  std::vector<std::vector<float>> importedAnnual;
  std::vector<float> importedTotals;
  double money = 0;
  double zooRating = 0;
  double day = 0;  // days into this month, fractional
  long dayCount = 0; // whole days since January 1, year 1
  struct Monthly {
    Line line;
    int key;
    double perMonth;
    double owed = 0; // (unused: the original pays each tick as it is)
  };
  std::vector<Monthly> monthly;
  double fundingClock = 0; // seconds to the next funding tick (every 3 s)
  float foundationCount = 0; // the foundations asked so far (zoo.exe +0x120)
  bool dirty = true;
  void newMonth();
};

#endif // ZOO_SIM_HPP
