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
//   0x41f1ba, the marketing tick 0x41f2ae), charged here in whole dollars as
//   it adds up (research at $400 a month for 26 seconds in April cost $28,
//   marketing at $200 for 2 seconds in February $1). A tick's paid only if
//   the zoo has it - with no money nothing's paid, so funding never takes
//   the cash below zero.
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
    double owed = 0; // charged in whole dollars as it adds up
  };
  std::vector<Monthly> monthly;
  double fundingClock = 0; // seconds to the next funding tick (every 3 s)
  float foundationCount = 0; // the foundations asked so far (zoo.exe +0x120)
  bool dirty = true;
  void newMonth();
};

#endif // ZOO_SIM_HPP
