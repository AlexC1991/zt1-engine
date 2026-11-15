#ifndef ORIGINAL_CONSTANTS_HPP
#define ORIGINAL_CONSTANTS_HPP

// The numbers zoo.exe keeps in its own code rather than in the game's files,
// each with where it is. Gameplay code takes them from here, not as bare
// numbers, so re/lab/fidelity.py can tell the original's numbers from ours.
// A number from the game's files goes in the struct its file is read into
// (economy.cfg: Original::Economy below), with the original's default for
// when the file doesn't set it.

#include <string>

class ResourceManager;

namespace Original {

// Selling (zoo.exe sellFactor 0x634eb8): what a sold object, unit or animal
// brings back of its cost - ZTEntity::objectSellValue 0x41ec7f,
// ZTUnit::sellValue 0x61338b, ZTAnimal::sellValue 0x48d21d
inline constexpr float sellFactor = 0.8f;
// ZTAnimal::sellValue 0x48d21d: happiness (-100 to 100) as 0 to 1,
// (happiness + 100) x 0.005 (0x630d64, 0x635060); the whole halved (0x630d54)
inline constexpr float sellHappinessOffset = 100.0f;
inline constexpr float sellHappinessScale = 0.005f;
inline constexpr float sellHalf = 0.5f;

// The price arrows' step: admission (0x630d58, zooinfo1 Up/DownSpinner
// 0x616e56, 0x616e85) and a building's (pushed as 0x3e800000 at 0x617611
// and 0x5a5c28 for ZTBuilding::raisePrice 0x611b43, lowerPrice 0x5a5bbc)
inline constexpr float priceStep = 0.25f;

// Research and Conservation's funding line shows a level's cost (a month)
// times this, a day's (0x635040, researchFundingText 0x470d0f)
inline constexpr float fundingShownPerDay = 1.0f / 30.0f;

// Terraforming's price a height unit: zoo.ini [Map] elevationCost, 8.0 when
// it's not there (zoo.exe 0x4625af: 0x41000000; the shipped zoo.ini has no
// such key)
inline constexpr float elevationCost = 8.0f;

// The zoo rating's esthetics term (zooRatingTerms 0x41fcc1): the zoo's
// esthetics x 10 (0x630d60), no more than 100 (0x630d64) nor less than 0,
// x -0.1 (0x630d74) rounded (fistp: to the nearest, halves to even) and
// taken off - so 0 to 10 points, rounded, not cut down
inline constexpr float ratingEstheticScale = 10.0f;
inline constexpr float ratingEstheticMax = 100.0f;
inline constexpr float ratingEstheticBack = -0.1f;

// economy.cfg's [characteristics], as ZTGameMgr::loadConfig 0x591c0f reads
// them; each starts at ZTGameMgr::setDefaults 0x4c2683's value, for a key
// the file leaves out (the shipped file has no admission limits: $0 to $100)
struct Economy {
  float minAdmission = 0.0f;           // cMinAdultAdmissionPrice (ZTGameMgr+0x1154)
  float maxAdmission = 100.0f;         // cMaxAdultAdmissionPrice (+0x1158)
  float pricingFactor = 1.0f;          // cPricingFactor (+0x115c)
  float donationFactor = 2.77778e-13f; // cDonationFactor (+0x1160)
  float buildingUseCostDefault = 29.0f; // cBuildingUseCostDefault (+0x1164)
  float buildingUseCostMax = 100.0f;   // cBuildingUseCostMax (+0x1168)
  float zooDooRecycling = 50.0f;       // cZooDooRecyclingAmount (+0x116c)
  float admissionMultiple = 1.0f;      // cAdmissionMultiple (+0x1170)
  float adviceEveryMs = 10000.0f;      // [checks] message: the advice messages' timer (zoo.exe 0x4351a9)
  // The advisor's shares (zooAdvisories 0x420347): a message when more than
  // this share of the animals or guests are so
  float pctSick = 0.3f, pctProtestors = 0.3f;             // cPctSick (+0x70), cPctProtestors (+0x74)
  float pctHungry = 0.2f, pctThirsty = 0.2f;              // cPctHungry (+0x7c), cPctThirsty (+0x84)
  float pctBathroom = 0.3f, pctSouvenir = 0.7f;           // cPctBathroom (+0x8c), cPctSouvenir (+0x94)
  float pctTired = 0.3f, pctTrash = 0.4f;                 // cPctTired (+0xa0), cPctTrash (+0xa8)
  float highGuestHappy = 80.0f, lowGuestHappy = -20.0f;   // cHigh/cLowAvgGuestHappyThreshold (+0xf4, +0xfc)
  // The private foundation (foundationDonation 0x613e4a), read as whole
  // numbers (0x409c14); only Tutorial 2's file gives it a chance
  int donationsAvail = 3;     // cDonationsAvail (+0x124)
  int donationLow = 10000;    // cDonationLow (+0x128)
  int donationHigh = 20000;   // cDonationHigh (+0x12c)
  int donationChance = 0;     // cDonationChance (+0x130), percent a tick
  // Zoo members (economy.cfg's cMembership* and cEndowmentGift* keys, read
  // as whole numbers by loadConfig 0x591c0f; defaults from setDefaults
  // 0x4c2683 where it sets one): a guest leaving happier than this may
  // join, this percent of the time; each month a member stays unless a
  // roll of 0-100 less the factor is above the zoo rating, and gives low
  // to high dollars (Private Donations)
  int membershipJoinHappiness = 80; // cMembershipJoinHappiness (+0x138)
  int membershipJoinFactor = 0;     // cMembershipJoinFactor (+0x13c)
  int endowmentGiftLow = 10000;     // cEndowmentGiftLow (+0x140)
  int endowmentGiftHigh = 20000;    // cEndowmentGiftHigh (+0x144)
  int membershipJoinChance = 50;    // cMembershipJoinChance (+0x148)
  // The zoo rating's kinds term (zooRatingTerms 0x41fcc1): 10 x the kinds
  // in exhibits, no more than this, over this - economy.cfg says 20
  int speciesAvailable = 44;        // cSpeciesAvailable (+0x134)
};
// The cash a foundation gives below: a fixed $1,000 (zoo.exe 0x635128, read
// by zooPeriodicTick 0x435243 - not cLoanAvailable, which the file also has)
inline constexpr float foundationBelowCash = 1000.0f;
// An animal's watchers (zoo.exe 0x4479fd): a viewing area's guests count for
// this much less with every tile between it and them (0x630d68, a double;
// the product truncated each time)
inline constexpr double guestCrowdFalloff = 0.9;
inline Economy &economy() {
  static Economy e;
  return e;
}
// Read economy.cfg into economy() (ZooSim.cpp): once, when a zoo starts;
// for a scenario (its .scn), its [start] economy file over it
void loadEconomy(ResourceManager *rm, const std::string &scenario = "");

// ZTGameMgr::setAdmissionPrice 0x4bbf10: no more than the most, and the
// least if that leaves it at or below the least
inline float clampAdmission(float price) {
  const Economy &e = economy();
  float p = price < e.maxAdmission ? price : e.maxAdmission;
  return p <= e.minAdmission ? e.minAdmission : p;
}

} // namespace Original

#endif // ORIGINAL_CONSTANTS_HPP
