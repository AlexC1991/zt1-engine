#ifndef ORIGINAL_CONSTANTS_HPP
#define ORIGINAL_CONSTANTS_HPP

// The numbers zoo.exe keeps in its own code rather than in the game's files,
// each with where it is. Gameplay code takes them from here, not as bare
// numbers, so re/lab/fidelity.py can tell the original's numbers from ours.
// A number from the game's files goes in the struct its file is read into
// (economy.cfg: Original::Economy below), with the original's default for
// when the file doesn't set it.

#include <cstdint>
#include <string>

class ResourceManager;

namespace Original {

// Selling (zoo.exe sellFactor 0x634eb8): what a sold object, unit or animal
// brings back of its cost - ZTEntity::objectSellValue 0x41ec7f,
// ZTUnit::sellValue 0x61338b, ZTAnimal::sellValue 0x48d21d
inline constexpr float sellFactor = 0.8f;
// The bulldozer: a press stays a click while the cursor is on the pressed
// tile and no more than this many pixels from the press (zoo.exe
// bulldozeMouse 0x509071, at 0x4ec0cb: cmp 0x10, jg)
inline constexpr int bulldozeClickPixels = 16;
// Zoo Status, while open, is redone every 10000 ms (zoo.exe 0x401644:
// 0x643670 = the time + 10000)
inline constexpr uint64_t zooStatusRefreshMs = 10000;
// A path laid on a lake tile paints it this terrain type first (zoo.exe
// placeObject 0x4d9088 at 0x5cdd85: the terrain list's entry 3, +0x90 - the
// list is by type: measured 2026-11-17 in the running original,
// [0x638058]+0x10 entry 3 has type 3, cost 20.0: ttDirt), and its hover
// price adds that terrain's cost (placementCheck 0x4f885e: 0x4dc8a4(3))
inline constexpr int pathOnLakeTerrain = 3;
// A building's looping use sound (its [UseSound] loops = 1) fades by this
// much attenuation a ms of play: out while it has no users, in while it has
// (ZTBuilding::update 0x434c4b: +0x1b4 +/- ms x 3), kept to 0..5000; at
// 5000 it stops (sound slot 0x60)
inline constexpr int useLoopFadePerMs = 3;
inline constexpr int useLoopFadeMax = 5000;
// A building's [AmbientSound] or use sound is played only while its
// attenuation and its distance from the view come to no more than this
// (0x41e3f8 at 0x41e4ea, 0x4b0bca: cmp 0x898), and one ambient sound
// starts at most every 1000 ms of real time (0x4b0c49: GetTickCount + 0x3e8
// into 0x6394e8)
inline constexpr int buildingSoundReach = 0x898;
inline constexpr uint32_t ambientStartGapMs = 1000;
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
// Zoo Status' graphs show this month and the 11 before it (zooGraphsRefresh
// 0x52ff4d: each line's loop runs 12 months, 0xc, from the month less 11)
inline constexpr size_t graphMonths = 12;
// A Restroom's type (its cNameID): rubbled, it leaves bathrubl (ZTBuilding::
// toRubble 0x611bd6: cmp 0x1f48)
inline constexpr int restroomNameId = 8008;
// Rubble's sell value (ZTRubble's class slot 0x120, 0x615a89): the type it
// was made from (its cNameID, +0x15c) - that type's cost (type slot 0xa4)
// times 0.5 (0x635420); 0 that way gives 0.1 (0x63553c); no such type 1
// (0x635490)
inline constexpr float rubbleSellFactor = 0.5f;
inline constexpr float rubbleSellAtLeast = 0.1f;
inline constexpr float rubbleSellUntyped = 1.0f;
inline constexpr long long monthsOpenDays = 30;
inline int monthsOpen(double openedDay, double today) {
  const long long days = static_cast<long long>(today - openedDay);
  const long long months = static_cast<long long>(static_cast<double>(days) + monthsOpenRound) / monthsOpenDays;
  return months < 1 ? 1 : static_cast<int>(months);
}
// A tank filter's months (filterPanelNumbers 0x615d28, ZTTankFilter::update
// 0x497e0f at 0x498102): the whole days since then, plus 29.9 cut, over 30 -
// as monthsOpen but with no minimum, so 0 for its first 30 days
inline int monthsSince(double fromDay, double today) {
  const long long days = static_cast<long long>(today - fromDay);
  return static_cast<int>(static_cast<long long>(static_cast<double>(days) + monthsOpenRound) / monthsOpenDays);
}
// ZTTankFilter::update 0x497e0f: its months in operation counted again every
// 3000 ms (+0x18c, reset to 0xbb8 when it runs out)
inline constexpr float filterMonthCheckSeconds = 3.0f;
// (zoo.exe gameDateDefaultTicks 0x4c2bb9 is a settings default of 1 January
// 2000 as a FILETIME, which of its date fields wasn't worked out; a new zoo's
// date is 1 January 2001, gameMgrNewZooDefaults 0x58f39c)
// A FILETIME (100 ns since 1601) for January 1 2001, the zoo calendar's
// day 0 (400 years, 146097 days, after 1601), and a day in FILETIME units
// (0x40674d's divisor above): ours: how a day count becomes the original's
// opening time and back
inline constexpr unsigned long long fileTime2001 = 126227808000000000ULL;
inline constexpr unsigned long long fileTimeDay = 864000000000ULL;

// The message bar (its element's constructor 0x520a08, its add 0x49c6d7):
// it keeps the newest 25 messages (+0x440), the oldest dropped, and a
// message of kind 0-2 is dropped when the same text is already kept from
// less than zoo.ini [UI] minimumMessageInterval seconds ago (+0x444, 60
// when the key's missing), timed by the clock (timeGetTime), not the game
inline constexpr int messagesKept = 25;
inline constexpr int minimumMessageInterval = 60;
inline constexpr int messageRepeatKinds = 3;

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
  // A price against its building's default (cDefaultCost): a bargain at or
  // under cItemCheap times it, too dear at or over cItemExpensive times it
  // (+0x104, +0x108 - [0x638048]+0x114/+0x118 as guestBuy 0x42d858 reads
  // them; setDefaults 0x4c2683: 0.8 and 1.2; economy.cfg: 0.5 and 2.
  // measured: 0.5 and 2.0 live in a new freeform zoo, 2026-11-16)
  float itemCheap = 0.8f, itemExpensive = 1.2f;
  // Guest arrivals (guestArrivalCheck 0x424375; its this is ZTGameMgr+0x10):
  // the admission bands - the price over each in turn - cAdultAdmission's
  // five, copied over the exe's 50, 20, 10, 5, 0 (0x6392ac) when the file
  // gives five (loadConfig 0x591a1f); measured 49, 29, 19, 9, 0 in a new
  // freeform zoo, 2026-11-16. And cCreateGuestChanceVeryLow..VeryHigh
  // (this+0xac..+0xbc; setDefaults 0x4c2a12: 1 each), measured 3, 5, 25,
  // 30, 40 (gameMgr+0xbc..+0xcc)
  float adultAdmission[5] = {50.0f, 20.0f, 10.0f, 5.0f, 0.0f};
  int createGuestChance[5] = {1, 1, 1, 1, 1};
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
// A tank's salinity (habitat +0x18c, and +0x190 the kind it's to become):
// salt is 100 (tanks.cfg initialSalinity; the Salt Water button's,
// tankmod_Buttons_click 0x469128 -> tankFillBuy 0x6115f2(100)), fresh 0
// (0x6115f2(0)); the game reads it as salt when over 0 (isSaltWater
// 0x4936df, tankPrices 0x459b19)
inline constexpr int saltSalinity = 100;
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

// A name typed into an information panel (zoo.exe's setName, class slot
// 0x1c of every unit and object, 0x449515 -> 0x449215; an exhibit's
// 0x5aedbd) is taken only when it has something to see: a letter, digit or
// punctuation mark (the C library's ctype 0x117, 0x4491ea) or a double-
// byte character's lead byte. All spaces (or nothing), the name stays as
// it was. ours: any other byte over 0x7f counts too (the original asks the
// code page's own tables)
inline bool nameTaken(const std::string &typed) {
  for (unsigned char c : typed)
    if (c > 0x7f || (c > 0x20 && c < 0x7f))
      return true;
  return false;
}

// ZTGameMgr::setAdmissionPrice 0x4bbf10: no more than the most, and the
// least if that leaves it at or below the least
inline float clampAdmission(float price) {
  const Economy &e = economy();
  float p = price < e.maxAdmission ? price : e.maxAdmission;
  return p <= e.minAdmission ? e.minAdmission : p;
}

// The main bar's happiness meters (ZTGameMgr::update 0x435055, once a
// second: the animal meter 0x41d08b, the guest meter 0x41d15d): an average
// (-100..100) shown as (it + 100) x 100 / 200, 0 when there's none to count
inline int mainBarMeter(int average, int count) { return count ? (average + 100) * 100 / 200 : 0; }

// A 'fun' building's score when zoo.ini [AI] useAttractionAttractiveness is
// set (zoo.exe attractionHighestScore 0x60b086): 100 - (1000 - its squared
// distance in tiles from the guest's tile, at most 990) / 10, the division
// cut toward zero - the further, the higher, up to 99 from about 31.5 tiles
// on. The guest takes the highest, the first of equals (0x60b10f: only a
// higher score replaces it)
inline int attractionScore(int dx, int dy) {
  int d = dx * dx + dy * dy;
  if (d > 990)
    d = 990;
  return 100 - (1000 - d) / 10;
}

// The limits a guest's happiness and needs are held to (ZTGuestType::
// setDefaults 0x4c47dd: happiness +0x1e4 / +0x1ec, hunger, thirst, energy
// and the restroom +0x1f0 to +0x20c - no .ai key sets them), as
// guestApplyPending 0x43b03a clamps them
inline constexpr float guestHappinessMax = 100.0f, guestHappinessMin = -100.0f;
inline constexpr float guestNeedMax = 100.0f, guestNeedMin = 0.0f;

// The zoo's thoughts (ZTThoughtMgr, [0x639090]): at most 1000, the oldest
// dropped past it (its constructor 0x464b83: +0xc = 1000, read by thoughtPush
// 0x42311a; measured: 1000 live, 2026-11-16); Guest Information lists a
// guest's newest 5 of them (guestThoughtsList 0x46a040 -> 0x46a7c8(guest, 5))
inline constexpr int thoughtListCap = 1000, guestInfoThoughts = 5;

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

// zoo.exe 0x43646e (0x49c3a6..0x49c4b5, 0x5aa019): fMove's time (ms, its
// argument x 1000 - 0x425533) as plays of the move's animation, each
// 0x42514c(0) ms long: one, then one more while what's left of the time
// after them is over 0 - so ceil(time / length), at least one. (A length of
// 0 with a time: the second play is queued before the length is tested,
// 0x5aa022, so two)
inline int timedMovePlays(int timeMs, int lengthMs) {
  int plays = 1;
  for (int left = timeMs - lengthMs; left > 0; left -= lengthMs) {
    plays++;
    if (lengthMs <= 0)
      break;
  }
  return plays;
}

// A unit's walk about (unitWalkAbout 0x43d948): a tile on a ring round it,
// its half-widths rand % (the AI manager's slot 0x20 / 0x24) + 1 + BaseX /
// BaseY. The AI manager (ZTAIMgr, [0x638098]) reads them from zoo.ini [AI]
// (0x5267ec: BaseX, BaseY 1, RandomX, RandomY 5; 0x52599f: mRandomXGuest,
// mRandomYGuest, mRandomXStaff, mRandomYStaff 9, below 1 made 1); its
// slots 0x20 / 0x24 (0x43dcf2 / 0x43dcc7) give a guest's or a staff
// member's own pair, any other unit RandomX / RandomY. The shipped zoo.ini
// has no [AI] section. measured: 2026-11-17 in the running original (#229),
// [0x638098] +0xd4..+0xe0 1 1 5 5, +0x140..+0x14c 9 9 9 9
// A guest's show pick (0x440f0c): none when the nearest open grandstand is
// further than zoo.ini [AI] maxDistToGoToShow tiles (read once; default
// 150 at [0x6395ac] - the shipped zoo.ini has no [AI] section)
inline constexpr int maxDistToGoToShow = 150;
// A unit's step cost that means "impassable" (zoo.exe DAT_00635494, 20 in
// its data: staffStepCost 0x41456c caps at it; gateCrossCheck 0x46676a calls
// a pair at it - 1 or more code 4)
inline constexpr int impassableStepCost = 20;
inline constexpr int aiWalkBase = 1;
inline constexpr int aiWalkRandom = 5;
inline constexpr int aiWalkRandomStaff = 9;
inline constexpr int aiWalkRandomGuest = 9;

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
// A guest is made with +0x204 = rand % 11 (0x4f6082, class slot 0x108, called
// by the unit factory 0x412669) and its rate is given + that - 5. measured
// (Play original, 2026-11-19, the every-item zoo left running: 58 guests, 32
// of them arrivals): +0x204 from 0 to 10 in the arrivals (the generated ones
// 0), and the rate 28 + it (x0.9 once rested, tiredness 19 or less: variant 9
// at tiredness 6 gave 33, variant 2 at 9 gave 27). ours: the number is a
// stable spread of the guest's id (a new saved field would change the save)
inline int guestSpeedVariant(int guestId) {
  unsigned h = static_cast<unsigned>(guestId) * 2654435761u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return static_cast<int>(h % 11u);
}
inline int guestWalkRate(int tiredness, int variant = 0) {
  const int rate = guestRateGiven + variant - guestRateLess;
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

// cSicklyAnimalPct (global 0x6386f8; staff/keeper.ai, scient.ai and
// trainer.ai all 15): animalIsSickly 0x4a88cf - health x 100 at or under
// cMaxHits x (100 - it) - an animal a keeper goes to heal
inline constexpr int sicklyAnimalPct = 15;

// zoo.exe guestRecordVisit 0x4a840e: a guest's visited list (+0x260) keeps
// its last 10 places (past 10, the oldest dropped)
inline constexpr size_t guestVisitedPlaces = 10;

// zoo.exe ZTGuest::guestNearNeedBuilding 0x46b7d6 (push 0xa): a guest put
// down in need looks for a building within 10 tiles (the squared tile
// distance against 10 x 10, guestFindNearBuilding 0x46b818)
inline constexpr int guestPutDownBuildingRadius = 10;

// guestArrivalCheck 0x424375's table: by the rating (this+0x1c, the shown
// one, plus the marketing benefit) - 81 or more, 60, 30, under 30 - and the
// admission band (the price over cAdultAdmission[0], [1], [2], [3], else
// the cheapest), which of the five chances, VeryLow (0) to VeryHigh (4), is
// rolled (chancePercent 0x4244ab) for a guest at the gate (0x4f6e3c); -1
// none. Read from its disassembly (jump tables 0x42456c, 0x42457c,
// 0x42458c and 0x42452e)
inline constexpr int guestArrivalChance[4][5] = {
    {0, 2, 3, 3, 4}, {-1, 1, 2, 3, 4}, {-1, 0, 2, 2, 3}, {-1, -1, 0, 1, 2}};
// ... and the cells whose arrival also posts "good value" (0x2721, 10017,
// by 0x429d68: only with more than 10 guests in the zoo, this+0x44)
inline constexpr bool guestArrivalGoodValue[4][5] = {
    {false, false, false, true, true}, {false, false, false, false, true},
    {false, false, false, false, true}, {false, false, false, false, false}};
inline constexpr int goodValueGuests = 10;

// A unit's footprints (class slot 0x94: ZTUnit::footprint 0x41070b, the
// animal's 0x410803), in half tiles. cMapFootprint (ZTUnitType +0x150) is 2
// unless its .ai says (ZTUnitType::setDefaults 0x4b5c71; only apato.ai does:
// 4); an animal's crate (cBoxFootprintX/Y/Z, ZTAnimalType +0x1d8) and egg
// (cEggFootprintX/Y/Z, +0x410) are 2 each way unless set
// (ZTAnimalType::setDefaults 0x4b659e)
inline constexpr int unitMapFootprint = 2;
inline constexpr int animalBoxFootprint = 2;
inline constexpr int animalEggFootprint = 2;
// zooViewMouse 0x4ded90's animal branch (0x4e1279 -> 0x5ad00a -> 0x5cd93c):
// on a tile whose slope code (+0x81) isn't flat or one of the four plain
// slopes, an animal whose footprint covers this many half-tile squares or
// more (its +0x150, UnitBase::addedToWorld 0x412cdc: x times y of slot
// 0x94's footprint) can't be put down
inline constexpr int animalSteepFootprintArea = 4;
// animalPlacementRule 0x4f86fb (`cmp [esp+0x68], 2; jg`): an animal whose
// footprint (slot 0x94) is over this many half tiles either way gets the
// footprint-wide check 0x4f7fbd
inline constexpr int animalFootprintCheckOver = 2;
// 0x4f7fbd: a footprint (slot 0x94) over this many half tiles either way
// needs flat tiles of one height (a check from a crate only) ...
inline constexpr int footprintFlatOver = 1;
// ... and one (slot 0x98) over this many either way no fence or cliff on
// the edges inside it ...
inline constexpr int footprintEdgesOver = 2;
// ... a cliff being an edge whose code (tile +0x82, 2 bits a side; byte 6
// of a map's tile) is over this
inline constexpr int footprintCliffCodeMax = 1;
// A unit's facing +0x12c (0 to 7) as a new one is put down (zooViewMouse
// 0x4e00d3: ((its panel's rotation - the view's) * 2 - 4) & 7; setPlaceItem
// 0x4f189a the same with the panel's 0): this with both at 0. Its
// footprints swap x and y when facing & 3 isn't 0 (0x4272d4, 0x40f916,
// 0x410803's crate and egg)
inline constexpr int placedUnitFacing = 4;
inline constexpr int footprintSwapMask = 3;
// A side's code as terrain edits work it out again (zoo.exe 0x45e1c2): the
// two tiles' edges level and flat, level and sloped alike, or not joined -
// the tile below the cliff, the one above it
inline constexpr int sideCodeFlat = 0;
inline constexpr int sideCodeSloped = 1;
inline constexpr int sideCodeCliffBelow = 2;
inline constexpr int sideCodeCliffAbove = 3;

// The rename cheats (zoo.exe 0x44940b and what reads their flags)
namespace Cheat {
// "Eggman": an egg made from then on (animalBecomeEgg 0x5a5283, with 1)
// hatches in 5 ticks, not its cTimeToHatch (0x5dc046: +0x398 = 5)
inline constexpr int eggmanHatchTicks = 5;
// "Dr. Dolittle": the bears whose breeding it helps - cNameID 0x1398 the
// grizzly, 0x1399 the black bear (0x5da846, 0x5db99f)
inline constexpr int grizzlyNameId = 0x1398, blackBearNameId = 0x1399;
// "Dr. Scholl" / "Dr. Frankenstein": the keeper type whose renaming they
// are (cNameID 0x254f, the dinosaur scientist) - and Dr. Scholl's scientists
// move at twice the rate (setMoveSpeed 0x41678b: rate x 2)
inline constexpr int scientistNameId = 0x254f;
inline constexpr int schollRateFactor = 2;
// "Dr. Frankenstein": 1 heal in 10 (Job4_HealAnimal::atTarget 0x4702e8:
// rand() % 10 == 0)
inline constexpr int frankensteinOneIn = 10;
// "Deer" (a grizzly): every grizzly type's cBashStrength (+0x3b4) 501
// (0x6075ea: 0x1f5)
inline constexpr int deerBashStrength = 0x1f5;
// "Comet" (a reindeer, cNameID 0x14b4): every animal whose type's
// cExpansionID (+0x54) is 1 sold for nothing (0x607757)
inline constexpr int reindeerNameId = 0x14b4;
inline constexpr int cometExpansionId = 1;
// "Yellowman" / "Redman": the shirt (+0xfc[0]) given (0x619215: 9,
// 0x61922d: 1)
inline constexpr int yellowShirt = 9, redShirt = 1;
// The guest flags (+0x340) the names set: "Stinky" 2, "rpro" 4,
// "Boogyman" 8, "Yellowman" 0x10, "Redman" 0x20
inline constexpr int stinky = 2, rpro = 4, boogyman = 8, yellowman = 0x10, redman = 0x20;
// The exhibit name cheats (dinoExhibitNameList 0x5aee66, run on a name typed
// into an exhibit's name box, 0x5aedbd): "wonderland" adds 30 to the rating
// the guest arrival check weighs (0x42444d: add eax, 0x1e); the types the
// others make available (typesSetAvailable 0x591325, by cNameID): "Xanadu"
// 0x13b9 (the unicorn), "Cretaceous Corral" 0x13b6 (the triceratops),
// "Kathmandu" 0x13b7, "Super Croc" 0x14d4 (the deinosuchus); "Copralite
// Corner" (0x607871) turns every dung object of cNameID 0x17ce..0x17d0
// (dinopoo, 6094) into the type of cNameID 0x23f0 (lrock1, a rock)
inline constexpr int wonderlandArrivalBonus = 0x1e;
inline constexpr int xanaduNameId = 0x13b9, cretaceousCorralNameId = 0x13b6, kathmanduNameId = 0x13b7,
                     superCrocNameId = 0x14d4;
inline constexpr int dinoDungNameIdFirst = 0x17ce, dinoDungNameIdLast = 0x17d0;
inline constexpr int copraliteRockNameId = 0x23f0;
} // namespace Cheat

} // namespace Original

#endif // ORIGINAL_CONSTANTS_HPP
