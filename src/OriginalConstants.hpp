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

// A building's months open (Building Information, buildingInfoNumbers
// 0x488446; Zoo Status' building list, 0x5317c5): the whole days since it
// opened (the zoo's FILETIME, 0x40e7e0, less the building's +0x180, over
// 864,000,000,000 - a day in 100 ns - by the 64-bit division 0x40674d),
// plus 29.9 (0x6351a8) cut toward zero (fistp in the chop mode), over 30
// as whole numbers; at least 1. Days 0-30 show 1 month, 31-60 show 2.
inline constexpr float monthsOpenRound = 29.9f;
inline constexpr long long monthsOpenDays = 30;
inline int monthsOpen(double openedDay, double today) {
  const long long days = static_cast<long long>(today - openedDay);
  const long long months = static_cast<long long>(static_cast<double>(days) + monthsOpenRound) / monthsOpenDays;
  return months < 1 ? 1 : static_cast<int>(months);
}
// A FILETIME (100 ns since 1601) for January 1 2001, the zoo calendar's
// day 0 (400 years, 146097 days, after 1601), and a day in FILETIME units
// (0x40674d's divisor above): ours: how a day count becomes the original's
// opening time and back
inline constexpr unsigned long long fileTime2001 = 126227808000000000ULL;
inline constexpr unsigned long long fileTimeDay = 864000000000ULL;

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
// A viewing area's sight line (zoo.exe viewingSees 0x4f39a2 -> 0x4f2d44):
// the line runs in half tiles, its height in half height units from a
// tile's own height doubled and this much over it at either end
inline constexpr int sightEyeHeight = 2;
// ...a fence on a tile edge it crosses (0x4f2fa0) hides what's behind when
// its height (cHeight; a tank wall its own, ZTFence +0x454) plus how much
// higher its tile is than the viewing tile is over this
inline constexpr int sightFenceClear = 1;
// A tank's glass (cSeeThrough) is seen through while its water's purity is
// over this (zoo.exe 0x6390b0, read live: the "can not see" line)
inline constexpr float seeThroughPurity = 1.0f;
// Scenery hides what's behind it unless its .ai says cBlocksLOS 0 (zoo.exe
// sceneryTypeLoad 0x4ba5b0: 1 when the file doesn't set it - the show toys
// set 0)
inline constexpr int sceneryBlocksLOS = 1;
// A guest done watching gives a donation only while its viewing area's
// crowd (0x425f6a) is under this (guestDoneViewingExhibit 0x42ea14: 0x32)
inline constexpr int donationCrowdLimit = 50;
inline Economy &economy() {
  static Economy e;
  return e;
}
// Read economy.cfg into economy() (ZooSim.cpp): once, when a zoo starts;
// for a scenario (its .scn), its [start] economy file over it
void loadEconomy(ResourceManager *rm, const std::string &scenario = "");

// Selling or bulldozing something (sellObject 0x50a432 -> removeObject
// 0x50a0b0): asked first when its type has cNeedsConfirm (type +0x134, its
// .ai's [Characteristics/Integers]; the buildings and the tank filter say
// 1) and zoo.ini's [UI] confirmSell isn't 0 (0x51f80c into +0x456, read by
// sellConfirmDialog 0x50bf8d; 1 when it's not there). Animals (and staff,
// fired) are asked whatever their type says, if confirmSell is on
bool sellNeedsConfirm(ResourceManager *rm, const std::string &file);
// The question (sellConfirmDialog 0x50bf8d): text 154 "Are you sure you
// want to sell %s?\nRefund: " with the thing's name, then the refund cut
// toward zero (removeObject's fistp in the chop mode, 0x50c0bf) and grouped
// ("$1,234", moneyToText 0x40eca1 with grouping on)
std::string sellQuestion(ResourceManager *rm, const std::string &name, float refund);
// Whether zoo.ini's [UI] confirmSell asks (0x51f80c; default 1)
bool confirmSell();

// ZTGameMgr::setAdmissionPrice 0x4bbf10: no more than the most, and the
// least if that leaves it at or below the least
inline float clampAdmission(float price) {
  const Economy &e = economy();
  float p = price < e.maxAdmission ? price : e.maxAdmission;
  return p <= e.minAdmission ? e.minAdmission : p;
}

// The game's updates a second: zoo.ini [user] UpdateRate=15. measured: in
// the running original (2026-11-15) the ms each update moves on by
// (moveAccumulate's counter, read live) was never under 67, mostly 67-69
inline constexpr int updateRate = 15;
inline constexpr int updateMs = (1000 + updateRate - 1) / updateRate; // 67
// A unit's position steps: 64 to a tile (its +0x114/+0x118)
inline constexpr int unitStepsPerTile = 64;
// zoo.exe 0x435c42 (moveAccumulate): each update a moving unit takes
// max(1, floor(speed x ms / 1000)) steps and drops the rest (its counter
// +0x1e8 is zeroed first); its speed (+0x1f8, set through class slot 0x1c0)
// is the .ai rate it moves at - cSlowRate walking, cFastRate running. So a
// rate's tiles a second: cSlowRate 33 takes 2 steps an update, 0.466 tiles a
// second (measured: tour guides 0.46-0.47 over 8-12 s walks, issue #152)
inline float unitTilesPerSecond(int rate) {
  int steps = rate * updateMs / 1000;
  if (steps < 1)
    steps = 1;
  return static_cast<float>(steps) * 1000.0f / updateMs / unitStepsPerTile;
}

// zoo.exe 0x42250a (ZTGuest's class slot 0x1c0, which sets the rate +0x1f8
// a move uses): with the zoo manager's +0x11c set, the rate given plus the
// guest's own byte (+0x204) less 5; at tiredness (+0x2c0) 75 or more times
// 1.1 (0x635828), at 19 or less times 0.9 (0x630d68), cut to a whole
// number. measured (Play original, 2026-11-16, everything-ff03-units: 30
// guests of all four kinds, tiredness 9-12, +0x204 0 in every one, +0x11c
// 1): every rate 25, so the rate given is 33 for every kind - not the
// kind's own cSlowRate - and they walked a median 0.233 tiles a second
inline constexpr int guestRateGiven = 33, guestRateLess = 5;
inline constexpr double guestTiredFactor = 1.1, guestRestedFactor = 0.9;
inline int guestWalkRate(int tiredness) {
  const int rate = guestRateGiven - guestRateLess;
  if (tiredness >= 75)
    return static_cast<int>(rate * guestTiredFactor);
  if (tiredness <= 19)
    return static_cast<int>(rate * guestRestedFactor);
  return rate;
}

// zoo.exe 0x425b6d: the kinds of animal a watching guest mimics, by their
// cNameID - the chimpanzee (5003) and the mountain gorilla (5021) with its
// bMimic (1), the roaring dinosaurs (5085, 5097-5099, 5326, 5327) with its
// bMimic2 (2); any other, none (0)
inline int mimicOf(int nameId) {
  if (nameId == 5003 || nameId == 5021)
    return 1;
  if (nameId == 5085 || (nameId >= 5097 && nameId <= 5099) || nameId == 5326 || nameId == 5327)
    return 2;
  return 0;
}

} // namespace Original

#endif // ORIGINAL_CONSTANTS_HPP
