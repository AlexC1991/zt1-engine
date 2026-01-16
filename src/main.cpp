#define SDL_MAIN_HANDLED // Tell SDL we handle our own main()

#include <SDL2/SDL.h>

#include "Config.hpp"
#include "IniReader.hpp"
#include "Input.hpp"
#include "InputManager.hpp"
#include "LoadScreen.hpp"
#include "ResourceManager.hpp"
#include "ScenarioManager.hpp"
#include "Window.hpp"

#include "ui/UiImage.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiText.hpp"

ScenarioManager *g_scenarioManager = nullptr;

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

// Real IDs from .lyt files
static constexpr int SCENARIO_PREVIEW_IMAGE_ID = 50001; // ui/scenario.lyt
static constexpr int FREEFORM_PREVIEW_IMAGE_ID = 11501; // ui/mapselec.lyt

static std::string getFolderFromPath(const std::string &path) {
  size_t lastSlash = path.find_last_of("/\\");
  if (lastSlash == std::string::npos) return "";
  return path.substr(0, lastSlash);
}

static std::string getFileStem(const std::string &path) {
  size_t lastSlash = path.find_last_of("/\\");
  std::string file =
    (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);

  size_t dot = file.find_last_of('.');
  if (dot == std::string::npos) return file;
  return file.substr(0, dot);
}

static void setScenarioPreview(
  UiImage *img,
  ResourceManager *rm,
  const std::string &scnPath
) {
  if (img == nullptr || rm == nullptr) return;

  // Example:
  // scenario/scn03/scn03.scn
  // preview files exist at:
  // scenario/scn03/scn03/N
  // scenario/scn03/scn03/scn03.pal
  std::string folder = getFolderFromPath(scnPath); // scenario/scn03
  std::string stem = getFileStem(scnPath);         // scn03

  std::string raw = folder + "/" + stem + "/N";
  std::string pal = folder + "/" + stem + "/" + stem + ".pal";

  if (rm->hasResource(raw) && rm->hasResource(pal)) {
    img->setZt1Image(raw, pal);
  } else {
    SDL_Log("Scenario preview missing raw/pal: %s | %s", raw.c_str(), pal.c_str());
  }
}

static void setFreeformPreview(
  UiImage *img,
  ResourceManager *rm,
  const std::string &freeformScnPath
) {
  if (img == nullptr || rm == nullptr) return;

  // Example:
  // freeform/lagoon.scn
  // preview files exist at:
  // freeform/lagoon/N
  // freeform/lagoon/lagoon.pal
  std::string baseFolder = getFolderFromPath(freeformScnPath); // freeform
  std::string stem = getFileStem(freeformScnPath);             // lagoon

  std::string raw = baseFolder + "/" + stem + "/N";
  std::string pal = baseFolder + "/" + stem + "/" + stem + ".pal";

  if (rm->hasResource(raw) && rm->hasResource(pal)) {
    img->setZt1Image(raw, pal);
  } else {
    SDL_Log("Freeform preview missing raw/pal: %s | %s", raw.c_str(), pal.c_str());
  }
}

static void updateScenarioDetails(
  UiLayout *layout,
  ScenarioManager *scenarioManager,
  ResourceManager *resourceManager
) {
  if (!layout || !scenarioManager || !resourceManager) return;
  if (!g_scenarioListBox) return;

  int selectedIdx = g_scenarioListBox->getSelectedIndex();
  if (selectedIdx < 0) return;

  const ScenarioInfo *scenario = scenarioManager->getScenario(selectedIdx);
  if (!scenario) return;

  SDL_Log("Updating scenario details for: %s", scenario->name.c_str());

  if (!g_scenarioDescText) {
    UiElement *elem = layout->getElementById(50004);
    if (elem) g_scenarioDescText = dynamic_cast<UiText *>(elem);
  }
  if (g_scenarioDescText) {
    std::string desc =
      scenarioManager->loadScenarioDescription(scenario->scenarioPath);
    g_scenarioDescText->setText(desc);
  }

  if (!g_scenarioMap) {
    UiElement *elem = layout->getElementById(SCENARIO_PREVIEW_IMAGE_ID);
    if (elem) g_scenarioMap = dynamic_cast<UiImage *>(elem);
  }
  if (g_scenarioMap) {
    setScenarioPreview(g_scenarioMap, resourceManager, scenario->scenarioPath);
  } else {
    SDL_Log(
      "Scenario preview UiImage (id=%d) not found in layout.",
      SCENARIO_PREVIEW_IMAGE_ID
    );
  }
}

static void updateFreeformDetails(
  UiLayout *layout,
  ScenarioManager *scenarioManager,
  ResourceManager *resourceManager
) {
  if (!layout || !scenarioManager || !resourceManager) return;
  if (!g_freeformListBox) return;

  int selectedIdx = g_freeformListBox->getSelectedIndex();
  if (selectedIdx < 0) return;

  const FreeformMap *map = scenarioManager->getFreeformMap(selectedIdx);
  if (!map) return;

  SDL_Log("Updating freeform details for: %s", map->name.c_str());

  if (!g_freeformDescText) {
    UiElement *elem = layout->getElementById(11507);
    if (elem) g_freeformDescText = dynamic_cast<UiText *>(elem);
  }
  if (g_freeformDescText) {
    std::string desc = map->description.empty() ? map->name : map->description;
    g_freeformDescText->setText(desc);
  }

  if (!g_freeformMap) {
    UiElement *elem = layout->getElementById(FREEFORM_PREVIEW_IMAGE_ID);
    if (elem) g_freeformMap = dynamic_cast<UiImage *>(elem);
  }
  if (g_freeformMap) {
    setFreeformPreview(g_freeformMap, resourceManager, map->path);
  } else {
    SDL_Log(
      "Freeform preview UiImage (id=%d) not found in layout.",
      FREEFORM_PREVIEW_IMAGE_ID
    );
  }
}

static void populateScenarioList(UiLayout *layout, ScenarioManager *scenarioManager) {
  SDL_Log(
    "Populating scenario list with %zu scenarios",
    scenarioManager->getScenarios().size()
  );

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
        g_scenarioListBox->addItem(scenario.name, scenario.scenarioPath);
      }
    }
  }
}

static void populateFreeformList(UiLayout *layout, ScenarioManager *scenarioManager) {
  SDL_Log(
    "Populating freeform list with %zu maps",
    scenarioManager->getFreeformMaps().size()
  );

  g_freeformListBox = nullptr;
  g_freeformDescText = nullptr;
  g_freeformMap = nullptr;

  UiElement *element = layout->getElementById(11504);
  if (element) {
    g_freeformListBox = dynamic_cast<UiListBox *>(element);
    if (g_freeformListBox) {
      g_freeformListBox->clear();
      g_freeformListBox->setSelectionAction(UiAction::FREEFORM_LIST_SELECTION);

      for (const auto &map : scenarioManager->getFreeformMaps()) {
        g_freeformListBox->addItem(map.name, map.path);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  SDL_SetMainReady();

  Config config;
  ResourceManager resource_manager(&config);

  Window window("ZT1-Engine", config.getScreenWidth(), config.getScreenHeight(), 60.0f);
  window.set_cursor(resource_manager.getCursor(9));

  LoadScreen::run(&window, &config, &resource_manager);

  g_scenarioManager = new ScenarioManager(&resource_manager);
  g_scenarioManager->loadScenarios();
  g_scenarioManager->loadFreeformMaps();

  IniReader *lyt_reader = resource_manager.getIniReader("ui/startup.lyt");
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
        lyt_reader = resource_manager.getIniReader("ui/credits.lyt");
        layout = new UiLayout(lyt_reader, &resource_manager);
        g_currentState = LayoutState::CREDITS;
        break;

      case UiAction::STARTUP_PLAY_FREEFORM:
        delete layout;
        lyt_reader = resource_manager.getIniReader("ui/mapselec.lyt");
        layout = new UiLayout(lyt_reader, &resource_manager);
        g_currentState = LayoutState::FREEFORM_SELECT;
        populateFreeformList(layout, g_scenarioManager);
        break;

      case UiAction::STARTUP_PLAY_SCENARIO:
        delete layout;
        lyt_reader = resource_manager.getIniReader("ui/scenario.lyt");
        layout = new UiLayout(lyt_reader, &resource_manager);
        g_currentState = LayoutState::SCENARIO_SELECT;
        populateScenarioList(layout, g_scenarioManager);
        break;

      case UiAction::CREDITS_EXIT:
      case UiAction::SCENARIO_BACK_TO_MAIN_MENU:
        delete layout;
        lyt_reader = resource_manager.getIniReader("ui/startup.lyt");
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