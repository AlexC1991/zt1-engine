#include "ScenarioDatabase.hpp"
#include <SDL2/SDL.h>
#include <fstream>
#include <sstream>

ScenarioDatabase::ScenarioDatabase() {}

ScenarioDatabase::~ScenarioDatabase() {
  scenarios.clear();
  idToIndex.clear();
}

void ScenarioDatabase::initialize() {
  scenarios.clear();
  idToIndex.clear();

  initBaseGameScenarios();
  initDinosaurDigsScenarios();
  initMarineManiaSenarios();

  SDL_Log("ScenarioDatabase: Initialized %d scenarios", (int)scenarios.size());
}

void ScenarioDatabase::addScenario(const ScenarioEntry &entry) {
  int index = (int)scenarios.size();
  scenarios.push_back(entry);
  idToIndex[entry.id] = index;
}

const ScenarioEntry *
ScenarioDatabase::getScenario(const std::string &id) const {
  auto it = idToIndex.find(id);
  if (it != idToIndex.end()) {
    return &scenarios[it->second];
  }
  return nullptr;
}

const ScenarioEntry *ScenarioDatabase::getScenarioByIndex(int index) const {
  if (index >= 0 && index < (int)scenarios.size()) {
    return &scenarios[index];
  }
  return nullptr;
}

std::vector<const ScenarioEntry *>
ScenarioDatabase::getScenariosByExpansion(ScenarioExpansion expansion) const {
  std::vector<const ScenarioEntry *> result;
  for (const auto &s : scenarios) {
    if (s.expansion == expansion) {
      result.push_back(&s);
    }
  }
  return result;
}

std::vector<const ScenarioEntry *> ScenarioDatabase::getScenariosByDifficulty(
    const std::string &difficulty) const {
  std::vector<const ScenarioEntry *> result;
  for (const auto &s : scenarios) {
    if (s.difficulty == difficulty) {
      result.push_back(&s);
    }
  }
  return result;
}

ScenarioState ScenarioDatabase::getState(const std::string &id) const {
  const ScenarioEntry *entry = getScenario(id);
  if (entry) {
    return entry->state;
  }
  return ScenarioState::LOCKED;
}

void ScenarioDatabase::setState(const std::string &id, ScenarioState state) {
  auto it = idToIndex.find(id);
  if (it != idToIndex.end()) {
    scenarios[it->second].state = state;
  }
}

bool ScenarioDatabase::isUnlocked(const std::string &id) const {
  ScenarioState state = getState(id);
  return state == ScenarioState::UNLOCKED || state == ScenarioState::COMPLETED;
}

bool ScenarioDatabase::isCompleted(const std::string &id) const {
  return getState(id) == ScenarioState::COMPLETED;
}

std::string ScenarioDatabase::getStateMessage(const std::string &id) const {
  const ScenarioEntry *entry = getScenario(id);
  if (!entry)
    return "";

  switch (entry->state) {
  case ScenarioState::LOCKED:
    return entry->lockedMessage;
  case ScenarioState::UNLOCKED:
    return entry->unlockedMessage;
  case ScenarioState::COMPLETED:
    return entry->completedMessage;
  }
  return "";
}

// Get the appropriate icon for current state
std::string ScenarioDatabase::getStateIcon(const std::string &id) const {
  const ScenarioEntry *entry = getScenario(id);
  if (!entry)
    return "";

  switch (entry->state) {
  case ScenarioState::LOCKED:
    return entry->lockedIconPath;
  case ScenarioState::UNLOCKED:
    return entry->unlockedIconPath;
  case ScenarioState::COMPLETED:
    return entry->completedIconPath;
  }
  return "";
}

// Get objectives for a scenario
const std::vector<std::string> &
ScenarioDatabase::getObjectives(const std::string &id) const {
  static const std::vector<std::string> empty;
  const ScenarioEntry *entry = getScenario(id);

  if (!entry)
    return empty;

  if (entry->useFileObjectives)
    return empty;

  return entry->objectives;
}

void ScenarioDatabase::unlock(const std::string &id) {
  auto it = idToIndex.find(id);
  if (it != idToIndex.end()) {
    if (scenarios[it->second].state == ScenarioState::LOCKED) {
      scenarios[it->second].state = ScenarioState::UNLOCKED;
    }
  }
}

void ScenarioDatabase::complete(const std::string &id) {
  auto it = idToIndex.find(id);
  if (it != idToIndex.end()) {
    scenarios[it->second].state = ScenarioState::COMPLETED;
  }
}

std::string
ScenarioDatabase::getDefaultLockMessage(ScenarioExpansion expansion,
                                        const std::string &difficulty) {
  std::string expansionName;
  switch (expansion) {
  case ScenarioExpansion::BASE_GAME:
    expansionName = "Zoo Tycoon";
    break;
  case ScenarioExpansion::DINOSAUR_DIGS:
    expansionName = "Dinosaur Digs";
    break;
  case ScenarioExpansion::MARINE_MANIA:
    expansionName = "Marine Mania";
    break;
  }

  std::string requiredDifficulty;
  if (difficulty == "Intermediate") {
    requiredDifficulty = "beginner";
  } else if (difficulty == "Advanced") {
    requiredDifficulty = "intermediate";
  } else if (difficulty == "Very Advanced") {
    requiredDifficulty = "advanced";
  } else {
    requiredDifficulty = "previous";
  }

  return "You must complete all of the " + expansionName + " " +
         requiredDifficulty + " scenarios to unlock this scenario.";
}

void ScenarioDatabase::initBaseGameScenarios() {
  // ============================================
  // BASE GAME - BEGINNER
  // ============================================

  ScenarioEntry tut1;
  tut1.id = "aa";
  tut1.name = "Zoo Tycoon Tutorial 1";
  tut1.scenarioPath = "scenario/scn01a/scn01a.scn";
  tut1.expansion = ScenarioExpansion::BASE_GAME;
  tut1.difficulty = "Beginner";
  tut1.state = ScenarioState::UNLOCKED;
  tut1.unlockedMessage = "Learn the basics of Zoo Tycoon!";
  tut1.lockedIconPath = "ui/scenario/lock/lock";
  tut1.unlockedIconPath = "ui/scenario/iconp/iconp";
  tut1.completedIconPath = "ui/scenario/iconc/iconc";
  tut1.useFileObjectives = true;
  addScenario(tut1);

  ScenarioEntry tut2;
  tut2.id = "ab";
  tut2.name = "Zoo Tycoon Tutorial 2";
  tut2.scenarioPath = "scenario/scn01/scn01.scn";
  tut2.expansion = ScenarioExpansion::BASE_GAME;
  tut2.difficulty = "Beginner";
  tut2.state = ScenarioState::UNLOCKED;
  tut2.unlockedMessage = "Learn more about Zoo Tycoon!";
  tut2.lockedIconPath = "ui/scenario/lock/lock";
  tut2.unlockedIconPath = "ui/scenario/iconp/iconp";
  tut2.completedIconPath = "ui/scenario/iconc/iconc";
  tut2.useFileObjectives = true;
  addScenario(tut2);

  ScenarioEntry tut3;
  tut3.id = "ac";
  tut3.name = "Zoo Tycoon Tutorial 3";
  tut3.scenarioPath = "scenario/scn02/scn02.scn";
  tut3.expansion = ScenarioExpansion::BASE_GAME;
  tut3.difficulty = "Beginner";
  tut3.state = ScenarioState::UNLOCKED;
  tut3.unlockedMessage = "Master your zoo tycoon skills!";
  tut3.lockedIconPath = "ui/scenario/lock/lock";
  tut3.unlockedIconPath = "ui/scenario/iconp/iconp";
  tut3.completedIconPath = "ui/scenario/iconc/iconc";
  tut3.useFileObjectives = true;
  addScenario(tut3);

  ScenarioEntry smallZoo;
  smallZoo.id = "ba";
  smallZoo.name = "Small Zoo";
  smallZoo.scenarioPath = "scenario/scn03/scn03.scn";
  smallZoo.expansion = ScenarioExpansion::BASE_GAME;
  smallZoo.difficulty = "Beginner";
  smallZoo.state = ScenarioState::UNLOCKED;
  smallZoo.unlockedMessage = "Start with a small zoo!";
  smallZoo.lockedIconPath = "ui/scenario/lock/lock";
  smallZoo.unlockedIconPath = "ui/scenario/iconp/iconp";
  smallZoo.completedIconPath = "ui/scenario/iconc/iconc";
  smallZoo.useFileObjectives = true;
  addScenario(smallZoo);

  ScenarioEntry seaside;
  seaside.id = "bb";
  seaside.name = "Seasideville Zoo";
  seaside.scenarioPath = "scenario/scn05/scn05.scn";
  seaside.expansion = ScenarioExpansion::BASE_GAME;
  seaside.difficulty = "Beginner";
  seaside.state = ScenarioState::UNLOCKED;
  seaside.unlockedMessage = "Build a zoo by the sea!";
  seaside.lockedIconPath = "ui/scenario/lock/lock";
  seaside.unlockedIconPath = "ui/scenario/iconp/iconp";
  seaside.completedIconPath = "ui/scenario/iconc/iconc";
  seaside.useFileObjectives = true;
  addScenario(seaside);

  ScenarioEntry forest;
  forest.id = "bc";
  forest.name = "Forest Zoo";
  forest.scenarioPath = "scenario/scn04/scn04.scn";
  forest.expansion = ScenarioExpansion::BASE_GAME;
  forest.difficulty = "Beginner";
  forest.state = ScenarioState::UNLOCKED;
  forest.unlockedMessage = "Build a zoo in the forest!";
  forest.lockedIconPath = "ui/scenario/lock/lock";
  forest.unlockedIconPath = "ui/scenario/iconp/iconp";
  forest.completedIconPath = "ui/scenario/iconc/iconc";
  forest.useFileObjectives = true;
  addScenario(forest);

  // ============================================
  // BASE GAME - INTERMEDIATE
  // ============================================

  ScenarioEntry burkitsville;
  burkitsville.id = "ca";
  burkitsville.name = "Revitalize Burkitsville Zoo (Intermediate)";
  burkitsville.scenarioPath = "scenario/scn07/scn07.scn";
  burkitsville.expansion = ScenarioExpansion::BASE_GAME;
  burkitsville.difficulty = "Intermediate";
  burkitsville.state = ScenarioState::LOCKED;
  burkitsville.lockedMessage = "You must complete all of the Zoo Tycoon "
                               "beginner scenarios to unlock this scenario.";
  burkitsville.unlockedMessage = "Turn around a struggling zoo!";
  burkitsville.lockedIconPath = "ui/scenario/lock/lock";
  burkitsville.unlockedIconPath = "ui/scenario/iconp/iconp";
  burkitsville.completedIconPath = "ui/scenario/iconc/iconc";
  burkitsville.useFileObjectives = true;
  addScenario(burkitsville);

  ScenarioEntry innerCity;
  innerCity.id = "cb";
  innerCity.name = "Inner City Zoo";
  innerCity.scenarioPath = "scenario/scn06/scn06.scn";
  innerCity.expansion = ScenarioExpansion::BASE_GAME;
  innerCity.difficulty = "Intermediate";
  innerCity.state = ScenarioState::LOCKED;
  innerCity.lockedMessage = "You must complete all of the Zoo Tycoon "
                            "beginner scenarios to unlock this scenario.";
  innerCity.unlockedMessage = "Build a zoo in the city!";
  innerCity.lockedIconPath = "ui/scenario/lock/lock";
  innerCity.unlockedIconPath = "ui/scenario/iconp/iconp";
  innerCity.completedIconPath = "ui/scenario/iconc/iconc";
  innerCity.useFileObjectives = true;
  addScenario(innerCity);

  ScenarioEntry cats;
  cats.id = "cc";
  cats.name = "Saving the Great Cats";
  cats.scenarioPath = "scenario/scn11/scn11.scn";
  cats.expansion = ScenarioExpansion::BASE_GAME;
  cats.difficulty = "Intermediate";
  cats.state = ScenarioState::LOCKED;
  cats.lockedMessage = "You must complete all of the Zoo Tycoon "
                       "beginner scenarios to unlock this scenario.";
  cats.unlockedMessage = "Help save the great cats!";
  cats.lockedIconPath = "ui/scenario/lock/lock";
  cats.unlockedIconPath = "ui/scenario/iconp/iconp";
  cats.completedIconPath = "ui/scenario/iconc/iconc";
  cats.useFileObjectives = true;
  addScenario(cats);

  ScenarioEntry endangered;
  endangered.id = "cd";
  endangered.name = "Endangered Species Zoo";
  endangered.scenarioPath = "scenario/scn08/scn08.scn";
  endangered.expansion = ScenarioExpansion::BASE_GAME;
  endangered.difficulty = "Intermediate";
  endangered.state = ScenarioState::LOCKED;
  endangered.lockedMessage = "You must complete all of the Zoo Tycoon "
                             "beginner scenarios to unlock this scenario.";
  endangered.unlockedMessage = "Protect endangered species!";
  endangered.lockedIconPath = "ui/scenario/lock/lock";
  endangered.unlockedIconPath = "ui/scenario/iconp/iconp";
  endangered.completedIconPath = "ui/scenario/iconc/iconc";
  endangered.useFileObjectives = true;
  addScenario(endangered);

  // ============================================
  // BASE GAME - ADVANCED
  // ============================================

  ScenarioEntry island;
  island.id = "da";
  island.name = "Island Zoo";
  island.scenarioPath = "scenario/scn12/scn12.scn";
  island.expansion = ScenarioExpansion::BASE_GAME;
  island.difficulty = "Advanced";
  island.state = ScenarioState::LOCKED;
  island.lockedMessage = "You must complete all of the Zoo Tycoon "
                         "intermediate scenarios to unlock this scenario.";
  island.unlockedMessage = "Build a zoo on an island!";
  island.lockedIconPath = "ui/scenario/lock/lock";
  island.unlockedIconPath = "ui/scenario/iconp/iconp";
  island.completedIconPath = "ui/scenario/iconc/iconc";
  island.useFileObjectives = true;
  addScenario(island);

  ScenarioEntry savannah;
  savannah.id = "db";
  savannah.name = "African Savannah Zoo (Advanced)";
  savannah.scenarioPath = "scenario/scn13/scn13.scn";
  savannah.expansion = ScenarioExpansion::BASE_GAME;
  savannah.difficulty = "Advanced";
  savannah.state = ScenarioState::LOCKED;
  savannah.lockedMessage = "You must complete all of the Zoo Tycoon "
                           "intermediate scenarios to unlock this scenario.";
  savannah.unlockedMessage = "Create an authentic African savannah experience!";
  savannah.lockedIconPath = "ui/scenario/lock/lock";
  savannah.unlockedIconPath = "ui/scenario/iconp/iconp";
  savannah.completedIconPath = "ui/scenario/iconc/iconc";
  savannah.useFileObjectives = true;
  addScenario(savannah);

  ScenarioEntry mountain;
  mountain.id = "dc";
  mountain.name = "Mountain Zoo";
  mountain.scenarioPath = "scenario/scn14/scn14.scn";
  mountain.expansion = ScenarioExpansion::BASE_GAME;
  mountain.difficulty = "Advanced";
  mountain.state = ScenarioState::LOCKED;
  mountain.lockedMessage = "You must complete all of the Zoo Tycoon "
                           "intermediate scenarios to unlock this scenario.";
  mountain.unlockedMessage = "Build a zoo in the mountains!";
  mountain.lockedIconPath = "ui/scenario/lock/lock";
  mountain.unlockedIconPath = "ui/scenario/iconp/iconp";
  mountain.completedIconPath = "ui/scenario/iconc/iconc";
  mountain.useFileObjectives = true;
  addScenario(mountain);

  ScenarioEntry rainforest;
  rainforest.id = "dd";
  rainforest.name = "Tropical Rainforest Zoo";
  rainforest.scenarioPath = "scenario/scn15/scn15.scn";
  rainforest.expansion = ScenarioExpansion::BASE_GAME;
  rainforest.difficulty = "Advanced";
  rainforest.state = ScenarioState::LOCKED;
  rainforest.lockedMessage = "You must complete all of the Zoo Tycoon "
                             "intermediate scenarios to unlock this scenario.";
  rainforest.unlockedMessage = "Create a tropical rainforest zoo!";
  rainforest.lockedIconPath = "ui/scenario/lock/lock";
  rainforest.unlockedIconPath = "ui/scenario/iconp/iconp";
  rainforest.completedIconPath = "ui/scenario/iconc/iconc";
  rainforest.useFileObjectives = true;
  addScenario(rainforest);

  // ============================================
  // BASE GAME - VERY ADVANCED
  // ============================================

  ScenarioEntry paradise;
  paradise.id = "ea";
  paradise.name = "Paradise Island (Very Advanced)";
  paradise.scenarioPath = "scenario/scn10/scn10.scn";
  paradise.expansion = ScenarioExpansion::BASE_GAME;
  paradise.difficulty = "Very Advanced";
  paradise.state = ScenarioState::LOCKED;
  paradise.lockedMessage = "You must complete all of the Zoo Tycoon advanced "
                           "scenarios to unlock this scenario.";
  paradise.unlockedMessage = "Build the ultimate island zoo paradise!";
  paradise.lockedIconPath = "ui/scenario/lock/lock";
  paradise.unlockedIconPath = "ui/scenario/iconp/iconp";
  paradise.completedIconPath = "ui/scenario/iconc/iconc";
  paradise.useFileObjectives = true;
  addScenario(paradise);

  ScenarioEntry pandas;
  pandas.id = "fa";
  pandas.name = "Breeding Giant Pandas (Very Advanced)";
  pandas.scenarioPath = "scenario/scn09/scn09.scn";
  pandas.expansion = ScenarioExpansion::BASE_GAME;
  pandas.difficulty = "Very Advanced";
  pandas.state = ScenarioState::LOCKED;
  pandas.lockedMessage = "You must complete the very advanced Paradise Island "
                         "scenario to unlock this scenario.";
  pandas.unlockedMessage = "Successfully breed endangered giant pandas!";
  pandas.lockedIconPath = "ui/scenario/lock/lock";
  pandas.unlockedIconPath = "ui/scenario/iconp/iconp";
  pandas.completedIconPath = "ui/scenario/iconc/iconc";
  pandas.useFileObjectives = true;
  addScenario(pandas);
}

void ScenarioDatabase::initDinosaurDigsScenarios() {
  // ============================================
  // DINOSAUR DIGS - BEGINNER
  // ============================================

  ScenarioEntry ddTut;
  ddTut.id = "ad";
  ddTut.name = "Dinosaur Digs Tutorial";
  ddTut.scenarioPath = "scenario/scn19/scn19.scn";
  ddTut.expansion = ScenarioExpansion::DINOSAUR_DIGS;
  ddTut.difficulty = "Beginner";
  ddTut.state = ScenarioState::UNLOCKED; // Always unlocked
  ddTut.unlockedMessage = "Learn to care for prehistoric creatures!";
  ddTut.lockedIconPath = "ui/scenario/lockj/lockj";
  ddTut.unlockedIconPath = "ui/scenario/iconp/iconp";
  ddTut.completedIconPath = "ui/scenario/iconc/iconc";
  ddTut.useFileObjectives = true;
  addScenario(ddTut);

  ScenarioEntry iceAge;
  iceAge.id = "be";
  iceAge.name = "Ice Age Animals Zoo";
  iceAge.scenarioPath = "scenario/scn20/scn20.scn";
  iceAge.expansion = ScenarioExpansion::DINOSAUR_DIGS;
  iceAge.difficulty = "Beginner";
  iceAge.state = ScenarioState::UNLOCKED; // Always unlocked
  iceAge.unlockedMessage = "Create an ice age zoo!";
  iceAge.lockedIconPath = "ui/scenario/lockj/lockj";
  iceAge.unlockedIconPath = "ui/scenario/iconp/iconp";
  iceAge.completedIconPath = "ui/scenario/iconc/iconc";
  iceAge.useFileObjectives = true;
  addScenario(iceAge);

  // ============================================
  // DINOSAUR DIGS - INTERMEDIATE
  // ============================================

  ScenarioEntry valley;
  valley.id = "cg";
  valley.name = "Dinosaur Digs: Valley of the Dinosaurs (Intermediate)";
  valley.scenarioPath = "scenario/scn23/scn23.scn";
  valley.expansion = ScenarioExpansion::DINOSAUR_DIGS;
  valley.difficulty = "Intermediate";
  valley.state = ScenarioState::LOCKED;
  valley.lockedMessage = "You must complete all of the Dinosaur Digs beginner "
                         "scenarios to unlock this scenario.";
  valley.unlockedMessage = "Explore the Valley of the Dinosaurs!";
  valley.lockedIconPath = "ui/scenario/lockj/lockj";
  valley.unlockedIconPath = "ui/scenario/iconp/iconp";
  valley.completedIconPath = "ui/scenario/iconc/iconc";
  valley.useFileObjectives = true;
  addScenario(valley);

  ScenarioEntry jurassic;
  jurassic.id = "ch";
  jurassic.name = "Jurassic Zoo";
  jurassic.scenarioPath = "scenario/scn21/scn21.scn";
  jurassic.expansion = ScenarioExpansion::DINOSAUR_DIGS;
  jurassic.difficulty = "Intermediate";
  jurassic.state = ScenarioState::LOCKED;
  jurassic.lockedMessage = "You must complete all of the Dinosaur Digs "
                           "beginner scenarios to unlock this scenario.";
  jurassic.unlockedMessage = "Build a zoo from the Jurassic period!";
  jurassic.lockedIconPath = "ui/scenario/lockj/lockj";
  jurassic.unlockedIconPath = "ui/scenario/iconp/iconp";
  jurassic.completedIconPath = "ui/scenario/iconc/iconc";
  jurassic.useFileObjectives = true;
  addScenario(jurassic);

  // ============================================
  // DINOSAUR DIGS - ADVANCED
  // ============================================

  ScenarioEntry lab;
  lab.id = "de";
  lab.name = "Dinosaur Digs: Dinosaur Island Research Lab (Advanced)";
  lab.scenarioPath = "scenario/scn24/scn24.scn";
  lab.expansion = ScenarioExpansion::DINOSAUR_DIGS;
  lab.difficulty = "Advanced";
  lab.state = ScenarioState::LOCKED;
  lab.lockedMessage = "You must complete all of the Dinosaur Digs intermediate "
                      "scenarios to unlock this scenario.";
  lab.unlockedMessage = "Run the Dinosaur Island Research Lab!";
  lab.lockedIconPath = "ui/scenario/lockj/lockj";
  lab.unlockedIconPath = "ui/scenario/iconp/iconp";
  lab.completedIconPath = "ui/scenario/iconc/iconc";
  lab.useFileObjectives = true;
  addScenario(lab);

  // ============================================
  // DINOSAUR DIGS - VERY ADVANCED
  // ============================================

  ScenarioEntry lostWorld;
  lostWorld.id = "fb";
  lostWorld.name =
      "Dinosaur Digs: Return to Dinosaur Island Research Lab (Very Advanced)";
  lostWorld.scenarioPath = "scenario/scn25/scn25.scn";
  lostWorld.expansion = ScenarioExpansion::DINOSAUR_DIGS;
  lostWorld.difficulty = "Very Advanced";
  lostWorld.state = ScenarioState::LOCKED;
  lostWorld.lockedMessage = "You must complete all of the Dinosaur Digs "
                            "advanced scenarios to unlock this scenario.";
  lostWorld.unlockedMessage = "Discover a lost world!";
  lostWorld.lockedIconPath = "ui/scenario/lockj/lockj";
  lostWorld.unlockedIconPath = "ui/scenario/iconp/iconp";
  lostWorld.completedIconPath = "ui/scenario/iconc/iconc";
  lostWorld.useFileObjectives = true;
  addScenario(lostWorld);

  ScenarioEntry trex;
  trex.id = "ga";
  trex.name = "Dinosaur Digs: Breeding the T. rex (Very Advanced)";
  trex.scenarioPath = "scenario/scn26/scn26.scn";
  trex.expansion = ScenarioExpansion::DINOSAUR_DIGS;
  trex.difficulty = "Very Advanced";
  trex.state = ScenarioState::LOCKED;
  trex.lockedMessage = "You must complete the very advanced Return to Dinosaur "
                       "Island Research Lab scenario to unlock this scenario.";
  trex.unlockedMessage = "Breed the mighty T. rex!";
  trex.lockedIconPath = "ui/scenario/lockj/lockj";
  trex.unlockedIconPath = "ui/scenario/iconp/iconp";
  trex.completedIconPath = "ui/scenario/iconc/iconc";
  trex.useFileObjectives = true;
  addScenario(trex);
}

void ScenarioDatabase::initMarineManiaSenarios() {
  // ============================================
  // MARINE MANIA - TUTORIALS
  // ============================================

  ScenarioEntry mmTut1;
  mmTut1.id = "ae";
  mmTut1.name = "Marine Mania Tutorial 1";
  mmTut1.scenarioPath = "scenario/scn27/scn27.scn";
  mmTut1.expansion = ScenarioExpansion::MARINE_MANIA;
  mmTut1.difficulty = "Beginner";
  mmTut1.state = ScenarioState::UNLOCKED;
  mmTut1.unlockedMessage = "Learn to build aquatic exhibits!";
  mmTut1.lockedIconPath = "ui/scenario/lockk/lockk";
  mmTut1.unlockedIconPath = "ui/scenario/iconp/iconp";
  mmTut1.completedIconPath = "ui/scenario/iconc/iconc";
  mmTut1.useFileObjectives = false;
  mmTut1.objectives = {
      " - Filter game content by Marine Mania.",
      " - Hire a marine specialist.",
      " - Build a dolphin exhibit with tank walls.",
      " - Adopt and place a bottlenose dolphin in the marine exhibit.",
      " - Purchase and place kelp foliage in the dolphin exhibit.",
      " - Purchase a tank filter.",
      " - Raise the exhibit tank walls.",
      " - Lower the exhibit tank walls.",
      " - Raise the exhibit tank base.",
      " - Lower the exhibit tank base.",
      " - Purchase and place a swim with dolphins building."};
  addScenario(mmTut1);

  ScenarioEntry mmTut2;
  mmTut2.id = "af";
  mmTut2.name = "Marine Mania Tutorial 2";
  mmTut2.scenarioPath = "scenario/scn28/scn28.scn";
  mmTut2.expansion = ScenarioExpansion::MARINE_MANIA;
  mmTut2.difficulty = "Beginner";
  mmTut2.state = ScenarioState::UNLOCKED;
  mmTut2.unlockedMessage = "Learn about shows!";
  mmTut2.lockedIconPath = "ui/scenario/lockk/lockk";
  mmTut2.unlockedIconPath = "ui/scenario/iconp/iconp";
  mmTut2.completedIconPath = "ui/scenario/iconc/iconc";
  mmTut2.useFileObjectives = false;
  mmTut2.objectives = {
      " - View the show animals, then filter content by Marine Mania.",
      " - Adopt and place an orca in the tank exhibit.",
      " - Build an orca show exhibit adjacent to the orca exhibit.",
      " - Purchase and place 3 grandstands.",
      " - Add a trick to the orca's show script.",
      " - Change the orca's show frequency to infrequent.",
      " - Purchase an orca ball.",
      " - Add the 'play with ball' trick to the orca's script.",
      " - Fund research for a trick program."};
  addScenario(mmTut2);

  ScenarioEntry mmTut3;
  mmTut3.id = "ag";
  mmTut3.name = "Marine Mania Tutorial 3";
  mmTut3.scenarioPath = "scenario/scn28b/scn28b.scn";
  mmTut3.expansion = ScenarioExpansion::MARINE_MANIA;
  mmTut3.difficulty = "Beginner";
  mmTut3.state = ScenarioState::UNLOCKED;
  mmTut3.unlockedMessage = "Learn about combined exhibits!";
  mmTut3.lockedIconPath = "ui/scenario/lockk/lockk";
  mmTut3.unlockedIconPath = "ui/scenario/iconp/iconp";
  mmTut3.completedIconPath = "ui/scenario/iconc/iconc";
  mmTut3.useFileObjectives = false;
  mmTut3.objectives = {
      " - Complete the land exhibit next to the walrus tank.",
      " - Lower the walrus tank to be flush with the land exhibit.",
      " - Hire a zookeeper.",
      " - Lower the show tank so that the pass-through gate appears."};
  addScenario(mmTut3);

  // ============================================
  // MARINE MANIA - BEGINNER
  // ============================================

  ScenarioEntry orca;
  orca.id = "bf";
  orca.name = "Orca Show";
  orca.scenarioPath = "scenario/scn29/scn29.scn";
  orca.expansion = ScenarioExpansion::MARINE_MANIA;
  orca.difficulty = "Beginner";
  orca.state = ScenarioState::UNLOCKED;
  orca.unlockedMessage = "Put on an Orca show!";
  orca.lockedIconPath = "ui/scenario/lockk/lockk";
  orca.unlockedIconPath = "ui/scenario/iconp/iconp";
  orca.completedIconPath = "ui/scenario/iconc/iconc";
  orca.useFileObjectives = true;
  addScenario(orca);

  ScenarioEntry seasideMM;
  seasideMM.id = "bg";
  seasideMM.name = "Seasideville Dolphin Park";
  seasideMM.scenarioPath = "scenario/scn31/scn31.scn";
  seasideMM.expansion = ScenarioExpansion::MARINE_MANIA;
  seasideMM.difficulty = "Beginner";
  seasideMM.state = ScenarioState::UNLOCKED;
  seasideMM.unlockedMessage = "Create a dolphin park!";
  seasideMM.lockedIconPath = "ui/scenario/lockk/lockk";
  seasideMM.unlockedIconPath = "ui/scenario/iconp/iconp";
  seasideMM.completedIconPath = "ui/scenario/iconc/iconc";
  seasideMM.useFileObjectives = true;
  addScenario(seasideMM);

  ScenarioEntry shark;
  shark.id = "bh";
  shark.name = "Shark World";
  shark.scenarioPath = "scenario/scn30/scn30.scn";
  shark.expansion = ScenarioExpansion::MARINE_MANIA;
  shark.difficulty = "Beginner";
  shark.state = ScenarioState::UNLOCKED;
  shark.unlockedMessage = "Create a shark exhibit!";
  shark.lockedIconPath = "ui/scenario/lockk/lockk";
  shark.unlockedIconPath = "ui/scenario/iconp/iconp";
  shark.completedIconPath = "ui/scenario/iconc/iconc";
  shark.useFileObjectives = true;
  addScenario(shark);

  // ============================================
  // MARINE MANIA - INTERMEDIATE
  // ============================================

  ScenarioEntry oceans;
  oceans.id = "ci";
  oceans.name = "Oceans of the World Zoo";
  oceans.scenarioPath = "scenario/scn39/scn39.scn";
  oceans.expansion = ScenarioExpansion::MARINE_MANIA;
  oceans.difficulty = "Intermediate";
  oceans.state = ScenarioState::LOCKED;
  oceans.lockedMessage = "You must complete all of the Marine Mania beginner "
                         "scenarios to unlock this scenario.";
  oceans.unlockedMessage = "Build an oceans of the world zoo!";
  oceans.lockedIconPath = "ui/scenario/lockk/lockk";
  oceans.unlockedIconPath = "ui/scenario/iconp/iconp";
  oceans.completedIconPath = "ui/scenario/iconc/iconc";
  oceans.useFileObjectives = true;
  addScenario(oceans);

  ScenarioEntry saveMarine;
  saveMarine.id = "cj";
  saveMarine.name = "Save the Marine Animals";
  saveMarine.scenarioPath = "scenario/scn32/scn32.scn";
  saveMarine.expansion = ScenarioExpansion::MARINE_MANIA;
  saveMarine.difficulty = "Intermediate";
  saveMarine.state = ScenarioState::LOCKED;
  saveMarine.lockedMessage = "You must complete all of the Marine Mania "
                             "beginner scenarios to unlock this scenario.";
  saveMarine.unlockedMessage = "Save the marine animals!";
  saveMarine.lockedIconPath = "ui/scenario/lockk/lockk";
  saveMarine.unlockedIconPath = "ui/scenario/iconp/iconp";
  saveMarine.completedIconPath = "ui/scenario/iconc/iconc";
  saveMarine.useFileObjectives = true;
  addScenario(saveMarine);

  ScenarioEntry free;
  free.id = "ck";
  free.name = "Free Admission";
  free.scenarioPath = "scenario/scn34/scn34.scn";
  free.expansion = ScenarioExpansion::MARINE_MANIA;
  free.difficulty = "Intermediate";
  free.state = ScenarioState::LOCKED;
  free.lockedMessage = "You must complete all of the Marine Mania beginner "
                       "scenarios to unlock this scenario.";
  free.unlockedMessage = "Run a free admission zoo!";
  free.lockedIconPath = "ui/scenario/lockk/lockk";
  free.unlockedIconPath = "ui/scenario/iconp/iconp";
  free.completedIconPath = "ui/scenario/iconc/iconc";
  free.useFileObjectives = true;
  addScenario(free);

  ScenarioEntry show;
  show.id = "cl";
  show.name = "Marine Mania: Aquatic Show Park (Intermediate)";
  show.scenarioPath = "scenario/scn33/scn33.scn";
  show.expansion = ScenarioExpansion::MARINE_MANIA;
  show.difficulty = "Intermediate";
  show.state = ScenarioState::LOCKED;
  show.lockedMessage = "You must complete all of the Marine Mania beginner "
                       "scenarios to unlock this scenario.";
  show.unlockedMessage = "Create an amazing aquatic show park!";
  show.lockedIconPath = "ui/scenario/lockk/lockk";
  show.unlockedIconPath = "ui/scenario/iconp/iconp";
  show.completedIconPath = "ui/scenario/iconc/iconc";
  show.useFileObjectives = true;
  addScenario(show);

  // ============================================
  // MARINE MANIA - ADVANCED
  // ============================================

  ScenarioEntry conservation;
  conservation.id = "df";
  conservation.name = "Marine Conservation";
  conservation.scenarioPath = "scenario/scn35/scn35.scn";
  conservation.expansion = ScenarioExpansion::MARINE_MANIA;
  conservation.difficulty = "Advanced";
  conservation.state = ScenarioState::LOCKED;
  conservation.lockedMessage =
      "You must complete all of the Marine Mania "
      "intermediate scenarios to unlock this scenario.";
  conservation.unlockedMessage = "Focus on marine conservation!";
  conservation.lockedIconPath = "ui/scenario/lockk/lockk";
  conservation.unlockedIconPath = "ui/scenario/iconp/iconp";
  conservation.completedIconPath = "ui/scenario/iconc/iconc";
  conservation.useFileObjectives = true;
  addScenario(conservation);

  ScenarioEntry saveZoo;
  saveZoo.id = "dg";
  saveZoo.name = "Save the Zoo";
  saveZoo.scenarioPath = "scenario/scn36/scn36.scn";
  saveZoo.expansion = ScenarioExpansion::MARINE_MANIA;
  saveZoo.difficulty = "Advanced";
  saveZoo.state = ScenarioState::LOCKED;
  saveZoo.lockedMessage = "You must complete all of the Marine Mania "
                          "intermediate scenarios to unlock this scenario.";
  saveZoo.unlockedMessage = "Save the zoo from bankruptcy!";
  saveZoo.lockedIconPath = "ui/scenario/lockk/lockk";
  saveZoo.unlockedIconPath = "ui/scenario/iconp/iconp";
  saveZoo.completedIconPath = "ui/scenario/iconc/iconc";
  saveZoo.useFileObjectives = true;
  addScenario(saveZoo);

  // ============================================
  // MARINE MANIA - VERY ADVANCED
  // ============================================

  ScenarioEntry giant;
  giant.id = "gb";
  giant.name = "Marine Mania: Giant Marine Park (Very Advanced)";
  giant.scenarioPath = "scenario/scn37/scn37.scn";
  giant.expansion = ScenarioExpansion::MARINE_MANIA;
  giant.difficulty = "Very Advanced";
  giant.state = ScenarioState::LOCKED;
  giant.lockedMessage =
      "You must complete the advanced Marine Conservation and Save the Zoo "
      "scenarios to unlock this scenario.";
  giant.unlockedMessage = "Build a massive marine park!";
  giant.lockedIconPath = "ui/scenario/lockk/lockk";
  giant.unlockedIconPath = "ui/scenario/iconp/iconp";
  giant.completedIconPath = "ui/scenario/iconc/iconc";
  giant.useFileObjectives = true;
  addScenario(giant);

  ScenarioEntry super;
  super.id = "gc";
  super.name = "Marine Mania: Super Zoo (Very Advanced)";
  super.scenarioPath = "scenario/scn38/scn38.scn";
  super.expansion = ScenarioExpansion::MARINE_MANIA;
  super.difficulty = "Very Advanced";
  super.state = ScenarioState::LOCKED;
  super.lockedMessage =
      "You must complete the advanced Marine Conservation and Save the Zoo "
      "scenarios to unlock this scenario.";
  super.unlockedMessage = "Create the Super Zoo!";
  super.lockedIconPath = "ui/scenario/lockk/lockk";
  super.unlockedIconPath = "ui/scenario/iconp/iconp";
  super.completedIconPath = "ui/scenario/iconc/iconc";
  super.useFileObjectives = true;
  addScenario(super);
}

void ScenarioDatabase::saveState(const std::string &filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    SDL_Log("ScenarioDatabase: Failed to save state to %s", filename.c_str());
    return;
  }

  file << "{\n";
  file << "  \"scenarios\": {\n";

  for (size_t i = 0; i < scenarios.size(); i++) {
    const auto &s = scenarios[i];
    std::string stateStr;
    switch (s.state) {
    case ScenarioState::LOCKED:
      stateStr = "locked";
      break;
    case ScenarioState::UNLOCKED:
      stateStr = "unlocked";
      break;
    case ScenarioState::COMPLETED:
      stateStr = "completed";
      break;
    }

    file << "    \"" << s.id << "\": \"" << stateStr << "\"";
    if (i < scenarios.size() - 1)
      file << ",";
    file << "\n";
  }

  file << "  }\n";
  file << "}\n";

  file.close();
  SDL_Log("ScenarioDatabase: Saved state to %s", filename.c_str());
}

bool ScenarioDatabase::loadState(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    SDL_Log("ScenarioDatabase: No saved state found at %s", filename.c_str());
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();

  // Simple JSON parsing for scenario states
  for (auto &s : scenarios) {
    std::string searchKey = "\"" + s.id + "\": \"";
    size_t pos = content.find(searchKey);
    if (pos != std::string::npos) {
      pos += searchKey.length();
      size_t endPos = content.find("\"", pos);
      if (endPos != std::string::npos) {
        std::string stateStr = content.substr(pos, endPos - pos);
        if (stateStr == "locked") {
          s.state = ScenarioState::LOCKED;
        } else if (stateStr == "unlocked") {
          s.state = ScenarioState::UNLOCKED;
        } else if (stateStr == "completed") {
          s.state = ScenarioState::COMPLETED;
        }
      }
    }

    else {
      // Legacy format check (Name -> Int)
      // Look for "Name": 1
      std::string searchKeyLegacy = "\"" + s.name + "\": ";
      pos = content.find(searchKeyLegacy);
      if (pos != std::string::npos) {
        pos += searchKeyLegacy.length();
        // Scan for digit
        for (size_t k = 0; k < 20 && (pos + k) < content.length(); k++) {
          char c = content[pos + k];
          if (isdigit(c)) {
            int val = c - '0';
            if (val == 1)
              s.state = ScenarioState::UNLOCKED;
            else if (val == 2)
              s.state = ScenarioState::COMPLETED;
            else
              s.state = ScenarioState::LOCKED;
            break;
          }
        }
      }
    }
  }

  SDL_Log("ScenarioDatabase: Loaded state from %s", filename.c_str());
  return true;
}
