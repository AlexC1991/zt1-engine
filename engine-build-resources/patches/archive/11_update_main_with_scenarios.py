import os

def apply(src_dir, root_dir):
    """Update main.cpp to use ScenarioManager, populate lists, and handle selection"""
    
    filepath = os.path.join(src_dir, "main.cpp")
    
    if not os.path.exists(filepath):
        print("    ! main.cpp not found")
        return False
    
    with open(filepath, "r", encoding="utf-8") as f:
        old_content = f.read()
    
    # Check if already updated with full implementation
    if "updateScenarioDetails" in old_content:
        return False
    
    new_main = '''#define SDL_MAIN_HANDLED  // Tell SDL we handle our own main()

#include <SDL2/SDL.h>

#include "Config.hpp"
#include "Window.hpp"
#include "ResourceManager.hpp"
#include "IniReader.hpp"
#include "LoadScreen.hpp"
#include "InputManager.hpp"
#include "Input.hpp"
#include "ScenarioManager.hpp"

#include "AniFile.hpp"
#include "CompassDirection.hpp"

#include "ui/UiLayout.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiText.hpp"

// Global managers
ScenarioManager* g_scenarioManager = nullptr;

// Current layout state
enum class LayoutState {
    MAIN_MENU,
    SCENARIO_SELECT,
    FREEFORM_SELECT,
    CREDITS,
    OPTIONS
};
LayoutState g_currentState = LayoutState::MAIN_MENU;

// Cached list box pointers for quick access
UiListBox* g_scenarioListBox = nullptr;
UiListBox* g_freeformListBox = nullptr;
UiText* g_scenarioDescText = nullptr;
UiText* g_freeformDescText = nullptr;

// Update scenario description when selection changes
void updateScenarioDetails(UiLayout* layout, ScenarioManager* scenarioManager) {
    if (!g_scenarioListBox) return;
    
    int selectedIdx = g_scenarioListBox->getSelectedIndex();
    if (selectedIdx < 0) return;
    
    const ScenarioInfo* scenario = scenarioManager->getScenario(selectedIdx);
    if (!scenario) return;
    
    SDL_Log("Updating scenario details for: %s", scenario->name.c_str());
    
    // Update description text (id=50004 is Story text element)
    if (!g_scenarioDescText) {
        UiElement* elem = layout->getElementById(50004);
        if (elem) {
            g_scenarioDescText = dynamic_cast<UiText*>(elem);
        }
    }
    
    if (g_scenarioDescText) {
        std::string desc = scenarioManager->loadScenarioDescription(scenario->scenarioPath);
        g_scenarioDescText->setText(desc);
    }
}

// Update freeform map description when selection changes
void updateFreeformDetails(UiLayout* layout, ScenarioManager* scenarioManager) {
    if (!g_freeformListBox) return;
    
    int selectedIdx = g_freeformListBox->getSelectedIndex();
    if (selectedIdx < 0) return;
    
    const FreeformMap* map = scenarioManager->getFreeformMap(selectedIdx);
    if (!map) return;
    
    SDL_Log("Updating freeform details for: %s", map->name.c_str());
    
    // Update description text (id=11507 is Map description text element)
    if (!g_freeformDescText) {
        UiElement* elem = layout->getElementById(11507);
        if (elem) {
            g_freeformDescText = dynamic_cast<UiText*>(elem);
        }
    }
    
    if (g_freeformDescText) {
        // Use map description or name as fallback
        std::string desc = map->description;
        if (desc.empty()) {
            desc = map->name;
        }
        g_freeformDescText->setText(desc);
    }
}

// Populate scenario list (id=50002 from scenario.lyt)
void populateScenarioList(UiLayout* layout, ScenarioManager* scenarioManager) {
    SDL_Log("Populating scenario list with %zu scenarios", 
            scenarioManager->getScenarios().size());
    
    // Reset cached pointers
    g_scenarioListBox = nullptr;
    g_scenarioDescText = nullptr;
    
    // Find the ScenarioList element (id=50002)
    UiElement* element = layout->getElementById(50002);
    if (element) {
        g_scenarioListBox = dynamic_cast<UiListBox*>(element);
        if (g_scenarioListBox) {
            g_scenarioListBox->clear();
            g_scenarioListBox->setSelectionAction(UiAction::SCENARIO_LIST_SELECTION);
            
            for (const auto& scenario : scenarioManager->getScenarios()) {
                g_scenarioListBox->addItem(scenario.name, scenario.scenarioPath);
            }
            SDL_Log("Added %zu scenarios to list", scenarioManager->getScenarios().size());
        } else {
            SDL_Log("Element 50002 is not a UiListBox");
        }
    } else {
        SDL_Log("Could not find ScenarioList element (id=50002)");
    }
}

// Populate freeform map list (id=11504 from mapselec.lyt)
void populateFreeformList(UiLayout* layout, ScenarioManager* scenarioManager) {
    SDL_Log("Populating freeform list with %zu maps", 
            scenarioManager->getFreeformMaps().size());
    
    // Reset cached pointers
    g_freeformListBox = nullptr;
    g_freeformDescText = nullptr;
    
    // Find the Map List element (id=11504)
    UiElement* element = layout->getElementById(11504);
    if (element) {
        g_freeformListBox = dynamic_cast<UiListBox*>(element);
        if (g_freeformListBox) {
            g_freeformListBox->clear();
            g_freeformListBox->setSelectionAction(UiAction::FREEFORM_LIST_SELECTION);
            
            for (const auto& map : scenarioManager->getFreeformMaps()) {
                g_freeformListBox->addItem(map.name, map.path);
            }
            SDL_Log("Added %zu maps to list", scenarioManager->getFreeformMaps().size());
        } else {
            SDL_Log("Element 11504 is not a UiListBox");
        }
    } else {
        SDL_Log("Could not find Map List element (id=11504)");
    }
}

int main(int argc, char *argv[]) {
  SDL_SetMainReady();  // Required when using SDL_MAIN_HANDLED

  Config config;
  ResourceManager resource_manager(&config);

  Window window("ZT1-Engine", config.getScreenWidth(), config.getScreenHeight(), 60.0f);
  window.set_cursor(resource_manager.getCursor(9));

  LoadScreen::run(&window, &config, &resource_manager);

  // Initialize scenario manager and load data
  g_scenarioManager = new ScenarioManager(&resource_manager);
  g_scenarioManager->loadScenarios();
  g_scenarioManager->loadFreeformMaps();

  IniReader * lyt_reader = resource_manager.getIniReader("ui/startup.lyt");
  UiLayout * layout = new UiLayout(lyt_reader, &resource_manager);
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
        
      case UiAction::STARTUP_ZOO_ITEMS:
        delete layout;
        lyt_reader = resource_manager.getIniReader("ui/gameopts.lyt");
        layout = new UiLayout(lyt_reader, &resource_manager);
        g_currentState = LayoutState::OPTIONS;
        break;
        
      case UiAction::CREDITS_EXIT:
      case UiAction::SCENARIO_BACK_TO_MAIN_MENU:
        delete layout;
        lyt_reader = resource_manager.getIniReader("ui/startup.lyt");
        layout = new UiLayout(lyt_reader, &resource_manager);
        g_currentState = LayoutState::MAIN_MENU;
        // Reset cached pointers
        g_scenarioListBox = nullptr;
        g_freeformListBox = nullptr;
        g_scenarioDescText = nullptr;
        g_freeformDescText = nullptr;
        break;
        
      // Handle list selection changes
      case UiAction::SCENARIO_LIST_SELECTION:
        updateScenarioDetails(layout, g_scenarioManager);
        break;
        
      case UiAction::FREEFORM_LIST_SELECTION:
        updateFreeformDetails(layout, g_scenarioManager);
        break;
        
      default:
        if (action != UiAction::NONE) {
          SDL_Log("Got unassigned action %i", (int) action);
        }
        break;
    }
    layout->draw(window.renderer, NULL);

    window.present();
  }

  // Cleanup
  delete g_scenarioManager;
  delete layout;

  return 0;
}
'''
    
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(new_main)
    
    print("    -> Updated main.cpp with selection handling and description updates")
    return True
