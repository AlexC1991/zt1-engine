#ifndef ORIGINAL_CONSTANTS_HPP
#define ORIGINAL_CONSTANTS_HPP

// The numbers zoo.exe keeps in its own code rather than in the game's files,
// each with where it is. Gameplay code takes them from here, not as bare
// numbers, so re/lab/fidelity.py can tell the original's numbers from ours.
// A number from the game's files goes in the struct its file is read into
// (economy.cfg: Original::Economy below), with the original's default for
// when the file doesn't set it.

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
};
inline Economy &economy() {
  static Economy e;
  return e;
}
// Read economy.cfg into economy() (ZooSim.cpp): once, when a zoo starts
void loadEconomy(ResourceManager *rm);

// ZTGameMgr::setAdmissionPrice 0x4bbf10: no more than the most, and the
// least if that leaves it at or below the least
inline float clampAdmission(float price) {
  const Economy &e = economy();
  float p = price < e.maxAdmission ? price : e.maxAdmission;
  return p <= e.minAdmission ? e.minAdmission : p;
}

} // namespace Original

#endif // ORIGINAL_CONSTANTS_HPP
