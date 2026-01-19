#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <algorithm>
#include <string>

#include "Config.hpp"
#include "IniReader.hpp"
#include "Input.hpp"
#include "InputManager.hpp"
#include "LoadScreen.hpp"
#include "ResourceManager.hpp"
#include "ScenarioManager.hpp"

#include "Utils.hpp"
#include "Window.hpp"

#include "ui/UiImage.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiText.hpp"

// --- FORWARD DECLARATIONS ---
static void updateScenarioDetails(UiLayout *layout,
                                  ScenarioManager *scenarioManager,
                                  ResourceManager *resourceManager);
static void updateFreeformDetails(UiLayout *layout,
                                  ScenarioManager *scenarioManager,
                                  ResourceManager *resourceManager);
static void populateScenarioList(UiLayout *layout,
                                 ScenarioManager *scenarioManager);
static void populateFreeformList(UiLayout *layout,
                                 ScenarioManager *scenarioManager);

ScenarioManager *g_scenarioManager = nullptr;
// UserProfile removed - using ScenarioDatabase

enum class LayoutState {
  MAIN_MENU,
  SCENARIO_SELECT,
  FREEFORM_SELECT,
  CREDITS,
  OPTIONS
};
LayoutState g_currentState = LayoutState::MAIN_MENU;

UiListBox *g_scenarioListBox = nullptr;
UiListBox *g_freeformListBox = nullptr;
UiText *g_scenarioDescText = nullptr;
UiText *g_freeformDescText = nullptr;
UiImage *g_scenarioMap = nullptr;
UiImage *g_freeformMap = nullptr;

// [PATCH] Starting Cash UI
UiText *g_startingCashText = nullptr;
UiText *g_difficultyText = nullptr;
int g_currentStartingCash = 100000; // Default $100,000
const int CASH_VALUES[] = {25000,  50000,  75000,  100000,
                           150000, 200000, 250000, 500000};
const int NUM_CASH_VALUES = 8;
int g_cashIndex = 3; // Index into CASH_VALUES (starts at $100,000)

// These IDs trigger the TARGET_WIDTH/HEIGHT resize in UiImage.cpp
static constexpr int SCENARIO_PREVIEW_IMAGE_ID = 50001;
static constexpr int FREEFORM_PREVIEW_IMAGE_ID = 11501;

// --- HELPER: GET DIFFICULTY LEVEL ---
static std::string getDifficultyLevel(const std::string &name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower.find("very advanced") != std::string::npos)
    return "Very Advanced";
  if (lower.find("advanced") != std::string::npos)
    return "Advanced";
  if (lower.find("intermediate") != std::string::npos)
    return "Intermediate";
  if (lower.find("beginner") != std::string::npos)
    return "Beginner";
  return "Beginner";
}

// --- HELPER: GET LABEL FOR LIST DISPLAY ---
static std::string getDifficultyLabel(const std::string &name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower.find("tutorial") != std::string::npos)
    return "";
  return "(" + getDifficultyLevel(name) + ")";
}

static std::string getFolderFromPath(const std::string &path) {
  size_t lastSlash = path.find_last_of("/\\");
  if (lastSlash == std::string::npos)
    return "";
  return path.substr(0, lastSlash);
}

static std::string getFileStem(const std::string &path) {
  size_t lastSlash = path.find_last_of("/\\");
  std::string file =
      (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);

  size_t dot = file.find_last_of('.');
  if (dot == std::string::npos)
    return file;
  return file.substr(0, dot);
}

// --- ROUTE THROUGH UIIMAGE SCRIPT FOR RESIZING ---
static void setScenarioPreview(UiImage *img, ResourceManager *rm,
                               const std::string &scnPath, bool isLocked,
                               const std::string &scenarioName = "") {
  if (img == nullptr || rm == nullptr)
    return;

  if (isLocked) {
    // Determine lock image based on expansion from scenario name
    std::string lockFolder = "ui/scenario/lock";
    std::string lockFileName = "lock";

    std::string lowerName = scenarioName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   ::tolower);

    if (lowerName.find("dinosaur digs") != std::string::npos) {
      lockFolder = "ui/scenario/lockj";
      lockFileName = "lockj";
    } else if (lowerName.find("marine mania") != std::string::npos) {
      lockFolder = "ui/scenario/lockk";
      lockFileName = "lockk";
    }

    // Try PNG first (custom images), then fall back to ZT1 format
    std::string lockPng = lockFolder + "/" + lockFileName + ".png";
    std::string lockRaw = lockFolder + "/N";
    std::string lockPal = lockFolder + "/" + lockFileName + ".pal";

    if (rm->hasResource(lockPng)) {
      img->setImage(lockPng);
    } else if (rm->hasResource(lockRaw)) {
      img->setZt1Image(lockRaw, lockPal);
    } else {
      img->setImage("");
    }
    return;
  }

  // Standard Unlocked Routing
  std::string folder = getFolderFromPath(scnPath);
  std::string stem = getFileStem(scnPath);

  // Try multiple path patterns since expansions use different conventions:
  // Base game: scenario/scn03/scn03/N with scn03.pal
  // Dinosaur Digs: scenario/scn23/map23/N with map23.pal
  std::vector<std::pair<std::string, std::string>> pathsToTry;

  // Pattern 1: folder/stem/N (base game pattern: scn03/scn03/N)
  pathsToTry.push_back(
      {folder + "/" + stem + "/N", folder + "/" + stem + "/" + stem + ".pal"});

  // Pattern 2: folder/map##/N (Dinosaur Digs pattern: scn23/map23/N)
  // Extract number from stem (e.g., "scn23" -> "23")
  std::string numPart;
  for (char c : stem) {
    if (c >= '0' && c <= '9') {
      numPart += c;
    }
  }
  if (!numPart.empty()) {
    std::string mapFolder = "map" + numPart;
    pathsToTry.push_back({folder + "/" + mapFolder + "/N",
                          folder + "/" + mapFolder + "/" + mapFolder + ".pal"});
  }

  // Try each pattern until one works
  for (const auto &paths : pathsToTry) {
    const std::string &raw = paths.first;
    const std::string &pal = paths.second;
    if (rm->hasResource(raw) && rm->hasResource(pal)) {
      img->setZt1Image(raw, pal);
      return;
    }
  }
}

static void setFreeformPreview(UiImage *img, ResourceManager *rm,
                               const std::string &freeformScnPath) {
  if (img == nullptr || rm == nullptr)
    return;

  std::string baseFolder = getFolderFromPath(freeformScnPath);
  std::string stem = getFileStem(freeformScnPath);

  std::string raw = baseFolder + "/" + stem + "/N";
  std::string pal = baseFolder + "/" + stem + "/" + stem + ".pal";

  if (rm->hasResource(raw) && rm->hasResource(pal)) {
    img->setZt1Image(raw, pal);
  }
}

static std::string getLayoutPath(ResourceManager *rm,
                                 const std::string &filename) {
  // Layout files are in ui/ folder, subfolders contain assets only
  return "ui/" + filename;
}

static void updateScenarioDetails(UiLayout *layout,
                                  ScenarioManager *scenarioManager,
                                  ResourceManager *resourceManager) {
  if (!layout || !scenarioManager || !resourceManager)
    return;
  if (!g_scenarioListBox)
    return;

  int selectedIdx = g_scenarioListBox->getSelectedIndex();
  if (selectedIdx < 0)
    return;

  const ScenarioInfo *scenario = scenarioManager->getScenario(selectedIdx);
  if (!scenario)
    return;

  bool isLocked = scenario->isLocked;
  // Fallback for non-database legacy scenarios
  if (scenario->iconPath.empty()) {
    // If not in database, assume unlocked for now, or check internal logic
    // But parseScenarioConfig should have handled this.
  }

  if (!g_scenarioDescText) {
    UiElement *elem = layout->getElementById(50004);
    if (elem)
      g_scenarioDescText = dynamic_cast<UiText *>(elem);
  }
  if (g_scenarioDescText) {
    if (g_scenarioDescText) {
      if (isLocked) {
        if (!scenario->stateMessage.empty()) {
          g_scenarioDescText->setText(scenario->stateMessage);
        } else {
          // Fallback for scenarios not in database
          std::string diff = getDifficultyLevel(scenario->name);
          std::string required = "beginner";
          if (diff == "Very Advanced")
            required = "advanced";
          else if (diff == "Advanced")
            required = "intermediate";
          else if (diff == "Intermediate")
            required = "beginner";
          std::string msg = "You must complete all of the Zoo Tycoon " +
                            required + " scenarios to unlock this scenario.";
          g_scenarioDescText->setText(msg);
        }
      } else {
        std::string desc =
            scenarioManager->loadScenarioDescription(scenario->scenarioPath);
        g_scenarioDescText->setText(desc);
      }
    }
  }

  if (!g_scenarioMap) {
    UiElement *elem = layout->getElementById(SCENARIO_PREVIEW_IMAGE_ID);
    if (elem)
      g_scenarioMap = dynamic_cast<UiImage *>(elem);
  }
  if (g_scenarioMap) {
    setScenarioPreview(g_scenarioMap, resourceManager, scenario->scenarioPath,
                       isLocked, scenario->name);
  }

  UiElement *objEl = layout->getElementById(50006);
  if (objEl) {
    UiListBox *objList = dynamic_cast<UiListBox *>(objEl);
    if (objList) {
      objList->clear();
      if (!isLocked) {
        std::vector<std::string> goals =
            scenarioManager->loadScenarioObjectives(scenario->scenarioPath);
        for (const auto &goal : goals) {
          objList->addItem(goal);
        }
      } else {
        objList->addItem("");
      }
    }
  }
}

static void updateFreeformDetails(UiLayout *layout,
                                  ScenarioManager *scenarioManager,
                                  ResourceManager *resourceManager) {
  if (!layout || !scenarioManager || !resourceManager)
    return;
  if (!g_freeformListBox)
    return;

  int selectedIdx = g_freeformListBox->getSelectedIndex();
  if (selectedIdx < 0)
    return;

  const FreeformMap *map = scenarioManager->getFreeformMap(selectedIdx);
  if (!map)
    return;

  if (!g_freeformDescText) {
    UiElement *elem = layout->getElementById(11507);
    if (elem)
      g_freeformDescText = dynamic_cast<UiText *>(elem);
  }
  if (g_freeformDescText) {
    std::string desc = map->description.empty() ? map->name : map->description;
    g_freeformDescText->setText(desc);
  }

  if (!g_freeformMap) {
    UiElement *elem = layout->getElementById(FREEFORM_PREVIEW_IMAGE_ID);
    if (elem)
      g_freeformMap = dynamic_cast<UiImage *>(elem);
  }
  if (g_freeformMap) {
    setFreeformPreview(g_freeformMap, resourceManager, map->path);
  }
}

static void populateScenarioList(UiLayout *layout,
                                 ScenarioManager *scenarioManager) {
  SDL_Log("Populating scenario list...");

  g_scenarioListBox = nullptr;
  g_scenarioDescText = nullptr;
  g_scenarioMap = nullptr;

  UiElement *element = layout->getElementById(50002);
  if (element) {
    g_scenarioListBox = dynamic_cast<UiListBox *>(element);
    if (g_scenarioListBox) {
      g_scenarioListBox->clear();
      g_scenarioListBox->setSelectionAction(UiAction::SCENARIO_LIST_SELECTION);

      for (const auto &scenario : scenarioManager->getScenarios()) {
        std::string iconPath = scenario.iconPath;

        // Fallback for scenarios not in database
        if (iconPath.empty()) {
          if (!scenario.iconPath.empty()) {
            iconPath = scenario.iconPath;
          } else {
            // Fallback default
            iconPath = "ui/scenario/iconp/iconp";
          }
        }

        std::string displayName = scenario.name;
        std::string diffLabel = getDifficultyLabel(displayName);

        if (!diffLabel.empty() && displayName.find("(") == std::string::npos) {
          displayName += " " + diffLabel;
        }

        g_scenarioListBox->addItem(displayName, scenario.scenarioPath,
                                   iconPath);
      }
    }
  }
}

static void populateFreeformList(UiLayout *layout,
                                 ScenarioManager *scenarioManager) {
  g_freeformListBox = nullptr;
  g_freeformDescText = nullptr;
  g_freeformMap = nullptr;
  g_startingCashText = nullptr;
  g_difficultyText = nullptr;

  UiElement *element = layout->getElementById(11504);
  if (element) {
    g_freeformListBox = dynamic_cast<UiListBox *>(element);
    if (g_freeformListBox) {
      g_freeformListBox->clear();
      g_freeformListBox->setSelectionAction(UiAction::FREEFORM_LIST_SELECTION);

      for (const auto &map : scenarioManager->getFreeformMaps()) {
        // Build display name with size indicator
        std::string displayName = map.name;
        if (!map.size.empty()) {
          displayName += " (" + map.size + ")";
        }
        g_freeformListBox->addItem(displayName, map.path);
      }
    }
  }

  // Initialize Starting Cash text (ID 11510)
  element = layout->getElementById(11510);
  if (element) {
    g_startingCashText = dynamic_cast<UiText *>(element);
    if (g_startingCashText) {
      // Format cash with $ and commas
      char buf[32];
      snprintf(buf, sizeof(buf), "$%d,000", g_currentStartingCash / 1000);
      g_startingCashText->setText(buf);
    }
  }

  // Initialize Difficulty text (ID 11531)
  element = layout->getElementById(11531);
  if (element) {
    g_difficultyText = dynamic_cast<UiText *>(element);
    if (g_difficultyText) {
      g_difficultyText->setText("Intermediate");
    }
  }
}

int main(int argc, char *argv[]) {
  SDL_SetMainReady();

  Config config;
  ResourceManager resource_manager(&config);

  Window window("ZT1-Engine", config.getScreenWidth(), config.getScreenHeight(),
                60.0f);
  window.set_cursor(resource_manager.getCursor(9));

  LoadScreen::run(&window, &config, &resource_manager);

  g_scenarioManager = new ScenarioManager(&resource_manager);
  g_scenarioManager->loadScenarios();
  g_scenarioManager->loadFreeformMaps();

  // Load saved scenario states from project root saves/ folder
  // Path is relative to executable: build/Release/zt1-engine.exe ->
  // ../../saves/
  if (!g_scenarioManager->getDatabase()->loadState(
          "../../saves/user_scenarios.json")) {
    // No save found, create default
    SDL_Log("No save found, creating default save file.");
    g_scenarioManager->getDatabase()->saveState(
        "../../saves/user_scenarios.json");
  }

  // Sync scenarios with database and sort by difficulty
  g_scenarioManager->syncWithDatabase();
  g_scenarioManager->sortByDifficulty();

  IniReader *lyt_reader = resource_manager.getIniReader(
      getLayoutPath(&resource_manager, "startup.lyt"));
  UiLayout *layout = new UiLayout(lyt_reader, &resource_manager);
  g_currentState = LayoutState::MAIN_MENU;

  InputManager input_manager;
  std::vector<Input> inputs;

  int running = 1;
  UiAction action = UiAction::NONE;

  while (running > 0) {
    window.clear();
    inputs = input_manager.getInputs();

    for (Input input : inputs) {
      if (input.event == InputEvent::QUIT) {
        running = 0;
      }
    }

    action = layout->handleInputs(inputs);
    switch (action) {
    case UiAction::STARTUP_EXIT:
      running = false;
      break;

    case UiAction::STARTUP_CREDITS:
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "credits.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      g_currentState = LayoutState::CREDITS;
      break;

    case UiAction::STARTUP_PLAY_FREEFORM:
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "mapselec.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      g_currentState = LayoutState::FREEFORM_SELECT;
      populateFreeformList(layout, g_scenarioManager);
      break;

    case UiAction::STARTUP_PLAY_SCENARIO:
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "scenario.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      g_currentState = LayoutState::SCENARIO_SELECT;
      populateScenarioList(layout, g_scenarioManager);
      break;

    case UiAction::CREDITS_EXIT:
    case UiAction::SCENARIO_BACK_TO_MAIN_MENU:
      delete layout;
      lyt_reader = resource_manager.getIniReader(
          getLayoutPath(&resource_manager, "startup.lyt"));
      layout = new UiLayout(lyt_reader, &resource_manager);
      g_currentState = LayoutState::MAIN_MENU;

      g_scenarioListBox = nullptr;
      g_freeformListBox = nullptr;
      g_scenarioDescText = nullptr;
      g_freeformDescText = nullptr;
      g_scenarioMap = nullptr;
      g_freeformMap = nullptr;
      break;

    case UiAction::SCENARIO_LIST_SELECTION:
      updateScenarioDetails(layout, g_scenarioManager, &resource_manager);
      break;

    case UiAction::FREEFORM_LIST_SELECTION:
      updateFreeformDetails(layout, g_scenarioManager, &resource_manager);
      break;

    case UiAction::CASH_SPINNER_UP:
      // Increase starting cash
      if (g_cashIndex < NUM_CASH_VALUES - 1) {
        g_cashIndex++;
        g_currentStartingCash = CASH_VALUES[g_cashIndex];
        if (g_startingCashText) {
          char buf[32];
          snprintf(buf, sizeof(buf), "$%d,000", g_currentStartingCash / 1000);
          g_startingCashText->setText(buf);
        }
      }
      break;

    case UiAction::CASH_SPINNER_DOWN:
      // Decrease starting cash
      if (g_cashIndex > 0) {
        g_cashIndex--;
        g_currentStartingCash = CASH_VALUES[g_cashIndex];
        if (g_startingCashText) {
          char buf[32];
          snprintf(buf, sizeof(buf), "$%d,000", g_currentStartingCash / 1000);
          g_startingCashText->setText(buf);
        }
      }
      break;

    default:
      break;
    }

    layout->draw(window.renderer, nullptr);
    window.present();
  }

  delete g_scenarioManager;

  delete layout;

  return 0;
}