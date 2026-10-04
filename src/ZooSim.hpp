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
// - Funding (research, conservation, marketing) is a monthly cost spread
//   over the month's days, charged in whole dollars: research at $400 a
//   month for 26 seconds in April cost $28, marketing at $200 for 2 seconds
//   in February $1.
// - The Income / Expenses page shows the last four months, oldest on the
//   left, this month on the right; money spent shows in red with a minus.
//   Net income is this month's total. The graphs show one point a month.
// ============================================================================
class ZooSim {
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
  // A monthly cost (research, conservation, marketing funding), spread
  // over the month's days as they pass
  void setMonthlyCost(Line line, int key, double perMonth);

  double cash() const { return this->money; }
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
  bool dirty = true;
  void newMonth();
};

#endif // ZOO_SIM_HPP
