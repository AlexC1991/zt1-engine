#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <string>

#include "ArtScaler.hpp"
#include "GpuFsr.hpp"
#include "ItemCatalog.hpp"
#include "Config.hpp"
#include "RenderSettings.hpp"
#include "IniReader.hpp"
#include "Input.hpp"
#include "InputManager.hpp"
#include "LoadScreen.hpp"
#include "ResourceManager.hpp"
#include "ScenarioManager.hpp"

#include "Utils.hpp"
#include "Window.hpp"
#include "World.hpp"

#include "ui/UiImage.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiGameScreen.hpp"
#include "ui/UiMiniMap.hpp"
#include "ui/UiStatusImage.hpp"
#include "ui/UiText.hpp"

// Memory tracking system
#include "MemoryDumpLog.hpp"
#include "MemoryTracker.hpp"

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
  OPTIONS,
  GAME_LOOP
};
LayoutState g_currentState = LayoutState::MAIN_MENU;

UiListBox *g_scenarioListBox = nullptr;
UiListBox *g_freeformListBox = nullptr;
UiText *g_scenarioDescText = nullptr;
UiText *g_freeformDescText = nullptr;
UiImage *g_scenarioMap = nullptr;
UiImage *g_freeformMap = nullptr;
World *g_world = nullptr;

// [PATCH] Starting Cash UI
UiText *g_startingCashText = nullptr;
UiText *g_difficultyText = nullptr;
// Starting cash spinner, same rules as the original (its zoo.ini [UI]
// MSStartingCash / MSCashIncrement / MSMinCash / MSMaxCash)
const int CASH_START = 75000;
const int CASH_STEP = 5000;
const int CASH_MIN = 10000;
const int CASH_MAX = 500000;
int g_currentStartingCash = CASH_START;

static void showStartingCash() {
  if (!g_startingCashText)
    return;
  // "$75,000"
  std::string digits = std::to_string(g_currentStartingCash);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(i, ",");
  g_startingCashText->setText("$" + digits);
}

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
                               const FreeformMap &map) {
  if (img == nullptr || rm == nullptr)
    return;

  // The preview is the "N" frame next to the .scn's icon=, with the icon's
  // palette (e.g. freeform/smbeach/N + freeform/smbeach/smbeach.pal).
  // Fall back to the .scn's own name for maps without an icon entry.
  std::string icon = map.iconPath;
  if (icon.empty()) {
    std::string stem = getFileStem(map.path);
    icon = getFolderFromPath(map.path) + "/" + stem + "/" + stem;
  }
  std::string raw = getFolderFromPath(icon) + "/N";
  std::string pal = icon + ".pal";

  if (rm->hasResource(raw) && rm->hasResource(pal)) {
    img->setZt1Image(raw, pal);
  } else {
    SDL_Log("[Freeform] No preview for %s (%s)", map.path.c_str(), raw.c_str());
  }
}

// ============================================================================
// MENU SCALING
// ============================================================================
// The original's menu layouts (*.lyt) are authored for 800x600. Like
// OpenRCT2's UI scale, they are scaled to fit the window (or to zoo.ini
// [user] uiScale) and centred. Text is rendered by the font at the final
// size so it stays sharp; art is filtered smoothly unless the scale is a
// whole number. Mouse positions are mapped back into layout space.
// ============================================================================

static float g_uiScaleSetting = 0.0f; // 0 = fit the window
static bool g_widescreen = false;     // in-game screen fills the window
static const int UI_LAYOUT_W = 800;
static const int UI_LAYOUT_H = 600;

struct UiTransform {
  float scale = 1.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
};

static UiTransform getUiTransform(SDL_Renderer *renderer) {
  int w = UI_LAYOUT_W, h = UI_LAYOUT_H;
  SDL_GetRendererOutputSize(renderer, &w, &h);
  UiTransform t;
  t.scale = g_uiScaleSetting > 0.0f
                ? g_uiScaleSetting
                : std::min(w / float(UI_LAYOUT_W), h / float(UI_LAYOUT_H));
  t.scale = std::max(0.5f, t.scale);
  // The viewport is set in layout units, so snap the offset to them; mouse
  // mapping uses the same snapped offset
  t.offsetX = std::round((w - UI_LAYOUT_W * t.scale) * 0.5f / t.scale) * t.scale;
  t.offsetY = std::round((h - UI_LAYOUT_H * t.scale) * 0.5f / t.scale) * t.scale;
  return t;
}

static void mapInputsToLayout(std::vector<Input> &inputs, const UiTransform &t) {
  for (Input &in : inputs) {
    if (in.type != InputType::POSITIONED)
      continue;
    in.x = static_cast<int>((in.x - t.offsetX) / t.scale);
    in.y = static_cast<int>((in.y - t.offsetY) / t.scale);
    in.position.x = in.x;
    in.position.y = in.y;
  }
}

// Draws a UI at the UI scale. With GPU FSR the art goes into a 1:1 layer
// that FSR upscales to the window, then the text is drawn at the window's
// resolution on top; otherwise it is all drawn scaled in one go.
// draw(renderer) draws the UI in layout units.
template <typename DrawFn>
static void drawUiScaled(SDL_Renderer *renderer, ResourceManager *rm,
                         const UiTransform &t, DrawFn draw) {
  rm->setTextScale(t.scale);
  int outW = UI_LAYOUT_W, outH = UI_LAYOUT_H;
  SDL_GetRendererOutputSize(renderer, &outW, &outH);

  if (GpuFsr::upscales(t.scale)) {
    GpuFsr::beginLayer(renderer, GpuFsr::Layer::Ui,
                       static_cast<int>(std::ceil(outW / t.scale)),
                       static_cast<int>(std::ceil(outH / t.scale)),
                       SDL_Color{0, 0, 0, 0});
    RenderSettings::artScaleMode = SDL_ScaleModeNearest;
    RenderSettings::uiPass = RenderSettings::UiPass::Art;
    draw(renderer);
    GpuFsr::endLayer(renderer, GpuFsr::Layer::Ui, outW / t.scale,
                     outH / t.scale, true);
    RenderSettings::uiPass = RenderSettings::UiPass::Text;
  } else {
    bool wholeScale = std::fabs(t.scale - std::round(t.scale)) < 0.01f;
    RenderSettings::artScaleMode =
        wholeScale ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
  }
  SDL_RenderSetScale(renderer, t.scale, t.scale);
  draw(renderer);
  SDL_RenderSetScale(renderer, 1.0f, 1.0f);
  RenderSettings::uiPass = RenderSettings::UiPass::All;
  RenderSettings::artScaleMode = SDL_ScaleModeNearest;
}

static void drawLayoutCentered(SDL_Renderer *renderer, UiLayout *layout,
                               ResourceManager *rm) {
  UiTransform t = getUiTransform(renderer);
  drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
    // Viewport is given in layout units
    SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                         static_cast<int>(std::lround(t.offsetY / t.scale)),
                         UI_LAYOUT_W, UI_LAYOUT_H};
    SDL_RenderSetViewport(r, &viewport);
    SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
    layout->draw(r, &layoutRect);
    SDL_RenderSetViewport(r, nullptr);
  });
}

// ============================================================================
// IN-GAME HUD (ui/main.lyt)
// ============================================================================
// The original's in-game screen: the frame pieces, left toolbar and bottom
// bar (pause, date, money, ratings, minimap, zoom/rotate). It covers the
// whole window at the menus' UI scale; the map is drawn underneath.
// ============================================================================

static UiGameScreen *g_hud = nullptr;

static std::string formatMoney(int amount) {
  std::string digits = std::to_string(amount);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
    digits.insert(i, ",");
  return "$" + digits;
}

// Zoom buttons: both are shown, and the one that cannot zoom any further is
// greyed out (at the default zoom the original greys out zoom in)
static const float HUD_ZOOM_MAX = 1.0f;
static const float HUD_ZOOM_MIN = 0.5f;

static void updateZoomButtons(World *world) {
  if (!g_hud || !world)
    return;
  float zoom = world->getCamera().zoom;
  if (UiElement *in = g_hud->getElementById(1007)) {
    in->setHidden(false);
    in->setDisabled(zoom >= HUD_ZOOM_MAX - 0.01f);
  }
  if (UiElement *out = g_hud->getElementById(1023)) {
    out->setHidden(false);
    out->setDisabled(zoom <= HUD_ZOOM_MIN + 0.01f);
  }
}

static void destroyHud() {
  delete g_hud;
  g_hud = nullptr;
}

static void createHud(ResourceManager *rm, World *world, int startingCash) {
  destroyHud();
  // The HUD and its panels, as the original's in-game screen
  g_hud = new UiGameScreen(rm, "ui/main.lyt", "ui/gamescrn.lyt");

  if (UiText *date = dynamic_cast<UiText *>(g_hud->getElementById(1030)))
    date->setText("Jan, Year 1");
  if (UiText *money = dynamic_cast<UiText *>(g_hud->getElementById(1016))) {
    money->setText(formatMoney(startingCash));
    // The original shows money in green while it is positive
    money->setTextColor(startingCash >= 0 ? SDL_Color{83, 219, 83, 255}
                                          : SDL_Color{255, 60, 60, 255});
  }

  // Undo starts visible but greyed out (nothing to undo yet); the Scenario
  // button is greyed out in freeform games
  if (UiElement *undo = g_hud->getElementById(1075)) {
    undo->setHidden(false);
    undo->setDisabled(true);
  }
  if (UiElement *scenario = g_hud->getElementById(4107))
    scenario->setDisabled(true);
  updateZoomButtons(world);

  // Ratings: zoo, animal, guest. Placeholders until the simulation computes
  // them: the zoo rating a new freeform zoo shows in the original, and no
  // animals or guests yet.
  if (auto *zoo = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1015)))
    zoo->setValue(36); // a new zoo on Death Mountain in the original
  if (auto *animals = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1011)))
    animals->setValue(0);
  if (auto *guests = dynamic_cast<UiStatusImage *>(g_hud->getElementById(1013)))
    guests->setValue(0);

  if (auto *mini = dynamic_cast<UiMiniMap *>(g_hud->getElementById(1026))) {
    mini->setCallbacks(
        [world](SDL_Renderer *r, const SDL_Rect &box) { world->drawMiniMap(r, box); },
        [world](float fx, float fy) { world->miniMapClick(fx, fy); });
  }
}

// The HUD's layout covers the window in layout units (window / UI scale)
static SDL_Rect hudLayoutRect(SDL_Renderer *renderer, const UiTransform &t) {
  int w = UI_LAYOUT_W, h = UI_LAYOUT_H;
  SDL_GetRendererOutputSize(renderer, &w, &h);
  // Rounded, not truncated: the right-hand and centred pieces then sit
  // within half a layout pixel of the window's real right edge and centre
  return {0, 0, static_cast<int>(std::lround(w / t.scale)),
          static_cast<int>(std::lround(h / t.scale))};
}

// The in-game screen's area on the window: like the original (and the
// menus), its 800x600 screen scaled to fit and centred; with the
// widescreen option, the whole window
static SDL_Rect gameViewRect(SDL_Renderer *renderer) {
  int w = UI_LAYOUT_W, h = UI_LAYOUT_H;
  SDL_GetRendererOutputSize(renderer, &w, &h);
  if (g_widescreen)
    return {0, 0, w, h};
  UiTransform t = getUiTransform(renderer);
  return {static_cast<int>(std::lround(t.offsetX)),
          static_cast<int>(std::lround(t.offsetY)),
          static_cast<int>(std::lround(UI_LAYOUT_W * t.scale)),
          static_cast<int>(std::lround(UI_LAYOUT_H * t.scale))};
}

static void drawHud(SDL_Renderer *renderer, ResourceManager *rm) {
  if (!g_hud)
    return;
  UiTransform t = getUiTransform(renderer);
  if (g_widescreen) {
    SDL_Rect rect = hudLayoutRect(renderer, t);
    drawUiScaled(renderer, rm, t,
                 [&](SDL_Renderer *r) { g_hud->draw(r, &rect); });
    return;
  }
  // Centred at 800x600, exactly like the menus
  drawUiScaled(renderer, rm, t, [&](SDL_Renderer *r) {
    SDL_Rect viewport = {static_cast<int>(std::lround(t.offsetX / t.scale)),
                         static_cast<int>(std::lround(t.offsetY / t.scale)),
                         UI_LAYOUT_W, UI_LAYOUT_H};
    SDL_RenderSetViewport(r, &viewport);
    SDL_Rect layoutRect = {0, 0, UI_LAYOUT_W, UI_LAYOUT_H};
    g_hud->draw(r, &layoutRect);
    SDL_RenderSetViewport(r, nullptr);
  });
}

static UiAction hudInputs(SDL_Renderer *renderer, std::vector<Input> inputs) {
  if (!g_hud)
    return UiAction::NONE;
  UiTransform t = getUiTransform(renderer);
  if (g_widescreen)
    t.offsetX = t.offsetY = 0; // the HUD covers the whole window
  mapInputsToLayout(inputs, t);
  return g_hud->handleInputs(inputs);
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
    setFreeformPreview(g_freeformMap, resourceManager, *map);
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

      // Names already carry the size, e.g. "Small Beach (Small)"
      for (const auto &map : scenarioManager->getFreeformMaps())
        g_freeformListBox->addItem(map.name, map.path);

      // Like the original, start with the first map selected
      if (!scenarioManager->getFreeformMaps().empty())
        g_freeformListBox->setSelectedIndex(0);
    }
  }

  // Initialize Starting Cash text (ID 11510)
  element = layout->getElementById(11510);
  if (element) {
    g_startingCashText = dynamic_cast<UiText *>(element);
    showStartingCash();
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

// ============================================================================
// MAP RENDER CHECK
// (--render-maps <outDir> [elevationScale] [tileWidth] [unused] [stem,stem]
//  [extra rotation on top of the map's start view] [shadingMode: 0 lit,
//  1 unlit, 2 normals])
// ============================================================================
// Headless verification: for every freeform map, saves the game's own
// map-select preview next to our WorldRenderer output, so the two can be
// compared. Uses the same World::loadFreeform path as the Play button.
// ============================================================================

// Optional fixed tile width for --render-maps (0 = fit whole map)
static int g_renderCheckTileWidth = 0;
// Optional comma-separated map stems to render (empty = all)
static std::string g_renderCheckOnly;
// Extra view rotation on top of each map's start rotation
static int g_renderCheckExtraRotation = 0;

static bool saveTargetToBmp(SDL_Renderer *renderer, SDL_Texture *target,
                            const std::string &path) {
  int w = 0, h = 0;
  SDL_QueryTexture(target, nullptr, nullptr, &w, &h);
  SDL_SetRenderTarget(renderer, target);
  SDL_Surface *surface =
      SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  bool ok = surface &&
            SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                 surface->pixels, surface->pitch) == 0 &&
            SDL_SaveBMP(surface, path.c_str()) == 0;
  if (surface)
    SDL_FreeSurface(surface);
  SDL_SetRenderTarget(renderer, nullptr);
  if (!ok)
    SDL_Log("[RenderCheck] Failed to save %s: %s", path.c_str(), SDL_GetError());
  return ok;
}

static void renderWorldToBmp(SDL_Renderer *renderer, World *world,
                             bool debugColors, bool elevation,
                             const std::string &path) {
  WorldMap &map = world->getMap();
  WorldRenderer &wr = world->getRenderer();
  int span = map.getWidth() + map.getHeight();

  // Fit the whole map into ~1200px wide, unless a tile width was given
  int tileW = g_renderCheckTileWidth > 0
                  ? g_renderCheckTileWidth
                  : std::max(4, (2400 / span) & ~1);
  int tileH = tileW / 2;
  wr.setTileSize(tileW, tileH);
  if (wr.isTerrainDebugEnabled() != debugColors)
    wr.toggleTerrainDebug();
  if (wr.isElevationEnabled() != elevation)
    wr.toggleElevation();

  // Room for the tallest point and the floor slab under the lowest one
  float unitPx = wr.getHeightUnitPixels();
  int minH = map.getMinHeight() - 1, maxH = map.getMaxHeight();
  int headroom = static_cast<int>((maxH - minH) * unitPx);
  int outW = span * tileW / 2 + 16;
  int outH = span * tileH / 2 + 16 + headroom;

  Camera &cam = wr.getCamera();
  cam.zoom = 1.0f;
  cam.screenCenterX = outW / 2;
  cam.screenCenterY = outH / 2;
  // View grid is height x width in rotations 0 and 2
  bool swapped = wr.getViewRotation() % 2 == 0;
  int viewU = swapped ? map.getHeight() : map.getWidth();
  int viewV = swapped ? map.getWidth() : map.getHeight();
  cam.x = -(viewU - viewV) * tileW / 4;
  cam.y = -span * tileH / 4 + static_cast<int>((maxH + minH) * 0.5f * unitPx);

  SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET, outW, outH);
  SDL_SetRenderTarget(renderer, target);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  wr.renderTerrain(renderer, map, SpriteDatabase::get());
  saveTargetToBmp(renderer, target, path);
  SDL_DestroyTexture(target);
}

static int runMapRenderCheck(SDL_Renderer *renderer, ResourceManager *rm,
                             ScenarioManager *sm, World *world,
                             const std::string &outDir) {
  int count = 0;
  for (int i = 0;; i++) {
    const FreeformMap *map = sm->getFreeformMap(i);
    if (!map)
      break;

    std::string stem = getFileStem(map->path);
    if (!g_renderCheckOnly.empty() &&
        ("," + g_renderCheckOnly + ",").find("," + stem + ",") == std::string::npos)
      continue;
    std::string prefix = outDir + "/" + stem;
    SDL_Log("[RenderCheck] %s (%s)", map->name.c_str(), map->path.c_str());

    // Game's own preview, as shown on the map-select screen
    std::string baseFolder = getFolderFromPath(map->path);
    std::string raw = baseFolder + "/" + stem + "/N";
    std::string pal = baseFolder + "/" + stem + "/" + stem + ".pal";
    if (rm->hasResource(raw) && rm->hasResource(pal)) {
      SDL_Texture *preview = rm->getZt1Texture(renderer, raw, pal);
      if (preview) {
        int w = 0, h = 0;
        SDL_QueryTexture(preview, nullptr, nullptr, &w, &h);
        SDL_Texture *target = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
        SDL_SetRenderTarget(renderer, target);
        SDL_RenderCopy(renderer, preview, nullptr, nullptr);
        saveTargetToBmp(renderer, target, prefix + "_preview.bmp");
        SDL_DestroyTexture(target);
      }
    }

    bool loaded = world->loadFreeform(map->path);
    if (loaded)
      world->getRenderer().rotateView(g_renderCheckExtraRotation);
    if (!loaded) {
      SDL_Log("[RenderCheck]   map failed to load");
      continue;
    }
    // With a fixed tile width (comparison against the original), only the
    // textured view is needed
    if (g_renderCheckTileWidth == 0) {
      renderWorldToBmp(renderer, world, true, false, prefix + "_flat.bmp");
      renderWorldToBmp(renderer, world, true, true, prefix + "_debug.bmp");
    }
    renderWorldToBmp(renderer, world, false, true, prefix + "_textured.bmp");
    count++;
  }
  SDL_Log("[RenderCheck] Rendered %d maps to %s", count, outDir.c_str());
  return count > 0 ? 0 : 1;
}

// --panel-shots <outDir> [mapStem]: start a freeform game (default Death
// Mountain), open every toolbar panel and buy tab in turn and save each
// frame as <outDir>/<name>.bmp, then quit. For comparing with the original
// without driving the mouse.
static int runPanelShots(SDL_Renderer *renderer, ResourceManager *rm,
                         ScenarioManager *sm, World *world,
                         const std::string &outDir, const std::string &stem) {
  const FreeformMap *chosen = nullptr;
  for (int i = 0; const FreeformMap *map = sm->getFreeformMap(i); i++)
    if (getFileStem(map->path) == stem)
      chosen = map;
  if (!chosen || !world->loadFreeform(chosen->path)) {
    SDL_Log("[PanelShots] map %s not found", stem.c_str());
    return 1;
  }
  createHud(rm, world, CASH_START);
  if (!g_hud)
    return 1;
  std::filesystem::create_directories(outDir);

  struct Shot {
    int panel;
    const char *tab; // the buy tab's category ("" = none)
    const char *name;
  };
  const Shot shots[] = {
      {0, "", "none"},           {3, "animals", "animals"},
      {3, "shelters", "shelters"}, {3, "toys", "toys"},
      {3, "showtoys", "showtoys"}, {4, "structures", "structures"},
      {4, "scenery", "scenery"},   {8, "fence", "fence"},
      {8, "paths", "paths"},       {8, "foliage", "foliage"},
      {8, "rocks", "rocks"},       {10, "", "staff"},
      {8, "#3362", "terrain"},     {8, "#3361", "height"},
      {15, "#4008", "research"},   {15, "#4009", "research2"},
      {15, "#4010", "conservation"}, {5, "", "gameopts"}};
  for (const Shot &shot : shots) {
    // "#<id>": a tab picked by its button (the terraform tabs)
    if (shot.tab[0] == '#') {
      g_hud->showPanel(shot.panel, "");
      g_hud->showTab(shot.panel, std::atoi(shot.tab + 1));
    } else {
      g_hud->showPanel(shot.panel, shot.tab);
    }
    for (int frame = 0; frame < 3; frame++) { // let art load and settle
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      world->setView(gameViewRect(renderer),
                     g_widescreen ? 1.0f : getUiTransform(renderer).scale);
      world->draw(renderer);
      drawHud(renderer, rm);
      if (frame < 2)
        SDL_RenderPresent(renderer);
    }
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_Surface *s =
        SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                  s->pixels, s->pitch) == 0)
      SDL_SaveBMP(s, (outDir + "/" + shot.name + ".bmp").c_str());
    if (s)
      SDL_FreeSurface(s);
    SDL_RenderPresent(renderer);
    SDL_Log("[PanelShots] %s", shot.name);
  }
  return 0;
}

int main(int argc, char *argv[]) {
  SDL_SetMainReady();

  // Initialize memory tracking and crash dump system
  // ZT_MEMORY_DUMP_INIT();
  // Auto-dump every 60 seconds (helps catch state before crash)
  // MemoryDumpLog::get().setAutoDumpInterval(60);
  SDL_Log("Memory tracking initialized");

  Config config;
  g_uiScaleSetting = config.getUiScale();
  g_widescreen = config.getWidescreen();
  ResourceManager resource_manager(&config);

  // FSR runs on the GPU, which needs SDL's OpenGL renderer (shaders). If
  // that is not available the art is upscaled once when it loads instead.
  ArtScaler::Mode artMode = ArtScaler::parseMode(config.getArtUpscale());
  if (artMode == ArtScaler::Mode::Fsr)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
  Window window("ZT1-Engine", config.getScreenWidth(), config.getScreenHeight(),
                60.0f);
  bool gpuFsr = artMode == ArtScaler::Mode::Fsr &&
                GpuFsr::init(window.renderer, config.getArtSharpness());
  ArtScaler::configure(gpuFsr ? ArtScaler::Mode::Off : artMode,
                       config.getArtUpscaleFactor(), config.getArtSharpness());
  window.set_cursor(resource_manager.getCursor(9));

  LoadScreen::run(&window, &config, &resource_manager);

  // ZT_DUMP_CATALOG=1: log every buy tab's list and quit (for comparing
  // with the original)
  if (std::getenv("ZT_DUMP_CATALOG")) {
    ItemCatalog::get().load(&resource_manager);
    return 0;
  }

  g_scenarioManager = new ScenarioManager(&resource_manager);
  g_scenarioManager->loadScenarios();
  g_scenarioManager->loadFreeformMaps();

  g_world = new World(&resource_manager);

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

  for (int i = 1; i + 1 < argc; i++) {
    if (std::string(argv[i]) == "--panel-shots")
      return runPanelShots(window.renderer, &resource_manager,
                           g_scenarioManager, g_world, argv[i + 1],
                           i + 2 < argc ? argv[i + 2] : "deathmtn");
    if (std::string(argv[i]) == "--render-maps") {
      if (i + 2 < argc)
        g_world->getRenderer().setElevationScale(std::atoi(argv[i + 2]));
      if (i + 3 < argc)
        g_renderCheckTileWidth = std::atoi(argv[i + 3]) & ~1;
      if (i + 5 < argc && std::string(argv[i + 5]) != "all")
        g_renderCheckOnly = argv[i + 5];
      if (i + 6 < argc)
        g_renderCheckExtraRotation = std::atoi(argv[i + 6]);
      if (i + 7 < argc)
        g_world->getRenderer().setShadingMode(std::atoi(argv[i + 7]));
      return runMapRenderCheck(window.renderer, &resource_manager,
                               g_scenarioManager, g_world, argv[i + 1]);
    }
  }

  InputManager input_manager;
  std::vector<Input> inputs;

  int running = 1;
  UiAction action = UiAction::NONE;

  // Delta time tracking
  Uint32 lastFrameTime = SDL_GetTicks();
  float deltaTime = 0.0f;

  while (running > 0) {
    // Calculate delta time
    Uint32 currentTime = SDL_GetTicks();
    deltaTime = (currentTime - lastFrameTime) / 1000.0f; // Convert to seconds
    lastFrameTime = currentTime;

    window.clear();
    inputs = input_manager.getInputs();

    for (Input input : inputs) {
      if (input.event == InputEvent::QUIT) {
        running = 0;
      }
      // Handle ESC key to return to freeform menu from game loop
      if (input.event == InputEvent::KEY_ESCAPE && g_currentState == LayoutState::GAME_LOOP) {
        destroyHud();
        // Return to freeform selection menu
        lyt_reader = resource_manager.getIniReader(
            getLayoutPath(&resource_manager, "mapselec.lyt"));
        layout = new UiLayout(lyt_reader, &resource_manager);
        g_currentState = LayoutState::FREEFORM_SELECT;
        populateFreeformList(layout, g_scenarioManager);
        updateFreeformDetails(layout, g_scenarioManager, &resource_manager);
      }
    }

    if (layout) {
      std::vector<Input> layoutInputs = inputs;
      mapInputsToLayout(layoutInputs, getUiTransform(window.renderer));
      action = layout->handleInputs(layoutInputs);
    } else if (g_currentState == LayoutState::GAME_LOOP && g_hud) {
      action = hudInputs(window.renderer, inputs);
    } else {
      // No layout means we are likely in game loop, so no UI actions
      // Define a safe no-op action if explicit NONE isn't available,
      // relying on default case of switch.
      // Assuming cast to UiAction is possible or using a known safe value.
      // Since I don't know the Enum for NONE, I'll just skip the switch logic
      // by hacking the flow or I'll just use the last value? No, that's bad.
      // Let's assume 9999 is safe for default case.
      action = (UiAction)9999;
    }

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
      updateFreeformDetails(layout, g_scenarioManager, &resource_manager);
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
      g_currentStartingCash =
          std::min(CASH_MAX, g_currentStartingCash + CASH_STEP);
      showStartingCash();
      break;

    case UiAction::CASH_SPINNER_DOWN:
      g_currentStartingCash =
          std::max(CASH_MIN, g_currentStartingCash - CASH_STEP);
      showStartingCash();
      break;

    case UiAction::PLAY_SCENARIO_START:
      if (g_scenarioListBox && g_scenarioListBox->getSelectedIndex() >= 0) {
        int idx = g_scenarioListBox->getSelectedIndex();
        const ScenarioInfo *info = g_scenarioManager->getScenario(idx);
        if (info && !info->isLocked) {
          SDL_Log("Starting Scenario: %s", info->name.c_str());
          g_world->loadScenario(info->scenarioPath);
          createHud(&resource_manager, g_world, g_currentStartingCash);

          delete layout;
          layout = nullptr; // the in-game HUD replaces the menus
          g_currentState = LayoutState::GAME_LOOP;
        }
      }
      break;

    case UiAction::PLAY_FREEFORM_START:
      if (g_freeformListBox && g_freeformListBox->getSelectedIndex() >= 0) {
        int idx = g_freeformListBox->getSelectedIndex();
        const FreeformMap *map = g_scenarioManager->getFreeformMap(idx);
        if (map) {
          SDL_Log("Starting Freeform: %s", map->name.c_str());
          g_world->loadFreeform(map->path);
          createHud(&resource_manager, g_world, g_currentStartingCash);

          delete layout;
          layout = nullptr;
          g_currentState = LayoutState::GAME_LOOP;
        }
      }
      break;

    // In-game HUD buttons
    case UiAction::HUD_ZOOM_IN:
      g_world->zoomStep(+1);
      updateZoomButtons(g_world);
      break;
    case UiAction::HUD_ZOOM_OUT:
      g_world->zoomStep(-1);
      updateZoomButtons(g_world);
      break;
    case UiAction::HUD_ROTATE_CW:
      g_world->rotateView(+1);
      break;
    case UiAction::HUD_ROTATE_CCW:
      g_world->rotateView(-1);
      break;
    case UiAction::HUD_PAUSE:
    case UiAction::HUD_PLAY:
      if (g_hud) {
        bool pause = action == UiAction::HUD_PAUSE;
        g_world->setPaused(pause);
        // Pause and Play share a spot; show the one that undoes the state
        if (UiElement *p = g_hud->getElementById(1071))
          p->setHidden(pause);
        if (UiElement *p = g_hud->getElementById(1072))
          p->setHidden(!pause);
      }
      break;

    default:
      break;
    }

    // START OF LOOP LOGGING
    if (g_currentState == LayoutState::GAME_LOOP) {
      // SDL_Log("MainLoop: Loop Heartbeat");
    }

    // UPDATE LOGGING
    if (g_currentState == LayoutState::GAME_LOOP) {
      // SDL_Log("MainLoop: Pre-Update");
      const Uint8 *state = SDL_GetKeyboardState(nullptr);
      g_world->update(state, deltaTime);
      // SDL_Log("MainLoop: Post-Update");

      // SDL_Log("MainLoop: Pre-Draw");
      g_world->setView(gameViewRect(window.renderer),
                       g_widescreen ? 1.0f
                                    : getUiTransform(window.renderer).scale);
      g_world->draw(window.renderer);
      updateZoomButtons(g_world); // the mouse wheel zooms too
      drawHud(window.renderer, &resource_manager);
      // SDL_Log("MainLoop: Post-Draw");
    } else if (layout) {
      drawLayoutCentered(window.renderer, layout, &resource_manager);
    }

    window.present();

    // Check if it's time for periodic memory dump
    // ZT_MEMORY_DUMP_CHECK();
  }

  // Write final memory report before cleanup
  SDL_Log("Writing final memory report...");
  ZT_MEMORY_REPORT();

  delete g_scenarioManager;
  delete g_world;
  delete layout;

  // Shutdown memory tracking (writes exit dump to logs/)
  ZT_MEMORY_DUMP_SHUTDOWN();

  return 0;
}