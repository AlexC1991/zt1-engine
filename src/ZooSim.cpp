#include "ZooSim.hpp"
#include "IniReader.hpp"
#include "OriginalConstants.hpp"
#include "ResourceManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// economy.cfg's [characteristics] into Original::economy() (zoo.exe
// ZTGameMgr::loadConfig 0x591c0f): a key it leaves out keeps the original's
// default (ZTGameMgr::setDefaults 0x4c2683)
void Original::loadEconomy(ResourceManager *rm) {
  Economy e;
  if (IniReader *ini = rm ? rm->getIniReader("economy.cfg") : nullptr) {
    auto num = [&](const char *key, float &into) {
      std::string v = ini->get("characteristics", key);
      if (!v.empty())
        into = static_cast<float>(std::atof(v.c_str()));
    };
    num("cMinAdultAdmissionPrice", e.minAdmission);
    num("cMaxAdultAdmissionPrice", e.maxAdmission);
    num("cPricingFactor", e.pricingFactor);
    num("cDonationFactor", e.donationFactor);
    num("cBuildingUseCostDefault", e.buildingUseCostDefault);
    num("cBuildingUseCostMax", e.buildingUseCostMax);
    num("cZooDooRecyclingAmount", e.zooDooRecycling);
    num("cAdmissionMultiple", e.admissionMultiple);
    delete ini;
  }
  economy() = e;
}

double ZooSim::Month::total() const {
  double t = 0;
  for (int i = AdmissionsIncome; i < LineCount; i++)
    t += this->lines[i];
  return t;
}

// Year 1 is 2001
int ZooSim::daysInMonth(int month, int year) {
  static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int y = 2000 + year;
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return month == 1 && leap ? 29 : days[month % 12];
}

int ZooSim::weekday() const {
  // January 1 2001 was a Monday
  return static_cast<int>((1 + this->dayCount) % 7);
}

void ZooSim::start(double cash, double rating) {
  this->months.clear();
  this->months.push_back(Month{});
  this->money = cash;
  this->zooRating = rating;
  this->months.back().rating = rating;
  this->day = 0;
  this->dayCount = 0;
  this->monthly.clear();
  this->dirty = true;
}

void ZooSim::spend(Line line, double amount) {
  if (this->months.empty() || amount == 0)
    return;
  this->months.back().lines[line] -= amount;
  this->money -= amount;
  this->dirty = true;
}

void ZooSim::earn(Line line, double amount) {
  if (this->months.empty() || amount == 0)
    return;
  this->months.back().lines[line] += amount;
  this->money += amount;
  this->dirty = true;
}

void ZooSim::setMonthlyCost(Line line, int key, double perMonth) {
  for (Monthly &m : this->monthly)
    if (m.line == line && m.key == key) {
      m.perMonth = perMonth;
      return;
    }
  this->monthly.push_back({line, key, perMonth, 0});
}

void ZooSim::newMonth() {
  Month next;
  const Month &last = this->months.back();
  next.month = (last.month + 1) % 12;
  next.year = last.year + (last.month == 11 ? 1 : 0);
  next.rating = this->zooRating;
  this->months.push_back(next);
  this->dirty = true;
}

void ZooSim::update(double seconds) {
  if (this->months.empty() || seconds <= 0)
    return;
  // In steps no longer than the rest of the day, so costs land in the month
  // they were spent in and day changes are counted
  while (seconds > 1e-9) {
    const int monthDays = daysInMonth(this->month(), this->year());
    double toNextDay = std::floor(this->day) + 1 - this->day;
    double days = std::min(seconds / kSecondsPerDay, toNextDay);
    for (Monthly &m : this->monthly) {
      m.owed += m.perMonth * days / monthDays;
      double whole = std::floor(m.owed);
      if (whole >= 1) {
        m.owed -= whole;
        this->months.back().lines[m.line] -= whole;
        this->money -= whole;
      }
    }
    if (!this->monthly.empty() && days > 0)
      this->dirty = true;
    this->day += days;
    seconds -= days * kSecondsPerDay;
    this->months.back().rating = this->zooRating;
    if (days >= toNextDay - 1e-9) {
      // A new day
      this->day = std::round(this->day);
      this->dayCount++;
      this->dirty = true;
      if (this->day >= monthDays) {
        this->day = 0;
        this->newMonth();
      }
    }
  }
}
